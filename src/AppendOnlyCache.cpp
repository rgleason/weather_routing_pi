/***************************************************************************
 * Copyright (C) 2026 OpenCPN contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 ***************************************************************************/

#include "AppendOnlyCache.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr char kFileMagic[8] = {'O', 'C', 'P', 'C', 'A', 'C', 'H', '1'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::uint32_t kRecordMagic = 0x31524357U;  // "WCR1"
constexpr std::uint32_t kTombstoneSize =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kMaximumKeyBytes = 4096;
constexpr std::uint32_t kMaximumValueBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kMaximumIdentityBytes = 64U * 1024U;

template <typename T>
bool WriteScalar(std::ostream& output, const T& value) {
  output.write(reinterpret_cast<const char*>(&value), sizeof(value));
  return output.good();
}

template <typename T>
bool ReadScalar(std::istream& input, T* value) {
  input.read(reinterpret_cast<char*>(value), sizeof(*value));
  return input.good();
}

std::uint32_t UpdateCrc32(std::uint32_t crc, const unsigned char* data,
                          std::size_t size) {
  crc = ~crc;
  for (std::size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
  }
  return ~crc;
}

std::uint32_t RecordCrc(const std::string& key,
                        const std::vector<unsigned char>& value) {
  std::uint32_t crc = UpdateCrc32(
      0, reinterpret_cast<const unsigned char*>(key.data()), key.size());
  if (!value.empty()) crc = UpdateCrc32(crc, value.data(), value.size());
  return crc;
}

void SetError(std::string* error, const std::string& message) {
  if (error) *error = message;
}

bool ReplacePathWithFile(const std::string& replacement,
                         const std::string& destination) {
#ifdef _WIN32
  const std::wstring replacement_w =
      std::filesystem::path(replacement).wstring();
  const std::wstring destination_w =
      std::filesystem::path(destination).wstring();
  if (::ReplaceFileW(destination_w.c_str(), replacement_w.c_str(), nullptr,
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr))
    return true;
  return ::MoveFileExW(replacement_w.c_str(), destination_w.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
         FALSE;
#else
  return std::rename(replacement.c_str(), destination.c_str()) == 0;
#endif
}

bool PreserveIncompatibleFile(const std::string& path, std::string* error) {
  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error)) return true;
  if (exists_error) {
    SetError(error, "unable to inspect incompatible cache file: " +
                        exists_error.message());
    return false;
  }

  std::string preserved = path + ".incompatible";
  for (unsigned int suffix = 1; std::filesystem::exists(preserved); ++suffix)
    preserved = path + ".incompatible." + std::to_string(suffix);

  std::error_code rename_error;
  std::filesystem::rename(path, preserved, rename_error);
  if (rename_error) {
    SetError(error, "refusing to overwrite incompatible cache file: " +
                        rename_error.message());
    return false;
  }
  return true;
}

bool WriteRecord(std::ostream& output, const std::string& key,
                 const std::vector<unsigned char>* value,
                 std::uint64_t* value_offset, std::uint32_t* crc_out,
                 std::string* error) {
  if (key.empty() || key.size() > kMaximumKeyBytes) {
    SetError(error, "invalid cache key size");
    return false;
  }
  if (value && value->size() > kMaximumValueBytes) {
    SetError(error, "cache value exceeds record limit");
    return false;
  }
  const std::uint32_t key_size = static_cast<std::uint32_t>(key.size());
  const std::uint32_t value_size =
      value ? static_cast<std::uint32_t>(value->size()) : kTombstoneSize;
  const std::vector<unsigned char> empty;
  const std::uint32_t crc = RecordCrc(key, value ? *value : empty);
  if (!WriteScalar(output, kRecordMagic) || !WriteScalar(output, key_size) ||
      !WriteScalar(output, value_size) || !WriteScalar(output, crc)) {
    SetError(error, "unable to write cache record header");
    return false;
  }
  output.write(key.data(), key.size());
  if (!output.good()) {
    SetError(error, "unable to write cache record key");
    return false;
  }
  if (value_offset)
    *value_offset = static_cast<std::uint64_t>(output.tellp());
  if (value && !value->empty())
    output.write(reinterpret_cast<const char*>(value->data()), value->size());
  if (!output.good()) {
    SetError(error, "unable to write cache record payload");
    return false;
  }
  if (crc_out) *crc_out = crc;
  return true;
}

}  // namespace

