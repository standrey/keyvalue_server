#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>

int daemon_pid;
char * lock_path;

static void child_handler (int signum) {
    int fd;
    int len;
    int sent;
    char sz[20];

    switch(signum) {
case SIGALRM:
    exit(1);
break;

case SIGUSR1:
    fd= open(lock_path, O_TRUNC | O_RDWR | O_CREAT, 0640);
if (fd < 0) {
fprintf(stderr, "unable to create lock file %s, error code %d, error string %s\n", lock_path, errno, strerror(errno));
exit(1);
break;
    }
len = sprintf(sz, "%u", daemon_pid);
sent = write(fd, sz, len);
if (sent != len) {
        fprintf(stderr, "unable write pid to lock %s, error code %d, string error %s\n", lock_path, errno, strerror(errno));
    }
close(fd);
exit(sent == len);
break;
case SIGCHLD:
    exit(1);
break;
    }
}

static void daemon_closing(int sigact) {
    if (getpid() == daemon_pid) {
        if (lock_path) {
            unlink(lock_path);
            free(lock_path);
            lock_path = NULL;
        }
        kill(getpid(), SIGKILL);
    }
}


int daemonize(const char* lockpath){
    pid_t sid, parent;
    int fd;
    char buf[10];
    int n, ret;
    struct sigaction act;

    if (getpid() == 1)
    {
        return 1;
    }

    fd = open(lockpath, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, buf, sizeof(buf));
        close(fd);
        if (n) {
            n = atoi(buf);
            ret = kill(n, 0);
            if (ret >= 0) {
                fprintf(stderr, "daemon already running from pid %d\n" , n);
                exit(1);
            }
            fprintf(stderr, "removing stale lock file %s from dead pid %d\n", lockpath, n);
            unlink(lockpath);
        }
    }

    n = strlen(lockpath) + 1;
    lock_path = (char *)malloc(n);
    if (!lock_path) {
        fprintf(stderr, "out of memory in %s\n", __FUNCTION__);
        return 1;
    }

    strcpy(lock_path, lockpath);

    signal(SIGCHLD, child_handler);
    signal(SIGUSR1, child_handler);
    signal(SIGALRM, child_handler);
    
    daemon_pid = fork();
    if (daemon_pid < 0) {
        fprintf(stderr, "unable to fork daemon, error code %d, error message %s\n", errno, strerror(errno));
        exit(1);
    }

    if (daemon_pid > 0) {
        alarm(2);
        pause();
        exit(1);
    }

    parent = getppid();
    daemon_pid = getpid();

    signal(SIGCHLD, SIG_DFL);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    umask(0);

    sid = setsid();
    if(sid < 0) {
        fprintf(stderr, "unable to create a new session, error code %d, error message %s\n", errno, strerror(errno));
        exit(1);
    }

    if (chdir("/") < 0) {
        fprintf(stderr, "unable to change directory to /, error code %d, error message %s\n", errno, strerror(errno));
        exit(1);
    }

    if(!freopen("/dev/null", "r", stdin)) { 
        fprintf(stderr, "unable to freopen stdin, error code %d, error message %s\n", errno, strerror(errno));
    }

    if(!freopen("/dev/null", "w", stdout)) { 
        fprintf(stderr, "unable to freopen stdout, error code %d, error message %s\n", errno, strerror(errno));
    }

    if(!freopen("/dev/null", "w", stderr)) { 
        fprintf(stderr, "unable to freopne stderr, error code %d, error message %s\n", errno, strerror(errno));
    }

    kill(parent, SIGUSR1);

    act.sa_handler = daemon_closing;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    sigaction(SIGTERM, &act, NULL);

    return 0;
}
