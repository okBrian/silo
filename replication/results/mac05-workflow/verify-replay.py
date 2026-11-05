#!/usr/bin/env python3
"""Independently reconcile archived TSV metrics with the regenerated per-type CSV."""
import csv
import json
import math
from pathlib import Path
import sys

batch, report = map(Path, sys.argv[1:])
metadata = json.loads((batch / 'batch.json').read_text())
with (report / 'per-type.csv').open(newline='') as stream:
    table = {(row['run_id'], row['type']): row for row in csv.DictReader(stream)}
with (report / 'run-health.csv').open(newline='') as stream:
    health = {row['run_id']: row for row in csv.DictReader(stream)}
assert len(health) == len(metadata['run_ids']) == 8
metrics = {'queue': ('generated_ns', 'first_start_ns'),
           'global_queue': ('generated_ns', 'admitted_ns'),
           'local_queue': ('admitted_ns', 'first_start_ns'),
           'end_to_end': ('generated_ns', 'finished_ns'),
           'scheduled': ('scheduled_ns', 'finished_ns')}
commits = handoffs = shorts = 0
for run_id in metadata['run_ids']:
    directory = batch / run_id
    manifest = json.loads((directory / 'manifest.json').read_text())
    assert manifest['state'] == 'finished' and manifest['exit_status'] == 0
    with (directory / 'stdout.log').open(newline='') as stream:
        rows = [row for row in csv.DictReader(stream, delimiter='\t') if row['phase'] == 'measurement']
    assert all(row['outcome'] == 'committed' for row in rows)
    assert health[run_id]['unaccounted_at_exit'] == '0'
    assert health[run_id]['reconciled'] == 'True'
    commits += len(rows)
    shorts += sum(row['type'] != 'Q2' for row in rows)
    handoffs += sum(int(row['cooperative_yields']) for row in rows)
    summary, = [dict(item.split('=', 1) for item in line.split())
                for line in (directory / 'stderr.log').read_text().splitlines()
                if line.startswith('phase=measurement ')]
    cutoff = int(summary['cutoff_ns'])
    for kind in ('NewOrder', 'Payment', 'Q2'):
        members = [row for row in rows if row['type'] == kind]
        stats = table[run_id, kind]
        assert stats['policy'] == manifest['policy']
        assert stats['status'] == 'complete' and stats['reconciled'] == 'True'
        assert int(stats['generated']) == int(stats['committed']) == len(members)
        assert int(stats['aborts']) == sum(int(row['aborts']) for row in members)
        assert int(stats['attempts']) == sum(int(row['attempts']) for row in members)
        assert int(stats['not_committed']) == 0
        in_window = sum(int(row['finished_ns']) < cutoff for row in members)
        assert int(stats['commits_in_window']) == in_window
        assert float(stats['window_throughput_txn_s']) == in_window / manifest['phases_s']['measurement']
        for metric, (start, end) in metrics.items():
            values = sorted(int(row[end]) - int(row[start]) for row in members)
            assert int(stats[f'{metric}_n']) == len(values) > 0
            for name, quantile in [('p50', .5), ('p90', .9), ('p99', .99), ('p99_9', .999)]:
                expected = values[math.ceil(quantile * len(values)) - 1] / 1000
                assert float(stats[f'{metric}_{name}_us']) == expected
            assert stats[f'{metric}_p99_9_low_sample'] == str(len(values) < 1000)
print(f'PASS independent per-type replay: {len(table)} measurement rows; all five latency '
      f'distributions, four quantiles, sample flags, throughput, attempts, aborts and exit accounting')
print(f'Measurement totals: commits={commits} shorts={shorts} cooperative_handoffs={handoffs}')
