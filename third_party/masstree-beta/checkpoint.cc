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
#include "checkpoint.hh"

// add one key/value to a checkpoint.
// called by checkpoint_tree() for each node.
bool ckstate::visit_value(Str key, const row_type* value, threadinfo&) {
    if (endkey && key >= endkey)
        return false;
    if (!row_is_marker(value)) {
        msgpack::unparser<kvout> up(*vals);
        up.write(key).write_wide(value->timestamp());
        value->checkpoint_write(up);
        ++count;
    }
    return true;
}
