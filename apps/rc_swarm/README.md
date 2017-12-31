## Connection logic
 * There are `N` physical machines in the swarm. Each machine emulates `M`
   ``virtual'' (not related to virtualization) machines. Each machine runs `W`
   worker threads.
 * Each worker thread creates `N * M` QPs. This accurately emulates the number
   of QPs that each worker would create in a cluster with `N * M` physical
   machines.
   * A thread with local thread index `t` creates all its QPs on port index
     `t % num_ports`.
 * The `N * M` QPs for local thread index `t` are connected as follows:
   * Thread `t` only connects to other threads with local thread index `t`.
     This works around the lack of cross-port connectivity on NetApp.
   * For each `m` in `0, ..., M - 1`, QPs `mN, ..., mN + N - 1` will be
     connected to QPs `mN, ..., mN + N - 1` at other threads with local index
     `t`. In other words, QPs created by a thread for VM `m` will be connected
     QPs created by other threads for VM `m`.
   * QP `k` in `mN, ..., mN + N - 1` connects to physical machine `k % N`.
 * To achieve this connection setup, QPs are named as follows. At physical machine
   `n` and local thread index `t`, QP `k` in `mN, ..., mN + N - 1` is named
   `on_phys_mc_<n>-at_thr_<t>-for_phys_mc_<k % N>-for_vm_<m>`.

## Difference from sender-scalability
When `MACHINE_0_ONLY == 1`, only machine #0 sends RDMA requests to remote
machines. This configuration is different from the `sender-scalability`
benchmark, where each worker thread creates a QP to each remote **worker**,
whereas in `rc-swarm`, each worker creates a QP to each remote **machine**.

Due to the lower number of QPs, `rc-swarm` requires a larger `WINDOW_SIZE` to
achieve high throughput when `MACHINE_0_ONLY == 1`. Even then, I have been
able to achieve only 71M READs/s from the one machine and 5 remote machines,
whereas `sender-scalability` can achieve 94M READs/s in the same cluster config.

## Performance caveat
For some reason, using a prealloc connected buffer for the control blocks of
the server's ports leads to low performance (tested for `MACHINE_0_ONLY == 1`).

## Outstanding operations logic
For READs, we limit the number of outstanding
operations per-thread to `WINDOW_SIZE`. This is because too many outstanding
requests can cause WQE cache misses, and this benchmark focuses on QP cache
misses.

A similar restriction is harder for WRITEs because we cannot detect completion
by polling on memory. For WRITEs, we have a reasonable limit on the number of
outstanding operations per-QP (i.e., around `2 * UNSIG_BATCH`), not per-thread.
The per-thread limit is around `2 * UNSIG_BATCH * NUM_CLIENTS`.

## Testing with low fanout
The `rw-tput-sender` benchmark should be used to measure outbound performance
with low QP fanout.
