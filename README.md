# Nestdaq Analyzer

Online analysis tools for the NestDAQ data acquisition system.

> **Status: under development.** The generic part (data format, DAQ health, per-channel monitor, file replay)
> and the E88 monitor build and are tested with generated data. The FairMQ device also builds and receives synthetic time frames over an isolated IPC channel. Live DAQ inputs remain untested.

## Documentation

| For | Read |
|---|---|
| Writing or changing analysis code: add a histogram or a module, run and test it locally, share the change (beginner friendly) | [docs/developing.md](docs/developing.md) · [日本語](docs/developing.ja.md) |
| The two input paths (file and live data stream) | [docs/online-input.md](docs/online-input.md) |

## What it does

NestDAQ assembles the data of the front-end modules (FEMs, for example HR-TDC and LR-TDC boards) into
time frames. The online analyzer reads these time frames and fills histograms that a shift member can watch
in a browser (THttpServer / JSROOT), together with warnings:

| Page | Contents |
|---|---|
| Summary | status per check (DAQ, channels, detectors, trigger, analyzer), list of warnings, time frames seen by the analyzer |
| DAQ | time frame flags, words per module and type, heartbeat delimiter flags, missing or repeated heartbeat frames, data rate per module |
| Channels/&lt;module&gt; | hit pattern, rate, ToT and time within the heartbeat frame for every channel; hot channels and channels that went silent |
| E88/&lt;detector&gt; | (E88 only) one folder per plane, named as in the detector definition: SC1, SC2, MRPC, AC/SUM, AC/SUM_QTC, AC/AC1_CH, AC/AC2_CH, REFC/layer0, REFC/layer1, TRIG. Hit pattern (segment x side, down in the lower row), and for each side (`_up` and `_down` for two-ended planes, side 0 = up and 1 = down in the channel map) the hits before any coincidence, the ToT and the time relative to the nearest SC mean time, binned by segment and labelled with the channel names of the channel map; two-ended coincidences and t(up) - t(down) |
| E88/Trigger | (E88 only) T1 and B2TT record counts and rates, T1 - SC mean time, B2TT record time - T1, event id steps |
| E88/SC pair TF | (E88 only) time frames with an SC up/down pair (the selection of the SC coincidence filter) and their fraction per second |
| DAQ/B2TT | (E88 only) records of the B2TT receiver: record types, trigger type, event kind, flags, local sequence steps (lost records), spill id and resynchronisation count over time, trigger records per spill, interval between trigger records, frame of the record relative to the time frame, error records per second |

The same monitor modules run on any input: a file written by the FileSink (`nda-replay`), or a FairMQ channel
of the running DAQ (`nda-online-monitor`). See [docs/online-input.md](docs/online-input.md).

## Layout

```
NestdaqAnalyzer/
├── CMakeLists.txt
├── core/
│   ├── format/        time frame parsing, TDC word decoding, FileSink reader (no ROOT, no FairMQ)
│   ├── monitor/       module interface, histogram registry, engine, warnings (ROOT)
│   ├── modules/       generic modules: DaqHealthModule, ChannelModule
│   └── util/          SHA-256 of input files (no ROOT, no FairMQ)
├── experiments/
│   └── e88/           E88 monitor: parameter files, B2TT records, E88Module
├── apps/              nda-replay (reads files, no FairMQ)
├── tools/             analyzer-dev.py: run your build next to a running DAQ, compare two analyses
├── devices/           nda-online-monitor (FairMQ device, built only when FairMQ is found)
├── test/              unit tests
├── docs/              online-input.md
├── external/
│   └── nestdaq-user-impl/   data format headers (git submodule, pinned)
├── NestdaqTFSlicer/, analyzer/   original devices (see below)
└── README.md
```

Experiment-specific modules live in `experiments/<experiment>/`. They are only active when their parameters are
given; their parameter files (detector definition, channel map, settings) are kept outside this repository.

## Build

Requirements: CMake 3.16 or newer, a C++17 compiler, ROOT with the `http` feature
(tested with ROOT 6.38 and gcc 14). FairMQ is only needed for the device.

```bash
git submodule update --init
cmake -S . -B build
cmake --build build -j
ctest --test-dir build        # unit tests
```

The test `e88_conformance` runs the conformance cases of the E88 parameter repository (its
`conformance/cases.txt`) through the parameter reader, and loads the indexes `2026nov/index.txt` (must load
without unassigned rows; warnings for channels not cabled are allowed) and `dummy/index.txt` (must load without
warnings). The parameter repository is not part of
this repository: give its location with `E88PARAM_DIR`; without it the test is reported as skipped.

```bash
E88PARAM_DIR=<parameter repository> ctest --test-dir build --output-on-failure
```

Executables are written to `build/bin/`. CMake options:

