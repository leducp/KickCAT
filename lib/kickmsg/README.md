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
kickmsg::ChannelConfig cfg;
cfg.max_subscribers   = 4;
cfg.sub_ring_capacity = 64;
cfg.pool_size         = 256;
cfg.max_payload_size  = 4096;

auto region = kickmsg::SharedRegion::create(
    "/my_topic", kickmsg::ChannelType::PubSub, cfg);

// Subscribe, then publish
kickmsg::Subscriber sub(region);
kickmsg::Publisher  pub(region);

uint32_t value = 42;
pub.send(&value, sizeof(value));

auto sample = sub.try_receive();
// sample->data(), sample->len()
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
auto sample = sub.receive(std::chrono::milliseconds{100});
// blocks via futex until data arrives or timeout
```

## Building

### Standalone

```bash
# Install dependencies (GTest for tests)
uv venv .venv && source .venv/bin/activate
uv pip install conan
conan install . --output-folder=build --build=missing

# Build
cmake -S . -B build \
    -DCMAKE_PREFIX_PATH=build \
    -DCMAKE_BUILD_TYPE=Release \
    -DKICKMSG_BUILD_TESTS=ON \
    -DKICKMSG_BUILD_EXAMPLES=ON
cmake --build build

# Run tests
./build/tests/kickmsg_unit
./build/tests/kickmsg_stress_test

# Run examples
./build/examples/hello_pubsub
./build/examples/hello_zerocopy
./build/examples/hello_broadcast
```

### As a subdirectory

```cmake
add_subdirectory(kickmsg)
target_link_libraries(my_app PRIVATE kickmsg)
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `KICKMSG_BUILD_TESTS` | `OFF` | Build unit and stress tests |
| `KICKMSG_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `KICKMSG_ENABLE_TSAN` | `OFF` | Build TSan stress test variant |

## Platform Support

| Platform | SharedMemory | Futex |
|----------|-------------|-------|
| Linux | `shm_open` / `mmap` | `SYS_futex` |
| macOS | `shm_open` / `mmap` | `__ulock_wait` / `__ulock_wake` |

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design: shared-memory layout, concurrency model, publish/subscribe flows, crash resilience, garbage collection, and ABA safety analysis.

## License

[CeCILL-C](LICENSE)
