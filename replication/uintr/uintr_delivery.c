#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <x86gprintrin.h>

#include "cpuid_uintr.h"

/* UB-ADBLAB/ubuntu-linux-uintr hwe-6.11-next syscall_64.tbl. */
enum {
    REGISTER_HANDLER = 471,
    UNREGISTER_HANDLER = 472,
    VECTOR_FD = 473,
    REGISTER_SENDER = 474,
    VECTOR = 0x2a,
    ROUNDS = 16,
    WAIT_SECONDS = 2,
    CHUNK_STEPS = 1024,
    CHUNK_SUM = CHUNK_STEPS * (CHUNK_STEPS + 1) / 2,
};

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "handler needs inline lock-free atomics");

static const struct timespec poll_interval = { .tv_nsec = 1000000 };
static atomic_uint ready, hits, bad_vector, resumed;
static atomic_bool stop, receiver_failed;
static int vector_fd; /* Published by the first ready store. */

static void __attribute__((interrupt, target("general-regs-only")))
handle_uintr(struct __uintr_frame *frame, unsigned long long vector)
{
    (void)frame;
    if (vector != VECTOR)
        atomic_store_explicit(&bad_vector, 1, memory_order_relaxed);
    unsigned count = atomic_load_explicit(&hits, memory_order_relaxed);
    atomic_store_explicit(&hits, count + 1, memory_order_relaxed);
}

static bool has_uintr(void)
{
    unsigned int edx;
    return read_uintr_cpuid(&edx) && uintr_bit(edx);
}

static bool has_kernel_symbols(void)
{
    const char *const names[] = {
        "__x64_sys_uintr_register_handler",
        "__x64_sys_uintr_unregister_handler",
        "__x64_sys_uintr_vector_fd",
        "__x64_sys_uintr_register_sender",
    };
    const unsigned count = sizeof(names) / sizeof(names[0]);
    const unsigned required = (1u << count) - 1;
    unsigned found = 0;
    FILE *symbols = fopen("/proc/kallsyms", "r");
    if (!symbols) {
        perror("/proc/kallsyms");
        return false;
    }
    char line[512], name[128];
    while (found != required && fgets(line, sizeof(line), symbols)) {
        if (sscanf(line, "%*s %*c %127s", name) != 1)
            continue;
        for (unsigned i = 0; i < count; ++i) {
            if (strcmp(name, names[i]) == 0)
                found |= 1u << i;
        }
    }
    fclose(symbols);
    return found == required;
}

static bool pin_to_cpu(int cpu)
{
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) != 0) {
        perror("sched_setaffinity");
        return false;
    }
    return true;
}

static uint64_t add_chunk(uint64_t sum)
{
    for (unsigned i = 1; i <= CHUNK_STEPS; ++i) {
        sum += i;
        /* Keep the running value live across the interrupt window. */
        __asm__ volatile("" : "+r"(sum));
    }
    return sum;
}

static bool receive_round(unsigned round)
{
    uint64_t sum = add_chunk(0);
    uint64_t chunks = 1;
    /* This window contains only the computation and lock-free polling. */
    _stui();
    atomic_signal_fence(memory_order_seq_cst);
    atomic_store_explicit(&ready, round, memory_order_release);
    do {
        sum = add_chunk(sum);
        ++chunks;
    } while (atomic_load_explicit(&hits, memory_order_relaxed) < round &&
             !atomic_load_explicit(&stop, memory_order_acquire));
    _clui();

    if (atomic_load_explicit(&stop, memory_order_acquire))
        return false;

    /* Ack from ordinary code after the handler returned and work resumed. */
    sum = add_chunk(sum);
    uint64_t expected = (chunks + 1) * CHUNK_SUM;
    unsigned count = atomic_load_explicit(&hits, memory_order_relaxed);
    unsigned vector_error = atomic_load_explicit(&bad_vector, memory_order_relaxed);
    if (count != round || vector_error || sum != expected) {
        fprintf(stderr, "round %u: handlers=%u wrong_vector=%u checksum=%" PRIu64
                " expected=%" PRIu64 "\n", round, count, vector_error, sum, expected);
        atomic_store_explicit(&receiver_failed, true, memory_order_release);
        return false;
    }
    atomic_store_explicit(&resumed, round, memory_order_release);
    return true;
}

