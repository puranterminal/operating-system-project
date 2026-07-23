#define _GNU_SOURCE   /* needed so glibc exposes explicit_bzero() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH   "/tmp/auth.sock"
#define BACKEND_PATH  "./backend"
#define MAX_ATTEMPTS  3
#define LOCK_SECONDS  30

typedef struct {
    char username[32];
    char password[32];
} request_t;

typedef struct {
    int  ok;
    char message[64];
} response_t;

/* ---------- PROCESS ISOLATION ---------- */
static pid_t start_backend(void) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }

    if (pid == 0) {
        execve(BACKEND_PATH, (char *[]){ BACKEND_PATH, NULL }, NULL);
        perror("execve failed");
        _exit(1);
    }

    printf("[FRONTEND] Backend started as separate process, PID=%d\n", pid);
    usleep(300000);
    return pid;
}

/* ---------- SECURE IPC ---------- */
static int connect_to_backend(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    for (int i = 0; i < 10; i++) {
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return sock;
        usleep(200000);
    }
    close(sock);
    return -1;
}

static int authenticate(const char *user, const char *pass) {
    int sock = connect_to_backend();
    if (sock < 0) {
        printf("[FRONTEND] Could not reach backend.\n");
        return 0;
    }

    request_t req = {0};
    strncpy(req.username, user, sizeof(req.username) - 1);
    strncpy(req.password, pass, sizeof(req.password) - 1);

    send(sock, &req, sizeof(req), 0);
    explicit_bzero(&req, sizeof(req));

    response_t resp = {0};
    recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    close(sock);

    printf("[BACKEND SAYS] %s\n", resp.message);
    return resp.ok;
}

int main(void) {
    printf("[FRONTEND] Started, PID=%d, EUID=%d\n\n", getpid(), geteuid());

    pid_t backend_pid = start_backend();

    int failed_attempts = 0;

    while (1) {
        char username[32], password[32];

        printf("Username: ");
        if (!fgets(username, sizeof(username), stdin)) break;
        username[strcspn(username, "\n")] = '\0';

        printf("Password: ");
        if (!fgets(password, sizeof(password), stdin)) break;
        password[strcspn(password, "\n")] = '\0';

        int ok = authenticate(username, password);

        explicit_bzero(username, sizeof(username));
        explicit_bzero(password, sizeof(password));

        if (ok) {
            printf("\n>>> ACCESS GRANTED <<<\n");
            failed_attempts = 0;
            break;
        }

        failed_attempts++;
        printf("\n>>> ACCESS DENIED <<< (attempt %d of %d)\n\n",
               failed_attempts, MAX_ATTEMPTS);

        if (failed_attempts >= MAX_ATTEMPTS) {
            printf("[LOCKOUT] Too many failed attempts.\n");
            printf("[LOCKOUT] Locking for %d seconds...\n", LOCK_SECONDS);

            for (int remaining = LOCK_SECONDS; remaining > 0; remaining--) {
                printf("\r[LOCKOUT] Time remaining: %2d seconds ", remaining);
                fflush(stdout);
                sleep(1);
            }
            printf("\r[LOCKOUT] Lock expired. You may try again.        \n\n");

            failed_attempts = 0;
        }
    }

    kill(backend_pid, SIGTERM);
    waitpid(backend_pid, NULL, 0);
    return 0;
}