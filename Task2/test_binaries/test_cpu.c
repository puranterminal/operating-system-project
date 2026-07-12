/*
 * test_cpu.c
 * ================================================================
 * STP503CA9 Task 2 — Test Binary 1: CPU Burner
 * ================================================================
 * Simulates malware that consumes excessive CPU.
 * Expected result: sandbox kills it for CPU LIMIT EXCEEDED.
 *
 * COMPILE: gcc -Wall -o test_cpu test_cpu.c
 */
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    printf("[TEST_CPU] Started — burning CPU indefinitely...\n");
    fflush(stdout);

    /* Simulates a CPU-intensive infinite loop.
     * A real malware might use crypto mining or
     * brute-force algorithms in this pattern.     */
    volatile uint64_t x = 1;
    while (1) {
        x ^= (x << 13);
        x ^= (x >> 7);
        x ^= (x << 17);
    }
    return 0;
}
