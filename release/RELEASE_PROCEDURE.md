# KickCAT Release Procedure

This document outlines the mandatory steps to be followed when releasing a new version of KickCAT. The process ensures stability, hardware compatibility, and correct deployment across supported platforms.

## 1. Release Candidate (RC) Tagging

Before an official release, a Release Candidate must be created to trigger the full CI pipeline and provide artifacts for hardware testing.

1. Ensure the version in `conanfile.py` and `pyproject.toml` is updated to the target version (e.g., `1.2.0-rc1`).
2. Update the `CHANGELOG.md` (if present) or internal release notes.
3. Push a tag with the `-rc` suffix:
   ```bash
   git tag -a v1.2.0-rc1 -m "Release Candidate 1 for v1.2.0"
   git push origin v1.2.0-rc1
   ```
4. Verify that the GitHub Actions CI pipeline completes successfully for the tag.

## 2. Hardware Long-Duration Test (6 Hours)

Stability is critical for EtherCAT applications. A 6-hour continuous run must be performed on actual hardware.

### Hardware Setup
- **Master:** Raspberry Pi (any model with Ethernet, 4 or 5 recommended).
- **Slaves in Bus:**
  - 1x Freedom-K64F (NuttX slave).
  - 1x XMC4800 Relax Kit (NuttX slave).
  - (Optional) EasyCAT or other LAN9252-based slaves.

### Test Procedure
1. **Fetch CI Artifacts:** Download the latest `build-linux` (for RPi) and `firmware-*.bin` artifacts from the RC tag's CI run.
2. **Deploy Slaves:** Use `scripts/deploy_artifacts.sh` to flash the Freedom and XMC boards.
3. **Deploy Master:** `scp` the compiled `easycat_example` or the Python wheel to the Raspberry Pi.
4. **Run Test:** Execute the master for at least **6 hours** in a cyclic loop.
   - Use `tools/checkNetworkStability.cc` or an example app that monitors `AL Status` and `WC` (Working Counter) errors.
5. **Validation:** The test is successful if:
   - Zero lost frames are reported (or within acceptable low threshold for the environment).
   - No slave dropped out of `OP` state.
   - No crashes or memory leaks (check `dmesg` and process memory usage).

## 3. Automatic Deployment & Test Script

To facilitate the hardware test, an automated script should be used to gather artifacts and deploy them.

### Automated Workflow
1. **Gather Links:** Get the artifact download URLs from the GitHub Action run.
2. **Run Deployment Script:** (Proposed/Internal tool)
   ```bash
   ./release/hw_test_deploy.sh --version v1.2.0-rc1 --pi-addr <pi-ip>
   ```
   *This script should:*
   - Download artifacts using `gh run download` or direct links.
   - Use `scp` to move the master binary/wheel to the Pi.
   - Use `scripts/deploy_artifacts.sh` (locally connected) or a remote trigger to flash slaves.
   - Start the master process on the Pi.

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
1. Verify the package is available on PyPI: `pip install kickcat==1.2.0` (wait a few minutes for index update).
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
