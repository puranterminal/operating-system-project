#define _GNU_SOURCE   /* needed so glibc exposes setresuid() and explicit_bzero() */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#define SOCKET_PATH  "/tmp/auth.sock"
#define NOBODY_UID   65534

typedef struct {
    char username[32];
    char password[32];
} request_t;

typedef struct {
    int  ok;
    char message[64];
} response_t;

typedef struct { const char *user; const char *pass; } cred_t;
static const cred_t DB[] = {
    { "puran", "puran@123" },
    { "admin", "Admin@123"  },
    { NULL, NULL }
};

/* ---------- PRIVILEGE DROPPING ---------- */
static void drop_privileges(void) {
    uid_t before = geteuid();
    printf("[BACKEND] EUID before drop : %d\n", before);

    if (setresuid(NOBODY_UID, NOBODY_UID, NOBODY_UID) != 0) {
        perror("[FATAL] setresuid failed");
        exit(1);
    }

    uid_t after = geteuid();
    printf("[BACKEND] EUID after  drop : %d\n", after);
    if (after == 0) {
        fprintf(stderr, "[FATAL] still root, aborting\n");
        exit(1);
    }

    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "Uid:", 4)) {
                printf("[BACKEND] /proc/self/status -> %s", line);
                break;
            }
        }
        fclose(f);
    }
    printf("[BACKEND] Privilege drop verified. Cannot re-elevate.\n\n");
}

/* ---------- SECURE IPC ---------- */
static int create_socket(void) {
    unlink(SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    chmod(SOCKET_PATH, S_IRUSR | S_IWUSR);
    listen(fd, 4);
    printf("[BACKEND] Listening on %s (mode 0600)\n", SOCKET_PATH);
    return fd;
}

/* ---------- ATTACK RESISTANCE ---------- */
static int request_is_valid(const request_t *r) {
    int u_ok = 0, p_ok = 0;
    for (size_t i = 0; i < sizeof(r->username); i++) if (r->username[i]=='\0'){u_ok=1;break;}
    for (size_t i = 0; i < sizeof(r->password); i++) if (r->password[i]=='\0'){p_ok=1;break;}
    return u_ok && p_ok && r->username[0] != '\0';
}

static int check_credentials(const char *user, const char *pass) {
    for (int i = 0; DB[i].user; i++)
        if (strcmp(DB[i].user, user) == 0 && strcmp(DB[i].pass, pass) == 0)
            return 1;
    return 0;
}

static void handle_client(int client_fd) {
    request_t  req;
    response_t resp = {0};

    ssize_t n = recv(client_fd, &req, sizeof(req), MSG_WAITALL);

    /* ---- ATTACK RESISTANCE: reject anything malformed ---- */
    if (n != (ssize_t)sizeof(req) || !request_is_valid(&req)) {
        printf("[BACKEND] [ATTACK-RESISTANCE] Malformed/undersized packet "
               "rejected (received %zd bytes, expected %zu bytes)\n",
               n, sizeof(req));

        resp.ok = 0;
        snprintf(resp.message, sizeof(resp.message), "Malformed request rejected");
        send(client_fd, &resp, sizeof(resp), 0);
        close(client_fd);
        return;
    }

    printf("[BACKEND] Auth request for user '%s'\n", req.username);

    resp.ok = check_credentials(req.username, req.password);
    snprintf(resp.message, sizeof(resp.message),
             resp.ok ? "Access granted" : "Access denied");

    send(client_fd, &resp, sizeof(resp), 0);

    /* SECURE MEMORY WIPE */
    explicit_bzero(&req, sizeof(req));
    explicit_bzero(&resp, sizeof(resp));

    close(client_fd);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[BACKEND] Started, PID=%d, EUID=%d\n", getpid(), geteuid());

    int server_fd = create_socket();

    printf("\n--- PRIVILEGE DROP ---\n");
    drop_privileges();

    printf("[BACKEND] Waiting for connections...\n\n");
    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) { perror("accept"); continue; }
        handle_client(client_fd);
    }
    return 0;
}