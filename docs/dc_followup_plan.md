# DC subsystem follow-up plan

Tracks work deferred from the `fix_dc_sync` PR. Two follow-up PRs, landed in this order to keep each change safe.

---

## PR #1 — DC unit tests

**Goal:** lay down a regression-catching safety net for the DC subsystem before making any further functional changes.

**Scope:** pure unit tests against existing DC code. No behavior change. No new features.

### New file

`unit/src/dc-t.cc`

### Test matrix

#### A. Propagation delay calculation (`computePropagationDelay`)

Highest-value target — pure math, topology-sensitive, historically uncovered. The Y-branch-2 convergence issue observed on the Marvin robot would likely have been caught here.

Approach: construct mock slaves with synthetic port receive times, call `computePropagationDelay`, verify `slave.delay`.

| ID | Test | What it verifies |
|----|------|------------------|
| T1 | Linear 2-slave chain | Basic formula: `delay = (t_port1 - t_port0) / 2` |
| T2 | Linear 4-slave chain | Cumulative delay chaining |
| T3 | Y-topology, symmetric branches | Both branches produce matching delay profiles |
| T4 | Y-topology, asymmetric branch depths | Per-branch delay chains independent |
| T5 | Mixed DC / non-DC slaves (linear) | `parenthold` logic: non-DC 2-port slaves treated as wire delay |
| T6 | `tDiff` non-zero | Correctly applied with proper sign |
| T7 | 32-bit port time wraparound | `portDelta` wrap handling invoked correctly |

#### B. Timer `apply_offset`

| ID | Test | What it verifies |
|----|------|------------------|
| T8 | Initial state | `filtered_drift()` starts at 0ns; first call seeds only |
| T9 | Zero delta | Repeated identical offset → no drift, no deadline shift |
| T10 | Steady-state convergence | 1000 calls with linear drift → `filtered_drift` converges to per-cycle rate |
| T11 | Sign handling | Negative drift → negative `filtered_drift`, deadline decreases |
| T12 | Clock anomaly absorption | Single large delta absorbed smoothly by IIR |

#### C. `dcMasterOffset`

| ID | Test | What it verifies |
|----|------|------------------|
| T13 | Before DC init | Returns `0ns` (`dc_slave_` nullptr) |
| T14 | After FRMW capture | Returns expected signed offset |
| T15 | Sign convention | Master ahead → positive; behind → negative |

#### D. `isDCSynchronized`

| ID | Test | What it verifies |
|----|------|------------------|
| T16 | All slaves within threshold | Returns `true` |
| T17 | One slave above threshold | Returns `false` |
| T18 | Sign-magnitude encoding | Bit-31 set value compared as positive magnitude |

### Infrastructure

- Reuse the `MockLink` pattern already in `unit/src/bus-t.cc`
- Helpers to construct synthetic `Slave` with specified ports active, ESC features, and port receive times
- Helper to inject mock FRMW responses into the capture callback

### Acceptance

- All tests pass on current `master` without any source changes
- `dc.cc` line coverage rises from ~0% to >70%
- No hardware dependency (all tests use mocks)

---

## PR #2 — ESC catalog and per-chip `tDiff`

**Goal:** replace the hardcoded `tDiff = 0ns` in `computePropagationDelay` with a per-slave lookup, seeded with known-good values for common ESCs.

**Depends on:** PR #1 (tests must be in place first — they'll catch any regression introduced by the lookup plumbing).

### Structure

**New files**
- `lib/master/include/kickcat/esc_catalog.h`
- `lib/master/src/esc_catalog.cc`

**Modified**
- `lib/master/src/Prints.cc` — delete local `typeToString`, delegate to catalog
- `lib/master/src/dc.cc` — per-slave `tDiff` lookup
- `unit/src/dc-t.cc` — extend T6 to use real catalog entries

### API

```cpp
namespace kickcat::esc_catalog
{
    /// Human-readable ESC type name. Returns "Unknown" if not catalogued.
    char const* typeToString(uint8_t esc_type);

    /// Difference between processing and forwarding delay
    /// (ESC datasheet §9.1.2 Table 30). Returns 0ns for uncatalogued types.
    nanoseconds tDiff(uint8_t esc_type);

    /// Override tDiff for a specific ESC type, e.g. for a characterized
    /// custom or uncatalogued chip.
    void registerTDiff(uint8_t esc_type, nanoseconds value);
}
```

### Catalog entries

One `EscInfo` record per chip: `{ type, name, tDiff }`. Each entry **must cite its datasheet source** in a comment. For chips where the tDiff value cannot be verified, use `0ns` — conservative default, same as today.

Initial entries:

