/***************************************************************************
 *   Copyright (C) 2026 by the OpenCPN Weather Routing contributors        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "TimeZoneDisplay.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace marine_time {
namespace {

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L
#define MARINE_TIME_HAS_CHRONO_TZDB 1
#else
#define MARINE_TIME_HAS_CHRONO_TZDB 0
#endif

wxDateTime DateTimeFromSeconds(std::chrono::seconds value) {
  return wxDateTime(static_cast<time_t>(value.count()));
}

wxString CanonicalAbbreviation(const wxString& zoneName,
                               std::chrono::seconds utcOffset,
                               const wxString& databaseAbbreviation) {
  // MSVC obtains IANA transitions from Windows ICU, whose localized short
  // display name can be an offset such as "GMT+1" rather than the canonical
  // tzdata abbreviation. Keep the database authoritative for transitions and
  // offsets, but make the familiar British Isles labels deterministic.
  static const std::set<wxString> britishZones = {
      "Europe/London", "Europe/Guernsey", "Europe/Isle_of_Man",
      "Europe/Jersey", "GB",              "GB-Eire"};
  if (britishZones.count(zoneName) != 0) {
    if (utcOffset == std::chrono::hours{0}) return "GMT";
    if (utcOffset == std::chrono::hours{1}) return "BST";
  }
  if (zoneName == "Europe/Dublin" || zoneName == "Eire") {
    if (utcOffset == std::chrono::hours{0}) return "GMT";
    if (utcOffset == std::chrono::hours{1}) return "IST";
  }
  return databaseAbbreviation;
}

struct TzifType {
  std::int32_t utcOffset = 0;
  bool daylight = false;
  std::string abbreviation;
};

struct TzifZone {
  std::vector<std::int64_t> transitions;
  std::vector<std::uint8_t> transitionTypes;
  std::vector<TzifType> types;
};

struct TzifCounts {
  std::uint32_t utcIndicators = 0;
  std::uint32_t standardIndicators = 0;
  std::uint32_t leapSeconds = 0;
  std::uint32_t transitions = 0;
  std::uint32_t types = 0;
  std::uint32_t abbreviationBytes = 0;
};

std::uint32_t ReadBig32(const std::vector<unsigned char>& bytes,
                        std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::int64_t ReadSignedTime(const std::vector<unsigned char>& bytes,
                            std::size_t offset, std::size_t width) {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < width; ++i)
    value = (value << 8) | bytes[offset + i];
  if (width == 4)
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(value));
  if ((value & (std::uint64_t{1} << 63)) == 0)
    return static_cast<std::int64_t>(value);
  const std::uint64_t magnitude = (~value) + 1;
  if (magnitude == (std::uint64_t{1} << 63))
    return std::numeric_limits<std::int64_t>::min();
  return -static_cast<std::int64_t>(magnitude);
}

bool ReadTzifHeader(const std::vector<unsigned char>& bytes,
                    std::size_t offset, char& version, TzifCounts& counts) {
  if (offset + 44 > bytes.size() || bytes[offset] != 'T' ||
      bytes[offset + 1] != 'Z' || bytes[offset + 2] != 'i' ||
      bytes[offset + 3] != 'f')
    return false;
  version = static_cast<char>(bytes[offset + 4]);
  counts.utcIndicators = ReadBig32(bytes, offset + 20);
  counts.standardIndicators = ReadBig32(bytes, offset + 24);
  counts.leapSeconds = ReadBig32(bytes, offset + 28);
  counts.transitions = ReadBig32(bytes, offset + 32);
  counts.types = ReadBig32(bytes, offset + 36);
  counts.abbreviationBytes = ReadBig32(bytes, offset + 40);
  return counts.types > 0 && counts.types <= 256;
}

bool AddChecked(std::size_t& value, std::uint64_t amount,
                std::size_t limit) {
  if (amount > limit || value > limit - static_cast<std::size_t>(amount))
    return false;
  value += static_cast<std::size_t>(amount);
  return true;
}

bool SkipTzifBlock(const std::vector<unsigned char>& bytes, std::size_t& offset,
                   const TzifCounts& counts, std::size_t timeWidth) {
  const std::uint64_t size =
      static_cast<std::uint64_t>(counts.transitions) * timeWidth +
      counts.transitions + static_cast<std::uint64_t>(counts.types) * 6 +
      counts.abbreviationBytes +
      static_cast<std::uint64_t>(counts.leapSeconds) * (timeWidth + 4) +
      counts.standardIndicators + counts.utcIndicators;
  return AddChecked(offset, size, bytes.size());
}

std::shared_ptr<const TzifZone> ParseTzif(
    const std::vector<unsigned char>& bytes) {
  char version = '\0';
  TzifCounts counts;
  if (!ReadTzifHeader(bytes, 0, version, counts)) return {};

  std::size_t offset = 44;
  std::size_t timeWidth = 4;
  if (version == '2' || version == '3' || version == '4') {
    if (!SkipTzifBlock(bytes, offset, counts, 4)) return {};
    if (!ReadTzifHeader(bytes, offset, version, counts)) return {};
    offset += 44;
    timeWidth = 8;
  }

  const std::uint64_t required =
      static_cast<std::uint64_t>(counts.transitions) * timeWidth +
      counts.transitions + static_cast<std::uint64_t>(counts.types) * 6 +
      counts.abbreviationBytes;
  if (required > bytes.size() || offset > bytes.size() - required) return {};

  auto zone = std::make_shared<TzifZone>();
  zone->transitions.reserve(counts.transitions);
  for (std::uint32_t i = 0; i < counts.transitions; ++i) {
    zone->transitions.push_back(ReadSignedTime(bytes, offset, timeWidth));
    offset += timeWidth;
  }
  zone->transitionTypes.assign(bytes.begin() + offset,
                               bytes.begin() + offset + counts.transitions);
  offset += counts.transitions;

  struct RawType {
    std::int32_t offset;
    bool daylight;
    std::uint8_t abbreviation;
  };
  std::vector<RawType> rawTypes;
  rawTypes.reserve(counts.types);
  for (std::uint32_t i = 0; i < counts.types; ++i) {
    rawTypes.push_back(
        {static_cast<std::int32_t>(ReadBig32(bytes, offset)),
         bytes[offset + 4] != 0, bytes[offset + 5]});
    offset += 6;
  }
  const std::string abbreviations(
      reinterpret_cast<const char*>(bytes.data() + offset),
      counts.abbreviationBytes);
  for (const RawType& raw : rawTypes) {
    if (raw.abbreviation >= abbreviations.size()) return {};
    const std::size_t end = abbreviations.find('\0', raw.abbreviation);
    zone->types.push_back(
        {raw.offset, raw.daylight,
         abbreviations.substr(raw.abbreviation, end - raw.abbreviation)});
  }
  for (std::uint8_t type : zone->transitionTypes)
    if (type >= zone->types.size()) return {};
  return zone;
}

const std::vector<std::filesystem::path>& ZoneInfoRoots() {
  static const std::vector<std::filesystem::path> roots = [] {
    std::vector<std::filesystem::path> result;
    if (const char* configured = std::getenv("OCPN_TIMEZONE_DIR"))
      if (*configured) result.emplace_back(configured);
#ifndef _WIN32
    result.emplace_back("/usr/share/zoneinfo");
    result.emplace_back("/usr/share/lib/zoneinfo");
    result.emplace_back("/usr/lib/zoneinfo");
#endif
    return result;
  }();
  return roots;
}

bool SafeZoneName(const std::string& name) {
  if (name.empty() || name.front() == '/' ||
      name.find('\\') != std::string::npos)
    return false;
  const std::filesystem::path path(name);
  for (const auto& part : path)
    if (part == ".." || part == ".") return false;
  return true;
}

std::filesystem::path FindZoneFile(const wxString& zoneName) {
  const std::string name = zoneName.ToStdString();
  if (!SafeZoneName(name)) return {};
  std::error_code error;
  for (const auto& root : ZoneInfoRoots()) {
    const auto canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error) {
      error.clear();
      continue;
    }
    const auto candidate =
        std::filesystem::weakly_canonical(canonicalRoot / name, error);
    if (error) {
      error.clear();
      continue;
    }
    const auto relative = std::filesystem::relative(candidate, canonicalRoot,
                                                     error);
    if (error || relative.empty() || *relative.begin() == "..") {
      error.clear();
      continue;
    }
    if (std::filesystem::is_regular_file(candidate, error) && !error)
      return candidate;
    error.clear();
  }
  return {};
}

std::shared_ptr<const TzifZone> LoadTzif(const wxString& zoneName) {
  static std::mutex mutex;
  static std::map<std::string, std::shared_ptr<const TzifZone>> cache;
  const std::string key = zoneName.ToStdString();
  {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
  }

  std::shared_ptr<const TzifZone> parsed;
  const auto path = FindZoneFile(zoneName);
  if (!path.empty()) {
    std::ifstream stream(path, std::ios::binary);
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    parsed = ParseTzif(bytes);
  }
  const std::lock_guard<std::mutex> lock(mutex);
  return cache.emplace(key, std::move(parsed)).first->second;
}

const TzifType* TypeAt(const TzifZone& zone, std::int64_t utcSeconds) {
  if (zone.types.empty()) return nullptr;
  const auto after =
      std::upper_bound(zone.transitions.begin(), zone.transitions.end(),
                       utcSeconds);
  if (after != zone.transitions.begin()) {
    const std::size_t index =
        static_cast<std::size_t>(after - zone.transitions.begin() - 1);
    return &zone.types[zone.transitionTypes[index]];
  }
  for (const TzifType& type : zone.types)
    if (!type.daylight) return &type;
  return &zone.types.front();
}

std::vector<wxString> TzifZoneNames() {
  std::set<std::string> names;
  const std::set<std::string> excluded = {
      "iso3166.tab", "leap-seconds.list", "leapseconds", "localtime",
      "posixrules", "tzdata.zi", "zone.tab", "zone1970.tab"};
  std::error_code error;
  for (const auto& root : ZoneInfoRoots()) {
    if (!std::filesystem::is_directory(root, error)) {
      error.clear();
      continue;
    }
    std::filesystem::recursive_directory_iterator iterator(
        root, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
      const auto relative = std::filesystem::relative(iterator->path(), root,
                                                       error);
      if (error) break;
      const std::string name = relative.generic_string();
      if (iterator->is_directory(error) &&
          (name == "posix" || name == "right")) {
        iterator.disable_recursion_pending();
      } else if (!error && excluded.count(name) == 0 &&
                 name.rfind("posix/", 0) != 0 &&
                 name.rfind("right/", 0) != 0 &&
                 (iterator->is_regular_file(error) ||
                  iterator->is_symlink(error))) {
        std::ifstream stream(iterator->path(), std::ios::binary);
        char magic[4] = {};
        if (stream.read(magic, sizeof(magic)) &&
            std::string(magic, sizeof(magic)) == "TZif")
          names.insert(name);
      }
      error.clear();
      iterator.increment(error);
    }
    error.clear();
  }
  std::vector<wxString> result;
  result.reserve(names.size());
  for (const auto& name : names) result.push_back(wxString::FromUTF8(name));
  return result;
}

#if MARINE_TIME_HAS_CHRONO_TZDB
const std::chrono::time_zone* Locate(const wxString& name) {
  try {
    return std::chrono::locate_zone(name.ToStdString());
  } catch (const std::runtime_error&) {
    return nullptr;
  }
}

std::chrono::sys_seconds ToSysSeconds(const wxDateTime& value) {
  return std::chrono::sys_seconds{
      std::chrono::seconds{static_cast<std::int64_t>(value.GetTicks())}};
}
#endif

}  // namespace

std::vector<wxString> AvailableTimeZones() {
  std::set<wxString> names;
#if MARINE_TIME_HAS_CHRONO_TZDB
  try {
    const auto& zones = std::chrono::get_tzdb().zones;
    for (const auto& zone : zones) {
      const auto zoneName = zone.name();
      const wxString name =
          wxString::FromUTF8(zoneName.data(), zoneName.size());
      if (name != "UTC") names.insert(name);
    }
  } catch (const std::runtime_error&) {
  }
#endif
  for (const wxString& name : TzifZoneNames())
    if (name != "UTC") names.insert(name);
  std::vector<wxString> result;
  result.reserve(names.size() + 1);
  result.emplace_back("UTC");
  result.insert(result.end(), names.begin(), names.end());
  return result;
}

wxString SystemTimeZone() {
#if MARINE_TIME_HAS_CHRONO_TZDB
  try {
    const auto name = std::chrono::current_zone()->name();
    return wxString::FromUTF8(name.data(), name.size());
  } catch (const std::runtime_error&) {
  }
#endif
  if (const char* environment = std::getenv("TZ")) {
    wxString name = wxString::FromUTF8(environment);
    if (name.StartsWith(":")) name.Remove(0, 1);
    if (LoadTzif(name)) return name;
  }
#ifndef _WIN32
  std::error_code error;
  const auto local =
      std::filesystem::weakly_canonical("/etc/localtime", error).string();
  const std::string marker = "/zoneinfo/";
  const std::size_t markerAt = local.find(marker);
  if (!error && markerAt != std::string::npos) {
    const wxString name =
        wxString::FromUTF8(local.substr(markerAt + marker.size()));
    if (LoadTzif(name)) return name;
  }
  std::ifstream timezone("/etc/timezone");
  std::string name;
  if (std::getline(timezone, name)) {
    const wxString candidate = wxString::FromUTF8(name);
    if (LoadTzif(candidate)) return candidate;
  }
#endif
  return "UTC";
}

bool IsTimeZoneAvailable(const wxString& zoneName) {
  if (zoneName == "UTC") return true;
#if MARINE_TIME_HAS_CHRONO_TZDB
  if (Locate(zoneName)) return true;
#endif
  return LoadTzif(zoneName) != nullptr;
}

wxDateTime ToWallClock(const wxDateTime& utc, const wxString& zoneName) {
  if (!utc.IsValid()) return wxDateTime();
  if (zoneName == "UTC") return DateTimeFromSeconds(
      std::chrono::seconds{static_cast<std::int64_t>(utc.GetTicks())});
#if MARINE_TIME_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(zoneName)) {
    const auto wall = zone->to_local(ToSysSeconds(utc));
    return DateTimeFromSeconds(
        std::chrono::duration_cast<std::chrono::seconds>(
            wall.time_since_epoch()));
  }
#endif
  if (const auto zone = LoadTzif(zoneName)) {
    if (const auto* type =
            TypeAt(*zone, static_cast<std::int64_t>(utc.GetTicks()))) {
      return DateTimeFromSeconds(
          std::chrono::seconds{static_cast<std::int64_t>(utc.GetTicks()) +
                               type->utcOffset});
    }
  }
  return wxDateTime();
}

WallClockConversion FromWallClock(int year, int month, int day, int hour,
                                  int minute, int second,
                                  const wxString& zoneName) {
  using namespace std::chrono;
  WallClockConversion result;
  const year_month_day date{std::chrono::year{year},
                            std::chrono::month{static_cast<unsigned>(month)},
                            std::chrono::day{static_cast<unsigned>(day)}};
  if (!date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return result;
  }
  const local_seconds wall{local_days{date}.time_since_epoch() + hours{hour} +
                           minutes{minute} + seconds{second}};

  if (zoneName == "UTC") {
    result.utc = DateTimeFromSeconds(
        duration_cast<seconds>(wall.time_since_epoch()));
    result.status = WallClockStatus::Valid;
    return result;
  }

#if MARINE_TIME_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(zoneName)) {
    const auto info = zone->get_info(wall);
    if (info.result == local_info::nonexistent) {
      result.status = WallClockStatus::Nonexistent;
      return result;
    }
    const bool ambiguous = info.result == local_info::ambiguous;
    const auto instant = zone->to_sys(wall, choose::earliest);
    result.utc = DateTimeFromSeconds(
        duration_cast<seconds>(instant.time_since_epoch()));
    result.status =
        ambiguous ? WallClockStatus::Ambiguous : WallClockStatus::Valid;
    return result;
  }
#endif
  if (const auto zone = LoadTzif(zoneName)) {
    const auto naive =
        duration_cast<seconds>(wall.time_since_epoch()).count();
    std::set<std::int64_t> candidates;
    for (const TzifType& possible : zone->types) {
      const std::int64_t candidate = naive - possible.utcOffset;
      const TzifType* actual = TypeAt(*zone, candidate);
      if (actual && candidate + actual->utcOffset == naive)
        candidates.insert(candidate);
    }
    if (candidates.empty()) {
      result.status = WallClockStatus::Nonexistent;
      return result;
    }
    result.utc = DateTimeFromSeconds(seconds{*candidates.begin()});
    result.status = candidates.size() > 1 ? WallClockStatus::Ambiguous
                                          : WallClockStatus::Valid;
  }
  return result;
}

wxString TimeZoneAbbreviation(const wxDateTime& utc,
                              const wxString& zoneName) {
  if (!utc.IsValid()) return wxEmptyString;
  if (zoneName == "UTC") return "UTC";
#if MARINE_TIME_HAS_CHRONO_TZDB
  if (const auto* zone = Locate(zoneName)) {
    const auto info = zone->get_info(ToSysSeconds(utc));
    return CanonicalAbbreviation(
        zoneName, std::chrono::duration_cast<std::chrono::seconds>(info.offset),
        wxString::FromUTF8(info.abbrev));
  }
#endif
  if (const auto zone = LoadTzif(zoneName)) {
    if (const auto* type =
            TypeAt(*zone, static_cast<std::int64_t>(utc.GetTicks())))
      return CanonicalAbbreviation(
          zoneName, std::chrono::seconds{type->utcOffset},
          wxString::FromUTF8(type->abbreviation));
  }
  return wxEmptyString;
}

wxString FormatInTimeZone(const wxDateTime& utc, const wxString& format,
                          const wxString& zoneName,
                          bool appendAbbreviation) {
  const wxDateTime wall = ToWallClock(utc, zoneName);
  if (!wall.IsValid()) return wxEmptyString;
  wxString result = wall.Format(format, wxDateTime::UTC);
  if (appendAbbreviation) {
    const wxString abbreviation = TimeZoneAbbreviation(utc, zoneName);
    if (!abbreviation.empty()) result += " " + abbreviation;
  }
  return result;
}

}  // namespace marine_time
