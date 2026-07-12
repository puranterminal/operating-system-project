/*
 * test_timeout.c
 * ================================================================
 * STP503CA9 Task 2 — Test Binary: Timeout Trigger
 * ================================================================
 * Simulates malware that runs indefinitely without doing obvious work.
 * Expected result: sandbox kills it for TIME LIMIT EXCEEDED.
 *
 * COMPILE: gcc -Wall -std=c11 -o test_timeout test_timeout.c
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[TEST_TIMEOUT] Started — sleeping indefinitely...\n");
    fflush(stdout);

    int count = 0;
    while (1) {
        sleep(1);
        count++;
        printf("[TEST_TIMEOUT] Still running... %d seconds\n", count);
        fflush(stdout);
    }

    return 0;
}
