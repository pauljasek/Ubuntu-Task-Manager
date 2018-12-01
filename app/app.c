/*
 * Ubuntu Task Manager
 * CSE 2431: Operating Systems
 * Authors: Paul Jasek and Anna Yu
 * A simple graphic task manager for Ubuntu.
 */

#include <gtk/gtk.h>
#include <gmodule.h>
#include <sys/syscall.h>

#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <signal.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "process_util.h"

#define HIJACKED_SYSCALL __NR_tuxcall
#define MAX_PROCESSES 1000

/*
 * Struct for storing process statistics.
 */
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


/*
 * Initialize global variables
 */
int pid_list[MAX_PROCESSES];
GNode *process_tree;
long long previous_total_time = 0;
long long previous_time[MAX_PROCESSES];
GtkTreeStore *store;
pid_t selected_pid = 0;

enum
{
   PID_COLUMN,
   PROCESS_NAME_COLUMN,
   CPU_USAGE_COLUMN,
   MEMORY_USAGE_COLUMN,
   MEMORY_USAGE_NUMBER_COLUMN,
   N_COLUMNS
};

/*
 * Build the process tree starting at a given pid.
 */
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

/*
 * Updates the process tree without deleting and recreating it.
 */
void update_process_tree(GNode *tree) {
  struct process *tree_data = (struct process *) tree->data;

  int length = syscall(HIJACKED_SYSCALL, pid_list, ((struct process *) tree->data)->pid);
  int children = g_node_n_children (tree);
  
  for (int c = children - 1, i = length - 1; c >= 0 || i >= 0;)
  {
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

/*
 * Calculate the cpu usage of a given process in the process tree.
 */
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

/*
 * Add child processes cpu usage to an ancestor process cumulative cpu usage.
 */
gboolean calculate_cum_cpu_utilization(GNode *node, gpointer sum) {
    struct process *data;
    data = (struct process *) node->data;

    double *val = (double *) sum;
    *val += data->cpu_usage;

    return 0;
}

/*
 * Calculate the cumulative cpu usage of a given process in the process tree.
 */
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

/*
 * Update the memory usage of a process in the process tree.
 */
gboolean update_memory_utilization(GNode *node, gpointer info) {
  struct process *data;
  data = (struct process *) node->data;
  data->memory_usage = get_process_memory_usage(data->pid);

  return 0;
}

/*
 * Add child processes memory usage to the cummulative memory usage of an ancestor process.
 */
gboolean calculate_cum_memory_utilization(GNode *node, gpointer sum) {
    struct process *data;
    data = (struct process *) node->data;

    long long *val = (long long *) sum;
    *val += data->memory_usage;

    return 0;
}

/*
 * Calculate the cumulative memory usage for a given process.
 */
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

/*
 * Update the process name.
 */
gboolean update_process_name(GNode *node, gpointer info) {
  struct process *data;
  data = (struct process *) node->data;
  get_process_name(data->pid, data->name);

  return 0;
}

/*
 * Update the GUI with calculated information from the process tree.
 */
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

/*
 * Updates all processes in the process tree and GTK treeview with calculated statistics. 
 */
gboolean update_utilization(GtkWidget * data)
{
  // Update the structure of the process tree.
  update_process_tree(process_tree);

  // Calculate the CPU usage of each process in the tree.
  long long total_time = get_total_cpu_jiffies();
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cpu_utilization,
                 &total_time);
  previous_total_time = total_time;

  // Calculate the cumulative CPU usage of each process in the tree.
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cum_cpu_utilization,
                 NULL);

  // Calculate the memory usage of each process in the tree.
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_memory_utilization,
                 NULL);

  // Calculate the cumulative memory usage of each process in the tree.
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_cum_memory_utilization,
                 NULL);

  // Update the process name of each process in the tree.
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_process_name,
                 NULL);

  // Update the values of the GTK TreeView.
  g_node_traverse (process_tree,
                 G_IN_ORDER,
                 G_TRAVERSE_ALL,
                 -1,
                 update_gtk_tree,
                 NULL);


  return TRUE;
}



/*
 * Handles selection of a process within the GTK TreeView.
 */
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

/*
 * Sends a SIGKILL signal to the selected process.
 */
void kill_button_callback( GtkWidget *widget, gpointer data )
{
  kill(selected_pid, SIGKILL);
}

/*
 * Sends a SIGTERM signal to the selected process.
 */
void terminate_button_callback( GtkWidget *widget, gpointer data )
{
  kill(selected_pid, SIGTERM);
}

/*
 * Toggle sort to sort column by memory amount.
 */
void toggle_memory_sort(GtkWidget *widget, gpointer data)
{
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store), MEMORY_USAGE_NUMBER_COLUMN, GTK_SORT_DESCENDING);
}

