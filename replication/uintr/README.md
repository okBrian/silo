# I03 and I12 Linux hardware kit

The kit needs x86-64 Linux, GCC with `-muintr` support, glibc development headers, GNU Make, and the Linux tools `lscpu`, `taskset`, and `timeout`. Copy this directory to the test host. Build and run inside the allocation that will execute the smoke. Silo, Masstree, and benchmark setup are unnecessary for this test.

## I03 · Check CPU exposure before host setup

In Bash, run these commands from the copied kit directory. Set `machine_type` to the exact cloud instance type from the provisioning record, or to the vendor and machine model on a physical host. Include the allocation or virtualization type if applicable. Use a fresh evidence directory for each attempt.

```bash
set -o pipefail
machine_type='REPLACE_WITH_EXACT_INSTANCE_TYPE_OR_MACHINE_MODEL'
evidence=$(mktemp -d "${TMPDIR:-/tmp}/silo-uintr.XXXXXX")
printf 'evidence=%s\n' "$evidence"
probe_status=0
(
    { gcc --version && getconf GNU_LIBC_VERSION && make CC=gcc cpuid; } 2>&1 |
        tee "$evidence/probe-build.log" || exit $?
    bash probe-host.sh "$machine_type" ./cpuid 2>&1 | tee "$evidence/host-probe.log"
) || probe_status=$?
printf '%s\n' "$probe_status" > "$evidence/probe.exit"
```

Exit `77` means the feature is unavailable. Save the evidence and stop UINTR setup for this allocation. Exit `0` means CPUID exposes UINTR. Use the recorded topology and affinity to plan placement and confirm the running kernel interface with the host operator. Any other exit is a probe error to resolve before proceeding.

## I12 · Verify hardware delivery and return to computation

This test targets the UB-ADBLAB Ubuntu 6.11 patched kernel, reference revision `1ac7ea6ddc193f6557e9404a48724f3276f88270`. Confirm that the booted kernel came from that revision or verify its matching syscall table and implementation before invoking the test. It uses syscalls 471–474 for handler registration, handler teardown, vector-FD creation, and sender registration. The binary checks CPUID and the corresponding `/proc/kallsyms` names before invoking the ABI. Record the running kernel's source revision from the operator or build record. [Upstream kernel](https://github.com/UB-ADBLAB/ubuntu-linux-uintr/tree/1ac7ea6ddc193f6557e9404a48724f3276f88270) · [Syscall table](https://github.com/UB-ADBLAB/ubuntu-linux-uintr/blob/1ac7ea6ddc193f6557e9404a48724f3276f88270/arch/x86/entry/syscalls/syscall_64.tbl)

Choose two online logical CPUs within the logged affinity mask. Prefer different `(SOCKET, CORE)` pairs on one socket when available. Record the placement if only SMT siblings are available. With fewer than two allowed logical CPUs, obtain a larger allocation. The executable pins both threads and checks that the requested CPUs are in the original mask.

Run in the same shell as the I03 step after confirming the compatible kernel. If you rebooted or changed allocations, rerun I03 in a fresh evidence directory first.

```bash
receiver_cpu=0 # Replace both IDs with the chosen pair.
sender_cpu=1
kernel_revision='REPLACE_WITH_CONFIRMED_SOURCE_REVISION'
delivery_status=0
(
    set -o pipefail
    test "$(cat "$evidence/probe.exit")" = 0 || exit 1
    {
        gcc --version &&
        getconf GNU_LIBC_VERSION &&
        make CC=gcc uintr_delivery &&
        sha256sum Makefile cpuid.c cpuid_uintr.h probe-host.sh uintr_delivery.c cpuid uintr_delivery
    } 2>&1 | tee "$evidence/delivery-build.log" || exit $?
    {
        printf 'kernel_revision=%s receiver_cpu=%s sender_cpu=%s\n' \
            "$kernel_revision" "$receiver_cpu" "$sender_cpu"
        taskset -c "$receiver_cpu" ./cpuid &&
        taskset -c "$sender_cpu" ./cpuid &&
        time timeout --kill-after=2s 30s ./uintr_delivery --patched-kernel "$receiver_cpu" "$sender_cpu"
    } 2>&1 | tee "$evidence/delivery.log"
) || delivery_status=$?
printf '%s\n' "$delivery_status" > "$evidence/delivery.exit"
```

The sender issues 16 interrupts, with one send outstanding at a time. The receiver starts an integer calculation before enabling delivery. Its handler records the vector and increments the delivery count. After the handler returns, ordinary receiver code continues the calculation, verifies its checksum against the sum of `1..1024` per chunk, and acknowledges that round. A sender wait expires after two seconds, and the command has a 30-second overall timeout.

The receiver disables delivery, closes its vector FD, unregisters its handler, and joins before success. Sender cleanup uses process exit, which releases its user-interrupt target table. The reference kernel's explicit sender-unregister syscall passes an uninitialized FD to `fdput()`. [Process-exit cleanup](https://github.com/UB-ADBLAB/ubuntu-linux-uintr/blob/1ac7ea6ddc193f6557e9404a48724f3276f88270/arch/x86/kernel/uintr.c#L319-L369) · [Sender-unregister implementation](https://github.com/UB-ADBLAB/ubuntu-linux-uintr/blob/1ac7ea6ddc193f6557e9404a48724f3276f88270/arch/x86/kernel/uintr.c#L798-L826)

Record I12 as validated only with exit `0`, 16 sends, 16 acknowledgments, 16 handlers, and the `PASS` line from the hardware run. Preserve the full logs and exit files on failure too.
