/*

This source file is the amalgamated version of the original.
Please see github.com/willemt/raft for the original version.

HEAD commit: e428eeb921a014192d1d703dd317f3f29f5916c5


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


#ifndef RAFT_DEFS_H_
#define RAFT_DEFS_H_

/**
 * Unique entry ids are mostly used for debugging and nothing else,
 * so there is little harm if they collide.
 */
typedef int raft_entry_id_t;

/**
 * Monotonic term counter.
 */
typedef long int raft_term_t;

/**
 * Monotonic log entry index.
 *
 * This is also used to as an entry count size type.
 */
typedef long int raft_index_t;

/**
 * Unique node identifier.
 */
typedef int raft_node_id_t;

#endif  /* RAFT_DEFS_H_ */
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



typedef enum {
    RAFT_ERR_NOT_LEADER=-2,
    RAFT_ERR_ONE_VOTING_CHANGE_ONLY=-3,
    RAFT_ERR_SHUTDOWN=-4,
    RAFT_ERR_NOMEM=-5,
    RAFT_ERR_NEEDS_SNAPSHOT=-6,
    RAFT_ERR_SNAPSHOT_IN_PROGRESS=-7,
    RAFT_ERR_SNAPSHOT_ALREADY_LOADED=-8,
    RAFT_ERR_LAST=-100,
} raft_error_e;

typedef enum {
    RAFT_MEMBERSHIP_ADD,
    RAFT_MEMBERSHIP_REMOVE,
} raft_membership_e;

#define RAFT_REQUESTVOTE_ERR_GRANTED          1
#define RAFT_REQUESTVOTE_ERR_NOT_GRANTED      0
#define RAFT_REQUESTVOTE_ERR_UNKNOWN_NODE    -1

typedef enum {
    RAFT_STATE_NONE,
    RAFT_STATE_FOLLOWER,
    RAFT_STATE_CANDIDATE,
    RAFT_STATE_LEADER
} raft_state_e;

/** Allow entries to apply while taking a snapshot */
#define RAFT_SNAPSHOT_NONBLOCKING_APPLY     1

typedef enum {
    /**
     * Regular log type.
     * This is solely for application data intended for the FSM.
     */
    RAFT_LOGTYPE_NORMAL,
    /**
     * Membership change.
     * Non-voting nodes can't cast votes or start elections.
     * Nodes in this non-voting state are used to catch up with the cluster,
     * when trying to the join the cluster.
     */
    RAFT_LOGTYPE_ADD_NONVOTING_NODE,
    /**
     * Membership change.
     * Add a voting node.
     */
    RAFT_LOGTYPE_ADD_NODE,
    /**
     * Membership change.
     * Nodes become demoted when we want to remove them from the cluster.
     * Demoted nodes can't take part in voting or start elections.
     * Demoted nodes become inactive, as per raft_node_is_active.
     */
    RAFT_LOGTYPE_DEMOTE_NODE,
    /**
     * Membership change.
     * The node is removed from the cluster.
     * This happens after the node has been demoted.
     * Removing nodes is a 2 step process: first demote, then remove.
     */
    RAFT_LOGTYPE_REMOVE_NODE,
    /**
     * Users can piggyback the entry mechanism by specifying log types that
     * are higher than RAFT_LOGTYPE_NUM.
     */
    RAFT_LOGTYPE_NUM=100,
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
    raft_term_t term;

    /** the entry's unique ID */
    raft_entry_id_t id;

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
    raft_entry_id_t id;

    /** the entry's term */
    raft_term_t term;

    /** the entry's index */
    raft_index_t idx;
} msg_entry_response_t;

/** Vote request message.
 * Sent to nodes when a server wants to become leader.
 * This message could force a leader/candidate to become a follower. */
typedef struct
{
    /** currentTerm, to force other leader/candidate to step down */
    raft_term_t term;

    /** candidate requesting vote */
    raft_node_id_t candidate_id;

    /** index of candidate's last log entry */
    raft_index_t last_log_idx;

    /** term of candidate's last log entry */
    raft_term_t last_log_term;
} msg_requestvote_t;

/** Vote request response message.
 * Indicates if node has accepted the server's vote request. */
