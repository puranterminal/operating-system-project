#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    printf("[MEM] Eating RAM...\n");
    while (1) {
        char *p = malloc(10*1024*1024);          // 10 MB
        for (int i = 0; i < 10*1024*1024; i += 4096) p[i] = 1;   // touch it
        usleep(200000);
    }
    return 0;
}
