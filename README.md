# builder_sphere

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

---

## Directory Structure

```text
builder_sphere/
├── builder.cpp             # Core event builder and coincidence sorter executable
├── dispacher.cpp           # Multi-threaded job orchestrator and RPC server
├── config.json             # Default configuration template
├── configSchema.json       # JSON Schema definition for configuration validation
├── Makefile                # Build automation
├── eventProto.capnp        # Cap'n Proto serialization schema
├── config_editor.py        # Graphical configuration editor (Tkinter)
├── viewWaveform.py         # Pulse waveform visualizer
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
If modifying `eventProto.capnp` or performing a fresh build:

```bash
make cap
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

---

## Performance & Optimization

In September 2026, the `builder_sphere` pipeline underwent major architectural optimizations (Phase 1: Flat-Memory Architecture & Phase 2: Multi-Core Parallel Engine):

### Benchmark Summary

| Optimization Stage | Run 1 (40 MB, ~3.3M Events) | Run 2 (1.0 GB, ~82.8M Events) | Peak RAM (Run 2) | Output MD5 Match |
| :--- | :--- | :--- | :--- | :--- |
| **Original Baseline** | 7.0s | ~210s (3m 30s) | ~8.5 GB | Reference |
| **Phase 1 (Flat-Memory Arch)** | 6.3s | 133s (2m 13s) | 3.6 GB (*~58% reduction*) | **100% Bit-for-bit match** |
| **Phase 2 (Multi-Core Parallel)** | **4.55s** (*~35% faster*) | **93.7s (1m 33s)** (*~55% faster*) | **~3.6 GB** | **100% Bit-for-bit match** |

### Key Optimizations

1. **Flat-Memory Event Buffers (Phase 1)**:
   - Eliminated all 82+ million `new event()` heap allocations in `capLoader` by reading directly into pre-allocated contiguous memory.
   - Packed event structures from 80 bytes down to 40 bytes per event.
2. **Multi-Core Parallel Coincidence Engine (Phase 2)**:
   - Parallelized the sliding-window coincidence search across all CPU cores using `tbb::parallel_for` with dynamic chunking.
   - Deterministic chunk merging preserves exact chronological ordering for 100% bit-for-bit output reproducibility.
3. **Zero-Contention Parallel Sort (Phase 2)**:
   - Stripped thread-local atomic tracking counters from `tbb::parallel_sort`'s inner comparator lambda, eliminating ~2.1 billion TLS lookups and atomic bus locks.
4. **L1-Cached Channel Descriptor Table (Phase 2)**:
   - Precomputed a 2D channel lookup array (`[32][64]`) fitting in L1 CPU cache, eliminating 82.7 million `std::map` tree searches and disjoint vector indirections during data ingestion.
   - Throttled progress reporting to avoid thousands of `lseek(fd, 0, SEEK_CUR)` Linux system call context switches.
5. **Zero-Copy & Scratch-Buffered Output Pipeline (Phases 1 & 2)**:
   - Reused static scratch buffers in `postProcessEvent` and overlap resolution to eliminate millions of dynamic vector heap allocations.
   - Replaced deep copying of mapping vectors with `const auto &` references and direct function indexing in `gaspDataWriter`.

*Note: Antigravity with Gemini 3.8 Flash High was used for profiling, flat-memory architectural refactoring, and multi-core parallel zero-allocation optimizations.*


---

## License & Authors

Maintained by **Razvan Lica** ([@rlica](https://github.com/rlica)) and contributors.
Developed for experimental nuclear physics data acquisition and offline event reconstruction.
