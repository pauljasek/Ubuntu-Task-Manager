#include <gtk/gtk.h>
#include <gmodule.h>

#include <sys/syscall.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/sched.h>

#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <signal.h>
#include <math.h>


#define HIJACKED_SYSCALL __NR_tuxcall

#define MAX_PROCESSES 1000

struct process {
  pid_t pid;
  long long previous_time;
  double cpu_usage;
  double cum_cpu_usage;
  long long cum_memory_usage;
  long long memory_usage;
  char memory_usage_string[100];
  char name[1000];
  GtkTreeIter iter;
};

int pid_list[MAX_PROCESSES];
GNode *process_tree;

double get_total_cpu_jiffies()
{
  char dummy[10];
  long long total_time = 0, temp;
  FILE *fp = fopen("/proc/stat", "r");
  if (!fp) return 3000000;

  if (fscanf(fp, "%s", dummy) == EOF) {
      fclose(fp);
      return 3000000;
  }
  for (int i = 0; i < 10; i++) {
    if (fscanf(fp, "%lld", &temp) == EOF) {
      fclose(fp);
      return 3000000;
    }
    total_time += temp;
  }
  fclose(fp);

  return total_time;
}

long long get_process_cpu_jiffies(int pid) {
  char filename[200];
  char items[52][100];
  long long utime, stime, cutime, cstime, starttime, total_time;

  sprintf(filename, "/proc/%d/stat", pid);

  FILE *fp = fopen(filename, "r");
  if (!fp) return 1;
  
  for (int i = 0; i < 52; i++)
  {
    if (fscanf(fp, "%s", items[i]) == EOF) {
      fclose(fp);
      return 0;
    };
  }

  fclose(fp);

  utime = strtoll(items[13],NULL,10);
  stime = strtoll(items[14],NULL,10);
  cutime = strtoll(items[15],NULL,10);
  cstime = strtoll(items[16],NULL,10);

  return utime + cutime + stime + cstime;
}

long long get_process_memory_usage(int pid) {
  char filename[200];
  sprintf(filename, "/proc/%d/status", pid);
  FILE *fp = fopen(filename, "r");
  if (!fp) return 1;
  char *line, *memory_usage_str;
  size_t len = 1024;

  line = malloc(len);
  memory_usage_str = malloc(len);

	
  while (1)
  {
	if (getline(&line, &len, fp) == -1)
	{
		free(line);
		fclose(fp);
		return 1;
	}
	if (!strncmp(line, "VmSize:", 7))
	{
		strcpy(memory_usage_str, strdup(&line[8]));
		strtok(memory_usage_str, " kB");
		break;
	}
  }

  long long memory_usage = atoll(memory_usage_str) * 1024;

  free(line);
  free(memory_usage_str);
  fclose(fp);

  return memory_usage;
}

