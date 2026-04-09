# KickMsg Stress Test Scenarios

All scenarios run as part of `kickmsg_stress_test`. Use `endurance.sh` for
extended runs. TSAN builds scale message counts by 100x to keep runtime
manageable.

## Treiber stack (`treiber.cc`)

**What**: 8 threads × 100K pop/push cycles on the lock-free free-stack.

**Why**: Validates ABA safety of the tagged-pointer Treiber stack under high
contention. Every cycle pops a slot, writes to it, and pushes it back.

**Config**: pool=64, 8 threads.

**Failure means**: ABA bug in the Treiber stack, corruption of the free-list
linked structure, or slot duplication.

## Subscriber churn (`churn.cc`)

**What**: 4 subscriber threads repeatedly join and leave (5 rounds each) while
a publisher sends continuously.

**Why**: Exercises the full ring lifecycle: Free → Live → Draining → Free.
Tests drain_unconsumed correctness, in_flight quiescence spin, and ring reuse.

**Config**: max_subs=4, ring=32, pool=128, 10K messages.

**Failure means**: Double-decrement on drain, ring state corruption on reuse,
or refcount leak.

## GC recovery (`gc_recovery.cc`)

**What**: Manually poisons a ring entry with LOCKED_SEQUENCE and orphans a slot
with refcount > 0. Calls `repair_locked_entries()` and `reclaim_orphaned_slots()`
and verifies they fix both issues.

**Why**: Validates the explicit recovery API that operators use after a publisher
crash.

**Failure means**: GC does not repair the poisoned entry, does not reclaim the
orphaned slot, or corrupts the free-stack.

## Fairness (`fairness.cc`)

**What**: 1 publisher × 16 subscribers, 100K messages. Measures the receive
distribution spread (min vs max across subscribers).

**Why**: Verifies that per-subscriber rings provide equal service — a slow
subscriber should not starve fast ones.

**Config**: ring=256, pool=512 (large enough for no eviction pressure).

**Failure means**: Extreme receive imbalance, zero-receive subscriber, or
data corruption.

## MPMC (`mpmc.cc`)

**What**: Parameterized multi-publisher multi-subscriber stress with 7 configs:

| Pubs | Subs | Msgs/Pub | Pool | Ring | Mode |
|------|------|----------|------|------|------|
| 2 | 4 | 100K | 256 | 64 | copy |
| 8 | 8 | 50K | 128 | 32 | copy |
| 1 | 1 | 500K | 64 | 16 | copy |
| 16 | 16 | 20K | 32 | 8 | copy |
| 2 | 4 | 100K | 256 | 64 | zerocopy |
| 8 | 8 | 50K | 128 | 32 | zerocopy |
| 16 | 16 | 20K | 32 | 8 | zerocopy |

**Why**: Core correctness test. Validates payload integrity (magic + checksum),
per-publisher sequence ordering, refcount lifecycle, and pool integrity across
a range of contention levels and both receive modes.

**Failure means**: Data corruption, per-publisher reorder, refcount leak, or
Treiber stack corruption.

## Pool exhaustion (`pool_exhaustion.cc`)

**What**: 8 publishers fight over 8 slots while 4 subscribers consume slowly
(1us sleep between receives).

**Why**: Maximizes the -EAGAIN / retry rate. Tests Treiber stack under extreme
pop/push frequency and refcount correctness when most sends fail.

**Config**: pool=8, ring=4, max_subs=4, 10K msgs per publisher.

**Failure means**: Treiber ABA under extreme cycling, refcount underflow from
batch excess, or double-push corrupting the free-stack.

## Live repair (`live_repair.cc`)

**What**: 4 publishers + 4 subscribers running for 2 seconds. A background
injector periodically poisons ring entries with LOCKED_SEQUENCE. A background
healer calls `diagnose()` + `repair_locked_entries()`.

**Why**: Validates the claim that `repair_locked_entries()` is safe under live
traffic. The repair does a plain store to `sequence` while publishers may be
CAS-ing the same entry — this test verifies the "benign double-store" argument.

**Failure means**: Data corruption caused by repair racing with a live publisher,
or repair failing to unblock a poisoned entry.

## Single-slot ring (`edge_cases.cc`)

**What**: ring=2 (smallest valid power-of-2), pool=32, 4 publishers × 10K
messages.

**Why**: Every publish wraps and evicts the previous entry. Hammers
`wait_and_capture_slot` on every message. Tests the wrap + two-phase commit
hot path with zero buffering.

**Failure means**: Eviction race, wait_and_capture_slot timeout under normal
load, or refcount corruption from immediate reuse.

## Subscriber saturation (`edge_cases.cc`)

**What**: max_subs=4, attempt to create 6 subscribers. Verify 5th and 6th
throw `std::runtime_error`. Destroy one, verify a new subscriber can join.
Publisher runs throughout.

**Why**: Tests the subscriber slot allocation boundary and ring reuse after
a subscriber disconnects.

**Failure means**: Subscriber joins a non-Free ring, ring leak after disconnect,
or data corruption during the join/leave cycle.