/*
 * Toggle sort to sort column by cpu usage.
 */
void toggle_cpu_sort(GtkWidget *widget, gpointer data)
{
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(store), CPU_USAGE_COLUMN, GTK_SORT_DESCENDING);
}


/*
 * Guild the GUI!
 */
static void activate (GtkApplication *app, gpointer user_data)
{
  
  /*
   * Create the window.
   */
  GtkWidget *window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Task Manager");
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 800);

  /*
   * Create the scrolling window.
   */
  GtkWidget *scwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scwin), GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

  /*
   * Create the TreeView.
   */
  store = gtk_tree_store_new (N_COLUMNS,
					    G_TYPE_INT,
                                            G_TYPE_STRING,
                                            G_TYPE_DOUBLE,
                                            G_TYPE_STRING,
					    G_TYPE_ULONG
  );

  GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);
  gtk_tree_sortable_set_sort_column_id (sortable, CPU_USAGE_COLUMN, GTK_SORT_DESCENDING);

  GtkWidget *tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (store));

  /*
   * Set up column rendering
   */
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

  /*
   * Setup column sorting.
   */
  gtk_tree_view_set_headers_clickable((GtkTreeView *)tree, TRUE);
  g_signal_connect (G_OBJECT (cpu_usage_column), "clicked", G_CALLBACK (toggle_cpu_sort), NULL);
  g_signal_connect (G_OBJECT (memory_usage_column), "clicked", G_CALLBACK (toggle_memory_sort), NULL);

  /*
   * Add the tree to the scrolling window.
   */
  gtk_container_add (GTK_CONTAINER (scwin), tree);


  /*
   * Setup the selection handler for the TreeView.
   */
  GtkTreeSelection *select;

  select = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
  gtk_tree_selection_set_mode (select, GTK_SELECTION_SINGLE);
  g_signal_connect (G_OBJECT (select), "changed",
                  G_CALLBACK (tree_selection_changed_cb),
                  NULL);

  /*
   * Create the action bar with kill buttons.
   */
  GtkWidget *action_bar = gtk_action_bar_new();

  GtkWidget *terminate_button = gtk_button_new_with_label("End Task");
  GtkWidget *kill_button = gtk_button_new_with_label("Force Kill Task");


  // place buttons within boxes to size properly.
  GtkWidget *button_boxes[2] = {gtk_box_new(GTK_ORIENTATION_VERTICAL , 100), gtk_box_new(GTK_ORIENTATION_VERTICAL , 100)};
  gtk_box_pack_start((GtkBox *) button_boxes[0], terminate_button, FALSE, FALSE, 0);
  gtk_box_pack_start((GtkBox *) button_boxes[1], kill_button, FALSE, FALSE, 0);

  gtk_widget_set_size_request (kill_button,100,25);
  gtk_widget_set_size_request (terminate_button,100,25);

  // add button boxes to the action bar
  gtk_action_bar_pack_start ((GtkActionBar *) action_bar, button_boxes[0]);
  gtk_action_bar_pack_start ((GtkActionBar *) action_bar, button_boxes[1]);

  // place action bar and scrolling window within the application
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL , 0);
  gtk_box_pack_start ((GtkBox *) box, scwin, TRUE, TRUE, 0);
  gtk_box_pack_start ((GtkBox *) box, action_bar, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), box);

  /*
   * Setup SIGKILL and SIGTERM callback functions for button clicks.
   */
  g_signal_connect (G_OBJECT (kill_button), "clicked",
                        G_CALLBACK (kill_button_callback), NULL);

  g_signal_connect (G_OBJECT (terminate_button), "clicked",
                        G_CALLBACK (terminate_button_callback), NULL);


  /*
   * Setup the process tree.
   */
  process_tree = build_process_tree(1, NULL);
  gtk_tree_view_expand_row (GTK_TREE_VIEW(tree),
			    gtk_tree_model_get_path ((GtkTreeModel *) store,
                            			     &(((struct process *) process_tree->data)->iter)),
                            FALSE);
  update_utilization(NULL);

  /*
   * Show the window.
   */
  gtk_widget_show_all (window);

  /*
   * Start timeout for updating statistics.
   */
  g_timeout_add(2000, (GSourceFunc) update_utilization, NULL);
}

int main (int argc, char **argv)
{
  GtkApplication *app;
  int status;

  /*
   * Launch the GUI Application
   */
  app = gtk_application_new ("student.pauljasekandannayu.taskmanager", G_APPLICATION_FLAGS_NONE);
  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);
  status = g_application_run (G_APPLICATION (app), argc, argv);
  g_object_unref (app);

  return status;
}