char* MEM_SIZES[] = {"Bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
void format_memory(long long bytes, char *string) {
  int i = log2(bytes)/log2(1024);
  float decimal = 1.0 * bytes / pow(1024, i);
  sprintf(string, "%.1f %s", decimal, MEM_SIZES[i]);
}

int get_process_name(int pid, char *name) {
  char filename[100], path[1000] = {'\0'};
  sprintf(filename, "/proc/%d/cmdline", pid);
  FILE *fp = fopen(filename, "r");
  if (!fp) return 1;
  if (fscanf(fp, "%s", path) == EOF) {
    fclose(fp);
    return 1;
  }
  if (strlen(path) > 0) {
    strcpy(name, basename(path));
  }
  fclose(fp);
  return 0;
}

long long previous_total_time = 0;
long long previous_time[MAX_PROCESSES];
GtkTreeStore *store;

enum
{
   PID_COLUMN,
   PROCESS_NAME_COLUMN,
   CPU_USAGE_COLUMN,
   MEMORY_USAGE_COLUMN,
   MEMORY_USAGE_NUMBER_COLUMN,
   N_COLUMNS
};

GNode * build_process_tree(pid_t parent_pid, GtkTreeIter *parent_iter) {
  struct process *process_data;
  
  process_data = calloc(1, sizeof(struct process));
  process_data->pid = parent_pid;
  gtk_tree_store_append (store, &(process_data->iter), parent_iter);  /* Acquire an iterator */
  gtk_tree_store_set (store, &(process_data->iter),
		      PID_COLUMN, process_data->pid,
                      PROCESS_NAME_COLUMN, process_data->name,
                      CPU_USAGE_COLUMN, process_data->cum_cpu_usage * 100,
                      MEMORY_USAGE_COLUMN, process_data->memory_usage_string,
		      MEMORY_USAGE_NUMBER_COLUMN, process_data->cum_memory_usage,
                      -1);

  GNode *parent = g_node_new (process_data);

  int length = syscall(HIJACKED_SYSCALL, pid_list, parent_pid);
  for (int i = length - 1; i >= 0; i--)
  {
    g_node_insert (parent, 0, build_process_tree(pid_list[i], &(process_data->iter)));
  }

  return parent;
}

void update_process_tree(GNode *tree) {
  struct process *tree_data = (struct process *) tree->data;

  int length = syscall(HIJACKED_SYSCALL, pid_list, ((struct process *) tree->data)->pid);
  int children = g_node_n_children (tree);
  
  for (int c = children - 1, i = length - 1; c >= 0 || i >= 0;)
  {
    //printf("%d/%d children %d/%d new\n", c, children, i, length);
    if (c < 0) {
      //create
      g_node_insert (tree, c + 1, build_process_tree(pid_list[i], &(tree_data->iter)));
      i--;
      continue;
    }
    GNode *child = g_node_nth_child (tree, c);
    struct process *child_data = (struct process *) child->data;
    pid_t child_pid = child_data->pid;
    if (child_pid > pid_list[i] || i < 0) {
      //remove
      update_process_tree(child);
      gtk_tree_store_remove (store, &(child_data->iter));
      g_node_destroy(child);
      c--;
    } else if (child_pid == pid_list[i]) {
      //update
      update_process_tree(child);
      c--;
      i--;      
    } else {
      //create
      g_node_insert (tree, c + 1, build_process_tree(pid_list[i], &(tree_data->iter)));
      i--;
    }
  }
}

gboolean update_cpu_utilization(GNode *node, gpointer total_time_ptr) {
    struct process *data;
    data = (struct process *) node->data;

    long long total_time = * (long long *) total_time_ptr;
    long long time = get_process_cpu_jiffies(data->pid);
    
    double cpu_utilization = 1.0 * (time - data->previous_time)/(total_time - previous_total_time);

    if (data->previous_time == 0)
    {
      cpu_utilization = 0;
    }

    data->previous_time = time;

    if (cpu_utilization == 0) cpu_utilization = 0;
    
    data->cpu_usage = cpu_utilization;

    return 0;
}

gboolean calculate_cum_cpu_utilization(GNode *node, gpointer sum) {
    struct process *data;
    data = (struct process *) node->data;

    double *val = (double *) sum;
    *val += data->cpu_usage;

    return 0;
}

gboolean update_cum_cpu_utilization(GNode *node, gpointer info) {
    struct process *data;
    data = (struct process *) node->data;

    data->cum_cpu_usage = 0;

    g_node_traverse (node,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 calculate_cum_cpu_utilization,
                 &data->cum_cpu_usage);
    
    return 0;
}

gboolean update_memory_utilization(GNode *node, gpointer info) {
  struct process *data;
  data = (struct process *) node->data;
  data->memory_usage = get_process_memory_usage(data->pid);

  return 0;
}

gboolean calculate_cum_memory_utilization(GNode *node, gpointer sum) {
    struct process *data;
    data = (struct process *) node->data;

    long long *val = (long long *) sum;
    *val += data->memory_usage;

    return 0;
}

gboolean update_cum_memory_utilization(GNode *node, gpointer info) {
    struct process *data;
    data = (struct process *) node->data;

    data->cum_memory_usage = 0;

    g_node_traverse (node,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 calculate_cum_memory_utilization,
                 &data->cum_memory_usage);

    format_memory(data->cum_memory_usage, data->memory_usage_string);
    
    return 0;
}

gboolean update_process_name(GNode *node, gpointer info) {
  struct process *data;
  data = (struct process *) node->data;
  get_process_name(data->pid, data->name);

  return 0;
}

gboolean update_gtk_tree(GNode *node, gpointer info) {
  struct process *data;
  data = (struct process *) node->data;


  gtk_tree_store_set (store, &data->iter,
		      PID_COLUMN, data->pid,
                      PROCESS_NAME_COLUMN, data->name,
                      CPU_USAGE_COLUMN, data->cum_cpu_usage * 100,
                      MEMORY_USAGE_COLUMN, data->memory_usage_string,
		      MEMORY_USAGE_NUMBER_COLUMN, data->cum_memory_usage,
                      -1);

  return 0;
}


gboolean update_utilization(GtkWidget * data)
{
  update_process_tree(process_tree);

  long long total_time = get_total_cpu_jiffies();

  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cpu_utilization,
                 &total_time);

  previous_total_time = total_time;

  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cum_cpu_utilization,
                 NULL);


  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_memory_utilization,
                 NULL);

  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cum_memory_utilization,
                 NULL);

  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_process_name,
                 NULL);


  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_gtk_tree,
                 NULL);


  return TRUE;
}

