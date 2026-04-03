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
                 │   │ write_pos: 42│ │ write_pos: 42│ │ (active=0)   │
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
(2) writes its payload, (3) pre-sets the refcount to the number of active
subscribers, then (4) pushes the slot index into each active ring via CAS.
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
│  magic              0x4B49434B4D534721 ("KICKMSG!")       │
│  version            1                                     │
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
without breaking existing readers. The `magic` field is used by
`create_or_open()` to spin-wait until the creator has finished
initialization.


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
│  active: 1          write_pos: AtomicU64 = 42            │
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
- **Sequence number** is monotonically increasing (`pos + 1`), used as a
  seqlock for data consistency validation and as a commit barrier between
  publishers (see Publish Flow below).
- Stale entries (sequence < subscriber's expected) are detected and
  reported as lost messages.


## Publish Flow

Any publisher can call `send()` or `allocate()` + `publish()`. Multiple
publishers may race concurrently on the same channel.

```
Publisher
   │
   ▼
1. treiber_pop(free_top)           Allocate a slot from the free stack.
   → Slot[3]                       CAS on free_top (ABA-safe).
   │
   ▼
2. memcpy payload → Slot[3].data   Write payload into the slot.
   │
   ▼
3. Slot[3].refcount = N            Pre-set to number of active subscribers.
   │                               Done BEFORE publishing to any ring, so
   │                               the slot cannot be freed prematurely.
   ▼
4. For each active Ring[i]:
   │
   ├─► CAS write_pos: 42 → 43     Claim position 42 (MPSC-safe).
   │   Multiple publishers may     Losers retry with updated value.
   │   race here; CAS serializes.
   │
   ├─► If ring full (wrap):
   │     wait_for_commit()         Bounded wait (COMMIT_TIMEOUT, 100ms).
   │     ├─ Committed:             Entry is valid. Read its slot_idx
   │     │    release_slot()       and decrement refcount (not set to 0,
   │     │                         just -1 for this ring's reference).
   │     │                         If refcount reaches 0 → treiber_push.
   │     └─ Timeout (crash):       Previous writer crashed. Skip
   │          skip release          release_slot() because slot_idx may
   │                                be garbage. The pool slot referenced
   │                                by the abandoned entry is leaked.
   │                                The ring entry itself is overwritten
   │                                and stays operational.
   │
   ├─► entry.slot_idx   = 3       Write entry fields (relaxed).
   │   entry.payload_len = 128
   │
   └─► entry.sequence   = 43      Commit (release store).
                                   This is the commit barrier:
                                   - Subscribers see it as "data ready"
                                   - Next publisher at this position (after
                                     wrap) sees it as "previous write done"
   │
   ▼
5. Adjust refcount for inactive   fetch_sub(inactive_count).
   rings.                         If refcount → 0: treiber_push.
   │
   ▼
6. futex_wake_all(write_pos)      Wake any sleeping subscribers.
```

### Why pre-set refcount before publishing?

If we incremented refcount one ring at a time, a fast eviction on
Ring[1] could drop the slot's refcount to 0 and free it before we've
even published to Ring[2]. Pre-setting to `max_active_subs` ensures
the slot stays alive for the entire publish loop.

### What does "evict" mean for refcount?

Eviction decrements by 1, not sets to 0. Each ring holds **one
reference** to the slot. When a ring entry is overwritten, only that
ring's reference is released:

```
Slot[5] refcount = 2   (Ring[0] and Ring[1] both reference it)

Ring[0] wraps → evicts Slot[5]:
  refcount.fetch_sub(1) → was 2, now 1
  1 ≠ 0 → slot stays alive (Ring[1] still references it)

Ring[1] subscriber reads Slot[5]:
  refcount.fetch_sub(1) → was 1, now 0
  0 → treiber_push(Slot[5]) back to free stack
```


## Subscribe Flow

Each subscriber reads from its own ring. The read position is
process-local (not in shared memory), so there is no
reader-reader or reader-writer contention on it.

```
Subscriber X (read_pos_ = 41, local)
   │
   ▼
1. write_pos(42) > read_pos_(41)?   Check for new data.
   Yes → data available.            No → return nullopt or futex_wait.
   │
   ▼
2. entry = entries[41 & mask]        Read the ring entry.
   seq1 = entry.sequence (acquire)
   │
   │  Three outcomes:
   │
   ├─► seq1 == expected (42)         Data ready → proceed to read.
   │
   ├─► seq1 > expected (42)          Subscriber fell behind. The entry
   │     (e.g. seq1 = 47)            was overwritten while we weren't
   │     lost_ += (47 - 42)          looking. Skip ahead, count as lost.
   │     read_pos_++                  Continue loop → retry next entry.
   │     continue
   │
   └─► seq1 < expected (42)          Entry not yet committed. A publisher
         return nullopt               claimed this position (write_pos was
                                      incremented) but hasn't stored the
                                      sequence yet. Come back later.
                                      Not a deadlock: if the publisher
                                      crashed, the next publisher at this
                                      position will eventually overwrite
                                      the entry (after COMMIT_TIMEOUT),
                                      and the subscriber will then see
                                      seq > expected (skip path above).
   │
   ▼
3. Read slot_idx and payload_len from the entry.
   │
   ├──── Copy mode: try_receive() ─────────────────────────────┐
   │                                                           │
   │  memcpy Slot[slot_idx].data → local recv_buf_             │
   │  seq2 = entry.sequence (acquire)                          │
   │  seq2 == seq1?  → yes: data consistent                    │
   │                → no:  entry was overwritten during the     │
   │                       memcpy (race with a publisher that   │
   │                       wrapped around). Count as lost,      │
   │                       retry.                               │
   │  read_pos_++                                               │
   │  return SampleRef { recv_buf_, payload_len }               │
   │                                                           │
   │  Note: SampleRef points into recv_buf_ (subscriber-local  │
   │  buffer). Calling try_receive() again overwrites it.      │
   │  Copy data from SampleRef before the next call.           │
   │                                                           │
   ├──── Zero-copy mode: try_receive_view() ───────────────────┤
   │                                                           │
   │  CAS Slot.refcount: rc → rc+1   Pin the slot (only if     │
   │    (retry while rc > 0)          rc > 0, i.e. slot alive) │
   │    (if rc == 0: slot freed       between seq1 read and    │
   │     between seq1 and now,        now. Count as lost.)     │
   │     skip as lost message)                                 │
   │  seq2 = entry.sequence (acquire)                          │
   │  seq2 == seq1?  → yes: pin valid                          │
   │                → no:  entry overwritten after we pinned.   │
   │                       Undo pin: fetch_sub(1).              │
   │                       If refcount → 0: treiber_push.       │
   │                       Count as lost, retry.                │
   │  read_pos_++                                               │
   │  return SampleView { Slot, payload_len }                   │
   │    │                                                      │
   │    └──▶ ~SampleView():                                     │
   │         refcount.fetch_sub(1)                              │
   │         if refcount → 0: treiber_push(slot)                │
   │                                                           │
   │  SampleView holds a direct pointer into shared memory.    │
   │  The refcount pin keeps the slot alive until the view     │
   │  is destroyed. Best for large payloads where memcpy       │
   │  would dominate latency.                                  │
   └───────────────────────────────────────────────────────────┘
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
    │  publish(): refcount = active_subs, push index to rings
    ▼
  PUBLISHED (referenced by N ring entries and/or SampleViews)
    │
    │  Sources of refcount decrement:
    │  - Ring overflow eviction     (publisher evicts oldest entry)
    │  - try_receive() completion   (copy done, reference consumed)
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
  the ring-push loop (delivered    only k out of N active rings
  to k of N active rings)         received the slot index. Step 5
                                   (excess adjustment) never runs.
                                   The k rings that received the slot
                                   will eventually decrement refcount
                                   (via eviction or consumption), but
                                   refcount settles at (max_subs - k)
                                   and never reaches 0. The slot is
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
claimed but never committed, it calls `wait_for_commit()`:

```
wait_for_commit(entry, expected_seq, timeout):
    deadline = now() + timeout
    loop (check clock every 1024 iterations):
        if entry.sequence >= expected_seq → return true   (committed)
        if now() >= deadline             → return false   (timeout)
```

On timeout, the publisher:
1. Skips `release_slot()` (the old `slot_idx` may be garbage)
2. Overwrites the entry with its own data and commits normally
3. The ring resumes normal operation

The timeout is configurable per channel via `RingConfig::commit_timeout`
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
1. The subscriber sees `seq < expected` and returns `nullopt`
   (data not ready yet)
2. Eventually, another publisher wraps to the same position,
   times out on `wait_for_commit`, and overwrites the entry
   with a higher sequence number
3. The subscriber then sees `seq > expected` (skip path),
   counts the gap as lost messages, and resumes

### Leak classes

There are two distinct classes of slot leaks:

```
Class   Cause                              Stuck state
───────────────────────────────────────────────────────────────────────
A       Subscriber destructs while         Ring inactive, entries
        entries remain unconsumed.         committed, refcount never
        (deactivation race)                decremented by this ring.

B       Publisher crashes after            Slot refcount inflated;
        treiber_pop or after write_pos     ring entry uncommitted or
        CAS but before sequence store.     slot never published at all.
        (crash leak)
```

**Class A is fully closed.** The subscriber destructor walks its ring's
live window before deactivating:

```
~Subscriber():
    1. active = 0                          (stop new publishes to this ring)
    2. wp = ring.write_pos
    3. clamp read_pos to [wp - capacity, wp)  (older entries already evicted)
    4. for each entry in [read_pos, wp):
         if sequence == expected:
           slot.refcount--
           if refcount == 0: treiber_push(slot)
         else:
           skip (uncommitted, falls into Class B)
```

Entries mid-commit by a concurrent publisher (sequence not yet stored)
are skipped -- at most 1 per concurrent publisher. Their refcount
inflation falls into Class B.

### Leak budget

Only Class B can leak slots. Each publisher crash leaks at most
**2 pool slots**:

- The slot the crashed publisher allocated (refcount stuck > 0 because
  step 5 never adjusted for inactive/undelivered rings)
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

### Algorithm

```
collect_garbage(region):
    hdr = region.header()

    // 1. Build a set of all slot indices referenced by any ring entry
    referenced = {}
    for each ring i in [0, max_subs):
        entries = ring_entries(ring_at(i))
        wp = ring.write_pos
        capacity = hdr.sub_ring_capacity
        start = wp > capacity ? wp - capacity : 0
        for pos in [start, wp):
            e = entries[pos & mask]
            if e.sequence >= pos + 1:         // committed
                referenced.insert(e.slot_idx)

    // 2. Reclaim orphaned slots
    reclaimed = 0
    for idx in [0, pool_size):
        slot = slot_at(idx)
        if slot.refcount > 0 and idx not in referenced:
            slot.refcount = 0
            treiber_push(free_top, slot, idx)
            reclaimed++

    return reclaimed
```

### What this recovers

```
Crash scenario                       GC effect
──────────────────────────────────────────────────────────────────────
After treiber_pop, before publish    Slot has refcount 0 or stale
                                     value, not in any ring → reclaimed.

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
std::size_t reclaimed = region.collect_garbage();
```

`SharedRegion::collect_garbage()` walks all rings and the entire pool,
reclaims orphaned slots, and returns the number of slots recovered.

The caller must ensure no publishers or subscribers are actively using
the region, or accept best-effort results. A standalone CLI utility
that opens the region read-write, runs GC, and reports results could
also be built on top of this method.


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

active (subscriber)     One-shot transition (0 → 1). No cycle possible.

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

Both architectures support native 64-bit atomic CAS on aligned values, so
there is no risk of torn reads. The correctness is portable; only the
per-operation latency differs (by a few nanoseconds).

### Platform Abstraction

All platform-specific code is isolated in two interfaces:

```
Abstraction      Linux                           macOS (Darwin)
──────────────────────────────────────────────────────────────────────
SharedMemory     shm_open / ftruncate / mmap     (same, POSIX)
Futex            SYS_futex (FUTEX_WAIT/_WAKE)    __ulock_wait/_wake
```

**macOS caveat:** `__ulock_wait` / `__ulock_wake` are private Apple APIs.
They are used internally by libc++ and libdispatch (stable since macOS
10.12), but Apple has not published a public header or formal stability
guarantee. If Apple changes this ABI in a future release, a fallback to
`kqueue` with `EVFILT_USER` or `dispatch_semaphore` would be needed.

The core engine (`types.h`, `Region.h`, `Publisher.h`, `Subscriber.h`,
`Node.h`) uses only `std::atomic` C++17 and these two abstractions --
no platform `#ifdef` leaks into the messaging logic.

To add a new OS, implement:
- `src/OS/<Platform>/SharedMemory.cc` (~50 lines) -- or reuse `Unix/`
- `src/OS/<Platform>/Futex.cc` (~15 lines)


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
how `RingConfig` defaults are set.

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

### Zero-copy pin CAS bound

In `try_receive_view()`, the subscriber CAS-pins a slot by incrementing
its refcount from `rc` to `rc + 1` (retrying while `rc > 0`). Under
contention from M concurrent subscribers pinning the same hot slot,
each CAS may fail and retry. However, the retry count is bounded by M:
every successful CAS by another subscriber represents forward progress,
so this loop cannot livelock.
