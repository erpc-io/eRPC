/*

This source file is the amalgamated version of the original.
Please see github.com/willemt/raft for the original version.

HEAD commit: 9b34fef2fefa6c87e1a98b37b4d8b550ead6a0a6


Copyright (c) 2013, Willem-Hendrik Thiart
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * The names of its contributors may not be used to endorse or promote
      products derived from this software without specific prior written
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL WILLEM-HENDRIK THIART BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/


#ifndef RAFT_AMALGAMATION_SH
#define RAFT_AMALGAMATION_SH

/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @author Willem Thiart himself@willemthiart.com
 */

#ifndef RAFT_H_
#define RAFT_H_

#define RAFT_ERR_NOT_LEADER                  -2
#define RAFT_ERR_ONE_VOTING_CHANGE_ONLY      -3
#define RAFT_ERR_SHUTDOWN                    -4

#define RAFT_REQUESTVOTE_ERR_GRANTED          1
#define RAFT_REQUESTVOTE_ERR_NOT_GRANTED      0
#define RAFT_REQUESTVOTE_ERR_UNKNOWN_NODE    -1

typedef enum {
    RAFT_STATE_NONE,
    RAFT_STATE_FOLLOWER,
    RAFT_STATE_CANDIDATE,
    RAFT_STATE_LEADER
} raft_state_e;

typedef enum {
    RAFT_LOGTYPE_NORMAL,
    RAFT_LOGTYPE_ADD_NONVOTING_NODE,
    RAFT_LOGTYPE_ADD_NODE,
    RAFT_LOGTYPE_DEMOTE_NODE,
    RAFT_LOGTYPE_REMOVE_NODE,
    RAFT_LOGTYPE_NUM,
} raft_logtype_e;

typedef struct
{
    void *buf;

    unsigned int len;
} raft_entry_data_t;

/** Entry that is stored in the server's entry log. */
typedef struct
{
    /** the entry's term at the point it was created */
    unsigned int term;

    /** the entry's unique ID */
    unsigned int id;

    /** type of entry */
    int type;

    raft_entry_data_t data;
} raft_entry_t;

/** Message sent from client to server.
 * The client sends this message to a server with the intention of having it
 * applied to the FSM. */
typedef raft_entry_t msg_entry_t;

/** Entry message response.
 * Indicates to client if entry was committed or not. */
typedef struct
{
    /** the entry's unique ID */
    unsigned int id;

    /** the entry's term */
    int term;

    /** the entry's index */
    int idx;
} msg_entry_response_t;

/** Vote request message.
 * Sent to nodes when a server wants to become leader.
 * This message could force a leader/candidate to become a follower. */
typedef struct
{
    /** currentTerm, to force other leader/candidate to step down */
    int term;

    /** candidate requesting vote */
    int candidate_id;

    /** index of candidate's last log entry */
    int last_log_idx;

    /** term of candidate's last log entry */
    int last_log_term;
} msg_requestvote_t;

/** Vote request response message.
 * Indicates if node has accepted the server's vote request. */
typedef struct
{
    /** currentTerm, for candidate to update itself */
    int term;

    /** true means candidate received vote */
    int vote_granted;
} msg_requestvote_response_t;

/** Appendentries message.
 * This message is used to tell nodes if it's safe to apply entries to the FSM.
 * Can be sent without any entries as a keep alive message.
 * This message could force a leader/candidate to become a follower. */
typedef struct
{
    /** currentTerm, to force other leader/candidate to step down */
    int term;

    /** the index of the log just before the newest entry for the node who
     * receives this message */
    int prev_log_idx;

    /** the term of the log just before the newest entry for the node who
     * receives this message */
    int prev_log_term;

    /** the index of the entry that has been appended to the majority of the
     * cluster. Entries up to this index will be applied to the FSM */
    int leader_commit;

    /** number of entries within this message */
    int n_entries;

    /** array of entries within this message */
    msg_entry_t* entries;
} msg_appendentries_t;

/** Appendentries response message.
 * Can be sent without any entries as a keep alive message.
 * This message could force a leader/candidate to become a follower. */
typedef struct
{
    /** currentTerm, to force other leader/candidate to step down */
    int term;

    /** true if follower contained entry matching prevLogidx and prevLogTerm */
    int success;

    /* Non-Raft fields follow: */
    /* Having the following fields allows us to do less book keeping in
     * regards to full fledged RPC */

    /** This is the highest log IDX we've received and appended to our log */
    int current_idx;

    /** The first idx that we received within the appendentries message */
    int first_idx;
} msg_appendentries_response_t;

typedef void* raft_server_t;
typedef void* raft_node_t;

/** Callback for sending request vote messages.
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] node The node's ID that we are sending this message to
 * @param[in] msg The request vote message to be sent
 * @return 0 on success */
typedef int (
*func_send_requestvote_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_t* node,
    msg_requestvote_t* msg
    );

/** Callback for sending append entries messages.
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] node The node's ID that we are sending this message to
 * @param[in] msg The appendentries message to be sent
 * @return 0 on success */
typedef int (
*func_send_appendentries_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_t* node,
    msg_appendentries_t* msg
    );

/** Callback for detecting when non-voting nodes have obtained enough logs.
 * This triggers only when there are no pending configuration changes.
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] node The node
 * @return 0 does not want to be notified again; otherwise -1 */
typedef int (
*func_node_has_sufficient_logs_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_t* node
    );

#ifndef HAVE_FUNC_LOG
#define HAVE_FUNC_LOG
/** Callback for providing debug logging information.
 * This callback is optional
 * @param[in] raft The Raft server making this callback
 * @param[in] node The node that is the subject of this log. Could be NULL.
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] buf The buffer that was logged */
typedef void (
*func_log_f
)    (
    raft_server_t* raft,
    raft_node_t* node,
    void *user_data,
    const char *buf
    );
#endif

/** Callback for saving who we voted for to disk.
 * For safety reasons this callback MUST flush the change to disk.
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] voted_for The node we voted for
 * @return 0 on success */
typedef int (
*func_persist_int_f
)   (
    raft_server_t* raft,
    void *user_data,
    int node
    );

/** Callback for saving log entry changes.
 *
 * This callback is used for:
 * <ul>
 *      <li>Adding entries to the log (ie. offer)</li>
 *      <li>Removing the first entry from the log (ie. polling)</li>
 *      <li>Removing the last entry from the log (ie. popping)</li>
 *      <li>Applying entries</li>
 * </ul>
 *
 * For safety reasons this callback MUST flush the change to disk.
 *
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] entry The entry that the event is happening to.
 *    For offering, polling, and popping, the user is allowed to change the
 *    memory pointed to in the raft_entry_data_t struct. This MUST be done if
 *    the memory is temporary.
 * @param[in] entry_idx The entries index in the log
 * @return 0 on success */
typedef int (
*func_logentry_event_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_entry_t *entry,
    int entry_idx
    );

typedef struct
{
    /** Callback for sending request vote messages */
    func_send_requestvote_f send_requestvote;

    /** Callback for sending appendentries messages */
    func_send_appendentries_f send_appendentries;

    /** Callback for finite state machine application
     * Return 0 on success.
     * Return RAFT_ERR_SHUTDOWN if you want the server to shutdown. */
    func_logentry_event_f applylog;

    /** Callback for persisting vote data
     * For safety reasons this callback MUST flush the change to disk. */
    func_persist_int_f persist_vote;

    /** Callback for persisting term data
     * For safety reasons this callback MUST flush the change to disk. */
    func_persist_int_f persist_term;

    /** Callback for adding an entry to the log
     * For safety reasons this callback MUST flush the change to disk.
     * Return 0 on success.
     * Return RAFT_ERR_SHUTDOWN if you want the server to shutdown. */
    func_logentry_event_f log_offer;

    /** Callback for removing the oldest entry from the log
     * For safety reasons this callback MUST flush the change to disk.
     * @note If memory was malloc'd in log_offer then this should be the right
     *  time to free the memory. */
    func_logentry_event_f log_poll;

    /** Callback for removing the youngest entry from the log
     * For safety reasons this callback MUST flush the change to disk.
     * @note If memory was malloc'd in log_offer then this should be the right
     *  time to free the memory. */
    func_logentry_event_f log_pop;

    /** Callback for determining which node this configuration log entry
     * affects. This call only applies to configuration change log entries.
     * @return the node ID of the node */
    func_logentry_event_f log_get_node_id;

    /** Callback for detecting when a non-voting node has sufficient logs. */
    func_node_has_sufficient_logs_f node_has_sufficient_logs;

    /** Callback for catching debugging log messages
     * This callback is optional */
    func_log_f log;
} raft_cbs_t;

