# builder_sphere_ai

High-performance event builder, coincidence sorter, and data processing pipeline for nuclear physics detector arrays (HPGe, BGO Compton suppression shields, LaBr₃ scintillators, and particle detectors).

`builder_sphere` ingests high-throughput raw digitizer data in Cap'n Proto binary streams, performs time-walk / delay alignment, reconstructs multi-channel coincidence events with anti-coincidence vetoes, and formats the built events into GASP and CERN ROOT output formats.

---

## Features

- **Coincidence Engine**:
  - Sliding time-window coincidence sorting with sub-nanosecond precision.
  - Multi-detector priority queuing and anti-coincidence suppression (e.g. BGO vetoing for Compton-suppressed HPGe).
  - Configurable channel-to-detector lookup tables (LUT), pair coincidence masks, and time-delay matrices.
- **Genetic Time Aligner (`align`)**:
  - Evolutionary optimization algorithm for automated fine-tuning of inter-detector delay offsets and time-walk parameters.
- **Multi-Core Dispatcher (`build`)**:
  - Parallel batch processing of multiple runs across CPU cores.
  - RPC-based live state synchronization and progress reporting using Cap'n Proto RPC (`EzRpcServer`).
- **Flexible Output Support**:
  - Standard **GASP** binary format (compatible with gamma-ray analysis packages such as Radware).
  - **CERN ROOT** trees and 1D/2D histograms.
- **Zero-Allocation Flat-Memory Architecture**:
  - Cache-friendly contiguous event buffers eliminating per-event heap churn.
  - In-place parallel sorting with Intel TBB.
  - Memory footprint reduced by ~58% (~3.6 GB peak RSS for 1 GB raw streams containing 80+ million events) while maintaining 100% bit-for-bit output reproducibility.
- **Ancillary Tools**:
  - `config_editor.py`: Python GUI for visual configuration of detector mappings, coincidence matrices, and delay parameters.
  - `viewWaveform.py`: Rapid plotting and inspection of digitizer pulse waveforms.
  - `compare_gasp_runs.py`: Standard-library-only GASP binary decoder, structural validator, and run-to-run comparator driven by `config.json`.

---

## Directory Structure

```text
builder_sphere_ai/
├── builder.cpp             # Core event builder and coincidence sorter executable
├── dispacher.cpp           # Multi-threaded job orchestrator and RPC server
├── config.json             # Default configuration template
├── configSchema.json       # JSON Schema definition for configuration validation
├── Makefile                # Build automation
├── LICENSE                 # MIT License
├── eventProto.capnp        # Cap'n Proto serialization schema
├── config_editor.py        # Graphical configuration editor (Tkinter)
├── viewWaveform.py         # Pulse waveform visualizer
├── compare_gasp_runs.py    # Config-driven GASP binary validator & run comparator
├── include/                # C++ headers (data loaders, writers, sync, utilities)
├── src/                    # C++ implementations and generated Cap'n Proto code
├── input/                  # Input directory (raw .cap files and run configs - ignored by git)
└── output/                 # Output directory (built GASP/ROOT files - ignored by git)
```

---

## Prerequisites & Dependencies

The project requires a Linux environment (or WSL on Windows) with the following tools and libraries:

1. **C++ Compiler**: GCC `g++` (C++17 support)
2. **CERN ROOT** (v6.x or newer):
   - Required libraries: `libCore`, `libTree`, `libRIO`, `libHist`
3. **Cap'n Proto & KJ** (v0.8+):
   - Tools: `capnp`, `capnpc-c++`
   - Libraries: `libcapnp`, `libcapnp-rpc`, `libkj`, `libkj-async`
4. **Intel TBB & OpenMP**:
   - `libtbb`, `-fopenmp`
5. **Python 3** (for utilities):
   - `python3-tk` (for `config_editor.py`)
   - `matplotlib`, `numpy` (for `viewWaveform.py`)

---

## Build Instructions

### 1. Set Up Environment Variables
Ensure ROOT and project paths are defined in your shell:

```bash
# Source your ROOT installation
source /path/to/root/bin/thisroot.sh

# Set base environment variables (pointing to this directory)
export ROOT_BASE=$ROOTSYS
export BUILDER_BASE=$(pwd)
```

### 2. Generate Cap'n Proto Serialization Code
`include/eventProto.capnp.h` and `src/eventProto.capnp.c++` are **generated** and are not tracked in git. This is deliberate: the generated header contains a hard version guard

```cpp
#if CAPNP_VERSION != 8000
#error "Version mismatch between generated code and library headers. ..."
#endif
```

so committing it would pin the repository to a single Cap'n Proto release. `make` regenerates the files automatically (`capnp_sync.sh`) whenever the schema is newer than the generated code, or whenever the locally installed Cap'n Proto version differs from the one that produced it. Nothing needs to be done by hand on a fresh clone.

To regenerate explicitly (for example after editing `eventProto.capnp`):

```bash
make cap
```

To inspect the three versions involved — the `capnp` compiler, the Cap'n Proto library headers actually used for compilation, and the version the generated code came from:

