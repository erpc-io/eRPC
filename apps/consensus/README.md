## General notes
 * A replicated counter is implemented. In the common case, one client sends a
   request to the leader. When the request is committed, the leader sends the
   current value of the counter to the client.
 * The client's request consists of its global ID. This request is replicated
   in the shared log. A Raft server increments its counter in the `applylog()`
   callback.