typedef struct
{
    /** User data pointer for addressing.
     * Examples of what this could be:
     * - void* pointing to implementor's networking data
     * - a (IP,Port) tuple */
    void* udata_address;
} raft_node_configuration_t;

/** Initialise a new Raft server.
 *
 * Request timeout defaults to 200 milliseconds
 * Election timeout defaults to 1000 milliseconds
 *
 * @return newly initialised Raft server */
raft_server_t* raft_new();

/** De-initialise Raft server.
 * Frees all memory */
void raft_free(raft_server_t* me);

/** De-initialise Raft server. */
void raft_clear(raft_server_t* me);

/** Set callbacks and user data.
 *
 * @param[in] funcs Callbacks
 * @param[in] user_data "User data" - user's context that's included in a callback */
void raft_set_callbacks(raft_server_t* me, raft_cbs_t* funcs, void* user_data);

/** Add node.
 *
 * If a voting node already exists the call will fail.
 *
 * @note The order this call is made is important.
 *  This call MUST be made in the same order as the other raft nodes.
 *  This is because the node ID is assigned depending on when this call is made
 *
 * @param[in] user_data The user data for the node.
 *  This is obtained using raft_node_get_udata.
 *  Examples of what this could be:
 *  - void* pointing to implementor's networking data
 *  - a (IP,Port) tuple
 * @param[in] id The integer ID of this node
 *  This is used for identifying clients across sessions.
 * @param[in] is_self Set to 1 if this "node" is this server
 * @return
 *  node if it was successfully added;
 *  NULL if the node already exists */
raft_node_t* raft_add_node(raft_server_t* me, void* user_data, int id, int is_self);

#define raft_add_peer raft_add_node

/** Add a node which does not participate in voting.
 * If a node already exists the call will fail.
 * Parameters are identical to raft_add_node
 * @return
 *  node if it was successfully added;
 *  NULL if the node already exists */
raft_node_t* raft_add_non_voting_node(raft_server_t* me_, void* udata, int id, int is_self);

/** Remove node.
 * @param node The node to be removed. */
void raft_remove_node(raft_server_t* me_, raft_node_t* node);

/** Set election timeout.
 * The amount of time that needs to elapse before we assume the leader is down
 * @param[in] msec Election timeout in milliseconds */
void raft_set_election_timeout(raft_server_t* me, int msec);

/** Set request timeout in milliseconds.
 * The amount of time before we resend an appendentries message
 * @param[in] msec Request timeout in milliseconds */
void raft_set_request_timeout(raft_server_t* me, int msec);

/** Process events that are dependent on time passing.
 * @param[in] msec_elapsed Time in milliseconds since the last call
 * @return
 *  0 on success;
 *  -1 on failure;
 *  RAFT_ERR_SHUTDOWN when server should be shutdown */
int raft_periodic(raft_server_t* me, int msec_elapsed);

/** Receive an appendentries message.
 *
 * Will block (ie. by syncing to disk) if we need to append a message.
 *
 * Might call malloc once to increase the log entry array size.
 *
 * The log_offer callback will be called.
 *
 * @note The memory pointer (ie. raft_entry_data_t) for each msg_entry_t is
 *   copied directly. If the memory is temporary you MUST either make the
 *   memory permanent (ie. via malloc) OR re-assign the memory within the
 *   log_offer callback.
 *
 * @param[in] node The node who sent us this message
 * @param[in] ae The appendentries message
 * @param[out] r The resulting response
 * @return 0 on success */
int raft_recv_appendentries(raft_server_t* me,
                            raft_node_t* node,
                            msg_appendentries_t* ae,
                            msg_appendentries_response_t *r);

/** Receive a response from an appendentries message we sent.
 * @param[in] node The node who sent us this message
 * @param[in] r The appendentries response message
 * @return 0 on success */
int raft_recv_appendentries_response(raft_server_t* me,
                                     raft_node_t* node,
                                     msg_appendentries_response_t* r);

/** Receive a requestvote message.
 * @param[in] node The node who sent us this message
 * @param[in] vr The requestvote message
 * @param[out] r The resulting response
 * @return 0 on success */
int raft_recv_requestvote(raft_server_t* me,
                          raft_node_t* node,
                          msg_requestvote_t* vr,
                          msg_requestvote_response_t *r);

/** Receive a response from a requestvote message we sent.
 * @param[in] node The node this response was sent by
 * @param[in] r The requestvote response message
 * @return
 *  0 on success;
 *  RAFT_ERR_SHUTDOWN server should be shutdown; */
int raft_recv_requestvote_response(raft_server_t* me,
                                   raft_node_t* node,
                                   msg_requestvote_response_t* r);

/** Receive an entry message from the client.
 *
 * Append the entry to the log and send appendentries to followers.
 *
 * Will block (ie. by syncing to disk) if we need to append a message.
 *
 * Might call malloc once to increase the log entry array size.
 *
 * The log_offer callback will be called.
 *
 * @note The memory pointer (ie. raft_entry_data_t) in msg_entry_t is
 *  copied directly. If the memory is temporary you MUST either make the
 *  memory permanent (ie. via malloc) OR re-assign the memory within the
 *  log_offer callback.
 *
 * Will fail:
 * <ul>
 *      <li>if the server is not the leader
 * </ul>
 *
 * @param[in] node The node who sent us this message
 * @param[in] ety The entry message
 * @param[out] r The resulting response
 * @return
 *  0 on success;
 *  RAFT_ERR_NOT_LEADER server is not the leader;
 *  RAFT_ERR_SHUTDOWN server should be shutdown;
 *  RAFT_ERR_ONE_VOTING_CHANGE_ONLY there is a non-voting change inflight;
 */
int raft_recv_entry(raft_server_t* me,
                    msg_entry_t* ety,
                    msg_entry_response_t *r);

/**
 * @return server's node ID; -1 if it doesn't know what it is */
int raft_get_nodeid(raft_server_t* me);

/**
 * @return the server's node */
raft_node_t* raft_get_my_node(raft_server_t *me_);

/**
 * @return currently configured election timeout in milliseconds */
int raft_get_election_timeout(raft_server_t* me);

/**
 * @return number of nodes that this server has */
int raft_get_num_nodes(raft_server_t* me);

/**
 * @return number of voting nodes that this server has */
int raft_get_num_voting_nodes(raft_server_t* me_);

/**
 * @return number of items within log */
int raft_get_log_count(raft_server_t* me);

/**
 * @return current term */
int raft_get_current_term(raft_server_t* me);

/**
 * @return current log index */
int raft_get_current_idx(raft_server_t* me);

/**
 * @return commit index */
int raft_get_commit_idx(raft_server_t* me_);

/**
 * @return 1 if follower; 0 otherwise */
int raft_is_follower(raft_server_t* me);

/**
 * @return 1 if leader; 0 otherwise */
int raft_is_leader(raft_server_t* me);

/**
 * @return 1 if candidate; 0 otherwise */
int raft_is_candidate(raft_server_t* me);

/**
 * @return currently elapsed timeout in milliseconds */
int raft_get_timeout_elapsed(raft_server_t* me);

/**
 * @return request timeout in milliseconds */
int raft_get_request_timeout(raft_server_t* me);

/**
 * @return index of last applied entry */
int raft_get_last_applied_idx(raft_server_t* me);

/**
 * @return the node's next index */
