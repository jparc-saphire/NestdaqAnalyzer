// Loads the E88 parameters given on the command line and builds the configuration of E88Module.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "e88/E88Module.hh"

namespace nda::e88 {

struct ParamOptions {
  std::string index;        // param_index v1; selects the files by run number
  std::string detectorDef;  // overrides the index
  std::string channelMap;   // overrides the index
  std::string settings;     // overrides the index (optional)
  bool allowUnassigned = false;  // skip channel-map rows with '-' in fem_ip and ch instead of failing
  bool Given() const { return !index.empty() || !detectorDef.empty() || !channelMap.empty() || !settings.empty(); }
};

// What was read: every file with its SHA-256, and the non-fatal findings. Keep it with the output of the run.
struct LoadReport {
  struct File {
    std::string kind;  // param_index, detector_def, channel_map, settings
    std::string path;
    std::string sha256;
  };
  std::string runSeries;  // from the index (empty without an index)
  std::uint64_t run = 0;
  std::vector<File> files;
  int unassigned = 0;  // channel-map rows skipped as unassigned
  std::vector<std::string> warnings;

  // One "key value" line per item: run, run_series, file <kind> <path> sha256 <hex>, unassigned_rows, warning.
  std::string Text() const;
};

// Returns false with `error` set when a file is missing or malformed.
bool LoadConfig(const ParamOptions& options, std::uint64_t run, E88Config& config, LoadReport& report,
                std::string& error);

}  // namespace nda::e88
