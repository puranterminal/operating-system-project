#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define SOCKET_PATH    "/tmp/priv_auth.sock"
#define BACKEND_PATH   "./backend"
#define MAX_USERNAME   64
#define MAX_PASSWORD   128
#define MAX_ATTEMPTS   3
#define LOCK_SECONDS   30

int main(void) {
    printf("\n  [FRONTEND] Started  PID=%d  EUID=%d\n\n",
           getpid(), geteuid());
    printf("  [FRONTEND] Welcome to Privilege-Separated Auth System\n");
    return EXIT_SUCCESS;
}

static void echo_off(struct termios *saved) {
    struct termios t;
    tcgetattr(STDIN_FILENO, saved);
    t = *saved;
    t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static void echo_on(struct termios *saved) {
    tcsetattr(STDIN_FILENO, TCSANOW, saved);
}

static int read_input(const char *prompt, char *buf, size_t maxlen, int hide) {
    struct termios saved;
    printf("  %s", prompt);
    fflush(stdout);
    if (hide) echo_off(&saved);
    if (fgets(buf, (int)maxlen, stdin) == NULL) {
        if (hide) echo_on(&saved);
        printf("\n");
        return -1;
    }
    if (hide) echo_on(&saved);
    printf("\n");
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    if (len == 0 || len >= maxlen - 1) return -1;
    return (int)len;
}

static pid_t launch_backend(void) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        char *argv[] = { BACKEND_PATH, NULL };
        char *envp[] = { "PATH=/usr/bin:/bin", "HOME=/tmp", NULL };
        printf("  [CHILD] PID=%d calling execve -> backend\n", getpid());
        fflush(stdout);
        execve(BACKEND_PATH, argv, envp);
        perror("execve failed");
        _exit(EXIT_FAILURE);
    }

    printf("  [PARENT] PID=%d spawned backend PID=%d\n", getpid(), (int)pid);
    usleep(400000);
    return pid;
}