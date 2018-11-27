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
#define FORCE_ENABLE_ASSERTIONS 1
#undef NDEBUG
#include "compiler.hh"
#include <stdlib.h>
#include <algorithm>
#include <sys/time.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "kvrandom.hh"
#include "string_slice.hh"
#include "kpermuter.hh"
#include "value_bag.hh"
#include "value_string.hh"
#include "json.hh"
using namespace lcdf;

uint8_t xb[100];
uint16_t xw[100];
uint32_t xl[100];

struct fake_threadinfo {
    static void* allocate(size_t sz, memtag = memtag_none) {
	return new char[sz];
    }
    static void deallocate(void* p, size_t, memtag = memtag_none) {
	delete[] reinterpret_cast<char*>(p);
    }
};

// also check bitfield layout
union myunion {
    uint32_t x;
    struct {
	unsigned one : 1;
	unsigned two : 2;
	unsigned three : 3;
    };
} xunion;

void test_atomics() {
    uint32_t x;

    xunion.one = 1;
    assert(xunion.x == 1);

    x = xchg(&xb[0], 2);
    assert(x == 0 && xb[0] == 2 && xb[1] == 0 && xb[2] == 0 && xb[3] == 0);
    x = xchg(&xb[0], 3);
    assert(x == 2 && xb[0] == 3 && xb[1] == 0 && xb[2] == 0 && xb[3] == 0);

    x = xchg(&xw[0], 1);
    assert(x == 0);

    x = fetch_and_add(&xl[0], 100);
    assert(x == 0 && xl[0] == 100);
    x = cmpxchg(&xl[0], 100, 200);
    assert(x == 100 && xl[0] == 200);
    if (bool_cmpxchg(&xl[0], 200, 300))
	xl[1] = 400;
    assert(xl[0] == 300 && xl[1] == 400);
    if (bool_cmpxchg(&xl[0], 400, 100))
	xl[1] = 500;
    assert(xl[0] == 300 && xl[1] == 400);
}

void test_psdes_nr() {
    kvrandom_psdes_nr r;
    union {
	uint32_t u;
	float f;
    } u;

    r.reset(1);
    u.u = r.next();
    assert(u.u == 0x509C0C23U);
    u.u = r[99];
    assert(u.u == 0xA66CB41A);

    r.reset(99);
    u.u = r.next();
    assert(u.u == 0x64300984);
    u.u = r[99];
    assert(u.u == 0x59BA89EB);
}

template <typename T>
void time_random() {
    T r;
    uint32_t x = 0;
    for (int i = 0; i < 1000000000; ++i)
	x ^= r.next();
    assert(x != 0);
}

template <typename T>
void time_keyslice() {
    char *b = (char *) malloc(4096);
    FILE *f = fopen("/dev/urandom", "rb");
    ssize_t rr = fread(b, 1, 4096, f);
    assert(rr == 4096);
    fclose(f);
    T x = 0;
    kvrandom_lcg_nr r;
    x ^= string_slice<T>::make(b, 1);
    for (int i = 0; i < 1000000000; ++i)
	x ^= string_slice<T>::make(b + r.next() % 2048, r.next() % 16);
    assert(x);
}