int raft_node_get_next_idx(raft_node_t* node);

/**
 * @return this node's user data */
int raft_node_get_match_idx(raft_node_t* me);

/**
 * @return this node's user data */
void* raft_node_get_udata(raft_node_t* me);

/**
 * Set this node's user data */
void raft_node_set_udata(raft_node_t* me, void* user_data);

/**
 * @param[in] idx The entry's index
 * @return entry from index */
raft_entry_t* raft_get_entry_from_idx(raft_server_t* me, int idx);

/**
 * @param[in] node The node's ID
 * @return node pointed to by node ID */
raft_node_t* raft_get_node(raft_server_t* me_, const int id);

/**
 * Used for iterating through nodes
 * @param[in] node The node's idx
 * @return node pointed to by node idx */
raft_node_t* raft_get_node_from_idx(raft_server_t* me_, const int idx);

/**
 * @return number of votes this server has received this election */
int raft_get_nvotes_for_me(raft_server_t* me);

/**
 * @return node ID of who I voted for */
int raft_get_voted_for(raft_server_t* me);

/** Get what this node thinks the node ID of the leader is.
 * @return node of what this node thinks is the valid leader;
 *   -1 if the leader is unknown */
int raft_get_current_leader(raft_server_t* me);

/** Get what this node thinks the node of the leader is.
 * @return node of what this node thinks is the valid leader;
 *   NULL if the leader is unknown */
raft_node_t* raft_get_current_leader_node(raft_server_t* me);

/**
 * @return callback user data */
void* raft_get_udata(raft_server_t* me);

/** Vote for a server.
 * This should be used to reload persistent state, ie. the voted-for field.
 * @param[in] node The server to vote for */
void raft_vote(raft_server_t* me_, raft_node_t* node);

/** Vote for a server.
 * This should be used to reload persistent state, ie. the voted-for field.
 * @param[in] nodeid The server to vote for by nodeid */
void raft_vote_for_nodeid(raft_server_t* me_, const int nodeid);

/** Set the current term.
 * This should be used to reload persistent state, ie. the current_term field.
 * @param[in] term The new current term */
void raft_set_current_term(raft_server_t* me, const int term);

/** Set the commit idx.
 * This should be used to reload persistent state, ie. the commit_idx field.
 * @param[in] commit_idx The new commit index. */
void raft_set_commit_idx(raft_server_t* me, int commit_idx);

/** Add an entry to the server's log.
 * This should be used to reload persistent state, ie. the commit log.
 * @param[in] ety The entry to be appended
 * @return
 *  0 on success;
 *  RAFT_ERR_SHUTDOWN server should shutdown */
int raft_append_entry(raft_server_t* me, raft_entry_t* ety);

/** Confirm if a msg_entry_response has been committed.
 * @param[in] r The response we want to check */
int raft_msg_entry_response_committed(raft_server_t* me_,
                                      const msg_entry_response_t* r);

/** Get node's ID.
 * @return ID of node */
int raft_node_get_id(raft_node_t* me_);

/** Tell if we are a leader, candidate or follower.
 * @return get state of type raft_state_e. */
int raft_get_state(raft_server_t* me_);

/** The the most recent log's term
 * @return the last log term */
int raft_get_last_log_term(raft_server_t* me_);

/** Turn a node into a voting node.
 * Voting nodes can take part in elections and in-regards to commiting entries,
 * are counted in majorities. */
void raft_node_set_voting(raft_node_t* node, int voting);

/** Tell if a node is a voting node or not.
 * @return 1 if this is a voting node. Otherwise 0. */
int raft_node_is_voting(raft_node_t* me_);

/** Apply all entries up to the commit index
 * @return
 *  0 on success;
 *  RAFT_ERR_SHUTDOWN when server should be shutdown */
int raft_apply_all(raft_server_t* me_);

/** Become leader
 * WARNING: this is a dangerous function call. It could lead to your cluster
 * losing it's consensus guarantees. */
void raft_become_leader(raft_server_t* me);

/** Determine if entry is voting configuration change.
 * @param[in] ety The entry to query.
 * @return 1 if this is a voting configuration change. */
int raft_entry_is_voting_cfg_change(raft_entry_t* ety);

/** Determine if entry is configuration change.
 * @param[in] ety The entry to query.
 * @return 1 if this is a configuration change. */
int raft_entry_is_cfg_change(raft_entry_t* ety);

#endif /* RAFT_H_ */
#ifndef RAFT_LOG_H_
#define RAFT_LOG_H_

typedef void* log_t;

log_t* log_new();

void log_set_callbacks(log_t* me_, raft_cbs_t* funcs, void* raft);

void log_free(log_t* me_);

void log_clear(log_t* me_);

/**
 * Add entry to log.
 * Don't add entry if we've already added this entry (based off ID)
 * Don't add entries with ID=0 
 * @return 0 if unsucessful; 1 otherwise */
int log_append_entry(log_t* me_, raft_entry_t* c);

/**
 * @return number of entries held within log */
int log_count(log_t* me_);

/**
 * Delete all logs from this log onwards */
void log_delete(log_t* me_, int idx);

/**
 * Empty the queue. */
void log_empty(log_t * me_);

/**
 * Remove oldest entry
 * @return oldest entry */
void *log_poll(log_t * me_);

raft_entry_t* log_get_from_idx(log_t* me_, int idx, int *n_etys);

raft_entry_t* log_get_at_idx(log_t* me_, int idx);

/**
 * @return youngest entry */
raft_entry_t *log_peektail(log_t * me_);

void log_delete(log_t* me_, int idx);

int log_get_current_idx(log_t* me_);

#endif /* RAFT_LOG_H_ */
#ifndef RAFT_PRIVATE_H_
#define RAFT_PRIVATE_H_

enum {
    RAFT_NODE_STATUS_DISCONNECTED,
    RAFT_NODE_STATUS_CONNECTED,
    RAFT_NODE_STATUS_CONNECTING,
    RAFT_NODE_STATUS_DISCONNECTING
};

/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file. 
 *
 * @file
 * @author Willem Thiart himself@willemthiart.com
 */

typedef struct {
    /* Persistent state: */

    /* the server's best guess of what the current term is
     * starts at zero */
    int current_term;

    /* The candidate the server voted for in its current term,
     * or Nil if it hasn't voted for any.  */
    int voted_for;

    /* the log which is replicated */
    void* log;

    /* Volatile state: */

    /* idx of highest log entry known to be committed */
    int commit_idx;

    /* idx of highest log entry applied to state machine */
    int last_applied_idx;

    /* follower/leader/candidate indicator */
    int state;

    /* amount of time left till timeout */
    int timeout_elapsed;

    raft_node_t* nodes;
    int num_nodes;

    int election_timeout;
    int request_timeout;

    /* what this node thinks is the node ID of the current leader, or -1 if
     * there isn't a known current leader. */
    raft_node_t* current_leader;

    /* callbacks */
    raft_cbs_t cb;
    void* udata;

    /* my node ID */
    raft_node_t* node;

    /* the log which has a voting cfg change, otherwise -1 */
    int voting_cfg_change_log_idx;

    /* our membership with the cluster is confirmed (ie. configuration log was
     * committed) */
    int connected;
} raft_server_private_t;

void raft_election_start(raft_server_t* me);

void raft_become_candidate(raft_server_t* me);

void raft_become_follower(raft_server_t* me);

void raft_vote(raft_server_t* me, raft_node_t* node);

void raft_set_current_term(raft_server_t* me,int term);

/**
 * @return 0 on error */
int raft_send_requestvote(raft_server_t* me, raft_node_t* node);

int raft_send_appendentries(raft_server_t* me, raft_node_t* node);

int raft_send_appendentries_all(raft_server_t* me_);

/**
 * Apply entry at lastApplied + 1. Entry becomes 'committed'.
 * @return 1 if entry committed, 0 otherwise */
int raft_apply_entry(raft_server_t* me_);

/**
 * Appends entry using the current term.
 * Note: we make the assumption that current term is up-to-date
 * @return 0 if unsuccessful */
