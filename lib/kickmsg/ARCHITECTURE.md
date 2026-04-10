# KickMsg Architecture

## Shared-memory Layout

Every KickMsg channel is a single POSIX shared-memory region containing
three contiguous areas:

```
Shared Memory Region (/dev/shm/prefix_topic)
┌─────────────────────────────────────────────────────────────────┐
│  Header              │  Subscriber Rings (N)  │  Slot Pool      │
│  (offsets, config,   │  (one MPSC ring per    │  (shared data   │
│   Treiber stack top) │   subscriber)          │   slots)        │
└─────────────────────────────────────────────────────────────────┘
```

The header stores offsets to the rings and pool sections, making the
layout self-describing and forward-compatible.


## Concurrency Model

At the channel level, KickMsg is **MPMC** (N publishers, M subscribers).
Internally this is decomposed into **M independent MPSC rings**: each
subscriber owns exactly one ring, and all publishers write to all active
rings. No ring ever has two readers.

```
                      Treiber Free Stack
                      (lock-free, ABA-safe)
                      ┌───────────────────────┐
                 ┌───►│ free_top [gen:17 | 3] │◄────────────────────────┐
                 │    └─────────┬─────────────┘                         │
           1.pop │              │ [3]→[8]→[14]→NIL                      │
                 │              │ (linked via next_free)                │ 5.push
                 │              │                                       │ (rc→0)
            ┌────┴────┐         │   Slot Pool                           │
            │ Pub A   │         │   ┌─────────────────┐                 │
            │         │         │   │ Slot[0]  rc:2   │ ◄── published   │
            │ 2.write │         │   │ Slot[1]  rc:0   │ ◄── free ───────┤
            │ payload │         │   │ Slot[2]  rc:1   │ ◄── published   │
            │  into   │         │   │ Slot[3]  rc:0   │ ◄── free        │
            │  slot   │         │   │ ...             │                 │
            │         │         │   │ Slot[15] rc:0   │ ◄── free ───────┘
            │ 3.set   │         │   └─────────────────┘
            │ refcount│         │
            │   = N   │         │
            └────┬────┘         │
                 │              │
  4.push slot    │        ┌─────┴─────┐
  index to each  │        │           │
  active ring    │        ▼           ▼
                 ├──►┌─Ring[0]──────┐ ┌─Ring[1]──────┐ ┌─Ring[2]──────┐
                 │   │ write_pos: 42│ │ write_pos: 42│ │ (state=Free) │
                 │   │ MPSC via CAS │ │ MPSC via CAS │ │  unused      │
                 │   └──────┬───────┘ └──────┬───────┘ └──────────────┘
                 │          │                │
                 └──►...    │                │
                            │                │
           read + rc--      │                │  read + rc--
          (or evict+rc--)   │                │ (or evict+rc--)
                            ▼                ▼
                       Subscriber X     Subscriber Y
                       (read_pos=41)    (read_pos=39)
                        process-local    process-local
```

**Full cycle**: a publisher (1) pops a free slot from the Treiber stack,
(2) writes its payload, (3) pre-sets the refcount to `max_subs` (the
maximum number of subscriber rings), then (4) pushes the slot index into
each Live ring via CAS, releasing the reference inline for non-Live rings.
On the other end, each subscriber reads entries from its own ring and
decrements the slot's refcount. When the ring wraps, the publisher evicts
the oldest entry and decrements its slot's refcount too. In both cases,
whoever drives the refcount to 0 (5) pushes the slot back to the free
stack, completing the cycle.

The rings are independent: each subscriber consumes at its own pace.
A slow subscriber only overflows **its own ring** -- fast subscribers
are unaffected and keep receiving everything without loss.

```
Example: mixed-speed subscribers, all seeing the same write_pos

Ring[0] → fast sub     write_pos=1000, read_pos=998   (2 pending, plenty of room)
Ring[1] → slow sub     write_pos=1000, read_pos=936   (64 pending, ring full!)
Ring[2] → medium sub   write_pos=1000, read_pos=980   (20 pending, fine)
```

This is the entire point of per-subscriber rings vs. a single shared
MPMC ring: **no cross-subscriber impact**.


## Payload Contract

KickMsg slots carry raw bytes -- there is no serialization or
deserialization step on the hot path. The publisher `memcpy`s (or
directly writes via `allocate()`) into a shared-memory slot, and the
subscriber reads the bytes as-is. This eliminates encoding overhead
entirely, but it requires that **payloads are self-contained**:

- **No pointers or references.** A pointer is only valid in the
  address space of the process that created it. Shared memory is mapped
  at different virtual addresses in each process, so any pointer stored
  in a slot will be meaningless (or dangerous) to the reader.
- **No heap-owning types.** `std::string`, `std::vector`, smart
  pointers, etc. contain internal pointers to heap allocations. They
  must not be placed directly in a slot.
- **POD structs are fine.** Fixed-size structs of scalars, enums, and
  arrays (e.g. `struct Imu { float ax, ay, az; uint64_t timestamp; }`)
  work out of the box.
- **Variable-length data** is supported by writing the raw bytes and
  passing the length via `send(ptr, len)`. The receiver gets the length
  from `SampleRef::len()` or `SampleView::len()`.

If you need to send complex or dynamically-sized types, serialize them
into the slot yourself (e.g. FlatBuffers, Protocol Buffers, or a
custom wire format). KickMsg handles the transport; serialization is
the user's responsibility.


## Shared-memory Header

The region header is self-describing and forward-compatible:

