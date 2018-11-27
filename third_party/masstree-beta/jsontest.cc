// -*- c-basic-offset: 4 -*-
/*
 * jsontest.{cc,hh} -- regression tests for Json
 * Eddie Kohler
 *
 * Copyright (c) 2012-2014 Eddie Kohler
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */

#include "json.hh"
#include <unordered_map>
using namespace lcdf;

#define CHECK(x) do { if (!(x)) { std::cerr << __FILE__ << ":" << __LINE__ << ": test '" << #x << "' failed\n"; exit(1); } } while (0)
#define CHECK_JUP(x, str) do { if ((x).unparse() != (str)) { std::cerr << __FILE__ << ":" << __LINE__ << ": '" #x "' is '" << (x) << "', not '" << (str) << "'\n"; exit(1); } } while (0)


#if 0
template <typename T> void incr(T& x) __attribute__((noinline));
template <typename T>
void incr(T& x) {
    ++x;
}
#endif

void benchmark_parse() {
    std::vector<String> parse_examples;
    parse_examples.push_back("{}");
    parse_examples.push_back("{\"foo\":\"bar\",\"baz\":\"flim\"}");
    parse_examples.push_back("[1,2,3,4,5]");
    parse_examples.push_back("[]");
    parse_examples.push_back("[{},{\"b\":[]}]");
    Json j;
#if 0
    for (int i = 0; i < 10000000; ++i)
        j.assign_parse(parse_examples[rand() % parse_examples.size()]);
#else
    using std::swap;
    Json::streaming_parser jsp;
    for (int i = 0; i < 10000000; ++i) {
        jsp.reset();
        swap(jsp.result(), j);
        const String& str = parse_examples[rand() % parse_examples.size()];
        jsp.consume(str.begin(), str.end(), str, true);
    }
#endif
    exit(0);
}