typedef struct
{
    /** currentTerm, for candidate to update itself */
    raft_term_t term;

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
    raft_term_t term;

    /** the index of the log just before the newest entry for the node who
     * receives this message */
    raft_index_t prev_log_idx;

    /** the term of the log just before the newest entry for the node who
     * receives this message */
    raft_term_t prev_log_term;

    /** the index of the entry that has been appended to the majority of the
     * cluster. Entries up to this index will be applied to the FSM */
    raft_index_t leader_commit;

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
    raft_term_t term;

    /** true if follower contained entry matching prevLogidx and prevLogTerm */
    int success;

    /* Non-Raft fields follow: */
    /* Having the following fields allows us to do less book keeping in
     * regards to full fledged RPC */

    /** If success, this is the highest log IDX we've received and appended to
     * our log; otherwise, this is the our currentIndex */
    raft_index_t current_idx;

    /** The first idx that we received within the appendentries message */
    raft_index_t first_idx;
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

/**
 * Log compaction
 * Callback for telling the user to send a snapshot.
 *
 * @param[in] raft Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] node Node's ID that needs a snapshot sent to
 **/
typedef int (
*func_send_snapshot_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_t* node
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
 * @param[in] vote The node we voted for
 * @return 0 on success */
typedef int (
*func_persist_vote_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_id_t vote
    );

/** Callback for saving current term (and nil vote) to disk.
 * For safety reasons this callback MUST flush the term and vote changes to
 * disk atomically.
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] term Current term
 * @param[in] vote The node value dictating we haven't voted for anybody
 * @return 0 on success */
typedef int (
*func_persist_term_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_term_t term,
    raft_node_id_t vote
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
    raft_index_t entry_idx
    );

/** Callback for being notified of membership changes.
 *
 * Implementing this callback is optional.
 *
 * Remove notification happens before the node is about to be removed.
 *
 * @param[in] raft The Raft server making this callback
 * @param[in] user_data User data that is passed from Raft server
 * @param[in] node The node that is the subject of this log. Could be NULL.
 * @param[in] entry The entry that was the trigger for the event. Could be NULL.
 * @param[in] type The type of membership change */
typedef void (
*func_membership_event_f
)   (
    raft_server_t* raft,
    void *user_data,
    raft_node_t *node,
    raft_entry_t *entry,
    raft_membership_e type
    );

typedef struct
{
    /** Callback for sending request vote messages */
    func_send_requestvote_f send_requestvote;

    /** Callback for sending appendentries messages */
    func_send_appendentries_f send_appendentries;

    /** Callback for notifying user that a node needs a snapshot sent */
    func_send_snapshot_f send_snapshot;

    /** Callback for finite state machine application
     * Return 0 on success.
     * Return RAFT_ERR_SHUTDOWN if you want the server to shutdown. */
    func_logentry_event_f applylog;

    /** Callback for persisting vote data
     * For safety reasons this callback MUST flush the change to disk. */
    func_persist_vote_f persist_vote;

    /** Callback for persisting term (and nil vote) data
     * For safety reasons this callback MUST flush the term and vote changes to
     * disk atomically. */
    func_persist_term_f persist_term;

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

    /** Callback called for every existing log entry when clearing the log.
     * If memory was malloc'd in log_offer and the entry doesn't get a chance
     * to go through log_poll or log_pop, this is the last chance to free it.
     */
    func_logentry_event_f log_clear;

    /** Callback for determining which node this configuration log entry
     * affects. This call only applies to configuration change log entries.
     * @return the node ID of the node */
    func_logentry_event_f log_get_node_id;

    /** Callback for detecting when a non-voting node has sufficient logs. */
    func_node_has_sufficient_logs_f node_has_sufficient_logs;

    func_membership_event_f notify_membership_event;

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
raft_server_t* raft_new(void);

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
raft_node_t* raft_add_node(raft_server_t* me, void* user_data, raft_node_id_t id, int is_self);

#define raft_add_peer raft_add_node

/** Add a node which does not participate in voting.
 * If a node already exists the call will fail.
 * Parameters are identical to raft_add_node
 * @return
 *  node if it was successfully added;
 *  NULL if the node already exists */
raft_node_t* raft_add_non_voting_node(raft_server_t* me_, void* udata, raft_node_id_t id, int is_self);

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
 *  RAFT_ERR_SHUTDOWN when server MUST shutdown */
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
 * @return
 *  0 on success
 *  RAFT_ERR_NEEDS_SNAPSHOT
 *  */
int raft_recv_appendentries(raft_server_t* me,
                            raft_node_t* node,
                            msg_appendentries_t* ae,
                            msg_appendentries_response_t *r);

/** Receive a response from an appendentries message we sent.
 * @param[in] node The node who sent us this message
 * @param[in] r The appendentries response message
 * @return
 *  0 on success;
 *  -1 on error;
 *  RAFT_ERR_NOT_LEADER server is not the leader */
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
 *  RAFT_ERR_SHUTDOWN server MUST shutdown; */
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
 *  RAFT_ERR_SHUTDOWN server MUST shutdown;
 *  RAFT_ERR_ONE_VOTING_CHANGE_ONLY there is a non-voting change inflight;
 *  RAFT_ERR_NOMEM memory allocation failure
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
raft_index_t raft_get_log_count(raft_server_t* me);

/**
 * @return current term */
raft_term_t raft_get_current_term(raft_server_t* me);

/**
 * @return current log index */
raft_index_t raft_get_current_idx(raft_server_t* me);

/**
 * @return commit index */
raft_index_t raft_get_commit_idx(raft_server_t* me_);

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
raft_index_t raft_get_last_applied_idx(raft_server_t* me);

/**
 * @return the node's next index */
raft_index_t raft_node_get_next_idx(raft_node_t* node);

/**
 * @return this node's user data */
raft_index_t raft_node_get_match_idx(raft_node_t* me);

/**
 * @return this node's user data */
void* raft_node_get_udata(raft_node_t* me);

/**
 * Set this node's user data */
void raft_node_set_udata(raft_node_t* me, void* user_data);

/**
 * @param[in] idx The entry's index
 * @return entry from index */
raft_entry_t* raft_get_entry_from_idx(raft_server_t* me, raft_index_t idx);

/**
 * @param[in] node The node's ID
 * @return node pointed to by node ID */
raft_node_t* raft_get_node(raft_server_t* me_, const raft_node_id_t id);

/**
 * Used for iterating through nodes
 * @param[in] node The node's idx
 * @return node pointed to by node idx */
raft_node_t* raft_get_node_from_idx(raft_server_t* me_, const raft_index_t idx);

/**
 * @return number of votes this server has received this election */
int raft_get_nvotes_for_me(raft_server_t* me);

/**
 * @return node ID of who I voted for */
int raft_get_voted_for(raft_server_t* me);

/** Get what this node thinks the node ID of the leader is.
 * @return node of what this node thinks is the valid leader;
 *   -1 if the leader is unknown */
raft_node_id_t raft_get_current_leader(raft_server_t* me);

/** Get what this node thinks the node of the leader is.
 * @return node of what this node thinks is the valid leader;
 *   NULL if the leader is unknown */
raft_node_t* raft_get_current_leader_node(raft_server_t* me);

/**
 * @return callback user data */
void* raft_get_udata(raft_server_t* me);

/** Vote for a server.
 * This should be used to reload persistent state, ie. the voted-for field.
 * @param[in] node The server to vote for
 * @return
 *  0 on success */
int raft_vote(raft_server_t* me_, raft_node_t* node);

/** Vote for a server.
 * This should be used to reload persistent state, ie. the voted-for field.
 * @param[in] nodeid The server to vote for by nodeid
 * @return
 *  0 on success */
int raft_vote_for_nodeid(raft_server_t* me_, const raft_node_id_t nodeid);

/** Set the current term.
 * This should be used to reload persistent state, ie. the current_term field.
 * @param[in] term The new current term
 * @return
 *  0 on success */
int raft_set_current_term(raft_server_t* me, const raft_term_t term);

/** Set the commit idx.
 * This should be used to reload persistent state, ie. the commit_idx field.
 * @param[in] commit_idx The new commit index. */
void raft_set_commit_idx(raft_server_t* me, raft_index_t commit_idx);

/** Add an entry to the server's log.
 * This should be used to reload persistent state, ie. the commit log.
 * @param[in] ety The entry to be appended
 * @return
 *  0 on success;
 *  RAFT_ERR_SHUTDOWN server should shutdown
 *  RAFT_ERR_NOMEM memory allocation failure */
int raft_append_entry(raft_server_t* me, raft_entry_t* ety);

/** Confirm if a msg_entry_response has been committed.
 * @param[in] r The response we want to check */
int raft_msg_entry_response_committed(raft_server_t* me_,
                                      const msg_entry_response_t* r);

/** Get node's ID.
 * @return ID of node */
raft_node_id_t raft_node_get_id(raft_node_t* me_);

/** Tell if we are a leader, candidate or follower.
 * @return get state of type raft_state_e. */
int raft_get_state(raft_server_t* me_);

/** Get the most recent log's term
 * @return the last log term */
raft_term_t raft_get_last_log_term(raft_server_t* me_);

/** Turn a node into a voting node.
 * Voting nodes can take part in elections and in-regards to committing entries,
 * are counted in majorities. */
void raft_node_set_voting(raft_node_t* node, int voting);

/** Tell if a node is a voting node or not.
 * @return 1 if this is a voting node. Otherwise 0. */
int raft_node_is_voting(raft_node_t* me_);

/** Check if a node has sufficient logs to be able to join the cluster.
 **/
int raft_node_has_sufficient_logs(raft_node_t* me_);

/** Apply all entries up to the commit index
 * @return
 *  0 on success;
 *  RAFT_ERR_SHUTDOWN when server MUST shutdown */
int raft_apply_all(raft_server_t* me_);

/** Become leader
 * WARNING: this is a dangerous function call. It could lead to your cluster
 * losing it's consensus guarantees. */
void raft_become_leader(raft_server_t* me);

/** Become follower. This may be used to give up leadership. It does not change
 * currentTerm. */
void raft_become_follower(raft_server_t* me);

/** Determine if entry is voting configuration change.
 * @param[in] ety The entry to query.
 * @return 1 if this is a voting configuration change. */
int raft_entry_is_voting_cfg_change(raft_entry_t* ety);

/** Determine if entry is configuration change.
 * @param[in] ety The entry to query.
 * @return 1 if this is a configuration change. */
int raft_entry_is_cfg_change(raft_entry_t* ety);

/** Begin snapshotting.
 *
 * While snapshotting, raft will:
 *  - not apply log entries
 *  - not start elections
 *
 * If the RAFT_SNAPSHOT_NONBLOCKING_APPLY flag is specified, log entries will
 * be applied during snapshot.  The FSM must isolate the snapshot state and
 * guarantee these changes do not affect it.
 *
 * @return 0 on success
 *
 **/
int raft_begin_snapshot(raft_server_t *me_, int flags);

/** Stop snapshotting.
 *
 * The user MUST include membership changes inside the snapshot. This means
 * that membership changes are included in the size of the snapshot. For peers
 * that load the snapshot, the user needs to deserialize the snapshot to
 * obtain the membership changes.
 *
 * The user MUST compact the log up to the commit index. This means all
 * log entries up to the commit index MUST be deleted (aka polled).
 *
 * @return
 *  0 on success
 *  -1 on failure
 **/
int raft_end_snapshot(raft_server_t *me_);

/** Cancel snapshotting.
 *
 * If an error occurs during snapshotting, this function can be called instead
 * of raft_end_snapshot() to cancel the operation.
 *
 * The user MUST be sure the original snapshot is left untouched and remains
 * usable.
 */
int raft_cancel_snapshot(raft_server_t *me_);

/** Get the entry index of the entry that was snapshotted
 **/
raft_index_t raft_get_snapshot_entry_idx(raft_server_t *me_);

/** Check is a snapshot is in progress
 **/
int raft_snapshot_is_in_progress(raft_server_t *me_);

/** Check if entries can be applied now (no snapshot in progress, or
 * RAFT_SNAPSHOT_NONBLOCKING_APPLY specified).
 **/
int raft_is_apply_allowed(raft_server_t* me_);

/** Remove the first log entry.
 * This should be used for compacting logs.
 * @return 0 on success
 **/
int raft_poll_entry(raft_server_t* me_, raft_entry_t **ety);

/** Get last applied entry
 **/
raft_entry_t *raft_get_last_applied_entry(raft_server_t *me_);

raft_index_t raft_get_first_entry_idx(raft_server_t* me_);

/** Start loading snapshot
 *
 * This is usually the result of a snapshot being loaded.
 * We need to send an appendentries response.
 *
 * This will remove all other nodes (not ourself). The user MUST use the
 * snapshot to load the new membership information.
 *
 * @param[in] last_included_term Term of the last log of the snapshot
 * @param[in] last_included_index Index of the last log of the snapshot
 *
 * @return
 *  0 on success
 *  -1 on failure
 *  RAFT_ERR_SNAPSHOT_ALREADY_LOADED
 **/
int raft_begin_load_snapshot(raft_server_t *me_,
                       raft_term_t last_included_term,
		       raft_index_t last_included_index);

/** Stop loading snapshot.
 *
 * @return
 *  0 on success
 *  -1 on failure
 **/
int raft_end_load_snapshot(raft_server_t *me_);

raft_index_t raft_get_snapshot_last_idx(raft_server_t *me_);

raft_term_t raft_get_snapshot_last_term(raft_server_t *me_);

void raft_set_snapshot_metadata(raft_server_t *me_, raft_term_t term, raft_index_t idx);

/** Check if a node is active.
 * Active nodes could become voting nodes.
 * This should be used for creating the membership snapshot.
 **/
int raft_node_is_active(raft_node_t* me_);

/** Make the node active.
 *
 * The user sets this to 1 between raft_begin_load_snapshot and
 * raft_end_load_snapshot.
 *
 * @param[in] active Set a node as active if this is 1
 **/
void raft_node_set_active(raft_node_t* me_, int active);

/** Check if a node's voting status has been committed.
 * This should be used for creating the membership snapshot.
 **/
int raft_node_is_voting_committed(raft_node_t* me_);

/** Check if a node's membership to the cluster has been committed.
 * This should be used for creating the membership snapshot.
 **/
int raft_node_is_addition_committed(raft_node_t* me_);

/**
 * Register custom heap management functions, to be used if an alternative
 * heap management is used.
 **/
void raft_set_heap_functions(void *(*_malloc)(size_t),
                             void *(*_calloc)(size_t, size_t),
                             void *(*_realloc)(void *, size_t),
                             void (*_free)(void *));

/** Confirm that a node's voting status is final
 * @param[in] node The node
 * @param[in] voting Whether this node's voting status is committed or not */
void raft_node_set_voting_committed(raft_node_t* me_, int voting);

/** Confirm that a node's voting status is final
 * @param[in] node The node
 * @param[in] committed Whether this node's membership is committed or not */
void raft_node_set_addition_committed(raft_node_t* me_, int committed);

/** Check if a voting change is in progress
 * @param[in] raft The Raft server
 * @return 1 if a voting change is in progress */
int raft_voting_change_is_in_progress(raft_server_t* me_);

#endif /* RAFT_H_ */
/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @author Willem Thiart himself@willemthiart.com
 */

#ifndef RAFT_PRIVATE_H_
#define RAFT_PRIVATE_H_



enum {
    RAFT_NODE_STATUS_DISCONNECTED,
    RAFT_NODE_STATUS_CONNECTED,
    RAFT_NODE_STATUS_CONNECTING,
    RAFT_NODE_STATUS_DISCONNECTING
};

typedef struct {
    /* Persistent state: */

    /* the server's best guess of what the current term is
     * starts at zero */
    raft_term_t current_term;

    /* The candidate the server voted for in its current term,
     * or Nil if it hasn't voted for any.  */
    raft_node_id_t voted_for;

    /* the log which is replicated */
    void* log;

    /* Volatile state: */

    /* idx of highest log entry known to be committed */
    raft_index_t commit_idx;

    /* idx of highest log entry applied to state machine */
    raft_index_t last_applied_idx;

    /* follower/leader/candidate indicator */
    int state;

    /* amount of time left till timeout */
    int timeout_elapsed;

    raft_node_t* nodes;
    int num_nodes;

    int election_timeout;
    int election_timeout_rand;
    int request_timeout;

    /* what this node thinks is the node ID of the current leader, or NULL if
     * there isn't a known current leader. */
    raft_node_t* current_leader;

    /* callbacks */
    raft_cbs_t cb;
    void* udata;

    /* my node ID */
    raft_node_t* node;

    /* the log which has a voting cfg change, otherwise -1 */
    raft_index_t voting_cfg_change_log_idx;

    /* Our membership with the cluster is confirmed (ie. configuration log was
     * committed) */
    int connected;

    int snapshot_in_progress;
    int snapshot_flags;

    /* Last compacted snapshot */
    raft_index_t snapshot_last_idx;
    raft_term_t snapshot_last_term;

    /* Previous index/term values stored during snapshot,
     * which are restored if the operation is cancelled.
     */
    raft_index_t saved_snapshot_last_idx;
    raft_term_t saved_snapshot_last_term;
} raft_server_private_t;

int raft_election_start(raft_server_t* me);

int raft_become_candidate(raft_server_t* me);

void raft_randomize_election_timeout(raft_server_t* me_);

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

void raft_set_last_applied_idx(raft_server_t* me, raft_index_t idx);

void raft_set_state(raft_server_t* me_, int state);

int raft_get_state(raft_server_t* me_);

raft_node_t* raft_node_new(void* udata, raft_node_id_t id);

void raft_node_free(raft_node_t* me_);

void raft_node_set_next_idx(raft_node_t* node, raft_index_t nextIdx);

void raft_node_set_match_idx(raft_node_t* node, raft_index_t matchIdx);

raft_index_t raft_node_get_match_idx(raft_node_t* me_);

void raft_node_vote_for_me(raft_node_t* me_, const int vote);

int raft_node_has_vote_for_me(raft_node_t* me_);

void raft_node_set_has_sufficient_logs(raft_node_t* me_);

int raft_votes_is_majority(const int nnodes, const int nvotes);

void raft_offer_log(raft_server_t* me_, raft_entry_t* ety, const raft_index_t idx);

void raft_pop_log(raft_server_t* me_, raft_entry_t* ety, const raft_index_t idx);

raft_index_t raft_get_num_snapshottable_logs(raft_server_t* me_);

int raft_node_is_active(raft_node_t* me_);

void raft_node_set_voting_committed(raft_node_t* me_, int voting);

void raft_node_set_addition_committed(raft_node_t* me_, int committed);

/* Heap functions */
extern void *(*__raft_malloc)(size_t size);
extern void *(*__raft_calloc)(size_t nmemb, size_t size);
extern void *(*__raft_realloc)(void *ptr, size_t size);
extern void (*__raft_free)(void *ptr);

#endif /* RAFT_PRIVATE_H_ */
#ifndef RAFT_LOG_H_
#define RAFT_LOG_H_



typedef void* log_t;

log_t* log_new(void);

log_t* log_alloc(raft_index_t initial_size);

void log_set_callbacks(log_t* me_, raft_cbs_t* funcs, void* raft);

void log_free(log_t* me_);

void log_clear(log_t* me_);

void log_clear_entries(log_t* me_);

/**
 * Add entry to log.
 * Don't add entry if we've already added this entry (based off ID)
 * Don't add entries with ID=0
 * @return 0 if unsuccessful; 1 otherwise */
int log_append_entry(log_t* me_, raft_entry_t* c);

/**
 * @return number of entries held within log */
raft_index_t log_count(log_t* me_);

/**
 * Delete all logs from this log onwards */
int log_delete(log_t* me_, raft_index_t idx);

/**
 * Empty the queue. */
void log_empty(log_t * me_);

/**
 * Remove oldest entry. Set *etyp to oldest entry on success. */
int log_poll(log_t * me_, void** etyp);

/** Get an array of entries from this index onwards.
 * This is used for batching.
 */
raft_entry_t* log_get_from_idx(log_t* me_, raft_index_t idx, int *n_etys);

raft_entry_t* log_get_at_idx(log_t* me_, raft_index_t idx);

/**
 * @return youngest entry */
raft_entry_t *log_peektail(log_t * me_);

raft_index_t log_get_current_idx(log_t* me_);

int log_load_from_snapshot(log_t *me_, raft_index_t idx, raft_term_t term);

raft_index_t log_get_base(log_t* me_);

#endif /* RAFT_LOG_H_ */
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

typedef struct
{
    /* size of array */
    raft_index_t size;

    /* the amount of elements in the array */
    raft_index_t count;

    /* position of the queue */
    raft_index_t front, back;

    /* we compact the log, and thus need to increment the Base Log Index */
    raft_index_t base;

    raft_entry_t* entries;

    /* callbacks */
    raft_cbs_t *cb;
    void* raft;
} log_private_t;

static int mod(raft_index_t a, raft_index_t b)
{
    int r = a % b;
    return r < 0 ? r + b : r;
}

static int __ensurecapacity(log_private_t * me)
{
    raft_index_t i, j;
    raft_entry_t *temp;

    if (me->count < me->size)
        return 0;

    temp = (raft_entry_t*)__raft_calloc(1, sizeof(raft_entry_t) * me->size * 2);
    if (!temp)
        return RAFT_ERR_NOMEM;

    for (i = 0, j = me->front; i < me->count; i++, j++)
    {
        if (j == me->size)
            j = 0;
        memcpy(&temp[i], &me->entries[j], sizeof(raft_entry_t));
    }

    /* clean up old entries */
    __raft_free(me->entries);

    me->size *= 2;
    me->entries = temp;
    me->front = 0;
    me->back = me->count;
    return 0;
}

int log_load_from_snapshot(log_t *me_, raft_index_t idx, raft_term_t term)
{
    log_private_t* me = (log_private_t*)me_;

    log_clear_entries(me_);
    log_clear(me_);
    me->base = idx;

    return 0;
}

log_t* log_alloc(raft_index_t initial_size)
{
    log_private_t* me = (log_private_t*)__raft_calloc(1, sizeof(log_private_t));
    if (!me)
        return NULL;
    me->size = initial_size;
    log_clear((log_t*)me);
    me->entries = (raft_entry_t*)__raft_calloc(1, sizeof(raft_entry_t) * me->size);
    if (!me->entries) {
        __raft_free(me);
        return NULL;
    }
    return (log_t*)me;
}

log_t* log_new(void)
{
    return log_alloc(INITIAL_CAPACITY);
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

void log_clear_entries(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    raft_index_t i;

    if (!me->count || !me->cb || !me->cb->log_clear)
        return;

    for (i = me->base; i <= me->base + me->count; i++)
    {
        me->cb->log_clear(me->raft, raft_get_udata(me->raft),
                          &me->entries[(me->front + i - me->base) % me->size], i);
    }
}

/** TODO: rename log_append */
int log_append_entry(log_t* me_, raft_entry_t* ety)
{
    log_private_t* me = (log_private_t*)me_;
    raft_index_t idx = me->base + me->count + 1;
    int e;

    e = __ensurecapacity(me);
    if (e != 0)
        return e;

    memcpy(&me->entries[me->back], ety, sizeof(raft_entry_t));

    if (me->cb && me->cb->log_offer)
    {
        void* ud = raft_get_udata(me->raft);
        e = me->cb->log_offer(me->raft, ud, &me->entries[me->back], idx);
        if (0 != e)
            return e;
        raft_offer_log(me->raft, &me->entries[me->back], idx);
    }

    me->count++;
    me->back++;
    me->back = me->back % me->size;

    return 0;
}

raft_entry_t* log_get_from_idx(log_t* me_, raft_index_t idx, int *n_etys)
{
    log_private_t* me = (log_private_t*)me_;
    raft_index_t i;

    assert(0 <= idx - 1);

    if (me->base + me->count < idx || idx <= me->base)
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

raft_entry_t* log_get_at_idx(log_t* me_, raft_index_t idx)
{
    log_private_t* me = (log_private_t*)me_;
    raft_index_t i;

    if (idx == 0)
        return NULL;

    if (me->base + me->count < idx || idx <= me->base)
        return NULL;

    /* idx starts at 1 */
    idx -= 1;

    i = (me->front + idx - me->base) % me->size;
    return &me->entries[i];
}

raft_index_t log_count(log_t* me_)
{
    return ((log_private_t*)me_)->count;
}

int log_delete(log_t* me_, raft_index_t idx)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == idx)
        return -1;

    if (idx < me->base)
        idx = me->base;

    for (; idx <= me->base + me->count && me->count;)
    {
        raft_index_t idx_tmp = me->base + me->count;
        raft_index_t back = mod(me->back - 1, me->size);

        if (me->cb && me->cb->log_pop)
        {
            int e = me->cb->log_pop(me->raft, raft_get_udata(me->raft),
                                    &me->entries[back], idx_tmp);
            if (0 != e)
                return e;
        }
        raft_pop_log(me->raft, &me->entries[back], idx_tmp);
        me->back = back;
        me->count--;
    }
    return 0;
}

int log_poll(log_t * me_, void** etyp)
{
    log_private_t* me = (log_private_t*)me_;
    raft_index_t idx = me->base + 1;

    if (0 == me->count)
        return -1;

    const void *elem = &me->entries[me->front];
    if (me->cb && me->cb->log_poll)
    {
        int e = me->cb->log_poll(me->raft, raft_get_udata(me->raft),
                                 &me->entries[me->front], idx);
        if (0 != e)
            return e;
    }
    me->front++;
    me->front = me->front % me->size;
    me->count--;
    me->base++;

    *etyp = (void*)elem;
    return 0;
}

raft_entry_t *log_peektail(log_t * me_)
{
    log_private_t* me = (log_private_t*)me_;

    if (0 == me->count)
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

    __raft_free(me->entries);
    __raft_free(me);
}

raft_index_t log_get_current_idx(log_t* me_)
{
    log_private_t* me = (log_private_t*)me_;
    return log_count(me_) + me->base;
}

raft_index_t log_get_base(log_t* me_)
{
    return ((log_private_t*)me_)->base;
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




#define RAFT_NODE_VOTED_FOR_ME        (1 << 0)
#define RAFT_NODE_VOTING              (1 << 1)
#define RAFT_NODE_HAS_SUFFICIENT_LOG  (1 << 2)
#define RAFT_NODE_INACTIVE            (1 << 3)
#define RAFT_NODE_VOTING_COMMITTED    (1 << 4)
#define RAFT_NODE_ADDITION_COMMITTED  (1 << 5)

typedef struct
{
    void* udata;

    raft_index_t next_idx;
    raft_index_t match_idx;

    int flags;

    raft_node_id_t id;
} raft_node_private_t;

raft_node_t* raft_node_new(void* udata, raft_node_id_t id)
{
    raft_node_private_t* me;
    me = (raft_node_private_t*)__raft_calloc(1, sizeof(raft_node_private_t));
    if (!me)
        return NULL;
    me->udata = udata;
    me->next_idx = 1;
    me->match_idx = 0;
    me->id = id;
    me->flags = RAFT_NODE_VOTING;
    return (raft_node_t*)me;
}

void raft_node_free(raft_node_t* me_)
{
    __raft_free(me_);
}

raft_index_t raft_node_get_next_idx(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->next_idx;
}

void raft_node_set_next_idx(raft_node_t* me_, raft_index_t nextIdx)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    /* log index begins at 1 */
    me->next_idx = nextIdx < 1 ? 1 : nextIdx;
}

raft_index_t raft_node_get_match_idx(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->match_idx;
}

void raft_node_set_match_idx(raft_node_t* me_, raft_index_t matchIdx)
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
    {
        assert(!raft_node_is_voting(me_));
        me->flags |= RAFT_NODE_VOTING;
    }
    else
    {
        assert(raft_node_is_voting(me_));
        me->flags &= ~RAFT_NODE_VOTING;
    }
}

int raft_node_is_voting(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_VOTING) != 0;
}

int raft_node_has_sufficient_logs(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_HAS_SUFFICIENT_LOG) != 0;
}

