cat > Task1/frontend.c << 'EOF'
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
EOF