void test_kpermuter() {
    typedef kpermuter<15> kpermuter_type;
    kpermuter_type k = kpermuter_type::make_empty();
    int i = k.insert_from_back(0);
    assert(k.size() == 1 && i == 0 && k[0] == 0);
    i = k.insert_from_back(0);
    assert(k.size() == 2 && i == 1 && k[0] == 1 && k[1] == 0);
    i = k.insert_from_back(2);
    assert(k.size() == 3 && i == 2 && k[0] == 1 && k[1] == 0 && k[2] == 2);
    k.remove(1);
    assert(k.size() == 2 && k[0] == 1 && k[1] == 2 && k[2] == 0);
    k.remove(1);
    assert(k.size() == 1 && k[0] == 1 && k[1] == 2 && k[2] == 0 && k[3] == 14);
    i = k.insert_from_back(0);
    assert(k.size() == 2 && i == 3 && k[0] == 3 && k[1] == 1 && k[2] == 2 && k[3] == 0 && k[4] == 14);
    k.insert_selected(0, 3);
    assert(k.size() == 3 && k[0] == 0 && k[1] == 3 && k[2] == 1 && k[3] == 2 && k[4] == 14);
    k.insert_selected(2, 11);
    assert(k.size() == 4 && k[0] == 0 && k[1] == 3 && k[2] == 7 && k[3] == 1 && k[4] == 2
	   && k[5] == 14 && k[11] == 8 && k[12] == 6);
    k.insert_selected(2, 14);
    assert(k.size() == 5 && k[0] == 0 && k[1] == 3 && k[2] == 4 && k[3] == 7 && k[4] == 1 && k[5] == 2
	   && k[6] == 14 && k[12] == 8 && k[13] == 6);
    k.exchange(0, 1);
    assert(k.size() == 5 && k[0] == 3 && k[1] == 0 && k[2] == 4 && k[3] == 7 && k[4] == 1 && k[5] == 2
	   && k[6] == 14 && k[12] == 8 && k[13] == 6);
    k.remove_to_back(2);
    assert(k.size() == 4 && k[0] == 3 && k[1] == 0 && k[2] == 7 && k[3] == 1 && k[4] == 2
	   && k[5] == 14 && k[11] == 8 && k[12] == 6 && k[14] == 4);
    assert(k.back() == 4);
    i = k.insert_from_back(2);
    assert(k.size() == 5 && k[0] == 3 && k[1] == 0 && k[2] == 4 && k[3] == 7 && k[4] == 1 && k[5] == 2
	   && k[6] == 14 && k[12] == 8 && k[13] == 6 && i == 4);
    k.exchange(0, 0);
    assert(k.size() == 5 && k[0] == 3 && k[1] == 0 && k[2] == 4 && k[3] == 7 && k[4] == 1 && k[5] == 2
	   && k[6] == 14 && k[12] == 8 && k[13] == 6 && i == 4);

    assert(find_lowest_zero_nibble(0x0120U) == 0);
    assert(find_lowest_zero_nibble(0x0123U) == 3);
    assert(find_lowest_zero_nibble(0x12345678U) == -1);

    kpermuter<14> ka = kpermuter<14>::make_empty();
    i = ka.insert_from_back(0);
    assert(ka.size() == 1 && i == 0 && ka[0] == 0);
    i = ka.insert_from_back(0);
    assert(ka.size() == 2 && i == 1 && ka[0] == 1 && ka[1] == 0);
    i = ka.insert_from_back(2);
    assert(ka.size() == 3 && i == 2 && ka[0] == 1 && ka[1] == 0 && ka[2] == 2);
    ka.remove_to_back(1);
    assert(ka.size() == 2 && ka[0] == 1 && ka[1] == 2 && ka.back() == 0);
}

void test_string_slice() {
    typedef string_slice<uint32_t> ss_type;
    assert(ss_type::make("a", 1) == ss_type::make("aaa", 1));
    assert(ss_type::make_sloppy("0123abcdef" + 4, 1)
	   == ss_type::make_sloppy("bcdea01293" + 4, 1));
    assert(ss_type::make_comparable("a", 1) < ss_type::make_comparable("b", 1));
    assert(ss_type::equals_sloppy("0123abcdef" + 4, "abcdea02345" + 5, 1));
    assert(ss_type::make_comparable("abcd", 4) < ss_type::make_comparable("abce", 4));
    assert(ss_type::make_comparable("abce", 4) > ss_type::make_comparable("abcd", 4));
    assert(ss_type::equals_sloppy("0123abcdef" + 4, "abcdeabcd5" + 5, 4));
    assert(!ss_type::equals_sloppy("0123abcdef" + 4, "abcdeabcd5" + 5, 5));
    assert(String("12345").find_right("") == 5);
    assert(String("12345").find_right("5") == 4);
    assert(String("12345").find_right("23") == 1);
    assert(String("12345", 0).find_right("23") == -1);
}