namespace weather_routing {

AppendOnlyCache::AppendOnlyCache()
    : maximum_entries_(0),
      serial_(0),
      file_bytes_(0),
      bytes_read_(0),
      bytes_written_(0),
      recovered_tail_records_(0),
      compactions_(0),
      identity_was_reset_(false),
      opened_(false) {}

bool AppendOnlyCache::WriteHeader(std::ostream& output, std::string* error) {
  if (identity_.size() > kMaximumIdentityBytes) {
    SetError(error, "cache identity exceeds format limit");
    return false;
  }
  const std::uint32_t identity_size =
      static_cast<std::uint32_t>(identity_.size());
  output.write(kFileMagic, sizeof(kFileMagic));
  if (!WriteScalar(output, kFormatVersion) ||
      !WriteScalar(output, identity_size)) {
    SetError(error, "unable to write cache header");
    return false;
  }
  output.write(identity_.data(), identity_.size());
  if (!output.good()) {
    SetError(error, "unable to write cache identity");
    return false;
  }
  return true;
}

bool AppendOnlyCache::Reset(std::string* error) {
  std::ofstream output(path_, std::ios::out | std::ios::binary |
                                  std::ios::trunc);
  if (!output.is_open()) {
    SetError(error, "unable to create cache file");
    return false;
  }
  if (!WriteHeader(output, error)) return false;
  output.close();
  if (!output.good()) {
    SetError(error, "unable to finalize cache header");
    return false;
  }
  index_.clear();
  serial_ = 0;
  std::ifstream input(path_, std::ios::binary | std::ios::ate);
  file_bytes_ =
      input.is_open() ? static_cast<std::uint64_t>(input.tellg()) : 0;
  bytes_written_ += file_bytes_;
  opened_ = true;
  return true;
}

bool AppendOnlyCache::Open(const std::string& path,
                           const std::string& identity,
                           std::size_t maximum_entries, std::string* error) {
  path_ = path;
  identity_ = identity;
  maximum_entries_ = std::max<std::size_t>(1, maximum_entries);
  index_.clear();
  serial_ = 0;
  file_bytes_ = 0;
  bytes_read_ = 0;
  bytes_written_ = 0;
  recovered_tail_records_ = 0;
  identity_was_reset_ = false;
  opened_ = false;

  std::ifstream input(path_, std::ios::in | std::ios::binary);
  if (!input.is_open()) return Reset(error);

  char magic[sizeof(kFileMagic)] = {};
  std::uint32_t version = 0;
  std::uint32_t identity_size = 0;
  input.read(magic, sizeof(magic));
  const bool basic_header =
      input.good() && !std::memcmp(magic, kFileMagic, sizeof(kFileMagic)) &&
      ReadScalar(input, &version) && ReadScalar(input, &identity_size) &&
      version == kFormatVersion && identity_size <= kMaximumIdentityBytes;
  std::string stored_identity(identity_size, '\0');
  if (basic_header && identity_size)
    input.read(&stored_identity[0], identity_size);
  if (!basic_header || !input.good() || stored_identity != identity_) {
    identity_was_reset_ = true;
    input.close();
    if (!PreserveIncompatibleFile(path_, error)) return false;
    return Reset(error);
  }

  std::uint64_t valid_file_bytes =
      sizeof(kFileMagic) + sizeof(version) + sizeof(identity_size) +
      identity_size;
  while (true) {
    const std::streampos record_start = input.tellg();
    std::uint32_t magic_value = 0;
    std::uint32_t key_size = 0;
    std::uint32_t value_size = 0;
    std::uint32_t stored_crc = 0;
    if (!ReadScalar(input, &magic_value)) {
      if (!input.eof()) ++recovered_tail_records_;
      break;
    }
    if (!ReadScalar(input, &key_size) || !ReadScalar(input, &value_size) ||
        !ReadScalar(input, &stored_crc) || magic_value != kRecordMagic ||
        key_size == 0 || key_size > kMaximumKeyBytes ||
        (value_size != kTombstoneSize &&
         value_size > kMaximumValueBytes)) {
      ++recovered_tail_records_;
      break;
    }
    std::string key(key_size, '\0');
    input.read(&key[0], key_size);
    if (!input.good()) {
      ++recovered_tail_records_;
      break;
    }
    const std::uint64_t value_offset =
        static_cast<std::uint64_t>(input.tellg());
    std::vector<unsigned char> value;
    if (value_size != kTombstoneSize) {
      value.resize(value_size);
      if (value_size)
        input.read(reinterpret_cast<char*>(value.data()), value_size);
      if (!input.good() || RecordCrc(key, value) != stored_crc) {
        ++recovered_tail_records_;
        break;
      }
      index_[key] = {value_offset, value_size, stored_crc, ++serial_};
    } else {
      const std::vector<unsigned char> empty;
      if (RecordCrc(key, empty) != stored_crc) {
        ++recovered_tail_records_;
        break;
      }
      index_.erase(key);
      ++serial_;
    }
    const std::streampos record_end = input.tellg();
    if (record_end <= record_start) {
      ++recovered_tail_records_;
      break;
    }
    valid_file_bytes = static_cast<std::uint64_t>(record_end);
    bytes_read_ += static_cast<std::uint64_t>(record_end - record_start);
  }
  input.clear();
  input.seekg(0, std::ios::end);
  file_bytes_ = static_cast<std::uint64_t>(input.tellg());
  input.close();
  if (recovered_tail_records_ && file_bytes_ > valid_file_bytes) {
    std::error_code resize_error;
    std::filesystem::resize_file(path_, valid_file_bytes, resize_error);
    if (resize_error) {
      SetError(error, "unable to truncate incomplete cache tail");
      return false;
    }
    file_bytes_ = valid_file_bytes;
  }
  opened_ = true;
  return EnforceEntryLimit(error);
}

bool AppendOnlyCache::ReadValue(const IndexEntry& entry,
                                const std::string& key,
                                std::vector<unsigned char>* value,
                                std::string* error) {
  if (!value) {
    SetError(error, "cache output is null");
    return false;
  }
  std::ifstream input(path_, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    SetError(error, "unable to open cache for reading");
    return false;
  }
  input.seekg(static_cast<std::streamoff>(entry.value_offset));
  value->resize(entry.value_size);
  if (entry.value_size)
    input.read(reinterpret_cast<char*>(value->data()), entry.value_size);
  if (!input.good() || RecordCrc(key, *value) != entry.crc) {
    value->clear();
    SetError(error, "cache record failed CRC validation");
    return false;
  }
  bytes_read_ += entry.value_size;
  return true;
}

bool AppendOnlyCache::Get(const std::string& key,
                          std::vector<unsigned char>* value,
                          std::string* error) {
  std::map<std::string, IndexEntry>::iterator found = index_.find(key);
  if (found == index_.end()) return false;
  if (!ReadValue(found->second, key, value, error)) {
    index_.erase(found);
    return false;
  }
  found->second.access_serial = ++serial_;
  return true;
}

bool AppendOnlyCache::Contains(const std::string& key) const {
  return opened_ && index_.find(key) != index_.end();
}

bool AppendOnlyCache::Erase(const std::string& key, std::string* error) {
  if (!opened_) {
    SetError(error, "cache is not open");
    return false;
  }
  const auto found = index_.find(key);
  if (found == index_.end()) return true;
  const IndexEntry previous = found->second;
  index_.erase(found);
  if (AppendTombstones({key}, error)) return true;
  index_[key] = previous;
  return false;
}

bool AppendOnlyCache::AppendTombstones(
    const std::vector<std::string>& keys, std::string* error) {
  if (keys.empty()) return true;
  std::ofstream output(path_,
                       std::ios::out | std::ios::binary | std::ios::app);
  if (!output.is_open()) {
    SetError(error, "unable to open cache for tombstones");
    return false;
  }
  const std::streampos before = output.tellp();
  for (const std::string& key : keys)
    if (!WriteRecord(output, key, nullptr, nullptr, nullptr, error))
      return false;
  output.close();
  if (!output.good()) {
    SetError(error, "unable to finalize cache tombstones");
    return false;
  }
  std::ifstream size_input(path_, std::ios::binary | std::ios::ate);
  file_bytes_ = static_cast<std::uint64_t>(size_input.tellg());
  bytes_written_ +=
      file_bytes_ - static_cast<std::uint64_t>(before);
  return true;
}

bool AppendOnlyCache::EnforceEntryLimit(std::string* error) {
  if (index_.size() <= maximum_entries_) return true;
  std::vector<std::pair<std::uint64_t, std::string>> access;
  access.reserve(index_.size());
  for (const auto& item : index_)
    access.push_back({item.second.access_serial, item.first});
  std::sort(access.begin(), access.end());
  const std::size_t remove_count = index_.size() - maximum_entries_;
  std::vector<std::string> removed;
  removed.reserve(remove_count);
  for (std::size_t i = 0; i < remove_count; ++i) {
    removed.push_back(access[i].second);
    index_.erase(access[i].second);
  }
  return AppendTombstones(removed, error);
}

bool AppendOnlyCache::PutBatch(
    const std::vector<AppendOnlyCacheRecord>& records, std::string* error) {
  if (!opened_) {
    SetError(error, "cache is not open");
    return false;
  }
  if (records.empty()) return true;
  std::ofstream output(path_,
                       std::ios::out | std::ios::binary | std::ios::app);
  if (!output.is_open()) {
    SetError(error, "unable to open cache for append");
    return false;
  }
  const std::uint64_t before = static_cast<std::uint64_t>(output.tellp());
  for (const AppendOnlyCacheRecord& record : records) {
    std::uint64_t value_offset = 0;
    std::uint32_t crc = 0;
    if (!WriteRecord(output, record.key, &record.value, &value_offset, &crc,
                     error))
      return false;
    index_[record.key] = {
        value_offset, static_cast<std::uint32_t>(record.value.size()), crc,
        ++serial_};
  }
  output.close();
  if (!output.good()) {
    SetError(error, "unable to finalize cache append");
    return false;
  }
  std::ifstream size_input(path_, std::ios::binary | std::ios::ate);
  file_bytes_ = static_cast<std::uint64_t>(size_input.tellg());
  bytes_written_ += file_bytes_ - before;
  return EnforceEntryLimit(error);
}

bool AppendOnlyCache::Compact(std::string* error) {
  if (!opened_) {
    SetError(error, "cache is not open");
    return false;
  }
  const std::string temporary_path = path_ + ".compact.tmp";
  std::ofstream output(temporary_path,
                       std::ios::out | std::ios::binary | std::ios::trunc);
  if (!output.is_open() || !WriteHeader(output, error)) return false;

  std::map<std::string, IndexEntry> replacement;
  for (const auto& item : index_) {
    std::vector<unsigned char> value;
    if (!ReadValue(item.second, item.first, &value, error)) {
      output.close();
      std::remove(temporary_path.c_str());
      return false;
    }
    std::uint64_t offset = 0;
    std::uint32_t crc = 0;
    if (!WriteRecord(output, item.first, &value, &offset, &crc, error)) {
      output.close();
      std::remove(temporary_path.c_str());
      return false;
    }
    replacement[item.first] = {
        offset, static_cast<std::uint32_t>(value.size()), crc,
        item.second.access_serial};
  }
  output.close();
  if (!output.good()) {
    SetError(error, "unable to finalize compacted cache");
    std::remove(temporary_path.c_str());
    return false;
  }
  if (!ReplacePathWithFile(temporary_path, path_)) {
    SetError(error, "unable to replace cache with compacted file");
    std::remove(temporary_path.c_str());
    return false;
  }
  index_.swap(replacement);
  std::ifstream size_input(path_, std::ios::binary | std::ios::ate);
  file_bytes_ = static_cast<std::uint64_t>(size_input.tellg());
  bytes_written_ += file_bytes_;
  ++compactions_;
  return true;
}

bool AppendOnlyCache::Clear(std::string* error) {
  if (path_.empty()) {
    SetError(error, "cache path is empty");
    return false;
  }
  return Reset(error);
}

std::size_t AppendOnlyCache::EntryCount() const { return index_.size(); }
std::uint64_t AppendOnlyCache::FileBytes() const { return file_bytes_; }
std::uint64_t AppendOnlyCache::BytesRead() const { return bytes_read_; }
std::uint64_t AppendOnlyCache::BytesWritten() const {
  return bytes_written_;
}
std::uint64_t AppendOnlyCache::RecoveredTailRecords() const {
  return recovered_tail_records_;
}
std::uint64_t AppendOnlyCache::Compactions() const { return compactions_; }
bool AppendOnlyCache::IdentityWasReset() const {
  return identity_was_reset_;
}

}  // namespace weather_routing
