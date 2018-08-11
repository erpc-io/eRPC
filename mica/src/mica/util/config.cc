//#pragma once
#ifndef MICA_UTIL_CONFIG_CC_
#define MICA_UTIL_CONFIG_CC_

#include <sstream>
#include <fstream>
#include <streambuf>
#include <cstdio>
#include "mica/util/config.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Winline"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wexpansion-to-defined"
#include "mica/util/rapidjson/stringbuffer.h"
#include "mica/util/rapidjson/writer.h"
#include "mica/util/rapidjson/error/en.h"
#pragma GCC diagnostic pop

namespace mica {
namespace util {
Config::Config() : Config(nullptr, nullptr, "") {}

Config::Config(const Config& config)
    : Config(config.root_, config.current_, config.path_) {}

Config Config::empty_array(std::string path) {
  std::shared_ptr<rapidjson::Document> root =
      std::make_shared<rapidjson::Document>();
  root->SetArray();
  return Config(root, root.get(), path);
}

Config Config::empty_dict(std::string path) {
  std::shared_ptr<rapidjson::Document> root =
      std::make_shared<rapidjson::Document>();
  root->SetObject();
  return Config(root, root.get(), path);
}

Config Config::load_file(std::string path) {
  std::string conf;

  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    fprintf(stderr, "error: could not open %s\n", path.c_str());
    assert(false);
    return Config(nullptr, nullptr, std::string() + "<" + path + ">");
  }

  ifs.seekg(0, std::ios::end);
  conf.reserve(static_cast<size_t>(ifs.tellg()));
  ifs.seekg(0, std::ios::beg);

