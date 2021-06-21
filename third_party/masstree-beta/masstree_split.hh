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
#define MASSTREE_SPLIT_HH
#include "masstree_tcursor.hh"
#include "btree_leaflink.hh"
namespace Masstree {

/** @brief Return ikey at position @a i, assuming insert of @a ka at @a ka_i. */
template <typename P>
inline typename P::ikey_type
leaf<P>::ikey_after_insert(const permuter_type& perm, int i,
                           const tcursor<P>* cursor) const
{
    if (i < cursor->kx_.i) {
        return this->ikey0_[perm[i]];
    } else if (i == cursor->kx_.i) {
        return cursor->ka_.ikey();
    } else {
        return this->ikey0_[perm[i - 1]];
    }
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
int leaf<P>::split_into(leaf<P>* nr, tcursor<P>* cursor,
                        ikey_type& split_ikey, threadinfo& ti)
{
    masstree_precondition(!this->concurrent || (this->locked() && nr->locked()));
    masstree_precondition(this->size() >= this->width - 1);

    // B+tree leaf insertion.
    // Split *this, with items [0,T::width), into *this + nr, simultaneously
    // inserting "ka:value" at position "p" (0 <= p <= T::width).

    // `mid` determines the split point. Post-split, `*this` contains [0,mid)
    // and `nr` contains [mid,T::width+1).
    // If `p < mid`, then the new item goes into `*this`, and the first element
    //   of `nr` will be former item (mid - 1).
    // If `p >= mid`, then the new item goes into nr.

    // pick initial insertion point
    permuter_type perml(this->permutation_);
    int width = perml.size();   // == this->width or this->width - 1
    int mid = this->width / 2 + 1;
    int p = cursor->kx_.i;
    if (p == 0 && !this->prev_) {
        // reverse-sequential optimization
        mid = 1;
    } else if (p == width && !this->next_.ptr) {
        // sequential optimization
        mid = width;
    }

    // adjust insertion point to keep keys with the same ikey0 together
    ikey_type mid_ikey = ikey_after_insert(perml, mid, cursor);
    if (mid_ikey == ikey_after_insert(perml, mid - 1, cursor)) {
        int midl = mid - 2, midr = mid + 1;
        while (true) {
            if (midr <= width
                && mid_ikey != ikey_after_insert(perml, midr, cursor)) {
                mid = midr;
                break;
            } else if (midl >= 0
                       && mid_ikey != ikey_after_insert(perml, midl, cursor)) {
                mid = midl + 1;
                break;
            }
            --midl, ++midr;
        }
        masstree_invariant(mid > 0 && mid <= width);
    }

    // move items to `nr`
    typename permuter_type::value_type pv = perml.value_from(mid - (p < mid));
    for (int x = mid; x <= width; ++x) {
        if (x == p) {
            nr->assign_initialize(x - mid, cursor->ka_, ti);
        } else {
            nr->assign_initialize(x - mid, this, pv & 15, ti);
            pv >>= 4;
        }
    }
    permuter_type permr = permuter_type::make_sorted(width + 1 - mid);
    if (p >= mid) {
        permr.remove_to_back(p - mid);
    }
    nr->permutation_ = permr.value();
    split_ikey = nr->ikey0_[0];

    // link `nr` across leaves
    btree_leaflink<leaf<P>, P::concurrent>::link_split(this, nr);

    // return split type
    return p < mid ? 0 : 1 + (mid == width);
}

template <typename P>
int internode<P>::split_into(internode<P>* nr, int p, ikey_type ka,
                             node_base<P>* value, ikey_type& split_ikey,
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

    for (int i = 0; i <= nr->nkeys_; ++i) {
        nr->child_[i]->set_parent(nr);
    }

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

    node_type* child = leaf_type::make(n_->ksuf_used_capacity(), n_->phantom_epoch(), ti);
    child->assign_version(*n_);
    ikey_type xikey[2];
    int split_type = n_->split_into(static_cast<leaf_type*>(child),
                                    this, xikey[0], ti);
    unsigned sense = 0;
    node_type* n = n_;
    uint32_t height = 0;

    while (true) {
        masstree_invariant(!n->concurrent || (n->locked() && child->locked() && (n->isleaf() || n->splitting())));
        internode_type *next_child = 0;

        internode_type *p = n->locked_parent(ti);

        int kp = -1;
        if (n->parent_exists(p)) {
            kp = internode_type::bound_type::upper(xikey[sense], *p);
            p->mark_insert();
        }

        if (kp < 0 || p->height_ > height + 1) {
            internode_type *nn = internode_type::make(height + 1, ti);
            nn->child_[0] = n;
            nn->assign(0, xikey[sense], child);
            nn->nkeys_ = 1;
            if (kp < 0) {
                nn->make_layer_root();
            } else {
                nn->set_parent(p);
                p->child_[kp] = nn;
            }
            fence();
            n->set_parent(nn);
        } else {
            if (p->size() >= p->width) {
                next_child = internode_type::make(height + 1, ti);
                next_child->assign_version(*p);
                next_child->mark_nonroot();
                kp = p->split_into(next_child, kp, xikey[sense],
                                   child, xikey[sense ^ 1], split_type);
            }
            if (kp >= 0) {
                p->shift_up(kp + 1, kp, p->size() - kp);
                p->assign(kp, xikey[sense], child);
                fence();
                ++p->nkeys_;
            }
        }

        // complete split by stripping shifted items from left node
        // (this is delayed until both nodes are reachable because
        // creating new internodes is expensive; might as well leave items
        // in the left leaf reachable until that's done)
        if (n == n_) {
            leaf_type* nl = static_cast<leaf_type*>(n);
            leaf_type* nr = static_cast<leaf_type*>(child);
            // shrink `nl` to only the relevant items
            permuter_type perml(nl->permutation_);
            int width = perml.size();
            perml.set_size(width - nr->size());
            // removed item, if any, must be @ perml.size()
            if (width != nl->width) {
                perml.exchange(perml.size(), nl->width - 1);
            }
            nl->mark_split();
            nl->permutation_ = perml.value();
            // account for split
            if (split_type == 0) {
                kx_.p = perml.back();
                nl->assign(kx_.p, ka_, ti);
                new_nodes_.emplace_back(nr, nr->full_unlocked_version_value());
            } else {
                kx_.i = kx_.p = kx_.i - perml.size();
                n_ = nr;
                updated_v_ = nl->full_unlocked_version_value();
            }
        }

        // hand-over-hand locking
        if (n != n_) {
            n->unlock();
        }
        if (child != n_) {
            child->unlock();
        }
        if (next_child) {
            n = p;
            child = next_child;
            sense ^= 1;
            ++height;
        } else if (p) {
            p->unlock();
            break;
        } else {
            break;
        }
    }

    return false;
}

} // namespace Masstree
#endif
