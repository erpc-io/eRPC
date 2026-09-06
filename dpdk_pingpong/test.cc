// No EAL, hugepages, root privileges, or NIC access needed.
#define main pingpong_application_main
#include "main.cc"
#undef main
int main() {
  Port a{0}, b{1};
  a.mac.addr_bytes[5] = 1; b.mac.addr_bytes[5] = 2;
  for (unsigned size : {8u, 64u, 128u, 256u, 512u, 1024u}) {
    auto bytes = frame(a, b, size);
    check(bytes.size() == size + 56, "Wrong frame size");
    auto *ip = reinterpret_cast<rte_ipv4_hdr *>(bytes.data() + 14);
    check(rte_ipv4_cksum(ip) == 0, "Invalid IPv4 checksum");
    check(rte_be_to_cpu_16(ip->total_length) == bytes.size() - 14, "Wrong IP length");
    uint64_t sent = 0x123456789abcdefULL, got = 0;
    memcpy(bytes.data() + 46, &sent, sizeof(sent));
    for (unsigned prefix : {0u, 16u, 32u}) {
      std::vector<uint8_t> storage(128 + prefix + bytes.size());
      memcpy(storage.data() + 128 + prefix, bytes.data(), bytes.size());
      rte_mbuf m{};
      m.buf_addr = storage.data(); m.data_off = 128 + prefix;
      m.data_len = bytes.size(); m.pkt_len = bytes.size(); m.nb_segs = 1;
      check(decode(&m, b.mac, bytes.size(), got) && got == sent, "Prefix/sequence decode failed");
      check(!decode(&m, a.mac, bytes.size(), got), "Wrong destination accepted");
      check(!decode(&m, b.mac, bytes.size()+1, got), "Wrong length accepted");
      m.data_len = 10;
      check(!decode(&m, b.mac, bytes.size(), got), "Truncated packet accepted");
    }
  }
  for (const char *bad : {"-1", "64x", "", "1025"}) {
    bool rejected = false;
    try { number(bad, 64, 1024); } catch (const std::exception &) { rejected = true; }
    check(rejected, "Bad argument accepted");
  }
  printf("PASS: frame lengths, IP checksum, sequence matching, RX prefixes, invalid packets/arguments\n");
}
