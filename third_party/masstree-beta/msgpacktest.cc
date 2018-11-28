#include "msgpack.hh"
using namespace lcdf;

enum { status_ok, status_error, status_incomplete };

__attribute__((noreturn))
static void test_error(const char* file, int line,
                       const char* data, int len,
                       String division, String message) {
    std::cerr << file << ":" << line << " ("
              << String(data, data + len).printable() << ")"
              << (division ? " " : "") << division << ": "
              << message << "\n";
    exit(1);
}

static void onetest(const char* file, int line,
                    const char* data, int len,
                    String division, const char* take, int expected_take,
                    const char* unparse, int status,
                    msgpack::streaming_parser& a) {
    if (expected_take >= 0 && take != data + expected_take)
        test_error(file, line, data, len, division, "accept took " + String(take - data) + " chars, expected " + String(expected_take));

    if (status == status_ok && !a.success())
        test_error(file, line, data, len, division, "accept not done");
    if (status == status_error && !a.error())
        test_error(file, line, data, len, division, "accept not error");
    if (status == status_incomplete && a.done())
        test_error(file, line, data, len, division, "accept not incomplete");

    if (unparse && a.result().unparse() != unparse)
        test_error(file, line, data, len, division, "result was " + a.result().unparse() + "\n\texpected " + String(unparse));
}

static void test(const char* file, int line,
                 const char* data, int len, int expected_take,
                 const char* unparse, int status = status_ok) {
    assert(expected_take <= len);

    msgpack::streaming_parser a;
    const char* take;
    take = a.consume(data, data + len);
    onetest(file, line, data, len, "", take, expected_take, unparse, status, a);

    if (len > 1) {
        a.reset();
        take = data;
        while (take != data + len) {
            const char* x = a.consume(take, take + 1);
            if (x != take + (take < data + expected_take))
                test_error(file, line, data, len, "by 1s", "accept took unusual amount after " + String(x - data));
            ++take;
        }
        onetest(file, line, data, len, "by 1s", take, -1, unparse, status, a);
    }
}

#define TEST(...) test(__FILE__, __LINE__, ## __VA_ARGS__)

void check_correctness() {
    TEST("\0", 1, 1, "0");
    TEST("\xFF  ", 3, 1, "-1");
    TEST("\xC0  ", 3, 1, "null");
    TEST("\xC2  ", 3, 1, "false");
    TEST("\xC3  ", 3, 1, "true");
    TEST("\xD0\xEE", 2, 2, "-18");
    TEST("\x81\xA7" "compact\xC3", 11, 10, "{\"compact\":true}");
    TEST("\x81\x00\x81\xA7" "compact\xC3", 13, 12, "{\"0\":{\"compact\":true}}");
    TEST("\x82\x00\x81\xA7" "compact\xC3\xA1" "a\xC2", 16, 15, "{\"0\":{\"compact\":true},\"a\":false}");
    TEST("\x93\x00\x01\x02", 5, 4, "[0,1,2]");
    TEST("\x90     ", 5, 1, "[]");
    TEST("\xDC\x00\x00     ", 5, 3, "[]");
    TEST("\224\002\322\000\001\242\321\262p|00356|1000000000\245?!?#*\225\001\322\000\001\242\322\242t|\242t}\332\000Rt|<user_id:5>|<time:10>|<poster_id:5> s|<user_id>|<poster_id> p|<poster_id>|<time>",
         130, 32, "[2,107217,\"p|00356|1000000000\",\"?!?#*\"]", status_ok);
    TEST("\xCF\x80\0\0\0\0\0\0\0", 9, 9, "9223372036854775808");

    {
        msgpack::streaming_parser a;
        Json j = Json::array(0, 0, 0);
        swap(j, a.result());
        a.reset();
        a.consume("\x91\xC2", 2);
        assert(a.success() && a.result().unparse() == "[false]");
        a.reset();
        a.consume("\xC0", 1);
        assert(a.success() && a.result().unparse() == "null");
        a.reset();
        a.consume("\x82\xA7" "compact\xC3\x00\x00", 12);
        assert(a.success() && a.result().unparse() == "{\"compact\":true,\"0\":0}");
        a.reset();
        a.consume("\x82\xA7" "compact\xC3\x00\x00", 12);
        assert(a.success() && a.result().unparse() == "{\"compact\":true,\"0\":0}");
    }

    {
        StringAccum sa;
        msgpack::unparser<StringAccum> up(sa);
        up.clear();
        up << -32;
        assert(sa.take_string() == "\xE0");
        up.clear();
        up << -33;
        assert(sa.take_string() == "\xD0\xDF");
        up.clear();
        up << 127;
        assert(sa.take_string() == "\x7F");
        up.clear();
        up << 128;
        assert(sa.take_string() == String("\xD1\x00\x80", 3));
        up << -32768;
        assert(sa.take_string() == String("\xD1\x80\x00", 3));
        up << -32769;
        assert(sa.take_string() == String("\xD2\xFF\xFF\x7F\xFF", 5));
    }

    {
        StringAccum sa;
        msgpack::unparser<StringAccum> up(sa);
        up.clear();
        up << msgpack::array(2) << Json((uint64_t) 1 << 63)
           << Json((int64_t) 1 << 63);
        String result = sa.take_string();
        TEST(result.c_str(), result.length(), result.length(),
             "[9223372036854775808,-9223372036854775808]");
    }

    std::cout << "All tests pass!\n";
}

Json __attribute__((noinline)) parse_json(const char* first, const char* last) {
    return Json::parse(first, last);
}

Json __attribute__((noinline)) parse_json(const String& str) {
    return Json::parse(str);
}

static const char sample_json[] = "{\"name\": \"Deborah Estrin\", \"email\": \"estrin@usc.edu\", \"affiliation\": \"University of Southern California\", \"roles\": [\"pc\"]}";

static const char sample_msgpack[] = "\204\244name\256Deborah Estrin\245email\256estrin@usc.edu\253affiliation\331!University of Southern California\245roles\221\242pc";

static int parse_json_loop_size = 10000000;

void parse_json_loop_1() {
    int total_size = 0;
    const char* sample_json_end = sample_json + strlen(sample_json);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = Json::parse(sample_json, sample_json_end);
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

void parse_json_loop_2() {
    int total_size = 0;
    const char* sample_json_end = sample_json + strlen(sample_json);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = Json::parse(String(sample_json, sample_json_end));
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

void parse_json_loop_3() {
    int total_size = 0;
    String sample_json_str(sample_json);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = Json::parse(sample_json_str);
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

void parse_msgpack_loop_1() {
    int total_size = 0;
    const char* sample_msgpack_end = sample_msgpack + strlen(sample_msgpack);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = msgpack::parse(sample_msgpack, sample_msgpack_end);
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

void parse_msgpack_loop_2() {
    int total_size = 0;
    const char* sample_msgpack_end = sample_msgpack + strlen(sample_msgpack);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = msgpack::parse(String(sample_msgpack, sample_msgpack_end));
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

void parse_msgpack_loop_3() {
    int total_size = 0;
    String sample_msgpack_str(sample_msgpack);
    for (int i = 0; i != parse_json_loop_size; ++i) {
        Json j = msgpack::parse(sample_msgpack_str);
        total_size += j.size();
    }
    assert(total_size == 4 * parse_json_loop_size);
}

int main(int argc, char** argv) {
    (void) argc, (void) argv;

    check_correctness();
}
