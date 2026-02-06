#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define VERSION "1.3.0"
#define MAX_EXCLUDES 128
#define MAX_JOBS 64
#define EVENT_BUF (1024 * (sizeof(struct inotify_event) + 16))
#define CMD_BUF 16384

struct config {
  char name[64];
  char local_root[PATH_MAX];
  char remote_user[64];
  char remote_host[256];
  char remote_root[PATH_MAX];
  char remote_password[256];
  int delete;
  int delay_ms;
  char excludes[MAX_EXCLUDES][PATH_MAX];
  int exclude_count;
};

static struct config global_cfg;
static struct config jobs[MAX_JOBS];
static int job_count = 0;
static int use_syslog = 0;

/* ---------- Logging ---------- */
static void ws_log(int priority, const char *format, ...) {
  va_list args;
  va_start(args, format);

  if (use_syslog) {
    vsyslog(priority, format, args);
  } else {
    FILE *out = (priority >= LOG_ERR) ? stderr : stdout;
    vfprintf(out, format, args);
    fprintf(out, "\n");
    fflush(out);
  }

  va_end(args);
}

static void die(const char *msg) {
  if (use_syslog) {
    syslog(LOG_ERR, "%s: %m", msg);
  } else {
    perror(msg);
  }
  exit(EXIT_FAILURE);
}

/* ---------- Utils ---------- */
static void trim(char *s) {
  char *p = s;
  while (*p == ' ' || *p == '\t')
    p++;
  memmove(s, p, strlen(p) + 1);

  for (int i = (int)strlen(s) - 1; i >= 0; i--) {
    if (s[i] == '\n' || s[i] == '\r' || s[i] == ' ' || s[i] == '\t')
      s[i] = 0;
    else
      break;
  }
}

static int check_permissions(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0)
    return -1;
  if (st.st_mode & (S_IRWXG | S_IRWXO)) {
    ws_log(
        LOG_ERR,
        "Security Error: '%s' has too open permissions. Must be 0600 or 0700.",
        path);
    return -1;
  }
  return 0;
}

/* ---------- Config Loading ---------- */
static void init_config(struct config *c) {
  memset(c, 0, sizeof(struct config));
  c->delay_ms = 500;
}

static void copy_config(struct config *dst, const struct config *src) {
  memcpy(dst, src, sizeof(struct config));
}

static void parse_config_file(const char *file, struct config *c) {
  if (check_permissions(file) != 0) {
    ws_log(LOG_WARNING, "Skipping insecure config file: %s", file);
    return;
  }

  FILE *f = fopen(file, "r");
  if (!f) {
    ws_log(LOG_WARNING, "Could not open config file %s", file);
    return;
  }

  char line[4096];
  while (fgets(line, sizeof(line), f)) {
    trim(line);
    if (!line[0] || line[0] == '#')
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = 0;

    char *key = line;
    char *val = eq + 1;
    trim(key);
    trim(val);

    if (!strcmp(key, "local.root")) {
      if (!realpath(val, c->local_root)) {
        ws_log(LOG_ERR, "Error: Invalid local.root '%s' in %s", val, file);
      }
    } else if (!strcmp(key, "remote.user")) {
      strncpy(c->remote_user, val, sizeof(c->remote_user) - 1);
    } else if (!strcmp(key, "remote.host")) {
      strncpy(c->remote_host, val, sizeof(c->remote_host) - 1);
    } else if (!strcmp(key, "remote.root")) {
      strncpy(c->remote_root, val, sizeof(c->remote_root) - 1);
    } else if (!strcmp(key, "remote.password")) {
      strncpy(c->remote_password, val, sizeof(c->remote_password) - 1);
    } else if (!strcmp(key, "rsync.delete")) {
      c->delete = !strcmp(val, "true");
    } else if (!strcmp(key, "rsync.delay_ms")) {
      c->delay_ms = atoi(val);
    } else if (!strcmp(key, "exclude")) {
      if (c->exclude_count < MAX_EXCLUDES) {
        strncpy(c->excludes[c->exclude_count++], val, PATH_MAX - 1);
      }
    }
  }
  fclose(f);
}

