#include <gtk/gtk.h>
#include <unistd.h>
#include <stdio.h>

double get_total_cpu_jiffies()
{
  char dummy[10];
  long long total_time = 0, temp;
  FILE *fp = fopen("/proc/stat", "r");
  fscanf(fp, "%s", dummy);
  for (int i = 0; i < 10; i++) {
    fscanf(fp, "%lld", &temp);
    total_time += temp;
  }
  fclose(fp);

  return total_time;
}

long long get_process_cpu_jiffies(int pid) {
  char filename[100];
  char items[52][100];
  long long utime, stime, cutime, cstime, starttime, total_time;

  sprintf(filename, "/proc/%d/stat", pid);

  FILE *fp = fopen(filename, "r");


  
  for (int i = 0; i < 52; i++)
  {
    fscanf(fp, "%s", items[i]);
  }

  fclose(fp);

  utime = strtoll(items[13],NULL,10);
  stime = strtoll(items[14],NULL,10);
  cutime = strtoll(items[15],NULL,10);
  cstime = strtoll(items[16],NULL,10);

  return utime + cutime + stime + cstime;
}

int previous_total_time = 0;
int previous_time = 0;

gboolean update_utilization(GtkWidget *label)
{
    long long total_time = get_total_cpu_jiffies();
    long long time = get_process_cpu_jiffies(1945);

    double cpu_utilization = 100.0 * (previous_time - time)/(previous_total_time - total_time);
    previous_total_time = total_time;
    previous_time = time;

    char text_string[40];
    sprintf(text_string, "%.1lf%%", cpu_utilization);

    gtk_label_set_text((GtkLabel *)label, text_string);

    return TRUE;
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
  GtkWidget *window;
  GtkWidget *table, *labels[2][3];

  window = gtk_application_window_new (app);
  gtk_window_set_title (GTK_WINDOW (window), "Task Manager");
  gtk_window_set_default_size (GTK_WINDOW (window), 600, 800);


  table = gtk_grid_new ();
  gtk_grid_set_column_spacing (GTK_GRID (table), 10);
  gtk_container_add (GTK_CONTAINER (window), table);

  labels[0][0] = gtk_frame_new ("Process Name");
  labels[0][1] = gtk_frame_new ("CPU Usage");
  labels[0][2] = gtk_frame_new ("Memory Usage");
  labels[1][0] = gtk_label_new ("Test");
  labels[1][1] = gtk_label_new ("0%");
  labels[1][2] = gtk_label_new ("0%");

  for (int row = 0; row < 2; row++)
  {
    for (int column = 0; column < 3; column++)
    {
      gtk_grid_attach (GTK_GRID (table), labels[row][column], column, row, 1, 1);
    }
  }

  gtk_widget_show_all (window);

  g_timeout_add(1000, (GSourceFunc) update_utilization, labels[1][1]);
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
