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
#include "perfstat.hh"
#include "compiler.hh"
#include "kvstats.hh"
#if HAVE_NUMA_H
#include <numa.h>
#endif

enum { MaxCores = 48 };   // Maximum number of cores kvdb statistics support
enum { MaxNumaNode = 8 }; // Maximum number of Numa node kvdb statistics support
enum { CoresPerChip = MaxCores / MaxNumaNode };

namespace Perf {

#if MEMSTATS && HAVE_NUMA_H && HAVE_LIBNUMA
static struct {
  long long free;
  long long size;
} numa[MaxNumaNode];
#endif

void
stat::initmain(bool pinthreads) {
    (void) pinthreads;
#if PMC_ENABLED
    always_assert(pinthreads && "Using performance counter requires pinning threads to cores!");
#endif
#if MEMSTATS && HAVE_NUMA_H && HAVE_LIBNUMA
    if (numa_available() != -1) {
        always_assert(numa_max_node() <= MaxNumaNode);
        for (int i = 0; i <= numa_max_node(); i++)
            numa[i].size = numa_node_size64(i, &numa[i].free);
    }
#endif
}

template <typename T>
kvstats
sum_all_cores(const stat **s, int n, const int offset) {
    kvstats sum;
    for (int i = 0; i < n; i++) {
        if (!s[i])
            continue;
        T v = *reinterpret_cast<const T *>(reinterpret_cast<const char *>(s[i]) + offset);
        sum.add(v);
    }
    return sum;
}

template <typename T>
kvstats
sum_one_chip(const stat **s, int n, const int offset, const int chipidx) {
    kvstats sum;
    for (int i = 0; i < n; i++) {
        if (!s[i] || s[i]->cid / (MaxCores / MaxNumaNode) != chipidx)
            continue;
        T v = *reinterpret_cast<const T *>(reinterpret_cast<const char *>(s[i]) + offset);
        sum.add(v);
    }
    return sum;
}

template <typename T>
kvstats
sum_all_per_chip(const stat **s, int n, const int offset) {
    kvstats per_chip[MaxNumaNode];
    for (int i  = 0; i < n; i++) {
        if (!s[i])
            continue;
        T v = *reinterpret_cast<const T *>(reinterpret_cast<const char *>(s[i]) + offset);
        per_chip[i / CoresPerChip].add(v);
    }
    kvstats sum;
    for (int i = 0; i < MaxNumaNode; i++)
        if (per_chip[i].count)
            sum.add(per_chip[i].avg());
    return sum;
}

void
stat::print(const stat **s, int n) {
    (void)n;
    (void)s;
#define sum_all_cores_of(field) \
    sum_all_cores<typeof(s[0]->field)>(s, n, offsetof(Perf::stat, field))
#define sum_one_chip_of(field, c) \
    sum_one_chip<typeof(s[0]->field)>(s, n, offsetof(Perf::stat, field), c)
#define sum_all_per_chip_of(field) \
    sum_all_per_chip<typeof(s[0]->field)>(s, n, offsetof(Perf::stat, field))

#define sum_all_cores_of_array(field, oa) \
    sum_all_cores<typeof(s[0]->field[0])>(s, n, offsetof(Perf::stat, field) + \
                                          sizeof(s[0]->field[0]) * oa)
#define sum_one_chip_of_array(field, oa, c) \
    sum_one_chip<typeof(s[0]->field[0])>(s, n, offsetof(Perf::stat, field) + \
                                         sizeof(s[0]->field[0]) * oa, c)
#define sum_all_per_chip_of_array(field, oa) \
    sum_all_per_chip<typeof(s[0]->field[0])>(s, n, offsetof(Perf::stat, field) + \
                                             sizeof(s[0]->field[0]) * oa)

#if GETSTATS && 0
    for (int i = 0; i < n; i++)
        if (s[i]->ngets < 1000) {
            s[i] = NULL;
            continue;
        }
    kvstats ngets = sum_all_cores_of(ngets);
    kvstats ntsc = sum_all_cores_of(ntsc);
    kvstats np = sum_all_cores_of(nprobe);
    if (np.sum >= 1)
        fprintf(stderr, "Total probe %.0f, probe/get %.2f\n", np.sum, np.sum / ngets.sum);
#if PMC_ENABLED
    fprintf(stderr, "(Inaccurate because PMC is Enabled!)");
#endif
    fprintf(stderr, "Cycles/get (between mark_get_begin and mark_get_end): %.0f\n",
            ntsc.sum / ngets.sum);
#if PMC_ENABLED
    for (int i = 0; i < n; i++) {
        if (!s[i])
            continue;
        fprintf(stderr, "Core %d:\n", i);
        for (int pi = 0; pi < 4; pi++) {
            fprintf(stderr, "\tpmc[%d]: %016" PRIx64 "->%016" PRIx64 "\n",
                    pi, s[i]->pmc_firstget[pi], s[i]->pmc_start[pi]);
            always_assert(s[i]->pmc_start[pi] >= s[i]->pmc_firstget[pi]);
            always_assert(s[i]->t1_lastget >= s[i]->t0_firstget);
        }
    }
    // Compute the start and end time of get phase
    kvstats getstart = sum_all_cores_of(t0_firstget);
    kvstats getend = sum_all_cores_of(t1_lastget);
    getstart.print_report("time of first get");
    getend.print_report("time of last get");

    // Compute per-chip pmc during the whole get phase
    double pcpmc_phase[MaxNumaNode][4];
    for (int i = 0; i < MaxNumaNode; i++)
        for (int pi = 0; pi < 4; pi++)
            pcpmc_phase[i][pi] = sum_one_chip_of_array(pmc_start, pi, i).avg() - 
                                 sum_one_chip_of_array(pmc_firstget, pi, i).avg();

    // Compute cputime and realtime during get phase
    kvstats t_firstget = sum_all_cores_of(t0_firstget);
    kvstats t_lastget = sum_all_cores_of(t1_lastget);
    double realtime = t_lastget.avg() - t_firstget.avg();

    for (int pi = 0; pi < 4; pi++) {
        fprintf(stderr, "DRAM access to node (pmc %d)\n", pi);
        double sum = 0;
        for (int i = 0; i < MaxNumaNode; i++) {
            fprintf(stderr, "\tFrom chip %2d: %8.1f GB/s\n", i,
                    pcpmc_phase[i][pi] * 64 / (realtime * (1 << 30)));
            sum += pcpmc_phase[i][pi];
        }
        fprintf(stderr, "\tSum: %8.1f GB/s\n", 
                sum * 64 / (realtime * (1 << 30)));
    }
    // Print per-get pmc_lookup
    fprintf(stderr, "Per get statistics (counted between mark_get_begin and mark_get_end):\n");
    for (int pi = 0; (ngets.sum > 0) && pi < 4; pi ++) {
        kvstats pmc_lookup = sum_all_cores_of_array(pmc_lookup, pi);
        kvstats pcpmc_lookup = sum_all_per_chip_of_array(pmc_lookup, pi);
        fprintf(stderr, "\tpmc%d/get: %6.1f, per_chip_pmc%d/get: %6.1f\n",
                pi, (double) pmc_lookup.sum / ngets.sum, pi, 
               (double) pcpmc_lookup.sum / ngets.sum);
    }
#endif
#endif

#if MEMSTATS && HAVE_NUMA_H && HAVE_LIBNUMA && 0
    // collect tree memory
    kvstats tree_mem = sum_all_cores_of(tree_mem);
    kvstats tree_keys = sum_all_cores_of(tree_keys);
    fprintf(stderr, "Memory statistics\n");
    fprintf(stderr, "\tAllocated per key: %.0f bytes, %.0f\n", tree_mem.sum / tree_keys.sum, tree_keys.sum);
    if (numa_available() != -1) {
        unsigned long total_alloc = 0;
        for (int i = 0; i <= numa_max_node(); i++) {
            kvstats chip = sum_one_chip_of(tree_mem, i);
            long long nowfree;
            long long size = numa_node_size64(i, &nowfree);
            total_alloc += numa[i].free - nowfree;
            fprintf(stderr, "\tNode %d (MB): size %6lld, allocated = %6lld - "
                    "%6lld = %6lld, tree_mem %6.0f\n",
                    i, size >> 20, numa[i].free >> 20, nowfree >> 20,
                    (numa[i].free - nowfree) / (1 << 20), 
                    chip.sum / (1 << 20));
        }
        fprintf(stderr, "Total allocated memory %ld MB\n", total_alloc >> 20);
    }
#endif

#if GCSTATS
    // collect memory used by epoch based garbage collector
    kvstats gc_nfree = sum_all_cores_of(gc_nfree);
    kvstats gc_nalloc = sum_all_cores_of(gc_nalloc);
    fprintf(stderr, "reuse per gc slot: %.0f, freed: %.0f, allocated: %.0f\n",
            gc_nfree.sum / gc_nalloc.sum, gc_nfree.sum, gc_nalloc.sum);
#endif
}

}