| Type | Chip | tDiff | Source |
|------|------|-------|--------|
| 0x01 | First terminals | 0ns | uncatalogued |
| 0x02 | ESC10, ESC20 | 0ns | uncatalogued |
| 0x03 | First EK1100 | 0ns | uncatalogued |
| 0x04 | IP Core | 0ns | FPGA-implementation dependent |
| 0x05 | Internal FPGA | 0ns | user-specific |
| 0x11 | ET1100 | ~140ns | Beckhoff ESC datasheet §III — **verify before commit** |
| 0x12 | ET1200 | ~140ns | Beckhoff ESC datasheet §III — **verify before commit** |
| 0x91 | TMS320F2838x | 0ns | no public figure |
| 0x98 | XMC4800 | 0ns | no public figure |
| 0xc0 | LAN9252 | ~60ns | Microchip datasheet — **verify before commit** |

All numeric values listed as `~` must be confirmed against the datasheet before the PR is merged. If a value can't be verified, leave it at `0ns` with a `// tDiff unknown` comment — strictly no guessing.

### Scope decisions (recorded)

- **Static global state** for overrides (not per-Bus) — catalog is a chip property, not a master instance property.
- **ET1100 and ET1200 are separate entries** even if their tDiff value is the same — different type IDs, different catalog rows.
- **Conservative default (0ns) for unknowns** — explicitly exclude guessed values for EtherCAT G and future high-speed variants. Those get proper entries when their type IDs appear and their datasheets are consulted.
- **No thread safety** — KickCAT is single-threaded by design (embedded μC target).

### Formula change

Replace in `computePropagationDelay`:

```cpp
constexpr nanoseconds tDiff = 0ns;
```

with a per-slave lookup, using the correct slave's type (parent or child, per spec §9.1.2.2 — verify during implementation, not before).

### Testing

- Unit tests in `esc_catalog-t.cc`:
  - Known types return expected name and `tDiff`
  - Unknown types return `"Unknown"` / `0ns`
  - `registerTDiff` adds a new value
  - `registerTDiff` override wins over catalog value
- Integration: rerun the 17-slave Y-topology. If slaves are catalogued, expect branch-2 initial drift to decrease. If not catalogued, baseline unchanged (0ns default preserves current behaviour).
- Regression: all tests from PR #1 continue passing.

### Acceptance

- All new tests pass
- PR #1 tests still pass (no regression)
- `tDiff` values traceable to datasheet citations in the code

---

## Deferred: tDiff measurement tool

**Not included in PR #2.** Listed here so it's not lost.

### Goal

Let users characterize an uncatalogued ESC **in-situ** (inside an already-assembled network — no physical modification required), and emit a ready-to-paste catalog entry.

### Use case

User has a robot arm with Chinese motor drives. Can't easily swap drives for benchmark. Wants to characterize the drive's tDiff without disassembling the hardware.

### Approach A: derive from receive-time registers (preferred)

Use the ESC's own timing registers:

- `0x0900–0x090F` — per-port receive times (first preamble bit)
- `0x0918` — ECAT processing unit receive time
- `0x092C` — system time difference (after PLL stabilization)
- `0x0932` — speed counter difference

The math combines per-port receive times on the target slave with known cable delays (computed from adjacent slaves' own receive times) to isolate tDiff for the target. Works even when the slave is in the middle of a chain.

Precision limited by the ESC's 10 ns clock quantization, which is fine for values in the 40–150 ns range.

### Approach B: PLL observable differencing (fallback)

After a stable init, compare `0x0932` (speed counter diff) across same-chain-position slaves of the same chip type. Identical chips should show identical speed counter values once converged; discrepancies hint at tDiff miscalibration. Gives a *relative* signal, not an absolute value — less useful but simpler and a useful cross-check.

### Tool layout

```
tools/measure_tdiff/
    main.cc        # argparse; --slave <addr>, --reference-type <type>, --all
    # Output: tDiff estimate in ns with confidence bound
    # Output format: C++ snippet ready to paste into esc_catalog.cc
```

### Validation before release

Run on a network where the catalog already contains a characterized slave (e.g., ET1100 at position 0). The tool must report the catalog's known-good value within ±10 ns — or it's not ready to use on uncharacterized slaves.

### Why deferred

Measurement math needs careful formula work. A hasty implementation would pollute the catalog with wrong values — worse than no tool at all. Ship the catalog first so users have a stable base to verify against; tackle the tool with proper test coverage once PR #2 is out.

---

## Ordering summary

```
   fix_dc_sync PR  (current)
           │
           ▼
   PR #1: DC unit tests       (safety net, no behaviour change)
           │
           ▼
   PR #2: ESC catalog + tDiff (tractable improvement)
           │
           ▼
   PR #3: Measurement tool    (deferred, needs validation)
```

Each PR is independently valuable: stopping after any of the three still delivers real improvement over the current state.