| Option | Default | Meaning |
|---|---|---|
| `NDA_BUILD_DEVICES` | `AUTO` | build `nda-online-monitor`: `AUTO` (if FairMQ is found), `ON`, `OFF` |
| `NDA_BUILD_LEGACY` | `OFF` | build the original devices (needs FairMQ and NestdaqUnpacker) |
| `NESTDAQ_UNPACKER_SRC_DIR` | (none) | `NestdaqUnpacker/src`, only for `NDA_BUILD_LEGACY` |
| `NESTDAQ_USER_IMPL_DIR` | `external/nestdaq-user-impl` | nestdaq-user-impl sources (data format headers) |
| `NDA_BUILD_TESTS` | `ON` | unit tests |

## Usage

```bash
# Process a file and write the histograms
build/bin/nda-replay --input run000123.dat --output monitor-run000123.root

# Watch a file that is still being written, serve the histograms on a port
build/bin/nda-replay --input run000124.dat --follow --http http:<port> --expect-fem <module IPs>
```

`nda-replay --help` lists all options (sampling, refresh interval, JSON summary, ...).

The page refreshes the displayed histograms every `--update` seconds (Monitoring is on when the page opens). Four buttons
at the top:

| Button | Effect |
|---|---|
| **Reset** (all browsers) | clears all histograms and counters of the program; they fill again from the next time frame. Everybody who looks at the page sees it |
| **Reset** (this browser only) | the page remembers the contents of the histograms when the button is pressed and shows the histograms counted from then on. The program and other browsers are not affected |
| **Undo browser reset** | shows the histograms as they are again |
| **Restart** | only with `--allow-restart`: `nda-replay` stops with exit status 75 so that the program that started it can start it again. Everybody sees it |

`nda-online-monitor` has no Restart button. The reset of one browser works on histograms that accumulate counts (hit
patterns, ToT, coincidences, counters). Histograms that show a current value (rates, the status) and the histograms against
run time are marked in the page (item field `_nda_instant`) and never changed. A browser reset ends by itself when the
program is reset or started again (every start and every Reset gives the histograms a new identifier, the object
`Control/generation`), and the page says so. It is kept only in the open tab: reloading the page forgets it. The reset of one
browser is done by a small script that the program serves to the page (`nda/browser-reset.mjs`, JSROOT 7); the server stays
read-only and executes no other commands or object methods. A histogram that shows a current value instead of counts must be marked with `HistRegistry::MarkInstant` when it is booked. The page answers while `--follow` waits for new data.

### Developing next to a running DAQ

`nda-replay` only reads the file, so several people can analyze the file of a running DAQ at the same time, each with a
build of their own branch, without touching the DAQ or the analyzer that the shift members watch. `tools/analyzer-dev.py`
does this without a browser and compares two analyses of the same run:

```bash
# one run (a run that is still being written is followed to its end); the results go to a new directory
tools/analyzer-dev.py run --data-dir <data-dir> --run <N> --out <new-dir> [--param <param-dir>/index.txt]
# the newest run file, then every newer one, until Ctrl-C; also serve the histograms on a loopback port of your own
tools/analyzer-dev.py run --data-dir <data-dir> --follow-new --out <new-dir> --http 127.0.0.1:<port>
# the same run analyzed by two builds: exit status 0 when the results are the same
tools/analyzer-dev.py compare <dir-A>/run<NNNNNN>.root <dir-B>/run<NNNNNN>.root
```

- `run` uses `build/bin/nda-replay` of this clone (`--exe` takes another build) and writes `runNNNNNN.root`,
  `runNNNNNN.json` (the summary), `analyzer-runNNNNNN.log` per run and `report.json` with the commands and the build: the
  executable's SHA-256 and, from the CMake build directory, the source commit, the branch and whether the clone has
  uncommitted changes. More `nda-replay` options go after `--` (for example `-- --max-frames 1000`).
- The analyzer runs with a lower CPU priority (`--nice`, default 10). `--out` must be a new directory outside the data
  directory, and `--http` only takes a loopback address that is free.
- `compare` takes two `.root` files (every histogram bin by bin; needs PyROOT) or two summary `.json` files (every number;
  file paths and run times are ignored). `--tolerance` sets a relative tolerance.
- A change of the analyzer is checked by analyzing a reference run with the analyzer in use and with your build and
  comparing the two: the histograms you did not mean to change must be the same.

### Multi-word records

Some boards put records of several 64-bit words into their stream: a header word with its own 6-bit type and a
word count in bits 49:44, followed by payload words whose upper bits are arbitrary. Give their type with
`--record-type <type>` so that the payload is not read as TDC or delimiter words (the E88 monitor registers the
type of its B2TT records itself).

### E88 monitor

```bash
build/bin/nda-replay --input run000123.dat --param <param dir>/index.txt --http http:<port>
# while the cabling is not fixed (channel-map rows with '-' in fem_ip and ch)
build/bin/nda-replay --input run000123.dat --param <param dir>/index.txt --e88-allow-unassigned
# or give the files one by one
build/bin/nda-replay --input run000123.dat --detector-def detectors.txt --channel-map channel-map.txt --settings settings.txt
```

