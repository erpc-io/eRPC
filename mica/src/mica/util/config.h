#pragma once
#ifndef MICA_UTIL_CONFIG_H_
#define MICA_UTIL_CONFIG_H_

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "mica/common.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wexpansion-to-defined"
#include "mica/util/rapidjson/document.h"
#pragma GCC diagnostic pop

namespace mica {
namespace util {
class Config {
 public:
  Config();
  Config(const Config& config);
  ~Config() {}

  // This constructor is public to support emplace_back(), emplace().
  Config(const std::shared_ptr<rapidjson::Document>& root,
         rapidjson::Value* current, std::string path);

  Config& operator=(const Config& config) = delete;

  static Config empty_array(std::string path);
  static Config empty_dict(std::string path);

  static Config load_file(std::string path);
  void dump_file(std::string path) const;

  static Config load(std::string json_text, std::string path);
  std::string dump() const;

  std::string get_path() const;

  bool exists() const;
  bool is_bool() const;
  bool is_int64() const;
  bool is_uint64() const;
  bool is_double() const;
  bool is_str() const;
  bool is_array() const;
  bool is_dict() const;

  bool get_bool() const;
  int64_t get_int64() const;
  uint64_t get_uint64() const;
  double get_double() const;
  std::string get_str() const;

  bool get_bool(bool default_v) const;
  int64_t get_int64(int64_t default_v) const;
  uint64_t get_uint64(uint64_t default_v) const;
  double get_double(double default_v) const;
  std::string get_str(const std::string& default_v) const;

  size_t size() const;
  const Config get(size_t index) const;
  Config get(size_t index);

  std::vector<std::string> keys() const;
  const Config get(std::string key) const;
  Config get(std::string key);

  Config& push_back_bool(bool v);
  Config& push_back_int64(int64_t v);
  Config& push_back_uint64(uint64_t v);
  Config& push_back_double(double v);
  Config& push_back_string(const std::string& v);
  Config& push_back_array(const Config& v);
  Config& push_back_dict(const Config& v);

  Config& insert_bool(std::string key, bool v);
  Config& insert_int64(std::string key, int64_t v);
  Config& insert_uint64(std::string key, uint64_t v);
  Config& insert_double(std::string key, double v);
  Config& insert_string(std::string key, std::string v);
  Config& insert_array(std::string key, const Config& v);
  Config& insert_dict(std::string key, const Config& v);

 private:
  std::shared_ptr<rapidjson::Document> root_;
  rapidjson::Value* current_;
  std::string path_;
};
}
}

#endif
