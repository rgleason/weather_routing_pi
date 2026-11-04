// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClimatologyCompatibility.h"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>

#include <json/json.h>

namespace weather_routing {
namespace {

bool SupportedVersion(int major, int minor) {
  if (minor < 0 || minor >= 1000) return false;
  return (major == 0 && minor >= 10) || (major == 1 && minor <= 6) ||
         (major == 2 && minor == 0);
}

template <typename Function>
bool ReadPointer(const Json::Value& value, Function& function) {
  if (!value.isString()) return false;
  const std::string text = value.asString();
  if (text == "(nil)") return true;
  if (text.empty() || !std::isxdigit(static_cast<unsigned char>(text.front())))
    return false;
  // MSVC's %p has no 0x prefix; Unix providers normally include it.
  char* end = nullptr;
  errno = 0;
  const auto address = std::strtoull(text.c_str(), &end, 16);
  if (errno == ERANGE || end == text.c_str() || *end != '\0' ||
      address > std::numeric_limits<std::uintptr_t>::max()) return false;
  function = reinterpret_cast<Function>(static_cast<std::uintptr_t>(address));
  return true;
}

}  // namespace

ClimatologyMessage ReadClimatologyMessage(const std::string& message) {
  ClimatologyMessage result;
  Json::Reader reader;
  Json::Value value;
  if (!reader.parse(message, value) || !value.isObject() ||
      !value["ClimatologyVersionMajor"].isInt() ||
      !value["ClimatologyVersionMinor"].isInt()) return result;
  result.major = value["ClimatologyVersionMajor"].asInt();
  result.minor = value["ClimatologyVersionMinor"].asInt();
  result.valid = true;
  result.supported = SupportedVersion(result.major, result.minor);
  if (!result.supported) return result;

  ClimatologyMessage callbacks;
  if (!ReadPointer(value["ClimatologyDataPtr"], callbacks.data) ||
      !ReadPointer(value["ClimatologyWindAtlasDataPtr"], callbacks.wind_atlas) ||
      !ReadPointer(value["ClimatologyCycloneTrackCrossingsPtr"], callbacks.cyclones)) {
    result.valid = false;
    return result;
  }
  result.data = callbacks.data;
  result.wind_atlas = callbacks.wind_atlas;
  result.cyclones = callbacks.cyclones;
  if (value["ClimatologyDatasetVersion"].isString())
    result.dataset = value["ClimatologyDatasetVersion"].asString();
  return result;
}

}  // namespace weather_routing
