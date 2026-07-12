#include <stdio.h>
#include <signal.h>
#include <unistd.h>
void ignore(int s) { printf("[STUBBORN] Caught SIGTERM - ignoring!\n"); }
int main(void) {
    signal(SIGTERM, ignore);
    printf("[STUBBORN] Refusing to die...\n");
    while (1) { sleep(1); }
    return 0;
}