int main(int argc, char** argv) {
    (void) argc, (void) argv;
    //benchmark_parse();

    Json j;
    CHECK(j.empty());
    CHECK(!j);

    j = Json::make_object();
    CHECK(j.empty());
    CHECK(j);

    j.set("foo", "bar");
    CHECK(j["foo"]);
    CHECK(j["foo"].to_s() == "bar");
    CHECK(j.size() == 1);

    j.set("baz", "flim");
    CHECK(j.size() == 2);
    CHECK(j.unparse() == "{\"foo\":\"bar\",\"baz\":\"flim\"}");

    j.erase("foo");
    CHECK(j.size() == 1);

    j.assign_parse("2");
    CHECK(j == 2);

    j.assign_parse("null");
    CHECK(j == Json());

    j.assign_parse("\"a\"");
    CHECK(j == "a");

    j.assign_parse("[1,2]");
    CHECK(j.unparse() == "[1,2]");

    j.assign_parse("[[[]],{\"a\":{}}]");
    CHECK(j.unparse() == "[[[]],{\"a\":{}}]");

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
    CHECK(j["x22"]["time"] == "Tue Feb  7 20:20:33 2012");
    CHECK(j["x22"]["cores"] == 2);
    {
        Json::object_iterator it = j.obegin();
        CHECK(it.key() == "x22");
        ++it;
        CHECK(it.key() == "x23");
        ++it;
        CHECK(it.key() == "x24");
        ++it;
        CHECK(it.key() == "b");
        ++it;
        CHECK(it.key() == "c");
    }

    {
        Json jcopy = j;
        CHECK(j.size() == 5);
        int count = 0;
        for (Json::iterator it = jcopy.begin(); it != jcopy.end(); ++it) {
            it->second = Json();
            ++count;
        }
        CHECK(!jcopy["x22"]);
        CHECK(j["x22"]["cores"] == 2);
        CHECK(count == jcopy.size());
    }

    CHECK(!j["x49"]);
    CHECK(j.size() == 5);
    CHECK(!j["x49"]["45"][2]["a"]);
    CHECK(j.size() == 5);
    j["x49"]["45"][2]["a"] = 1;
    CHECK(j.size() == 6);
    CHECK(j["x22"].is_object() && j.get("x22").is_object());
    CHECK(j["x23"].is_object() && j.get("x23").is_object());
    CHECK(j["b"].is_string() && j.get("b").is_string());
    CHECK(j["x49"].is_object() && j.get("x49").is_object());
    CHECK(j["x49"]["45"].is_array());
    CHECK(j["x49"]["45"].size() == 3 && j["x49"]["45"][2].is_object());

    j = Json::make_object();
    j["a"] = j["b"];
    CHECK(j.size() == 1);
    CHECK(j["a"].is_null());
    CHECK(j.count("a") == 1);

    Json k = Json::make_array();
    {
        CHECK(k.size() == 0);
        Json::array_iterator it = k.abegin();
        CHECK(!it.live());
    }
    j = Json::make_object();
    j["a"] = k[2];
    CHECK(j.size() == 1);
    CHECK(k.size() == 0);
    CHECK(j["a"].is_null());
    CHECK(j.count("a") == 1);

    j = Json(1);
    CHECK(j.get("a").is_null());

    j.assign_parse("{\"a\":1,\"b\":true,\"c\":\"\"}");
    {
        int i = 0;
        bool b = false;
        String s;
        CHECK(j.get("a", i));
        CHECK(i == 1);
        CHECK(j.get("a", i).get("b", b));
        CHECK(b == true);
        CHECK(!j.get("a", s).status());
        CHECK(!j.get("a", s).status(b));
        CHECK(b == false);
        CHECK(j.get("a", k));
        CHECK(k == Json(1));
        CHECK(!j.get("cc", k));
    }

    j["a"] = Json(5);
    j.set("a", Json(5));
    j["b"] = Json::parse("[]");

    {
        Json j1 = Json::make_object(), j2 = Json::make_object();
        j1.set("a", j2); // stores a COPY of j2 in j1
        j2.set("b", 1);
        CHECK(j1.unparse() == "{\"a\":{}}");
        CHECK(j2.unparse() == "{\"b\":1}");
    }

    {
        Json j = Json::parse("{\"a\":true}");
        if (j["foo"]["bar"])
            CHECK(false);
        CHECK(j.unparse() == "{\"a\":true}");
        j["foo"]["bar"] = true;
        CHECK(j.unparse() == "{\"a\":true,\"foo\":{\"bar\":true}}");
        j["a"]["2"] = false;
        CHECK(j.unparse() == "{\"a\":{\"2\":false},\"foo\":{\"bar\":true}}");
        j[3] = true;
        CHECK(j.unparse() == "{\"a\":{\"2\":false},\"foo\":{\"bar\":true},\"3\":true}");
        CHECK(j[3] == Json(true));
        j = Json::parse("[1,2,3,4]");
        CHECK(j["2"] == Json(3));
        CHECK(j.unparse() == "[1,2,3,4]");
        j["a"] = true;
        CHECK(j.unparse() == "{\"0\":1,\"1\":2,\"2\":3,\"3\":4,\"a\":true}");
        CHECK(j["2"] == Json(3));
    }

    {
        Json j = Json::parse("{}");
        j.set("foo", String::make_out_of_memory()).set(String::make_out_of_memory(), 2);
        CHECK(j.unparse() == "{\"foo\":\"\360\237\222\243ENOMEM\360\237\222\243\",\"\360\237\222\243ENOMEM\360\237\222\243\":2}");
        j.clear();
        CHECK(j.unparse() == "{}");
        CHECK(j.get("foo") == Json());
    }

    {
        Json j = Json::array(1, 2, 3, 4, 5, 6, 7, 8);
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8]");
        Json jcopy = j;
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8]");
        CHECK(jcopy.unparse() == "[1,2,3,4,5,6,7,8]");
        j.push_back(9);
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8,9]");
        CHECK(jcopy.unparse() == "[1,2,3,4,5,6,7,8]");
        jcopy.push_back(10);
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8,9]");
        CHECK(jcopy.unparse() == "[1,2,3,4,5,6,7,8,10]");
    }

    {
        unsigned long s = 77;
        Json j = Json::array(0, 0, 0);
        Json k = Json::array(0, s, "foo");
        CHECK(j[1].is_i());
        CHECK(k[1].is_u());
        j[1] = k[1];
        CHECK(j[1].is_u());
    }

#if 0
    {
        Json j = Json::array(1, 2, 3);
        //int j[3] = {1, 2, 3};
        for (int i = 0; i < 1000000; ++i)
            incr(j[1].value());
        std::cout << j << "\n";
    }
