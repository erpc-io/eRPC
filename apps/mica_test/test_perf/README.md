# Notes
 * This is a benchmark to compare MICA's CRCW performance.

# FixedTable performance (CRCW mode)
 * Value-with-key bucket performance is recorded here because it is significantly
   better than values-at-end-of-keys performance. We might want to go to the
   old bucket layout later for better performance.
 * Value-with-key bucket (old bucket layout), 16-byte values, batch size = 4
   * 1 thread, 100% GET: 21.7 M/s
   * 1 thread, 50% GET: 21.0 M/s
   * 14 threads, 100% GET: 16.1 M/s
   * 14 thread, 50% GET: 12.7 M/s
 * Value-with-key bucket (old bucket layout), 40-byte values, batch size = 4
   * 1 thread, 100% GET: 15.4 M/s
   * 1 thread, 50% GET: 14.8 M/s
   * 14 threads, 100% GET: 10.4 M/s
   * 14 thread, 50% GET: 8.5 M/s
