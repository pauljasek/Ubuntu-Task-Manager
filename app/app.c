#include <gtk/gtk.h>

#include <sys/syscall.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/unistd.h>
#include <linux/sched.h>

#include <unistd.h>
#include <stdio.h>
#include <libgen.h>

#define HIJACKED_SYSCALL __NR_tuxcall

#define MAX_PROCESSES 1000

struct process {
  pid_t pid;
  double cpu_usage;
} processes[MAX_PROCESSES];

int comp_processes (const void * elem1, const void * elem2) 
{
    const struct process *p1 = elem1, *p2 = elem2;
    if (p1->cpu_usage > p2->cpu_usage) return  -1;
    if (p1->cpu_usage < p2->cpu_usage) return 1;
    return 0;
}

int pid_list[MAX_PROCESSES];
char process_name_list[MAX_PROCESSES][100];

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

int get_process_memory_usage(int pid, char * memory_usage) {
  char filename[200];
  sprintf(filename, "/proc/%d/status", pid);
  FILE *fp = fopen(filename, "r");
  if (!fp) return 1;
  char *line;
  size_t len = 1024;

  line = malloc(len);

	
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
		strcpy(memory_usage, strdup(&line[8]));
		strtok(memory_usage, "\n");
		break;
	}
  }

  free(line);
  fclose(fp);
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
GtkWidget *labels[MAX_PROCESSES + 1][3];

gboolean update_utilization(GtkWidget * data)
{
  int length = syscall(HIJACKED_SYSCALL, pid_list, process_name_list);

  printf("%d\n", length);

  long long total_time = get_total_cpu_jiffies();
  for (int i = 0; i < length; i++)
  {
    long long time = get_process_cpu_jiffies(pid_list[i]);
    
    double cpu_utilization = 100.0 * (time - previous_time[i])/(total_time - previous_total_time);

    if (previous_time[i] == 0)
    {
      cpu_utilization = 0;
    }

    previous_time[i] = time;

    if (cpu_utilization == 0) cpu_utilization = 0;
    
    processes[i].pid = pid_list[i];
    processes[i].cpu_usage = cpu_utilization;
  }

  qsort (processes, length, sizeof(*processes), comp_processes);

  for (int i = 0, row = 0; i < length; i++, row++)
  {
    char text_string[1000], name_string[1000] = {'\0'}, *converted_string;
    get_process_name(processes[i].pid, name_string);
    converted_string = g_locale_to_utf8(name_string, strlen(name_string), NULL, NULL, NULL);
    if (converted_string == NULL) {
      printf("%s\n", name_string);
      sprintf(text_string, "Pid: %d", processes[i].pid);
      converted_string = text_string;
    }

    if (strlen(converted_string) == 0) {
      row--;
    } else {
      gtk_label_set_text((GtkLabel *) labels[row + 1][0], converted_string);
      sprintf(text_string, "%.4lf%%", processes[i].cpu_usage);
      gtk_label_set_text((GtkLabel *) labels[row + 1][1], text_string);
      char mem_usage_string[1000] = {'\0'};
      get_process_memory_usage(processes[i].pid, mem_usage_string);
      gtk_label_set_text((GtkLabel *) labels[row + 1][2], mem_usage_string);
    }
  }

  previous_total_time = total_time;

  return TRUE;
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
  GtkWidget *window, *scwin;
  GtkWidget *kill_button;
  GtkWidget *table, *check_buttons[MAX_PROCESSES + 1];

  window = gtk_application_window_new (app);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window), GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_ALWAYS);
  gtk_window_set_title (GTK_WINDOW (window), "Task Manager");
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 800);

  scwin = gtk_scrolled_window_new(NULL, NULL);
  gtk_container_add (GTK_CONTAINER (window), scwin);

  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);
  gtk_container_add (GTK_CONTAINER (scwin), table);

  labels[0][0] = gtk_frame_new (" Process Name ");
  labels[0][1] = gtk_frame_new (" CPU Usage ");
  labels[0][2] = gtk_frame_new (" Memory Usage ");

  gtk_frame_set_label_align ((GtkFrame *) labels[0][0], 0.5, 0.0);
  gtk_frame_set_label_align ((GtkFrame *) labels[0][1], 0.5, 0.0);
  gtk_frame_set_label_align ((GtkFrame *) labels[0][2], 0.5, 0.0);

  for (int i = 0; i < MAX_PROCESSES; i++)
  {
    labels[i + 1][0] = gtk_label_new ("");
    labels[i + 1][1] = gtk_label_new ("");
    labels[i + 1][2] = gtk_label_new ("");
  }

  kill_button = gtk_button_new_with_label("Kill");
  gtk_grid_attach (GTK_GRID (table), kill_button, 0, 0, 1, 1);


  for (int row = 0; row < MAX_PROCESSES + 1; row++)
  {
    if (row != 0)
    {
      check_buttons[row] = gtk_check_button_new();
      gtk_grid_attach (GTK_GRID (table), check_buttons[row], 0, row, 1, 1);
    }
    for (int column = 0; column < 3; column++)
    {
      gtk_grid_attach (GTK_GRID (table), labels[row][column], column + 1, row, 1, 1);
    }
  }

  gtk_widget_show_all (window);

  g_timeout_add(500, (GSourceFunc) update_utilization, NULL);
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
