#pragma once

#include <fstream>
#include <string>
#include <vector>
#include "common.h"

namespace erpc {

// Tokenize the input string by the delimiter into a vector
static std::vector<std::string> split(std::string input, char delimiter) {
  std::vector<std::string> ret;
  std::stringstream ss(input);
  std::string token;

  while (getline(ss, token, delimiter)) ret.push_back(token);
  return ret;
}

/// Return the nth line from a file. n is zero-indexed.
static std::string get_line_n(std::string filename, size_t n) {
  std::ifstream in(filename.c_str());
  erpc::rt_assert(!in.fail(), "Required file " + filename + " not found");
  std::string s;

  for (size_t i = 0; i < n; i++) {
    std::getline(in, s);
    erpc::rt_assert(!s.empty(), "Insufficient lines in " + filename);
  }

  std::getline(in, s);
  erpc::rt_assert(!s.empty(), "Insufficient lines in " + filename);
  return s;
}

static bool is_valid_process_line(std::string line) {
  // The line must not have leading or trailing space
  if (line.at(0) == ' ' || line.at(line.length() - 1) == ' ') return false;

  std::vector<std::string> splits = split(line, ' ');

  return splits.size() == 3 && splits[0].length() > 0 &&
         splits[1].length() > 0 && splits[2].length() > 0;
}

static bool is_valid_uri(std::string uri) {
  std::vector<std::string> splits = split(uri, ':');
  return splits.size() == 2 && splits[0].length() > 0 && splits[1].length() > 0;
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

  std::vector<std::string> split_vec = split(line, ' ');
  return split_vec[0];
}

/// Return the SM UDP port of the process with index process_i, from the autorun
/// processes file. The autorun process file is formatted like so:
/// <DNS name> <UDP port> <NUMA>
static std::string get_udp_port_for_process(size_t process_i) {
  std::string process_file = "../eRPC/scripts/autorun_process_file";
  std::string line = get_line_n(process_file, process_i);

  std::vector<std::string> split_vec = split(line, ' ');
  return split_vec[1];
}

/// Return the URI of the process with index process_i
static std::string get_uri_for_process(size_t process_i) {
  std::string hostname = erpc::get_hostname_for_process(process_i);
  std::string udp_port_str = erpc::get_udp_port_for_process(process_i);
  return hostname + ":" + udp_port_str;
}

}  // namespace erpc
