#include "e88/Setup.hh"

#include <sstream>

#include "util/Sha256.hh"

namespace nda::e88 {

namespace {

bool Record(LoadReport& report, const std::string& kind, const std::string& path, std::string& error) {
  LoadReport::File f{kind, path, ""};
  if (!util::Sha256File(path, f.sha256, error)) return false;
  report.files.push_back(f);
  return true;
}

}  // namespace

std::string LoadReport::Text() const {
  std::ostringstream os;
  os << "run " << run << "\n";
  if (!runSeries.empty()) os << "run_series " << runSeries << "\n";
  for (const auto& f : files) os << "file " << f.kind << " " << f.path << " sha256 " << f.sha256 << "\n";
  os << "unassigned_rows " << unassigned << "\n";
  for (const auto& w : warnings) os << "warning " << w << "\n";
  return os.str();
}

bool LoadConfig(const ParamOptions& options, std::uint64_t run, E88Config& config, LoadReport& report,
                std::string& error) {
  report = LoadReport{};
  report.run = run;
  ParamFiles files;
  if (!options.index.empty()) {
    if (!ReadParamIndex(options.index, run, files, error)) return false;
    if (!Record(report, "param_index", options.index, error)) return false;
    report.runSeries = files.runSeries;
    report.warnings.insert(report.warnings.end(), files.warnings.begin(), files.warnings.end());
  }
  if (!options.detectorDef.empty()) files.detectorDef = options.detectorDef;
  if (!options.channelMap.empty()) files.channelMap = options.channelMap;
  if (!options.settings.empty()) files.settings = options.settings;
  if (files.detectorDef.empty() || files.channelMap.empty()) {
    error = "the E88 monitor needs a detector definition and a channel map (or a parameter index)";
    return false;
  }
  config = E88Config{};
  if (!ReadDetectorDef(files.detectorDef, config.detectors, error)) return false;
  if (!Record(report, "detector_def", files.detectorDef, error)) return false;
  if (!ReadChannelMap(files.channelMap, config.detectors, config.channels, error, options.allowUnassigned)) return false;
  if (!Record(report, "channel_map", files.channelMap, error)) return false;
  if (run < config.channels.firstRun || run > config.channels.lastRun) {
    error = files.channelMap + ": run " + std::to_string(run) + " is outside valid_runs";
    return false;
  }
  report.unassigned = config.channels.unassigned;
  for (const auto& w : config.channels.warnings) report.warnings.push_back("channel map: " + w);
  if (!files.settings.empty()) {
    if (!ReadSettings(files.settings, config.settings, error)) return false;
    if (!Record(report, "settings", files.settings, error)) return false;
    for (const auto& w : config.settings.warnings) report.warnings.push_back("settings: " + w);
  }
  config.ApplySettings();
  return true;
}

}  // namespace nda::e88
