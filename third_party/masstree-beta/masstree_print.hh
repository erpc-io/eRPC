/* Masstree
 * Eddie Kohler, Yandong Mao, Robert Morris
 * Copyright (c) 2012-2014 President and Fellows of Harvard College
 * Copyright (c) 2012-2014 Massachusetts Institute of Technology
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
#ifndef MASSTREE_PRINT_HH
#define MASSTREE_PRINT_HH
#include "masstree_struct.hh"
#include <stdio.h>
#include <inttypes.h>

namespace Masstree {

class key_unparse_printable_string {
public:
    template <typename K>
    static int unparse_key(key<K> key, char* buf, int buflen) {
        String s = key.unparse().printable();
        int cplen = std::min(s.length(), buflen);
        memcpy(buf, s.data(), cplen);
        return cplen;
    }
};

template <typename T>
class value_print {
  public:
    static void print(T value, FILE* f, const char* prefix,
                      int indent, Str key, kvtimestamp_t initial_timestamp,
                      char* suffix) {
        value->print(f, prefix, indent, key, initial_timestamp, suffix);
    }
};

template <>
class value_print<unsigned char*> {
  public:
    static void print(unsigned char* value, FILE* f, const char* prefix,
                      int indent, Str key, kvtimestamp_t,
                      char* suffix) {
        fprintf(f, "%s%*s%.*s = %p%s\n",
                prefix, indent, "", key.len, key.s, value, suffix);
    }
};

template <>
class value_print<uint64_t> {
  public:
    static void print(uint64_t value, FILE* f, const char* prefix,
                      int indent, Str key, kvtimestamp_t,
                      char* suffix) {
        fprintf(f, "%s%*s%.*s = %" PRIu64 "%s\n",
                prefix, indent, "", key.len, key.s, value, suffix);
    }
};

template <typename P>
void leaf<P>::print(FILE *f, const char *prefix, int depth, int kdepth) const
{
    f = f ? f : stderr;
    prefix = prefix ? prefix : "";
    typename node_base<P>::nodeversion_type v;
    permuter_type perm;
    do {
        v = *this;
        fence();
        perm = permutation_;
    } while (this->has_changed(v));
    int indent = 2 * depth;
    if (depth > P::print_max_indent_depth && P::print_max_indent_depth > 0)
        indent = 2 * P::print_max_indent_depth;

    {
        char buf[1024];
        int l = 0;
        if (ksuf_ && extrasize64_ < -1)
            l = snprintf(buf, sizeof(buf), " [ksuf i%dx%d]", -extrasize64_ - 1, (int) ksuf_->capacity() / 64);
        else if (ksuf_)
            l = snprintf(buf, sizeof(buf), " [ksuf x%d]", (int) ksuf_->capacity() / 64);
        else if (extrasize64_)
            l = snprintf(buf, sizeof(buf), " [ksuf i%d]", extrasize64_);
        if (P::debug_level > 0) {
            kvtimestamp_t cts = timestamp_sub(created_at_[0], initial_timestamp);
            l += snprintf(&buf[l], sizeof(buf) - l, " @" PRIKVTSPARTS, KVTS_HIGHPART(cts), KVTS_LOWPART(cts));
        }
        static const char* const modstates[] = {"", "-", "D"};
        fprintf(f, "%s%*sleaf %p: %d %s, version %" PRIx64 "%s, permutation %s, parent %p, prev %p, next %p%.*s\n",
                prefix, indent, "", this,
                perm.size(), perm.size() == 1 ? "key" : "keys",
                (uint64_t) v.version_value(),
                modstate_ <= 2 ? modstates[modstate_] : "??",
                perm.unparse().c_str(),
                parent_, prev_, next_.ptr,
                l, buf);
    }

    if (v.deleted() || (perm[0] != 0 && prev_))
        fprintf(f, "%s%*s%s = [] #0\n", prefix, indent + 2, "", key_type(ikey_bound()).unparse().c_str());

    char keybuf[MASSTREE_MAXKEYLEN];
    char xbuf[15];
    for (int idx = 0; idx < perm.size(); ++idx) {
        int p = perm[idx];
        int l = P::key_unparse_type::unparse_key(this->get_key(p), keybuf, sizeof(keybuf));
        sprintf(xbuf, " #%x/%d", p, keylenx_[p]);
        leafvalue_type lv = lv_[p];
        if (this->has_changed(v)) {
            fprintf(f, "%s%*s[NODE CHANGED]\n", prefix, indent + 2, "");
            break;
        } else if (!lv)
            fprintf(f, "%s%*s%.*s = []%s\n", prefix, indent + 2, "", l, keybuf, xbuf);
        else if (is_layer(p)) {
            fprintf(f, "%s%*s%.*s = SUBTREE%s\n", prefix, indent + 2, "", l, keybuf, xbuf);
            node_base<P> *n = lv.layer();
            while (!n->is_root())
                n = n->maybe_parent();
            n->print(f, prefix, depth + 1, kdepth + key_type::ikey_size);
        } else {
            typename P::value_type tvx = lv.value();
            P::value_print_type::print(tvx, f, prefix, indent + 2, Str(keybuf, l), initial_timestamp, xbuf);
        }
    }

    if (v.deleted())
        fprintf(f, "%s%*s[DELETED]\n", prefix, indent + 2, "");
}

template <typename P>
void internode<P>::print(FILE* f, const char* prefix, int depth, int kdepth) const
{
    f = f ? f : stderr;
    prefix = prefix ? prefix : "";
    internode<P> copy(*this);
    for (int i = 0; i < 100 && (copy.has_changed(*this) || this->inserting() || this->splitting()); ++i)
        memcpy(&copy, this, sizeof(copy));
    int indent = 2 * depth;
    if (depth > P::print_max_indent_depth && P::print_max_indent_depth > 0)
        indent = 2 * P::print_max_indent_depth;

    {
        char buf[1024];
        int l = 0;
        if (P::debug_level > 0) {
            kvtimestamp_t cts = timestamp_sub(created_at_[0], initial_timestamp);
            l = snprintf(buf, sizeof(buf), " @" PRIKVTSPARTS, KVTS_HIGHPART(cts), KVTS_LOWPART(cts));
        }
        fprintf(f, "%s%*sinternode %p[%u]%s: %d keys, version %" PRIx64 ", parent %p%.*s\n",
                prefix, indent, "", this,
                height_, this->deleted() ? " [DELETED]" : "",
                copy.size(), (uint64_t) copy.version_value(), copy.parent_,
                l, buf);
    }

    char keybuf[MASSTREE_MAXKEYLEN];
    for (int p = 0; p < copy.size(); ++p) {
        if (copy.child_[p])
            copy.child_[p]->print(f, prefix, depth + 1, kdepth);
        else
            fprintf(f, "%s%*s[]\n", prefix, indent, "");
        int l = P::key_unparse_type::unparse_key(copy.get_key(p), keybuf, sizeof(keybuf));
        fprintf(f, "%s%*s%p[%u.%d] %.*s\n",
                prefix, indent, "", this, height_, p, l, keybuf);
    }
    if (copy.child_[copy.size()])
        copy.child_[copy.size()]->print(f, prefix, depth + 1, kdepth);
    else
        fprintf(f, "%s%*s[]\n", prefix, indent, "");
}

template <typename P>
void basic_table<P>::print(FILE* f) const {
    root_->print(f ? f : stdout, "", 0, 0);
}

} // namespace Masstree
#endif
