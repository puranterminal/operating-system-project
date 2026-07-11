#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdarg.h>

#define TIME_LIMIT_SEC     10
#define CPU_LIMIT_PERCENT  80
#define MEM_LIMIT_KB       102400
#define POLL_INTERVAL_MS   500
#define LOG_FILE           "sandbox.log"

#define REASON_NORMAL   0
#define REASON_TIMEOUT  1
#define REASON_CPU      2
#define REASON_MEMORY   3

typedef struct {
    pid_t           child_pid;
    atomic_int      child_alive;
    atomic_int      termination_reason;
    pthread_mutex_t lock;
    double          cpu_usage;
    long            mem_kb;
    time_t          start_time;
} sandbox_state_t;

static sandbox_state_t g_state;
static FILE           *g_log = NULL;

static void sandbox_log(const char *fmt, ...)
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[16];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);

    va_list ap;
    va_start(ap, fmt);
    printf("[%s] ", ts);
    vprintf(fmt, ap);
    printf("\n");
    fflush(stdout);
    va_end(ap);

    if (g_log) {
        va_start(ap, fmt);
        fprintf(g_log, "[%s] ", ts);
        vfprintf(g_log, fmt, ap);
        fprintf(g_log, "\n");
        fflush(g_log);
        va_end(ap);
    }
}

static void terminate_child(int reason, const char *why)
{
    int expected = 1;
    if (!atomic_compare_exchange_strong(
            &g_state.child_alive, &expected, 0)) {
        return;
    }
    atomic_store(&g_state.termination_reason, reason);
    sandbox_log("[SANDBOX] TERMINATING PID=%d — %s",
                g_state.child_pid, why);
    kill(g_state.child_pid, SIGKILL);
}

static double read_cpu_percent(pid_t pid)
{
    static long              prev_ticks = 0;
    static struct timespec   prev_ts    = {0, 0};

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return -1.0;

    int   ipid; char comm[256]; char state;
    int   ppid, pgrp, session, tty, tpgid;
    unsigned int flags;
    unsigned long minflt, cminflt, majflt, cmajflt;
    long  utime, stime;

    int parsed = fscanf(f,
        "%d %255s %c %d %d %d %d %d %u "
        "%lu %lu %lu %lu %ld %ld",
        &ipid, comm, &state,
        &ppid, &pgrp, &session, &tty, &tpgid, &flags,
        &minflt, &cminflt, &majflt, &cmajflt,
        &utime, &stime);
    fclose(f);

    if (parsed != 15) return -1.0;

    long           total_ticks = utime + stime;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    double cpu_pct = 0.0;
    if (prev_ts.tv_sec > 0) {
        double elapsed_s =
            (double)(now.tv_sec  - prev_ts.tv_sec) +
            (double)(now.tv_nsec - prev_ts.tv_nsec) / 1e9;
        long   delta = total_ticks - prev_ticks;
        long   hz    = sysconf(_SC_CLK_TCK);
        if (elapsed_s > 0 && hz > 0)
            cpu_pct = (delta / (double)hz) / elapsed_s * 100.0;
    }

    prev_ticks = total_ticks;
    prev_ts    = now;
    return cpu_pct;
}

static long read_mem_kb(pid_t pid)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            long kb = 0;
            sscanf(line + 6, " %ld", &kb);
            fclose(f);
            return kb;
        }
    }
    fclose(f);
    return -1;
}

/* ── THREAD 1: TIME MONITOR ─────────────────────────────────── */
static void *monitor_time(void *arg)
{
    (void)arg;
    sandbox_log("[TIME ] Monitor started. Limit: %d seconds",
                TIME_LIMIT_SEC);

    struct timespec interval = {
        .tv_sec  = 0,
        .tv_nsec = POLL_INTERVAL_MS * 1000000L
    };

    while (atomic_load(&g_state.child_alive)) {
        nanosleep(&interval, NULL);
        if (!atomic_load(&g_state.child_alive)) break;

        time_t elapsed = time(NULL) - g_state.start_time;
        sandbox_log("[TIME ] Elapsed: %lds / %ds",
                    (long)elapsed, TIME_LIMIT_SEC);

        if (elapsed >= TIME_LIMIT_SEC) {
            sandbox_log("[TIME ] Limit exceeded!");
            terminate_child(REASON_TIMEOUT, "TIME LIMIT EXCEEDED");
            break;
        }
    }

    sandbox_log("[TIME ] Thread exiting.");
    return NULL;
}

/* ── THREAD 2: CPU MONITOR ──────────────────────────────────── */
static void *monitor_cpu(void *arg)
{
    (void)arg;
    sandbox_log("[CPU  ] Monitor started. Limit: %d%%",
                CPU_LIMIT_PERCENT);

    struct timespec interval = {
        .tv_sec  = 0,
        .tv_nsec = POLL_INTERVAL_MS * 1000000L
    };

    read_cpu_percent(g_state.child_pid);

    while (atomic_load(&g_state.child_alive)) {
        nanosleep(&interval, NULL);
        if (!atomic_load(&g_state.child_alive)) break;

        double cpu = read_cpu_percent(g_state.child_pid);
        if (cpu < 0.0) break;

        pthread_mutex_lock(&g_state.lock);
        g_state.cpu_usage = cpu;
        pthread_mutex_unlock(&g_state.lock);

        sandbox_log("[CPU  ] Usage: %.1f%%", cpu);

        if (cpu > CPU_LIMIT_PERCENT) {
            sandbox_log("[CPU  ] Limit exceeded: %.1f%% > %d%%",
                        cpu, CPU_LIMIT_PERCENT);
            terminate_child(REASON_CPU, "CPU LIMIT EXCEEDED");
            break;
        }
    }

    sandbox_log("[CPU  ] Thread exiting.");
    return NULL;
}