```bash
make capnp-check
```

```
capnproto compiler : 0.8.0
capnproto library  : 8000
generated code     : 8000 (include/eventProto.capnp.h)
status             : up to date
```

### 3. Compile Executables
Compile both the dispatcher (`build`) and core builder (`builder`):

```bash
make
```

To clean intermediate object files and binaries:
```bash
make clean
```

**Cap'n Proto version requirements and troubleshooting**

- The `capnp` **compiler and library must be the same version** (e.g. `capnproto` / `libcapnp-dev` 0.8.x together, or both 1.x). Mismatched pairs are rejected up front with an explicit error.
- Different Cap'n Proto versions across machines are fine: each machine generates its own code from `eventProto.capnp`. The schema pins the file ID (`@0x86067e6a0fae5e5e;`), so the wire format is version independent and existing `.cap` files stay readable.
- If the build stops with `Version mismatch between generated code and library headers`, run `make cap` (or `make clean && make`) to regenerate against the installed version.
- If `capnp` is missing, the build reuses existing generated code when it is already valid for the installed library; otherwise it fails with instructions to install the compiler package (`capnproto` on Debian/Ubuntu, `capnproto-devel` on Fedora/RHEL — these provide both `capnp` and `capnpc-c++`).


---

## Configuration (`config.json`)

The behavior of `builder_sphere` is driven by a central JSON configuration validated against `configSchema.json`. Key sections include:

- **`general`**:
  - `"run"`: Operation mode — `"build"`, `"convert"`, `"align"`, or `"delay"`.
  - `"maxCores"`: Number of CPU threads to utilize.
  - `"timeUnitMultiplier"`: Time scale conversion factor.
- **`input`**:
  - `"path"`: Directory containing input `.cap` files.
  - `"fileIdentifier"`: Prefix/substring filter for input filenames.
  - `"keepIndex"`: Range of run indices to process `[start, stop]`.
- **`build`**:
  - `"coincWindow"`: Coincidence time window (in time units, e.g. ps or ns).
  - `"coincMask"`: Matrix defining valid coincidence combinations across detector types.
  - `"antiCoincFlag"`: Detector types treated as vetoes (e.g. BGO shield veto).
  - `"typePriority"`: Priority order for event ordering within a coincidence group.
- **`detector`**:
  - `"lut"`: Channel-to-detector lookup mapping.
  - `"delay"`: Time offset calibration values per channel.
  - `"type"`: Classification ID per channel (e.g. 0: HPGe, 1: LaBr₃, 2: BGO, 3: particle cell).
- **`output`**:
  - `"path"`: Destination directory for built runs.
  - `"fileFormat"`: Desired format (`"gasp"` or `"root"`).

---

## Running the Builder

### Standard Build Workflow
1. Place raw `.cap` data files into `input/`.
2. Configure `config.json` with desired detector parameters and coincidence settings.
3. Launch the dispatcher:
   ```bash
   ./build config.json
   ```
4. Output files will be generated in `output/` with run names matching the input datasets.

### Running from Any Directory (`~/.bashrc`)
To run the dispatcher from any experiment directory containing a `config.json`, make sure ROOT (`thisroot.sh`) is sourced and add a shell function to `~/.bashrc`. Scoping `BUILDER_BASE` and `PATH` to the function avoids conflicts when both the original `builder_sphere` and `builder_sphere_ai` are installed on the same machine (ensuring each dispatcher loads its own `configSchema.json` and launches its own `builder` worker):

```bash
# Example ~/.bashrc function (adjust the path to your installation directory)
build-ai() {
    BUILDER_BASE="/home/dfn/Installed/builder_sphere_ai" \
    PATH="/home/dfn/Installed/builder_sphere_ai:$PATH" \
    /home/dfn/Installed/builder_sphere_ai/build "$@"
}
```

Reload your shell (`source ~/.bashrc`), then run `build-ai` from any directory containing `config.json`.

### Direct Execution of the Builder
To run a single file directly through the low-level builder engine:
```bash
./builder config.json
```

---

## Graphical Tools

### Configuration Editor
Launch the GUI to inspect and modify detector maps, delay matrices, and coincidence masks interactively:
```bash
python3 config_editor.py config.json
```

### Waveform Viewer
Inspect raw digitizer traces from an input run:
```bash
python3 viewWaveform.py
```

### GASP Run Comparison & Validation (`compare_gasp_runs.py`)
Decode, structurally validate, and compare GASP binary outputs (e.g., baseline vs. modified builder runs) using only the Python 3 standard library (`config.json` drives `eventMask` decoding and `(board, channel, delay)` annotations):

What it checks:
- **Header & Block Integrity**: Validates `0x4748` (`GH`) file headers, `0x4758` (`GX`) block headers, `0xF000 | size` event markers, ascending LUT ordering, configured LUT membership, and `build.coincMask` compliance.
- **Aggregate & Per-LUT/Board Statistics**: Event word sizes, `scaledTime` range, per-type multiplicities (plus constituent `t0`/`t1` non-zero counts in `hpgeBGO`), field min/max ranges, and per-LUT hit deltas annotated with `(board, channel, delay)`.
- **Resynchronized Stream Alignment**: Uses `difflib.SequenceMatcher(..., autojunk=False)` to classify discrepancies into `modified_detectors`, `modified_values_only`, `only_in_old`, and `only_in_new`, and `--find-first-diff` streams full files when differences start past event `N`.