int raft_append_entry(raft_server_t* me_, raft_entry_t* c);

void raft_set_last_applied_idx(raft_server_t* me, int idx);

void raft_set_state(raft_server_t* me_, int state);

int raft_get_state(raft_server_t* me_);

raft_node_t* raft_node_new(void* udata, int id);

void raft_node_set_next_idx(raft_node_t* node, int nextIdx);

void raft_node_set_match_idx(raft_node_t* node, int matchIdx);

int raft_node_get_match_idx(raft_node_t* me_);

void raft_node_vote_for_me(raft_node_t* me_, const int vote);

int raft_node_has_vote_for_me(raft_node_t* me_);

void raft_node_set_has_sufficient_logs(raft_node_t* me_);

int raft_node_has_sufficient_logs(raft_node_t* me_);

int raft_votes_is_majority(const int nnodes, const int nvotes);

void raft_pop_log(raft_server_t* me_, raft_entry_t* ety, const int idx);

void raft_offer_log(raft_server_t* me_, raft_entry_t* ety, const int idx);

#endif /* RAFT_PRIVATE_H_ */
/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @brief ADT for managing Raft log entries (aka entries)
 * @author Willem Thiart himself@willemthiart.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>





#define INITIAL_CAPACITY 10
#define in(x) ((log_private_t*)x)

typedef struct
{
    /* size of array */
    int size;

    /* the amount of elements in the array */
    int count;

    /* position of the queue */
    int front, back;

    /* we compact the log, and thus need to increment the Base Log Index */
    int base;

    raft_entry_t* entries;

    /* callbacks */
    raft_cbs_t *cb;
    void* raft;
} log_private_t;

static void __raft__ensurecapacity(log_private_t * me)
{
    int i, j;
    raft_entry_t *temp;

    if (me->count < me->size)
        return;

    temp = (raft_entry_t*)calloc(1, sizeof(raft_entry_t) * me->size * 2);

    for (i = 0, j = me->front; i < me->count; i++, j++)
    {
        if (j == me->size)
            j = 0;
        memcpy(&temp[i], &me->entries[j], sizeof(raft_entry_t));
    }

    /* clean up old entries */
    free(me->entries);

    me->size *= 2;
    me->entries = temp;
    me->front = 0;
    me->back = me->count;
}

log_t* log_new()
{
    log_private_t* me = (log_private_t*)calloc(1, sizeof(log_private_t));
    if (!me)
        return NULL;
    me->size = INITIAL_CAPACITY;
    me->count = 0;
    me->back = in(me)->front = 0;
    me->entries = (raft_entry_t*)calloc(1, sizeof(raft_entry_t) * me->size);
    return (log_t*)me;
}

void log_set_callbacks(log_t* me_, raft_cbs_t* funcs, void* raft)
{
    log_private_t* me = (log_private_t*)me_;

    me->raft = raft;
    me->cb = funcs;
}

void log_clear(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    me->count = 0;
    me->back = 0;
    me->front = 0;
    me->base = 0;
}

int log_append_entry(log_t* me_, raft_entry_t* c)
{
    log_private_t* me = (log_private_t*)me_;
    int e = 0;

    __raft__ensurecapacity(me);

    if (me->cb && me->cb->log_offer)
    {
        void* ud = raft_get_udata(me->raft);
        e = me->cb->log_offer(me->raft, ud, c, me->back);
        raft_offer_log(me->raft, c, me->back);
        if (e == RAFT_ERR_SHUTDOWN)
            return e;
    }

    memcpy(&me->entries[me->back], c, sizeof(raft_entry_t));
    me->count++;
    me->back++;

    return e;
}

raft_entry_t* log_get_from_idx(log_t* me_, int idx, int *n_etys)
{
    log_private_t* me = (log_private_t*)me_;
    int i;

    assert(0 <= idx - 1);

    if (me->base + me->count < idx || idx < me->base)
    {
        *n_etys = 0;
        return NULL;
    }

    /* idx starts at 1 */
    idx -= 1;

    i = (me->front + idx - me->base) % me->size;

    int logs_till_end_of_log;

    if (i < me->back)
        logs_till_end_of_log = me->back - i;
    else
        logs_till_end_of_log = me->size - i;

    *n_etys = logs_till_end_of_log;
    return &me->entries[i];
}

raft_entry_t* log_get_at_idx(log_t* me_, int idx)
{
    log_private_t* me = (log_private_t*)me_;
    int i;

    assert(0 <= idx - 1);

    if (me->base + me->count < idx || idx < me->base)
        return NULL;

    /* idx starts at 1 */
    idx -= 1;

    i = (me->front + idx - me->base) % me->size;
    return &me->entries[i];

}

int log_count(log_t* me_)
{
    return ((log_private_t*)me_)->count;
}

void log_delete(log_t* me_, int idx)
{
    log_private_t* me = (log_private_t*)me_;
    int end;

    /* idx starts at 1 */
    idx -= 1;
    idx -= me->base;

    for (end = log_count(me_); idx < end; idx++)
    {
        if (me->cb && me->cb->log_pop)
            me->cb->log_pop(me->raft, raft_get_udata(me->raft),
                            &me->entries[me->back - 1], me->back);
        raft_pop_log(me->raft, &me->entries[me->back - 1], me->back);
        me->back--;
        me->count--;
    }
}

void *log_poll(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == log_count(me_))
        return NULL;

    const void *elem = &me->entries[me->front];
    if (me->cb && me->cb->log_poll)
        me->cb->log_poll(me->raft, raft_get_udata(me->raft),
                         &me->entries[me->front], me->front);
    me->front++;
    me->count--;
    me->base++;
    return (void*)elem;
}

raft_entry_t *log_peektail(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == log_count(me_))
        return NULL;

    if (0 == me->back)
        return &me->entries[me->size - 1];
    else
        return &me->entries[me->back - 1];
}

void log_empty(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    me->front = 0;
    me->back = 0;
    me->count = 0;
}

void log_free(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    free(me->entries);
    free(me);
}

int log_get_current_idx(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    return log_count(me_) + me->base;
}
/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @brief Representation of a peer
 * @author Willem Thiart himself@willemthiart.com
 * @version 0.1
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>



#define RAFT_NODE_VOTED_FOR_ME       1
#define RAFT_NODE_VOTING             (1 << 1)
#define RAFT_NODE_HAS_SUFFICIENT_LOG (1 << 2)

typedef struct
{
    void* udata;

    int next_idx;
    int match_idx;

    int flags;

    int id;
} raft_node_private_t;

raft_node_t* raft_node_new(void* udata, int id)
{
    raft_node_private_t* me;
    me = (raft_node_private_t*)calloc(1, sizeof(raft_node_private_t));
    if (!me)
        return NULL;
    me->udata = udata;
    me->next_idx = 1;
    me->match_idx = 0;
    me->id = id;
    me->flags = RAFT_NODE_VOTING;
    return (raft_node_t*)me;
}

int raft_node_get_next_idx(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->next_idx;
}

void raft_node_set_next_idx(raft_node_t* me_, int nextIdx)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    /* log index begins at 1 */
    me->next_idx = nextIdx < 1 ? 1 : nextIdx;
}

int raft_node_get_match_idx(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->match_idx;
}

void raft_node_set_match_idx(raft_node_t* me_, int matchIdx)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    me->match_idx = matchIdx;
}

void* raft_node_get_udata(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->udata;
}

void raft_node_set_udata(raft_node_t* me_, void* udata)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    me->udata = udata;
}

void raft_node_vote_for_me(raft_node_t* me_, const int vote)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    if (vote)
        me->flags |= RAFT_NODE_VOTED_FOR_ME;
    else
        me->flags &= ~RAFT_NODE_VOTED_FOR_ME;
}

int raft_node_has_vote_for_me(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_VOTED_FOR_ME) != 0;
}

void raft_node_set_voting(raft_node_t* me_, int voting)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    if (voting)
        me->flags |= RAFT_NODE_VOTING;
    else
        me->flags &= ~RAFT_NODE_VOTING;
}

