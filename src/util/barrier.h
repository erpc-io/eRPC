#pragma once

namespace erpc {

static void memory_barrier() { asm volatile("" ::: "memory"); }

static void lfence() { asm volatile("lfence" ::: "memory"); }

static void sfence() { asm volatile("sfence" ::: "memory"); }

static void mfence() { asm volatile("mfence" ::: "memory"); }

static void pause() { asm volatile("pause"); }

static void clflush(volatile void* p) { asm volatile("clflush (%0)" ::"r"(p)); }

static void cpuid(unsigned int* eax, unsigned int* ebx, unsigned int* ecx,
                  unsigned int* edx) {
  asm volatile("cpuid"
               : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
               : "0"(*eax), "2"(*ecx));
}

}  // namespace erpc
