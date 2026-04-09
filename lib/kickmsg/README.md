# KickMsg

Lock-free shared-memory messaging library for inter-process communication.

KickMsg provides MPMC publish/subscribe over shared memory with zero-copy receive, per-subscriber ring isolation, and crash resilience — all without locks or kernel-mediated synchronization on the hot path.

## Features

- **Lock-free**: all data paths use atomic CAS (Treiber stack, MPSC rings)
- **Zero-copy receive**: `SampleView` pins slots via refcount, avoiding memcpy for large payloads
- **Per-subscriber isolation**: a slow subscriber only overflows its own ring — fast subscribers are unaffected
- **Crash resilient**: publisher crashes never deadlock the channel; bounded slot leaks are recoverable via GC
- **Topic-centric naming**: subscribers connect by topic name, not publisher identity
- **C++17**, no external dependencies beyond OS layer

## Channel Patterns

| Pattern | API | SHM name |
|---------|-----|----------|
| PubSub (1-to-N) | `advertise` / `subscribe` | `/{prefix}_{topic}` |
| Broadcast (N-to-N) | `join_broadcast` | `/{prefix}_broadcast_{channel}` |
| Mailbox (N-to-1) | `create_mailbox` / `open_mailbox` | `/{prefix}_{owner}_mbx_{tag}` |

## Quick Start

```cpp
#include <kickmsg/Publisher.h>
#include <kickmsg/Subscriber.h>

// Create a channel
kickmsg::channel::Config cfg;
cfg.max_subscribers   = 4;
cfg.sub_ring_capacity = 64;
cfg.pool_size         = 256;
cfg.max_payload_size  = 4096;

auto region = kickmsg::SharedRegion::create(
    "/my_topic", kickmsg::channel::PubSub, cfg);

// Subscribe, then publish
kickmsg::Subscriber sub(region);
kickmsg::Publisher  pub(region);

uint32_t value = 42;
pub.send(&value, sizeof(value));

auto sample = sub.try_receive();
// sample->data(), sample->len(), sample->ring_pos()
```

### Node API (topic-centric)

```cpp
#include <kickmsg/Node.h>

kickmsg::Node pub_node("sensor", "myapp");
auto pub = pub_node.advertise("imu");

// Any node can subscribe by topic name alone
kickmsg::Node sub_node("logger", "myapp");
auto sub = sub_node.subscribe("imu");
```

### Zero-copy receive

```cpp
auto view = sub.try_receive_view();
// view->data() points directly into shared memory
// slot is pinned until view is destroyed
```

### Blocking receive

```cpp
auto sample = sub.receive(100ms);
// blocks via futex until data arrives or timeout
```

### Health diagnostics and crash recovery

```cpp
// Periodic health check (read-only, safe under live traffic)
auto report = region.diagnose();
// report.locked_entries, report.retired_rings,
// report.draining_rings, report.live_rings

// Repair poisoned entries (safe under live traffic)
region.repair_locked_entries();

// Reset retired rings (after confirming crashed publisher is gone)
region.reset_retired_rings();

// Reclaim leaked slots (requires full quiescence)
region.reclaim_orphaned_slots();
```

## Building

KickMsg is built as part of the KickCAT project.

```bash
# From the KickCAT root:
./scripts/configure.sh build --with=unit_tests
./scripts/setup_build.sh build
cd build && make -j

# Run tests
./kickmsg_unit
./kickmsg_stress_test

# Run examples (if BUILD_MASTER_EXAMPLES is ON)
./lib/kickmsg/examples/hello_pubsub
./lib/kickmsg/examples/hello_zerocopy
./lib/kickmsg/examples/hello_broadcast
./lib/kickmsg/examples/hello_diagnose
```

### As a subdirectory

```cmake
add_subdirectory(kickmsg)
target_link_libraries(my_app PRIVATE kickmsg)
```

### Relevant CMake options (set in KickCAT's top-level CMakeLists.txt)

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_UNIT_TESTS` | `OFF` | Build unit and stress tests |
| `BUILD_MASTER_EXAMPLES` | `OFF` | Build example programs |
| `ENABLE_TSAN` | `OFF` | Enable ThreadSanitizer |

## Platform Support

| Platform | SharedMemory | Futex |
|----------|-------------|-------|
| Linux | `shm_open` / `mmap` | `SYS_futex` |
| macOS | `shm_open` / `mmap` | `__ulock_wait` / `__ulock_wake` |

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design: shared-memory layout, concurrency model, publish/subscribe flows, crash resilience, garbage collection, and ABA safety analysis.

## License

[CeCILL-C](LICENSE)
