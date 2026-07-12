/*
 * test_normal.c
 * ================================================================
 * STP503CA9 Task 2 — Test Binary: Normal Behaviour
 * ================================================================
 * Simulates a well-behaved program that completes normally.
 * Expected result: sandbox reports NORMAL COMPLETION.
 *
 * COMPILE: gcc -Wall -std=c11 -o test_normal test_normal.c
 */
#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("[TEST_NORMAL] Started — simulating normal work...\n");
    fflush(stdout);

    for (int i = 1; i <= 5; i++) {
        printf("[TEST_NORMAL] Working... step %d/5\n", i);
        fflush(stdout);
        sleep(1);
    }

    printf("[TEST_NORMAL] Finished successfully.\n");
    return 0;
}