Usage examples:
```bash
# Default: compare the first 1000 events of all matching runs in output/old vs output
python3 compare_gasp_runs.py

# Stream beyond N=1000 until the first event-level divergence if the prefix matches
python3 compare_gasp_runs.py --find-first-diff

# Inspect a specific run, larger event prefix, decoded event dump, and JSON summary
python3 compare_gasp_runs.py --runs Run_000170 -n 5000 --max-diffs 10 --dump-events 2 --json-report report.json

# Override config and input directories
python3 compare_gasp_runs.py --config output/config.json --old-dir output/old --new-dir output
```

---

## Performance & Optimization

In September 2026, the `builder_sphere` pipeline underwent major architectural optimizations spanning data ingestion, memory layouts, parallel concurrency, and vectorized binary compilation.

### Benchmark Progression Summary

All benchmarks were conducted under WSL (Ubuntu) on an Intel/AMD multi-core workstation (`maxCores: 10`) processing real experimental nuclear physics digitizer runs in Cap'n Proto binary format:

| Optimization Milestone | Run 1 (40 MB, ~3.3M Events) | Run 2 (1.0 GB, ~82.8M Events) | Peak RAM (Run 2) | Speedup vs Baseline | Output MD5 Match |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Original Baseline** | 7.0s | ~210s (3m 30s) | ~8.5 GB | Baseline | Reference |
| **Phase 1: Flat-Memory Arch** | 6.3s | 133s (2m 13s) | 3.6 GB | 1.6x faster (*-58% RAM*) | **100% Bit-for-bit match** |
| **Phase 2: Multi-Core Parallel** | 4.55s | 93.7s (1m 33s) | ~3.6 GB | 2.2x faster | **100% Bit-for-bit match** |
| **Phase 3A: SIMD + Zero-Map Writer** | 2.47s | 28.87s | ~3.6 GB | > 7.2x faster | **100% Bit-for-bit match** |
| **Phase 3B: Sliding-Window Streaming** | ~1.0s | 17.75s | 49.9 MB (*-99.4% RAM*) | > 11.8x faster | **100% Bit-for-bit match** |
| **Phase 4: Double-Buffered & Multi-Run** | **0.8s** | **16.20s** | **49.9 MB** (*-99.4% RAM*) | **> 13.0x FASTER** | **100% Bit-for-bit match** |

The 1.0 GB raw run (`run_Eu152_000002.cap`) now reconstructs in **16.20 seconds** (down from 3.5 minutes, **> 13.0x faster**), while slashing peak RAM from **8.5 GB down to under 50 MB** (**99.4% memory reduction**), with **exact bit-for-bit output reproducibility**.

### Multi-Run Concurrent Batch Benchmark (10 CPU Cores)

The true power of the streaming engine and double-buffering architecture was demonstrated by processing **all 4 experimental runs simultaneously** (`run 1`, `run 2`, `run 3`, `run 4`, total **~2.9 GB raw binary data**, **~250 million total events**) using the multi-process dispatcher across 10 CPU cores:

| Metric | Original Baseline | Optimized `builder_sphere` | Performance Gain |
| :--- | :--- | :--- | :--- |
| **Total Wall-Clock Time (All 4 Runs)** | ~700s (~11.5 minutes, sequential) | **25.04 seconds** | **> 27.9x faster turnaround** |
| **Total CPU Work (User Time)** | ~1,200s | **81.08s** | **> 14.8x CPU efficiency** |
| **Sustained Multi-Core CPU Utilization** | ~100% | **400%** | **4x higher parallel throughput** |
| **Peak Batch RAM (All Runs Concurrently)** | **~34 GB** (*out-of-memory crash*) | **~300 MB** | **99.1% memory reduction** |
| **System Stability** | Crashed on workstations (< 32 GB RAM) | **Smooth, zero memory spikes** | Stable on any laptop/workstation |
| **Bit-for-Bit Correctness** | Reference | **100% Bit-for-bit match** | Hashes match baseline references |

For a phase-by-phase summary of completed optimizations, streaming anti-splitting safeguards, and full subsystem documentation, see [FORK_CHANGES_MANUAL.md](FORK_CHANGES_MANUAL.md#detailed-summary-of-completed-optimizations).

---

## License & Authors

Licensed under the [MIT License](LICENSE).

Originally created by **Gabriel Turturica** ([@gabriel-turturica](https://github.com/gabriel-turturica)) in [`gabriel-turturica/builder_sphere`](https://github.com/gabriel-turturica/builder_sphere). This repository ([`rlica/builder_sphere_ai`](https://github.com/rlica/builder_sphere_ai)) is derived from the original codebase and is maintained with the assistance of LLMs, with the goal of optimizing CPU throughput and RAM usage while preserving bit-for-bit output parity.