static void *receive(void *arg)
{
    int cpu = *(const int *)arg;
    if (!pin_to_cpu(cpu) || !has_uintr()) {
        fprintf(stderr, "receiver CPU %d: pinning or CPUID check failed\n", cpu);
        goto fail;
    }
    if (syscall(REGISTER_HANDLER, handle_uintr, 0u) < 0) {
        perror("uintr_register_handler");
        goto fail;
    }
    _clui();

    long fd = syscall(VECTOR_FD, (uint64_t)VECTOR, 0u);
    if (fd < 0) {
        perror("uintr_vector_fd");
        atomic_store_explicit(&receiver_failed, true, memory_order_release);
    } else {
        vector_fd = (int)fd;
        printf("receiver pinned to CPU %d, vector=0x%x\n", sched_getcpu(), VECTOR);
        for (unsigned round = 1; round <= ROUNDS; ++round) {
            if (atomic_load_explicit(&stop, memory_order_acquire) || !receive_round(round))
                break;
        }
        /* Keep the vector FD and handler live until the sender stops. */
        while (!atomic_load_explicit(&stop, memory_order_acquire))
            nanosleep(&poll_interval, NULL);
        if (close(vector_fd) < 0) {
            perror("close vector fd");
            atomic_store_explicit(&receiver_failed, true, memory_order_release);
        }
    }
    if (syscall(UNREGISTER_HANDLER, 0u) < 0) {
        perror("uintr_unregister_handler");
        goto fail;
    }
    return NULL;

fail:
    atomic_store_explicit(&receiver_failed, true, memory_order_release);
    return NULL;
}

static bool wait_for(atomic_uint *sequence, unsigned round, const char *phase)
{
    struct timespec deadline, now;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) {
        perror("clock_gettime");
        return false;
    }
    deadline.tv_sec += WAIT_SECONDS;
    for (;;) {
        if (atomic_load_explicit(&receiver_failed, memory_order_acquire))
            return false;
        if (atomic_load_explicit(sequence, memory_order_acquire) == round)
            return true;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
            perror("clock_gettime");
            return false;
        }
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            fprintf(stderr, "round %u: timeout waiting for %s (handlers=%u)\n",
                    round, phase, atomic_load_explicit(&hits, memory_order_relaxed));
            return false;
        }
        nanosleep(&poll_interval, NULL);
    }
}

static bool send_rounds(void)
{
    if (!wait_for(&ready, 1, "receiver readiness"))
        return false;
    long index = syscall(REGISTER_SENDER, vector_fd, 0u);
    if (index < 0) {
        perror("uintr_register_sender");
        return false;
    }

    unsigned sent = 0, acknowledged = 0;
    for (unsigned round = 1; round <= ROUNDS; ++round) {
        if (!wait_for(&ready, round, "receiver readiness"))
            break;
        _senduipi((unsigned long long)index);
        ++sent;
        if (!wait_for(&resumed, round, "post-handler checksum acknowledgment"))
            break;
        ++acknowledged;
    }
    printf("sends=%u acknowledgments=%u expected=%u handlers=%u\n", sent, acknowledged, ROUNDS,
           atomic_load_explicit(&hits, memory_order_relaxed));
    /* Process exit releases the sender entry. The reference kernel's
       unregister-sender syscall passes an uninitialized fd to fdput(). */
    return acknowledged == ROUNDS;
}

static int parse_cpu(const char *arg)
{
    char *end;
    long cpu = strtol(arg, &end, 10);
    if (end == arg || *end || cpu < 0 || cpu >= CPU_SETSIZE)
        return -1;
    return (int)cpu;
}

int main(int argc, char **argv)
{
    if (argc != 4 || strcmp(argv[1], "--patched-kernel") != 0) {
        fprintf(stderr, "Usage: %s --patched-kernel RECEIVER_CPU SENDER_CPU\n", argv[0]);
        return 2;
    }
    int receiver_cpu = parse_cpu(argv[2]), sender_cpu = parse_cpu(argv[3]);
    if (receiver_cpu < 0 || sender_cpu < 0 || receiver_cpu == sender_cpu) {
        fputs("Choose two distinct logical CPU IDs below CPU_SETSIZE.\n", stderr);
        return 2;
    }
    if (!has_uintr()) {
        puts("SKIP: CPUID UINTR unavailable. No UINTR instructions or syscalls attempted.");
        return 77;
    }
    if (!has_kernel_symbols()) {
        fputs("Required patched-kernel syscall symbols were not found in /proc/kallsyms.\n",
              stderr);
        return 1;
    }

    cpu_set_t allowed;
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        perror("sched_getaffinity");
        return 1;
    }
    if (!CPU_ISSET(receiver_cpu, &allowed) || !CPU_ISSET(sender_cpu, &allowed)) {
        fputs("Both CPUs must be within the initial affinity mask.\n", stderr);
        return 2;
    }
    if (!pin_to_cpu(sender_cpu) || !has_uintr()) {
        fprintf(stderr, "sender CPU %d: pinning or CPUID check failed\n", sender_cpu);
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("sender pinned to CPU %d, interface=UB-ADBLAB patched kernel\n", sched_getcpu());
    pthread_t receiver;
    int error = pthread_create(&receiver, NULL, receive, &receiver_cpu);
    if (error != 0) {
        fprintf(stderr, "pthread_create: %s\n", strerror(error));
        return 1;
    }
    bool delivered = send_rounds();
    atomic_store_explicit(&stop, true, memory_order_release);
    error = pthread_join(receiver, NULL);
    if (error != 0) {
        fprintf(stderr, "pthread_join: %s\n", strerror(error));
        return 1;
    }
    if (!delivered || atomic_load_explicit(&receiver_failed, memory_order_acquire))
        return 1;
    puts("PASS: all interrupts handled, receiver checksums verified, receiver cleaned up.");
    return 0;
}
