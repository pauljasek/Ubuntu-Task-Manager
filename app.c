#include <gtk/gtk.h>

static void
print_hello (GtkWidget *widget,
             gpointer   data)
{
  g_print ("Hello World\n");
}

static void
activate (GtkApplication *app,
          gpointer        user_data)
{
  GtkWidget *window;
  GtkWidget *button;
  GtkWidget *button_box;
  GtkWidget *table, *labels[3][3], *check_buttons[3];

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
  labels[1][1] = gtk_label_new ("20%");
  labels[1][2] = gtk_label_new ("3%");
  labels[2][0] = gtk_label_new ("MyProcess");
  labels[2][1] = gtk_label_new ("25%");
  labels[2][2] = gtk_label_new ("5%");


  for (int row = 0; row < 3; row++)
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