The parameter files are kept in a separate parameter repository, whose `FORMAT.md` (version 1) is the
specification; the reader in `experiments/e88/Params.cc` follows it and is checked against the repository's
conformance cases (see Build). Summary of the formats (whitespace separated text, `#` starts a comment,
`#!` lines are directives, a double-quoted string is one field):

| File | Header | Columns |
|---|---|---|
| detector definition | `#! format detector_def v1` | `detector module layer n_segment n_side edges [description]` |
| channel map | `#! format channel_map v1`, `#! valid_runs <first> <last>` | exactly `fem_ip ch detector module layer segment side name` |
| settings | `#! format settings v1` | `key value [value ...]` |
| parameter index | `#! format param_index v1`, `#! run_series <name>` | exactly `kind first_run last_run required file` |

Rules that make a file fail to load:

- every file: an integer column (run numbers, `module`, `layer`, `segment`, `side`, `n_segment`, `n_side`, `ch`
  and the parts of `fem_ip`) that is not decimal or has a leading zero (`010`, `0x3`); a directive given twice.
  Values in the settings may be hexadecimal (`0x2b`).
- detector definition: `detector` other than `SC`, `MRPC`, `AC`, `TRIG`, `REFC`; `module`, `layer` outside 0-255;
  `n_side` other than 1 or 2; `edges` other than `LT` or `L`; the same (detector, module, layer) twice.
- channel map: `fem_ip` not a dotted IPv4 address (no hexadecimal); `ch` outside 0-63; (detector, module,
  layer) not in the definition or segment/side outside it; duplicate (`fem_ip`, `ch`), logical channel or
  `name`. A row with `-` in both `fem_ip` and `ch` is an unassigned channel: it is skipped only with
  `--e88-allow-unassigned` (the number of skipped rows is reported and shown as a warning); a `-` in only one
  of the two columns is always an error.
- parameter index: run ranges outside 0-999999; `required` other than `yes` or `no`; overlapping run ranges
  of one kind (anywhere in the index, not only at the selected run); `detector_def` or `channel_map` rows that
  are not `required yes`; a run that is not covered by the rows of a kind that has a `required yes` row. Kinds other than
  `detector_def`, `channel_map` and `settings` (calibrations) are not used by the monitor.

Warnings (the files load, the warning is printed and shown on the Summary page): (segment, side) of the
definition missing from the channel map, unassigned rows, unknown setting keys, unknown index kinds.

Settings used by the monitor: `tdc_lsb_ns`, `b2tt_data_type`, `sc_pair_window`, `mrpc_pair_window`,
`t1_search`, `b2tt_search` and `window.<detector>` (range of the time relative to the SC mean time). A plane can have a range
of its own: `window.<folder>` with the folder below `E88/` written with `_` for `/` (for example `window.AC_SUM_QTC` for
`AC/SUM_QTC`) is used instead of `window.<detector>`. A hit outside the range is counted only in the overflow bins.
`t1_window`, `e88sw_mrpc` and `e88sw_dead` are known keys that the monitor does not use.
The channel named `T1` is the E88 trigger. Times are formed within one time frame, so coincidences that
straddle two time frames are lost (T1 and B2TT records are matched across time frames).

The files that were read are recorded with their SHA-256: printed at start-up (`E88 parameters file <kind>
<path> sha256 <hex>`), written to the `--summary-json` output (`e88_parameters`) and stored as the TNamed
`e88_parameters` in the histogram file (`--output`, or `--output-dir` of the device).

## Notes

- Include the nestdaq-user-impl headers only through `core/format/NestdaqHeaders.hh`:
  `TimeFrameHeader.h` sets `#pragma pack(2)` without restoring it, which also packs standard library
  classes declared afterwards and crashes at run time.
- The time unit of the raw TDC fields is assumed so that the field spans one heartbeat frame
  (high resolution: 2^-10 ns, low resolution: 1 ns). This is not yet confirmed with real data.

## Original devices

The first version of this repository provided two devices, kept until the new modules replace them:

* **NestdaqTFSlicer**: library for unpacking time frames and slicing data (`TFSlicerUnpacker`, `UnpackTdc`).
* **analyzer**: `OnlineAnalysisSlicer` (reads sliced time frames and creates histograms) and
  `OnlineAnalysisTFBuilder` (online event builder and analysis node).

They need FairMQ, ROOT, Boost and NestdaqUnpacker. Build them with
`cmake -S . -B build -DNDA_BUILD_LEGACY=ON -DNESTDAQ_UNPACKER_SRC_DIR=<path>/NestdaqUnpacker/src`.
Note that `UnpackTdc.h` decodes the heartbeat word with the older field layout
(flags in bits 57:48, frame number in bits 39:24), while the current firmware puts the frame number in bits 23:0
and the flags in bits 55:40 (`AmQStrTdcData.h`, namespace `v1`).
