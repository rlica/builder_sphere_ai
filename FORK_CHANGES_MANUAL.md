# `builder_sphere` Fork Changes & Technical Reference Manual

**Upstream Baseline:** `gabriel-turturica/builder_sphere` (`master` @ `67f4d37865f50a907cdf52ab67f19b49728b3ccb`, last pushed `2025-10-21`)  
**Current Fork Head:** `rlica/builder_sphere` (`master`, tag `v1.0.1` @ `217006744ce9b290ef1727eb14ac9d4798aac89a`)  
**Divergence:** **10 commits ahead, 0 commits behind** upstream (`22` files changed, `+2,633` insertions, `-7,500` deletions)

---

## 1. Executive Summary

This manual documents every architectural, algorithmic, build-system, configuration, and tooling change introduced in the `rlica/builder_sphere` fork relative to the upstream `gabriel-turturica/builder_sphere` `master` branch.

### Key Outcomes

- **> 13.0× Faster Single-Run Processing:** A 1.0 GB raw Cap'n Proto dataset (~82.8M events, `run_Eu152_000002.cap`) drops from **~210s (3m 30s)** on upstream to **16.20s** (and 40 MB / ~3.3M events drops from **7.0s** to **0.8s**).
- **99.4% Lower Peak Memory Footprint:** Peak RSS for the 1.0 GB run drops from **~8.5 GB** (and 3.6 GB after Phase 1) to **49.9 MB** via a double-buffered sliding-window streaming engine (`< 60 MB` constant memory budget).
- **> 27.9× Faster Multi-Run Concurrent Batch Turnaround:** Processing 4 experimental runs concurrently (~2.9 GB total, ~250M events across 10 CPU cores) completes in **25.04s** wall-clock time (**~300 MB** total peak RAM), compared to **~700s** sequential and **~34 GB** OOM crashes on upstream.
- **100% Bit-for-Bit Output Parity:** Verified via MD5 cryptographic checksums against upstream baseline outputs (`run_Eu152_000001` and `run_Eu152_000002`).
- **Cap'n Proto Version Independence (0.8.x & 1.x):** Untracked generated `eventProto.capnp.*` sources and added `capnp_sync.sh` + `Makefile` integration so fresh clones build automatically against any installed Cap'n Proto release without header-guard `#error` failures.
- **Live RPC Progress Dashboard & Monitoring:** Added a `StateSynchronizer` Cap'n Proto RPC service in `dispacher.cpp` (`build`) with an ANSI terminal dashboard and persistent `builderStatus.txt` status table.
- **New Ancillary Python Utilities & Documentation:** Added `config_editor.py` (Tkinter GUI configuration editor), began tracking `viewWaveform.py` (pulse waveform plotter), and replaced the stub `README.md` with full build, usage, and benchmark documentation.

