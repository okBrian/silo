# Local notification event model

MAC-10 is a **software notification model**, consumed by the checkpoint test
scheduler. It is separate from `linux-signal` and from native Intel UINTR.

## Publication and service contract

The producer first admits requests to fixed per-worker queues, then invokes
`HighPublished` once for each nonempty per-worker admission pass. The callback
can send a notification through `NotificationModel`; sending neither admits nor
executes a request. The authoritative pending-work state is the high queue.
`Driver::deliver_notification` sets an atomic hint. At an eligible completed
read, the scheduler consumes that hint before examining the queue. A nonempty
queue can be serviced even if delivery was withheld. A hint with an empty queue
cannot create work. Nested protection defers switching until a later safe read;
leaving protection alone does not switch. This is checkpoint service, with
voluntary context switches, not asynchronous UINTR entry/return validation.

`NotificationModel` has one pending bit per destination. Multiple sends before
delivery coalesce. Ordinary delivery consumes the pending bit before invoking
the receiver, allowing a new send during the callback to remain pending. An
ordinary delivery with no bit is a no-op; explicitly unsolicited delivery can
invoke the receiver without a bit. Such an event models a duplicate or spurious
hint, not a claim that any particular UINTR interface generates one.

## Clock and event schedules

`NotificationSchedule` accepts ordered `{logical_time, kind, worker}` events.
Kinds are `send=0`, `deliver=1`, `unsolicited=2`, `close=3`. `run_through(t)`
applies all events due at or before `t`, retaining insertion order at equal
times. No wall-clock timer or background thread executes events. Invalid worker
IDs, unsorted event times and backwards clock advances are rejected. Caller
synchronization transfers a schedule between producer and worker; it has one
coordinator at a time and does not support recursive execution.

The transport-only trace covers two destinations, delivery before any send,
delayed/coalesced sends, independent routing, duplicate delivery, equal-time
delivery-before-send, cancellation of a pending bit and events after close. A
separate re-arm trace sends again from the receiver callback.

The two shared receiver fixtures use these exact schedules (logical nanoseconds):

| Time | Early delivery | Delayed delivery |
| --- | --- | --- |
| 110 | Send twice, then deliver | Send twice |
| 120 | Ordinary no-op delivery, unsolicited delivery | Deliver pending bit, unsolicited delivery |
| 130 | Close after producer/worker join | Same |
| 140 | Attempt delivery, attempt send after receiver destruction | Same |
| 150 | Attempt unsolicited delivery | Same |

At 110, the active low computation checks twice inside nested protection, then
services two admitted highs outside protection. The first high conflicts once;
both commit. The same low attempt resumes with its oracle checksum. At 120 the
high queue is empty, so the stale/duplicate hints cause no additional switch.
These fixtures run with synthetic arithmetic and the real Silo Q2 adapter with
intervening committed stock writes. All model paths require zero real handlers
and zero asynchronous captures. Each receiver fixture records one voluntary
handoff/resumption, two deferrals, two high commits and three high attempts.

MAC-07's shared transition matrix additionally injects events at high entry,
the high empty check and low return, with ordinary, duplicate and delayed/
coalesced modes. Its partial-admission and drain-expiry cases retain rejected
and cancelled requests. MAC-08's shared equality/overrun/recovery/retry/reset
fixture now consumes a scheduled send/deliver pair at each actual admission
publication (initial admission at 110, recovery begins at 140). A refill can
span multiple passes as capacity and budget recover. Existing
0%/100% budget cases remain separate boundary-policy checks.

## Counters and lifetime

Event traces retain attempted sends, coalesced sends, attempted deliveries,
receiver deliveries, unsolicited deliveries, pending/closed state, dropped
sends/deliveries and pending-bit cancellations. Attempted sends include those
dropped after close. A coalesced send counts only an open destination with an
already pending bit. Scheduler records separately retain publications,
admissions, consumed hints, queue fallbacks, deferrals, switches/resumptions,
real signal handlers/captures, transaction attempts and terminal outcomes.

`close(worker)` is deliberately a **quiescent** lifecycle operation. Join all
senders and deliverers before closing; close every destination before destroying
its receiver. Close discards a pending bit once and is idempotent. Subsequent
scheduled events are counted/dropped without invoking the receiver. This is
not concurrent unregister, kernel descriptor reclamation, or a model of racing
hardware teardown. The ordinary send/deliver transport retains its atomic
pending bit; cross-field statistics are inspected after synchronization.

## Scope and reproduction

Run in a disposable initialized x86-64 Linux/glibc source copy:

```bash
bash replication/tests/validate_notification_model.sh /mac10-evidence
python3 -B replication/tests/verify_notification_model.py /mac10-evidence
```

The script builds all three controlled suites in debug, optimized and UBSan
variants, repeats each optimized suite 20 times, runs driver/tool regressions,
fresh-builds the Silo fixture and exercises checkpoint and separately labeled
signal regression modes. The Python verifier independently replays every
model-event counter and reconciles terminal IDs, counts, attempts, timestamps,
checksums and outcomes. See [actual commands and results](../results/mac10-notification-model/README.md).

Assumptions include one vector/pending bit per destination, explicit delivery
times, fixed routing and the project's publish-before-notify contract. The
model makes no claim about all architectural/kernel UINTR behavior, ordering
of real instructions, masking internals, UPID/vector tables, native register
state or hardware latency. The standalone [UINTR kit](../uintr/README.md) is
unchanged. I12 remains **prepared**, not hardware-validated. `SENDUIPI`, kernel
access, native context preservation and delivery costs remain MAC-06.
