# Developing the online analyzer: a beginner's guide

日本語版: [developing.ja.md](developing.ja.md)

This guide takes you from "I have never seen this code" to "my new histogram runs on a real data file and is
ready for review". You need to know a little C++ (classes, `std::vector`, lambdas), what a ROOT histogram is, and
basic `git`. You do **not** need to know FairMQ or how the DAQ works inside.

Contents

1. [The big picture](#1-the-big-picture)
2. [Get the code, build it, run the tests](#2-get-the-code-build-it-run-the-tests)
3. [Run the analyzer on a data file](#3-run-the-analyzer-on-a-data-file)
4. [Where things are in the code](#4-where-things-are-in-the-code)
5. [How a time frame reaches your code](#5-how-a-time-frame-reaches-your-code)
6. [Tutorial 1: add a histogram to the E88 monitor](#6-tutorial-1-add-a-histogram-to-the-e88-monitor)
7. [Tutorial 2: add a new module](#7-tutorial-2-add-a-new-module)
8. [Rules every module follows](#8-rules-every-module-follows)
9. [Parameters and settings](#9-parameters-and-settings)
10. [Checking your change before you share it](#10-checking-your-change-before-you-share-it)
11. [Sharing your change](#11-sharing-your-change)
12. [Troubleshooting](#12-troubleshooting)
13. [Glossary](#13-glossary)

## 1. The big picture

```
 boards ──► NestDAQ ──► time frames ──┬─► FileSink ──► run000123.dat ──► nda-replay          ─┐
 (HR-TDC, ...)  (TimeFrameBuilder)    │                                  (reads a file)       │  the same
                                      └─► nda-online-monitor (reads the live data stream)    ─┤  modules
                                                                                              │
                                      modules fill histograms ◄───────────────────────────────┘
                                                │
                                                ├─► a browser page (ROOT THttpServer + JSROOT)
                                                └─► a ROOT file (--output)
```

- NestDAQ cuts the data of all front-end modules (FEMs, for example HR-TDC boards) into **time frames**. The analyzer
  reads time frames.
- A **module** is a C++ class that receives every time frame and fills histograms. The analyzer is a list of modules.
- There are two programs, and they run **the same modules**:
  - `nda-replay` reads a recorded file (or follows a file that is still being written). Use it for development.
  - `nda-online-monitor` is a FairMQ device that reads the live data stream. The production analyzer of a DAQ session
    that writes no file uses it.
- Because the modules are the same, anything you develop with `nda-replay` on a file works in the live analyzer too.

The modules that exist today:

| Module | Where | What it shows |
|---|---|---|
| `DaqHealthModule` | `core/modules/` | data rate, missing heartbeat frames, delimiter flags, warnings about the DAQ |
| `ChannelModule` | `core/modules/` | hit pattern, rate, ToT, time for every channel; hot and silent channels |
| `E88Module` | `experiments/e88/` | detector view through the channel map (SC, MRPC, AC, ...), coincidences, T1, B2TT records |

## 2. Get the code, build it, run the tests

Requirements: CMake 3.16 or newer, a C++17 compiler, ROOT with the `http` feature (tested with ROOT 6.38 and 6.40).
FairMQ is needed only for `nda-online-monitor`.

```bash
git clone --recurse-submodules -b dev https://github.com/jparc-saphire/NestdaqAnalyzer.git
cd NestdaqAnalyzer
git switch -c my-feature              # your own branch; never work on dev or main directly
cmake -S . -B build
cmake --build build -j
ctest --test-dir build                # 5 tests; all must pass
```

- `dev` is the branch where changes are collected; `main` is what the production analyzer runs.
- The build takes well under a minute with `-j`. The executable is `build/bin/nda-replay`.
- One test (`e88_conformance`) needs the E88 parameter repository and is reported as skipped without it. To run it, give
  its location: `E88PARAM_DIR=<parameter repository> ctest --test-dir build`.
- If `cmake` cannot find ROOT, load your ROOT environment first (for example `source <ROOT>/bin/thisroot.sh`).
- The live device is not built unless FairMQ is found. To build it, tell CMake where NestDAQ is installed:
  `cmake -S . -B build-device -DCMAKE_PREFIX_PATH=<NestDAQ prefix> -DNDA_BUILD_DEVICES=ON` and then
  `cmake --build build-device -j --target nda-online-monitor`.

## 3. Run the analyzer on a data file

You need a run file written by the DAQ (`run000123.dat`). A short run of a few GB is enough. Ask the E88 DAQ team where
the recorded runs are kept. The E88 monitor also needs the parameter files (detector definition, channel map,
settings), which live in the E88 parameter repository; `--param` takes its `index.txt`.

```bash
# the first 5000 time frames, histograms written to a ROOT file
build/bin/nda-replay --input <run file> --max-frames 5000 --param <parameters>/2026nov/index.txt --output out.root

# the whole file, with the page served on a port of your own (see "Looking at the page" below)
build/bin/nda-replay --input <run file> --param <parameters>/2026nov/index.txt --http http:127.0.0.1:<port> --keep-alive
```

`--max-frames N` is the fastest way to try a change. `--sample N` processes one time frame out of N. `nda-replay --help`
lists everything.

**Looking at the ROOT file.** Folders of the page are directories of the file:

```python
import ROOT
f = ROOT.TFile.Open("out.root")
f.ls()                                   # the folders: DAQ, Channels, E88, ...
h = f.Get("E88/SC1/hit_pattern")
print(h.GetEntries())
```

You can also open the file with `root out.root` and `new TBrowser`.

**Looking at the page.** `--http http:127.0.0.1:<port>` serves the histograms (THttpServer with JSROOT). If you run on a
remote computer, forward the port and open the address in your own browser:

```bash
ssh -L <port>:127.0.0.1:<port> <the computer you run on>     # then open http://127.0.0.1:<port>/
```

Pick a port nobody else uses. Your page shows only what you started; it does not change the production analyzer or the
DAQ.

`tools/analyzer-dev.py` wraps these runs (it follows the newest run, keeps the commands and the build used, and
compares two analyses); see "Developing next to a running DAQ" in the [README](../README.md).

## 4. Where things are in the code

```
core/format/      reads time frames: headers, words, TDC decoding, FileSink reader (no ROOT, no FairMQ)
core/monitor/     the framework: Module (interface), HistRegistry (histograms), Engine (runs the modules)
core/modules/     generic modules (DaqHealthModule, ChannelModule)
experiments/e88/  the E88 monitor: parameter files (Params), B2TT records, E88Module
apps/             nda-replay (the file program)
devices/          nda-online-monitor (the FairMQ device)
test/             unit tests; test/Builder.hh builds fake time frames
tools/            analyzer-dev.py
docs/             this guide and online-input.md
```

Read these three small headers first; they are the whole interface you need:

- `core/monitor/Module.hh`: what a module must provide.
- `core/monitor/HistRegistry.hh`: how to create histograms.
- `core/format/Frames.hh` and `core/format/TdcWord.hh`: the time frame and the words in it.

## 5. How a time frame reaches your code

`Module::Process(const TimeFrameView& tf, const FrameContext& ctx)` is called for every time frame.

```
TimeFrameView  tf
 └─ tf.header                          time frame id, type bits
 └─ tf.subFrames[]                     one per front-end module (FEM)
      ├─ stf.header->femId             the module (an IPv4 address as a number)
      ├─ stf.header->femType           board type; tells the TDC resolution
      └─ stf.frames[]                  heartbeat frames of this module in this time frame
           ├─ hbf.words, hbf.nWords    the 64-bit words
           └─ the last words are delimiters (they carry the frame number)
```

Each 64-bit word has a type in its upper 6 bits (`format::HeadOf(word)`). Hits are `format::kLeading` and
`format::kTrailing` words; `format::DecodeTdc(word, kind)` returns the channel, the TDC value, the ToT and whether it is a
leading edge. This loop is the pattern used by `ChannelModule`:

```cpp
const auto kind = format::KindOfFemType(stf.header->femType);   // high or low resolution
const double unit = format::TdcUnitNs(kind);                    // ns per TDC count
for (const auto& hbf : stf.frames) {
  ctx.words->ForEach(hbf, [&](std::uint64_t w, format::WordRole role, std::size_t) {
    if (role != format::WordRole::kWord) return;                // skip the payload of multi-word records
    const std::uint8_t head = format::HeadOf(w);
    if (head != format::kLeading) return;
    const auto hit = format::DecodeTdc(w, kind);
    // hit.channel, hit.tdc * unit (ns inside the heartbeat frame), hit.tot * unit (ns)
  });
}
```

- Use `ctx.words->ForEach(...)` to walk the words; it knows which words belong to multi-word records (for example the
  B2TT records), whose payload must not be read as TDC words.
- One heartbeat frame is 65536 × 8 ns = 524.288 µs. A time in a heartbeat frame is relative to the start of that frame;
  `E88Module` shows how to put hits of different frames and modules on one time axis (`CollectFrame`).
- The E88 monitor does not use module/channel numbers directly. It maps (module, channel) to a detector, segment and
  side with the **channel map**; see `E88Module::CollectFrame` and section 9.

## 6. Tutorial 1: add a histogram to the E88 monitor

Goal: a histogram `E88/Tutorial/sc_hits_per_tf` of the number of SC leading edges in each time frame. It touches two
files and teaches the whole life of a histogram.

**Step 1: declare a pointer** in `experiments/e88/E88Module.hh`, next to the other members (the pointer is not an
owner: the `HistRegistry` owns every histogram):

```cpp
  std::vector<std::uint64_t> fChannelHits;  // per mapped channel
  TH1D* fScHitsPerTf = nullptr;             // SC leading edges per time frame
```

**Step 2: book it** in `E88Module::Book` in `experiments/e88/E88Module.cc`. `Book1D(folder, name, title, nbins, lo, hi)`
creates the histogram; the folder is the place in the page tree and in the ROOT file (a slash makes sub-folders). The
title is `"title;x axis;y axis"`:

```cpp
  fScHitsPerTf = r.Book1D("E88/Tutorial", "sc_hits_per_tf",
                          "SC leading edges per time frame;leading edges;time frames", 100, 0, 100);
```

**Step 3: fill it** in `E88Module::Process`, right after `CollectFrame(tf, ctx);`. After that call, `fPlanes[pi].hits`
holds the hits of the current time frame of every plane (see the comment in the header):

```cpp
  double nSc = 0;
  for (std::size_t pi = 0; pi < fPlanes.size(); ++pi) {
    if (fConfig.detectors.planes[pi].detector != "SC") continue;
    for (const auto& v : fPlanes[pi].hits) nSc += static_cast<double>(v.size());
  }
  fScHitsPerTf->Fill(nSc);
```

**Step 4: build and run:**

```bash
cmake --build build -j
build/bin/nda-replay --input <run file> --max-frames 5000 --param <parameters>/2026nov/index.txt --output out.root --quiet
python3 -c "import ROOT; h=ROOT.TFile.Open('out.root').Get('E88/Tutorial/sc_hits_per_tf'); print(h.GetEntries(), h.GetMean())"
```

You should see 5000 entries (one per time frame). The mean is the average number of SC leading edges per time frame.

What you learned: declare, book (once), fill (every time frame). The framework clears the histogram when someone presses
Reset on the page, and writes it to the ROOT file at the end. If your histogram is a **rate or a status**, not a count
that accumulates, mark it: `r.MarkInstant(hist)` (section 8).

## 7. Tutorial 2: add a new module

Goal: a module `WordsPerHbfModule` that histograms the number of words in each heartbeat frame. It is a new file, so you
also learn how modules are registered and tested.

**Step 1: the header** `core/modules/WordsPerHbfModule.hh`:

```cpp
// Tutorial module: how many 64-bit words does one heartbeat frame of a front-end module hold?
#pragma once

#include <string>

#include "monitor/Module.hh"

class TH1D;

namespace nda::modules {

class WordsPerHbfModule : public monitor::Module {
 public:
  std::string Name() const override { return "WordsPerHbf"; }
  void Book(monitor::HistRegistry& registry) override;
  void Process(const format::TimeFrameView& tf, const monitor::FrameContext& ctx) override;

  const TH1D* Hist() const { return fWords; }  // for the unit test

 private:
  TH1D* fWords = nullptr;  // owned by the HistRegistry
};

}  // namespace nda::modules
```

**Step 2: the source** `core/modules/WordsPerHbfModule.cc`:

```cpp
#include "modules/WordsPerHbfModule.hh"

#include <TH1D.h>

#include "monitor/HistRegistry.hh"

namespace nda::modules {

void WordsPerHbfModule::Book(monitor::HistRegistry& registry) {
  fWords = registry.Book1D("Tutorial", "words_per_hbf", "Words per heartbeat frame;words;heartbeat frames", 200, 0, 400);
}

void WordsPerHbfModule::Process(const format::TimeFrameView& tf, const monitor::FrameContext& /*ctx*/) {
  for (const auto& stf : tf.subFrames) {
    for (const auto& hbf : stf.frames) fWords->Fill(static_cast<double>(hbf.nWords));
  }
}

}  // namespace nda::modules
```

**Step 3: tell CMake** about the new source. In `CMakeLists.txt`, add it to the `nda_monitor` library:

```cmake
add_library(nda_monitor STATIC
  ...
  core/modules/ChannelModule.cc
  core/modules/WordsPerHbfModule.cc
)
```

**Step 4: register the module in both programs.** This is the step people forget. A module that is not added to the
engine is never called. Add the include and one line to **`apps/nda-replay.cc`**, next to the other `engine.Add(...)`:

```cpp
#include "modules/WordsPerHbfModule.hh"
...
  engine.Add(std::make_unique<nda::modules::WordsPerHbfModule>());
```

and the same two lines to **`devices/nda-online-monitor.cxx`**, next to its `fEngine->Add(...)`:

```cpp
#include "modules/WordsPerHbfModule.hh"
...
      fEngine->Add(std::make_unique<nda::modules::WordsPerHbfModule>());
```

**Step 5: write a test** `test/test_words_per_hbf.cc`. `test/Builder.hh` builds fake time frames, so no data file is
needed:

```cpp
#include <TH1D.h>
#include <TROOT.h>

#include <memory>

#include "Builder.hh"
#include "modules/WordsPerHbfModule.hh"
#include "monitor/Engine.hh"
#include "monitor/HistRegistry.hh"

using namespace nda;

int main() {
  gROOT->SetBatch(true);
  monitor::HistRegistry registry;
  monitor::Engine engine(registry, monitor::Engine::Options{});
  auto module = std::make_unique<modules::WordsPerHbfModule>();
  const auto* words = module.get();
  engine.Add(std::move(module));
  engine.Book();
  engine.BeginRun(monitor::RunInfo{1});

  // One module with one heartbeat frame: 2 data words + 2 delimiter words = 4 words.
  ndatest::Stf stf;
  stf.femId = 0xc0000210;
  std::vector<std::uint64_t> data = {ndatest::Leading(5, 10, 1000), ndatest::Trailing(5, 2000)};
  const auto n = static_cast<std::uint32_t>(data.size());
  data.push_back(ndatest::Delimiter1(100));
  data.push_back(ndatest::Delimiter2(8 * n, 8 * n));
  stf.frames.push_back(data);
  const auto tf = ndatest::BuildTimeFrame(100, {stf});
  CHECK(engine.Process(reinterpret_cast<const std::uint8_t*>(tf.data()), ndatest::Bytes(tf)));

  CHECK_EQ(words->Hist()->GetEntries(), 1.0);
  CHECK_EQ(words->Hist()->GetBinContent(words->Hist()->GetXaxis()->FindBin(4)), 1.0);
  if (gFailures) return 1;
  std::cout << "test_words_per_hbf: all checks passed\n";
  return 0;
}
```

and register it in `CMakeLists.txt`, inside `if(NDA_BUILD_TESTS)`, like the other tests:

```cmake
  add_executable(test_words_per_hbf test/test_words_per_hbf.cc)
  target_link_libraries(test_words_per_hbf PRIVATE nda_monitor)
  add_test(NAME words_per_hbf COMMAND test_words_per_hbf)
```

**Step 6: build, test, run:**

```bash
cmake -S . -B build && cmake --build build -j     # re-run cmake: the file list changed
ctest --test-dir build                            # words_per_hbf must pass
build/bin/nda-replay --input <run file> --max-frames 5000 --output out.root --quiet
python3 -c "import ROOT; h=ROOT.TFile.Open('out.root').Get('Tutorial/words_per_hbf'); print(h.GetEntries(), h.GetMean())"
```

The module does not need the parameter files, so `--param` is not needed. The histogram appears in the `Tutorial` folder.
If you built the device (section 2), it contains the module too, because you registered it in both programs.

When you are done with a tutorial, do not commit it: `git restore . && git clean -fd` (careful: this deletes your
uncommitted changes) or just switch to a new branch for the real work.

## 8. Rules every module follows

`Module` (`core/monitor/Module.hh`) has these hooks; override what you need:

| Hook | Called | Use it to |
|---|---|---|
| `Book(registry)` | once, before the first run | create all histograms |
| `BeginRun(run)` | at the start of a run and at every **Reset** | clear your own counters and state |
| `Process(tf, ctx)` | for every time frame | fill histograms |
| `Update()` | every few seconds and at the end | fill histograms that are derived from counters (rates, fractions) |
| `EndRun()` | at the end of a run | finish pending work |
| `Report(out)` | with `Update()` | add warnings (`Check` category, severity, text) shown on the Summary page |
| `JsonSummary()` | at the end | return a JSON object with your main counters (used by scripted checks) |

Rules:

1. **Histograms are cleared by the framework, your own variables are not.** Reset calls `BeginRun` again, so reset every
   member counter there (see `E88Module::BeginRun`). A histogram you fill from a counter in `Update()` must be rebuilt
   there.
2. **`Process` is the hot path.** It runs for every time frame at tens of MB/s up to about 100 MB/s. Do not allocate memory
   per hit (reuse `std::vector`s and `clear()` them), do not print, and do not write trees. Prefer counting in a plain
   array and filling the histogram in `Update()` when you can.
3. **Never keep a pointer into the time frame after `Process` returns.** The memory is reused or freed. Copy what you need.
4. **Time frames can be skipped.** With `--sample N`, or when the live analyzer cannot keep up, time frames are dropped.
   `ctx.contiguous` is false when frames were skipped before this one. Do not count a gap as missing data, and do not
   follow a sequence across it.
5. **Rates and status are "instant" histograms.** A histogram that shows the current value (a rate, a fraction, a
   histogram against run time) must be marked with `registry.MarkInstant(hist)` when it is booked, otherwise the
   per-browser Reset of the page would subtract it wrongly. Histograms against run time also need
   `hist->SetCanExtend(TH1::kXaxis)`; copy `fT1VsTime` in `E88Module::Book`.
6. **Names:** folder `Experiment/Detector`, histogram name in `snake_case`, a title with axis labels and units
   (`"time [ns]"`). A histogram name must be unique within its folder.
7. **Warnings are for shift members.** Add one with `Report` only when a person should act on it, and make the text say
   what to do or check. Use the existing `Check` categories (`kDaq`, `kChannels`, `kDetectors`, `kTrigger`, `kAnalyzer`).
8. **Includes:** include the NestDAQ data format headers only through `core/format/NestdaqHeaders.hh`
   (`TimeFrameHeader.h` sets `#pragma pack(2)` without restoring it and silently breaks the layout of standard classes
   included after it).

## 9. Parameters and settings

The E88 monitor knows nothing about the detector by itself. It reads three kinds of text files from the **E88 parameter
repository** (shared with the offline analysis): the detector definition (planes and segments), the channel map
(module and channel to detector, segment, side and name) and the settings (numbers such as the SC pair window). The
format is specified in that repository's `FORMAT.md`, and `experiments/e88/Params.cc` follows it.

- To use a **setting** in code: `config.settings.Number("my_key", index, default)` (see `E88Config::ApplySettings` and
  `E88Module::TimeWindow`). A new key must also be added to `Settings::KnownKey` in `Params.cc` (unknown keys are
  reported as warnings) and to the parameter repository (`FORMAT.md`, its check script), so that the offline analysis
  stays consistent. Ask the parameter repository's maintainer before you add a key.
- Give a default in the code so that the analyzer still starts when the key is missing.
- **Do not edit the parameter files that were used for results.** A change of a value is a new file or a new
  index row made by the parameter repository's maintainer. To try a value yourself, copy the settings file to your own
  directory, edit the copy and pass it with `--settings <your file>` (it overrides the index). Example: to widen the range
  of the AC time histogram, put `window.AC -300 500` in your copy.

## 10. Checking your change before you share it

1. **Unit tests:** `ctest --test-dir build` passes. Add a test for what you added (Tutorial 2 shows how); a new check in an
   existing test file is fine.
2. **Compile warnings:** the build uses `-Wall -Wextra`; leave none behind.
3. **Compare with the production analysis.** Analyze the same reference run with the production build and with yours,
   and check that everything you did not mean to change is identical:

   ```bash
   tools/analyzer-dev.py run --exe <production nda-replay> --data-dir <data dir> --run <N> --param <parameters>/index.txt --out results/prod
   tools/analyzer-dev.py run --data-dir <data dir> --run <N> --param <parameters>/index.txt --out results/mine
   tools/analyzer-dev.py compare results/prod/run<NNNNNN>.root results/mine/run<NNNNNN>.root
   ```

   Histograms you added show up as "only in mine"; everything else must be `same`. Paste the `compare` output into the
   pull request.
4. **Look at it in the page** once (section 3) to check that the axes, labels and ranges make sense.
5. **Speed:** run the whole reference file and note the time before and after (`time build/bin/nda-replay ...`). The
   analyzer must keep up with the data rate.

## 11. Sharing your change

- Work on your own branch made from `dev`. Commit small steps. Write the commit message in English: a one-line summary
  (what changed), then a paragraph on why.
- Do not commit data files, output ROOT files, IP addresses or host names, passwords, or notes about people. The
  repository is public.
- Push your branch and open a **pull request to `dev`**. Say what you added, how to see it, and paste the `compare` and
  `ctest` results.
- Someone other than the person who runs the production analyzer reviews it. After the merge into `dev`, the production
  owner merges `dev` into `main` and puts the new version into the DAQ between sessions, so that every run is analyzed by
  one known version. Do not ask for a change of the production analyzer while a run is going.
- In a hurry? The flow is the same; the review can be short but is not skipped.

## 12. Troubleshooting

| Symptom | Check |
|---|---|
| My histogram is not in the page or the file | Is the module added with `engine.Add` in **both** `apps/nda-replay.cc` and `devices/nda-online-monitor.cxx`? Did you re-run `cmake` after adding a file? Is the folder name what you look for? |
| `Book1D` crashes or the program exits at start | Two histograms with the same name in the same folder; or `Book` was not called (the engine books the modules once, before the first run) |
| Everything is empty | Wrong `--param` (channels not in the channel map are counted as "unmapped hits" on the Summary page), or `--sample` too large, or the input has no time frames of that run |
| The E88 folders are missing | Run without `--param`: only the generic modules run. The E88 monitor is active only with the parameter files |
| `cmake` cannot find ROOT or says the `http` feature is missing | Load your ROOT environment; ROOT must be built with `http` (THttpServer) |
| The program crashes when it starts to read | An include of `TimeFrameHeader.h` outside `NestdaqHeaders.hh` (see rule 8); or a time frame that is not aligned: the parser needs 8-byte aligned memory |
| It is slow | An allocation or a print in `Process`; a 2D histogram with very many bins filled per hit; a loop over all channels per hit |
| The device does not build | FairMQ was not found. Pass `-DCMAKE_PREFIX_PATH=<NestDAQ prefix>` (section 2) |
| The port is in use | Pick another port. Never use the ports of the DAQ or of the production analyzer |

## 13. Glossary

| Term | Meaning |
|---|---|
| FEM | front-end module: a data source of the DAQ (an HR-TDC board, the B2TT receiver, ...) |
| HBF | heartbeat frame: the basic time slice of the TDC data, 524.288 µs |
| STF | sub time frame: the data of one FEM in one time frame |
| TF | time frame: the STFs of all FEMs for the same heartbeat frames, assembled by the TimeFrameBuilder |
| TDC, ToT | time of the leading edge (counts of 2^-10 ns for the high-resolution TDC, as in the current settings), time over threshold |
| leading / trailing | the rising / falling edge of a signal; a hit word carries one edge |
| B2TT, T1 | the Belle II trigger-timing system and its records; T1 is the E88 trigger signal |
| SC, MRPC, AC, REFC | detectors of E88: scintillator counters (SC1 and SC2), multi-gap resistive plate chambers, aerogel Cherenkov counters, reference counters |
| QTC | the charge-to-time converter that the signals of some AC counters pass |
| channel map | the table from (FEM, channel) to detector, segment, side and name |
| THttpServer, JSROOT | the ROOT web server and the browser viewer that show the histograms |
