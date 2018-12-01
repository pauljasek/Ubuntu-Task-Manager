/*
 * Utilities for the Task Manager
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

/*
 * Get the total amount of cpu jiffies that have elapsed using the proc filesystem.
 */
long long get_total_cpu_jiffies()
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

/*
 * Get the amount of cpu jiffies that have elapsed for a specific process using the proc filesystem.
 */
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

/*
 * Get the process memory usage in bytes given a specified pid.
 */
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

/*
 * Takes a number of bytes and formats a given string.
 */
char* MEM_SIZES[] = {"Bytes", "kB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
void format_memory(long long bytes, char *string) {
  int i = log2(bytes)/log2(1024);
  float decimal = 1.0 * bytes / pow(1024, i);
  sprintf(string, "%.1f %s", decimal, MEM_SIZES[i]);
}

/*
 * Get the name of a process from it's pid.
 */
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

