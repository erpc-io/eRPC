#pragma once

#include <boost/algorithm/string.hpp>
#include <fstream>
#include <string>
#include <vector>
#include "common.h"

namespace erpc {

// Return the nth line from a file, with leading and trailing space trimmed
static std::string get_line_n(std::string filename, size_t n) {
  std::ifstream in(filename.c_str());

  std::string s;
  s.reserve(100);  // For performance

  for (size_t i = 0; i < n; i++) {
    std::getline(in, s);
    erpc::rt_assert(!s.empty(), "Insufficient lines in " + filename);
  }

  std::getline(in, s);
  erpc::rt_assert(!s.empty(), "Insufficient lines in " + filename);

  boost::algorithm::trim(s);
  return s;
}

static bool is_valid_process_line(std::string line) {
  std::vector<std::string> split;
  boost::split(split, line, boost::is_any_of(" "));

  return split.size() == 3 && split[0].length() > 0 && split[1].length() > 0 &&
         split[2].length() > 0;
}

static bool is_valid_uri(std::string uri) {
  std::vector<std::string> split;
  boost::split(split, uri, boost::is_any_of(":"));
  return split.size() == 2 && split[0].length() > 0 && split[1].length() > 0;
}

// Split a well-formed URI into its hostname and UDP port components
static void split_uri(const std::string &uri, std::string &hostname,
                      uint16_t &udp_port) {
  assert(is_valid_uri(uri));
  size_t colon_pos = uri.find(':');
  hostname = uri.substr(0, colon_pos /* = length of hostname */);
  udp_port =
      std::stoi(uri.substr(colon_pos + 1, std::string::npos /* till end */));
}

/// Extract just the hostname from a URI formatted as hostname:udp_port
static std::string extract_hostname_from_uri(std::string uri) {
  assert(is_valid_uri(uri));
  size_t colon_pos = uri.find(':');
  return uri.substr(0, colon_pos /* = length of hostname */);
}

/// Extract the UDP port from a URI formatted as hostname:udp_port
static uint16_t extract_udp_port_from_uri(std::string uri) {
  assert(is_valid_uri(uri));
  size_t colon_pos = uri.find(':');
  return std::stoi(uri.substr(colon_pos + 1, std::string::npos /* till end */));
}

/// Return the hostname of the process with index process_i, from the autorun
/// processes file. The autorun process file is formatted like so:
/// <DNS name> <UDP port> <NUMA>
static std::string get_hostname_for_process(size_t process_i) {
  std::string process_file = "../eRPC/scripts/autorun_process_file";
  std::string line = get_line_n(process_file, process_i);
  rt_assert(is_valid_process_line(line), "Invalid process line " + line);

  std::vector<std::string> split_vec;
  boost::split(split_vec, line, boost::is_any_of(" "));
  return split_vec[0];
}

/// Return the SM UDP port of the process with index process_i, from the autorun
/// processes file. The autorun process file is formatted like so:
/// <DNS name> <UDP port> <NUMA>
static std::string get_udp_port_for_process(size_t process_i) {
  std::string process_file = "../eRPC/scripts/autorun_process_file";
  std::string line = get_line_n(process_file, process_i);

  std::vector<std::string> split_vec;
  boost::split(split_vec, line, boost::is_any_of(" "));
  return split_vec[1];
}

/// Return the URI of the process with index process_i
static std::string get_uri_for_process(size_t process_i) {
  std::string hostname = erpc::get_hostname_for_process(process_i);
  std::string udp_port_str = erpc::get_udp_port_for_process(process_i);
  return hostname + ":" + udp_port_str;
}

}  // namespace erpc
