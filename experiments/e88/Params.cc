#include "e88/Params.hh"

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>


namespace nda::e88 {

namespace {

struct Line {
  int number = 0;
  std::vector<std::string> tokens;
  bool directive = false;  // "#!" line
};

// Reads non-empty lines split into tokens; comments are removed (a '#' inside a quoted string is kept).
bool ReadLines(const std::string& path, std::vector<Line>& lines, std::string& error) {
  std::ifstream in(path);
  if (!in) {
    error = "cannot open " + path;
    return false;
  }
  std::string text;
  int number = 0;
  while (std::getline(in, text)) {
    ++number;
    Line line;
    line.number = number;
    std::size_t start = 0;
    if (text.rfind("#!", 0) == 0) {
      line.directive = true;
      start = 2;
    }
    std::string token;
    bool quoted = false;
    for (std::size_t i = start; i < text.size(); ++i) {
      const char c = text[i];
      if (c == '"') {
        quoted = !quoted;
        continue;
      }
      if (!quoted && c == '#') break;
      if (!quoted && (c == ' ' || c == '\t' || c == '\r')) {
        if (!token.empty()) line.tokens.push_back(token);
        token.clear();
        continue;
      }
      token += c;
    }
    if (!token.empty()) line.tokens.push_back(token);
    if (!line.tokens.empty()) lines.push_back(std::move(line));
  }
  return true;
}

std::string Where(const std::string& path, const Line& line) { return path + ":" + std::to_string(line.number) + ": "; }

constexpr long kMaxRun = 999999;
constexpr long kMaxChannel = 63;  // high-resolution TDC: 64 channels per board

// Integer column of the parameter files: decimal digits only (no sign, no hexadecimal), no leading zero ("0" itself
// is allowed), within [lo, hi]. "010" and "0x3" are errors so that no reader takes them as octal or hexadecimal.
bool ToDecimal(const std::string& s, long lo, long hi, long& v) {
  if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos) return false;
  if (s.size() > 1 && s[0] == '0') return false;
  v = std::strtol(s.c_str(), nullptr, 10);
  return v >= lo && v <= hi;
}

// Decimal or 0x-prefixed hexadecimal (values of settings).
bool ToInt(const std::string& s, long& v) {
  char* end = nullptr;
  const int base = (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) ? 16 : 10;
  v = std::strtol(s.c_str(), &end, base);
  return end != s.c_str() && *end == '\0';
}

// Dotted IPv4 address: four decimal numbers 0-255 without leading zeros.
bool ParseDottedIPv4(const std::string& text, std::uint32_t& value) {
  value = 0;
  int parts = 0;
  std::size_t start = 0;
  while (true) {
    const auto dot = text.find('.', start);
    const std::string part = text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    long byte = 0;
    if (part.size() > 3 || !ToDecimal(part, 0, 255, byte)) return false;
    value = (value << 8) | static_cast<std::uint32_t>(byte);
    ++parts;
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  return parts == 4;
}

// Checks "#! format <format> v1" and collects the directives (each may appear once).
bool ReadDirectives(const std::string& path, const std::vector<Line>& lines, const std::string& format,
                    std::map<std::string, const Line*>& directives, std::string& error) {
  directives.clear();
  for (const auto& l : lines) {
    if (!l.directive) continue;
    if (!directives.emplace(l.tokens[0], &l).second) {
      error = Where(path, l) + "directive " + l.tokens[0] + " appears twice";
      return false;
    }
  }
  const auto it = directives.find("format");
  if (it == directives.end()) {
    error = path + ": missing '#! format " + format + " v1'";
    return false;
  }
  const auto& t = it->second->tokens;
  if (t.size() < 3 || t[1] != format || t[2] != "v1") {
    error = Where(path, *it->second) + "expected format " + format + " v1";
    return false;
  }
  return true;
}

}  // namespace

std::string Plane::Label() const {
  if (module == 0 && layer == 0) return detector;
  return detector + "_m" + std::to_string(module) + "_l" + std::to_string(layer);
}

int DetectorDef::Find(const std::string& detector, int module, int layer) const {
  for (std::size_t i = 0; i < planes.size(); ++i) {
    if (planes[i].detector == detector && planes[i].module == module && planes[i].layer == layer) return static_cast<int>(i);
  }
  return -1;
}

bool ReadDetectorDef(const std::string& path, DetectorDef& out, std::string& error) {
  out = DetectorDef{};
  std::vector<Line> lines;
  std::map<std::string, const Line*> directives;
  if (!ReadLines(path, lines, error) || !ReadDirectives(path, lines, "detector_def", directives, error)) return false;
  static const std::set<std::string> kDetectors = {"SC", "MRPC", "AC", "TRIG", "REFC"};
  for (const auto& l : lines) {
    if (l.directive) continue;
    if (l.tokens.size() < 6) {
      error = Where(path, l) + "expected: detector module layer n_segment n_side edges [description]";
      return false;
    }
    if (!kDetectors.count(l.tokens[0])) {
      error = Where(path, l) + "unknown detector " + l.tokens[0] + " (known: SC MRPC AC TRIG REFC)";
      return false;
    }
    Plane p;
    long module, layer, nseg, nside;
    if (!ToDecimal(l.tokens[1], 0, 255, module) || !ToDecimal(l.tokens[2], 0, 255, layer) ||
        !ToDecimal(l.tokens[3], 1, 65535, nseg) || !ToDecimal(l.tokens[4], 1, 2, nside)) {
      error = Where(path, l) + "bad number (module, layer 0-255; n_segment >= 1; n_side 1 or 2)";
      return false;
    }
    if (l.tokens[5] != "LT" && l.tokens[5] != "L") {
      error = Where(path, l) + "edges must be LT or L";
      return false;
    }
    p.detector = l.tokens[0];
    p.module = static_cast<int>(module);
    p.layer = static_cast<int>(layer);
    p.nSegments = static_cast<int>(nseg);
    p.nSides = static_cast<int>(nside);
    p.edges = l.tokens[5];
    if (out.Find(p.detector, p.module, p.layer) >= 0) {
      error = Where(path, l) + "duplicate plane " + p.Label();
      return false;
    }
    out.planes.push_back(p);
  }
  if (out.planes.empty()) {
    error = path + ": no detector";
    return false;
  }
  return true;
}

bool ReadChannelMap(const std::string& path, const DetectorDef& def, ChannelMap& out, std::string& error,
                    bool allowUnassigned) {
  out = ChannelMap{};
  std::vector<Line> lines;
  std::map<std::string, const Line*> directives;
  if (!ReadLines(path, lines, error) || !ReadDirectives(path, lines, "channel_map", directives, error)) return false;
  const auto runs = directives.find("valid_runs");
  if (runs == directives.end()) {
    error = path + ": missing '#! valid_runs first last'";
    return false;
  }
  {
    const auto& l = *runs->second;
    long a, b;
    if (l.tokens.size() < 3 || !ToDecimal(l.tokens[1], 0, kMaxRun, a) || !ToDecimal(l.tokens[2], 0, kMaxRun, b) ||
        a > b) {
      error = Where(path, l) + "bad valid_runs";
      return false;
    }
    out.firstRun = static_cast<std::uint64_t>(a);
    out.lastRun = static_cast<std::uint64_t>(b);
  }
  std::set<std::tuple<int, int, int>> keys;
  std::set<std::string> names;
  std::set<std::uint32_t> fems;
  for (const auto& l : lines) {
    if (l.directive) continue;
    if (l.tokens.size() != 8) {
      error = Where(path, l) + "expected 8 columns: fem_ip ch detector module layer segment side name";
      return false;
    }
    const std::string& fem = l.tokens[0];
    const std::string& ch = l.tokens[1];
    MappedChannel c;
    long module, layer, seg, side;
    if (!ToDecimal(l.tokens[3], 0, 255, module) || !ToDecimal(l.tokens[4], 0, 255, layer) ||
        !ToDecimal(l.tokens[5], 0, 65535, seg) || !ToDecimal(l.tokens[6], 0, 65535, side)) {
      error = Where(path, l) + "bad number";
      return false;
    }
    c.plane = def.Find(l.tokens[2], static_cast<int>(module), static_cast<int>(layer));
    if (c.plane < 0) {
      error = Where(path, l) + "detector " + l.tokens[2] + " module " + l.tokens[3] + " layer " + l.tokens[4] +
              " is not in the detector definition";
      return false;
    }
    const auto& p = def.planes[c.plane];
    if (seg >= p.nSegments || side >= p.nSides) {
      error = Where(path, l) + "segment or side out of range for " + p.Label();
      return false;
    }
    c.segment = static_cast<int>(seg);
    c.side = static_cast<int>(side);
    c.name = l.tokens[7];
    if (!keys.emplace(c.plane, c.segment, c.side).second) {
      error = Where(path, l) + "logical channel of " + c.name + " appears twice";
      return false;
    }
    if (!names.insert(c.name).second) {
      error = Where(path, l) + "name " + c.name + " appears twice";
      return false;
    }
    if (fem == "-" || ch == "-") {
      if (fem != "-" || ch != "-") {
        error = Where(path, l) + "an unassigned row has '-' in both fem_ip and ch";
        return false;
      }
      if (!allowUnassigned) {
        error = Where(path, l) + "unassigned row ('-'); such rows are skipped only when unassigned rows are allowed";
        return false;
      }
      ++out.unassigned;  // the logical channel is known but not cabled yet
      continue;
    }
    if (!ParseDottedIPv4(fem, c.femId)) {
      error = Where(path, l) + "fem_ip must be a dotted IPv4 address: " + fem;
      return false;
    }
    long channel;
    if (!ToDecimal(ch, 0, kMaxChannel, channel)) {
      error = Where(path, l) + "ch must be 0-" + std::to_string(kMaxChannel) + ": " + ch;
      return false;
    }
    c.channel = static_cast<int>(channel);
    const auto address = ChannelMap::Address(c.femId, c.channel);
    if (out.byAddress.count(address)) {
      error = Where(path, l) + "channel " + fem + " " + ch + " appears twice";
      return false;
    }
    out.byAddress.emplace(address, static_cast<int>(out.channels.size()));
    out.channels.push_back(c);
    if (fems.insert(c.femId).second) out.fems.push_back(c.femId);
  }
  for (std::size_t pi = 0; pi < def.planes.size(); ++pi) {
    const auto& p = def.planes[pi];
    int missing = 0;
    for (int s = 0; s < p.nSegments; ++s) {
      for (int side = 0; side < p.nSides; ++side) missing += keys.count({static_cast<int>(pi), s, side}) ? 0 : 1;
    }
    if (missing > 0) out.warnings.push_back(p.Label() + ": " + std::to_string(missing) + " (segment, side) not in the channel map");
  }
  if (out.unassigned > 0) {
    out.warnings.push_back(std::to_string(out.unassigned) + " unassigned rows ('-') skipped, " +
                           std::to_string(out.channels.size()) + " channels assigned");
  }
  return true;
}

const std::vector<std::string>* Settings::Get(const std::string& key) const {
  auto it = fValues.find(key);
  return it == fValues.end() ? nullptr : &it->second;
}

double Settings::Number(const std::string& key, std::size_t index, double fallback) const {
  const auto* v = Get(key);
  if (!v || index >= v->size()) return fallback;
  char* end = nullptr;
  const double d = std::strtod((*v)[index].c_str(), &end);
  return (end != (*v)[index].c_str() && *end == '\0') ? d : fallback;
}

long Settings::Integer(const std::string& key, std::size_t index, long fallback) const {
  const auto* v = Get(key);
  long value = 0;
  if (!v || index >= v->size() || !ToInt((*v)[index], value)) return fallback;
  return value;
}

bool Settings::KnownKey(const std::string& key) {
  static const std::set<std::string> kKeys = {"tdc_lsb_ns",  "b2tt_data_type", "sc_pair_window", "mrpc_pair_window",
                                              "t1_search",   "t1_window",      "b2tt_search",    "e88sw_mrpc",
                                              "e88sw_dead"};
  if (kKeys.count(key)) return true;
  return key.size() > 7 && key.rfind("window.", 0) == 0;
}

bool ReadSettings(const std::string& path, Settings& out, std::string& error) {
  out = Settings{};
  std::vector<Line> lines;
  std::map<std::string, const Line*> directives;
  if (!ReadLines(path, lines, error) || !ReadDirectives(path, lines, "settings", directives, error)) return false;
  for (const auto& l : lines) {
    if (l.directive) continue;
    if (out.Has(l.tokens[0])) {
      error = Where(path, l) + "key " + l.tokens[0] + " appears twice";
      return false;
    }
    if (l.tokens.size() < 2) {
      error = Where(path, l) + "key " + l.tokens[0] + " has no value";
      return false;
    }
    if (!Settings::KnownKey(l.tokens[0])) {
      out.warnings.push_back(Where(path, l) + "unknown key " + l.tokens[0] + " (ignored)");
    }
    out.Set(l.tokens[0], std::vector<std::string>(l.tokens.begin() + 1, l.tokens.end()));
  }
  return true;
}

bool ReadParamIndex(const std::string& path, std::uint64_t run, ParamFiles& out, std::string& error) {
  out = ParamFiles{};
  std::vector<Line> lines;
  std::map<std::string, const Line*> directives;
  if (!ReadLines(path, lines, error) || !ReadDirectives(path, lines, "param_index", directives, error)) return false;
  const auto series = directives.find("run_series");
  if (series == directives.end() || series->second->tokens.size() < 2) {
    error = path + ": missing '#! run_series <name>'";
    return false;
  }
  out.runSeries = series->second->tokens[1];
  const auto slash = path.rfind('/');
  const std::string dir = slash == std::string::npos ? "" : path.substr(0, slash + 1);

  static const std::set<std::string> kKinds = {"detector_def", "channel_map", "settings", "t0",
                                               "slewing",      "tot",         "position", "meantime",
                                               "fem_offset"};
  struct Row {
    std::string kind;
    long first, last;
    bool required;
    std::string file;
    int line;
  };
  std::vector<Row> rows;
  for (const auto& l : lines) {
    if (l.directive) continue;
    if (l.tokens.size() != 5) {
      error = Where(path, l) + "expected 5 columns: kind first last required file";
      return false;
    }
    Row r{l.tokens[0], 0, 0, false, l.tokens[4], l.number};
    if (!ToDecimal(l.tokens[1], 0, kMaxRun, r.first) || !ToDecimal(l.tokens[2], 0, kMaxRun, r.last) ||
        r.first > r.last) {
      error = Where(path, l) + "bad run range (0-" + std::to_string(kMaxRun) + ", first <= last)";
      return false;
    }
    if (l.tokens[3] != "yes" && l.tokens[3] != "no") {
      error = Where(path, l) + "required must be yes or no";
      return false;
    }
    r.required = l.tokens[3] == "yes";
    if ((r.kind == "detector_def" || r.kind == "channel_map") && !r.required) {
      error = Where(path, l) + r.kind + " must be required (yes)";
      return false;
    }
    if (!kKinds.count(r.kind)) out.warnings.push_back(Where(path, l) + "unknown kind " + r.kind + " (ignored)");
    for (const auto& o : rows) {
      if (o.kind == r.kind && r.first <= o.last && o.first <= r.last) {
        error = Where(path, l) + "run range overlaps line " + std::to_string(o.line) + " of kind " + r.kind;
        return false;
      }
    }
    if (r.file.rfind('/', 0) != 0) r.file = dir + r.file;
    rows.push_back(r);
  }
  for (const char* kind : {"detector_def", "channel_map"}) {
    bool found = false;
    for (const auto& r : rows) found |= r.kind == kind;
    if (!found) {
      error = path + ": no " + kind + " row";
      return false;
    }
  }
  std::set<std::string> chosen, requiredKinds;
  for (const auto& r : rows) {
    if (r.required) requiredKinds.insert(r.kind);
    if (run < static_cast<std::uint64_t>(r.first) || run > static_cast<std::uint64_t>(r.last)) continue;
    chosen.insert(r.kind);
    if (r.kind == "detector_def") out.detectorDef = r.file;
    if (r.kind == "channel_map") out.channelMap = r.file;
    if (r.kind == "settings") out.settings = r.file;
    // other kinds (calibrations) are not used online
  }
  for (const auto& kind : requiredKinds) {
    if (!chosen.count(kind)) {
      error = path + ": no " + kind + " file for run " + std::to_string(run) + " (required)";
      return false;
    }
  }
  return true;
}

}  // namespace nda::e88
