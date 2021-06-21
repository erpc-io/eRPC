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
#ifndef MASSTREE_STATS_HH
#define MASSTREE_STATS_HH
#include "masstree.hh"
#include "json.hh"

namespace Masstree {

template <typename P, typename TI>
void node_json_stats(node_base<P>* n, lcdf::Json& j, int layer, int depth,
                     TI& ti)
{
    if (!n)
        return;
    else if (n->isleaf()) {
        leaf<P>* lf = static_cast<leaf<P>*>(n);
        // track number of leaves by depth and size
        j[&"l1_node_by_depth"[!layer * 3]][depth] += 1;
        j[&"l1_leaf_by_depth"[!layer * 3]][depth] += 1;
        j[&"l1_leaf_by_size"[!layer * 3]][lf->size()] += 1;

        // per-key information
        typename leaf<P>::permuter_type perm(lf->permutation_);
        int n = 0, nksuf = 0;
        size_t active_ksuf_len = 0;
        for (int i = 0; i < perm.size(); ++i)
            if (lf->is_layer(perm[i])) {
                lcdf::Json x = j["l1_size"];
                j["l1_size"] = 0;
                node_json_stats(lf->lv_[perm[i]].layer(), j, layer + 1, 0, ti);
                j["l1_size_sum"] += j["l1_size"].to_i();
                j["l1_size"] = x;
                j["l1_count"] += 1;
            } else {
                ++n;
                int l = sizeof(typename P::ikey_type) * layer
                    + lf->keylenx_[perm[i]];
                if (lf->has_ksuf(perm[i])) {
                    size_t ksuf_len = lf->ksuf(perm[i]).len;
                    l += ksuf_len - 1;
                    active_ksuf_len += ksuf_len;
                    ++nksuf;
                }
                j["key_by_length"][l] += 1;
            }
        j["size"] += n;
        j["l1_size"] += n;
        j["key_by_layer"][layer] += n;

        // key suffix information
        if (lf->allocated_size() != lf->min_allocated_size()
            && lf->ksuf_external()) {
            j["overridden_ksuf"] += 1;
            j["overridden_ksuf_capacity"] += lf->allocated_size() - lf->min_allocated_size();
        }
        if (lf->ksuf_capacity()) {
            j["ksuf"] += 1;
            j["ksuf_capacity"] += lf->ksuf_capacity();
            j["ksuf_len"] += active_ksuf_len;
            j["ksuf_by_layer"][layer] += 1;
            if (!active_ksuf_len) {
                j["unused_ksuf_capacity"] += lf->ksuf_capacity();
                j["unused_ksuf_by_layer"][layer] += 1;
                if (lf->ksuf_external())
                    j["unused_ksuf_external"] += 1;
            } else
                j["used_ksuf_by_layer"][layer] += 1;
        }
    } else {
        internode<P> *in = static_cast<internode<P> *>(n);
        for (int i = 0; i <= in->size(); ++i)
            if (in->child_[i])
                node_json_stats(in->child_[i], j, layer, depth + 1, ti);
        j[&"l1_node_by_depth"[!layer * 3]][depth] += 1;
        j[&"l1_internode_by_size"[!layer * 3]][in->size()] += 1;
    }
}

template <typename P, typename TI>
void json_stats(lcdf::Json& j, basic_table<P>& table, TI& ti)
{
    using lcdf::Json;
    j["size"] = 0.0;
    j["l1_count"] = 0;
    j["l1_size"] = 0;
    const char* const jarrays[] = {
        "node_by_depth", "internode_by_size", "leaf_by_depth", "leaf_by_size",
        "l1_node_by_depth", "l1_internode_by_size", "l1_leaf_by_depth", "l1_leaf_by_size",
        "key_by_layer", "key_by_length",
        "ksuf_by_layer", "unused_ksuf_by_layer", "used_ksuf_by_layer"
    };
    for (const char* const* x = jarrays; x != jarrays + sizeof(jarrays) / sizeof(*jarrays); ++x)
        j[*x] = Json::make_array();

    node_json_stats(table.root(), j, 0, 0, ti);

    j.unset("l1_size");
    for (const char* const* x = jarrays; x != jarrays + sizeof(jarrays) / sizeof(*jarrays); ++x) {
        Json& a = j[*x];
        for (Json* it = a.array_data(); it != a.end_array_data(); ++it)
            if (!*it)
                *it = Json((size_t) 0);
        if (a.empty())
            j.unset(*x);
    }
}

template <typename P, typename TI>
lcdf::Json json_stats(basic_table<P>& table, TI& ti) {
    lcdf::Json j;
    json_stats(j, table, ti);
    return j;
}

}
#endif