void test_string_bag() {
    fake_threadinfo ti;
    typedef value_bag<uint16_t> bag_t;
    bag_t eb;
    if (eb.size() > sizeof(bag_t))
	fprintf(stderr, "sizes are off: %zu vs. %zu\n", eb.size(), sizeof(bag_t));
    assert(eb.size() <= sizeof(bag_t));
    bag_t* b = eb.update(0, Str("A", 1), 1, ti);
    assert(b->row_string() == Str("\001\000\006\000\007\000A", 7));
    assert(b->ncol() == 1);
    assert(b->col(0) == Str("A", 1));

    bag_t* bx = bag_t::create1(Str("A", 1), 1, ti);
    assert(bx->row_string() == b->row_string());
    bx->deallocate(ti);

    bag_t *bb = b->update(1, Str("BB", 2), 2, ti);
    b->deallocate(ti);
    b = bb;
    assert(b->row_string() == Str("\002\000"
				  "\010\000\011\000\013\000"
				  "ABB", 013));
    assert(b->ncol() == 2);
    assert(b->col(0) == Str("A", 1));
    assert(b->col(1) == Str("BB", 2));

    bb = b->update(3, Str("CCC", 3), 3, ti);
    b->deallocate(ti);
    b = bb;
    assert(b->row_string() == Str("\004\000"
				  "\014\000\015\000\017\000\017\000\022\000"
				  "ABBCCC", 022));
    assert(b->ncol() == 4);
    assert(b->col(0) == Str("A", 1));
    assert(b->col(1) == Str("BB", 2));
    assert(b->col(2) == Str("", 0));
    assert(b->col(3) == Str("CCC", 3));

    bb = b->update(1, Str("bbb", 3), 4, ti);
    b->deallocate(ti);
    b = bb;
    assert(b->row_string() == Str("\004\000"
				  "\014\000\015\000\020\000\020\000\023\000"
				  "AbbbCCC", 023));
    assert(b->ncol() == 4);
    assert(b->col(0) == Str("A", 1));
    assert(b->col(1) == Str("bbb", 3));
    assert(b->col(2) == Str("", 0));
    assert(b->col(3) == Str("CCC", 3));

    bb = b->update(0, Str("a", 1), 4, ti);
    b->deallocate(ti);
    b = bb;
    assert(b->row_string() == Str("\004\000"
				  "\014\000\015\000\020\000\020\000\023\000"
				  "abbbCCC", 023));
    assert(b->ncol() == 4);
    assert(b->col(0) == Str("a", 1));
    assert(b->col(1) == Str("bbb", 3));
    assert(b->col(2) == Str("", 0));
    assert(b->col(3) == Str("CCC", 3));

    bb = b->update(1, Str("", 0), 4, ti);
    b->deallocate(ti);
    b = bb;
    assert(b->row_string() == Str("\004\000"
				  "\014\000\015\000\015\000\015\000\020\000"
				  "aCCC", 020));
    assert(b->ncol() == 4);
    assert(b->col(0) == Str("a", 1));
    assert(b->col(1) == Str("", 0));
    assert(b->col(2) == Str("", 0));
    assert(b->col(3) == Str("CCC", 3));

    b->deallocate(ti);
}

void test_json()
{
    Json j;
    assert(j.empty());
    assert(!j);

    j = Json::make_object();
    assert(j.empty());
    assert(j);

    j.set("foo", "bar");
    assert(j["foo"]);
    assert(j["foo"].to_s() == "bar");
    assert(j.size() == 1);

    j.set("baz", "flim");
    assert(j.size() == 2);
    assert(j.unparse() == "{\"foo\":\"bar\",\"baz\":\"flim\"}");

    j.erase("foo");
    assert(j.size() == 1);

    j.assign_parse("2");
    assert(j == 2);

    j.assign_parse("null");
    assert(j == Json());

    j.assign_parse("\"a\"");
    assert(j == "a");

    j.assign_parse("[1,2]");
    assert(j.unparse() == "[1,2]");

    j.assign_parse("[[[]],{\"a\":{}}]");
    assert(j.unparse() == "[[[]],{\"a\":{}}]");

    j = Json::parse("{\"x22\":{\n\
      \"git-revision\":\"ebbd3d4767847300f552b181a10bda57a926f554M\",\n\
      \"time\":\"Tue Feb  7 20:20:33 2012\",\n\
      \"machine\":\"rtshanks-laptop\",\n\
      \"cores\":2,\n\
      \"runs\":[\"x22\\/rw2\\/mb\\/0\"]\n\
    },\n\
    \"x23\":{\n\
      \"git-revision\":\"ebbd3d4767847300f552b181a10bda57a926f554M\",\n\
      \"time\":\"Tue Feb  7 20:31:05 2012\",\n\
      \"machine\":\"rtshanks-laptop\",\n\
      \"cores\":2,\n\
      \"runs\":[\"x23\\/rw2\\/mb\\/0\",\"x23\\/rw1\\/mb\\/0\",\"x23\\/rw3\\/mb\\/0\"]\n\
    },\n\
    \"x24\":{\n\
      \"git-revision\":\"62e9970ca8ae9c6eebf2d71b7065ea694fb25282M\",\n\
      \"time\":\"Sat Feb 11 15:54:01 2012\",\n\
      \"machine\":\"rtshanks-laptop\",\n\
      \"cores\":2,\n\
      \"runs\":[\"x24\\/rw1\\/b\\/0\"]\n\
    },\"b\":\"c\",\"c\":\"d\"}");
    assert(j["x22"]["time"] == "Tue Feb  7 20:20:33 2012");
    assert(j["x22"]["cores"] == 2);
    {
	Json::object_iterator it = j.obegin();
	assert(it.key() == "x22");
	++it;
	assert(it.key() == "x23");
	++it;
	assert(it.key() == "x24");
	++it;
	assert(it.key() == "b");
	++it;
	assert(it.key() == "c");
    }

    {
	Json jcopy = j;
	assert(j.size() == 5);
	int count = 0;
	for (Json::iterator it = jcopy.begin(); it != jcopy.end(); ++it) {
	    it->second = Json();
	    ++count;
	}
	assert(!jcopy["x22"]);
	assert(j["x22"]["cores"] == 2);
	assert(count == jcopy.size());
    }

    assert(!j["x49"]);
    assert(j.size() == 5);
    assert(!j["x49"]["45"][2]["a"]);
    assert(j.size() == 5);
    j["x49"]["45"][2]["a"] = 1;
    assert(j.size() == 6);
    assert(j["x22"].is_object() && j.get("x22").is_object());
    assert(j["x23"].is_object() && j.get("x23").is_object());
    assert(j["b"].is_string() && j.get("b").is_string());
    assert(j["x49"].is_object() && j.get("x49").is_object());
    assert(j["x49"]["45"].is_array());
    assert(j["x49"]["45"].size() == 3 && j["x49"]["45"][2].is_object());

    j = Json::make_object();
    j["a"] = j["b"];
    assert(j.size() == 1);
    assert(j["a"].is_null());
    assert(j.count("a") == 1);

    j = Json::make_object();
    Json k = Json::make_array();
    j["a"] = k[2];
    assert(j.size() == 1);
    assert(k.size() == 0);
    assert(j["a"].is_null());
    assert(j.count("a") == 1);

    j = Json(1);
    assert(j.get("a").is_null());

    j.assign_parse("{\"a\":1,\"b\":true,\"c\":\"\"}");
    {
	int i = 0;
	bool b = false;
	String s;
	assert(j.get("a", i));
	assert(i == 1);
	assert(j.get("a", i).get("b", b));
	assert(b == true);
	assert(!j.get("a", s).status());
	assert(!j.get("a", s).status(b));
	assert(b == false);
	assert(j.get("a", k));
	assert(k == Json(1));
	assert(!j.get("cc", k));
    }

    j["a"] = Json(5);
    j.set("a", Json(5));
    j["b"] = Json::parse("[]");
}