```
Header (at offset 0)
┌───────────────────────────────────────────────────────────┐
│  magic (atomic)     0x4B49434B4D534721 ("KICKMSG!")       │
│  version            3                                     │
│  channel_type       PubSub | Broadcast                    │
│  total_size         total mmap size in bytes               │
│  sub_rings_offset   byte offset to first subscriber ring  │
│  pool_offset        byte offset to slot pool              │
│  max_subs           max subscriber slots                  │
│  sub_ring_capacity  entries per ring (power of 2)         │
│  sub_ring_mask      sub_ring_capacity - 1                 │
│  pool_size          number of slots in the pool           │
│  slot_data_size     max payload bytes per slot            │
│  slot_stride        slot_data_size + metadata, aligned    │
│  sub_ring_stride    ring header + entries, aligned        │
│  commit_timeout_us  crash detection timeout (microseconds)│
│  config_hash        FNV-1a hash of config (mismatch guard)│
│  creator_pid        PID of the creating process           │
│  created_at_ns      creation timestamp (nanoseconds)      │
│  creator_name_len   length of creator name string         │
│  creator_name[]     variable-length name (debugging)      │
│  free_top           Treiber stack head (atomic u64)       │
└───────────────────────────────────────────────────────────┘
```

Offsets (rather than fixed struct sizes) allow extending the header
without breaking existing readers. The `magic` field is an
`atomic<uint64_t>` written last with `release` during init and polled
with `acquire` by `create_or_open()` to spin-wait until the creator
has finished initialization.


## Treiber Free Stack

The slot pool is managed as a lock-free Treiber stack. Each free slot's
`next_free` field points to the next free slot, forming a singly-linked
list. The stack head (`free_top` in the Header) is a 64-bit atomic
packing a 32-bit generation counter and a 32-bit slot index to prevent
ABA.

```
free_top [gen:17 | idx:3]
     │
     ▼
  Slot[3]  ──next──▶  Slot[8]  ──next──▶  Slot[14]  ──next──▶  INVALID
  rc:0                rc:0                 rc:0
```

- **Pop** (allocate a slot): CAS `free_top` from `[gen|head]` to
  `[gen+1|head.next]`. The generation increment prevents ABA.
- **Push** (release a slot): CAS `free_top` from `[gen|head]` to
  `[gen+1|slot]` after setting `slot.next = head`.


## Subscriber Ring

Each ring is a fixed-size circular buffer of `Entry` records. An entry
contains a sequence number, slot index, and payload length -- all atomic.

```
Ring[0]
┌──────────────────────────────────────────────────────────┐
│  state: Live   in_flight: 0   write_pos: AtomicU64 = 42 │
│                                                          │
│  entries[0..7]:                                          │
│  ┌─────┬───────────┬──────────┬─────────────┐            │
│  │ idx │ sequence  │ slot_idx │ payload_len │            │
│  ├─────┼───────────┼──────────┼─────────────┤            │
│  │  0  │    37     │    5     │    128      │            │
│  │  1  │    38     │   12     │    256      │            │
│  │  2  │    39     │    0     │     64      │            │
│  │  3  │    40     │    7     │    512      │            │
│  │  4  │    41     │    2     │   1024      │ ◄── latest │
│  │  5  │    42     │   11     │    128      │ ◄── newest │
│  │  6  │    35     │    9     │    256      │ ◄── stale  │
│  │  7  │    36     │    1     │     64      │ ◄── stale  │
│  └─────┴───────────┴──────────┴─────────────┘            │
└──────────────────────────────────────────────────────────┘
```

- **Capacity** must be a power of 2 (index masking: `pos & (cap - 1)`).
- **state_flight** (atomic uint32): packed `[in_flight:30 | state:2]`.
  State bits: `Free(0)`, `Live(1)`, `Draining(2)`.
  In_flight bits: number of publishers currently admitted to this ring.
  Packing into a single variable eliminates cross-variable ordering
  concerns: the publisher's CAS atomically checks state and increments
  in_flight, so acquire/release is sufficient (no seq_cst needed).
- **write_pos** (atomic uint64): monotonically increasing position counter.
  Publishers claim positions via `fetch_add` (unconditional, O(1)).
- **has_waiter** (atomic uint32): set by the subscriber before blocking
  on `futex_wait`, cleared after. Publishers skip the `futex_wake_all`
  syscall when no subscriber is sleeping.
- **Sequence number** is monotonically increasing (`pos + 1`), used as a
  seqlock for data consistency validation and as a commit barrier between
  publishers (see Publish Flow below).
