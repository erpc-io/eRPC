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
#ifndef MASSTREE_SPLIT_HH
#define MASSTREE_SPLIT_HH 1
#include "masstree_tcursor.hh"
#include "btree_leaflink.hh"
namespace Masstree {

/** @brief Return ikey at position @a i, assuming insert of @a ka at @a ka_i. */
template <typename P>
inline typename P::ikey_type
leaf<P>::ikey_after_insert(const permuter_type& perm, int i,
                           const key_type& ka, int ka_i) const
{
    if (i < ka_i)
        return this->ikey0_[perm[i]];
    else if (i == ka_i)
        return ka.ikey();
    else
        return this->ikey0_[perm[i - 1]];
}

/** @brief Split this node into *@a nr and insert @a ka at position @a p.
    @pre *@a nr is a new empty leaf
    @pre this->locked() && @a nr->locked()
    @post split_ikey is the first key in *@a nr
    @return split type

    If @a p == this->size() and *this is the rightmost node in the layer,
    then this code assumes we're inserting nodes in sequential order, and
    the split does not move any keys.

    The split type is 0 if @a ka went into *this, 1 if the @a ka went into
    *@a nr, and 2 for the sequential-order optimization (@a ka went into *@a
    nr and no other keys were moved). */
template <typename P>
int leaf<P>::split_into(leaf<P>* nr, int p, const key_type& ka,
                        ikey_type& split_ikey, threadinfo& ti)
{
    // B+tree leaf insertion.
    // Split *this, with items [0,T::width), into *this + nr, simultaneously
    // inserting "ka:value" at position "p" (0 <= p <= T::width).

    // Let mid = floor(T::width / 2) + 1. After the split,
    // "*this" contains [0,mid) and "nr" contains [mid,T::width+1).
    // If p < mid, then x goes into *this, and the first element of nr
    //   will be former item (mid - 1).
    // If p >= mid, then x goes into nr.
    masstree_precondition(!this->concurrent || (this->locked() && nr->locked()));
    masstree_precondition(this->size() >= this->width - 1);

    int width = this->size();   // == this->width or this->width - 1
    int mid = this->width / 2 + 1;
    if (p == 0 && !this->prev_)
        mid = 1;
    else if (p == width && !this->next_.ptr)
        mid = width;

    // Never separate keys with the same ikey0.
    permuter_type perml(this->permutation_);
    ikey_type mid_ikey = ikey_after_insert(perml, mid, ka, p);
    if (mid_ikey == ikey_after_insert(perml, mid - 1, ka, p)) {
        int midl = mid - 2, midr = mid + 1;
        while (1) {
            if (midr <= width
                && mid_ikey != ikey_after_insert(perml, midr, ka, p)) {
                mid = midr;
                break;
            } else if (midl >= 0
                       && mid_ikey != ikey_after_insert(perml, midl, ka, p)) {
                mid = midl + 1;
                break;
            }
            --midl, ++midr;
        }
        masstree_invariant(mid > 0 && mid <= width);
    }

    typename permuter_type::value_type pv = perml.value_from(mid - (p < mid));
    for (int x = mid; x <= width; ++x)
        if (x == p)
            nr->assign_initialize(x - mid, ka, ti);
        else {
            nr->assign_initialize(x - mid, this, pv & 15, ti);
            pv >>= 4;
        }
    permuter_type permr = permuter_type::make_sorted(width + 1 - mid);
    if (p >= mid)
        permr.remove_to_back(p - mid);
    nr->permutation_ = permr.value();

    btree_leaflink<leaf<P>, P::concurrent>::link_split(this, nr);

    split_ikey = nr->ikey0_[0];
    return p >= mid ? 1 + (mid == width) : 0;
}

template <typename P>
int internode<P>::split_into(internode<P> *nr, int p, ikey_type ka,
                             node_base<P> *value, ikey_type& split_ikey,
                             int split_type)
{
    // B+tree internal node insertion.
    // Split *this, with items [0,T::width), into *this + nr, simultaneously
    // inserting "ka:value" at position "p" (0 <= p <= T::width).
    // The midpoint element of the result is stored in "split_ikey".

    // Let mid = ceil(T::width / 2). After the split, the key at
    // post-insertion position mid is stored in split_ikey. *this contains keys
    // [0,mid) and nr contains keys [mid+1,T::width+1).
    // If p < mid, then x goes into *this, pre-insertion item mid-1 goes into
    //   split_ikey, and the first element of nr is pre-insertion item mid.
    // If p == mid, then x goes into split_ikey and the first element of
    //   nr is pre-insertion item mid.
    // If p > mid, then x goes into nr, pre-insertion item mid goes into
    //   split_ikey, and the first element of nr is post-insertion item mid+1.
    masstree_precondition(!this->concurrent || (this->locked() && nr->locked()));

    int mid = (split_type == 2 ? this->width : (this->width + 1) / 2);
    nr->nkeys_ = this->width + 1 - (mid + 1);

    if (p < mid) {
        nr->child_[0] = this->child_[mid];
        nr->shift_from(0, this, mid, this->width - mid);
        split_ikey = this->ikey0_[mid - 1];
    } else if (p == mid) {
        nr->child_[0] = value;
        nr->shift_from(0, this, mid, this->width - mid);
        split_ikey = ka;
    } else {
        nr->child_[0] = this->child_[mid + 1];
        nr->shift_from(0, this, mid + 1, p - (mid + 1));
        nr->assign(p - (mid + 1), ka, value);
        nr->shift_from(p + 1 - (mid + 1), this, p, this->width - p);
        split_ikey = this->ikey0_[mid];
    }

    for (int i = 0; i <= nr->nkeys_; ++i)
        nr->child_[i]->set_parent(nr);

    this->mark_split();
    if (p < mid) {
        this->nkeys_ = mid - 1;
        return p;
    } else {
        this->nkeys_ = mid;
        return -1;
    }
}


template <typename P>
bool tcursor<P>::make_split(threadinfo& ti)
{
    // We reach here if we might need to split, either because the node is
    // full, or because we're trying to insert into position 0 (which holds
    // the ikey_bound). But in the latter case, perhaps we can rearrange the
    // permutation to do an insert instead.
    if (n_->size() < n_->width) {
        permuter_type perm(n_->permutation_);
        perm.exchange(perm.size(), n_->width - 1);
        kx_.p = perm.back();
        if (kx_.p != 0) {
            n_->permutation_ = perm.value();
            fence();
            n_->assign(kx_.p, ka_, ti);
            return false;
        }
    }

    node_type* n = n_;
    node_type* child = leaf_type::make(n_->ksuf_used_capacity(), n_->phantom_epoch(), ti);
    child->assign_version(*n_);
    ikey_type xikey[2];
    int split_type = n_->split_into(static_cast<leaf_type *>(child),
                                    kx_.i, ka_, xikey[0], ti);
    bool sense = false;

    while (1) {
        masstree_invariant(!n->concurrent || (n->locked() && child->locked() && (n->isleaf() || n->splitting())));
        internode_type *next_child = 0;

        internode_type *p = n->locked_parent(ti);

        if (!n->parent_exists(p)) {
            internode_type *nn = internode_type::make(ti);
            nn->child_[0] = n;
            nn->assign(0, xikey[sense], child);
            nn->nkeys_ = 1;
            nn->make_layer_root();
            fence();
            n->set_parent(nn);
        } else {
            int kp = internode_type::bound_type::upper(xikey[sense], *p);

            if (p->size() < p->width)
                p->mark_insert();
            else {
                next_child = internode_type::make(ti);
                next_child->assign_version(*p);
                next_child->mark_nonroot();
                kp = p->split_into(next_child, kp, xikey[sense],
                                   child, xikey[!sense], split_type);
            }

            if (kp >= 0) {
                p->shift_up(kp + 1, kp, p->size() - kp);
                p->assign(kp, xikey[sense], child);
                fence();
                ++p->nkeys_;
            }
        }

        if (n->isleaf()) {
            leaf_type *nl = static_cast<leaf_type *>(n);
            leaf_type *nr = static_cast<leaf_type *>(child);
            permuter_type perml(nl->permutation_);
            int width = perml.size();
            perml.set_size(width - nr->size());
            // removed item, if any, must be @ perml.size()
            if (width != nl->width)
                perml.exchange(perml.size(), nl->width - 1);
            nl->mark_split();
            nl->permutation_ = perml.value();
            if (split_type == 0) {
                kx_.p = perml.back();
                nl->assign(kx_.p, ka_, ti);
            } else {
                kx_.i = kx_.p = kx_.i - perml.size();
                n_ = nr;
            }
            // versions/sizes shouldn't change after this
            if (nl != n_) {
                assert(nr == n_);
                // we don't add n_ until lp.finish() is called (this avoids next_version_value() annoyances)
                updated_v_ = nl->full_unlocked_version_value();
            } else
                new_nodes_.emplace_back(nr, nr->full_unlocked_version_value());
        }

        if (n != n_)
            n->unlock();
        if (child != n_)
            child->unlock();
        if (next_child) {
            n = p;
            child = next_child;
            sense = !sense;
        } else if (p) {
            p->unlock();
            break;
        } else
            break;
    }

    return false;
}

} // namespace Masstree
#endif