#endif

    {
        Json::streaming_parser jsp;
        const uint8_t* x = reinterpret_cast<const uint8_t*>("\"abcdef\"");

        const uint8_t* r = jsp.consume(x, x + 8, String());
        CHECK(r == x + 8);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"abcdef\"");

        x = reinterpret_cast<const uint8_t*>("\"\\\"\\\\fartbox\" amksdnsndfa");
        jsp.reset();
        r = jsp.consume(x, x + 9, String());
        CHECK(r == x + 9);
        CHECK(!jsp.done());
        r = jsp.consume(x + 9, x + 17, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\\\"\\\\fartbox\"");

        jsp.reset();
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\\\"\\\\fartbox\"");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("\"\\uD834\\uDD1E\"f");
        r = jsp.consume(x, x + 15, String());
        CHECK(r == x + 14);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\xF0\x9D\x84\x9E\"");

        jsp.reset();
        r = jsp.consume(x, x + 2, String());
        CHECK(r == x + 2);
        r = jsp.consume(x + 2, x + 4, String());
        CHECK(r == x + 4);
        r = jsp.consume(x + 4, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 8, String());
        CHECK(r == x + 8);
        r = jsp.consume(x + 8, x + 10, String());
        CHECK(r == x + 10);
        r = jsp.consume(x + 10, x + 15, String());
        CHECK(r == x + 14);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\xF0\x9D\x84\x9E\"");

        x = reinterpret_cast<const uint8_t*>("\"\xF0\x9D\x84\x9E\" fart");
        jsp.reset();
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 5, x + 7, String());
        CHECK(r == x + 5);
        CHECK(jsp.error());

        jsp.reset();
        r = jsp.consume(x, x + 7, String());
        CHECK(r == x + 6);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\xF0\x9D\x84\x9E\"");

        jsp.reset();
        r = jsp.consume(x, x + 2, String());
        CHECK(r == x + 2);
        r = jsp.consume(x + 2, x + 4, String());
        CHECK(r == x + 4);
        r = jsp.consume(x + 4, x + 7, String());
        CHECK(r == x + 6);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"\xF0\x9D\x84\x9E\"");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{}");
        r = jsp.consume(x, x + 2, String());
        CHECK(r == x + 2);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("\"ab cde\" ");
        r = jsp.consume(x, x + 9, String());
        CHECK(r == x + 8);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "\"ab cde\"");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"a\":\"b\"}");
        r = jsp.consume(x, x + 9, String());
        CHECK(r == x + 9);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"a\":\"b\"}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("[]");
        r = jsp.consume(x, x + 2, String());
        CHECK(r == x + 2);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "[]");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("[\"a\",\"b\"]");
        r = jsp.consume(x, x + 9, String());
        CHECK(r == x + 9);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "[\"a\",\"b\"]");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("[[\"a\"],[[\"b\"]]]");
        r = jsp.consume(x, x + 15, String());
        CHECK(r == x + 15);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "[[\"a\"],[[\"b\"]]]");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("[[],[[]]]");
        r = jsp.consume(x, x + 9, String());
        CHECK(r == x + 9);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "[[],[[]]]");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\":\"def\"}");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":\"def\"}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\":true }");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":true}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\": null}");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":null}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\":false}");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":false}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\": -13 }");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":-13}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("{\"abc\":0    }");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 13, String());
        CHECK(r == x + 13);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "{\"abc\":0}");

        jsp.reset();
        x = reinterpret_cast<const uint8_t*>("[0,1,2,3,4e5,10.2]");
        r = jsp.consume(x, x + 3, String());
        CHECK(r == x + 3);
        r = jsp.consume(x + 3, x + 6, String());
        CHECK(r == x + 6);
        r = jsp.consume(x + 6, x + 9, String());
        CHECK(r == x + 9);
        r = jsp.consume(x + 9, x + 12, String());
        CHECK(r == x + 12);
        r = jsp.consume(x + 12, x + 18, String());
        CHECK(r == x + 18);
        CHECK(jsp.success());
        CHECK(jsp.result().unparse() == "[0,1,2,3,400000,10.2]");
    }

    {
        unsigned long s = 77;
        Json j = Json::array(0, s, "foo");
        CHECK(j.is_a());
        CHECK(j[1].is_u());
        CHECK(j[1] == s);
        CHECK(j[1] == 77);

        j = Json::array((uint64_t) -1, (int64_t) -1,
                        (uint64_t) 1, (int64_t) 1,
                        (uint64_t) 2, (int64_t) 2);
        CHECK(j[0] != j[1]);
        CHECK(j[2] == j[3]);
        CHECK(j[4] == j[5]);
        CHECK(j[0] != j[2]);
        CHECK(j[2] != j[4]);
        CHECK_JUP(j, "[18446744073709551615,-1,1,1,2,2]");
    }

    {
        std::vector<int> v = {1, 2, 3, 4, 5};
        Json j(v.begin(), v.end());
        CHECK(j.unparse() == "[1,2,3,4,5]");
    }

    {
        std::unordered_map<String, String> h;
        h["a"] = "b";
        h["c"] = "d";
        h["x"] = "e";
        Json j(h.begin(), h.end());
        CHECK(j.is_o());
        CHECK(j.size() == 3);
        CHECK(j["a"] == "b");
        CHECK(j["c"] == "d");
        CHECK(j["x"] == "e");
    }

    {
        Json j = Json::array(1, 2, 3, 4, 5, 6, 7, 8);
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8]");
        Json jcopy = j;
        CHECK(j.unparse() == "[1,2,3,4,5,6,7,8]");
        CHECK(jcopy.unparse() == "[1,2,3,4,5,6,7,8]");
        auto it = j.erase(j.abegin() + 4);
        CHECK_JUP(j, "[1,2,3,4,6,7,8]");
        CHECK_JUP(jcopy, "[1,2,3,4,5,6,7,8]");
        CHECK(it == j.abegin() + 4);
        it = j.erase(j.aend(), j.aend());
        CHECK_JUP(j, "[1,2,3,4,6,7,8]");
        CHECK(it == j.aend());
        it = j.erase(j.abegin(), j.abegin());
        CHECK_JUP(j, "[1,2,3,4,6,7,8]");
        CHECK(it == j.abegin());
        it = j.erase(j.abegin(), j.abegin() + 3);
        CHECK_JUP(j, "[4,6,7,8]");
        CHECK(it == j.abegin());
    }

    {
        Json j((uint64_t) 1 << 63);
        CHECK(j.is_u());
        CHECK(j.unparse() == "9223372036854775808");
        j = Json::parse("9223372036854775808");
        CHECK(j.is_u());
        CHECK(j.to_u() == (uint64_t) 1 << 63);
    }

    {
        Json j = Json::object("a", 1, "b", Json(2), "c", "9137471");
        CHECK(j.unparse() == "{\"a\":1,\"b\":2,\"c\":\"9137471\"}");
    }

    {
        Json a = Json::parse("[{\"a\":1},{\"b\":2},{\"c\":3},{\"d\":4}]");
        Json b = Json::array(a[1], a[3], a[2], a[0]);
        CHECK(&a[0]["a"].cvalue() == &b[3]["a"].cvalue());
        b.push_back(a[5]);
        CHECK(b[4] == Json());
        CHECK(a.size() == 4);
        b = Json::object("a5", a[5], "a3", a[3]);
        CHECK(b.unparse() == "{\"a5\":null,\"a3\":{\"d\":4}}");
        CHECK(a.size() == 4);
        b.set_list("a6", a[6], "a2", a[2]);
        CHECK(b.unparse() == "{\"a5\":null,\"a3\":{\"d\":4},\"a6\":null,\"a2\":{\"c\":3}}");
        CHECK(a.size() == 4);
    }

    {
        Json a = Json::parse("[{\"a\":\"\\\"\\\\\\/\"}]");
        CHECK(a[0]["a"] == "\"\\/");
        CHECK(a.unparse() == "[{\"a\":\"\\\"\\\\\\/\"}]");
    }

    CHECK(String("\\").encode_json() == "\\\\");
    CHECK(String("\011\002\xE2\x80\xA9\xE2\x80\xAA").encode_json() == "\\t\\u0002\\u2029\xE2\x80\xAA");
    CHECK(String("a").encode_uri_component() == "a");
    CHECK(String(" ").encode_uri_component() == "%20");
    CHECK(String("Ab -Ef").encode_uri_component() == "Ab%20-Ef");

    std::cout << "All tests pass!\n";
    return 0;
}