void raft_node_set_has_sufficient_logs(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    me->flags |= RAFT_NODE_HAS_SUFFICIENT_LOG;
}

void raft_node_set_active(raft_node_t* me_, int active)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    if (!active)
        me->flags |= RAFT_NODE_INACTIVE;
    else
        me->flags &= ~RAFT_NODE_INACTIVE;
}

int raft_node_is_active(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_INACTIVE) == 0;
}

void raft_node_set_voting_committed(raft_node_t* me_, int voting)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    if (voting)
        me->flags |= RAFT_NODE_VOTING_COMMITTED;
    else
        me->flags &= ~RAFT_NODE_VOTING_COMMITTED;
}

int raft_node_is_voting_committed(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_VOTING_COMMITTED) != 0;
}

raft_node_id_t raft_node_get_id(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return me->id;
}

void raft_node_set_addition_committed(raft_node_t* me_, int committed)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    if (committed)
        me->flags |= RAFT_NODE_ADDITION_COMMITTED;
    else
        me->flags &= ~RAFT_NODE_ADDITION_COMMITTED;
}

int raft_node_is_addition_committed(raft_node_t* me_)
{
    raft_node_private_t* me = (raft_node_private_t*)me_;
    return (me->flags & RAFT_NODE_ADDITION_COMMITTED) != 0;
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





#ifndef willemt_raft_min
#define willemt_raft_min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef willemt_raft_max
#define willemt_raft_max(a, b) ((a) < (b) ? (b) : (a))
#endif

void *(*__raft_malloc)(size_t) = malloc;
void *(*__raft_calloc)(size_t, size_t) = calloc;
void *(*__raft_realloc)(void *, size_t) = realloc;
void (*__raft_free)(void *) = free;

void raft_set_heap_functions(void *(*_malloc)(size_t),
                             void *(*_calloc)(size_t, size_t),
                             void *(*_realloc)(void *, size_t),
                             void (*_free)(void *))
{
    __raft_malloc = _malloc;
    __raft_calloc = _calloc;
    __raft_realloc = _realloc;
    __raft_free = _free;
}

static void __raft_console_log(raft_server_t *me_, raft_node_t* node, const char *fmt, ...)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (me->cb.log == NULL) return;
    char buf[1024];
    va_list args;

    va_start(args, fmt);
    vsprintf(buf, fmt, args);

    me->cb.log(me_, node, me->udata, buf);
}

