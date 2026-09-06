#pragma once

#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include "../../third_party/HdrHistogram_c/src/hdr_histogram.h"

namespace erpc {
// Call only after measurement stops. Counts are noncumulative HDR bin counts.
inline void export_hdr(const std::string &path, const hdr_histogram *hist,
                       double units_per_us, const std::string &metadata) {
  if (path.empty()) return;
  if (hist->total_count <= 0 || units_per_us <= 0)
    throw std::runtime_error("Cannot export empty/invalid histogram");
  std::ofstream out(path);
  if (!out) throw std::runtime_error("Cannot open histogram: " + path);
  out << "# format=erpc_hdr_v1\n" << metadata
      << "# total_count=" << hist->total_count << "\n"
      << "# units_per_us=" << units_per_us << "\n"
      << "lower_us,upper_us,count\n" << std::setprecision(17);
  hdr_iter it;
  hdr_iter_recorded_init(&it, hist);
  int64_t total = 0;
  while (hdr_iter_next(&it)) {
    out << it.lowest_equivalent_value / units_per_us << ','
        << it.highest_equivalent_value / units_per_us << ',' << it.count << '\n';
    total += it.count;
  }
  out.close();
  if (!out || total != hist->total_count)
    throw std::runtime_error("Incomplete histogram export: " + path);
}
}  // namespace erpc
