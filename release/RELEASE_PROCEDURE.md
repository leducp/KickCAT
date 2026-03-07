# KickCAT Release Procedure

This document outlines the mandatory steps to be followed when releasing a new version of KickCAT. The process ensures stability, hardware compatibility, and correct deployment across supported platforms.

## 1. Release Candidate (RC) Tagging

Before an official release, a Release Candidate must be created to trigger the full CI pipeline and provide artifacts for hardware testing.

1. Ensure the version in `conanfile.py` and `pyproject.toml` is updated to the target version (e.g., `1.2.0-rc1`).
2. [TODO] Update the `CHANGELOG.md` (if present) or internal release notes. 
3. Push a tag with the `-rc` suffix:
   ```bash
   git tag -a v1.2.0-rc1 -m "Release Candidate 1 for v1.2.0"
   git push origin v1.2.0-rc1
   ```
4. Verify that the GitHub Actions CI pipeline completes successfully for the tag.

## 2. Hardware Long-Duration Test (6 Hours)

Stability is critical for EtherCAT applications. A 6-hour continuous run must be performed on actual hardware.

### Hardware Setup
- **Master:** Raspberry Pi with RT Kernel.
- **Slaves in Bus:**
  - 1x Freedom-K64F (NuttX slave).
  - 1x XMC4800 Relax Kit (NuttX slave).

### Test Procedure
1. **Fetch CI Artifacts:** Download the latest artifacts from the RC tag's CI run:
   - `build-arm64.zip` (containing `hw_test_bench` for RPi).
   - `firmware-freedom-k64f-nuttx-master.zip` (NuttX firmware).
   - `firmware-xmc4800-relax-nuttx-master.zip` (NuttX firmware).
2. **Run Orchestrator:** Use the `release/hw_test_orchestrator.sh` script to automate deployment and monitoring (see Section 3).
3. **Validation:** The test is successful if:
   - [TODO] Zero lost frames (exceptions) are reported.
   - [TODO] Zero Working Counter (WC) errors are reported.
   - [TODO] No slave dropped out of `OP` state.
   - [TODO] No memory leaks (RSS remains stable in `hw_test_results.txt`).

## 3. Automatic Deployment & Test Script

The `release/hw_test_orchestrator.sh` script automates the entire hardware validation process, including SSH setup, firmware flashing, and remote monitoring.

### Usage
```bash
./release/hw_test_orchestrator.sh \No memory leaks
    -m ./path/to/build-arm64.zip \
    -f ./path/to/firmware-freedom-k64f-nuttx-master.zip \
    -x ./path/to/firmware-xmc4800-relax-nuttx-master.zip \
    -a <pi-ip-address> \
    -d 21600
```

### Features
- **SSH Key Management:** Automatically generates and deploys SSH keys to the Pi for passwordless access.
- **Slave Deployment:** Uses `scripts/deploy_artifacts.sh` to flash locally connected NutttX slaves.
- **Remote Monitoring:** Deploys the master binary to the Pi, starts the test, and logs Resident Set Size (RSS) memory usage every 60 minutes.
- **Robust Cleanup:** Ensures the remote process is killed and temporary files are removed on script exit or interruption.

### Monitoring Results
- **Local Progress:** `hw_test_results.txt` (contains elapsed time and memory stats).
- **Remote Log:** `~/hw_test.log` on the Raspberry Pi (contains detailed `hw_test_bench` output).

## 4. Official Release

Once the RC is validated via hardware tests:

1. Update the version to the final official version (e.g., `1.2.0`) in:
   - `conanfile.py`
   - `pyproject.toml`
2. Commit and push the changes to `master`.
3. Tag the official release:
   ```bash
   git tag -a v1.2.0 -m "Official Release v1.2.0"
   git push origin v1.2.0
   ```
4. GitHub Actions will automatically:
   - Build and upload Wheels to PyPI.
   - Create a GitHub Release with attached binaries and wheels.

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
2. Revert the versioning commits.
3. Issue a hotfix (e.g., `1.2.1`) following this same procedure but with an accelerated (2 hour) hardware test if the fix is low-risk.