int raft_node_is_voting(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_VOTING) != 0;
}

void raft_node_set_has_sufficient_logs(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    me->flags |= RAFT_NODE_HAS_SUFFICIENT_LOG;
}

int raft_node_has_sufficient_logs(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_HAS_SUFFICIENT_LOG) != 0;
}

int raft_node_get_id(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->id;
}
/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @brief Implementation of a Raft server
 * @author Willem Thiart himself@willemthiart.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* for varags */
#include <stdarg.h>





#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) < (b) ? (b) : (a))
#endif

static void __raft__log(raft_server_t *me_, raft_node_t* node, const char *fmt, ...)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    char buf[1024];
    va_list args;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);

    if (me->cb.log)
        me->cb.log(me_, node, me->udata, buf);
}

raft_server_t* raft_new()
{
    raft_server_private_t* me =
        (raft_server_private_t*)calloc(1, sizeof(raft_server_private_t));
    if (!me)
        return NULL;
    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    me->request_timeout = 200;
    me->election_timeout = 1000;
    me->log = log_new();
    me->voting_cfg_change_log_idx = -1;
    raft_set_state((raft_server_t*)me, RAFT_STATE_FOLLOWER);
    me->current_leader = NULL;
    return (raft_server_t*)me;
}

void raft_set_callbacks(raft_server_t* me_, raft_cbs_t* funcs, void* udata)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    memcpy(&me->cb, funcs, sizeof(raft_cbs_t));
    me->udata = udata;
    log_set_callbacks(me->log, &me->cb, me_);
}

void raft_free(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    log_free(me->log);
    free(me_);
}

void raft_clear(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    me->voting_cfg_change_log_idx = -1;
    raft_set_state((raft_server_t*)me, RAFT_STATE_FOLLOWER);
    me->current_leader = NULL;
    me->commit_idx = 0;
    me->last_applied_idx = 0;
    me->num_nodes = 0;
    me->node = NULL;
    me->voting_cfg_change_log_idx = 0;
    log_clear(me->log);
}

void raft_delete_entry_from_idx(raft_server_t* me_, int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    assert(me->commit_idx < idx);

    if (idx <= me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    log_delete(me->log, idx);
}

void raft_election_start(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    __raft__log(me_, NULL, "election starting: %d %d, term: %d ci: %d",
          me->election_timeout, me->timeout_elapsed, me->current_term,
          raft_get_current_idx(me_));

    raft_become_candidate(me_);
}

void raft_become_leader(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    __raft__log(me_, NULL, "becoming leader term:%d", raft_get_current_term(me_));

    raft_set_state(me_, RAFT_STATE_LEADER);
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->node == me->nodes[i])
            continue;

        raft_node_t* node = me->nodes[i];
        raft_node_set_next_idx(node, raft_get_current_idx(me_) + 1);
        raft_node_set_match_idx(node, 0);
        raft_send_appendentries(me_, node);
    }
}

void raft_become_candidate(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    __raft__log(me_, NULL, "becoming candidate");

    raft_set_current_term(me_, raft_get_current_term(me_) + 1);
    for (i = 0; i < me->num_nodes; i++)
        raft_node_vote_for_me(me->nodes[i], 0);
    raft_vote(me_, me->node);
    me->current_leader = NULL;
    raft_set_state(me_, RAFT_STATE_CANDIDATE);

    /* We need a random factor here to prevent simultaneous candidates.
     * If the randomness is always positive it's possible that a fast node
     * would deadlock the cluster by always gaining a headstart. To prevent
     * this, we allow a negative randomness as a potential handicap. */
    me->timeout_elapsed = me->election_timeout - 2 * (rand() % me->election_timeout);

    for (i = 0; i < me->num_nodes; i++)
        if (me->node != me->nodes[i] && raft_node_is_voting(me->nodes[i]))
            raft_send_requestvote(me_, me->nodes[i]);
}

void raft_become_follower(raft_server_t* me_)
{
    __raft__log(me_, NULL, "becoming follower");
    raft_set_state(me_, RAFT_STATE_FOLLOWER);
}

int raft_periodic(raft_server_t* me_, int msec_since_last_period)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    me->timeout_elapsed += msec_since_last_period;

    /* Only one voting node means it's safe for us to become the leader */
    if (1 == raft_get_num_voting_nodes(me_) &&
        raft_node_is_voting(raft_get_my_node((void*)me)) &&
        !raft_is_leader(me_))
        raft_become_leader(me_);

    if (me->state == RAFT_STATE_LEADER)
    {
        if (me->request_timeout <= me->timeout_elapsed)
            raft_send_appendentries_all(me_);
    }
    else if (me->election_timeout <= me->timeout_elapsed)
    {
        if (1 < raft_get_num_voting_nodes(me_) &&
            raft_node_is_voting(raft_get_my_node(me_)))
            raft_election_start(me_);
    }

    if (me->last_applied_idx < me->commit_idx)
        return raft_apply_entry(me_);

    return 0;
}

raft_entry_t* raft_get_entry_from_idx(raft_server_t* me_, int etyidx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_get_at_idx(me->log, etyidx);
}

int raft_voting_change_is_in_progress(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->voting_cfg_change_log_idx != -1;
}

int raft_recv_appendentries_response(raft_server_t* me_,
                                     raft_node_t* node,
                                     msg_appendentries_response_t* r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    __raft__log(me_, node,
          "received appendentries response %s ci:%d rci:%d 1stidx:%d",
          r->success == 1 ? "SUCCESS" : "fail",
          raft_get_current_idx(me_),
          r->current_idx,
          r->first_idx);

    if (!node)
        return -1;

    /* Stale response -- ignore */
    if (r->current_idx != 0 && r->current_idx <= raft_node_get_match_idx(node))
        return 0;

    if (!raft_is_leader(me_))
        return -1;

    /* If response contains term T > currentTerm: set currentTerm = T
       and convert to follower (§5.3) */
    if (me->current_term < r->term)
    {
        raft_set_current_term(me_, r->term);
        raft_become_follower(me_);
        return 0;
    }
    else if (me->current_term != r->term)
        return 0;

    if (0 == r->success)
    {
        /* If AppendEntries fails because of log inconsistency:
           decrement nextIndex and retry (§5.3) */
        int next_idx = raft_node_get_next_idx(node);
        assert(0 <= next_idx);
        if (r->current_idx < next_idx - 1)
            raft_node_set_next_idx(node, min(r->current_idx + 1, raft_get_current_idx(me_)));
        else
            raft_node_set_next_idx(node, next_idx - 1);

        /* retry */
        raft_send_appendentries(me_, node);
        return 0;
    }

    assert(r->current_idx <= raft_get_current_idx(me_));

    raft_node_set_next_idx(node, r->current_idx + 1);
    raft_node_set_match_idx(node, r->current_idx);

    if (!raft_node_is_voting(node) &&
        !raft_voting_change_is_in_progress(me_) &&
        raft_get_current_idx(me_) <= r->current_idx + 1 &&
        me->cb.node_has_sufficient_logs &&
        0 == raft_node_has_sufficient_logs(node)
        )
    {
        int e = me->cb.node_has_sufficient_logs(me_, me->udata, node);
        if (0 == e)
            raft_node_set_has_sufficient_logs(node);
    }

    /* Update commit idx */
    int point = r->current_idx;
    if (point)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me_, point);
        if (raft_get_commit_idx(me_) < point && ety->term == me->current_term)
        {
            int i, votes = 1;
            for (i = 0; i < me->num_nodes; i++)
            {
                if (me->node != me->nodes[i] &&
                    raft_node_is_voting(me->nodes[i]) &&
                    point <= raft_node_get_match_idx(me->nodes[i]))
                {
                    votes++;
                }
            }

            if (raft_get_num_voting_nodes(me_) / 2 < votes)
                raft_set_commit_idx(me_, point);
        }
    }

    /* Aggressively send remaining entries */
    if (raft_get_entry_from_idx(me_, raft_node_get_next_idx(node)))
        raft_send_appendentries(me_, node);

    /* periodic applies committed entries lazily */

    return 0;
}

