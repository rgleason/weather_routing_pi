/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#ifndef WEATHER_ROUTING_APPEND_ONLY_CACHE_H
#define WEATHER_ROUTING_APPEND_ONLY_CACHE_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace weather_routing {

struct AppendOnlyCacheRecord {
  std::string key;
  std::vector<unsigned char> value;
};

/**
 * Small crash-tolerant append-only key/value store for rebuildable caches.
 *
 * Records carry a CRC and are committed by complete append. A truncated or
 * corrupt final record is ignored while all earlier records remain usable.
 * The identity is supplied by the caller and invalidates the complete store
 * when charts, algorithms or data formats change.
 */
class AppendOnlyCache {
public:
  AppendOnlyCache();

  bool Open(const std::string& path, const std::string& identity,
            std::size_t maximum_entries, std::string* error = nullptr);
  bool PutBatch(const std::vector<AppendOnlyCacheRecord>& records,
                std::string* error = nullptr);
  bool Erase(const std::string& key, std::string* error = nullptr);
  bool Get(const std::string& key, std::vector<unsigned char>* value,
           std::string* error = nullptr);
  /** Return whether a CRC-validated live record is present in the index. */
  bool Contains(const std::string& key) const;
  bool Compact(std::string* error = nullptr);
  bool Clear(std::string* error = nullptr);

  std::size_t EntryCount() const;
  std::uint64_t FileBytes() const;
  std::uint64_t BytesRead() const;
  std::uint64_t BytesWritten() const;
  std::uint64_t RecoveredTailRecords() const;
  std::uint64_t Compactions() const;
  bool IdentityWasReset() const;

private:
  struct IndexEntry {
    std::uint64_t value_offset;
    std::uint32_t value_size;
    std::uint32_t crc;
    std::uint64_t access_serial;
  };

  bool Reset(std::string* error);
  bool WriteHeader(std::ostream& output, std::string* error);
  bool ReadValue(const IndexEntry& entry, const std::string& key,
                 std::vector<unsigned char>* value, std::string* error);
  bool AppendTombstones(const std::vector<std::string>& keys,
                        std::string* error);
  bool EnforceEntryLimit(std::string* error);

  std::string path_;
  std::string identity_;
  std::size_t maximum_entries_;
  std::map<std::string, IndexEntry> index_;
  std::uint64_t serial_;
  std::uint64_t file_bytes_;
  std::uint64_t bytes_read_;
  std::uint64_t bytes_written_;
  std::uint64_t recovered_tail_records_;
  std::uint64_t compactions_;
  bool identity_was_reset_;
  bool opened_;
};

}  // namespace weather_routing

#endif  // WEATHER_ROUTING_APPEND_ONLY_CACHE_H
