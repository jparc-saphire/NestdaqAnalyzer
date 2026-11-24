// Parameter files of the E88 analysis (detector definition, channel map, settings, parameter index).
// The files themselves are kept outside this repository and are given at run time.
// This file depends on neither ROOT nor FairMQ.
//
// Formats, version 1 (whitespace separated text, '#' starts a comment, "#!" lines are directives,
// a double-quoted string is one field). Every file starts with "#! format <kind> v1".
//   detector_def v1:  detector module layer n_segment n_side edges [description...]
//                     detector SC|MRPC|AC|TRIG, module and layer 0-255, n_side 1 or 2, edges LT|L
//   channel_map v1:   fem_ip ch detector module layer segment side name     ("#! valid_runs first last")
//                     exactly 8 columns, fem_ip dotted IPv4, ch 0-63; an unassigned row has '-' in both
//                     fem_ip and ch and is accepted only when the reader is asked to skip such rows
//   settings v1:      key value [value ...]                                  (unknown keys are warnings)
//   param_index v1:   kind first_run last_run required(yes|no) file          (file relative to the index)
//                     exactly 5 columns, "#! run_series <name>" required, run ranges of one kind must not
//                     overlap, a run not covered by a "required yes" row is an error
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace nda::e88 {

// One plane of a detector: (detector, module, layer) with its segments and sides.
struct Plane {
  std::string detector;
  int module = 0;
  int layer = 0;
  int nSegments = 0;
  int nSides = 0;
  std::string edges;
  std::string Label() const;  // "SC", or "SC_m1_l0" when module or layer is not 0
};

struct DetectorDef {
  std::vector<Plane> planes;
  int Find(const std::string& detector, int module, int layer) const;
};

struct MappedChannel {
  std::uint32_t femId = 0;
  int channel = 0;
  int plane = 0;  // index in DetectorDef::planes
  int segment = 0;
  int side = 0;
  std::string name;
};

struct ChannelMap {
  std::uint64_t firstRun = 0;
  std::uint64_t lastRun = 0;
  std::vector<MappedChannel> channels;
  std::unordered_map<std::uint64_t, int> byAddress;  // (femId << 8 | channel) -> index in `channels`
  std::vector<std::uint32_t> fems;                   // modules that appear in the map
  int unassigned = 0;                                // rows with '-' in fem_ip and ch (skipped)
  std::vector<std::string> warnings;                 // non-fatal findings of the checks (missing segments ...)

  static std::uint64_t Address(std::uint32_t femId, int channel) {
    return (std::uint64_t{femId} << 8) | static_cast<std::uint64_t>(channel & 0xFF);
  }
  const MappedChannel* Find(std::uint32_t femId, int channel) const {
    auto it = byAddress.find(Address(femId, channel));
    return it == byAddress.end() ? nullptr : &channels[it->second];
  }
};

class Settings {
 public:
  bool Has(const std::string& key) const { return fValues.count(key) != 0; }
  const std::vector<std::string>* Get(const std::string& key) const;
  double Number(const std::string& key, std::size_t index, double fallback) const;
  // Accepts decimal or 0x-prefixed hexadecimal values.
  long Integer(const std::string& key, std::size_t index, long fallback) const;
  void Set(const std::string& key, std::vector<std::string> values) { fValues[key] = std::move(values); }
  const std::map<std::string, std::vector<std::string>>& All() const { return fValues; }
  // Keys of the format (window.<detector> is a pattern); other keys are reported as warnings.
  static bool KnownKey(const std::string& key);

  std::vector<std::string> warnings;  // non-fatal findings (unknown keys)

 private:
  std::map<std::string, std::vector<std::string>> fValues;
};

struct ParamFiles {
  std::string runSeries;
  std::string detectorDef;
  std::string channelMap;
  std::string settings;
  std::vector<std::string> warnings;  // non-fatal findings (unknown kinds)
};

// All functions return false and set `error` (with file and line) on a fatal problem.
bool ReadDetectorDef(const std::string& path, DetectorDef& out, std::string& error);
// Rows with '-' in both fem_ip and ch are skipped and counted when `allowUnassigned` is true;
// otherwise they are an error. A '-' in only one of the two columns is always an error.
bool ReadChannelMap(const std::string& path, const DetectorDef& def, ChannelMap& out, std::string& error,
                    bool allowUnassigned = false);
bool ReadSettings(const std::string& path, Settings& out, std::string& error);
// Checks the whole index (all rows, every run range) and selects the files of `run`.
bool ReadParamIndex(const std::string& path, std::uint64_t run, ParamFiles& out, std::string& error);

}  // namespace nda::e88
