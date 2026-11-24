// SHA-256 of a file, used to record which input files (for example parameter files) a run used.
// Self-contained (FIPS 180-4); depends on neither ROOT nor FairMQ.
#pragma once

#include <string>

namespace nda::util {

// Lower-case hexadecimal SHA-256 of `data`.
std::string Sha256Hex(const std::string& data);

// Lower-case hexadecimal SHA-256 of the file at `path`; returns false (and sets `error`) if it cannot be read.
bool Sha256File(const std::string& path, std::string& hex, std::string& error);

}  // namespace nda::util
