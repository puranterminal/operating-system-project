/*
 * test_mem.c
 * ================================================================
 * STP503CA9 Task 2 — Test Binary: Memory Hog
 * ================================================================
 * Simulates malware that consumes increasing memory.
 * Expected result: sandbox kills it for MEMORY LIMIT EXCEEDED.
 *
 * COMPILE: gcc -Wall -std=c11 -o test_mem test_mem.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHUNK_SIZE (10 * 1024 * 1024)  /* 10 MB per allocation */

int main(void) {
    printf("[TEST_MEM] Started — allocating memory in 10 MB chunks...\n");
    fflush(stdout);

    long total_mb = 0;

    while (1) {
        char *buf = malloc(CHUNK_SIZE);
        if (!buf) {
            printf("[TEST_MEM] malloc failed at %ld MB\n", total_mb);
            break;
        }
        /* Touch memory so it is actually allocated (not lazy) */
        memset(buf, 0xAB, CHUNK_SIZE);
        total_mb += 10;
        printf("[TEST_MEM] Allocated %ld MB total\n", total_mb);
        fflush(stdout);
        sleep(1);
    }

    return 0;
}