void raft_randomize_election_timeout(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    /* [election_timeout, 2 * election_timeout) */
    me->election_timeout_rand = me->election_timeout + rand() % me->election_timeout;
    __raft_console_log(me_, NULL, "randomize election timeout to %d", me->election_timeout_rand);
}

raft_server_t* raft_new(void)
{
    raft_server_private_t* me =
        (raft_server_private_t*)__raft_calloc(1, sizeof(raft_server_private_t));
    if (!me)
        return NULL;
    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    me->request_timeout = 200;
    me->election_timeout = 1000;
    raft_randomize_election_timeout((raft_server_t*)me);
    me->log = log_new();
    if (!me->log) {
        __raft_free(me);
        return NULL;
    }
    me->voting_cfg_change_log_idx = -1;
    raft_set_state((raft_server_t*)me, RAFT_STATE_FOLLOWER);
    me->current_leader = NULL;

    me->snapshot_in_progress = 0;
    raft_set_snapshot_metadata((raft_server_t*)me, 0, 0);

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
    if (me->nodes)
        __raft_free(me->nodes);
    __raft_free(me_);
}

void raft_clear(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    me->current_term = 0;
    me->voted_for = -1;
    me->timeout_elapsed = 0;
    raft_randomize_election_timeout(me_);
    me->voting_cfg_change_log_idx = -1;
    raft_set_state((raft_server_t*)me, RAFT_STATE_FOLLOWER);
    me->current_leader = NULL;
    me->commit_idx = 0;
    me->last_applied_idx = 0;
    me->num_nodes = 0;
    me->node = NULL;
    log_clear_entries(me->log);
    log_clear(me->log);
}

