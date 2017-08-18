## General notes
 * A replicated counter is implemented. In the common case, one client sends a
   request to the leader. When the request is committed, the leader sends the
   current value of the counter to the client.
 * The client's request consists of its global ID. This request is replicated
   in the shared log. A Raft server increments its counter in the `applylog()`
   callback.

## Notes to run
 * Wait for the leader (machine 0) to get elected before starting the client.
   Not doing so can cause some weird issues like segfaults. Leader change seems
   to work, but that's a secondary issue.

## Optimization notes
 * The replicated counter works best with the following options:
   * Raft commit `9623f2f` from `anujkaliaiid/raft`
   * ERpc datapath checks are disabled
   * ERpc session request window is set to 1
   * IB/RoCE transport inline size is set to 120. This disallows the use of the
     modded driver that supports only 60-byte inline size.
