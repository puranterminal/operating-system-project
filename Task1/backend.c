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

    printf("  [BACKEND] Socket ready, waiting for connections...\n");
    close(server);
    unlink(SOCKET_PATH);
    return EXIT_SUCCESS;
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
