# MAC-03 · I03 and I12 kit evidence

November 4, 2026 · **Emulated Linux build and capability checks** · `erin/local-replication-evidence` based on `3f358dcfe76b888bfadc82f51f60f6441fe1e003`. The [build log](build.log) records hashes for the exact kit files and the two binaries.

The kit built in a fresh `linux/amd64` container inside the existing x86-64 Colima/QEMU guest. It used the cached Ubuntu 22.04 image at digest `sha256:076e7bf82319619f28dd0aa9c02ab34b055ad37100395b4f49a2ffba04fb5e11`, GCC 11.4, glibc 2.35, and kernel `6.8.0-117-generic`. The guest presented a QEMU TCG CPU and two vCPUs in distinct virtual cores. The container had both CPUs in its allowed mask.

| Check | What ran | Result |
| --- | --- | --- |
| Build | Installed `gcc libc6-dev make binutils` in the container. Copied the standalone kit into a fresh directory and ran `make CC=gcc`. | Both executables built with warnings treated as errors. [Build commands and output](build.log) |
| I03 capability | Ran `bash probe-host.sh` with the Colima guest/container description and `./cpuid`. Also ran `taskset -c 0 ./cpuid` and `taskset -c 1 ./cpuid`. | All returned `CPUID.7.0.EDX=0x00000010 UINTR=0` and exited 77. [Host inventory and CPUID](host-probe.log) · [Probe exit](host-probe.exit) · [Pinned probes](checks.log) |
| I12 startup guard | Ran `timeout --kill-after=2s 30s ./uintr_delivery --patched-kernel 0 1`. Also exercised missing arguments, an invalid CPU ID, and duplicate CPU IDs. | The supported syntax reached the CPUID guard and exited 77 before any UINTR setup. Invalid invocations exited 2. [Check output](checks.log) |
| Static checks | Ran `bash -n` on the probe script and both runbook snippets. Disassembled the delivery binary with `objdump -d`. | Shell syntax passed. The handler had a `uiret` return and no calls. The binary contained `stui`, `clui`, and `senduipi`. [Check output](checks.log) |

The prior AWS VM result is reported as `UINTR=0` in the [local plan](../../LOCAL_REPLICATION_PLAN.md). This run covered the Colima guest. I12 is prepared for the [patched-kernel hardware session](../../uintr/README.md). Native registration, handler delivery, and receiver resumption still need a qualified Intel Linux host.
