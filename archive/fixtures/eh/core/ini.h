// ini.h
//
// Minimal ini reader used for the shipped configuration files.
//
// Copyright (c) Northlight Interactive. Internal core header.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace nl {
namespace core {

class IniFile {
public:
    // Reads the whole file into a key to string map. Returns false when the
    // file cannot be opened or fails to parse. A file that parses but is
    // empty returns true and answers false to every Get.
    bool Load(const char* path);

    bool GetInt(const char* key, std::int32_t& out) const;
    bool GetFloat(const char* key, float& out) const;
    bool GetBool(const char* key, bool& out) const;
    bool GetString(const char* key, std::string& out) const;

    bool Has(const char* key) const;
    std::size_t KeyCount() const { return values_.size(); }

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace core
}  // namespace nl
