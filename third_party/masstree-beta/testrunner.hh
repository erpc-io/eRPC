#ifndef MASSTREE_TESTRUNNER_HH
#define MASSTREE_TESTRUNNER_HH
#include "string.hh"
#include <stdio.h>

class testrunner_base {
  public:
    testrunner_base(const lcdf::String& name)
        : name_(name), next_(0) {
        thehead ? thetail->next_ = this : thehead = this;
        thetail = this;
    }
    virtual ~testrunner_base() {
    }
    const lcdf::String& name() const {
        return name_;
    }
    static testrunner_base* first() {
        return thehead;
    }
    static testrunner_base* find(const lcdf::String& name) {
        testrunner_base* t = thehead;
        while (t && t->name_ != name)
            t = t->next_;
        return t;
    }
    static void print_names(FILE* stream, int maxcol);
  private:
    static testrunner_base* thehead;
    static testrunner_base* thetail;
    lcdf::String name_;
    testrunner_base* next_;
};

#ifdef TESTRUNNER_CLIENT_TYPE

class testrunner : public testrunner_base {
  public:
    inline testrunner(const lcdf::String& name)
        : testrunner_base(name) {
    }
    static testrunner* first() {
        return static_cast<testrunner*>(testrunner_base::first());
    }
    static testrunner* find(const lcdf::String& name) {
        return static_cast<testrunner*>(testrunner_base::find(name));
    }
    virtual void run(TESTRUNNER_CLIENT_TYPE) = 0;
};

#define MAKE_TESTRUNNER(name, text)                    \
    namespace {                                        \
    class testrunner_##name : public testrunner {      \
    public:                                            \
        testrunner_##name() : testrunner(#name) {}     \
        void run(TESTRUNNER_CLIENT_TYPE client) { text; client.finish(); } \
    }; static testrunner_##name testrunner_##name##_instance; }

#endif
#endif
