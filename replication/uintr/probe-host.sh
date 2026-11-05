#!/usr/bin/env bash
set -euo pipefail
export LC_ALL=C

if [[ $# != 2 || -z $1 ]]; then
    echo "Usage: bash probe-host.sh MACHINE_TYPE PATH_TO_CPUID" >&2
    exit 2
fi

printf 'machine_type=%s\n' "$1"
date -u '+timestamp=%Y-%m-%dT%H:%M:%SZ'
hostname
uname -smr
cat /etc/os-release
lscpu
lscpu --all --parse=CPU,SOCKET,CORE,NODE,ONLINE
taskset -pc "$$"
awk '/^(Cpus_allowed_list|Mems_allowed_list):/' "/proc/$$/status"
if [[ -n ${SLURM_JOB_ID:-} ]]; then
    printf 'slurm_job_id=%s partition=%s\n' "$SLURM_JOB_ID" "${SLURM_JOB_PARTITION:-unknown}"
fi
if [[ $(uname -m) != x86_64 ]]; then
    echo 'STOP: the kit requires x86-64 Linux.'
    exit 77
fi

probe_status=0
"$2" || probe_status=$?
case "$probe_status" in
    0) echo 'CPU capability exposed. Confirm the kernel interface before testing delivery.' ;;
    77) echo 'STOP: UINTR capability is unavailable in this allocation.' ;;
    *) echo "CPUID probe failed with exit $probe_status." >&2 ;;
esac
exit "$probe_status"
