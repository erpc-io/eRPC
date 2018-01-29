FAWN raw Ethernet cluster uses RoCE v1, so scripts are specialized for that.

* For some magical reason, `--gid-index=0` is needed.
* These scripts were tested with perftest version 5.6. Mellanox OFED 4.2 ships
  with perftest 4.2, with which nothing works at all.
