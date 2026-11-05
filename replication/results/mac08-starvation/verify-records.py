#!/usr/bin/env python3
"""Replay raw starvation terminal records and pressure batches independently."""
import csv
import hashlib
import io
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent
controlled = []
logs = [*(ROOT.glob('starvation-*.log')), *(ROOT.glob('repeat-*.log')), ROOT / 'tpcc-mixed.log']
for path in sorted(logs):
    if not path.exists():
        continue
    lines = path.read_text().splitlines()
    for i, line in enumerate(lines):
        if not line.startswith('CASE ') or not any(x in line.split()[1] for x in ('starvation', 'limits')):
            continue
        meta = dict(token.split('=', 1) for token in line.split()[2:])
        end = next(j for j in range(i + 2, len(lines)) if lines[j].startswith('phase='))
        rows = list(csv.DictReader(io.StringIO('\n'.join(lines[i + 1:end])), delimiter='\t'))
        ids = sorted(int(r['request_id']) for r in rows)
        assert ids == list(range(len(rows))), (path, meta, ids)
        assert all(r['outcome'] in ('committed', 'cancelled_shutdown') for r in rows)
        for r in rows:
            assert int(r['finished_ns']) >= int(r['generated_ns'])
            if r['outcome'] == 'committed':
                assert int(r['generated_ns']) <= int(r['admitted_ns']) <= int(r['first_start_ns']) <= int(r['finished_ns'])
                assert int(r['attempts']) == int(r['aborts']) + 1
                if r['type'] == 'Q2':
                    assert r['q2_checksum'] == meta['checksum'] and int(r['low_service_ns']) > 0
        highs = [r for r in rows if r['priority'] == 'high']
        cancelled = sum(r['outcome'] == 'cancelled_shutdown' for r in rows)
        assert sum(r['outcome'] == 'committed' for r in highs) == int(meta['high_commits'])
        if 'expire' in meta:
            assert len(rows) == 10 and len(highs) == 8
            assert cancelled == (3 if meta['expire'] == '1' else 0)
            active = next(r for r in rows if r['request_id'] == '0')
            assert active['attempts'] == '2' and active['aborts'] == '1'
            assert meta['retry_retained'] == meta['exact_threshold_dispatch'] == '1'
            assert meta['forced_returns'] == '5' and meta['high_service_ns'] == '60'
        else:
            assert len(rows) == 6 and len(highs) == 4 and cancelled == 0
            assert int(meta['active_highs']) == (1 if meta['threshold'] == '0' else 4)
        controlled.append({'log': path.name, 'case': line.split()[1], **meta, 'records': len(rows)})
assert len(controlled) == 96, len(controlled)
pressure = []
for threshold in (25, 100):
    base = ROOT / f'pressure-{threshold}'
    for manifest_path in sorted(base.glob('*/*/manifest.json')):
        manifest = json.loads(manifest_path.read_text())
        assert manifest['exit_status'] == 0 and manifest['state'] == 'finished'
        assert manifest['starvation_threshold'] == threshold
        assert manifest['starvation_clock'] == 'CLOCK_MONOTONIC nanoseconds'
        rows = list(csv.DictReader((manifest_path.parent / 'stdout.log').open(), delimiter='\t'))
        assert len(rows) == len({r['request_id'] for r in rows})
        assert sorted(int(r['request_id']) for r in rows) == list(range(len(rows)))
        assert all(r['outcome'] in ('committed', 'rejected_queue_full', 'cancelled_shutdown', 'retry_exhausted', 'user_aborted') for r in rows)
        for r in rows:
            if r['outcome'] == 'committed':
                assert int(r['admitted_ns']) <= int(r['first_start_ns']) <= int(r['finished_ns'])
                assert int(r['attempts']) == int(r['aborts']) + 1
        low = [r for r in rows if r['type'] == 'Q2' and r['outcome'] == 'committed']
        high = [r for r in rows if r['priority'] == 'high']
        assert low and len(high) == 2999 * 4 * manifest['workers']
        assert all(r['q2_checksum'] != 'null' and int(r['low_service_ns']) > 0 for r in low)
        counters = [dict(t.split('=', 1) for t in line.split()[1:]) for line in (manifest_path.parent / 'stderr.log').read_text().splitlines() if line.startswith('starvation phase=measurement')]
        forced = sum(int(c['forced_low_returns']) for c in counters)
        blocks = sum(int(c['admission_blocks']) for c in counters)
        assert (forced > 0 and blocks > 0) if threshold == 25 else forced == blocks == 0
        cutoff = int(next(line.split('cutoff_ns=')[1].split()[0] for line in (manifest_path.parent / 'stderr.log').read_text().splitlines() if line.startswith('phase=measurement ')))
        in_window = sum(int(r['finished_ns']) < cutoff for r in low)
        assert in_window > 0
        pressure.append({'threshold': threshold, 'workers': manifest['workers'], 'q2_commits': len(low), 'q2_in_window': in_window,
                         'short_arrivals': len(high), 'short_commits': sum(r['outcome'] == 'committed' for r in high),
                         'rejected': sum(r['outcome'] == 'rejected_queue_full' for r in rows), 'cancelled': sum(r['outcome'] == 'cancelled_shutdown' for r in rows),
                         'aborts': sum(int(r['aborts']) for r in rows), 'forced_returns': forced, 'admission_blocks': blocks,
                         'low_service_ns': sum(int(r['low_service_ns']) for r in low)})
assert len(pressure) == 4
result = {'controlled_cases': len(controlled), 'controlled_records': sum(c['records'] for c in controlled),
          'pressure': pressure, 'logs_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in logs if p.exists()}}
(ROOT / 'reconciliation.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
