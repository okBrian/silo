#include <stdio.h>

#include "cpuid_uintr.h"

int main(void)
{
    unsigned int edx;
    if (!read_uintr_cpuid(&edx)) {
        puts("CPUID leaf 7 unavailable");
        return 77;
    }
    printf("CPUID.7.0.EDX=0x%08x UINTR=%u\n", edx, uintr_bit(edx));
    return uintr_bit(edx) ? 0 : 77;
}