int raft_delete_entry_from_idx(raft_server_t* me_, raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    assert(raft_get_commit_idx(me_) < idx);

    if (idx <= me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    return log_delete(me->log, idx);
}

int raft_election_start(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    __raft_console_log(me_, NULL, "election starting: %d %d, term: %d ci: %d",
          me->election_timeout_rand, me->timeout_elapsed, me->current_term,
          raft_get_current_idx(me_));

    return raft_become_candidate(me_);
}

void raft_become_leader(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    __raft_console_log(me_, NULL, "becoming leader term:%d", raft_get_current_term(me_));

    raft_set_state(me_, RAFT_STATE_LEADER);
    me->timeout_elapsed = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node == node || !raft_node_is_active(node))
            continue;

        raft_node_set_next_idx(node, raft_get_current_idx(me_) + 1);
        raft_node_set_match_idx(node, 0);
        raft_send_appendentries(me_, node);
    }
}

int raft_become_candidate(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    __raft_console_log(me_, NULL, "becoming candidate");

    int e = raft_set_current_term(me_, raft_get_current_term(me_) + 1);
    if (0 != e)
        return e;
    for (i = 0; i < me->num_nodes; i++)
        raft_node_vote_for_me(me->nodes[i], 0);
    raft_vote(me_, me->node);
    me->current_leader = NULL;
    raft_set_state(me_, RAFT_STATE_CANDIDATE);

    raft_randomize_election_timeout(me_);
    me->timeout_elapsed = 0;

    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node != node &&
            raft_node_is_active(node) &&
            raft_node_is_voting(node))
        {
            raft_send_requestvote(me_, node);
        }
    }
    return 0;
}

void raft_become_follower(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    __raft_console_log(me_, NULL, "becoming follower");
    raft_set_state(me_, RAFT_STATE_FOLLOWER);
    raft_randomize_election_timeout(me_);
    me->timeout_elapsed = 0;
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
    else if (me->election_timeout_rand <= me->timeout_elapsed &&
        /* Don't become the leader when building snapshots or bad things will
         * happen when we get a client request */
        !raft_snapshot_is_in_progress(me_))
    {
        if (1 < raft_get_num_voting_nodes(me_) &&
            raft_node_is_voting(raft_get_my_node(me_)))
        {
            int e = raft_election_start(me_);
            if (0 != e)
                return e;
        }
    }

    if (me->last_applied_idx < raft_get_commit_idx(me_) &&
            raft_is_apply_allowed(me_))
    {
        int e = raft_apply_all(me_);
        if (0 != e)
            return e;
    }

    return 0;
}