*(Note: Most of the `-7,500` deleted lines in the diff come from untracking the previously committed generated Cap'n Proto sources `include/eventProto.capnp.h` and `src/eventProto.capnp.c++`, totaling 7,089 lines at HEAD.)*

---

## 2. Commit History & Release Timeline

All 10 commits between `67f4d37` (upstream `master`) and `2170067` (fork `master` / `v1.0.1`) in chronological order:

| Commit | Date | Tag / Phase | Summary |
| :--- | :--- | :--- | :--- |
| `ddea585` | `2026-09-19` | Phase 1 | Flat-memory `event` layout (~80 B → 40 B), `capLoader::readEventsFlat()`, index-based `tbb::parallel_sort`, lightweight `CoincRange`, `config_editor.py`, `viewWaveform.py`, `dataConfig`, `dataSyncClient`, `syncUtils`, `StateSynchronizer` RPC |
| `2c022a7` | `2026-09-19` | Docs | Added comprehensive `README.md` covering architecture, dependencies, build/run steps, tools, and Phase 1 benchmarks |
| `4125aa8` | `2026-09-20` | Phase 2 | Multi-core `tbb::parallel_for` coincidence search, L1-cached `FastChannelInfo channelTable[32][64]`, removed TLS atomic tracking from sort comparator, reusable scratch buffers, zero-copy `dumpToFile` |
| `6fecaf5` | `2026-09-20` | Docs | Updated `README.md` with Phase 2 multi-core benchmarks and optimization details |
| `6e54217` | `2026-09-20` | Phase 3A | Enabled `-O3 -march=native`, added `slotTable[16][32]` in `gaspDataWriter::addBuiltEvent`, added `posix_fadvise` sequential prefetching in `capLoader` |
| `fc477c8` | `2026-09-20` | Docs | Consolidated Phases 1, 2, and 3A benchmarks and technical summaries in `README.md` |
| `3a771aa` | `2026-09-20` | Phase 3B | Implemented sliding-window chunked streaming engine (`openStream`, `readEventsBatch`, `eventRing`, 200k-event jitter margin, cluster eviction, `< 50 MB` RAM) |
| `9acce22` | `2026-09-20` | Docs | Documented the three-tier safeguard against coincidence event splitting across streaming windows in `README.md` |
| `ec96bfa` | `2026-09-20` | Phase 4 (`v1.0.0`) | Asynchronous `std::async` double-buffered batch prefetching, parallel streaming coincidence search, static buffer hoisting in `gaspDataWriter`, RPC `try/catch` hardening for standalone runs |
| `2170067` | `2026-09-29` | Build (`v1.0.1`) | Untracked generated `eventProto.capnp.h` / `.c++`, added `capnp_sync.sh`, `pkg-config` flag resolution, `make cap`/`capnp-check`/`capnp-sync`, and fixed `OBJECTS` substitution for `.c++` files |

---

## 3. Upgrade & Build Guide (Migrating from Upstream `master`)

### Prerequisites

Ensure a Linux environment (or WSL on Windows) with:

1. **C++17 Compiler:** `g++` supporting `-std=c++17 -O3 -march=native`.
2. **CERN ROOT (v6.x or newer):** Providing `libTree`, `libRIO`, `libCore`, `libHist` under `$ROOT_BASE`.
3. **Cap'n Proto & KJ (v0.8+ or v1.x):**
   - Compiler tools: `capnp` and `capnpc-c++` (`capnproto` on Debian/Ubuntu, `capnproto-devel` on Fedora/RHEL).
   - Libraries/headers: `libcapnp`, `libcapnp-rpc`, `libkj`, `libkj-async` (`libcapnp-dev` on Debian/Ubuntu).
   - **Matching requirement:** The installed `capnp` compiler and C++ library headers must be the exact same version.
   - `pkg-config` is used automatically if installed (`pkg-config --cflags capnp`, `pkg-config --libs capnp-rpc`); otherwise `Makefile` falls back to `-lcapnp-rpc -lcapnp -lkj-async -lkj`.
4. **Intel TBB & OpenMP:** `libtbb` (`-ltbb`) and `-fopenmp`.
5. **Python 3 (for ancillary tools):**
   - `python3-tk` for `config_editor.py`.
   - `matplotlib`, `numpy`, `scipy` for `viewWaveform.py`.

### Environment Setup

```bash
source /path/to/root/bin/thisroot.sh
export ROOT_BASE=$ROOTSYS
export BUILDER_BASE=$(pwd)
```

Note that `BUILDER_BASE` must point to the repository root so `./build` can locate `$BUILDER_BASE/configSchema.json`.

### `Makefile` Targets & Build Workflow

| Command | Behavior |
| :--- | :--- |
| `make` | Automatically runs `capnp_sync.sh` (via `FORCE` / `src/.eventProto.capnp.stamp`) to ensure `include/eventProto.capnp.h` and `src/eventProto.capnp.c++` match the local Cap'n Proto headers, then compiles `build` (`dispacher.cpp`) and `builder` (`builder.cpp`). |
| `make capnp-check` | Reports `capnproto compiler`, `capnproto library`, `generated code`, and `status` (`up to date` or `needs regeneration (<reason>)`) without modifying any files. |
| `make cap` | Runs `bash ./capnp_sync.sh --force eventProto.capnp` to force regeneration of `include/eventProto.capnp.h` and `src/eventProto.capnp.c++`. |
| `make capnp-sync` | Runs `bash ./capnp_sync.sh eventProto.capnp src/.eventProto.capnp.stamp` (regenerates only when stale). |
| `make clean` | Removes `src/*.o`, `src/.eventProto.capnp.stamp`, `builder`, and `build`. |

#### Environment Variable Overrides (`Makefile` / `capnp_sync.sh`)

- `CAPNP` (default: `capnp`)
- `CAPNP_HEADER` (default: `include/eventProto.capnp.h`)
- `CAPNP_SOURCE` (default: `src/eventProto.capnp.c++`)
- `CAPNP_CFLAGS` (default: output of `pkg-config --cflags capnp`, passed as `EXTRA_CPPFLAGS` to the preprocessor check)
- `CPP` (default: `g++`, used by both `Makefile` and `capnp_sync.sh` to query `<capnp/common.h>` macros)

### Troubleshooting Cap'n Proto Version Mismatches

- **Existing untracked files after pulling `2170067`:** When pulling or checking out `master` on a workspace that previously had upstream `include/eventProto.capnp.h` or `src/eventProto.capnp.c++`, git removes those tracked files and ignores them. Running `make` automatically regenerates them for the local Cap'n Proto version.
- **Compiler/library mismatch:** `capnp_sync.sh` queries `#include <capnp/common.h>` via `$CXX $EXTRA_CPPFLAGS -E -dM -x c++ -` and rejects builds up front if `capnp --version` differs from `CAPNP_VERSION_{MAJOR,MINOR,MICRO}`. Install matching compiler and development library packages.
- **Missing `capnp` compiler:** If `capnp` is not in `PATH`, `capnp_sync.sh` allows the build to proceed **only** if existing generated `include/eventProto.capnp.h` and `src/eventProto.capnp.c++` are present, newer than `eventProto.capnp`, and already match the installed library `CAPNP_VERSION`. Otherwise it exits with installation instructions.

### Running & Monitoring

1. Place `config.json` in the **current working directory** (see Breaking Change #1: CLI config path arguments are no longer read by `./build`).
2. Run:
   ```bash
   ./build
   ```
3. In `"build"` mode, `dispacher.cpp` prints the requested file count and core count, binds an RPC server on the first open TCP port starting from `10000`, and renders a live ANSI terminal table refreshed at ~1 Hz alongside `builderStatus.txt` in the working directory:
   ```text
   ---------------------------------------- Builder status ----------------------------------------
     #id   |   #time   |             #file             |               #op               |   #status
   ------------------------------------------------------------------------------------------------
     1/4   |   00:12   | run_Eu152_000002.cap          | building events (3/4)           |  74.12%
   ```

---

## 4. Detailed Technical Breakdown by Subsystem

### 4.1 Build System & Cap'n Proto Version Independence

**Files:** `Makefile`, `capnp_sync.sh`, `.gitignore`, removed `include/eventProto.capnp.h`, removed `src/eventProto.capnp.c++`

1. **Untracking Generated Cap'n Proto Sources:**
   - Upstream tracked `include/eventProto.capnp.h` and `src/eventProto.capnp.c++` compiled with Cap'n Proto 0.8.0 (`#if CAPNP_VERSION != 8000`). Compiling on systems with Cap'n Proto 1.x (e.g., 1.3.0) failed with `#error "Version mismatch between generated code and library headers."`.
   - The fork removes both generated files from git tracking (`-5,208` / `-2,715` lines in `2170067`) and ignores them in `.gitignore` (`include/eventProto.capnp.h`, `src/eventProto.capnp.c++`, `src/.eventProto.capnp.stamp`, `eventProto.capnp.h`, `eventProto.capnp.c++`).
   - Because `eventProto.capnp` pins its schema file ID (`@0x86067e6a0fae5e5e;`), generated code from 0.8.x and 1.x maintains identical wire compatibility for reading existing `.cap` files.
2. **Version-Aware Synchronization (`capnp_sync.sh`):**
   - Computes numeric version as `MAJOR * 1000000 + MINOR * 1000 + MICRO`.
   - `generated_version()` parses the version guard in `include/eventProto.capnp.h` across both Cap'n Proto formats:
     - `0.8.x`: `#if CAPNP_VERSION != 8000`
     - `1.x`: `#ifndef CAPNP_VERSION ... #elif CAPNP_VERSION != 1003000`
   - `library_version()` preprocesses `#include <capnp/common.h>` using `$CXX $EXTRA_CPPFLAGS -E -dM -x c++ -` so any custom include flags are respected.
   - Regenerates via `$CAPNP compile -oc++ --src-prefix=. "$SCHEMA"` when `--force` is passed, files are missing, version parsing fails, `GEN != LIB`, or `eventProto.capnp` is newer than generated files. Verifies after compilation that `NEW == LIB`.
3. **`Makefile` Improvements & Bug Fixes:**
   - Resolves `CAPNP_CFLAGS` and `CAPNP_LIBS` via `pkg-config` with hard-coded fallback (`-lcapnp-rpc -lcapnp -lkj-async -lkj`).
   - Fixes a latent upstream bug where `OBJECTS` substituted `$(SOURCE_DIR)/%.cpp` on `OBJECTS_C++` (which held `.o` names), leaving `.c++` objects excluded when `OBJECTS_C++` differed; now explicitly builds `SOURCE_C++ := $(filter-out $(CAPNP_SOURCE),$(wildcard $(SOURCE_DIR)/*.c++)) $(CAPNP_SOURCE)` and maps `$(SOURCE_DIR)/%.c++` → `$(SOURCE_DIR)/%.o`.
   - Adds order-only prerequisite `$(OBJECTS): | $(CAPNP_STAMP)` and explicit dependency `$(CAPNP_HEADER)` on `.o` targets so `make` is safe on fresh clones and incremental builds.
   - Sets compiler optimization flags to `-std=c++17 -O3 -march=native` (up from `-std=c++17`) and appends `-g` to `LDLIBS`.
4. **`.gitignore` Cleanup:**
   - Adds `test.txt`, `builderStatus.txt`, generated Cap'n Proto files/stamp, and `.scratch/`.
   - Removes `viewWaveform.py` from `.gitignore` so it can be version-controlled.

---

### 4.2 Core Data Model & Memory Layout

**Files:** `include/utils.h`, `builder.cpp`

1. **Compacted `class event` (~80 bytes → 40 bytes, 50% reduction):**
   - Reordered member fields by alignment size:
     - `uint64_t timeStamp` (8 B)
     - `int32_t relativeTime` (4 B)
     - `float varPSD` (4 B, narrowed from `double`)
     - `vector<int16_t> *waveform = nullptr` (8 B pointer, replacing inline 24-byte `std::vector<int16_t>`)
     - `uint16_t energy` (2 B)
     - `uint8_t detectorType`, `detectorLut`, `boardNumb`, `chNumb`, `varCoincPair` (1 B each; first four narrowed from `uint16_t`)
     - `bool varAntiCoincFlag` (1 B)
     - `pair<uint8_t, uint8_t> varCoincPairType` (2 B)
   - Implemented full **Rule-of-Five** (`~event()`, copy constructor, noexcept move constructor, copy assignment, noexcept move assignment) to safely manage optional heap-allocated `waveform` pointers without per-event heap overhead when waveforms are absent (default `nullptr`).
   - Marked all getter methods `const` (`getTimeStamp() const`, `getDetectorType() const`, etc.) to support `const event &` zero-copy passing.
   - Updated `setWaveform(const vector<int16_t> &)` to only allocate when `!wave.empty()`, and `getWaveform() const` to return `{}` when `waveform == nullptr`.
2. **Lightweight `CoincRange` (`builder.cpp`):**
   - Replaced `pair<deque<event *>::iterator, deque<event *>::iterator>` (64 bytes of deque iterators) with `struct CoincRange { uint32_t start; uint32_t end; };` (8 bytes — an **87.5% memory reduction** for match ranges).

---

### 4.3 Data Ingestion & Streaming Loader

**Files:** `include/dataLoader.h`, `src/dataLoader.cpp`, `builder.cpp`

1. **`dataLoader` Base Interface (`include/dataLoader.h`):**
   - Added `virtual vector<event> readEventsFlat()` with a default adapter over `readEvents()` that copies `*ev` into a contiguous `vector<event>` and deletes the temporary heap `event *` objects (allowing all legacy loaders such as `rootLoader`, `compassLoader`, `delilaLoader`, etc. to work seamlessly with `convertRun()` and the non-streaming fallback in `buildRun()`).
   - Added virtual streaming hooks with safe defaults: `openStream()` (`false`), `readEventsBatch(vector<event> &, size_t)` (`0`), `closeStream()`, `isStreamEof()` (`true`), `getStreamProgress()` (`100.0`).
   - Added protected FD helpers `getFileSizeFromFd(int fd)` and `getCrtPosFromFd(int fd)` in `src/dataLoader.cpp`.
2. **`capLoader` Optimization (`include/dataLoader.h`, `src/dataLoader.cpp`):**
   - **L1-Cached Channel Table:** `capLoader` constructor precomputes `FastChannelInfo channelTable[32][64]` containing `delay`, `lut`, `type`, `coincPair`, `coincPairType`, and `isAntiCoinc`. This replaces ~82.7M per-event `std::map` lookups, `find()` scans over `antiCoincFlags`, and multi-level `vector<vector<...>>` pointer indirections with a single O(1) array access (`channelTable[brd][ch]`).
   - **Kernel Prefetching:** `capLoader::openStream()` opens the `.cap` file descriptor and calls `posix_fadvise(streamFd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_WILLNEED)` under `#if defined(__linux__) || defined(__WSL__)`.
   - **Batched Streaming Deserialization (`readEventsBatch`):** Reuses a persistent `kj::FdInputStream` + `kj::BufferedInputStreamWrapper` across calls, reading Cap'n Proto packed messages `{100000000, 64}` via `dest.emplace_back()` directly into the caller's contiguous `vector<event>` until either `targetCount` events are appended, EOF is reached, or `input.eventCount` (`eventNumbLimit`) is hit. Wraps deserialization in `try/catch (...)` setting `streamEof = true` on end/error.
   - **Flat & Legacy Loaders:** `capLoader::readEventsFlat()` opens the stream, pre-reserves `std::min<size_t>(streamFileSize / 11, eventNumbLimit)`, reads via `readEventsBatch`, and closes the stream. `capLoader::readEvents()` delegates to `readEventsFlat()`.
   - **`convertRun()` Cleanup (`builder.cpp`):** Switched from `loader->readEvents()` (`deque<event *>`) to `loader->readEventsFlat()` (`vector<event>`), avoiding pointer indirection and fixing upstream's leaked `event *` allocations in `convertRun()`.

---

### 4.4 Coincidence Engine & Double-Buffered Streaming

**File:** `builder.cpp`

`buildRun(json config)` now branches based on `loader->openStream()`:

#### Path A: Streaming Double-Buffered Engine (`openStream() == true`, used by `capLoader`)

1. **Pre-Allocated Ring & Staging Buffers:**
   - `eventRing`: reserved to `1,500,000` `event` objects (~60 MB at 40 B/event).
   - `nextBatch`: reserved to `BATCH_SIZE + 64` (`BATCH_SIZE = 500,000`).
   - `MARGIN_EVENTS = 200,000`.
2. **Asynchronous Double Buffering (`std::async`):**
   - Before entering the loop, launches an async task `readFuture` calling `loader->readEventsBatch(nextBatch, BATCH_SIZE)`.
   - On each iteration: awaits `readFuture.get()`, appends `nextBatch` to `eventRing`, clears `nextBatch`, and **immediately** launches `readFuture` for the subsequent 500k batch if `!isLast` — overlapping disk/Cap'n Proto deserialization with CPU sorting and coincidence matching.
3. **In-Place Parallel Sort:**
   - Sorts `eventRing` in place using `tbb::parallel_sort` by `getTimeStamp()` (zero auxiliary merge buffers, no TLS atomics in the comparator).
4. **Three-Tier Anti-Splitting Safeguard:**
   - **Tier 1 (Inter-Board Arrival Jitter Buffer):** When `!isLast`, holds back the trailing `MARGIN_EVENTS = 200,000` events (`evalLimit -= MARGIN_EVENTS`), representing ~10× the empirically measured maximum CAEN inter-board arrival displacement (20,560 events / ~137 ms). If `eventRing.size() <= MARGIN_EVENTS`, skips evaluation and reads another batch.
   - **Tier 2 (Coincidence Window Horizon Back-Off):** Walks `evalLimit` backward while `eventRing[evalLimit].getTimeStamp() - eventRing[evalLimit - 1].getTimeStamp() < coincWindow`, guaranteeing every evaluated anchor `i < evalLimit` sees its entire `[t_i, t_i + coincWindow)` window in `eventRing`.
   - **Tier 3 (Decoupled Cluster Finalization):** Scans `pendingMatches` into `overlapGroup` and refuses to finalize/write a cluster until either a quiet gap (`d2 >= d1`) closes the group (`clusterClosed == true`) or `isLast` is true.
   - *(Note: On `isLast`, `if (isLast && mIdx >= pendingMatches.size() - 1) break;` mirrors upstream's loop bound `i < eventPos.size() - 1` so output matches upstream bit-for-bit, including omitting the last unmatched/trailing boundary element in `eventPos`.)*
5. **Parallel Coincidence Search (`tbb::parallel_for`):**
   - Computes `numChunks = std::min<size_t>(10, std::max<size_t>(1, evalLimit / 10000))`.
   - Runs single-threaded when `numChunks <= 1`; otherwise partitions `[0, evalLimit)` into `numChunks` slices with thread-local `checkMask` and `localMatches`, then concatenates `chunkResults` in strict chunk order into `pendingMatches`.
6. **Zero-Allocation Overlap Selection & Post-Processing:**
   - `selectOverlapEvent()`: eliminates intermediate `vector<int> evtSize` and `map<int, double> stdValues` heap allocations, tracking `bestIdx` and `minStd` inline.
   - `getTimeStd()`: shifts timestamps relative to `t0 = eventPool[evt.start].getTimeStamp()` and accumulates in `double` instead of `uint128_t` / `float128_t` + `vector<double> diff(size)`.
   - `postProcessEvent()`: reuses `static vector<event> tempList` and `static vector<event *> ptrList` instead of allocating/deleting `new event(*(*i))` per built event.
   - `validateEvent()`: replaces `map<pair<int, int>, int> counts` with a direct O(n²) pairwise `(detectorLut, detectorType)` duplicate scan (typically $n \le 4$–$8$).
7. **Rolling Eviction & Throttled Progress:**
   - After writing closed clusters, erases `[eventRing.begin(), eventRing.begin() + lastEvictedEvtIdx)` when `!isLast`.
   - Reports progress via `rpcClient->updateState(...)` every 8 loop iterations (`(loopIter++ & 0x7) == 0`) using `loader->getStreamProgress()`.

#### Path B: Non-Streaming Fallback (`!openStream()`, e.g., `rootLoader`, `compassLoader`, etc.)

- Calls `loader->readEventsFlat()` + `shrink_to_fit()`.
- Calls `preProcessEvent(config, varEventPool)` (runs `tbb::parallel_sort` if `config["build"]["sortBeforeBuild"]` is `true`, reporting `"sorting data (2/4)"` via RPC).
- Partitions `varEventPool` across `numChunks = numThreads * 4` (from `general.maxCores`, falling back to `hardware_concurrency()`) via `tbb::parallel_for`.
- Resolves overlaps with `static vector<CoincRange> overlapEvents`, calls `postProcessEvent`, and closes the writer.

---

### 4.5 Output Writers

**Files:** `include/dataWriter.h`, `src/dataWriter.cpp`

1. **`gaspDataWriter::addBuiltEvent` — Zero-Map Slot Table & Inlined Serialization:**
   - Replaced per-event `map<pair<int, int>, vector<event *>> outputEvents` (red-black tree nodes + heap-allocated `vector<event *>` per active detector) with a reusable static 2D table:
     ```cpp
     struct BuiltDetectorSlot {
         int expectedSize = 0;
         event *signals[8] = {nullptr};
     };
     static BuiltDetectorSlot slotTable[16][32];
     static vector<pair<int, int>> activeSlots;
     ```
   - Looks up `const auto &mapping` by `const &` (eliminating vector copy), fills missing slots with `defaultEvents[id]`, and calls `std::sort(activeSlots.begin(), activeSlots.end())` to preserve exact `std::map` ascending `(oType, oLut)` key ordering.
   - Inlines GASP word packing into `addBuiltEvent` using hoisted `static vector<int> eventMask` and `static vector<short> eventBuffer` (pre-reserved to 128 shorts) and direct `typeGetters[iType][i]` lookup instead of `groupGetters[{iType, i}]` map searches.
2. **`gaspDataWriter::dumpToFile`:**
   - Updated signature to `const map<pair<int, int>, vector<event *>> &events` (zero-copy const reference), hoisted `static` `eventMask` and `eventBuffer`, and switched from `groupGetters[{iType, i}]` to `typeGetters[iType][i]`.
3. **`rootDataWriter` Fixes (`src/dataWriter.cpp`):**
   - Guarded reading `config["convert"]["root"]["addHistogram" | "addList" | "addRunTime"]` in `setConfig()` behind `if (config["general"]["run"].get<string>() == "convert")`, preventing missing-key JSON exceptions in `"build"` mode when `"convert"` is omitted or partial.
   - Removed a per-event debug `cout << time << " " << varAbsoluteTime << endl;` from `rootDataWriter::addBuiltEvent()`.

---

### 4.6 Live RPC Progress Tracking & Dispatcher

**Files:** `eventProto.capnp`, `dispacher.cpp`, `include/syncUtils.h`, `include/dataSyncClient.h`, `src/dataSyncClient.cpp`, `include/dataConfig.h`, `src/dataConfig.cpp`, `configSchema.json`, `builder.cpp`

1. **Cap'n Proto RPC Schema (`eventProto.capnp`):**
   - Added `interface StateSynchronizer` with `struct SyncState` (`procId :UInt16`, `maxId :UInt16`, `time :UInt64`, `filename :Text`, `op :Text`, `proc :Float64`) and `reportState @0 (state :SyncState) -> (acknowledged :Bool)`.
2. **Dispatcher (`dispacher.cpp`):**
   - `findAvailableSyncPort(config)` probes TCP ports starting from `10000` using `isPortAvailable()` (`SO_REUSEADDR` socket bind test in `include/syncUtils.h`) and assigns `config["syncPort"]` as a string.
   - `buildRun()` launches `startSyncTracker(config)`, which runs a `capnp::EzRpcServer` hosting `StateSynchronizerImpl` in an async thread polling `waitScope.poll()` while `syncFlag` is `true`, passing `crtRun = "build"`, `maxId`, `crtId` (1-based), `crtFile`, and `syncPort` to each spawned `./builder` worker.
   - `getPathFiles()` now calls `sort(files.begin(), files.end())` to return input files sorted alphabetically by filename.
   - Removed legacy `openConfigFile(argc, argv)` and `printStatus(finished, total)`. `main()` reads `"config.json"` directly from CWD.
3. **Dashboard Server (`include/syncUtils.h`):**
   - `StateSynchronizerImpl` prints an ANSI-updated console table (`#id | #time | #file | #op | #status`) every ~1 second (`1e9` ns) while concurrently writing the exact same formatted snapshot to `builderStatus.txt` (protected by `g_state_mutex`).
   - Handles terminal resize events dynamically via `ioctl(STDOUT_FILENO, TIOCGWINSZ, &w)`.
4. **Client & Thread-Safe Config Singleton (`dataConfig`, `dataSyncClient`, `builder.cpp`):**
   - `dataConfig` stores run config and `startTime` in a mutex-guarded singleton (`shared_ptr<dataConfig>`).
   - `dataSyncClient` wraps `capnp::EzRpcClient` targeting `localhost:<syncPort>`, throttling `reportState` RPC calls to at most once per `1e9` ns unless `force == true`.
   - Phase 4 wrapped `updateState()` in `try { ... } catch (...) {}` so standalone `./builder` executions do not hang or crash when no `build` RPC server is listening.
   - At the end of `main()` in `builder.cpp`, when `crtRun == "build"`, reports `{"done", 0}` (`force = true`) and sleeps `1s` (`this_thread::sleep_for(1s)`) so the dispatcher dashboard catches the final state before the worker exits.
   - *(Note: `dataSyncClient::startTracking()` and `stopTracking()` remain defined in `dataSyncClient.h`/`.cpp` from Phase 1 as unused legacy helpers for sorting-progress tracking.)*
5. **`configSchema.json`:**
   - Added `"syncPort": { "root": {"type": "string"} }` to top-level `properties` and reformatted comma spacing in `required`/`enum` arrays.

---

### 4.7 New Python Utilities & Documentation

**Files:** `config_editor.py`, `viewWaveform.py`, `README.md`

1. **`config_editor.py` (399 lines, new):**
   - Tkinter GUI (`JsonConfigEditor`) that creates one tabbed `ttk.Notebook` page per top-level key in `config.json`.
   - Automatically loads `./config.json` on startup if present (otherwise prompts `File -> Open...`), supports `Open...`, `Save`, `Save As...`, and serializes nested objects/lists cleanly (`tk.Text` for `gasp` lists, `ttk.Entry` otherwise).
   - Provides a custom **Detector** tab with a module selector (`Module 0..N`), **Add Module** / **Remove Selected Module** buttons, and 4×4 (16-channel) grids per detector property (`coincPairs`, `delay`, `lut`, `type`).
2. **`viewWaveform.py` (87 lines, newly tracked):**
   - Un-ignored from `.gitignore` and tracked in git. Reads multi-column waveform traces from `"garb"` (skipping 4 header lines) and displays them interactively via `matplotlib.pyplot`, with commented-out CFD trigger, `savgol_filter`, `uniform_filter1d`, and `TH1D` scaffolding.
3. **`README.md` (311 lines):**
   - Expanded from a minimal 2-line upstream file into full project documentation covering features, directory layout, dependencies, Cap'n Proto auto-sync workflow & troubleshooting, `config.json` overview, execution instructions, GUI tools, optimization phases 1–4, three-tier streaming safeguards, benchmarks, and MD5 checksums.

---

## 5. Breaking Changes, Behavioral Differences & Known Constraints

Review these carefully before merging upstream or deploying into new environments:

1. **`./build` ignores CLI config filename arguments:**
   - Upstream `openConfigFile(argc, argv)` accepted `./build my_config.json`. The fork removed `openConfigFile` and hardcodes `readConfigFile("config.json")` in `dispacher.cpp`. **Workaround:** run `./build` from the directory containing `config.json`, or restore CLI arg parsing if custom config paths are needed.
2. **Input file ordering changed from mtime to alphabetical:**
   - `dispacher.cpp::getPathFiles()` adds `sort(files.begin(), files.end())` after sorting by modification time, so files are always processed in lexicographical path order.
3. **Streaming mode ignores `build.sortBeforeBuild = false`:**
   - When `openStream()` succeeds (`capLoader`), `buildRun()` unconditionally runs `tbb::parallel_sort` on `eventRing`. Only the non-streaming fallback (`!openStream()`) respects `config["build"]["sortBeforeBuild"]`.
4. **Hard limits in `capLoader::channelTable[32][64]`:**
   - `capLoader` supports at most **32 boards** (`0..31`) and **64 channels per board** (`0..63`). Any out-of-bounds `(brd, ch)` silently falls back to `channelTable[0][0]`.
5. **`event` metadata field range reductions (`uint16_t` → `uint8_t`, `double` → `float`):**
   - `boardNumb`, `chNumb`, `detectorType`, and `detectorLut` now hold `0..255`, and `varPSD` is stored as 32-bit `float`. Configs or loaders with detector LUTs, types, boards, or channels `>= 256` will overflow `uint8_t`.
6. **Hard limits & non-reentrancy in `gaspDataWriter::addBuiltEvent` and `postProcessEvent`:**
   - `slotTable[16][32]` supports `oType` in `0..15`, `oLut` in `0..31`, and at most **8 signals per detector slot** (`event *signals[8]`); entries outside `0 <= oType < 16` or `0 <= oLut < 32` are silently skipped (and `slotIdx >= 8` would overflow `signals[8]`).
   - Function-local `static` buffers (`slotTable`, `activeSlots`, `eventMask`, `eventBuffer`, `tempList`, `ptrList`) make `gaspDataWriter::addBuiltEvent`, `dumpToFile`, and `postProcessEvent` **not thread-safe** if called concurrently within one process.
7. **200,000-event jitter buffer assumption (`MARGIN_EVENTS = 200000`):**
   - The streaming engine assumes inter-board packet arrival displacement never exceeds 200,000 events (roughly 10× the profiled 20,560-event worst case on CAEN multi-board `.cap` streams). Streams with larger out-of-order skew require increasing `MARGIN_EVENTS` (and `eventRing.reserve(...)`).
8. **Streaming parallel chunk cap of 10:**
   - The streaming coincidence search in `builder.cpp` caps `numChunks` at `std::min<size_t>(10, ...)` rather than reading `general.maxCores` (unlike the non-streaming fallback, which uses `maxCores * 4`).
9. **CPU portability with `-march=native`:**
   - `Makefile` compiles with `-O3 -march=native`, optimizing for the build host's ISA (e.g., AVX2). Binaries compiled on a newer CPU may trigger `SIGILL` on older cluster nodes unless rebuilt there or compiled with `FLAGS="-std=c++17 -O3"` (e.g. overriding `FLAGS` on the `make` command line).
10. **Standalone `./builder` invocation requirements:**
    - `./builder` expects a serialized JSON string as `argv[1]` containing runtime keys injected by the dispatcher (`crtRun`, `crtFile`, `crtId`, `maxId`, `syncPort` when `crtRun == "build"`). Missing `crtId`/`syncPort` in standalone `"build"` runs will throw inside `dataSyncClient` constructor or `updateState` before reaching `try/catch` (`dataSyncClient` constructor calls `cfg->config["syncPort"].get<string>()` outside `try/catch`). Additionally, `builder.cpp` sleeps `1s` before exiting when `crtRun == "build"`.
11. **`config_editor.py` CLI behavior & 16-channel grid:**
    - `config_editor.py` does **not** open `sys.argv[1]` (even though `README.md` gives `python3 config_editor.py config.json` as an example); it auto-loads `./config.json` from the working directory or opens files via `File -> Open...`. Its detector tab also hardcodes a 4×4 (16-channel) layout per digitizer module.
12. **`viewWaveform.py` hardcoded input & SciPy dependency:**
    - Reads a hardcoded file named `"garb"` from the working directory and requires `scipy` (`scipy.signal.savgol_filter`, `scipy.ndimage.uniform_filter1d`) in addition to `matplotlib` and `numpy`.
13. **Progress output in `convert` and `align` modes:**
    - Removing `printStatus()` from `dispacher.cpp` removed console progress counters from `convertRun()` and `alignRun()` (the RPC dashboard only runs in `buildRun()`).
14. **`configSchema.json` `syncPort` schema note:**
    - Top-level `"syncPort"` in `configSchema.json` is written as `{ "root": {"type": "string"} }` (a nested object rather than `{"type": "string"}`), and `dispacher.cpp` sets `config["syncPort"]` **after** `validateJsonSchema()` completes. Do not put `"syncPort": "10000"` directly inside `config.json`.

---

## 6. Complete File-by-File Change Matrix

Diff summary across all 22 touched files (`git diff --stat 67f4d37..2170067`):

| File | Status | Line Delta | Summary of Changes |
| :--- | :--- | :--- | :--- |
| `.gitignore` | Modified | `16 +-` | Ignore `test.txt`, `builderStatus.txt`, `.scratch/`, and generated `eventProto.capnp.*`/stamp; un-ignore `viewWaveform.py` |
| `Makefile` | Modified | `94 +-` | Add `capnp_sync.sh` auto-sync, `pkg-config` flags + fallback, `cap`/`capnp-check`/`capnp-sync` targets, `-O3 -march=native`, `-g`, fix `.c++` object mapping |
| `README.md` | Modified | `310 ++-` | Full user, build, Cap'n Proto troubleshooting, architecture, optimization, and benchmark documentation |
| `builder.cpp` | Modified | `478 +++--` | Streaming double-buffered `buildRun()`, `CoincRange`, `tbb::parallel_for` coincidence search, `tbb::parallel_sort`, hoisted post-processing, RPC reporting, `readEventsFlat()` in `convertRun()` |
| `capnp_sync.sh` | **Added** | `186 ++` | Preprocessor-based Cap'n Proto version detector and auto-regenerator (`--check` / `--force` / sync) |
| `configSchema.json` | Modified | `21 +-` | Add `syncPort` property; whitespace/alignment cleanup |
| `config_editor.py` | **Added** | `399 ++++` | Tkinter GUI editor for `config.json` with 4×4 per-module detector channel grids |
| `dispacher.cpp` | Modified | `151 +-` | Add `StateSynchronizerImpl` RPC server, dynamic port discovery (`10000+`), `sort()` in `getPathFiles()`, hardcode `./config.json`, remove `openConfigFile` & `printStatus` |
| `eventProto.capnp` | Modified | `18 +` | Add `interface StateSynchronizer` and `struct SyncState` |
| `include/dataConfig.h` | **Added** | `50 +` | Thread-safe config and start-timestamp singleton (`dataConfig`) |
| `include/dataLoader.h` | Modified | `51 +-` | Add `readEventsFlat()`, streaming virtual interface, FD helpers, and `capLoader::FastChannelInfo channelTable[32][64]` |
| `include/dataSyncClient.h` | **Added** | `63 +` | RPC progress client singleton declaration (`dataSyncClient`) |
| `include/dataWriter.h` | Modified | `2 +-` | Pass `events` map by `const &` to `gaspDataWriter::dumpToFile()` |
| `include/eventProto.capnp.h` | **Deleted** | `4619 -` | Untracked generated Cap'n Proto header (now generated per machine by `capnp_sync.sh`) |
| `include/syncUtils.h` | **Added** | `385 ++++` | `StateSynchronizerImpl` ANSI terminal dashboard, `builderStatus.txt` writer, and `isPortAvailable()` |
| `include/utils.h` | Modified | `185 +-` | Compact `class event` from ~80 B to 40 B with pointer-based `waveform`, Rule-of-Five, and `const` getters |
| `src/dataConfig.cpp` | **Added** | `4 +` | Static member definitions for `dataConfig` singleton |
| `src/dataLoader.cpp` | Modified | `287 +--` | `capLoader` constructor table precompute, `posix_fadvise`, `openStream`/`readEventsBatch`/`closeStream`/`getStreamProgress`, `readEventsFlat()`, FD size/pos helpers |
| `src/dataSyncClient.cpp` | **Added** | `105 +` | `dataSyncClient` 1 Hz throttled `EzRpcClient` with standalone `try/catch` guard |
| `src/dataWriter.cpp` | Modified | `152 +-` | `gaspDataWriter::addBuiltEvent` static `slotTable[16][32]` + inlined GASP serialization; `dumpToFile` buffer hoisting; `rootDataWriter` `convert` guard & log cleanup |
| `src/eventProto.capnp.c++` | **Deleted** | `2470 -` | Untracked generated Cap'n Proto source (now generated per machine by `capnp_sync.sh`) |
| `viewWaveform.py` | **Added** | `87 +` | Matplotlib/NumPy/SciPy pulse waveform inspection script |

---

## 7. Benchmarks & Output Verification

*(Figures recorded in `README.md` under WSL Ubuntu on a multi-core workstation with `maxCores: 10`.)*

### Single-Run Progression

| Milestone | Run 1 (40 MB, ~3.3M Events) | Run 2 (1.0 GB, ~82.8M Events) | Peak RAM (Run 2) | Speedup vs Baseline | Output MD5 Match |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Original Baseline** | 7.0s | ~210s (3m 30s) | ~8.5 GB | Baseline | Reference |
| **Phase 1: Flat-Memory Arch** | 6.3s | 133s (2m 13s) | 3.6 GB | 1.6× faster (*-58% RAM*) | **100% Bit-for-bit match** |
| **Phase 2: Multi-Core Parallel** | 4.55s | 93.7s (1m 33s) | ~3.6 GB | 2.2× faster | **100% Bit-for-bit match** |
| **Phase 3A: SIMD + Zero-Map Writer** | 2.47s | 28.87s | ~3.6 GB | > 7.2× faster | **100% Bit-for-bit match** |
| **Phase 3B: Sliding-Window Streaming** | ~1.0s | 17.75s | 49.9 MB (*-99.4% RAM*) | > 11.8× faster | **100% Bit-for-bit match** |
| **Phase 4: Double-Buffered & Multi-Run** | **0.8s** | **16.20s** | **49.9 MB** (*-99.4% RAM*) | **> 13.0× faster** | **100% Bit-for-bit match** |

### Multi-Run Concurrent Batch (All 4 Runs, ~2.9 GB, ~250M Events, 10 Cores)

| Metric | Original Baseline | Optimized Fork | Gain |
| :--- | :--- | :--- | :--- |
| **Total Wall-Clock Time** | ~700s (sequential) | **25.04s** | **> 27.9× faster turnaround** |
| **Total CPU Work (User Time)** | ~1,200s | **81.08s** | **> 14.8× CPU efficiency** |
| **Sustained CPU Utilization** | ~100% | **400%** | **4× parallel throughput** |
| **Peak Concurrent RAM** | ~34 GB (*OOM crash on < 32 GB*) | **~300 MB** | **99.1% memory reduction** |

### Reference MD5 Checksums & Verification Steps

Recorded reference MD5 checksums for `152Eu` validation outputs:

```text
5521848bc628bc1386ee9c0ce3523bad  output/run_Eu152_000001   (16,482,304 bytes)
c32a9cecf0443a2b1f5c573a2cb24395  output/run_Eu152_000002   (412,319,744 bytes)
500b80bbad755bcfa8fff71ef2d92c88  output/run_Eu152_000003   (410,796,032 bytes)
ef49b9bf68241ce007cec311b1476085  output/run_Eu152_000004   (410,664,960 bytes)
```

To verify your build against upstream `67f4d37`:

```bash
make capnp-check
make clean && make
./build
md5sum output/run_*
```

---

## 8. Git & Upstream Integration Reference

Currently `.git/config` defines only `origin` (`https://github.com/rlica/builder_sphere.git`). Useful commands for inspecting or integrating with upstream `gabriel-turturica/builder_sphere`:

```bash
# Add upstream remote (read/write access already verified for user rlica)
git remote add upstream https://github.com/gabriel-turturica/builder_sphere.git
git fetch upstream

# Inspect commits and diffs between upstream/master and fork master
git log --oneline upstream/master..master
git diff --stat upstream/master..master

# Verify Cap'n Proto toolchain status before building
make capnp-check

# Create a pull request from rlica:master against upstream master (if desired)
gh pr create --repo gabriel-turturica/builder_sphere --base master --head rlica:master
```