static void load_daemon_configs() {
  init_config(&global_cfg);

  const char *main_conf = "/etc/watchsync.conf";
  if (access(main_conf, F_OK) == 0) {
    parse_config_file(main_conf, &global_cfg);
  }

  const char *conf_dir = "/etc/watchsync.d";
  if (check_permissions(conf_dir) != 0) {
    ws_log(LOG_ERR, "Error: Config directory %s is insecure.", conf_dir);
    return;
  }

  DIR *d = opendir(conf_dir);
  if (!d)
    return;

  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_type != DT_REG)
      continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", conf_dir, e->d_name);

    if (job_count < MAX_JOBS) {
      struct config *c = &jobs[job_count++];
      copy_config(c, &global_cfg);
      strncpy(c->name, e->d_name, sizeof(c->name) - 1);
      parse_config_file(path, c);
    }
  }
  closedir(d);
}

static void prompt_password(struct config *c) {
  struct termios oldt, newt;
  printf("Remote password for %s@%s: ", c->remote_user, c->remote_host);
  fflush(stdout);

  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  if (fgets(c->remote_password, sizeof(c->remote_password), stdin)) {
    trim(c->remote_password);
  }

  tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
  printf("\n");
}

/* ---------- Sync Logic ---------- */
static void run_rsync(const struct config *c) {
  char excl_tpl[] = "/tmp/watchsync_excl_XXXXXX";
  char out_tpl[] = "/tmp/watchsync_out_XXXXXX";

  int excl_fd = mkstemp(excl_tpl);
  int out_fd = mkstemp(out_tpl);

  if (excl_fd < 0 || out_fd < 0) {
    ws_log(LOG_ERR, "[%s] mkstemp failed", c->name);
    if (excl_fd >= 0) {
      close(excl_fd);
      unlink(excl_tpl);
    }
    if (out_fd >= 0) {
      close(out_fd);
      unlink(out_tpl);
    }
    return;
  }

  FILE *excl_f = fdopen(excl_fd, "w");
  if (!excl_f) {
    close(excl_fd);
    unlink(excl_tpl);
    return;
  }
  for (int i = 0; i < c->exclude_count; i++)
    fprintf(excl_f, "%s\n", c->excludes[i]);
  fclose(excl_f);

  if (c->remote_password[0]) {
    setenv("SSHPASS", c->remote_password, 1);
  }

  char cmd[CMD_BUF];
  int n = snprintf(cmd, sizeof(cmd),
                   "sshpass -e rsync -az --itemize-changes %s "
                   "--exclude-from=%s %s/ %s@%s:%s/ > %s 2>&1",
                   c->delete ? "--delete" : "", excl_tpl, c->local_root,
                   c->remote_user, c->remote_host, c->remote_root, out_tpl);

  if (n < 0 || n >= (int)sizeof(cmd)) {
    ws_log(LOG_ERR, "[%s] Command buffer overflow", c->name);
    unlink(excl_tpl);
    unlink(out_tpl);
    close(out_fd);
    return;
  }

  int rc = system(cmd);

  FILE *out_f = fdopen(out_fd, "r");
  if (out_f) {
    char line[1024];
    int changes = 0;
    while (fgets(line, sizeof(line), out_f)) {
      trim(line);
      if (line[0] != 0) {
        if (!changes) {
          ws_log(LOG_INFO, "[%s] === RSYNC START ===", c->name);
          changes = 1;
        }
        ws_log(LOG_INFO, "[%s] %s", c->name, line);
      }
    }
    fclose(out_f);
    if (changes) {
      if (rc == 0)
        ws_log(LOG_INFO, "[%s] === RSYNC OK ===", c->name);
      else
        ws_log(LOG_ERR, "[%s] === RSYNC ERROR (exit code %d) ===", c->name, rc);
    }
  } else {
    close(out_fd);
  }

  unlink(excl_tpl);
  unlink(out_tpl);
}

