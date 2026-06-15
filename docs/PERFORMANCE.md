# Real-time performance tuning (Linux)

Guidance for low-latency, deterministic operation of a KickCAT master on Linux.

## 1. Use an RT-PREEMPT kernel

```bash
uname -a | grep PREEMPT
```

## 2. Set a real-time scheduler

```bash
sudo chrt -f 80 ./your_ethercat_app
```

## 3. Disable NIC interrupt coalescing

```bash
sudo ethtool -C eth0 rx-usecs 0 tx-usecs 0
```

## 4. Disable RT throttling

```bash
echo -1 | sudo tee /proc/sys/kernel/sched_rt_runtime_us
```

## 5. Isolate CPU cores

Add to the kernel boot parameters:

```
isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
```

## 6. Adjust network IRQ priority

```bash
# Find the IRQ number
cat /proc/interrupts | grep eth0
# Set the priority of the IRQ thread
sudo chrt -f 90 -p <IRQ_thread_PID>
```

## 7. AF_XDP socket (optional)

AF_XDP bypasses most of the kernel networking stack using shared-memory ring
buffers between user space and the NIC driver. This can reduce latency
significantly compared to the default `AF_PACKET` raw socket.

**Build requirements:** `libxdp-dev`, `libbpf-dev`, `clang` (for BPF compilation).

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install libxdp-dev libbpf-dev clang

# Build with AF_XDP support
cmake .. -DENABLE_AF_XDP=ON
make -j
```

**Usage:** prefix the interface name with `xdp:` to select the AF_XDP backend:

```bash
# AF_XDP socket (requires CAP_NET_ADMIN + CAP_BPF, or root)
sudo ./examples/master/easycat/easycat_example -i xdp:eth0

# Regular AF_PACKET socket (default, unchanged)
sudo ./examples/master/easycat/easycat_example -i eth0
```

**Requirements:**

- Linux kernel >= 5.4 with `CONFIG_XDP_SOCKETS=y` (see the check below)
- `CAP_NET_ADMIN` + `CAP_BPF` capabilities (or root)
- A NIC driver with XDP support (most modern drivers: `i40e`, `ixgbe`, `mlx5`,
  `igc`, `e1000e`, and others)

**Verify kernel support:**

```bash
grep CONFIG_XDP_SOCKETS /boot/config-$(uname -r)
# Expected: CONFIG_XDP_SOCKETS=y
```

If `CONFIG_XDP_SOCKETS` is not set, add the following to your kernel
configuration and rebuild:

```
CONFIG_BPF_SYSCALL=y
CONFIG_XDP_SOCKETS=y
CONFIG_XDP_SOCKETS_DIAG=y
```
