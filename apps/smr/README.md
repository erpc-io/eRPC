## General notes

## Implementation notes
 * `willemt/raft` must be installed at the system-level for this application.
   See the helper script raft-install.sh`.
 * A replicated key-value store is implemented with the following constraints:
   * Only PUTs are supported
   * Only one client is allowed
   * Log compaction and cluster configuration change is not implemented
   * Leader failure is allowed, but the RPCs sent during leader election must
     not exceed the maximum eRPC request size. (During leader election,
     Raft servers exchange their logs via AppendEntries RPCs.) In practice,
     since log compaction is not implemented, this means that the leader must be
     killed soon after the experiment is started. This limitation can be removed
     in two ways:
      * Use multiple eRPC requests to transmit one appendentries request.
      * Increase eRPC's maximum message size. This requires straightforward
        changes to packet header bits and the hugepage allocator.
 * Wait for the leader (machine 0) to get elected before starting the client.
   Not doing so can cause some weird issues like segfaults. Leader change seems
   to work, but that's not terribly important to our evaluation.

## Optimization notes
 * The replicated counter works best with the following options:
   * All machines are under the same switch
   * eRPC session request window is set to 1
   * IB/RoCE transport inline size is set to 120. This disallows the use of the
     modded driver that supports only 60-byte inline size.

## Code/design notes
 * In the common case:
   * The client sends a request containing a key-value item to its leader view
   * The leader processes one client request at a time. It sends `AppendEntries`
     RPCs to the followers, and busy-waits until the Raft entry is committed.
   * When the Raft library decides that an entry is committed, the `applylog()`
     Raft callback is invoked. We insert the key-value item here.
   * When the server's busy wait ends, it calls `raft_apply_all()` to ensure
     that all committed log entries have been applied to the state machine.
     Then it sends a response to the client.
 * Client actions on leader failure:
   * The client detects leader failure when it receives a callback with failure.
     It tries other Raft servers one-by-one until it finds a leader.
   * Raft servers that are not the leader reply to client by redirecting it to
     the leader. If a Raft server does not know the leader, it directs the
     the client to try again.

## A note about replicated counter
 * An earlier version of this code implemented a replicated counter, not a
   key-value store. The following is noteworthy for the counter application.
 * The replicated log contains client IDs, not counter values. This follows the
   traditional state machine replication model, where client-proposed requests
   are replicated in the log. Using counter values as log entries is difficult
   or impossible, as outlined below:
   * Consider a leader initially with counter value 0. It receives a client
     request, assigns it a counter 0, and starts replicating the Raft entry
     `counter = 0`. This entry gets stored at all followers's logs, but the
     leader crashes before it can send commit messages.
   * The followers chose a new leader. Since `counter = 0` is committed, it
     exists in the new leader's log. However, willemt's Raft implementation
     (and possibly any other Raft implementation) does not guarantee that all
     cluster-wide committed entries are applied to the state machines before the
     new leader is activated.
      * Note that the new leader cannot be sure that `counter = 0` is committed
        just because it exists in its log: Although Raft guarantees that a
        leader's log contains all committed entries from prior terms, it may
        also contain uncommitted entries. So the new leader cannot apply
        `counter = 0` to its state machine even if `raft_apply_all()` is called.
   * The new leader receives a client request. Since its counter is currently 0,
     it also replicates `counter = 0`. Both `counter = 0` entries are eventually
     committed and applied, which is wrong.
