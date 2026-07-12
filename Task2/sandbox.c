#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/wait.h>
 
#define TIME_LIMIT 10
#define CPU_LIMIT  80
#define MEM_LIMIT  102400
 
pid_t child;
int   running = 1;
int   killed  = 0;
char  reason[50] = "NORMAL";
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
 
void kill_child(char *why) {
    pthread_mutex_lock(&lock);
    if (killed) {
        pthread_mutex_unlock(&lock);
        return;
    }
    killed = 1;
    strcpy(reason, why);
    pthread_mutex_unlock(&lock);
 
    printf(">> VIOLATION: %s -> sending SIGTERM\n", why);
    kill(child, SIGTERM);
    sleep(2);
 
    if (running) {
        printf(">> SIGTERM ignored -> sending SIGKILL\n");
        kill(child, SIGKILL);
    }
}
 
double get_cpu() {
    static long old_ticks = 0;
    char path[64];
    sprintf(path, "/proc/%d/stat", child);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
 
    char buf[1024];
    fgets(buf, sizeof(buf), f);
    fclose(f);
 
    char *p = strrchr(buf, ')');
    long utime, stime;
    sscanf(p+2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %ld %ld",
           &utime, &stime);
 
    long ticks = utime + stime;
    double cpu = (ticks - old_ticks) * 100.0 / sysconf(_SC_CLK_TCK) / 0.5;
    old_ticks = ticks;
    return cpu;
}
 
long get_mem() {
    char path[64];
    sprintf(path, "/proc/%d/status", child);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
 
    char line[256];
    long kb = 0;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld", &kb) == 1) break;
    fclose(f);
    return kb;
}
 
void *watch_time(void *_) {
    int sec = 0;
    while (running) {
        sleep(1);
        sec++;
        printf("[TIME] %d/%d sec\n", sec, TIME_LIMIT);
        if (sec >= TIME_LIMIT) { kill_child("TIME LIMIT"); break; }
    }
    return NULL;
}
 
void *watch_cpu(void *_) {
    get_cpu();
    while (running) {
        usleep(500000);
        double c = get_cpu();
        if (c < 0) break;
        printf("[CPU ] %.0f%%\n", c);
        if (c > CPU_LIMIT) { kill_child("CPU LIMIT"); break; }
    }
    return NULL;
}
 
void *watch_mem(void *_) {
    while (running) {
        usleep(500000);
        long m = get_mem();
        if (m < 0) break;
        printf("[MEM ] %ld KB\n", m);
        if (m > MEM_LIMIT) { kill_child("MEMORY LIMIT"); break; }
    }
    return NULL;
}
 
int main(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: ./sandbox <program>\n"); return 1; }
 
    printf("=== SANDBOX START: %s ===\n", argv[1]);
 
    child = fork();
 
    if (child == 0) {
        char *env[] = { NULL };
        execve(argv[1], &argv[1], env);
        printf("execve failed!\n");
        _exit(1);
    }
 
    printf("Child PID: %d\n", child);
    printf("Starting 3 monitor threads...\n");
 
    pthread_t t1, t2, t3;
    pthread_create(&t1, NULL, watch_time, NULL);
    pthread_create(&t2, NULL, watch_cpu,  NULL);
    pthread_create(&t3, NULL, watch_mem,  NULL);
 
    int status;
    waitpid(child, &status, 0);
    running = 0;
 
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
 
    printf("=== FINAL REPORT ===\n");
    printf("Reason: %s\n", reason);
    if (WIFSIGNALED(status))
        printf("Killed by signal: %d\n", WTERMSIG(status));
    else
        printf("Exit status: %d\n", WEXITSTATUS(status));
    printf("====================\n");
    return 0;
}