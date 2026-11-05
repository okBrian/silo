#ifndef SILO_CPUID_UINTR_H
#define SILO_CPUID_UINTR_H

#include <cpuid.h>

static inline int read_uintr_cpuid(unsigned int *edx)
{
    unsigned int eax, ebx, ecx;
    return __get_cpuid_count(7, 0, &eax, &ebx, &ecx, edx);
}

static inline unsigned int uintr_bit(unsigned int edx)
{
    return (edx >> 5) & 1u;
}

#endif