/* ── THREAD 3: MEMORY MONITOR ───────────────────────────────── */
static void *monitor_mem(void *arg)
{
    (void)arg;
    sandbox_log("[MEM  ] Monitor started. Limit: %d KB",
                MEM_LIMIT_KB);

    struct timespec interval = {
        .tv_sec  = 0,
        .tv_nsec = POLL_INTERVAL_MS * 1000000L
    };

    while (atomic_load(&g_state.child_alive)) {
        nanosleep(&interval, NULL);
        if (!atomic_load(&g_state.child_alive)) break;

        long mem = read_mem_kb(g_state.child_pid);
        if (mem < 0) break;

        pthread_mutex_lock(&g_state.lock);
        g_state.mem_kb = mem;
        pthread_mutex_unlock(&g_state.lock);

        sandbox_log("[MEM  ] Usage: %ld KB", mem);

        if (mem > MEM_LIMIT_KB) {
            sandbox_log("[MEM  ] Limit exceeded: %ld KB > %d KB",
                        mem, MEM_LIMIT_KB);
            terminate_child(REASON_MEMORY, "MEMORY LIMIT EXCEEDED");
            break;
        }
    }

    sandbox_log("[MEM  ] Thread exiting.");
    return NULL;
}

/* ── MAIN ───────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: ./sandbox <binary>\n");
        return EXIT_FAILURE;
    }

    g_log = fopen(LOG_FILE, "a");

    sandbox_log("[SBOX ] ============================================");
    sandbox_log("[SBOX ] User Space Malware Analysis Sandbox");
    sandbox_log("[SBOX ] Binary   : %s",     argv[1]);
    sandbox_log("[SBOX ] Time lim : %ds",    TIME_LIMIT_SEC);
    sandbox_log("[SBOX ] CPU lim  : %d%%",   CPU_LIMIT_PERCENT);
    sandbox_log("[SBOX ] Mem lim  : %d KB",  MEM_LIMIT_KB);
    sandbox_log("[SBOX ] Sandbox  : PID=%d", getpid());

    memset(&g_state, 0, sizeof(g_state));
    atomic_init(&g_state.child_alive, 1);
    atomic_init(&g_state.termination_reason, REASON_NORMAL);
    pthread_mutex_init(&g_state.lock, NULL);
    g_state.start_time = time(NULL);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return EXIT_FAILURE;
    }

    if (pid == 0) {
        char *envp[] = {
            "PATH=/usr/bin:/bin",
            "HOME=/tmp",
            NULL
        };
        execve(argv[1], &argv[1], envp);
        fprintf(stderr, "[CHILD] execve failed: %s\n", strerror(errno));
        _exit(EXIT_FAILURE);
    }

    g_state.child_pid = pid;
    sandbox_log("[SBOX ] Child spawned: PID=%d", pid);
    sandbox_log("[SBOX ] Launching 3 monitoring threads...");

    pthread_t t_time, t_cpu, t_mem;

    pthread_create(&t_time, NULL, monitor_time, NULL);
    pthread_create(&t_cpu,  NULL, monitor_cpu,  NULL);
    pthread_create(&t_mem,  NULL, monitor_mem,  NULL);

    int status;
    waitpid(pid, &status, 0);

    atomic_store(&g_state.child_alive, 0);

    pthread_join(t_time, NULL);
    pthread_join(t_cpu,  NULL);
    pthread_join(t_mem,  NULL);

    time_t elapsed = time(NULL) - g_state.start_time;
    int    reason  = atomic_load(&g_state.termination_reason);

    const char *reason_str[] = {
        "NORMAL COMPLETION",
        "TIME LIMIT EXCEEDED",
        "CPU LIMIT EXCEEDED",
        "MEMORY LIMIT EXCEEDED"
    };

    sandbox_log("[SBOX ] ============================================");
    sandbox_log("[SBOX ] SANDBOX FINAL REPORT");
    sandbox_log("[SBOX ] Binary          : %s", argv[1]);
    sandbox_log("[SBOX ] Child PID       : %d", pid);
    sandbox_log("[SBOX ] Total runtime   : %lds", (long)elapsed);

    pthread_mutex_lock(&g_state.lock);
    sandbox_log("[SBOX ] Peak CPU usage  : %.1f%%", g_state.cpu_usage);
    sandbox_log("[SBOX ] Peak memory     : %ld KB", g_state.mem_kb);
    pthread_mutex_unlock(&g_state.lock);

    if (WIFEXITED(status))
        sandbox_log("[SBOX ] Exit status     : %d", WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        sandbox_log("[SBOX ] Killed by signal: %d (%s)",
                    WTERMSIG(status), strsignal(WTERMSIG(status)));

    sandbox_log("[SBOX ] Termination     : %s",
                reason_str[reason < 4 ? reason : 0]);
    sandbox_log("[SBOX ] ============================================");

    pthread_mutex_destroy(&g_state.lock);
    if (g_log) fclose(g_log);

    return EXIT_SUCCESS;
}