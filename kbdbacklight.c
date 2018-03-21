/*******************************************************************************
 * kbdbacklight is a tool to adjust keyboard backlight illumination to
 * the specified brightness [0..100%].
 *
 * Copyright (C) 2018  Andreas Gollsch
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <regex.h>
#include <dirent.h>

#define VERSION         "1.0"
#define SHM_KEY         "/kbdbacklight.cmd"
#define SYSFS_PATH      "/sys/class/leds"
#if !defined(_ODROID_)
#define DEVICE_NAME     "kbd_backlight"
#else
#define DEVICE_NAME     "heartbeat" // for test only
#endif
#define BRIGHT_FILE     "brightness"
#define MAX_BRIGHT_FILE "max_brightness"

typedef enum {
  KC_noc, // no command
  KC_inc,
  KC_dec,
  KC_set,
  KC_size
} kbdcmd_t;

typedef struct {
  sem_t sem;
  kbdcmd_t cmd;
  float value;
  int status;
  int srun;
} memcmd_t;

static char device_path[PATH_MAX];
static char *bin_name;
static memcmd_t *memcmd;

void print_usage()
{
  fprintf(stderr, "Usage: %s [option]\n", bin_name);
  fprintf(stderr, "options:\n");
  fprintf(stderr, " -h show this message\n");
  fprintf(stderr, " -v show version\n");
  fprintf(stderr, "server options:\n");
  fprintf(stderr, " -S start service\n");
  fprintf(stderr, " -B start service and push into background\n");
  fprintf(stderr, "client options:\n");
  fprintf(stderr, " -s <percentage> set brightness\n");
  fprintf(stderr, " -i <percentage> increment brightness\n");
  fprintf(stderr, " -d <percentage> decreament brightness\n");
  fprintf(stderr, " -m set brightness to maximum\n");
  fprintf(stderr, " -o turn off illumination\n");

  exit(EXIT_FAILURE);
}

int set_brightness()
{
  int fd;
  float cur, max;
  char buf[5], bright_file[PATH_MAX], max_bright_file[PATH_MAX];

  sprintf(max_bright_file, "%s/%s", device_path, MAX_BRIGHT_FILE);
  sprintf(bright_file, "%s/%s", device_path, BRIGHT_FILE);
  if((fd = open(max_bright_file, O_RDONLY)) == -1)
  {
    max = 255.;
  }
  else
  {
    if(read(fd, buf, 5) == -1)
      max = 255.;
    close(fd);
    max = atof(buf);
  }
  if((fd = open(bright_file, O_RDWR)) == -1)
    return 1;
  if(read(fd, buf, 5) == -1)
  {
    close(fd);
    return 2;
  }
  cur = atof(buf);
  switch(memcmd->cmd)
  {
  case KC_inc:
    cur += memcmd->value * max /  100.;
    if(cur > max)
      cur = max;
    break;
  case KC_dec:
    cur -= memcmd->value * max /  100.;
    if(cur < 0)
      cur = 0;
    break;
  case KC_set:
    cur = memcmd->value * max /  100.;
    break;
  default:
    break;
  }
  sprintf(buf, "%.0f", cur);
  if(write(fd, buf, sizeof(buf)) == -1)
  {
    close(fd);
    return 3;
  }
  close(fd);
  memcmd->value = cur * 100. / max;

  return 0;
}

void check_srun()
{
  sem_wait(&memcmd->sem);
  if(memcmd->srun == 0)
  {
    fprintf(stderr, "%s: Service not running\n", bin_name);
    sem_post(&memcmd->sem);
    shm_unlink(SHM_KEY);
    exit(EXIT_FAILURE);
  }
  sem_post(&memcmd->sem);
}

int check_root()
{
  if(getuid() != 0)
  {
    fprintf(stderr, "%s: root permission required\n", bin_name);
    shm_unlink(SHM_KEY);
    exit(EXIT_FAILURE);
  }
}

void create_sem()
{
  if(sem_init(&memcmd->sem, 1, 1) < 0)
  {
    fprintf(stderr, "%s: Semaphore init fail\n", bin_name);
    exit(EXIT_FAILURE);
  }
}

void open_mem(int oflag)
{
  int mem_id;

  umask(0);
  mem_id = shm_open(SHM_KEY, oflag, 0666);
  if(mem_id < 0)
  {
    if(oflag & O_CREAT)
      fprintf(stderr, "%s: Can't create shared memory\n", bin_name);
    else
      fprintf(stderr, "%s: Can't open shared memory. May service is "
                      "not running.\n", bin_name);
    exit(EXIT_FAILURE);
  }
  memcmd = (memcmd_t*)mmap(NULL, sizeof(memcmd_t), PROT_READ | PROT_WRITE,
                           MAP_SHARED, mem_id, 0);
  if(memcmd == MAP_FAILED)
  {
    fprintf(stderr, "%s: Can't create shared memory\n", bin_name);
    exit(EXIT_FAILURE);
  }
  if(ftruncate(mem_id, sizeof(memcmd_t)) < 0)
  {
    fprintf(stderr, "%s: Can't allocate memory\n", bin_name);
    exit(EXIT_FAILURE);
  }
  create_sem();
}

void server_loop()
{
  sem_wait(&memcmd->sem);
  if(memcmd->srun != 0)
  {
    fprintf(stderr, "%s: Service already running\n", bin_name);
    sem_post(&memcmd->sem);
    exit(EXIT_FAILURE);
  }
  memcmd->srun = 1;
  memcmd->status = set_brightness();
  sem_post(&memcmd->sem);
  for(;;)
  {
    sem_wait(&memcmd->sem);
    if(memcmd->cmd != KC_noc)
    {
      memcmd->status = set_brightness();
      memcmd->cmd = KC_noc;
    }
    sem_post(&memcmd->sem);
    usleep(100000);
  }
}

void send_cmd(kbdcmd_t cmd, float value)
{
  check_srun();
  sem_wait(&memcmd->sem);
  memcmd->cmd = cmd;
  memcmd->value = value;
  memcmd->status = 0;
  sem_post(&memcmd->sem);

  exit(EXIT_SUCCESS);
}

void print_value()
{
  check_srun();
  sem_wait(&memcmd->sem);
  if(memcmd->cmd == KC_noc)
    fprintf(stdout, "%.2f\n", memcmd->value);
  sem_post(&memcmd->sem);
}

int check_device(char *path)
{
  int ret;
  DIR *d;
  regex_t regex;
  struct dirent *dir;

  if(regcomp(&regex, ".*" DEVICE_NAME, REG_EXTENDED) != 0)
    return -1;
  d = opendir(SYSFS_PATH);
  if(d)
  {
    while((dir = readdir(d)) != NULL)
    {
      if(regexec(&regex, dir->d_name, 0, NULL, 0) == 0)
        ret = sprintf(path, "%s/%s", SYSFS_PATH, dir->d_name);
    }
    closedir(d);
  }

  return ret;
}

void sig_handler(int signo)
{
  if((signo == SIGINT) || (signo == SIGTERM))
  {
    shm_unlink(SHM_KEY);
    exit(EXIT_SUCCESS);
  }
}

int main(int argc, char *argv[])
{
  int opt, pid;

  bin_name = argv[0];
  if(signal(SIGINT, sig_handler) == SIG_ERR)
    fprintf(stderr, "%s: Can't catch SIGINT\n", bin_name);
  if(signal(SIGTERM, sig_handler) == SIG_ERR)
    fprintf(stderr, "%s: Can't catch SIGKILL\n", bin_name);
  while((opt = getopt(argc, argv, "i:d:s:BSomvh")) != -1)
  {
    switch(opt)
    {
    case 'B':
      check_root();
      if(check_device(device_path) < 1)
      {
        fprintf(stderr, "%s: Can not find keyboard illumination device\n",
                bin_name);
        exit(EXIT_FAILURE);
      }
      open_mem(O_RDWR | O_CREAT);
      pid = fork();
      if(pid == -1)
      {
        fprintf(stderr, "%s: Can't fork process to background\n", bin_name);
        exit(EXIT_FAILURE);
      }
      else if(pid > 0)
      {
        fprintf(stdout, "%s: Service up and running\n", bin_name);
        exit(EXIT_SUCCESS);
      }
      server_loop();
      break;
    case 'S':
      check_root();
      if(check_device(device_path) < 1)
      {
        fprintf(stderr, "%s: Can not find keyboard illumination device\n",
                bin_name);
        exit(EXIT_FAILURE);
      }
      open_mem(O_RDWR | O_CREAT);
      server_loop();
      break;
    case 'i':
      open_mem(O_RDWR);
      send_cmd(KC_inc, atof(optarg));
      break;
    case 'd':
      open_mem(O_RDWR);
      send_cmd(KC_dec, atof(optarg));
      break;
    case 's':
      open_mem(O_RDWR);
      send_cmd(KC_set, atof(optarg));
      break;
    case 'o':
      open_mem(O_RDWR);
      send_cmd(KC_set, 0.);
      break;
    case 'm':
      open_mem(O_RDWR);
      send_cmd(KC_set, 100.);
      break;
    case 'v':
      fprintf(stdout, "%s version %s\n", bin_name, VERSION);
      exit(EXIT_SUCCESS);
      break;
    case 'p':
      fprintf(stdout, ">%s<\n", optarg);
      exit(EXIT_SUCCESS);
      break;
    case 'h':
      fprintf(stderr, "%s is a tool to adjust keyboard backlight "
                      "illumination\nto the specified brightness [0..100%%]."
                      "\n\n", bin_name);
      fprintf(stderr, "%s Copyright (C) 2018 Andreas Gollsch "
                      "<a.gollsch@freenet.de>\n"
                      "This program comes with ABSOLUTELY NO WARRANTY.\n"
                      "This is free software, and you are welcome to "
                      "redistribute it\n"
                      "under certain conditions.\n\n", bin_name);
    default:
      print_usage();
    }
  }
  open_mem(O_RDWR);
  print_value();

  return EXIT_SUCCESS;
}
