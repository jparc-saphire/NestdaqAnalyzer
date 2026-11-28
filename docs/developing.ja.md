# online analyzer の開発の手引き（初心者向け）

English: [developing.md](developing.md)

この手引きは、「このコードを初めて見る」状態から、「自分で作ったヒストグラムが実際のデータファイルで動いて、
レビューに出せる」状態までを案内します。必要なのは、C++ の基本（クラス、`std::vector`、ラムダ式）、ROOT のヒストグラムが
何か、`git` の基本だけです。FairMQ や DAQ の内部は知らなくてかまいません。

目次

1. [全体の像](#1-全体の像)
2. [コードを取得して、build して、テストする](#2-コードを取得してbuild-してテストする)
3. [データファイルで analyzer を動かす](#3-データファイルで-analyzer-を動かす)
4. [コードのどこに何があるか](#4-コードのどこに何があるか)
5. [time frame が自分のコードに届くまで](#5-time-frame-が自分のコードに届くまで)
6. [チュートリアル 1: E88 の monitor にヒストグラムを足す](#6-チュートリアル-1-e88-の-monitor-にヒストグラムを足す)
7. [チュートリアル 2: 新しい module を足す](#7-チュートリアル-2-新しい-module-を足す)
8. [module が守る決まり](#8-module-が守る決まり)
9. [パラメタと設定](#9-パラメタと設定)
10. [共有する前の確認](#10-共有する前の確認)
11. [変更を共有する](#11-変更を共有する)
12. [困ったとき](#12-困ったとき)
13. [用語集](#13-用語集)

## 1. 全体の像

```
 ボード ──► NestDAQ ──► time frame ──┬─► FileSink ──► run000123.dat ──► nda-replay          ─┐
 (HR-TDC など) (TimeFrameBuilder)    │                                  （ファイルを読む）    │  同じ
                                     └─► nda-online-monitor （ライブのデータの流れを読む）    ─┤  module
                                                                                             │
                                     module がヒストグラムを埋める ◄──────────────────────────┘
                                                │
                                                ├─► ブラウザのページ（ROOT の THttpServer と JSROOT）
                                                └─► ROOT ファイル（--output）
```

- NestDAQ は、全部の front-end module（FEM。たとえば HR-TDC のボード）のデータを **time frame** に切ります。analyzer は
  time frame を読みます。
- **module** は、すべての time frame を受け取ってヒストグラムを埋める C++ のクラスです。analyzer は module のリストです。
- プログラムは 2 つあり、**同じ module** を動かします。
  - `nda-replay` は、記録されたファイル（または、まだ書かれている途中のファイル）を読みます。開発に使います。
  - `nda-online-monitor` は、ライブのデータの流れを読む FairMQ の device です。ファイルを書かない DAQ のセッションの
    本番の analyzer が使います。
- module が同じなので、`nda-replay` でファイルを使って作ったものは、ライブの analyzer でもそのまま動きます。

今ある module:

| module | 場所 | 見えるもの |
|---|---|---|
| `DaqHealthModule` | `core/modules/` | データレート、欠けた heartbeat frame、delimiter の flag、DAQ についての警告 |
| `ChannelModule` | `core/modules/` | 全 channel の hit pattern、レート、ToT、時間。hot な channel と沈黙した channel |
| `E88Module` | `experiments/e88/` | channel map を通した検出器の見え方（SC、MRPC、AC など）、コインシデンス、T1、B2TT の record |

## 2. コードを取得して、build して、テストする

必要なもの: CMake 3.16 以上、C++17 のコンパイラ、`http` を有効にした ROOT（ROOT 6.38 と 6.40 で確認済み）。
FairMQ は `nda-online-monitor` にだけ必要です。

```bash
git clone --recurse-submodules -b dev https://github.com/jparc-saphire/NestdaqAnalyzer.git
cd NestdaqAnalyzer
git switch -c my-feature              # 自分のブランチ。dev や main では直接作業しない
cmake -S . -B build
cmake --build build -j
ctest --test-dir build                # テストは 5 つ。すべて通ること
```

- `dev` は変更を集めるブランチ、`main` は本番の analyzer が使う版です。
- build は `-j` を付ければ 1 分もかかりません。実行ファイルは `build/bin/nda-replay` です。
- テストの 1 つ（`e88_conformance`）は E88 のパラメタのリポジトリを必要とし、無いときは skipped と表示されます。
  実行するには場所を教えます: `E88PARAM_DIR=<パラメタのリポジトリ> ctest --test-dir build`。
- `cmake` が ROOT を見つけられないときは、先に ROOT の環境を読み込みます（たとえば `source <ROOT>/bin/thisroot.sh`）。
- ライブの device は、FairMQ が見つかったときだけ build されます。build するには、NestDAQ のインストール先を CMake に教えます:
  `cmake -S . -B build-device -DCMAKE_PREFIX_PATH=<NestDAQ の prefix> -DNDA_BUILD_DEVICES=ON`、続けて
  `cmake --build build-device -j --target nda-online-monitor`。

## 3. データファイルで analyzer を動かす

DAQ が書いた run のファイル（`run000123.dat`）が要ります。数 GB の短い run で十分です。記録された run の置き場所は
E88 の DAQ 担当に聞いてください。E88 の monitor には、パラメタのファイル（検出器の定義、channel map、settings）も
要ります。これは E88 のパラメタのリポジトリにあり、`--param` にはその `index.txt` を渡します。

```bash
# 最初の 5000 個の time frame。ヒストグラムを ROOT ファイルに書く
build/bin/nda-replay --input <run のファイル> --max-frames 5000 --param <パラメタ>/2026nov/index.txt --output out.root

# ファイル全体。自分用のポートでページを出す（下の「ページで見る」を参照）
build/bin/nda-replay --input <run のファイル> --param <パラメタ>/2026nov/index.txt --http http:127.0.0.1:<port> --keep-alive
```

`--max-frames N` は、変更を試す一番速い方法です。`--sample N` は、N 個に 1 個の time frame だけを処理します。
`nda-replay --help` で全部の option が分かります。

**ROOT ファイルで見る。** ページのフォルダは、ファイルの中のディレクトリになります:

```python
import ROOT
f = ROOT.TFile.Open("out.root")
f.ls()                                   # フォルダ: DAQ、Channels、E88 など
h = f.Get("E88/SC1/hit_pattern")
print(h.GetEntries())
```

`root out.root` で開いて `new TBrowser` でも見られます。

**ページで見る。** `--http http:127.0.0.1:<port>` で、ヒストグラムを出します（THttpServer と JSROOT）。遠くの計算機で
動かしているときは、ポートを転送して、自分のブラウザでそのアドレスを開きます:

```bash
ssh -L <port>:127.0.0.1:<port> <動かしている計算機>     # そのあと http://127.0.0.1:<port>/ を開く
```

ほかの人が使っていないポートを選んでください。ページに出るのは、自分が起動したものだけです。本番の analyzer や DAQ には
影響しません。

`tools/analyzer-dev.py` は、これらの実行をまとめたものです（最新の run を追い、使ったコマンドと build を記録し、2 つの
解析を比べます）。[README](../README.md) の「Developing next to a running DAQ」を見てください。

## 4. コードのどこに何があるか

```
core/format/      time frame を読む: header、word、TDC のデコード、FileSink の読み込み（ROOT も FairMQ も不要）
core/monitor/     枠組み: Module（インターフェース）、HistRegistry（ヒストグラム）、Engine（module を動かす）
core/modules/     汎用の module（DaqHealthModule、ChannelModule）
experiments/e88/  E88 の monitor: パラメタ（Params）、B2TT の record、E88Module
apps/             nda-replay（ファイルのプログラム）
devices/          nda-online-monitor（FairMQ の device）
test/             単体テスト。test/Builder.hh は偽の time frame を作る
tools/            analyzer-dev.py
docs/             この手引きと online-input.md
```

まず、次の 3 つの小さなヘッダを読んでください。必要なインターフェースのすべてです。

- `core/monitor/Module.hh`: module が備えるべきもの
- `core/monitor/HistRegistry.hh`: ヒストグラムの作り方
- `core/format/Frames.hh` と `core/format/TdcWord.hh`: time frame と、その中の word

## 5. time frame が自分のコードに届くまで

`Module::Process(const TimeFrameView& tf, const FrameContext& ctx)` が、すべての time frame について呼ばれます。

```
TimeFrameView  tf
 └─ tf.header                          time frame の id、type の bit
 └─ tf.subFrames[]                     front-end module（FEM）ごとに 1 つ
      ├─ stf.header->femId             module（IPv4 アドレスを数にしたもの）
      ├─ stf.header->femType           ボードの種類。TDC の分解能が分かる
      └─ stf.frames[]                  この time frame にある、この module の heartbeat frame
           ├─ hbf.words、hbf.nWords    64 bit の word
           └─ 最後の word は delimiter（frame の番号を持つ）
```

64 bit の word の上位 6 bit が種類です（`format::HeadOf(word)`）。hit は `format::kLeading` と `format::kTrailing` の
word で、`format::DecodeTdc(word, kind)` が channel、TDC の値、ToT、立ち上がりかどうかを返します。次のループは、
`ChannelModule` が使っている型です。

```cpp
const auto kind = format::KindOfFemType(stf.header->femType);   // 高分解能か低分解能か
const double unit = format::TdcUnitNs(kind);                    // TDC 1 count あたりの ns
for (const auto& hbf : stf.frames) {
  ctx.words->ForEach(hbf, [&](std::uint64_t w, format::WordRole role, std::size_t) {
    if (role != format::WordRole::kWord) return;                // 複数 word の record の中身は飛ばす
    const std::uint8_t head = format::HeadOf(w);
    if (head != format::kLeading) return;
    const auto hit = format::DecodeTdc(w, kind);
    // hit.channel、hit.tdc * unit（heartbeat frame の中の ns）、hit.tot * unit（ns）
  });
}
```

- word をたどるには `ctx.words->ForEach(...)` を使います。複数 word の record（たとえば B2TT の record）に属する word
  を知っていて、その中身を TDC の word として読まないようにしてくれます。
- heartbeat frame 1 つは 65536 × 8 ns = 524.288 µs です。heartbeat frame の中の時間は、その frame の始まりからの時間です。
  別の frame や別の module の hit を 1 つの時間軸に載せる方法は、`E88Module`（`CollectFrame`）にあります。
- E88 の monitor は、module と channel の番号を直接は使いません。**channel map** で、(module, channel) を検出器、
  segment、side に対応づけます。`E88Module::CollectFrame` と第 9 節を見てください。

## 6. チュートリアル 1: E88 の monitor にヒストグラムを足す

目標: 各 time frame の SC の leading edge の数のヒストグラム `E88/Tutorial/sc_hits_per_tf`。触るのは 2 つのファイルで、
ヒストグラムの一生が分かります。

**手順 1: ポインタを宣言する。** `experiments/e88/E88Module.hh` の、ほかのメンバの隣に書きます（ポインタは持ち主ではありません。
すべてのヒストグラムは `HistRegistry` が持ちます）:

```cpp
  std::vector<std::uint64_t> fChannelHits;  // 割り当て済みの channel ごと
  TH1D* fScHitsPerTf = nullptr;             // time frame あたりの SC の leading edge
```

**手順 2: 作る。** `experiments/e88/E88Module.cc` の `E88Module::Book` に書きます。`Book1D(folder, name, title, nbins, lo, hi)` が
ヒストグラムを作ります。folder は、ページのツリーと ROOT ファイルの中の場所です（スラッシュでサブフォルダになります）。
title は `"title;x 軸;y 軸"` の形です:

```cpp
  fScHitsPerTf = r.Book1D("E88/Tutorial", "sc_hits_per_tf",
                          "SC leading edges per time frame;leading edges;time frames", 100, 0, 100);
```

**手順 3: 埋める。** `E88Module::Process` の、`CollectFrame(tf, ctx);` の直後に書きます。この呼び出しの後、
`fPlanes[pi].hits` に、今の time frame の全 plane の hit が入っています（ヘッダのコメントを見てください）:

```cpp
  double nSc = 0;
  for (std::size_t pi = 0; pi < fPlanes.size(); ++pi) {
    if (fConfig.detectors.planes[pi].detector != "SC") continue;
    for (const auto& v : fPlanes[pi].hits) nSc += static_cast<double>(v.size());
  }
  fScHitsPerTf->Fill(nSc);
```

**手順 4: build して動かす:**

```bash
cmake --build build -j
build/bin/nda-replay --input <run のファイル> --max-frames 5000 --param <パラメタ>/2026nov/index.txt --output out.root --quiet
python3 -c "import ROOT; h=ROOT.TFile.Open('out.root').Get('E88/Tutorial/sc_hits_per_tf'); print(h.GetEntries(), h.GetMean())"
```

エントリーは 5000 個（time frame ごとに 1 つ）になるはずです。平均は、time frame あたりの SC の leading edge の数です。

分かったこと: 宣言する、作る（1 回）、埋める（time frame ごと）。ページで Reset を押すとフレームワークがヒストグラムを消し、
最後に ROOT ファイルへ書きます。ヒストグラムが、積み上がる数ではなく**レートや状態**を表すときは、印を付けます:
`r.MarkInstant(hist)`（第 8 節）。

## 7. チュートリアル 2: 新しい module を足す

目標: heartbeat frame ごとの word の数をヒストグラムにする module `WordsPerHbfModule`。新しいファイルなので、
module の登録とテストのしかたも分かります。

**手順 1: ヘッダ** `core/modules/WordsPerHbfModule.hh`:

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

**手順 2: ソース** `core/modules/WordsPerHbfModule.cc`:

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

**手順 3: CMake に教える。** `CMakeLists.txt` の `nda_monitor` ライブラリに足します:

```cmake
add_library(nda_monitor STATIC
  ...
  core/modules/ChannelModule.cc
  core/modules/WordsPerHbfModule.cc
)
```

**手順 4: 2 つのプログラムの両方に module を登録する。** ここは忘れやすい手順です。engine に足していない module は、
呼ばれません。**`apps/nda-replay.cc`** の、ほかの `engine.Add(...)` の隣に、include と 1 行を足します:

```cpp
#include "modules/WordsPerHbfModule.hh"
...
  engine.Add(std::make_unique<nda::modules::WordsPerHbfModule>());
```

**`devices/nda-online-monitor.cxx`** の、`fEngine->Add(...)` の隣にも、同じ 2 つを足します:

```cpp
#include "modules/WordsPerHbfModule.hh"
...
      fEngine->Add(std::make_unique<nda::modules::WordsPerHbfModule>());
```

**手順 5: テストを書く** `test/test_words_per_hbf.cc`。`test/Builder.hh` が偽の time frame を作るので、データのファイルは
要りません:

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

そして、ほかのテストと同じように、`CMakeLists.txt` の `if(NDA_BUILD_TESTS)` の中に登録します:

```cmake
  add_executable(test_words_per_hbf test/test_words_per_hbf.cc)
  target_link_libraries(test_words_per_hbf PRIVATE nda_monitor)
  add_test(NAME words_per_hbf COMMAND test_words_per_hbf)
```

**手順 6: build、テスト、実行:**

```bash
cmake -S . -B build && cmake --build build -j     # ファイルの一覧が変わったので cmake をもう一度実行する
ctest --test-dir build                            # words_per_hbf が通ること
build/bin/nda-replay --input <run のファイル> --max-frames 5000 --output out.root --quiet
python3 -c "import ROOT; h=ROOT.TFile.Open('out.root').Get('Tutorial/words_per_hbf'); print(h.GetEntries(), h.GetMean())"
```

この module はパラメタのファイルを使わないので、`--param` は要りません。ヒストグラムは `Tutorial` フォルダに出ます。
device を build してあれば（第 2 節）、2 つのプログラムに登録したので、device にも同じ module が入っています。

チュートリアルが終わったら、それを commit しないでください。`git restore . && git clean -fd`（注意: commit していない変更が
消えます）にするか、本当の作業のために新しいブランチに移ります。

## 8. module が守る決まり

`Module`（`core/monitor/Module.hh`）には次の hook があります。必要なものだけ override します。

| hook | 呼ばれるとき | 使いみち |
|---|---|---|
| `Book(registry)` | 最初の run の前に 1 回 | ヒストグラムをすべて作る |
| `BeginRun(run)` | run の始まりと、**Reset** のたび | 自分のカウンタと状態を消す |
| `Process(tf, ctx)` | time frame ごと | ヒストグラムを埋める |
| `Update()` | 数秒ごとと、最後 | カウンタから作るヒストグラム（レート、割合）を埋める |
| `EndRun()` | run の終わり | 残った仕事を終える |
| `Report(out)` | `Update()` と一緒 | 警告を足す（`Check` の分類、重さ、文）。Summary のページに出る |
| `JsonSummary()` | 最後 | 主なカウンタの JSON を返す（スクリプトでの確認用） |

決まり:

1. **ヒストグラムはフレームワークが消すが、自分の変数は消さない。** Reset は `BeginRun` をもう一度呼ぶので、メンバのカウンタは
   そこで全部消します（`E88Module::BeginRun` を見てください）。`Update()` でカウンタから埋めるヒストグラムは、そこで
   作り直します。
2. **`Process` は一番忙しい経路。** time frame ごとに、数十 MB/s から約 100 MB/s のデータで呼ばれます。hit ごとにメモリを
   確保しない（`std::vector` を使い回して `clear()` する）、表示を出さない、tree を書かない。できるなら、普通の配列で数えて、
   `Update()` でヒストグラムに入れるほうがよいです。
3. **`Process` が戻った後は、time frame の中を指すポインタを持たない。** そのメモリは再利用されるか、解放されます。
   必要なものは写してください。
4. **time frame は飛ぶことがある。** `--sample N` のとき、またはライブの analyzer が追いつかないときは、time frame が落ちます。
   `ctx.contiguous` は、この frame の前に frame が飛んだときに false です。飛んだ分を「欠けたデータ」と数えない、
   飛んだ所をまたいで列を追わない。
5. **レートと状態は「instant」のヒストグラム。** 今の値を表すヒストグラム（レート、割合、run time に対するヒストグラム）は、
   作るときに `registry.MarkInstant(hist)` で印を付けます。付けないと、ページのブラウザごとの Reset が間違って差し引きます。
   run time に対するヒストグラムには、`hist->SetCanExtend(TH1::kXaxis)` も要ります。`E88Module::Book` の `fT1VsTime` を
   写してください。
6. **名前:** folder は `Experiment/Detector`、ヒストグラムの名前は `snake_case`、title には軸のラベルと単位
   （`"time [ns]"`）。ヒストグラムの名前は、同じ folder の中で一意にします。
7. **警告は、シフトの人のためのもの。** `Report` で警告を足すのは、人が何かしてほしいときだけにして、文には何をするか、
   何を確かめるかを書きます。既存の `Check` の分類（`kDaq`、`kChannels`、`kDetectors`、`kTrigger`、`kAnalyzer`）を使います。
8. **include:** NestDAQ のデータ形式のヘッダは、`core/format/NestdaqHeaders.hh` を通してだけ include します
   （`TimeFrameHeader.h` は `#pragma pack(2)` を元に戻さないので、その後に include した標準ライブラリのクラスの配置を、
   黙って壊します）。

## 9. パラメタと設定

E88 の monitor は、検出器のことを自分では何も知りません。**E88 のパラメタのリポジトリ**（offline の解析と共通）にある、
3 種類のテキストのファイルを読みます。検出器の定義（plane と segment）、channel map（module と channel から検出器、
segment、side、名前へ）、settings（SC の pair の窓などの数）です。形式はそのリポジトリの `FORMAT.md` が決めていて、
`experiments/e88/Params.cc` はそれに従っています。

- コードで**設定**を使うには: `config.settings.Number("my_key", index, default)`（`E88Config::ApplySettings` と
  `E88Module::TimeWindow` を見てください）。新しい key は、`Params.cc` の `Settings::KnownKey` にも足します（知らない key は
  警告になります）。offline の解析と食い違わないように、パラメタのリポジトリ（`FORMAT.md` とその確認のスクリプト）にも
  足します。key を足す前に、パラメタのリポジトリの担当者に聞いてください。
- コードに既定値を持たせます。key が無くても analyzer が起動するようにするためです。
- **結果に使ったパラメタのファイルは書き換えない。** 値の変更は、パラメタのリポジトリの担当者が作る新しいファイルか、
  新しい index の行で行います。自分で値を試すには、settings のファイルを自分のディレクトリに写して、写しを編集し、
  `--settings <自分のファイル>` で渡します（index より優先されます）。たとえば AC の時間のヒストグラムの範囲を広げるには、
  写しに `window.AC -300 500` と書きます。

## 10. 共有する前の確認

1. **単体テスト:** `ctest --test-dir build` が通る。足したものにテストを足す（チュートリアル 2 が例）。既存のテストの
   ファイルに check を足すだけでもかまいません。
2. **コンパイラの警告:** build は `-Wall -Wextra` を使います。警告を残さない。
3. **本番の解析と比べる。** 同じ基準の run を、本番の build と自分の build で解析して、変えるつもりのなかったものが
   すべて同じであることを確かめます:

   ```bash
   tools/analyzer-dev.py run --exe <本番の nda-replay> --data-dir <データのディレクトリ> --run <N> --param <パラメタ>/index.txt --out results/prod
   tools/analyzer-dev.py run --data-dir <データのディレクトリ> --run <N> --param <パラメタ>/index.txt --out results/mine
   tools/analyzer-dev.py compare results/prod/run<NNNNNN>.root results/mine/run<NNNNNN>.root
   ```

   足したヒストグラムは「only in mine」と出ます。それ以外はすべて `same` であること。`compare` の出力を pull request に
   貼ってください。
4. **ページで一度見る**（第 3 節）。軸、ラベル、範囲が妥当かを確かめます。
5. **速さ:** 基準のファイルを最後まで動かし、変更の前後の時間を控えます（`time build/bin/nda-replay ...`）。
   analyzer はデータレートに追いつかなければなりません。

## 11. 変更を共有する

- `dev` から作った自分のブランチで作業します。小さな単位で commit します。commit message は英語で、1 行目に何を変えたか、
  次の段落に理由を書きます。
- データのファイル、出力の ROOT ファイル、IP アドレスやホスト名、パスワード、人についてのメモは commit しません。
  このリポジトリは公開されています。
- ブランチを push して、**`dev` への pull request** を出します。何を足したか、どこで見えるか、`compare` と `ctest` の結果を
  書いてください。
- 本番の analyzer を動かす人とは別の人がレビューします。`dev` に merge された後、本番の担当が `dev` を `main` に入れて、
  セッションの合間に DAQ の新しい版にします。どの run も、1 つの決まった版で解析されるようにするためです。run が
  動いている間に、本番の analyzer を変えるように頼まないでください。
- 急ぎのときも流れは同じです。レビューは短くてよいですが、省きません。

## 12. 困ったとき

| 症状 | 確かめること |
|---|---|
| 自分のヒストグラムがページにもファイルにもない | module を、`apps/nda-replay.cc` と `devices/nda-online-monitor.cxx` の**両方**で `engine.Add` したか。ファイルを足した後に `cmake` をもう一度実行したか。探しているフォルダの名前は合っているか |
| `Book1D` で落ちる、または起動時に終了する | 同じ folder に同じ名前のヒストグラムが 2 つある。または `Book` が呼ばれていない（engine が最初の run の前に 1 回だけ module の `Book` を呼ぶ） |
| すべて空 | `--param` が違う（channel map にない channel は Summary のページに「unmapped hits」と出る）、`--sample` が大きすぎる、入力にその run の time frame がない |
| E88 のフォルダがない | `--param` なしで動かした。汎用の module だけが動く。E88 の monitor は、パラメタのファイルがあるときだけ動く |
| `cmake` が ROOT を見つけられない、または `http` が無いと言う | ROOT の環境を読み込む。ROOT は `http`（THttpServer）つきで build されている必要がある |
| 読み始めるとプログラムが落ちる | `NestdaqHeaders.hh` の外で `TimeFrameHeader.h` を include した（決まり 8）。または、8 byte に揃っていない time frame（parser は 8 byte に揃ったメモリを要求する） |
| 遅い | `Process` の中でメモリを確保している、または表示を出している。hit ごとに bin が非常に多い 2 次元ヒストグラムを埋めている。hit ごとに全 channel のループを回している |
| device が build できない | FairMQ が見つからない。`-DCMAKE_PREFIX_PATH=<NestDAQ の prefix>` を渡す（第 2 節） |
| ポートが使われている | 別のポートを選ぶ。DAQ や本番の analyzer のポートは絶対に使わない |

## 13. 用語集

| 用語 | 意味 |
|---|---|
| FEM | front-end module。DAQ のデータ源（HR-TDC のボード、B2TT の receiver など） |
| HBF | heartbeat frame。TDC のデータの基本の時間の単位で、524.288 µs |
| STF | sub time frame。1 つの time frame の中の、1 つの FEM のデータ |
| TF | time frame。同じ heartbeat frame についての全 FEM の STF を、TimeFrameBuilder がまとめたもの |
| TDC、ToT | 立ち上がりの時刻（高分解能の TDC では、いまの settings で 2^-10 ns を 1 count とする）、threshold を超えている時間 |
| leading / trailing | 信号の立ち上がり / 立ち下がり。hit の word は片方の edge を持つ |
| B2TT、T1 | Belle II の trigger timing の仕組みとその record。T1 は E88 の trigger 信号 |
| SC、MRPC、AC、REFC | E88 の検出器: シンチレータ（SC1 と SC2）、multi-gap RPC、aerogel Cherenkov、reference counter |
| QTC | AC の一部の信号が通る、電荷を時間に変える回路 |
| channel map | (FEM, channel) から検出器、segment、side、名前への表 |
| THttpServer、JSROOT | ヒストグラムを見せる ROOT の web server と、ブラウザの viewer |