int raft_recv_appendentries(
    raft_server_t* me_,
    raft_node_t* node,
    msg_appendentries_t* ae,
    msg_appendentries_response_t *r
    )
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    me->timeout_elapsed = 0;

    if (0 < ae->n_entries)
        __raft__log(me_, node, "recvd appendentries t:%d ci:%d lc:%d pli:%d plt:%d #%d",
              ae->term,
              raft_get_current_idx(me_),
              ae->leader_commit,
              ae->prev_log_idx,
              ae->prev_log_term,
              ae->n_entries);

    r->term = me->current_term;

    if (raft_is_candidate(me_) && me->current_term == ae->term)
    {
        raft_become_follower(me_);
    }
    else if (me->current_term < ae->term)
    {
        raft_set_current_term(me_, ae->term);
        r->term = ae->term;
        raft_become_follower(me_);
    }
    else if (ae->term < me->current_term)
    {
        /* 1. Reply false if term < currentTerm (§5.1) */
        __raft__log(me_, node, "AE term %d is less than current term %d",
              ae->term, me->current_term);
        goto fail_with_current_idx;
    }

    /* Not the first appendentries we've received */
    /* NOTE: the log starts at 1 */
    if (0 < ae->prev_log_idx)
    {
        raft_entry_t* e = raft_get_entry_from_idx(me_, ae->prev_log_idx);

        if (!e)
        {
            __raft__log(me_, node, "AE no log at prev_idx %d", ae->prev_log_idx);
            goto fail_with_current_idx;
        }

        /* 2. Reply false if log doesn't contain an entry at prevLogIndex
           whose term matches prevLogTerm (§5.3) */
        if (raft_get_current_idx(me_) < ae->prev_log_idx)
            goto fail_with_current_idx;

        if (e->term != ae->prev_log_term)
        {
            __raft__log(me_, node, "AE term doesn't match prev_term (ie. %d vs %d) ci:%d pli:%d",
                  e->term, ae->prev_log_term, raft_get_current_idx(me_), ae->prev_log_idx);
            /* Delete all the following log entries because they don't match */
            raft_delete_entry_from_idx(me_, ae->prev_log_idx);
            r->current_idx = ae->prev_log_idx - 1;
            goto fail;
        }
    }

    /* 3. If an existing entry conflicts with a new one (same index
       but different terms), delete the existing entry and all that
       follow it (§5.3) */
    if (0 < ae->prev_log_idx && ae->prev_log_idx + 1 < raft_get_current_idx(me_))
    {
        /* Heartbeats shouldn't cause logs to be deleted. Heartbeats might be 
         * sent before the leader received the last appendentries response */
        if (ae->n_entries != 0 &&
                /* this is an old out-of-order appendentry message */
                me->commit_idx < ae->prev_log_idx + 1)
            raft_delete_entry_from_idx(me_, ae->prev_log_idx + 1);
    }

    r->current_idx = ae->prev_log_idx;

    int i;
    for (i = 0; i < ae->n_entries; i++)
    {
        raft_entry_t* ety = &ae->entries[i];
        int ety_index = ae->prev_log_idx + 1 + i;
        raft_entry_t* existing_ety = raft_get_entry_from_idx(me_, ety_index);
        r->current_idx = ety_index;
        if (existing_ety && existing_ety->term != ety->term && me->commit_idx < ety_index)
        {
            raft_delete_entry_from_idx(me_, ety_index);
            break;
        }
        else if (!existing_ety)
            break;
    }

    /* Pick up remainder in case of mismatch or missing entry */
    for (; i < ae->n_entries; i++)
    {
        int e = raft_append_entry(me_, &ae->entries[i]);
        if (-1 == e)
            goto fail_with_current_idx;
        else if (RAFT_ERR_SHUTDOWN == e)
        {
            r->success = 0;
            r->first_idx = 0;
            return RAFT_ERR_SHUTDOWN;
        }
        r->current_idx = ae->prev_log_idx + 1 + i;
    }

    /* 4. If leaderCommit > commitIndex, set commitIndex =
        min(leaderCommit, index of most recent entry) */
    if (raft_get_commit_idx(me_) < ae->leader_commit)
    {
        int last_log_idx = max(raft_get_current_idx(me_), 1);
        raft_set_commit_idx(me_, min(last_log_idx, ae->leader_commit));
    }

    /* update current leader because we accepted appendentries from it */
    me->current_leader = node;

    r->success = 1;
    r->first_idx = ae->prev_log_idx + 1;
    return 0;

fail_with_current_idx:
    r->current_idx = raft_get_current_idx(me_);
fail:
    r->success = 0;
    r->first_idx = 0;
    return -1;
}

int raft_already_voted(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->voted_for != -1;
}

static int __raft__should_grant_vote(raft_server_private_t* me, msg_requestvote_t* vr)
{
    /* TODO: 4.2.3 Raft Dissertation:
     * if a server receives a RequestVote request within the minimum election
     * timeout of hearing from a current leader, it does not update its term or
     * grant its vote */

    if (!raft_node_is_voting(raft_get_my_node((void*)me)))
        return 0;

    if (vr->term < raft_get_current_term((void*)me))
        return 0;

    /* TODO: if voted for is candiate return 1 (if below checks pass) */
    if (raft_already_voted((void*)me))
        return 0;

    /* Below we check if log is more up-to-date... */

    int current_idx = raft_get_current_idx((void*)me);

    /* Our log is definitely not more up-to-date if it's empty! */
    if (0 == current_idx)
        return 1;

    raft_entry_t* e = raft_get_entry_from_idx((void*)me, current_idx);
    if (e->term < vr->last_log_term)
        return 1;

    if (vr->last_log_term == e->term && current_idx <= vr->last_log_idx)
        return 1;

    return 0;
}

int raft_recv_requestvote(raft_server_t* me_,
                          raft_node_t* node,
                          msg_requestvote_t* vr,
                          msg_requestvote_response_t *r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!node)
        node = raft_get_node(me_, vr->candidate_id);

    if (raft_get_current_term(me_) < vr->term)
    {
        raft_set_current_term(me_, vr->term);
        raft_become_follower(me_);
    }

    if (__raft__should_grant_vote(me, vr))
    {
        /* It shouldn't be possible for a leader or candidate to grant a vote
         * Both states would have voted for themselves */
        assert(!(raft_is_leader(me_) || raft_is_candidate(me_)));

        raft_vote_for_nodeid(me_, vr->candidate_id);
        r->vote_granted = 1;

        /* there must be in an election. */
        me->current_leader = NULL;

        me->timeout_elapsed = 0;
    }
    else
    {
        /* It's possible the candidate node has been removed from the cluster but
         * hasn't received the appendentries that confirms the removal. Therefore
         * the node is partitioned and still thinks its part of the cluster. It
         * will eventually send a requestvote. This is error response tells the
         * node that it might be removed. */
        if (!node)
        {
            r->vote_granted = RAFT_REQUESTVOTE_ERR_UNKNOWN_NODE;
            goto done;
        }
        else
            r->vote_granted = 0;
    }

done:
    __raft__log(me_, node, "node requested vote: %d replying: %s",
          node,
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown");

    r->term = raft_get_current_term(me_);
    return 0;
}

int raft_votes_is_majority(const int num_nodes, const int nvotes)
{
    if (num_nodes < nvotes)
        return 0;
    int half = num_nodes / 2;
    return half + 1 <= nvotes;
}