void test_value_updates() {
    fake_threadinfo ti;
    typedef value_bag<uint16_t> bag_t;
    typedef value_string vstr_t;
    bag_t* eb = new(ti.allocate(sizeof(bag_t))) bag_t;
    vstr_t* strb = new(ti.allocate(sizeof(vstr_t))) vstr_t;

    Json bagupdate = Json::array(0, "ABC", 1, "def", 2, "EGHIJ", 3, "klm");

    Json strupdate = Json::array(vstr_t::make_index(0, 3), "ABC",
                                 vstr_t::make_index(3, 3), "def",
                                 vstr_t::make_index(6, 5), "EGHIJ",
                                 vstr_t::make_index(11, 3), "klm");

    {
        bag_t* eb2 = eb->update(bagupdate.array_data(), bagupdate.end_array_data(), 1, ti);
        eb->deallocate(ti);
        eb = eb2;
        assert(eb->col(0) == Str("ABC"));
        assert(eb->col(1) == Str("def"));
        assert(eb->col(2) == Str("EGHIJ"));
        assert(eb->col(3) == Str("klm"));
    }

    {
        vstr_t* strb2 = strb->update(strupdate.array_data(), strupdate.end_array_data(), 1, ti);
        strb->deallocate(ti);
        strb = strb2;
        assert(strb->col(vstr_t::make_index(0, 3)) == Str("ABC"));
        assert(strb->col(vstr_t::make_index(3, 3)) == Str("def"));
        assert(strb->col(vstr_t::make_index(6, 5)) == Str("EGHIJ"));
        assert(strb->col(vstr_t::make_index(11, 3)) == Str("klm"));
        assert(strb->col(0) == Str("ABCdefEGHIJklm"));
    }

    eb->deallocate(ti);
    strb->deallocate(ti);
}

int main(int, char *[])
{
    //test_atomics();
    //test_psdes_nr();
    //time_random<kvrandom_lcg_nr>();
    assert(iceil_log2(2) == 2);
    assert(iceil_log2(3) == 4);
    assert(ifloor_log2(2) == 2);
    assert(ifloor_log2(3) == 2);
    //time_keyslice<uint64_t>();
    test_kpermuter();
    test_string_slice();
    test_string_bag();
    test_json();
    test_value_updates();
    std::cout << "Tests complete!\n";
    return 0;
}