pid_t selected_pid = 0;

/* Selection handler callback */
static void
tree_selection_changed_cb (GtkTreeSelection *selection, gpointer data)
{
        GtkTreeIter iter;
        GtkTreeModel *model;

        if (gtk_tree_selection_get_selected (selection, &model, &iter))
        {
                gtk_tree_model_get (model, &iter, PID_COLUMN, &selected_pid, -1);
        }
}

/* Our usual callback function */
void kill_button_callback( GtkWidget *widget, gpointer   data )
{
  kill(selected_pid, SIGTERM);
}

void toggle_memory_sort(GtkWidget *widget, gpointer data)
{
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store), MEMORY_USAGE_NUMBER_COLUMN, GTK_SORT_DESCENDING);
}

void toggle_cpu_sort(GtkWidget *widget, gpointer data)
{
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store), CPU_USAGE_COLUMN, GTK_SORT_DESCENDING);
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
  GtkWidget *window, *scwin;
  GtkWidget *kill_button;

  store = gtk_tree_store_new (N_COLUMNS,
					    G_TYPE_INT,
                                            G_TYPE_STRING,
                                            G_TYPE_DOUBLE,
                                            G_TYPE_STRING,
					    G_TYPE_ULONG
  );

  GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);
  gtk_tree_sortable_set_sort_column_id (sortable, CPU_USAGE_COLUMN, GTK_SORT_DESCENDING);

  window = gtk_application_window_new (app);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_window_set_title (GTK_WINDOW (window), "Task Manager");
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 800);

  scwin = gtk_scrolled_window_new(NULL, NULL);

  GtkWidget *tree;
  tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));

  GtkCellRenderer *renderer, *progress_renderer;
  GtkTreeViewColumn *name_column, *cpu_usage_column, *memory_usage_column;

  renderer = gtk_cell_renderer_text_new ();
  progress_renderer = gtk_cell_renderer_progress_new();

  name_column = gtk_tree_view_column_new_with_attributes ("Process Name",
                                                     renderer,
                                                     "text", PROCESS_NAME_COLUMN,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), name_column);

  cpu_usage_column = gtk_tree_view_column_new_with_attributes ("CPU Usage",
                                                     progress_renderer,
                                                     "value", CPU_USAGE_COLUMN,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), cpu_usage_column);

  memory_usage_column = gtk_tree_view_column_new_with_attributes ("Memory Usage",
                                                     renderer,
                                                     "text", MEMORY_USAGE_COLUMN,
                                                     NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tree), memory_usage_column);

  gtk_tree_view_set_headers_clickable(tree, TRUE);
  g_signal_connect (G_OBJECT (cpu_usage_column), "clicked", G_CALLBACK (toggle_cpu_sort), NULL);
  g_signal_connect (G_OBJECT (memory_usage_column), "clicked", G_CALLBACK (toggle_memory_sort), NULL);

  gtk_container_add (GTK_CONTAINER (scwin), tree);

  GtkWidget *action_bar = gtk_action_bar_new();
  GtkWidget *button_box = gtk_vbox_new(TRUE, 100);

  kill_button = gtk_button_new_with_label("End Task");

  /* Connect the "clicked" signal of the button to our callback */
  g_signal_connect (G_OBJECT (kill_button), "clicked",
                        G_CALLBACK (kill_button_callback), NULL);

  gtk_widget_set_size_request (kill_button,100,25);

  gtk_box_pack_start((GtkBox *) button_box, kill_button, FALSE, FALSE, 0);
  gtk_action_bar_pack_start ((GtkActionBar *) action_bar, button_box);

  GtkWidget *box = gtk_vbox_new(FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), box);

  gtk_box_pack_start ((GtkBox *) box, scwin, TRUE, TRUE, 0);
  gtk_box_pack_start ((GtkBox *) box, action_bar, FALSE, FALSE, 0);

  /* Setup the selection handler */
  GtkTreeSelection *select;

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
                  G_CALLBACK (tree_selection_changed_cb),
                  NULL);

  gtk_widget_show_all (window);

  process_tree = build_process_tree(1, NULL);
  gtk_tree_view_expand_row (GTK_TREE_VIEW(tree),
			    gtk_tree_model_get_path (store,
                            			     &(((struct process *) process_tree->data)->iter)),
                            FALSE);
  update_utilization(NULL);
  g_timeout_add(2000, (GSourceFunc) update_utilization, NULL);
}

int
main (int    argc,
      char **argv)
{
  GtkApplication *app;
  int status;

  app = gtk_application_new ("student.pauljasek.taskmanager", G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