int raft_recv_requestvote_response(raft_server_t* me_,
                                   raft_node_t* node,
                                   msg_requestvote_response_t* r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    __raft__log(me_, node, "node responded to requestvote status: %s",
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown");

    if (!raft_is_candidate(me_))
    {
        return 0;
    }
    else if (raft_get_current_term(me_) < r->term)
    {
        raft_set_current_term(me_, r->term);
        raft_become_follower(me_);
        return 0;
    }
    else if (raft_get_current_term(me_) != r->term)
    {
        /* The node who voted for us would have obtained our term.
         * Therefore this is an old message we should ignore.
         * This happens if the network is pretty choppy. */
        return 0;
    }

    __raft__log(me_, node, "node responded to requestvote status:%s ct:%d rt:%d",
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown",
          me->current_term,
          r->term);

    switch (r->vote_granted)
    {
        case RAFT_REQUESTVOTE_ERR_GRANTED:
            if (node)
                raft_node_vote_for_me(node, 1);
            int votes = raft_get_nvotes_for_me(me_);
            if (raft_votes_is_majority(raft_get_num_voting_nodes(me_), votes))
                raft_become_leader(me_);
            break;

        case RAFT_REQUESTVOTE_ERR_NOT_GRANTED:
            break;

        case RAFT_REQUESTVOTE_ERR_UNKNOWN_NODE:
            if (raft_node_is_voting(raft_get_my_node(me_)) &&
                me->connected == RAFT_NODE_STATUS_DISCONNECTING)
                return RAFT_ERR_SHUTDOWN;
            break;

        default:
            assert(0);
    }

    return 0;
}

int raft_recv_entry(raft_server_t* me_,
                    msg_entry_t* e,
                    msg_entry_response_t *r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    /* Only one voting cfg change at a time */
    if (raft_entry_is_voting_cfg_change(e))
        if (raft_voting_change_is_in_progress(me_))
            return RAFT_ERR_ONE_VOTING_CHANGE_ONLY;

    if (!raft_is_leader(me_))
        return RAFT_ERR_NOT_LEADER;

    __raft__log(me_, NULL, "received entry t:%d id: %d idx: %d",
          me->current_term, e->id, raft_get_current_idx(me_) + 1);

    raft_entry_t ety;
    ety.term = me->current_term;
    ety.id = e->id;
    ety.type = e->type;
    memcpy(&ety.data, &e->data, sizeof(raft_entry_data_t));
    raft_append_entry(me_, &ety);
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->node == me->nodes[i] || !me->nodes[i] ||
            !raft_node_is_voting(me->nodes[i]))
            continue;

        /* Only send new entries.
         * Don't send the entry to peers who are behind, to prevent them from
         * becoming congested. */
        int next_idx = raft_node_get_next_idx(me->nodes[i]);
        if (next_idx == raft_get_current_idx(me_))
            raft_send_appendentries(me_, me->nodes[i]);
    }

    /* if we're the only node, we can consider the entry committed */
    if (1 == raft_get_num_voting_nodes(me_))
        raft_set_commit_idx(me_, raft_get_current_idx(me_));

    r->id = e->id;
    r->idx = raft_get_current_idx(me_);
    r->term = me->current_term;

    if (raft_entry_is_voting_cfg_change(e))
        me->voting_cfg_change_log_idx = raft_get_current_idx(me_);

    return 0;
}

int raft_send_requestvote(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    msg_requestvote_t rv;

    assert(node);
    assert(node != me->node);

    __raft__log(me_, node, "sending requestvote to: %d", node);

    rv.term = me->current_term;
    rv.last_log_idx = raft_get_current_idx(me_);
    rv.last_log_term = raft_get_last_log_term(me_);
    rv.candidate_id = raft_get_nodeid(me_);
    assert(me->cb.send_requestvote);
    return me->cb.send_requestvote(me_, me->udata, node, &rv);
}

int raft_append_entry(raft_server_t* me_, raft_entry_t* ety)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (raft_entry_is_voting_cfg_change(ety))
        me->voting_cfg_change_log_idx = raft_get_current_idx(me_);

    return log_append_entry(me->log, ety);
}

int raft_apply_entry(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    /* Don't apply after the commit_idx */
    if (me->last_applied_idx == me->commit_idx)
        return -1;

    int log_idx = me->last_applied_idx + 1;

    raft_entry_t* ety = raft_get_entry_from_idx(me_, log_idx);
    if (!ety)
        return -1;

    __raft__log(me_, NULL, "applying log: %d, id: %d size: %d",
          me->last_applied_idx, ety->id, ety->data.len);

    me->last_applied_idx++;
    assert(me->cb.applylog);
    int e = me->cb.applylog(me_, me->udata, ety, me->last_applied_idx - 1);
    if (RAFT_ERR_SHUTDOWN == e)
        return RAFT_ERR_SHUTDOWN;

    /* Membership Change: confirm connection with cluster */
    if (RAFT_LOGTYPE_ADD_NODE == ety->type)
    {
        int node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, log_idx);
        raft_node_set_has_sufficient_logs(raft_get_node(me_, node_id));
        if (node_id == raft_get_nodeid(me_))
            me->connected = RAFT_NODE_STATUS_CONNECTED;
    }

    /* voting cfg change is now complete */
    if (log_idx == me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    return 0;
}

raft_entry_t* raft_get_entries_from_idx(raft_server_t* me_, int idx, int* n_etys)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_get_from_idx(me->log, idx, n_etys);
}

int raft_send_appendentries(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    assert(node);
    assert(node != me->node);

    msg_appendentries_t ae = {};
    ae.term = me->current_term;
    ae.leader_commit = raft_get_commit_idx(me_);
    ae.prev_log_idx = 0;
    ae.prev_log_term = 0;

    int next_idx = raft_node_get_next_idx(node);

    ae.entries = raft_get_entries_from_idx(me_, next_idx, &ae.n_entries);

    /* previous log is the log just before the new logs */
    if (1 < next_idx)
    {
        raft_entry_t* prev_ety = raft_get_entry_from_idx(me_, next_idx - 1);
        ae.prev_log_idx = next_idx - 1;
        if (prev_ety)
            ae.prev_log_term = prev_ety->term;
    }

    __raft__log(me_, node, "sending appendentries node: ci:%d comi:%d t:%d lc:%d pli:%d plt:%d",
          raft_get_current_idx(me_),
          raft_get_commit_idx(me_),
          ae.term,
          ae.leader_commit,
          ae.prev_log_idx,
          ae.prev_log_term);

    assert(me->cb.send_appendentries);
    return me->cb.send_appendentries(me_, me->udata, node, &ae);
}

int raft_send_appendentries_all(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i, e;

    me->timeout_elapsed = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->node != me->nodes[i])
        {
            e = raft_send_appendentries(me_, me->nodes[i]);
            if (0 != e)
                return e;
        }
    }

    return 0;
}

raft_node_t* raft_add_node(raft_server_t* me_, void* udata, int id, int is_self)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    /* set to voting if node already exists */
    raft_node_t* node = raft_get_node(me_, id);
    if (node)
    {
        if (!raft_node_is_voting(node))
        {
            raft_node_set_voting(node, 1);
            return node;
        }
        else
            /* we shouldn't add a node twice */
            return NULL;
    }

    me->num_nodes++;
    me->nodes = (raft_node_t*)realloc(me->nodes, sizeof(void*) * me->num_nodes);
    me->nodes[me->num_nodes - 1] = raft_node_new(udata, id);
    assert(me->nodes[me->num_nodes - 1]);
    if (is_self)
        me->node = me->nodes[me->num_nodes - 1];

    return me->nodes[me->num_nodes - 1];
}

raft_node_t* raft_add_non_voting_node(raft_server_t* me_, void* udata, int id, int is_self)
{
    if (raft_get_node(me_, id))
        return NULL;

    raft_node_t* node = raft_add_node(me_, udata, id, is_self);
    if (!node)
        return NULL;

    raft_node_set_voting(node, 0);
    return node;
}

void raft_remove_node(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    raft_node_t* new_array, *new_nodes;
    new_array = (raft_node_t*)calloc((me->num_nodes - 1), sizeof(void*));
    new_nodes = new_array;

    int i, found = 0;
    for (i = 0; i<me->num_nodes; i++)
    {
        if (me->nodes[i] == node)
        {
            found = 1;
            continue;
        }
        *new_nodes = me->nodes[i];
        new_nodes++;
    }

    assert(found);

    me->num_nodes--;
    free(me->nodes);
    me->nodes = new_array;

    free(node);
}