  conf.assign((std::istreambuf_iterator<char>(ifs)),
              std::istreambuf_iterator<char>());
  return Config::load(conf, std::string() + "<" + path + ">");
}

void Config::dump_file(std::string path) const {
  assert(exists());

  std::string conf = dump();

  std::ofstream ofs(path);
  if (!ofs.is_open()) {
    fprintf(stderr, "error: could not open %s\n", path.c_str());
    assert(false);
    return;
  }

  ofs << conf;
}

Config Config::load(std::string json_text, std::string path) {
  std::shared_ptr<rapidjson::Document> root =
      std::make_shared<rapidjson::Document>();
  root->Parse<rapidjson::ParseFlag::kParseDefaultFlags |
              rapidjson::ParseFlag::kParseCommentsFlag>(json_text.c_str());

  if (root->HasParseError()) {
    fprintf(stderr, "error parsing config: %s (offset=%zu)\n",
            rapidjson::GetParseError_En(root->GetParseError()),
            root->GetErrorOffset());
    return Config(nullptr, nullptr, path);
  }

  return Config(root, root.get(), path);
}

std::string Config::dump() const {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
#pragma GCC diagnostic pop

  current_->Accept(writer);
  const char* output = buffer.GetString();
  return std::string(output);
}

std::string Config::get_path() const { return path_; }

bool Config::exists() const { return current_ != nullptr; }

bool Config::is_bool() const { return exists() && current_->IsBool(); }

bool Config::is_int64() const { return exists() && current_->IsInt64(); }

bool Config::is_uint64() const { return exists() && current_->IsUint64(); }

bool Config::is_double() const { return exists() && current_->IsDouble(); }

bool Config::is_str() const { return exists() && current_->IsString(); }

bool Config::is_array() const { return exists() && current_->IsArray(); }

bool Config::is_dict() const { return exists() && current_->IsObject(); }

bool Config::get_bool() const {
  if (!exists()) {
    fprintf(stderr, "error: %s does not exist\n", path_.c_str());
    assert(false);
    return false;
  }
  if (!is_bool()) {
    fprintf(stderr, "error: %s is not a boolean value\n", path_.c_str());
    assert(false);
    return false;
  }
  return current_->GetBool();
}

int64_t Config::get_int64() const {
  if (!exists()) {
    fprintf(stderr, "error: %s does not exist\n", path_.c_str());
    assert(false);
    return 0;
  }
  if (!is_int64()) {
    fprintf(stderr, "error: %s is not an Int64 number\n", path_.c_str());
    assert(false);
    return 0;
  }
  return current_->GetInt64();
}

uint64_t Config::get_uint64() const {
  if (!exists()) {
    fprintf(stderr, "error: %s does not exist\n", path_.c_str());
    assert(false);
    return 0;
  }
  if (!is_uint64()) {
    fprintf(stderr, "error: %s is not an Uint64 number\n", path_.c_str());
    assert(false);
    return 0;
  }
  return current_->GetUint64();
}

double Config::get_double() const {
  if (!exists()) {
    fprintf(stderr, "error: %s does not exist\n", path_.c_str());
    assert(false);
    return 0.;
  }
  if (!is_double()) {
    fprintf(stderr, "error: %s is not a floating point number\n",
            path_.c_str());
    assert(false);
    return 0.;
  }
  return current_->GetDouble();
}

std::string Config::get_str() const {
  if (!exists()) {
    fprintf(stderr, "error: %s does not exist\n", path_.c_str());
    assert(false);
    return "";
  }
  if (!is_str()) {
    fprintf(stderr, "error: %s is not a string\n", path_.c_str());
    assert(false);
    return "";
  }
  return std::string(current_->GetString(), current_->GetStringLength());
}

bool Config::get_bool(bool default_v) const {
  if (exists())
    return get_bool();
  else
    return default_v;
}

int64_t Config::get_int64(int64_t default_v) const {
  if (exists())
    return get_int64();
  else
    return default_v;
}

uint64_t Config::get_uint64(uint64_t default_v) const {
  if (exists())
    return get_uint64();
  else
    return default_v;
}

double Config::get_double(double default_v) const {
  if (exists())
    return get_double();
  else
    return default_v;
}

std::string Config::get_str(const std::string& default_v) const {
  if (exists())
    return get_str();
  else
    return default_v;
}

size_t Config::size() const {
  assert(is_array());
  return current_->Size();
}

Config Config::get(size_t index) {
  assert(is_array());

  std::ostringstream oss;
  oss << path_ << '[' << index << ']';

  if (index >= current_->Size())
    return Config(root_, nullptr, oss.str());
  else
    return Config(root_, &((*current_)[static_cast<unsigned int>(index)]),
                  oss.str());
}

const Config Config::get(size_t index) const {
  return const_cast<Config*>(this)->get(index);
}

std::vector<std::string> Config::keys() const {
  assert(is_dict());

  std::vector<std::string> keys;
  for (rapidjson::Value::ConstMemberIterator it = current_->MemberBegin();
       it != current_->MemberEnd(); ++it) {
    keys.emplace_back(it->name.GetString(), it->name.GetStringLength());
  }
  return keys;
}

Config Config::get(std::string key) {
  assert(is_dict());

  std::ostringstream oss;
  oss << path_ << "[\"" << key << "\"]";

  if (!exists()) return Config(root_, nullptr, oss.str());

  rapidjson::Value::MemberIterator it = current_->FindMember(key.c_str());
  if (it == current_->MemberEnd()) return Config(root_, nullptr, oss.str());

  return Config(root_, &it->value, oss.str());
}

const Config Config::get(std::string key) const {
  return const_cast<Config*>(this)->get(key);
}

Config& Config::push_back_bool(bool v) {
  assert(is_array());
  current_->PushBack(v, root_->GetAllocator());
  return *this;
}

Config& Config::push_back_int64(int64_t v) {
  assert(is_array());
  current_->PushBack(v, root_->GetAllocator());
  return *this;
}

Config& Config::push_back_uint64(uint64_t v) {
  assert(is_array());
  current_->PushBack(v, root_->GetAllocator());
  return *this;
}

Config& Config::push_back_double(double v) {
  assert(is_array());
  current_->PushBack(v, root_->GetAllocator());
  return *this;
}

Config& Config::push_back_array(const Config& v) {
  assert(is_array());
  assert(v.is_array());
  current_->PushBack(*v.current_, root_->GetAllocator());
  return *this;
}

Config& Config::push_back_dict(const Config& v) {
  assert(is_array());
  assert(v.is_dict());
  current_->PushBack(*v.current_, root_->GetAllocator());
  return *this;
}

Config& Config::insert_bool(std::string key, bool v) {
  assert(is_dict());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, v, root_->GetAllocator());
  return *this;
}

Config& Config::insert_int64(std::string key, int64_t v) {
  assert(is_dict());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, v, root_->GetAllocator());
  return *this;
}

Config& Config::insert_uint64(std::string key, uint64_t v) {
  assert(is_dict());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, v, root_->GetAllocator());
  return *this;
}

Config& Config::insert_double(std::string key, double v) {
  assert(is_dict());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, v, root_->GetAllocator());
  return *this;
}

Config& Config::insert_array(std::string key, const Config& v) {
  assert(is_dict());
  assert(v.is_array());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, *v.current_, root_->GetAllocator());
  return *this;
}

Config& Config::insert_dict(std::string key, const Config& v) {
  assert(is_dict());
  assert(v.is_dict());
  rapidjson::Value v_key(key.c_str(), root_->GetAllocator());
  current_->AddMember(v_key, *v.current_, root_->GetAllocator());
  return *this;
}

Config::Config(const std::shared_ptr<rapidjson::Document>& root,
               rapidjson::Value* current, std::string path)
    : root_(root), current_(current), path_(path) {}
}
}

#endif