/* ---------- Watch Logic ---------- */
static void add_watch_recursive(int fd, const char *path) {
  inotify_add_watch(fd, path,
                    IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                        IN_MOVED_TO);
  DIR *d = opendir(path);
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
      continue;
    char sub[PATH_MAX];
    snprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
    struct stat st;
    if (lstat(sub, &st) == 0 && S_ISDIR(st.st_mode))
      add_watch_recursive(fd, sub);
  }
  closedir(d);
}

static void watch_job(const struct config *c) {
  int fd = inotify_init1(IN_NONBLOCK);
  if (fd < 0)
    die("inotify_init1");

  add_watch_recursive(fd, c->local_root);
  ws_log(LOG_INFO, "[%s] Monitoring %s", c->name, c->local_root);

  char buf[EVENT_BUF];
  struct timespec last_event = {0};
  int pending = 0;

  while (1) {
    ssize_t r = read(fd, buf, sizeof(buf));
    if (r > 0) {
      pending = 1;
      clock_gettime(CLOCK_MONOTONIC, &last_event);
    }
    if (pending) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      long diff = (now.tv_sec - last_event.tv_sec) * 1000 +
                  (now.tv_nsec - last_event.tv_nsec) / 1000000;
      if (diff >= c->delay_ms) {
        run_rsync(c);
        pending = 0;
        add_watch_recursive(fd, c->local_root);
      }
    }
    usleep(200 * 1000);
  }
}

/* ---------- CLI ---------- */
static void usage(const char *prog) {
  printf("watchsync version %s\n", VERSION);
  printf("Usage:\n");
  printf(
      "  %s -c <config_file>    Run in FOREGROUND with specific config file\n",
      prog);
  printf("  %s -d                 Run as DAEMON (reads /etc/watchsync.conf and "
         "/etc/watchsync.d/*)\n",
         prog);
  printf("  %s -h                 Show this help message\n\n", prog);
  printf("Note: Daemon mode logs to syslog. Foreground mode logs to stdout.\n");
  printf("Security note: Config files and /etc/watchsync.d must not be "
         "world/group accessible.\n");
}

static void daemonize() {
  pid_t pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);
  if (setsid() < 0)
    exit(EXIT_FAILURE);

  signal(SIGCHLD, SIG_IGN);

  pid = fork();
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS);

  umask(0);
  if (chdir("/") != 0) {
    perror("chdir");
    exit(EXIT_FAILURE);
  }

  int fd = open("/dev/null", O_RDWR);
  if (fd >= 0) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > 2)
      close(fd);
  }
}

int main(int argc, char **argv) {
  int opt;
  int is_daemon = 0;
  char *config_file = NULL;

  while ((opt = getopt(argc, argv, "dc:h")) != -1) {
    switch (opt) {
    case 'd':
      is_daemon = 1;
      break;
    case 'c':
      config_file = optarg;
      break;
    case 'h':
      usage(argv[0]);
      return 0;
    default:
      usage(argv[0]);
      return 1;
    }
  }

  if (is_daemon) {
    use_syslog = 1;
    openlog("watchsync", LOG_PID, LOG_DAEMON);

    load_daemon_configs();
    if (job_count == 0) {
      ws_log(LOG_ERR,
             "No valid jobs found. Check permissions and /etc/watchsync.d/");
      return 1;
    }

    daemonize();
    ws_log(LOG_INFO, "WatchSync daemon started with %d jobs", job_count);

    for (int i = 0; i < job_count; i++) {
      if (fork() == 0) {
        watch_job(&jobs[i]);
        exit(0);
      }
    }
    while (wait(NULL) > 0)
      ;
    closelog();
  } else if (config_file) {
    struct config c;
    init_config(&c);
    strncpy(c.name, "cli", sizeof(c.name) - 1);
    parse_config_file(config_file, &c);
    if (!c.local_root[0]) {
      ws_log(LOG_ERR, "Error: Configuration file is invalid or insecure.");
      return 1;
    }
    if (c.remote_password[0] == 0) {
      prompt_password(&c);
    }
    watch_job(&c);
  } else {
    usage(argv[0]);
    return 1;
  }

  return 0;
}
