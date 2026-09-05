# eRPC host preflight baseline

Captured on 2026-09-05 before changing the host for Solarflare loopback or
eRPC/DPDK operation.

## Current status

The machine is suitable for an initial Solarflare kernel-driver/OpenOnload
loopback test after assigning test IP addresses. It is not ready for eRPC over
DPDK: no DPDK installation is discoverable, no huge pages are reserved, and
the host does not currently expose usable IOMMU groups for VFIO.

No host configuration changes were made while collecting this baseline.

## Host

- OS: Rocky Linux 8.10 (Green Obsidian)
- Kernel: `4.18.0-553.83.1.el8_10.x86_64`
- Machine: Lenovo ThinkCentre M75s Gen 2, type `11R9` (`11R9S02700`)
- System board: `32E1`
- BIOS: Lenovo `M46KT53A`, dated 2024-09-06
- CPU: AMD Ryzen 5 PRO 5650G, 6 cores / 12 threads
- NUMA: one node (`node0`); both NIC functions report `numa_node=-1`, so Linux
  treats them as having no firmware-provided NUMA affinity
- CPU governor: `performance` on CPUs 0-11
- Boot tuning includes `processor.max_cstate=1`, `idle=poll`,
  `mitigations=off`, `skew_tick=1`, and `tsc=reliable`
- `irqbalance`: active

The boot command line contains duplicated `skew_tick=1 tsc=reliable
rcupdate.rcu_normal_after_boot=1` arguments. This is harmless but can be cleaned
up later.

## Solarflare adapter and links

- PCI functions: `0000:01:00.0` and `0000:01:00.1`
- PCI ID: `1924:0a03`
- Reported subsystem: Solarflare SFN8522-R2 8000 Series 10G Adapter
  (`1924:8017`)
- Linux interfaces: `enp1s0f0` and `enp1s0f1`
- Driver: `sfc` version `6.1.0.1009`
- Firmware: `8.5.0.1002 rx1 tx1`
- Both interfaces: administratively up, 10,000 Mb/s, full duplex, carrier
  detected
- Neither interface currently has an IP address or route
- The management/default route uses `enp5s0f0` at `192.168.8.175`, so the two
  Solarflare ports can be configured without disturbing management networking

The hardware identifies itself as `SFN8522-R2`, not `SFN8522-PLUS`. The current
checks do not establish which optional licensed features are enabled.

## OpenOnload

- Userspace version: `9.0.2.140`
- Onload kernel module version reported by the tool: `9.0.2.140`
- Loaded modules: `onload`, `sfc_char`, `sfc_resource`, and `sfc`
- `/dev/onload` and `/dev/onload_epoll` exist and are mode `0666`
- `sfutils-8.2.4.1004-1.x86_64` is installed

This is enough to attempt a POSIX TCP/UDP OpenOnload test once isolated test
addresses are assigned to the two ports. OpenOnload is a separate datapath from
eRPC's DPDK transport; launching an eRPC DPDK process through `onload` does not
replace the DPDK setup.

## IOMMU and VFIO

Boot command line:

```text
amd_iommu=on iommu=pt
```

Kernel configuration:

```text
CONFIG_IOMMU_SUPPORT=y
CONFIG_AMD_IOMMU=y
CONFIG_VFIO=m
CONFIG_VFIO_PCI=m
```

Observed boot messages:

```text
iommu: Default domain type: Passthrough (set via kernel command line)
AMD-Vi: AMD IOMMUv2 functionality not available on this system - This is not a bug.
```

Current runtime state:

- `/sys/kernel/iommu_groups` is empty
- Neither Solarflare PCI function has an `iommu_group` sysfs entry
- No `vfio` modules are loaded
- `iommu_v2` is loaded for the AMD GPU; that is not evidence that PCI DMA
  remapping and VFIO isolation are active

The `AMD IOMMUv2 functionality not available` line alone is not a fatal error:
IOMMUv2 is an optional feature used by devices such as GPUs. The decisive issue
for VFIO is that there are no numbered IOMMU groups. The kernel supports AMD
IOMMU and the command line requests it, but firmware has not supplied or
enabled a usable PCI IOMMU topology. Check the BIOS/UEFI IOMMU setting (often
separate from SVM/CPU virtualization) and then verify that boot logs contain
AMD-Vi/IVRS initialization and that numbered groups appear.

For Lenovo ThinkCentre firmware, first look under `Advanced > CPU Setup >
IOMMU`. Menu contents vary by firmware revision. This Cezanne unit uses the
`M46KT` BIOS family and its installed `M46KT53A` firmware predates newer Lenovo
releases. Try explicitly setting IOMMU to `Enabled` before treating a BIOS
update as necessary.

IOMMU/VFIO is not required for the first OpenOnload socket loopback because it
uses the `sfc` kernel driver. It must be resolved before choosing `vfio-pci` as
the DPDK binding method. The repository's older CloudLab helper instead assumes
`igb_uio`, which is a different binding path and is not currently loaded.

## Memory and toolchain

- 2 MiB huge pages: 0 reserved, 0 free
- `hugetlbfs`: mounted read/write at `/dev/hugepages`
- Repository requirement: at least 1,024 huge pages on every NUMA node
- `kernel.shmmax` and `kernel.shmall`: effectively unlimited
- Locked-memory soft/hard limit: 64 KiB / 64 KiB
- GCC/G++: 8.5.0
- CMake: 3.26.5
- Python: 3.11.13
- Missing: `numactl`, `numactl-devel`, Meson, and Ninja
- DPDK: `libdpdk` is not visible to `pkg-config`; `RTE_SDK` is unset

Before eRPC/DPDK, reserve at least 1,024 2 MiB pages on node 0, raise the
locked-memory limit for the launch user, install/build the DPDK dependencies,
and choose a supported Solarflare DPDK driver/binding strategy.

## Next checkpoints

1. Assign two isolated addresses to `enp1s0f0` and `enp1s0f1`, then confirm
   direct port-to-port routing and packet flow.
2. Run ordinary and OpenOnload-accelerated TCP/UDP loopback checks and record
   latency, loss, and CPU placement.
3. Decide whether the eRPC experiment will use DPDK with the Solarflare PMD or
   different supported NIC hardware.
4. Configure huge pages, locked memory, DPDK, and NIC ownership for that choice.
5. Build eRPC in debug mode first, run its relevant tests, then rebuild with
   `PERF=ON` for measurements.
