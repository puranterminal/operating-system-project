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
typedef struct {
    uint32_t magic;
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];
    uint32_t attempt_no;
} auth_request_t;

typedef struct {
    int result;
    uint32_t uid_after_drop;
    char message[256];
} auth_response_t;

static int connect_backend(void) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    for (int i = 0; i < 15; i++) {
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
            return sock;
        usleep(100000);
    }
    close(sock);
    return -1;
}

static int authenticate(const char *user, const char *pass, int attempt) {
    int sock = connect_backend();
    if (sock < 0) {
        printf("  [ERROR] Cannot reach backend.\n");
        return 0;
    }

    auth_request_t req;
    memset(&req, 0, sizeof(req));
    req.magic = 0xCAFEBABE;
    req.attempt_no = (uint32_t)attempt;
    strncpy(req.username, user, MAX_USERNAME - 1);
    strncpy(req.password, pass, MAX_PASSWORD - 1);

    send(sock, &req, sizeof(req), 0);
    memset(&req, 0, sizeof(req));

    auth_response_t resp;
    memset(&resp, 0, sizeof(resp));
    recv(sock, &resp, sizeof(resp), MSG_WAITALL);
    close(sock);

    printf("  [BACKEND] %s\n", resp.message);
    return resp.result;
}
static void print_banner(void) {
    printf("\n");
    printf("  +-----------------------------------------+\n");
    printf("  |   PRIVILEGE-SEPARATED AUTH SYSTEM       |\n");
    printf("  |   STP503CA9 - Task 1 - Puran Rijal      |\n");
    printf("  +-----------------------------------------+\n");
    printf("  |   [1]  Login                            |\n");
    printf("  |   [2]  Exit                             |\n");
    printf("  +-----------------------------------------+\n\n");
}
int main(void) {
    printf("\n  [FRONTEND] Started  PID=%d  EUID=%d\n\n",
           getpid(), geteuid());

    pid_t backend_pid = launch_backend();
    if (backend_pid < 0) return EXIT_FAILURE;

    int attempts = 0;
    time_t lock_until = 0;

    while (1) {
        print_banner();

        if (lock_until > 0) {
            time_t now = time(NULL);
            if (now < lock_until) {
                printf("  [LOCKED] Try again in %ld seconds.\n",
                       (long)(lock_until - now));
                sleep(2);
                continue;
            }
            lock_until = 0;
            attempts = 0;
        }

        char choice[8];
        printf("  Your choice: ");
        fflush(stdout);
        if (fgets(choice, sizeof(choice), stdin) == NULL) break;
        if (choice[0] == '2') break;
        if (choice[0] != '1') {
            printf("  [!] Enter 1 or 2.\n");
            continue;
        }

        char username[MAX_USERNAME];
        char password[MAX_PASSWORD];
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));

        printf("\n");
        if (read_input("Username: ", username, sizeof(username), 0) < 0) {
            printf("  [!] Invalid username.\n");
            continue;
        }
        if (read_input("Password: ", password, sizeof(password), 1) < 0) {
            printf("  [!] Invalid password.\n");
            memset(password, 0, sizeof(password));
            continue;
        }

        attempts++;
        printf("\n  [INFO] Attempt %d of %d\n", attempts, MAX_ATTEMPTS);

        int ok = authenticate(username, password, attempts);
        memset(username, 0, sizeof(username));
        memset(password, 0, sizeof(password));

        if (ok) {
            printf("\n  +-----------------------------------------+\n");
            printf("  |         ACCESS GRANTED - Welcome!       |\n");
            printf("  +-----------------------------------------+\n\n");
            attempts = 0;
            lock_until = 0;
        } else {
            printf("\n  [DENIED] Failed attempt %d of %d.\n",
                   attempts, MAX_ATTEMPTS);
            if (attempts >= MAX_ATTEMPTS) {
                lock_until = time(NULL) + LOCK_SECONDS;
                printf("  [LOCKED] Locked for %d seconds.\n", LOCK_SECONDS);
            }
        }
    }

    kill(backend_pid, SIGTERM);
    waitpid(backend_pid, NULL, 0);
    printf("\n  [FRONTEND] Goodbye.\n\n");
    return EXIT_SUCCESS;
}