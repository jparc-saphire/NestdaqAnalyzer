// Runs the conformance cases of the E88 parameter repository through the readers of experiments/e88.
//
// The parameter repository is not part of this repository; its location is given by the environment
// variable E88PARAM_DIR. The test is skipped (exit code 77) when the variable is not set.
//
// <E88PARAM_DIR>/conformance/cases.txt lists "file kind expect description" per line, where expect is
//   accept      read without error and without warning
//   warn        read without error, with a warning
//   unassigned  read only when unassigned rows are allowed; otherwise rejected
//   reject      rejected
// Channel maps and settings are read with conformance/detectors.txt as the detector definition.
// In addition, every <run_series>/index.txt listed in E88PARAM_INDEXES ("dir:run:mode"; default
// "2026nov:1:complete dummy:900000:clean") is loaded for the given run. Modes:
//   clean       loads by default, no unassigned row, no warning
//   complete    loads by default, no unassigned row; warnings (channels of the definition not cabled) allowed
//   unassigned  rejected by default, loads with unassigned rows allowed (cabling not fixed)
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "e88/Params.hh"
#include "e88/Setup.hh"

using namespace nda;

namespace {

int gFailures = 0;

bool Exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

struct Outcome {
  bool ok = false;
  std::size_t warnings = 0;
  std::string error;
};

std::string Describe(const Outcome& o) {
  if (!o.ok) return "rejected (" + o.error + ")";
  return o.warnings ? "read with " + std::to_string(o.warnings) + " warning(s)" : "read cleanly";
}

Outcome Read(const std::string& kind, const std::string& path, const e88::DetectorDef& def, bool allowUnassigned) {
  Outcome o;
  if (kind == "detector_def") {
    e88::DetectorDef d;
    o.ok = e88::ReadDetectorDef(path, d, o.error);
  } else if (kind == "channel_map") {
    e88::ChannelMap m;
    o.ok = e88::ReadChannelMap(path, def, m, o.error, allowUnassigned);
    o.warnings = m.warnings.size();
  } else if (kind == "settings") {
    e88::Settings s;
    o.ok = e88::ReadSettings(path, s, o.error);
    o.warnings = s.warnings.size();
  } else if (kind == "param_index") {
    // Run 0 lies outside the overlap of idx-overlap.txt: an overlap must be found whatever run is selected.
    e88::ParamFiles files;
    o.ok = e88::ReadParamIndex(path, 0, files, o.error);
    o.warnings = files.warnings.size();
    if (o.ok) {  // the files of the index must be readable as well
      e88::ParamOptions options;
      options.index = path;
      options.allowUnassigned = allowUnassigned;
      e88::E88Config config;
      e88::LoadReport report;
      o.ok = e88::LoadConfig(options, 0, config, report, o.error);
      o.warnings = report.warnings.size();
    }
  } else {
    o.error = "unknown kind " + kind + " in cases.txt";
  }
  return o;
}

void RunCase(const std::string& dir, const std::string& file, const std::string& kind, const std::string& expect,
             const e88::DetectorDef& def) {
  const std::string path = dir + "/conformance/" + file;
  const Outcome strict = Read(kind, path, def, false);
  const Outcome lenient = Read(kind, path, def, true);
  bool pass = false;
  if (expect == "accept") {
    pass = strict.ok && strict.warnings == 0 && lenient.ok && lenient.warnings == 0;
  } else if (expect == "warn") {
    pass = strict.ok && strict.warnings > 0 && lenient.ok && lenient.warnings > 0;
  } else if (expect == "unassigned") {
    pass = !strict.ok && lenient.ok;
  } else if (expect == "reject") {
    pass = !strict.ok && !lenient.ok;
  }
  if (!pass) ++gFailures;
  std::cout << (pass ? "PASS " : "FAIL ") << file << " " << kind << " expect " << expect
            << ": default " << Describe(strict) << "; unassigned allowed " << Describe(lenient) << "\n";
}

void LoadIndex(const std::string& dir, const std::string& spec) {
  std::string series, run, mode;
  std::stringstream ss(spec);
  std::getline(ss, series, ':');
  std::getline(ss, run, ':');
  std::getline(ss, mode, ':');
  const std::string index = dir + "/" + series + "/index.txt";
  if (!Exists(index)) {
    std::cout << "SKIP " << series << "/index.txt: not found\n";
    return;
  }
  e88::ParamOptions options;
  options.index = index;
  e88::E88Config config;
  e88::LoadReport report;
  std::string error;
  const auto runNumber = std::strtoull(run.c_str(), nullptr, 10);
  const bool strict = e88::LoadConfig(options, runNumber, config, report, error);
  bool pass;
  std::ostringstream detail;
  if (mode == "clean" || mode == "complete") {
    pass = strict && report.unassigned == 0 && (mode == "complete" || report.warnings.empty());
    detail << (strict ? "loaded" : "rejected (" + error + ")") << ", " << config.channels.channels.size()
           << " channels, " << report.warnings.size() << " warning(s)";
  } else {
    // The cabling is not fixed: unassigned rows are rejected by default and skipped when allowed.
    options.allowUnassigned = true;
    std::string error2;
    const bool lenient = e88::LoadConfig(options, runNumber, config, report, error2);
    pass = !strict && lenient && report.unassigned > 0;
    detail << "default " << (strict ? "loaded" : "rejected") << "; unassigned allowed "
           << (lenient ? "loaded" : "rejected (" + error2 + ")") << ", " << config.channels.channels.size()
           << " channels assigned, " << report.unassigned << " unassigned rows";
  }
  for (const auto& w : report.warnings) detail << "\n    warning: " << w;
  if (!pass) ++gFailures;
  std::cout << (pass ? "PASS " : "FAIL ") << series << "/index.txt run " << run << " (" << mode << "): " << detail.str()
            << "\n";
}

}  // namespace

int main() {
  const char* env = std::getenv("E88PARAM_DIR");
  if (!env || !*env) {
    std::cout << "E88PARAM_DIR is not set: conformance cases skipped\n";
    return 77;
  }
  const std::string dir = env;
  const std::string casesPath = dir + "/conformance/cases.txt";
  std::ifstream cases(casesPath);
  if (!cases) {
    std::cerr << "cannot open " << casesPath << "\n";
    return 1;
  }
  e88::DetectorDef def;
  std::string error;
  if (!e88::ReadDetectorDef(dir + "/conformance/detectors.txt", def, error)) {
    std::cerr << "conformance/detectors.txt: " << error << "\n";
    return 1;
  }
  int n = 0;
  for (std::string line; std::getline(cases, line);) {
    const auto hash = line.find('#');
    std::stringstream ss(line.substr(0, hash));
    std::string file, kind, expect;
    if (!(ss >> file)) continue;
    if (!(ss >> kind >> expect)) {
      std::cout << "FAIL bad line in cases.txt: " << line << "\n";
      ++gFailures;
      continue;
    }
    RunCase(dir, file, kind, expect, def);
    ++n;
  }
  if (n == 0) {
    std::cerr << "no case in " << casesPath << "\n";
    return 1;
  }
  const char* indexes = std::getenv("E88PARAM_INDEXES");
  std::stringstream specs(indexes ? indexes : "2026nov:1:complete dummy:900000:clean");
  for (std::string spec; specs >> spec;) LoadIndex(dir, spec);
  std::cout << n << " cases, " << gFailures << " failure(s)\n";
  return gFailures ? 1 : 0;
}