raft_entry_t* raft_get_entry_from_idx(raft_server_t* me_, raft_index_t etyidx)
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

    __raft_console_log(me_, node,
          "received appendentries response %s ci:%d rci:%d 1stidx:%d",
          r->success == 1 ? "SUCCESS" : "fail",
          raft_get_current_idx(me_),
          r->current_idx,
          r->first_idx);

    if (!node)
        return -1;

    if (!raft_is_leader(me_))
        return RAFT_ERR_NOT_LEADER;

    /* If response contains term T > currentTerm: set currentTerm = T
       and convert to follower (§5.3) */
    if (me->current_term < r->term)
    {
        int e = raft_set_current_term(me_, r->term);
        if (0 != e)
            return e;
        raft_become_follower(me_);
        me->current_leader = NULL;
        return 0;
    }
    else if (me->current_term != r->term)
        return 0;

    raft_index_t match_idx = raft_node_get_match_idx(node);

    if (0 == r->success)
    {
        /* If AppendEntries fails because of log inconsistency:
           decrement nextIndex and retry (§5.3) */
        raft_index_t next_idx = raft_node_get_next_idx(node);
        assert(0 < next_idx);
        /* Stale response -- ignore */
        if (r->current_idx < match_idx)
            return 0;
        if (r->current_idx < next_idx - 1)
            raft_node_set_next_idx(node, willemt_raft_min(r->current_idx + 1, raft_get_current_idx(me_)));
        else
            raft_node_set_next_idx(node, next_idx - 1);

        /* retry */
        raft_send_appendentries(me_, node);
        return 0;
    }


    if (!raft_node_is_voting(node) &&
        !raft_voting_change_is_in_progress(me_) &&
        raft_get_current_idx(me_) <= r->current_idx + 1 &&
        !raft_node_is_voting_committed(node) &&
        me->cb.node_has_sufficient_logs &&
        0 == raft_node_has_sufficient_logs(node)
        )
    {
        int e = me->cb.node_has_sufficient_logs(me_, me->udata, node);
        if (0 == e)
            raft_node_set_has_sufficient_logs(node);
    }

    if (r->current_idx <= match_idx)
        return 0;

    assert(r->current_idx <= raft_get_current_idx(me_));

    raft_node_set_next_idx(node, r->current_idx + 1);
    raft_node_set_match_idx(node, r->current_idx);

    /* Update commit idx */
    raft_index_t point = r->current_idx;
    if (point)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me_, point);
        if (raft_get_commit_idx(me_) < point && ety->term == me->current_term)
        {
            int i, votes = 1;
            for (i = 0; i < me->num_nodes; i++)
            {
                raft_node_t* node = me->nodes[i];
                if (me->node != node &&
                    raft_node_is_active(node) &&
                    raft_node_is_voting(node) &&
                    point <= raft_node_get_match_idx(node))
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
    int e = 0;

    if (0 < ae->n_entries)
        __raft_console_log(me_, node, "recvd appendentries t:%d ci:%d lc:%d pli:%d plt:%d #%d",
              ae->term,
              raft_get_current_idx(me_),
              ae->leader_commit,
              ae->prev_log_idx,
              ae->prev_log_term,
              ae->n_entries);

    r->success = 0;

    if (raft_is_candidate(me_) && me->current_term == ae->term)
    {
        raft_become_follower(me_);
    }
    else if (me->current_term < ae->term)
    {
        e = raft_set_current_term(me_, ae->term);
        if (0 != e)
            goto out;
        raft_become_follower(me_);
    }
    else if (ae->term < me->current_term)
    {
        /* 1. Reply false if term < currentTerm (§5.1) */
        __raft_console_log(me_, node, "AE term %d is less than current term %d",
              ae->term, me->current_term);
        goto out;
    }

    /* update current leader because ae->term is up to date */
    me->current_leader = node;

    me->timeout_elapsed = 0;

    /* Not the first appendentries we've received */
    /* NOTE: the log starts at 1 */
    if (0 < ae->prev_log_idx)
    {
        raft_entry_t* ety = raft_get_entry_from_idx(me_, ae->prev_log_idx);

        /* Is a snapshot */
        if (ae->prev_log_idx == me->snapshot_last_idx)
        {
            if (me->snapshot_last_term != ae->prev_log_term)
            {
                /* Should never happen; something is seriously wrong! */
                __raft_console_log(me_, node, "Snapshot AE prev conflicts with committed entry");
                e = RAFT_ERR_SHUTDOWN;
                goto out;
            }
        }
        /* 2. Reply false if log doesn't contain an entry at prevLogIndex
           whose term matches prevLogTerm (§5.3) */
        else if (!ety)
        {
            __raft_console_log(me_, node, "AE no log at prev_idx %d", ae->prev_log_idx);
            goto out;
        }
        else if (ety->term != ae->prev_log_term)
        {
            __raft_console_log(me_, node, "AE term doesn't match prev_term (ie. %d vs %d) ci:%d comi:%d lcomi:%d pli:%d",
                  ety->term, ae->prev_log_term, raft_get_current_idx(me_),
                  raft_get_commit_idx(me_), ae->leader_commit, ae->prev_log_idx);
            if (ae->prev_log_idx <= raft_get_commit_idx(me_))
            {
                /* Should never happen; something is seriously wrong! */
                __raft_console_log(me_, node, "AE prev conflicts with committed entry");
                e = RAFT_ERR_SHUTDOWN;
                goto out;
            }
            /* Delete all the following log entries because they don't match */
            e = raft_delete_entry_from_idx(me_, ae->prev_log_idx);
            goto out;
        }
    }

    r->success = 1;
    r->current_idx = ae->prev_log_idx;

    /* 3. If an existing entry conflicts with a new one (same index
       but different terms), delete the existing entry and all that
       follow it (§5.3) */
    int i;
    for (i = 0; i < ae->n_entries; i++)
    {
        raft_entry_t* ety = &ae->entries[i];
        raft_index_t ety_index = ae->prev_log_idx + 1 + i;
        raft_entry_t* existing_ety = raft_get_entry_from_idx(me_, ety_index);
        if (existing_ety && existing_ety->term != ety->term)
        {
            if (ety_index <= raft_get_commit_idx(me_))
            {
                /* Should never happen; something is seriously wrong! */
                __raft_console_log(me_, node, "AE entry conflicts with committed entry ci:%d comi:%d lcomi:%d pli:%d",
                      raft_get_current_idx(me_), raft_get_commit_idx(me_),
                      ae->leader_commit, ae->prev_log_idx);
                e = RAFT_ERR_SHUTDOWN;
                goto out;
            }
            e = raft_delete_entry_from_idx(me_, ety_index);
            if (0 != e)
                goto out;
            break;
        }
        else if (!existing_ety)
            break;
        r->current_idx = ety_index;
    }

    /* Pick up remainder in case of mismatch or missing entry */
    for (; i < ae->n_entries; i++)
    {
        e = raft_append_entry(me_, &ae->entries[i]);
        if (0 != e)
            goto out;
        r->current_idx = ae->prev_log_idx + 1 + i;
    }

    /* 4. If leaderCommit > commitIndex, set commitIndex =
        willemt_raft_min(leaderCommit, index of most recent entry) */
    if (raft_get_commit_idx(me_) < ae->leader_commit)
    {
        raft_index_t last_log_idx = willemt_raft_max(raft_get_current_idx(me_), 1);
        raft_set_commit_idx(me_, willemt_raft_min(last_log_idx, ae->leader_commit));
    }

out:
    r->term = me->current_term;
    if (0 == r->success)
        r->current_idx = raft_get_current_idx(me_);
    r->first_idx = ae->prev_log_idx + 1;
    return e;
}

int raft_already_voted(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->voted_for != -1;
}

static int __should_grant_vote(raft_server_private_t* me, msg_requestvote_t* vr)
{
    if (!raft_node_is_voting(raft_get_my_node((void*)me)))
        return 0;

    if (vr->term < raft_get_current_term((void*)me))
        return 0;

    /* TODO: if voted for is candidate return 1 (if below checks pass) */
    if (raft_already_voted((void*)me))
        return 0;

    /* Below we check if log is more up-to-date... */

    raft_index_t current_idx = raft_get_current_idx((void*)me);

    /* Our log is definitely not more up-to-date if it's empty! */
    if (0 == current_idx)
        return 1;

    raft_entry_t* ety = raft_get_entry_from_idx((void*)me, current_idx);
    int ety_term;

    // TODO: add test
    if (ety)
        ety_term = ety->term;
    else if (!ety && me->snapshot_last_idx == current_idx)
        ety_term = me->snapshot_last_term;
    else
        return 0;

    if (ety_term < vr->last_log_term)
        return 1;

    if (vr->last_log_term == ety_term && current_idx <= vr->last_log_idx)
        return 1;

    return 0;
}

int raft_recv_requestvote(raft_server_t* me_,
                          raft_node_t* node,
                          msg_requestvote_t* vr,
                          msg_requestvote_response_t *r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int e = 0;

    if (!node)
        node = raft_get_node(me_, vr->candidate_id);

    /* Reject request if we have a leader */
    if (me->current_leader && me->current_leader != node &&
            (me->timeout_elapsed < me->election_timeout)) {
        r->vote_granted = 0;
        goto done;
    }

    if (raft_get_current_term(me_) < vr->term)
    {
        e = raft_set_current_term(me_, vr->term);
        if (0 != e) {
            r->vote_granted = 0;
            goto done;
        }
        raft_become_follower(me_);
        me->current_leader = NULL;
    }

    if (__should_grant_vote(me, vr))
    {
        /* It shouldn't be possible for a leader or candidate to grant a vote
         * Both states would have voted for themselves */
        assert(!(raft_is_leader(me_) || raft_is_candidate(me_)));

        e = raft_vote_for_nodeid(me_, vr->candidate_id);
        if (0 == e)
            r->vote_granted = 1;
        else
            r->vote_granted = 0;

        /* must be in an election. */
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
    __raft_console_log(me_, node, "node requested vote: %d replying: %s",
          node,
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown");

    r->term = raft_get_current_term(me_);
    return e;
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

    __raft_console_log(me_, node, "node responded to requestvote status: %s",
          r->vote_granted == 1 ? "granted" :
          r->vote_granted == 0 ? "not granted" : "unknown");

    if (!raft_is_candidate(me_))
    {
        return 0;
    }
    else if (raft_get_current_term(me_) < r->term)
    {
        int e = raft_set_current_term(me_, r->term);
        if (0 != e)
            return e;
        raft_become_follower(me_);
        me->current_leader = NULL;
        return 0;
    }
    else if (raft_get_current_term(me_) != r->term)
    {
        /* The node who voted for us would have obtained our term.
         * Therefore this is an old message we should ignore.
         * This happens if the network is pretty choppy. */
        return 0;
    }

    __raft_console_log(me_, node, "node responded to requestvote status:%s ct:%d rt:%d",
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
                    msg_entry_t* ety,
                    msg_entry_response_t *r)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    if (raft_entry_is_voting_cfg_change(ety))
    {
        /* Only one voting cfg change at a time */
        if (raft_voting_change_is_in_progress(me_))
            return RAFT_ERR_ONE_VOTING_CHANGE_ONLY;

        /* Multi-threading: need to fail here because user might be
         * snapshotting membership settings. */
        if (!raft_is_apply_allowed(me_))
            return RAFT_ERR_SNAPSHOT_IN_PROGRESS;
    }

    if (!raft_is_leader(me_))
        return RAFT_ERR_NOT_LEADER;

    __raft_console_log(me_, NULL, "received entry t:%d id: %d idx: %d",
          me->current_term, ety->id, raft_get_current_idx(me_) + 1);

    ety->term = me->current_term;
    int e = raft_append_entry(me_, ety);
    if (0 != e)
        return e;

    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node == node ||
            !node ||
            !raft_node_is_active(node) ||
            !raft_node_is_voting(node))
            continue;

        /* Only send new entries.
         * Don't send the entry to peers who are behind, to prevent them from
         * becoming congested. */
        raft_index_t next_idx = raft_node_get_next_idx(node);
        if (next_idx == raft_get_current_idx(me_))
            raft_send_appendentries(me_, node);
    }

    /* if we're the only node, we can consider the entry committed */
    if (1 == raft_get_num_voting_nodes(me_))
        raft_set_commit_idx(me_, raft_get_current_idx(me_));

    r->id = ety->id;
    r->idx = raft_get_current_idx(me_);
    r->term = me->current_term;

    /* FIXME: is this required if raft_append_entry does this too? */
    if (raft_entry_is_voting_cfg_change(ety))
        me->voting_cfg_change_log_idx = raft_get_current_idx(me_);

    return 0;
}

int raft_send_requestvote(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    msg_requestvote_t rv;
    int e = 0;

    assert(node);
    assert(node != me->node);

    __raft_console_log(me_, node, "sending requestvote to: %d", node);

    rv.term = me->current_term;
    rv.last_log_idx = raft_get_current_idx(me_);
    rv.last_log_term = raft_get_last_log_term(me_);
    rv.candidate_id = raft_get_nodeid(me_);
    if (me->cb.send_requestvote)
        e = me->cb.send_requestvote(me_, me->udata, node, &rv);
    return e;
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

    if (!raft_is_apply_allowed(me_)) 
        return -1;

    /* Don't apply after the commit_idx */
    if (me->last_applied_idx == raft_get_commit_idx(me_))
        return -1;

    raft_index_t log_idx = me->last_applied_idx + 1;

    raft_entry_t* ety = raft_get_entry_from_idx(me_, log_idx);
    if (!ety)
        return -1;

    __raft_console_log(me_, NULL, "applying log: %d, id: %d size: %d",
          log_idx, ety->id, ety->data.len);

    me->last_applied_idx++;
    if (me->cb.applylog)
    {
        int e = me->cb.applylog(me_, me->udata, ety, me->last_applied_idx);
        if (RAFT_ERR_SHUTDOWN == e)
            return RAFT_ERR_SHUTDOWN;
    }

    /* voting cfg change is now complete */
    if (log_idx == me->voting_cfg_change_log_idx)
        me->voting_cfg_change_log_idx = -1;

    if (!raft_entry_is_cfg_change(ety))
        return 0;

    raft_node_id_t node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, log_idx);
    raft_node_t* node = raft_get_node(me_, node_id);

    switch (ety->type) {
        case RAFT_LOGTYPE_ADD_NODE:
            raft_node_set_addition_committed(node, 1);
            raft_node_set_voting_committed(node, 1);
            /* Membership Change: confirm connection with cluster */
            raft_node_set_has_sufficient_logs(node);
            if (node_id == raft_get_nodeid(me_))
                me->connected = RAFT_NODE_STATUS_CONNECTED;
            break;
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            raft_node_set_addition_committed(node, 1);
            break;
        case RAFT_LOGTYPE_DEMOTE_NODE:
            if (node)
                raft_node_set_voting_committed(node, 0);
            break;
        case RAFT_LOGTYPE_REMOVE_NODE:
            if (node)
                raft_remove_node(me_, node);
            break;
        default:
            break;
    }

    return 0;
}

