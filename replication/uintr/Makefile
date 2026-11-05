CFLAGS ?= -O2 -g
WARNINGS = -std=gnu11 -Wall -Wextra -Werror

all: cpuid uintr_delivery

cpuid: cpuid.c cpuid_uintr.h
	$(CC) $(CFLAGS) $(WARNINGS) cpuid.c -o $@

uintr_delivery: uintr_delivery.c cpuid_uintr.h
	$(CC) $(CFLAGS) $(WARNINGS) -muintr -mno-red-zone -pthread uintr_delivery.c -o $@

.PHONY: all
