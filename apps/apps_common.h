/**
 * @file apps_common.h
 * @brief Common code for apps
 */
#ifndef APPS_COMMON_H
#define APPS_COMMON_H

#include <fstream>

namespace ERpc {

/// A utility class to write stats to /tmp/
class TmpStat {
 public:
  TmpStat() {}

  TmpStat(std::string stat_name) {
    output_file = std::ofstream(std::string("/tmp/") + stat_name);
  }

  ~TmpStat() {
    output_file.flush();
    output_file.close();
  }

  void write(double stat) {
    output_file << stat << std::endl;
  }

  void write(size_t stat) {
    output_file << stat << std::endl;
  }

 private:
  std::ofstream output_file;
};

}  // End ERpc

#endif  // APPS_COMMON_H
