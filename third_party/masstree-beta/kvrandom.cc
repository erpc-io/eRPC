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
#include "kvrandom.hh"
#include "compiler.hh"
#include <stdio.h>

const uint32_t kvrandom_psdes_nr::c1[] = {
    0xBAA96887U, 0x1E17D32CU, 0x03BCDC3CU, 0x0F33D1B2U
};
const uint32_t kvrandom_psdes_nr::c2[] = {
    0x4B0F3B58U, 0xE874F0C3U, 0x6955C5A6U, 0x55A7CA46U
};

uint32_t kvrandom_psdes_nr::psdes(uint32_t lword, uint32_t irword) {
    for (int i = 0; i < niter; ++i) {
	uint32_t iswap = irword;
	uint32_t ia = irword ^ c1[i];
	uint32_t il = ia & 0xFFFF, ih = ia >> 16;
	uint32_t ib = il * il + ~(ih * ih);
	ia = (ib >> 16) | (ib << 16);
	irword = lword ^ ((ia ^ c2[i]) + il * ih);
	lword = iswap;
    }
    return irword;
}
