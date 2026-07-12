#include <stdio.h>
#include <unistd.h>
int main(void) {
    printf("[TIMEOUT] Sleeping forever...\n");
    while (1) { sleep(1); }
    return 0;
}