int raft_get_nvotes_for_me(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i, votes;

    for (i = 0, votes = 0; i < me->num_nodes; i++)
        if (me->node != me->nodes[i] && raft_node_is_voting(me->nodes[i]))
            if (raft_node_has_vote_for_me(me->nodes[i]))
                votes += 1;

    if (me->voted_for == raft_get_nodeid(me_))
        votes += 1;

    return votes;
}

void raft_vote(raft_server_t* me_, raft_node_t* node)
{
    raft_vote_for_nodeid(me_, node ? raft_node_get_id(node) : -1);
}

void raft_vote_for_nodeid(raft_server_t* me_, const int nodeid)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    me->voted_for = nodeid;
    assert(me->cb.persist_vote);
    me->cb.persist_vote(me_, me->udata, nodeid);
}

int raft_msg_entry_response_committed(raft_server_t* me_,
                                      const msg_entry_response_t* r)
{
    raft_entry_t* ety = raft_get_entry_from_idx(me_, r->idx);
    if (!ety)
        return 0;

    /* entry from another leader has invalidated this entry message */
    if (r->term != ety->term)
        return -1;
    return r->idx <= raft_get_commit_idx(me_);
}

int raft_apply_all(raft_server_t* me_)
{
    while (raft_get_last_applied_idx(me_) < raft_get_commit_idx(me_))
    {
        int e = raft_apply_entry(me_);
        if (0 != e)
            return e;
    }

    return 0;
}

int raft_entry_is_voting_cfg_change(raft_entry_t* ety)
{
    return RAFT_LOGTYPE_ADD_NODE == ety->type ||
           RAFT_LOGTYPE_DEMOTE_NODE == ety->type;
}

int raft_entry_is_cfg_change(raft_entry_t* ety)
{
    return (
        RAFT_LOGTYPE_ADD_NODE == ety->type ||
        RAFT_LOGTYPE_ADD_NONVOTING_NODE == ety->type ||
        RAFT_LOGTYPE_DEMOTE_NODE == ety->type ||
        RAFT_LOGTYPE_REMOVE_NODE == ety->type);
}

void raft_offer_log(raft_server_t* me_, raft_entry_t* ety, const int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!raft_entry_is_cfg_change(ety))
        return;

    int node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, idx);
    raft_node_t* node = raft_get_node(me_, node_id);
    int is_self = node_id == raft_get_nodeid(me_);

    switch (ety->type)
    {
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            if (!is_self)
            {
                raft_node_t* node = raft_add_non_voting_node(me_, NULL, node_id, is_self);
                assert(node);
            }
            break;

        case RAFT_LOGTYPE_ADD_NODE:
            node = raft_add_node(me_, NULL, node_id, is_self);
            assert(node);
            assert(raft_node_is_voting(node));
            break;

        case RAFT_LOGTYPE_DEMOTE_NODE:
            raft_node_set_voting(node, 0);
            break;

        case RAFT_LOGTYPE_REMOVE_NODE:
            if (node)
                raft_remove_node(me_, node);
            break;

        default:
            assert(0);
    }
}

void raft_pop_log(raft_server_t* me_, raft_entry_t* ety, const int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!raft_entry_is_cfg_change(ety))
        return;

    int node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, idx);

    switch (ety->type)
    {
        case RAFT_LOGTYPE_DEMOTE_NODE:
            {
            raft_node_t* node = raft_get_node(me_, node_id);
            raft_node_set_voting(node, 1);
            }
            break;

        case RAFT_LOGTYPE_REMOVE_NODE:
            {
            int is_self = node_id == raft_get_nodeid(me_);
            raft_node_t* node = raft_add_non_voting_node(me_, NULL, node_id, is_self);
            assert(node);
            }
            break;

        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            {
            int is_self = node_id == raft_get_nodeid(me_);
            raft_node_t* node = raft_get_node(me_, node_id);
            raft_remove_node(me_, node);
            if (is_self)
                assert(0);
            }
            break;

        case RAFT_LOGTYPE_ADD_NODE:
            {
            raft_node_t* node = raft_get_node(me_, node_id);
            raft_node_set_voting(node, 0);
            }
            break;

        default:
            assert(0);
            break;
    }
}
/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @author Willem Thiart himself@willemthiart.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

/* for varags */
#include <stdarg.h>





void raft_set_election_timeout(raft_server_t* me_, int millisec)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->election_timeout = millisec;
}

void raft_set_request_timeout(raft_server_t* me_, int millisec)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->request_timeout = millisec;
}

int raft_get_nodeid(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (!me->node)
        return -1;
    return raft_node_get_id(me->node);
}

int raft_get_election_timeout(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->election_timeout;
}

int raft_get_request_timeout(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->request_timeout;
}

int raft_get_num_nodes(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->num_nodes;
}

int raft_get_num_voting_nodes(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i, num = 0;
    for (i = 0; i < me->num_nodes; i++)
        if (raft_node_is_voting(me->nodes[i]))
            num++;
    return num;
}

int raft_get_timeout_elapsed(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->timeout_elapsed;
}

int raft_get_log_count(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_count(me->log);
}

int raft_get_voted_for(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return me->voted_for;
}

void raft_set_current_term(raft_server_t* me_, const int term)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (me->current_term < term)
    {
        me->current_term = term;
        me->voted_for = -1;
        assert(me->cb.persist_term);
        me->cb.persist_term(me_, me->udata, term);
    }
}

int raft_get_current_term(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->current_term;
}

int raft_get_current_idx(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_get_current_idx(me->log);
}

void raft_set_commit_idx(raft_server_t* me_, int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    assert(me->commit_idx <= idx);
    assert(idx <= raft_get_current_idx(me_));
    me->commit_idx = idx;
}

void raft_set_last_applied_idx(raft_server_t* me_, int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->last_applied_idx = idx;
}

int raft_get_last_applied_idx(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->last_applied_idx;
}

int raft_get_commit_idx(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->commit_idx;
}

void raft_set_state(raft_server_t* me_, int state)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    /* if became the leader, then update the current leader entry */
    if (state == RAFT_STATE_LEADER)
        me->current_leader = me->node;
    me->state = state;
}

int raft_get_state(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->state;
}

raft_node_t* raft_get_node(raft_server_t *me_, int nodeid)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    for (i = 0; i < me->num_nodes; i++)
        if (nodeid == raft_node_get_id(me->nodes[i]))
            return me->nodes[i];

    return NULL;
}

raft_node_t* raft_get_my_node(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    for (i = 0; i < me->num_nodes; i++)
        if (raft_get_nodeid(me_) == raft_node_get_id(me->nodes[i]))
            return me->nodes[i];

    return NULL;
}

raft_node_t* raft_get_node_from_idx(raft_server_t* me_, const int idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return me->nodes[idx];
}

int raft_get_current_leader(raft_server_t* me_)
{
    raft_server_private_t* me = (void*)me_;
    if (me->current_leader)
        return raft_node_get_id(me->current_leader);
    return -1;
}

raft_node_t* raft_get_current_leader_node(raft_server_t* me_)
{
    raft_server_private_t* me = (void*)me_;
    return me->current_leader;
}

void* raft_get_udata(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->udata;
}

int raft_is_follower(raft_server_t* me_)
{
    return raft_get_state(me_) == RAFT_STATE_FOLLOWER;
}

int raft_is_leader(raft_server_t* me_)
{
    return raft_get_state(me_) == RAFT_STATE_LEADER;
}

int raft_is_candidate(raft_server_t* me_)
{
    return raft_get_state(me_) == RAFT_STATE_CANDIDATE;
}

int raft_get_last_log_term(raft_server_t* me_)
{
    int current_idx = raft_get_current_idx(me_);
    if (0 < current_idx)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me_, current_idx);
        if (ety)
            return ety->term;
    }
    return 0;
}

int raft_is_connected(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->connected;
}
#endif /* RAFT_AMALGAMATIONE_SH */