- Stale entries (sequence < subscriber's expected) are detected and
  reported as lost messages.

### Subscriber join and visibility window

A subscriber joins by CAS-ing a Free ring to Live. The CAS expects
exactly `Free | in_flight=0` (packed value 0). A ring stuck at
`Free | in_flight>0` (from a crashed publisher whose subscriber
teardown timed out) stays retired until the operator calls
`reset_retired_rings()` to recover it.

Direct acceptance of non-zero in_flight would be unsafe: the packed
`state_flight` layout means a late `fetch_sub(IN_FLIGHT_ONE)` from
a slow publisher would underflow into the state bits, corrupting
the ring for the new subscriber. Force-resetting in_flight to 0
would also be unsafe: `commit_timeout` is a heuristic, not proof
of death. A slow-but-alive publisher could still execute its
pending `fetch_sub`, causing the same underflow.

**After a publisher crash, the operator must call
`repair_locked_entries()` for poisoned ring entries and
`reset_retired_rings()` for stuck ring headers.** These are
explicit recovery steps, not silent self-repair — crashes should
be visible to the operator.

**Ordering invariant**: the subscriber captures `write_pos` BEFORE
the CAS to Live, not after. Once the ring is Live, publishers can
immediately `fetch_add(write_pos)`, racing with the subscriber's
read. Capturing first guarantees `start_pos_ <= any position a
publisher can claim after seeing Live`. Without this ordering, the
subscriber's `drain_unconsumed` window `[start_pos_, wp)` can miss
entries committed between the CAS and the read — a refcount leak.

A newly joined subscriber may miss a small number of in-flight
publishes during the visibility window right after attachment:
publishers using a relaxed pre-check may still see the ring as
non-Live (stale read). Steady-state delivery begins once all
publishers observe the ring as Live.


## Publish Flow

Any publisher can call `send()` or `allocate()` + `publish()`. Multiple
publishers may race concurrently on the same channel.

```
Publisher
   |
   v
1. treiber_pop(free_top)           Allocate a slot from the free stack.
   -> Slot[3]                      CAS on free_top (ABA-safe).
   |
   v
2. memcpy payload -> Slot[3].data  Write payload into the slot.
   |
   v
3. Slot[3].refcount = max_subs     Pre-set to max subscriber count.
   |                               Done BEFORE publishing to any ring, so
   |                               the slot cannot be freed prematurely.
   v
4. For each Ring[i]:
   |
   |-- Relaxed pre-check:           Skip obviously non-Live rings with a
   |     state_flight (relaxed)     single relaxed load (no RMW atomic).
   |     if state != Live:          A stale read may miss a just-joined
   |       excess++, continue       subscriber (acceptable — lossy).
   |                                The CAS below catches false positives.
   |
   |-- CAS admission on             Atomically verify state==Live and
   |   state_flight (acq_rel):      increment in_flight in one CAS.
   |     old + IN_FLIGHT_ONE        All ordering is on a single variable,
   |                                so acquire/release is sufficient
   |     if state changed to        (no seq_cst, no Dekker protocol).
   |     non-Live during CAS:       CAS fails, excess++, continue.
   |       excess++, continue
   |
   |-- fetch_add(write_pos, 1)     Claim position 42. Unconditional:
   |                                O(1) under contention, compiles to
   |                                a single LDADDAL on AArch64 (LSE).
   |
   |-- If ring full (wrap):
   |     wait_and_capture_slot()   Spin-wait (check clock every 1024
   |     |                         iterations) up to commit_timeout
   |     |                         (default 100ms).
   |     |-- Committed:            Capture slot_idx (old_slot).
   |     |                         Release is DEFERRED until after lock
   |     |                         CAS succeeds (see below).
   |     '-- Timeout (crash):      Previous writer crashed. old_slot =
   |                                INVALID_SLOT. The pool slot referenced
   |                                by the abandoned entry is leaked
   |                                (recoverable by GC). The ring position
   |                                is poisoned until repair_locked_entries().
   |
   |-- Two-phase commit:
   |   |
   |   | Phase 1 - CAS lock:
   |   |   CAS entry.sequence       Atomically swap from prev_seq to
   |   |     prev_seq -> LOCKED      LOCKED_SEQUENCE (UINT64_MAX).
   |   |                             This exclusively owns the entry:
   |   |                             no other publisher can CAS from
   |   |                             LOCKED_SEQUENCE since they expect
   |   |                             prev_seq.
   |   |   Retry up to 64 times     If another publisher holds the lock
   |   |     if expected ==          (entry is LOCKED_SEQUENCE), retry.
   |   |     LOCKED_SEQUENCE         The holder will release quickly
   |   |                             (just two relaxed stores + one
   |   |                             release store).
   |   |   If expected is neither    Entry was committed by another
   |   |     prev_seq nor LOCKED     publisher. excess++, give up
   |   |                             on this ring.
   |   |
   |   | Lock failure:              Do NOT release old_slot — between
   |   |   excess++, continue       capture and now, the entry may have
   |   |                             been overwritten. old_slot could
   |   |                             belong to a newer generation. The
   |   |                             unreleased ref is a bounded leak
   |   |                             (1 per drop), recoverable by GC.
   |   |
   |   | Lock success — deferred release:
   |   |   Re-read e.slot_idx       After locking, we own the entry.
   |   |   If slot_idx != INVALID:  Release old_slot (this ring's
   |   |     release_slot(old_slot) reference to the previous occupant).
   |   |   If slot_idx == INVALID:  drain_unconsumed already released
   |   |     skip release            this ring's reference. Releasing
   |   |                             again would double-decrement.
   |   |   Why deferred? TOCTOU: between wait_and_capture_slot reading
   |   |   slot_idx and the lock CAS, another publisher (or drain) can
   |   |   modify the entry. Releasing before lock risks corrupting a
   |   |   live slot's refcount. After lock, no concurrent modification.
   |   |
   |   | Write entry fields (relaxed, safe because we hold the lock):
   |   |   entry.slot_idx    = 3
   |   |   entry.payload_len = 128
   |   |
   |   | Phase 2 - commit:
   |   '   entry.sequence = 43      Release-store commits the entry.
   |                                 Subscribers and future publishers
   |                                 at this position will see all
   |                                 preceding stores.
   |
   |-- state_flight.fetch_sub       Release admission — subscriber
   |     (IN_FLIGHT_ONE, release)   destructor can now observe
   |                                in_flight == 0.
   |
   '-- if has_waiter:               Conditional wake: skip the syscall
         futex_wake_all(write_pos)  when no subscriber is blocking.

5. Batch excess: fetch_sub(excess) on slot refcount.
   One atomic RMW for all non-delivered rings, instead of N
   individual decrements. Safe because Free rings have no drain
   to race with, and Draining rings where CAS failed never
   admitted us (in_flight was never incremented).
```

### Why a two-phase commit?

Without the lock, two publishers that CAS `write_pos` to adjacent
positions could interleave their `slot_idx` and `sequence` stores on
overlapping entries (after a ring wrap). The `LOCKED_SEQUENCE` sentinel
prevents this: only one publisher at a time can write an entry's data
fields, and the final release-store of the real sequence makes the
entry visible atomically.

Subscribers treat `LOCKED_SEQUENCE` the same as "not yet committed"
and return `nullopt`, so the lock is invisible to them except as a
brief delay.

### Why pre-set refcount before publishing?

If we incremented refcount one ring at a time, a fast eviction on
Ring[1] could drop the slot's refcount to 0 and free it before we've
even published to Ring[2]. Pre-setting to `max_subs` ensures
the slot stays alive for the entire publish loop. Skipped rings
(non-Live) release their reference inline inside the loop.

### What does "evict" mean for refcount?

Eviction decrements by 1, not sets to 0. Each ring holds **one
reference** to the slot. When a ring entry is overwritten, only that
ring's reference is released:

```
Slot[5] refcount = 2   (Ring[0] and Ring[1] both reference it)

Ring[0] wraps -> evicts Slot[5]:
  refcount.fetch_sub(1) -> was 2, now 1
  1 != 0 -> slot stays alive (Ring[1] still references it)

Ring[1] subscriber reads Slot[5]:
  refcount.fetch_sub(1) -> was 1, now 0
  0 -> treiber_push(Slot[5]) back to free stack
```


## Subscribe Flow

Each subscriber reads from its own ring. The read position is
process-local (not in shared memory), so there is no
reader-reader or reader-writer contention on it.

```
Subscriber X (read_pos_ = 41, local)
   |
   v
1. write_pos(42) > read_pos_(41)?   Check for new data.
   Yes -> data available.           No -> return nullopt or futex_wait.
   |
   v
2. entry = entries[41 & mask]        Read the ring entry.
   seq1 = entry.sequence (acquire)
   |
   |  Four outcomes:
   |
   |-- seq1 == expected (42)         Data ready -> proceed to read.
   |
   |-- seq1 > expected (42)          Subscriber fell behind. The entry
   |     (e.g. seq1 = 47)            was overwritten while we weren't
   |     lost_ += (47 - 42)          looking. Skip ahead, count as lost.
   |     read_pos_++                  Continue loop -> retry next entry.
   |     continue
   |
   |-- seq1 == LOCKED_SEQUENCE       A publisher is mid-commit on this
   |     return nullopt               entry. Come back later.
   |
   '-- seq1 < expected (42)          Entry not yet committed. A publisher
         return nullopt               claimed this position (write_pos was
                                      incremented) but hasn't stored the
                                      sequence yet. Come back later.
                                      Not a deadlock: if the publisher
                                      crashed, the next publisher at this
                                      position will eventually overwrite
                                      the entry (after commit_timeout),
                                      and the subscriber will then see
                                      seq > expected (skip path above).
   |
   v
3. Read slot_idx and payload_len from the entry.
   |
   |---- Both modes: refcount pin --------------------------------|
   |                                                              |
   |  Both try_receive() and try_receive_view() pin the slot      |
   |  via CAS before reading data. This prevents the publisher    |
   |  from freeing the slot while the subscriber reads it.        |
   |                                                              |
   |  CAS Slot.refcount: rc -> rc+1   Pin the slot (only if      |
   |    (retry while rc > 0)          rc > 0, i.e. slot alive)   |
   |    (if rc == 0: slot freed       between seq1 read and      |
   |     between seq1 and now,        now. Count as lost.)       |
   |     skip as lost message)                                   |
   |                                                              |
   |  seq2 = entry.sequence (acquire)  Seqlock validation: if    |
   |  seq2 == seq1?                    the entry was overwritten  |
   |    -> yes: pin valid              after we pinned, the       |
   |    -> no:  undo pin, count lost   slot_idx may be stale.    |
   |                                                              |
   |---- Copy mode: try_receive() --------------------------------|
   |                                                              |
   |  memcpy Slot[slot_idx].data -> local recv_buf_               |
   |  Unpin: refcount.fetch_sub(1)                                |
   |    If refcount -> 0: treiber_push(slot)                      |
   |  read_pos_++                                                 |
   |  return SampleRef { recv_buf_, payload_len }                 |
   |                                                              |
   |  Note: SampleRef points into recv_buf_ (subscriber-local    |
   |  buffer). Calling try_receive() again overwrites it.         |
   |  Copy data from SampleRef before the next call.              |
   |                                                              |
   |---- Zero-copy mode: try_receive_view() ----------------------|
   |                                                              |
   |  read_pos_++                                                 |
   |  return SampleView { Slot, payload_len }                     |
   |    |                                                         |
   |    '--> ~SampleView():                                       |
   |         refcount.fetch_sub(1)                                |
   |         if refcount -> 0: treiber_push(slot)                 |
   |                                                              |
   |  SampleView holds a direct pointer into shared memory.       |
   |  The refcount pin keeps the slot alive until the view        |
   |  is destroyed. Best for large payloads where memcpy          |
   |  would dominate latency.                                     |
   '--------------------------------------------------------------'
```


## Slot Lifecycle

A slot goes through the following states:

```
  FREE (in Treiber stack, refcount = 0)
    │
    │  treiber_pop() by publisher
    ▼
  WRITING (publisher owns it, refcount = 0)
    │
    │  publish(): refcount = max_subs, push index to Live rings
    ▼
  PUBLISHED (referenced by N ring entries and/or SampleViews)
    │
    │  Sources of refcount decrement (one per ring):
    │  - Non-Live ring skip         (publisher releases inline when
    │                                state != Live during admission)
    │  - Ring overflow eviction     (publisher evicts oldest entry)
    │  - drain_unconsumed()         (subscriber destructor releases
    │                                the ring's reference for all
    │                                entries in the live window)
    │
    │  Note: try_receive() pins (rc+1) and unpins (rc-1) the slot
    │  during memcpy — net zero. The ring's original reference is
    │  released later by eviction or drain, not by try_receive().
    │
    │  Additional pin source:
    │  - ~SampleView() destruction  (zero-copy pin released)
    │
    │  Each decrement is fetch_sub(1). Only the one that
    │  transitions refcount from 1 → 0 pushes to the free stack.
    ▼
  refcount hits 0
    │
    │  treiber_push() by whoever did the last fetch_sub
    ▼
  FREE (back in Treiber stack)
```


## Crash Resilience

A publisher can crash at any point during `publish()`. The design
ensures that the channel **never deadlocks**, at the cost of bounded
resource leaks.

### Crash points

```
Crash point                        Consequence
─────────────────────────────────────────────────────────────────────
After treiber_pop, before          Pool slot leaked (popped but
  refcount pre-set                 never published, refcount never
                                   set). Bounded: 1 slot per crash.

After refcount pre-set, during     Refcount was set to max_subs but
  the ring-push loop (delivered    only k out of N rings were visited
  to k of N rings)                 before the crash. Rings visited
                                   before the crash released their
                                   reference (inline for non-Live, or
                                   via eviction/consumption for Live).
                                   Remaining (max_subs - k) references
                                   are never released. The slot is
                                   permanently leaked.

After CAS on write_pos, before     Entry is uncommitted (sequence
  sequence store (the dangerous    never written). Next publisher at
  window)                          the same ring position waits up to
                                   commit_timeout, then overwrites
                                   the entry with its own data. The
                                   ring entry itself is NOT leaked
                                   (it is overwritten). The pool slot
                                   referenced by the crashed entry
                                   cannot be safely released (slot_idx
                                   may be garbage), so it is leaked.
                                   Subscriber sees a gap (lost msg).

After sequence store               No issue. Entry is committed.
                                   Subscribers can read it normally.
```

### Timeout mechanism

When a publisher wraps around to a ring entry that was previously
claimed but never committed, it calls `wait_and_capture_slot()`:

```
wait_and_capture_slot(entry, expected_seq, timeout):
    deadline = now() + timeout
    loop (check clock every 1024 iterations):
        seq = entry.sequence (acquire)
        if seq >= expected_seq and seq != LOCKED_SEQUENCE:
            return entry.slot_idx          (committed, capture the old slot)
        if now() >= deadline:
            return INVALID_SLOT            (timeout)
```

The function skips entries in `LOCKED_SEQUENCE` state because another
publisher is mid-commit on that entry and will release shortly.

On timeout (returns `INVALID_SLOT`), the publisher:
1. Skips `release_slot()` (the old `slot_idx` may be garbage)
2. Overwrites the entry with its own data via the two-phase commit
3. The ring resumes normal operation

The timeout is configurable per channel via `channel::Config::commit_timeout`
(default: 100 ms). The tradeoff:

- **Shorter timeout** → faster recovery after a crash, but higher risk
  of falsely evicting a slow-but-alive publisher under heavy scheduling
  pressure (RT preemption, CPU throttling, etc.).
- **Longer timeout** → safer under load, but adds worst-case latency
  whenever a publisher truly crashed mid-commit and a ring wraps to
  the abandoned position.

### How the subscriber recovers

The subscriber never deadlocks either. If a publisher crashes
mid-commit:
1. The subscriber sees `seq < expected` or `seq == LOCKED_SEQUENCE`
   and returns `nullopt` (data not ready yet)
2. Eventually, another publisher wraps to the same position,
   times out on `wait_and_capture_slot`, and overwrites the entry
   via the two-phase commit with a higher sequence number
3. The subscriber then sees `seq > expected` (skip path),
   counts the gap as lost messages, and resumes

### Leak classes

There are two distinct classes of slot leaks:

```
Class   Cause                              Stuck state
───────────────────────────────────────────────────────────────────────
A       Subscriber destructs while         Ring in Draining/Free state,
        entries remain unconsumed.         entries committed, refcount
        (deactivation race)                never decremented by this ring.

B       Publisher crashes after            Slot refcount inflated;
        treiber_pop or after write_pos     ring entry uncommitted or
        CAS but before sequence store.     slot never published at all.
        (crash leak)
```

**Class A is fully closed** via the `in_flight` quiescence protocol
and full-window drain:

```
~Subscriber():
    1. state = Draining (seq_cst)       — publishers see non-Live, skip this ring
    2. spin until in_flight == 0        — bounded by commit_timeout
       a) success: quiescence achieved
       b) timeout: publisher likely crashed — skip drain (see below)
    3. if quiesced: drain_unconsumed(ring):
         wp = ring.write_pos            — now guaranteed final
         oldest = max(0, wp - capacity)
         for each entry in [max(oldest, start_pos), wp):
           if sequence == pos + 1:      — committed and not evicted
             slot.refcount--
             if refcount == 0: treiber_push(slot)
             entry.slot_idx = INVALID_SLOT (seq_cst)
           else:
             skip (evicted, uncommitted, or locked — falls into Class B)
    4. state = Free (seq_cst)           — ring available for a new subscriber
```

The key invariant: `in_flight` is incremented by publishers BEFORE
reading `state`, so once `in_flight == 0` after `state = Draining`, no
publisher can be admitted. `write_pos` is truly final.

**Timeout path**: if `in_flight` does not reach 0 within
`commit_timeout`, the destructor does **not** force `in_flight` to 0
and does **not** run `drain_unconsumed()`. Forcing in_flight would
break the quiescence invariant: a slow-but-alive publisher could still
be mid-commit, and drain would race with it, causing double-decrements.
Instead, the destructor skips drain and transitions directly to `Free`.
Leaked slot references are recoverable by the GC paths
(`repair_locked_entries` + `reclaim_orphaned_slots`). A diagnostic
counter `drain_timeouts()` is incremented for observability.

The drain walks `[max(oldest, start_pos), wp)` — not just
`[read_pos, wp)` — because `try_receive()` pins and unpins the slot
(net-zero refcount change), leaving the ring's original reference
(rc=1) on consumed entries. Those entries in `[start_pos, read_pos)`
must also be released. `start_pos` is the `write_pos` captured at
subscriber construction, ensuring a reused ring slot doesn't
double-release entries from a previous subscriber.

After releasing each entry's slot, drain sets `entry.slot_idx` to
`INVALID_SLOT` to prevent a future publisher's eviction from
double-decrementing the refcount.

For `try_receive_view()`, a live `SampleView` holds an extra pin
(rc=2: ring ref + view pin). The drain releases the ring ref (rc→1);
`~SampleView()` releases the pin (rc→0) and pushes to free.

### Leak budget

Only Class B can leak slots. Each publisher crash leaks at most
**2 pool slots**:

- The slot the crashed publisher allocated (refcount stuck > 0 because
  the remaining rings were never visited for inline release)
- The slot referenced by the abandoned ring entry (if one existed
  at the wrapped position and its `slot_idx` could not be trusted)

With a typical pool of 256+ slots, the system can tolerate dozens of
crashes before running low. Class B leaks can be recovered by the
garbage collector (see below).


## Garbage Collection

Publisher crash leaks (Class B) leave pool slots with permanently
inflated refcounts. Since the crashed process is gone, no normal
code path will ever decrement them to zero. An explicit garbage
collection pass is needed for long-running systems.

### Design principles

- **On-demand only.** The GC must be triggered explicitly by the user
  (after a known crash, on operator command, or from a health-check
  routine). It never runs automatically or periodically, so it never
  interferes with the hot path.
- **Single caller.** Only one thread/process may run GC at a time.
- **Quiesced or fenced.** The simplest approach runs GC while
  publishers and subscribers are paused. A live-traffic variant is
  possible with snapshot fencing but adds complexity.

### Two separate operations

Recovery is split into two methods with different safety profiles:

**`repair_locked_entries()`** — safe under live traffic.

Scans all ring entries. If `sequence == LOCKED_SEQUENCE` (publisher crashed
mid-commit), commits the entry with `slot_idx = INVALID_SLOT` and the
correct final sequence (`pos + 1`). This unblocks future publishers
wrapping to this position: they CAS `(pos + 1) → LOCKED`, which now
succeeds. Subscribers and evictions skip `INVALID_SLOT` entries. The
worst case under live traffic is a benign double-store if a slow (but
alive) publisher commits at the same time.

```
repair_locked_entries(region):
    for each ring i in [0, max_subs):
        for pos in [oldest_live, write_pos):
            if entries[pos].sequence == LOCKED_SEQUENCE:
                entries[pos].slot_idx = INVALID_SLOT
                entries[pos].payload_len = 0
                entries[pos].sequence = pos + 1    // committed sequence
```

**`reclaim_orphaned_slots()`** — requires full quiescence.

Scans all ring entries to build a set of referenced slot indices, then
reclaims any slot with refcount > 0 that is not in the referenced set.
NOT safe under live traffic. Requires:
- All publishers quiesced (a publisher between refcount pre-set and
  ring push has rc > 0 but no ring entry yet).
- No outstanding `SampleView` objects (a view holds a refcount pin
  without a ring entry reference; reclaiming it would free memory
  still being read).

```
reclaim_orphaned_slots(region):
    referenced = {}
    for each ring i in [0, max_subs):
        for pos in [oldest_live, write_pos):
            if entries[pos].sequence >= pos + 1:
                referenced.insert(entries[pos].slot_idx)

    for idx in [0, pool_size):
        if slot[idx].refcount > 0 and idx not in referenced:
            slot[idx].refcount = 0
            treiber_push(free_top, slot[idx], idx)
```

### What this recovers

```
Crash scenario                       GC effect
──────────────────────────────────────────────────────────────────────
After treiber_pop, before publish    Slot has refcount 0, not in any
                                     ring, not in free stack. GC cannot
                                     distinguish it from a legitimately
                                     free slot → NOT reclaimed (Class B
                                     unrecoverable leak, bounded to 1
                                     slot per crash, see below).

After refcount pre-set, delivered    Slot is in k rings but refcount
  to k of N rings                    is max_subs. The k ring references
                                     keep it in the `referenced` set,
                                     so GC cannot blindly reclaim it.
                                     However, once those k entries are
                                     eventually evicted or consumed,
                                     refcount drops to (max_subs - k)
                                     and no ring references remain →
                                     next GC pass reclaims it.

After write_pos CAS, before          Entry is overwritten after
  sequence store                     commit_timeout. The crashed slot's
                                     index may be garbage and won't
                                     appear in any committed entry →
                                     reclaimed on next GC pass.
```

### API

```cpp
// Lightweight health check — read-only, safe under live traffic.
// Call periodically from a supervisor to detect crash damage.
// Note: a single nonzero reading may be a transient state (e.g.,
// Draining ring with publishers finishing). Call twice with a gap
// > commit_timeout; persistent counts indicate a real crash.
auto report = region.diagnose();
// report.locked_entries:  entries stuck at LOCKED_SEQUENCE
// report.retired_rings:   Free rings with stale in_flight > 0
// report.draining_rings:  Draining rings with in_flight > 0 (usually transient)
// report.live_rings:      active subscriber rings

// Safe under live traffic — repairs poisoned ring entries.
// Can be called freely on a health-check timer.
std::size_t repaired = region.repair_locked_entries();

// Resets retired rings (Free | in_flight>0) so new subscribers can
// claim them. Only safe after confirming the crashed publisher is gone.
// Deliberate post-crash action, not a routine maintenance call.
std::size_t reset = region.reset_retired_rings();

// Requires full quiescence — reclaims orphaned slots.
std::size_t reclaimed = region.reclaim_orphaned_slots();
```

**`diagnose()`** — read-only scan, safe under live traffic. Returns
counts of locked entries and stuck rings. The supervisor calls this
periodically; persistent nonzero counts signal recovery is needed.

**`repair_locked_entries()`** — commits locked entries with
`INVALID_SLOT`. Safe under live traffic (benign double-store if a
slow publisher commits at the same time). Can run on a timer.

**`reset_retired_rings()`** — resets stuck rings (`Free | in_flight>0`
→ `Free | in_flight=0`). Only safe after confirming the crashed
publisher is gone. Unlike `repair_locked_entries()`, this is a
deliberate post-crash action.

**`reclaim_orphaned_slots()`** — walks all rings to build a
referenced-slot set, then frees any unreferenced slot with
refcount > 0. NOT safe under live traffic — requires all publishers
quiesced and no outstanding `SampleView` objects.

### Recommended recovery sequence

```
1. diagnose() → persistent nonzero counts (check twice, gap > commit_timeout)
2. repair_locked_entries()          — safe under live traffic
3. reset_retired_rings()            — after confirming crashed publisher is gone
4. (optional) pause all publishers
5. reclaim_orphaned_slots()         — requires quiescence
6. resume publishers
```

Steps 4–6 are only needed if the pool is exhausted from leaked slots.
In most cases, steps 2–3 restore the channel to full operation.


## ABA Safety

The [ABA problem](https://en.wikipedia.org/wiki/ABA_problem) is the main
pitfall of lock-free CAS loops: between a thread's read and its CAS, other
threads may change a value away and back, making the CAS succeed on stale
state.

KickMsg avoids ABA by ensuring that **every CAS target is effectively
monotonic** -- it can never return to a previously observed value:

```
CAS site                Why ABA-safe
────────────────────────────────────────────────────────────────────────────
free_top (Treiber)      64-bit tagged pointer: 32-bit generation counter
                        incremented on every push/pop, packed with the
                        32-bit slot index. Same index + different
                        generation = CAS fails.

write_pos (rings)       Monotonically increasing 64-bit counter. Only goes
                        up, never revisits a value.

state (subscriber)      One-way state machine: Free → Live → Draining → Free.
                        Publishers only deliver to Live rings. The
                        Dekker admission (in_flight++ then state check)
                        ensures no publisher misses a Live → Draining
                        transition.

refcount (pinning)      CAS from rc to rc+1 only when rc > 0. Even if
                        intermediate transitions bring it back to the same
                        value, the invariant ("slot is alive") still
                        holds -- the operation is idempotent on the safety
                        property.
```

The key principle: **make every CAS target monotonic**, either naturally
(counters that only go up) or artificially (generation tag alongside a
recycled value).


## Portability

ABA safety is a property of the **algorithm**, not the CPU. It relies on
the C++ memory model guarantees for `std::atomic`, which are
architecture-independent.

What varies across architectures is the **cost** of atomic operations:

- **x86-64**: strong memory model. `compare_exchange` compiles to a single
  `LOCK CMPXCHG` instruction. `relaxed` loads/stores are free (no extra
  fences emitted).
- **AArch64 (ARMv8)**: weak memory model. `compare_exchange` uses
  `LDXR`/`STXR` (load-exclusive / store-exclusive) pairs. `acquire`/`release`
  orderings emit `LDAR`/`STLR` variants which carry a small cost compared
  to x86, but remain single instructions -- not full memory barriers.
  `relaxed` loads/stores are free on ARM as well.

Additional supported architectures:

- **RISC-V (RV64)**: weak memory model (RVWMO). Lock-free 64-bit atomics
  via `LR`/`SC` pairs. Acquire/release use `fence` instructions.
  Performance characteristics similar to AArch64.
- **MIPS64**: provides 64-bit `LL`/`SC` for lock-free CAS.

**Excluded**: 32-bit platforms (RV32, MIPS32, ARMv7) lack native 64-bit
atomic operations. The library enforces this at compile time via
`static_assert(std::atomic<uint64_t>::is_always_lock_free)`.

All supported architectures provide native 64-bit atomic CAS on aligned
values, so there is no risk of torn reads. The correctness is portable;
only the per-operation latency differs (by a few nanoseconds).

### Platform Abstraction (KickCAT Integration)

kickmsg is a library within the KickCAT project and relies on KickCAT's
OS abstraction layer for platform-specific functionality. The two
interfaces used are:

```
Abstraction      Provided by         Linux                    macOS (Darwin)
────────────────────────────────────────────────────────────────────────────────
SharedMemory     kickcat/OS/          shm_open / ftruncate     (same, POSIX)
                 SharedMemory.h       / mmap
Futex            kickcat/OS/          SYS_futex                __ulock_wait
                 Futex.h              (FUTEX_WAIT/_WAKE)       /_wake
```

**macOS caveat:** `__ulock_wait` / `__ulock_wake` are private Apple APIs.
They are used internally by libc++ and libdispatch (stable since macOS
10.12), but Apple has not published a public header or formal stability
guarantee. If Apple changes this ABI in a future release, a fallback to
`kqueue` with `EVFILT_USER` or `dispatch_semaphore` would be needed.

The core engine (`types.h`, `Region.h`, `Publisher.h`, `Subscriber.h`,
`Node.h`) uses only `std::atomic` C++17 and these two abstractions --
no platform `#ifdef` leaks into the messaging logic.

To add a new OS, implement the Futex and SharedMemory interfaces in
KickCAT's `src/OS/<Platform>/` directory.


## Why `futex` instead of `mutex` + `condvar`?

Blocking subscribers to wait for new data could be done with a
`pthread_mutex` + `pthread_cond` pair, but `futex` is a better fit:

- **No shared mutex state in the ring.**  A mutex/condvar requires
  initializing a `pthread_mutex_t` + `pthread_cond_t` in shared memory
  with `PTHREAD_PROCESS_SHARED`, which adds complexity and is fragile
  (a crashed process can leave the mutex locked, causing deadlock).
- **Atomic-native.**  `futex` operates directly on the atomic variable
  the subscriber already checks (`write_pos`). The subscriber does
  `if (write_pos == old) futex_wait(&write_pos, old)`. There is no
  separate lock to acquire.
- **No thundering herd in practice.**  The publisher does
  `futex_wake_all` after writing, but each subscriber reads from its
  own ring -- there is no contention on wakeup.
- **Minimal overhead.**  When data is already available, no syscall
  is issued at all (the subscriber's fast path is a single atomic load).
  The `futex` syscall only triggers when the subscriber must actually
  sleep.


## Channel Patterns

All patterns are conventions on top of the same MPMC pool + rings engine.
The backbone does not enforce these constraints; they are established by
the `Node` API which controls how shared-memory regions are named and
how `channel::Config` defaults are set.

```
PubSub (1-to-N)         Broadcast (N-to-N)         Mailbox (N-to-1)
/{prefix}_{topic}       /{prefix}_broadcast_{ch}    /{prefix}_{owner}_mbx_{tag}
                                                    max_subscribers=1
┌─────┐                 ┌─────┐   ┌─────┐          ┌─────┐   ┌─────┐
│Pub A│                 │Pub A│   │Pub B│          │Pub A│   │Pub B│
└──┬──┘                 └──┬──┘   └──┬──┘          └──┬──┘   └──┬──┘
   │                       │         │                │         │
   ▼                       └────┬────┘                └────┬────┘
┌──────┐                       ▼                           ▼
│ Pool │                   ┌──────┐                    ┌──────┐
└──┬───┘                   │ Pool │                    │ Pool │
   │                       └──┬───┘                    └──┬───┘
   ├──▶ Ring[0] → Sub A       ├──▶ Ring[0] → Sub A        │
   ├──▶ Ring[1] → Sub B       ├──▶ Ring[1] → Sub B        └──▶ Ring[0] → Owner
   └──▶ Ring[2] → Sub C       ├──▶ Ring[2] → Sub A(*)
                               └──▶ Ring[3] → Sub B(*)
                           (*) each node is both pub+sub
```

### Topic Naming

Topics are global within a prefix namespace. The publisher's node name
is not part of the shared-memory path for PubSub or Broadcast channels
(it is stored as metadata in the header's `creator_name` field).

```
Node API                           SHM name
──────────────────────────────────────────────────────
advertise("lidar", cfg)            /{prefix}_lidar
subscribe("lidar")                 /{prefix}_lidar
join_broadcast("events", cfg)      /{prefix}_broadcast_events
create_mailbox("reply", cfg)       /{prefix}_{node_name}_mbx_reply
open_mailbox("peer", "reply")      /{prefix}_peer_mbx_reply
```

Mailbox paths include the owner's node name because they are personal
reply channels -- the sender must know who to reply to.


## Design Tradeoffs

### Silent data loss on slow subscribers

When a subscriber's ring overflows, the publisher silently evicts the
oldest entry to make room. The subscriber only discovers lost messages
after the fact, via the `lost()` counter. This is an intentional design
choice:

- **Non-blocking guarantee.** A fast publisher is never stalled by a slow
  subscriber. This is critical for real-time data where the latest value
  matters more than completeness.
- **Per-subscriber isolation.** Overflow in one ring does not affect other
  subscribers (each ring is independent).
- **No backpressure.** There is no mechanism for a subscriber to signal
  the publisher to slow down. If you need reliable delivery, implement
  acknowledgement and flow control in the upper layer.

The `lost()` counter lets the application detect overflow and act on it
(e.g., log a warning, skip to the latest sample, or resize the ring).

### Pool exhaustion

When the slot pool is empty, `allocate()` returns `nullptr` and
`send()` returns `-EAGAIN`. If the payload exceeds `max_payload_size`,
`send()` returns `-EMSGSIZE`. On success, `send()` returns the number
of bytes written. The publisher must handle errors — typically by
yielding and retrying on `-EAGAIN`, or failing on `-EMSGSIZE`.
No exception is thrown and no slot is leaked.

This happens when all pool slots are in-flight (allocated, published,
but not yet consumed and released by all subscribers). Increasing
`pool_size` or reducing the number of active subscribers alleviates it.

### CAS-lock contention

During the two-phase commit, the publisher CAS-locks the ring entry
before writing data. If another publisher holds the lock, the current
publisher retries up to 64 times. If all retries fail, delivery to
that subscriber ring is silently abandoned — the message is lost for
that subscriber only. The excess refcount adjustment handles the
slot lifecycle correctly.

This only occurs under very high MPMC contention (many publishers
competing for the same ring entry). In practice, the lock is held
for two relaxed stores + one release store (~nanoseconds), so the
64-retry budget is generous.

### Unrecoverable slot leak (Class B)

If a publisher crashes between `treiber_pop` (slot allocated, refcount=0)
and `refcount.store(max_subs)`, the slot has refcount=0 and is neither
in the free stack nor referenced by any ring entry. The GC cannot
distinguish it from a legitimately free slot and will not reclaim it.

This is a bounded leak: at most one slot per publisher crash in that
specific window (a few instructions wide). The slot is recovered on
the next `SharedRegion::create` (full reinitialization).

**Operational guidance:** if your deployment involves frequent publisher
crashes (e.g. during development, or in a watchdog-restart architecture),
size the pool with enough headroom to absorb the expected number of
orphans between region recreations. For a pool of 256 slots and a
crash rate of one per hour, the leak is negligible. If crashes are
frequent enough to matter, the region should be recreated.

### Pool and Ring Sizing

The `pool_size` and `sub_ring_capacity` parameters interact:

- **`sub_ring_capacity`** is the per-subscriber jitter absorption buffer.
  When a subscriber is descheduled, its ring fills up. Once full, new
  messages are dropped for that subscriber regardless of free pool slots.
  At a publish rate of R Hz, a ring of capacity C gives C/R seconds of
  tolerance before loss.

- **`pool_size`** must be at least `sub_ring_capacity * max_subscribers`.
  Each active subscriber can hold up to `sub_ring_capacity` slot
  references (its entire ring window). Pool slots are only freed when
  **all** subscribers have consumed or evicted them (refcount reaches 0).
  With M slow subscribers each holding a full ring window, the pool
  needs M * C slots to avoid starvation. If the pool is too small,
  the publisher exhausts it and `allocate()` fails even when individual
  subscribers have room.

**Sizing rule:** `pool_size >= sub_ring_capacity * max_subscribers`
(hard minimum). In practice, add 2x headroom for bursty traffic:
`pool_size = sub_ring_capacity * max_subscribers * 2`.

The `sub_ring_capacity` is the primary tuning knob:

```
Publish rate    Ring capacity    Tolerance before loss
──────────────────────────────────────────────────────
  1 kHz            64              64 ms
  1 kHz           256             256 ms
 10 kHz            64             6.4 ms
 10 kHz           256              26 ms
100 kHz            64             640 us
100 kHz           256             2.6 ms
```

At 1 kHz (typical control loop), even the default ring capacity of 64
provides 64 ms of scheduling tolerance -- well beyond typical OS jitter
(< 10 ms). Under heavy system stress with `stress-ng`, a ring of 256
recovers ~70% of messages vs ~35% with ring=64 (10 subscribers, burst
publish at full rate).

### Zero-copy pin CAS bound

In `try_receive_view()`, the subscriber CAS-pins a slot by incrementing
its refcount from `rc` to `rc + 1` (retrying while `rc > 0`). Under
contention from M concurrent subscribers pinning the same hot slot,
each CAS may fail and retry. However, the retry count is bounded by M:
every successful CAS by another subscriber represents forward progress,
so this loop cannot livelock.
