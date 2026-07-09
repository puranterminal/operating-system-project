#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <pwd.h>

#define SOCKET_PATH    "/tmp/priv_auth.sock"
#define MAX_USERNAME   64
#define MAX_PASSWORD   128
#define BACKLOG        8
#define AUTH_MAGIC     0xCAFEBABE
#define NOBODY_UID     65534

static int create_socket(void) {
    unlink(SOCKET_PATH);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(fd); return -1;
    }
    chmod(SOCKET_PATH, S_IRUSR | S_IWUSR);
    if (listen(fd, BACKLOG) < 0) {
        perror("listen"); close(fd); return -1;
    }

    printf("  [BACKEND] Socket created   : %s (mode 0600)\n", SOCKET_PATH);
    return fd;
}

static uid_t drop_privileges(void) {
    uid_t euid = geteuid();
    printf("  [BACKEND] EUID before drop : %d\n", (int)euid);

    uid_t target = (euid == 0) ? NOBODY_UID : euid;

    if (setresuid(target, target, target) != 0) {
        fprintf(stderr, "  [FATAL] setresuid failed: %s\n", strerror(errno));
        abort();
    }

    if (geteuid() != target) {
        fprintf(stderr, "  [FATAL] geteuid() verification failed!\n");
        abort();
    }
    printf("  [BACKEND] EUID after  drop : %d\n", (int)geteuid());

    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f))
            if (strncmp(line, "Uid:", 4) == 0) {
                printf("  [BACKEND] /proc check      : %s", line);
                break;
            }
        fclose(f);
    }

    uid_t r, e, s;
    if (getresuid(&r, &e, &s) == 0) {
        printf("  [BACKEND] getresuid()      : real=%d effective=%d saved=%d\n",
               (int)r, (int)e, (int)s);
        if (r != target || e != target || s != target) {
            fprintf(stderr, "  [FATAL] getresuid() verification failed!\n");
            abort();
        }
    }

    printf("  [BACKEND] Privilege drop VERIFIED. Re-elevation IMPOSSIBLE.\n");
    return (uid_t)target;
}
typedef struct { const char *username; const char *password; } credential_t;

static const credential_t DB[] = {
    { "alice",       "SecurePass#2024"  },
    { "bob",         "P@ssw0rd!Linux"   },
    { "admin",       "Admin@Secure99"   },
    { "puran123",    "Puran@123!"       },
    { "puranrijal",  "Rijal@Secure99"   },
    { "puranhacker", "Hacker@Linux2024" },
    { NULL, NULL }
};
typedef struct {
    uint32_t magic;
    char     username[MAX_USERNAME];
    char     password[MAX_PASSWORD];
    uint32_t attempt_no;
} auth_request_t;

typedef struct {
    int      result;
    uint32_t uid_after_drop;
    char     message[256];
} auth_response_t;

static int validate_packet(const auth_request_t *req) {
    if (req->magic != AUTH_MAGIC) {
        printf("  [BACKEND] REJECT: wrong magic 0x%08X\n", req->magic);
        return 0;
    }
    int uok = 0, pok = 0;
    for (int i = 0; i < MAX_USERNAME; i++)
        if (req->username[i] == '\0') { uok = 1; break; }
    for (int i = 0; i < MAX_PASSWORD; i++)
        if (req->password[i] == '\0') { pok = 1; break; }
    if (!uok || !pok) {
        printf("  [BACKEND] REJECT: no null terminator\n");
        return 0;
    }
    if (req->username[0] == '\0' || req->password[0] == '\0') {
        printf("  [BACKEND] REJECT: empty field\n");
        return 0;
    }
    return 1;
}
static int ct_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    int diff = 0;
    for (size_t i = 0; i < n; i++) diff |= p[i] ^ q[i];
    return diff;
}

static int verify_credentials(const char *user, const char *pass) {
    for (int i = 0; DB[i].username != NULL; i++) {
        if (strcmp(DB[i].username, user) == 0) {
            size_t sl = strlen(DB[i].password);
            size_t gl = strlen(pass);
            if (sl != gl) return 0;
            return ct_memcmp(DB[i].password, pass, sl) == 0 ? 1 : 0;
        }
    }
    return 0;
}
static void handle_client(int client_fd, uid_t server_uid) {
    auth_request_t  req;
    auth_response_t resp;
    explicit_bzero(&req,  sizeof(req));
    explicit_bzero(&resp, sizeof(resp));

    ssize_t n = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    if (n != (ssize_t)sizeof(req)) {
        printf("  [BACKEND] Short read. Dropping.\n");
        explicit_bzero(&req, sizeof(req));
        close(client_fd);
        return;
    }

    printf("  [BACKEND] Request for user : '%s' (attempt %u)\n",
           req.username, req.attempt_no);

    if (!validate_packet(&req)) {
        resp.result         = 0;
        resp.uid_after_drop = server_uid;
        snprintf(resp.message, sizeof(resp.message),
                 "Request rejected.");
        send(client_fd, &resp, sizeof(resp), 0);
        explicit_bzero(&req,  sizeof(req));
        explicit_bzero(&resp, sizeof(resp));
        close(client_fd);
        return;
    }

    int ok = verify_credentials(req.username, req.password);
    resp.result         = ok;
    resp.uid_after_drop = server_uid;

    if (ok) {
        snprintf(resp.message, sizeof(resp.message),
                 "Access granted for user '%s'.", req.username);
        printf("  [BACKEND] AUTH SUCCESS     : %s\n", req.username);
    } else {
        snprintf(resp.message, sizeof(resp.message),
                 "Access denied. Invalid credentials.");
        printf("  [BACKEND] AUTH FAILURE     : %s\n", req.username);
    }

    send(client_fd, &resp, sizeof(resp), 0);

    explicit_bzero(&req,  sizeof(req));
    explicit_bzero(&resp, sizeof(resp));
    printf("  [BACKEND] Memory cleared with explicit_bzero()\n\n");
    close(client_fd);
}

int main(void) {
    printf("\n  [BACKEND] Process started  PID=%d  EUID=%d\n\n",
           getpid(), geteuid());

    int server = create_socket();
    if (server < 0) {
        fprintf(stderr, "  [FATAL] Socket creation failed.\n");
        return EXIT_FAILURE;
    }

    printf("\n  [BACKEND] === PRIVILEGE DROP ===\n");
    uid_t server_uid = drop_privileges();
    printf("  [BACKEND] ================================\n\n");

    printf("  [BACKEND] Server loop started...\n\n");

    while (1) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            perror("accept");
            continue;
        }
        printf("  [BACKEND] New connection accepted.\n");
        handle_client(client, server_uid);
    }

    close(server);
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
}