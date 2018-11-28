/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2013 President and Fellows of Harvard College
 * Copyright (c) 2012-2013 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Masstree LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Masstree LICENSE file; the license in that file
 * is legally binding.
 */
#ifndef KVC_HH
#define KVC_HH 1
#include "kvproto.hh"
#include "kvrow.hh"
#include "json.hh"
#include "msgpack.hh"
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <string>
#include <queue>
#include <vector>

class KVConn {
  public:
    KVConn(const char *server, int port, int target_core = -1)
        : inbuf_(new char[inbufsz]), inbufpos_(0), inbuflen_(0),
          j_(Json::make_array()) {
        struct hostent *ent = gethostbyname(server);
        always_assert(ent);
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        always_assert(fd > 0);
        fdtoclose_ = fd;
        int yes = 1;
        always_assert(fd >= 0);
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(port);
        memcpy(&sin.sin_addr.s_addr, ent->h_addr, ent->h_length);
        int r = connect(fd, (const struct sockaddr *)&sin, sizeof(sin));
        if (r) {
            perror("connect");
            exit(EXIT_FAILURE);
        }

        infd_ = fd;
        out_ = new_kvout(fd, 64*1024);
        handshake(target_core);
    }
    KVConn(int fd, bool tcp)
        : inbuf_(new char[inbufsz]), inbufpos_(0), inbuflen_(0), infd_(fd),
          j_(Json::make_array()) {
        out_ = new_kvout(fd, 64*1024);
        fdtoclose_ = -1;
        if (tcp)
            handshake(-1);
    }
    ~KVConn() {
        if (fdtoclose_ >= 0)
            close(fdtoclose_);
        free_kvout(out_);
        delete[] inbuf_;
        for (auto x : oldinbuf_)
            delete[] x;
    }
    void sendgetwhole(Str key, unsigned seq) {
        j_.resize(3);
        j_[0] = seq;
        j_[1] = Cmd_Get;
        j_[2] = String::make_stable(key);
        send();
    }
    void sendgetcol(Str key, int col, unsigned seq) {
        j_.resize(4);
        j_[0] = seq;
        j_[1] = Cmd_Get;
        j_[2] = String::make_stable(key);
        j_[3] = col;
        send();
    }
    void sendget(Str key, const std::vector<unsigned>& f, unsigned seq) {
        j_.resize(4);
        j_[0] = seq;
        j_[1] = Cmd_Get;
        j_[2] = String::make_stable(key);
        j_[3] = Json(f.begin(), f.end());
        send();
    }

    void sendputcol(Str key, int col, Str val, unsigned seq) {
        j_.resize(5);
        j_[0] = seq;
        j_[1] = Cmd_Put;
        j_[2] = String::make_stable(key);
        j_[3] = col;
        j_[4] = String::make_stable(val);
        send();
    }
    void sendputwhole(Str key, Str val, unsigned seq) {
        j_.resize(3);
        j_[0] = seq;
        j_[1] = Cmd_Replace;
        j_[2] = String::make_stable(key);
        j_[3] = String::make_stable(val);
        send();
    }
    void sendremove(Str key, unsigned seq) {
        j_.resize(3);
        j_[0] = seq;
        j_[1] = Cmd_Remove;
        j_[2] = String::make_stable(key);
        send();
    }

    void sendscanwhole(Str firstkey, int numpairs, unsigned seq) {
        j_.resize(4);
        j_[0] = seq;
        j_[1] = Cmd_Scan;
        j_[2] = String::make_stable(firstkey);
        j_[3] = numpairs;
        send();
    }
    void sendscan(Str firstkey, const std::vector<unsigned>& f,
                  int numpairs, unsigned seq) {
        j_.resize(5);
        j_[0] = seq;
        j_[1] = Cmd_Scan;
        j_[2] = String::make_stable(firstkey);
        j_[3] = numpairs;
        j_[4] = Json(f.begin(), f.end());
        send();
    }

    void checkpoint(int childno) {
	always_assert(childno == 0);
        fprintf(stderr, "asking for a checkpoint\n");
        j_.resize(2);
        j_[0] = 0;
        j_[1] = Cmd_Checkpoint;
        send();
        flush();

        printf("sent\n");
        (void) receive();
    }

    void flush() {
        kvflush(out_);
    }

    int check(int tryhard) {
        if (inbufpos_ == inbuflen_ && tryhard)
            hard_check(tryhard);
        return inbuflen_ - inbufpos_;
    }

    const Json& receive() {
        while (!parser_.done() && check(2))
            inbufpos_ += parser_.consume(inbuf_ + inbufpos_,
                                         inbuflen_ - inbufpos_,
                                         String::make_stable(inbuf_, inbufsz));
        if (parser_.success() && parser_.result().is_a())
            parser_.reset();
        else
            parser_.result() = Json();
        return parser_.result();
    }

  private:
    enum { inbufsz = 64 * 1024, inbufrefill = 56 * 1024 };
    char* inbuf_;
    int inbufpos_;
    int inbuflen_;
    std::vector<char*> oldinbuf_;
    int infd_;

    struct kvout *out_;

    Json j_;
    msgpack::streaming_parser parser_;

    int fdtoclose_;
    int partition_;

    void handshake(int target_core) {
        j_.resize(3);
        j_[0] = 0;
        j_[1] = Cmd_Handshake;
        j_[2] = Json::make_object().set("core", target_core)
            .set("maxkeylen", MASSTREE_MAXKEYLEN);
        send();
        kvflush(out_);

        const Json& result = receive();
        if (!result.is_a()
            || result[1] != Cmd_Handshake + 1
            || !result[2]) {
            fprintf(stderr, "Incompatible kvdb protocol\n");
            exit(EXIT_FAILURE);
        }
        partition_ = result[3].as_i();
    }
    inline void send() {
        msgpack::unparse(*out_, j_);
    }
    void hard_check(int tryhard);
};

#endif