raft_entry_t* raft_get_entries_from_idx(raft_server_t* me_, raft_index_t idx, int* n_etys)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_get_from_idx(me->log, idx, n_etys);
}

int raft_send_appendentries(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    assert(node);
    assert(node != me->node);

    if (!(me->cb.send_appendentries))
        return -1;

    msg_appendentries_t ae = {};
    ae.term = me->current_term;
    ae.leader_commit = raft_get_commit_idx(me_);
    ae.prev_log_idx = 0;
    ae.prev_log_term = 0;

    raft_index_t next_idx = raft_node_get_next_idx(node);

    /* figure out if the client needs a snapshot sent */
    if (0 < me->snapshot_last_idx && next_idx < me->snapshot_last_idx)
    {
        if (me->cb.send_snapshot)
            me->cb.send_snapshot(me_, me->udata, node);
        return RAFT_ERR_NEEDS_SNAPSHOT;
    }

    ae.entries = raft_get_entries_from_idx(me_, next_idx, &ae.n_entries);
    assert((!ae.entries && 0 == ae.n_entries) ||
            (ae.entries && 0 < ae.n_entries));

    /* previous log is the log just before the new logs */
    if (1 < next_idx)
    {
        raft_entry_t* prev_ety = raft_get_entry_from_idx(me_, next_idx - 1);
        if (!prev_ety)
        {
            ae.prev_log_idx = me->snapshot_last_idx;
            ae.prev_log_term = me->snapshot_last_term;
        }
        else
        {
            ae.prev_log_idx = next_idx - 1;
            ae.prev_log_term = prev_ety->term;
        }
    }

    __raft_console_log(me_, node, "sending appendentries node: ci:%d comi:%d t:%d lc:%d pli:%d plt:%d",
          raft_get_current_idx(me_),
          raft_get_commit_idx(me_),
          ae.term,
          ae.leader_commit,
          ae.prev_log_idx,
          ae.prev_log_term);

    return me->cb.send_appendentries(me_, me->udata, node, &ae);
}

int raft_send_appendentries_all(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i, e;

    me->timeout_elapsed = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->node == me->nodes[i] || !raft_node_is_active(me->nodes[i]))
            continue;

        e = raft_send_appendentries(me_, me->nodes[i]);
        if (0 != e)
            return e;
    }

    return 0;
}

raft_node_t* raft_add_node_internal(raft_server_t* me_, raft_entry_t *ety, void* udata, raft_node_id_t id, int is_self)
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

    node = raft_node_new(udata, id);
    if (!node)
        return NULL;
    void* p = __raft_realloc(me->nodes, sizeof(void*) * (me->num_nodes + 1));
    if (!p) {
        raft_node_free(node);
        return NULL;
    }
    me->num_nodes++;
    me->nodes = p;
    me->nodes[me->num_nodes - 1] = node;
    if (is_self)
        me->node = me->nodes[me->num_nodes - 1];

    node = me->nodes[me->num_nodes - 1];

    if (me->cb.notify_membership_event)
        me->cb.notify_membership_event(me_, raft_get_udata(me_), node, ety, RAFT_MEMBERSHIP_ADD);

    return node;
}

raft_node_t* raft_add_node(raft_server_t* me_, void* udata, raft_node_id_t id, int is_self)
{
    return raft_add_node_internal(me_, NULL, udata, id, is_self);
}

static raft_node_t* raft_add_non_voting_node_internal(raft_server_t* me_, raft_entry_t *ety, void* udata, raft_node_id_t id, int is_self)
{
    if (raft_get_node(me_, id))
        return NULL;

    raft_node_t* node = raft_add_node_internal(me_, ety, udata, id, is_self);
    if (!node)
        return NULL;

    raft_node_set_voting(node, 0);
    return node;
}

raft_node_t* raft_add_non_voting_node(raft_server_t* me_, void* udata, raft_node_id_t id, int is_self)
{
    return raft_add_non_voting_node_internal(me_, NULL, udata, id, is_self);
}

void raft_remove_node(raft_server_t* me_, raft_node_t* node)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (me->cb.notify_membership_event)
        me->cb.notify_membership_event(me_, raft_get_udata(me_), node, NULL, RAFT_MEMBERSHIP_REMOVE);

    assert(node);

    int i, found = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (me->nodes[i] == node)
        {
            found = 1;
            break;
        }
    }
    assert(found);
    memmove(&me->nodes[i], &me->nodes[i + 1], sizeof(*me->nodes) * (me->num_nodes - i - 1));
    me->num_nodes--;

    raft_node_free(node);
}

int raft_get_nvotes_for_me(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i, votes;

    for (i = 0, votes = 0; i < me->num_nodes; i++)
    {
        if (me->node != me->nodes[i] &&
            raft_node_is_active(me->nodes[i]) &&
            raft_node_is_voting(me->nodes[i]) &&
            raft_node_has_vote_for_me(me->nodes[i]))
        {
            votes += 1;
        }
    }

    if (me->voted_for == raft_get_nodeid(me_))
        votes += 1;

    return votes;
}

int raft_vote(raft_server_t* me_, raft_node_t* node)
{
    return raft_vote_for_nodeid(me_, node ? raft_node_get_id(node) : -1);
}

int raft_vote_for_nodeid(raft_server_t* me_, const raft_node_id_t nodeid)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (me->cb.persist_vote) {
        int e = me->cb.persist_vote(me_, me->udata, nodeid);
        if (0 != e)
            return e;
    }
    me->voted_for = nodeid;
    return 0;
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
    if (!raft_is_apply_allowed(me_))
        return 0;

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

void raft_offer_log(raft_server_t* me_, raft_entry_t* ety, const raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!raft_entry_is_cfg_change(ety))
        return;

    raft_node_id_t node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, idx);
    raft_node_t* node = raft_get_node(me_, node_id);
    int is_self = node_id == raft_get_nodeid(me_);

    switch (ety->type)
    {
        case RAFT_LOGTYPE_ADD_NONVOTING_NODE:
            if (!is_self)
            {
                if (node && !raft_node_is_active(node))
                {
                    raft_node_set_active(node, 1);
                }
                else if (!node)
                {
                    node = raft_add_non_voting_node_internal(me_, ety, NULL, node_id, is_self);
                    assert(node);
                }
            }
            break;

        case RAFT_LOGTYPE_ADD_NODE:
            node = raft_add_node_internal(me_, ety, NULL, node_id, is_self);
            assert(node);
            assert(raft_node_is_voting(node));
            break;

        case RAFT_LOGTYPE_DEMOTE_NODE:
            if (node)
                raft_node_set_voting(node, 0);
            break;

        case RAFT_LOGTYPE_REMOVE_NODE:
            if (node)
                raft_node_set_active(node, 0);
            break;

        default:
            assert(0);
    }
}

void raft_pop_log(raft_server_t* me_, raft_entry_t* ety, const raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!raft_entry_is_cfg_change(ety))
        return;

    raft_node_id_t node_id = me->cb.log_get_node_id(me_, raft_get_udata(me_), ety, idx);

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
            raft_node_t* node = raft_get_node(me_, node_id);
            raft_node_set_active(node, 1);
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

int raft_poll_entry(raft_server_t* me_, raft_entry_t **ety)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    int e = log_poll(me->log, (void*)ety);
    if (e != 0)
        return e;
    assert(*ety != NULL);

    return 0;
}

