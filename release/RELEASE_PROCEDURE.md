# KickCAT Release Procedure

This document outlines the mandatory steps to be followed when releasing a new version of KickCAT. The process ensures stability, hardware compatibility, and correct deployment across supported platforms.

## 1. Release Candidate (RC) Tagging

Before an official release, a Release Candidate must be created to trigger the full CI pipeline and provide artifacts for hardware testing.

> **Note:** Versioning is tag-driven. The `pyproject.toml` placeholder (`0.0.0`) is replaced
> automatically during wheel build via `tools/setup/version.sh`. No manual version edit is needed.

1. Push a tag with the `-rc` suffix:
   ```bash
   git tag -a v1.2.0-rc1 -m "Release Candidate 1 for v1.2.0"
   git push origin v1.2.0-rc1
   ```
2. Verify that the GitHub Actions CI pipeline completes successfully for the tag.

## 2. Hardware Long-Duration Test

Stability is critical for EtherCAT applications. A long-duration continuous run (1 week to 1 month) must be performed on actual hardware.

### Hardware Setup
- **Master:** Raspberry Pi with RT Kernel (see `release/integration_bench/`).
- **Slaves in Bus:**
  - 1x Freedom-K64F (NuttX slave).
  - 1x XMC4800 Relax Kit (NuttX slave).

### Test Procedure
1. **Fetch CI Artifacts:** Download the latest artifacts from the RC tag's CI run:
   - `build-arm64` (containing `hw_test_bench` for RPi).
   - `firmware-freedom-k64f-nuttx-stable` (NuttX firmware, pinned version).
   - `firmware-xmc4800-relax-nuttx-stable` (NuttX firmware, pinned version).
2. **Deploy:** Connect slave boards to the PC via J-Link, then run `release/hw_test_deploy.sh` (see Section 3).
3. **Run:** Reconnect slaves to the Pi, SSH in, and run `hw_test_run.sh` on the Pi.
4. **Monitor:** Use `hw_test_status.sh` to check progress at any time.
5. **Validation:** The test is successful if `hw_test_status.sh` reports `COMPLETED`:
   - `hw_test_bench` self-validates WC errors and lost frames against a calibrated baseline (exits with error on regression).
   - The monitor detects `hw_test_bench` crashes and reports `CRASHED`.
   - CPU and memory are checked via rolling-average thresholds (5% above baseline); breaches are reported as `THRESHOLD_BREACH`.

## 3. Deployment & Test Scripts

The hardware test is split into three scripts: deploy (PC-side), run (Pi-side), and status (either side).

### Step 1: Deploy (from PC)

Flash slave firmware and/or deploy the master binary to the Pi. At least one of `-m`, `-f`, `-x` must be specified. `-a` is required when deploying master.

```bash
# Full deployment (slaves + master)
./release/hw_test_deploy.sh \
    -m ./path/to/build-arm64.zip \
    -f ./path/to/firmware-freedom-k64f-nuttx-stable.zip \
    -x ./path/to/firmware-xmc4800-relax-nuttx-stable.zip \
    -a <pi-ip-address>

# Master only (e.g. after a rebuild)
./release/hw_test_deploy.sh -m ./path/to/build-arm64.zip -a <pi-ip-address>

# Flash a single slave board
./release/hw_test_deploy.sh -f ./path/to/firmware-freedom-k64f-nuttx-stable.zip
```

### Step 2: Run (on Pi)

After reconnecting slave boards to the Pi's EtherCAT network:

```bash
ssh pi@<pi-ip-address>
cd ~/hw_test
./hw_test_run.sh -i eth0 -d 2592000   # 30 days, pinned to CPU core 1 by default
```

The script calibrates CPU/MEM baselines interactively, then self-daemonizes. You can safely disconnect from SSH.

Available options:
- `-i <interface>` — Network interface (required).
- `-s <n>` — Expected slave count (default: 2).
- `-c <core>` — CPU core to pin `hw_test_bench` to (default: 1, should be an isolated core).
- `-d <seconds>` — Test duration (default: 2592000 / 30 days).

### Step 3: Check Status

From the Pi:
```bash
cd ~/hw_test && ./hw_test_status.sh
```

Or remotely from the PC:
```bash
./release/hw_test_status.sh -a <pi-ip-address>
```

Use `-f` to follow the monitor log continuously.

### Stopping a Running Test

```bash
cd ~/hw_test && ./hw_test_run.sh --stop
```

This gracefully terminates the monitor and `hw_test_bench`, and sets the test status to `STOPPED`.

### Features
- **Selective Deployment:** Deploy master, freedom, and/or XMC independently.
- **SSH Key Management:** Automatically generates and deploys SSH keys to the Pi for passwordless access.
- **Slave Deployment:** Uses `scripts/deploy_artifacts.sh` to flash locally connected NuttX slaves.
- **Capabilities (no root):** Uses `setcap` instead of `sudo` for raw socket access.
- **CPU Pinning:** `hw_test_bench` is pinned to an isolated CPU core via `taskset`.
- **Self-daemonizing:** The monitoring loop detaches from SSH and survives disconnection.
- **Resource Monitoring:** Automatically detects CPU runaway and memory leaks via rolling-average thresholds (5% above calibrated baseline).
- **Graceful Stop:** `--stop` cleanly terminates a running test.

### State Files (on Pi, in `~/hw_test/`)
- `hw_test_status.txt` — machine-readable current status (updated every sample).
- `hw_test_monitor.log` — periodic resource metrics log.
- `hw_test.log` — `hw_test_bench` stdout/stderr.
- `hw_test.pid` / `hw_test_bench.pid` — PID files for the monitor and bench processes.

## 4. Official Release

Once the RC is validated via hardware tests:

1. Tag the official release:
   ```bash
   git tag -a v1.2.0 -m "Official Release v1.2.0"
   git push origin v1.2.0
   ```
2. GitHub Actions will automatically:
   - Build and upload Wheels to PyPI.
   - Create a GitHub Release with attached wheels.

## 5. Post-Release Verification

### Wheel Deployment
1. Verify the package is available on PyPI: `pip install kickcat==1.2.0`.
2. Perform a manual test on a clean machine:
   ```bash
   python3 -m venv test_env
   source test_env/bin/activate
   pip install kickcat
   python3 -c "import kickcat; print(kickcat.__version__)"
   ```

### Conan Release
Update the Conan Center Index (if applicable) or the local Conan repository using the provided script:
```bash
./release/conan-version-deploy.sh 1.2.0 /path/to/conan-center-index-fork
```

## 6. Rollback Plan
If a critical issue is found post-release:
1. Yank the version from PyPI if necessary.
2. Issue a hotfix (e.g., `1.2.1`) following this same procedure but with an accelerated (2 hour) hardware test if the fix is low-risk.