raft_index_t raft_get_first_entry_idx(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    assert(0 < raft_get_current_idx(me_));

    if (me->snapshot_last_idx == 0)
        return 1;

    return me->snapshot_last_idx;
}

raft_index_t raft_get_num_snapshottable_logs(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (raft_get_log_count(me_) <= 1)
        return 0;
    return raft_get_commit_idx(me_) - log_get_base(me->log);
}

int raft_begin_snapshot(raft_server_t *me_, int flags)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (raft_get_num_snapshottable_logs(me_) == 0)
        return -1;

    raft_index_t snapshot_target = raft_get_commit_idx(me_);
    if (!snapshot_target || snapshot_target == 0)
        return -1;

    raft_entry_t* ety = raft_get_entry_from_idx(me_, snapshot_target);
    if (!ety)
        return -1;

    /* we need to get all the way to the commit idx */
    int e = raft_apply_all(me_);
    if (e != 0)
        return e;

    assert(raft_get_commit_idx(me_) == raft_get_last_applied_idx(me_));

    raft_set_snapshot_metadata(me_, ety->term, snapshot_target);
    me->snapshot_in_progress = 1;
    me->snapshot_flags = flags;

    __raft_console_log(me_, NULL,
        "begin snapshot sli:%d slt:%d slogs:%d\n",
        me->snapshot_last_idx,
        me->snapshot_last_term,
        raft_get_num_snapshottable_logs(me_));

    return 0;
}

int raft_cancel_snapshot(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!me->snapshot_in_progress)
        return -1;

    me->snapshot_last_idx = me->saved_snapshot_last_idx;
    me->snapshot_last_term = me->saved_snapshot_last_term;

    me->snapshot_in_progress = 0;

    return 0;
}

int raft_end_snapshot(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (!me->snapshot_in_progress || me->snapshot_last_idx == 0)
        return -1;

    assert(raft_get_num_snapshottable_logs(me_) != 0);
    assert(me->snapshot_last_idx == raft_get_commit_idx(me_));

    /* If needed, remove compacted logs */
    raft_index_t i = 0, end = raft_get_num_snapshottable_logs(me_);
    for (; i < end; i++)
    {
        raft_entry_t* _ety;
        int e = raft_poll_entry(me_, &_ety);
        if (e != 0)
            return -1;
    }

    me->snapshot_in_progress = 0;

    __raft_console_log(me_, NULL,
        "end snapshot base:%d commit-index:%d current-index:%d\n",
        log_get_base(me->log),
        raft_get_commit_idx(me_),
        raft_get_current_idx(me_));

    if (!raft_is_leader(me_))
        return 0;

    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];

        if (me->node == node || !raft_node_is_active(node))
            continue;

        raft_index_t next_idx = raft_node_get_next_idx(node);

        /* figure out if the client needs a snapshot sent */
        if (0 < me->snapshot_last_idx && next_idx < me->snapshot_last_idx)
        {
            if (me->cb.send_snapshot)
                me->cb.send_snapshot(me_, me->udata, node);
        }
    }

    return 0;
}

int raft_begin_load_snapshot(
    raft_server_t *me_,
    raft_term_t last_included_term,
    raft_index_t last_included_index)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;

    if (last_included_index == -1)
        return -1;

    if (last_included_index == 0 || last_included_term == 0)
        return -1;

    /* loading the snapshot will break cluster safety */
    if (last_included_index < me->last_applied_idx)
        return -1;

    /* snapshot was unnecessary */
    if (last_included_index < raft_get_current_idx(me_))
        return -1;

    if (last_included_term == me->snapshot_last_term && last_included_index == me->snapshot_last_idx)
        return RAFT_ERR_SNAPSHOT_ALREADY_LOADED;

    me->current_term = last_included_term;
    me->voted_for = -1;
    raft_set_state((raft_server_t*)me, RAFT_STATE_FOLLOWER);
    me->current_leader = NULL;

    log_load_from_snapshot(me->log, last_included_index, last_included_term);

    if (raft_get_commit_idx(me_) < last_included_index)
        raft_set_commit_idx(me_, last_included_index);

    me->last_applied_idx = last_included_index;
    raft_set_snapshot_metadata(me_, last_included_term, me->last_applied_idx);

    /* remove all nodes but self */
    int i, my_node_by_idx = 0;
    for (i = 0; i < me->num_nodes; i++)
    {
        if (raft_get_nodeid(me_) == raft_node_get_id(me->nodes[i]))
            my_node_by_idx = i;
        else
            raft_node_set_active(me->nodes[i], 0);
    }

    /* this will be realloc'd by a raft_add_node */
    me->nodes[0] = me->nodes[my_node_by_idx];
    me->num_nodes = 1;

    __raft_console_log(me_, NULL,
        "loaded snapshot sli:%d slt:%d slogs:%d\n",
        me->snapshot_last_idx,
        me->snapshot_last_term,
        raft_get_num_snapshottable_logs(me_));

    return 0;
}

int raft_end_load_snapshot(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    int i;

    /* Set nodes' voting status as committed */
    for (i = 0; i < me->num_nodes; i++)
    {
        raft_node_t* node = me->nodes[i];
        raft_node_set_voting_committed(node, raft_node_is_voting(node));
        raft_node_set_addition_committed(node, 1);
        if (raft_node_is_voting(node))
            raft_node_set_has_sufficient_logs(node);
    }

    return 0;
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
    raft_randomize_election_timeout(me_);
}

void raft_set_request_timeout(raft_server_t* me_, int millisec)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->request_timeout = millisec;
}

raft_node_id_t raft_get_nodeid(raft_server_t* me_)
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
        if (raft_node_is_active(me->nodes[i]) && raft_node_is_voting(me->nodes[i]))
            num++;
    return num;
}

int raft_get_timeout_elapsed(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->timeout_elapsed;
}

raft_index_t raft_get_log_count(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_count(me->log);
}

int raft_get_voted_for(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return me->voted_for;
}

int raft_set_current_term(raft_server_t* me_, const raft_term_t term)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (me->current_term < term)
    {
        int voted_for = -1;
        if (me->cb.persist_term)
        {
            int e = me->cb.persist_term(me_, me->udata, term, voted_for);
            if (0 != e)
                return e;
        }
        me->current_term = term;
        me->voted_for = voted_for;
    }
    return 0;
}

raft_term_t raft_get_current_term(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->current_term;
}

raft_index_t raft_get_current_idx(raft_server_t* me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return log_get_current_idx(me->log);
}

void raft_set_commit_idx(raft_server_t* me_, raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    assert(me->commit_idx <= idx);
    assert(idx <= raft_get_current_idx(me_));
    me->commit_idx = idx;
}

void raft_set_last_applied_idx(raft_server_t* me_, raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->last_applied_idx = idx;
}

raft_index_t raft_get_last_applied_idx(raft_server_t* me_)
{
    return ((raft_server_private_t*)me_)->last_applied_idx;
}

raft_index_t raft_get_commit_idx(raft_server_t* me_)
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

raft_node_t* raft_get_node(raft_server_t *me_, raft_node_id_t nodeid)
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

raft_node_t* raft_get_node_from_idx(raft_server_t* me_, const raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    return me->nodes[idx];
}

raft_node_id_t raft_get_current_leader(raft_server_t* me_)
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

raft_term_t raft_get_last_log_term(raft_server_t* me_)
{
    raft_index_t current_idx = raft_get_current_idx(me_);
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

int raft_snapshot_is_in_progress(raft_server_t *me_)
{
    return ((raft_server_private_t*)me_)->snapshot_in_progress;
}

int raft_is_apply_allowed(raft_server_t* me_)
{
    return (!raft_snapshot_is_in_progress(me_) ||
            (((raft_server_private_t*)me_)->snapshot_flags & RAFT_SNAPSHOT_NONBLOCKING_APPLY));
}

raft_entry_t *raft_get_last_applied_entry(raft_server_t *me_)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    if (raft_get_last_applied_idx(me_) == 0)
        return NULL;
    return log_get_at_idx(me->log, raft_get_last_applied_idx(me_));
}

raft_index_t raft_get_snapshot_last_idx(raft_server_t *me_)
{
    return ((raft_server_private_t*)me_)->snapshot_last_idx;
}

raft_term_t raft_get_snapshot_last_term(raft_server_t *me_)
{
    return ((raft_server_private_t*)me_)->snapshot_last_term;
}

void raft_set_snapshot_metadata(raft_server_t *me_, raft_term_t term, raft_index_t idx)
{
    raft_server_private_t* me = (raft_server_private_t*)me_;
    me->saved_snapshot_last_term = me->snapshot_last_term;
    me->saved_snapshot_last_idx = me->snapshot_last_idx;
    me->snapshot_last_term = term;
    me->snapshot_last_idx = idx;
}
#endif /* RAFT_AMALGAMATIONE_SH */
