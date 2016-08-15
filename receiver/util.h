#ifndef UTIL_H
#define UTIL_H 1

#include <stdint.h>
#include "stdbool.h"
#include <rte_prefetch.h>
#include <rte_malloc.h>
#include <rte_branch_prediction.h>
#include <rte_cycles.h>
#include "loguser.h"

#define LOG(format, ...)                                          \
                loguser_add(util_lu, format,  ##__VA_ARGS__);                     \


/*
* The strawman approach that updates triggers instead of the flow table
*/
#ifndef PACKETHISTORY
#define PACKETHISTORY    0
#endif

#if PACKETHISTORY
#define	PKT_PREFETCH_ENABLE 0
#define HASH_PREFETCH_ENABLE 0
#define SWEEP_PREFETCH_ENABLE 0 
#define TRIGGERTABLE_SWEEP    0
#define MULTISTEP 0
#define DPDK_MALLOC    0
#define DPDK_BIG_MALLOC   0
#define TRIGGERTABLE_INLINE_TRIGGER    0
#define FLATREPORT_PKT_BURST 64
#define DDOS_TABLE    0
#endif

/*
* disable or enable the DDoS table
*/
#ifndef DDOS_TABLE
#define DDOS_TABLE    1
#endif

/*
* Shall we use huge pages for any malloc?
*/
#ifndef DPDK_MALLOC
#define DPDK_MALLOC    0
#endif

/*
* Shall we use huge pages for mallocs of big data structures
*/
#ifndef DPDK_BIG_MALLOC
#define DPDK_BIG_MALLOC   1
#endif

/*
* Shalll we put triggers backtoback in a buffer or just keep track of their pointers
*/
#ifndef TRIGGERTABLE_INLINE_TRIGGER
#define TRIGGERTABLE_INLINE_TRIGGER   1
#endif

/*
* How many packets to process in a batch. This affects prefetchig
*/
#ifndef FLATREPORT_PKT_BURST
#define FLATREPORT_PKT_BURST 16 // smaller than 32 
#endif

/*
* Shall we sweep over triggers or sweep over flows
*/
#ifndef TRIGGERTABLE_SWEEP
#define TRIGGERTABLE_SWEEP   1 
#endif

/*
* Shall we break the sweep into multiple steps?
*/
#ifndef MULTISTEP
#define MULTISTEP 1
#endif

/*
* Shall we prefetch packets
*/
#ifndef PKT_PREFETCH_ENABLE
#define PKT_PREFETCH_ENABLE    1
#endif

/*
* Shall we prefetch flow table entries
*/
#ifndef HASH_PREFETCH_ENABLE
#define HASH_PREFETCH_ENABLE    1
#endif

/*
* Shall we use prefetching during the sweep process. Ex. prefetch triggers or flow entries explicitly
*/
#ifndef SWEEP_PREFETCH_ENABLE
#define SWEEP_PREFETCH_ENABLE   1 
#endif

/*
* How many flow entries for each trigger must be batched together to avoid pointer jumping during aggregation of statistics in sweeping
*/
#ifndef TRIGGERFLOW_BATCH
#define TRIGGERFLOW_BATCH 64
#endif


#if PKT_PREFETCH_ENABLE
#define PKT_PREFETCH0(p)       rte_prefetch0(p)
#define PKT_PREFETCH1(p)       rte_prefetch1(p)
#else
#define PKT_PREFETCH0(p)
#define PKT_PREFETCH1(p)
#endif

#if SWEEP_PREFETCH_ENABLE
#define SWEEP_PREFETCH0(p)       rte_prefetch0(p)
#else
#define SWEEP_PREFETCH0(p)
#endif



#define HASH_PREFETCH0(p)       rte_prefetch0(p)

#if DPDK_MALLOC
#define MALLOC(s)       rte_malloc(NULL, s, 0)
#define FREE(p)       rte_free(p)
#define MALLOC2(s,n) rte_malloc(NULL, s, n)
#else
#define MALLOC(s)     malloc(s)
#define FREE(p)       free(p)
#define MALLOC2(s,n) malloc(s)
#endif

#if DPDK_BIG_MALLOC
#define BIGMALLOC(s)       rte_malloc(NULL, s, 0)
#define BIGFREE(p)       rte_free(p)
#define BIGMALLOC2(s,n) rte_malloc(NULL, s, n)
#else
#define BIGMALLOC(s)     malloc(s)
#define BIGFREE(p)       free(p)
#define BIGMALLOC2(s,n) malloc(s) //posix_memalign is bad
#endif


void * myalign(int size, int align);
bool is_empty2(void *buf2, uint32_t size);
void set_CPU(int cpu);

/*
* return most significant bit of m. returns 0 for 0
*/
uint32_t gbp(uint32_t m);
uint16_t entry_size_64(uint16_t m);
unsigned long long rdtscl(void);
unsigned int countTrailing0M(uint64_t v);
int log2_32 (uint32_t value);
int log2_64 (uint64_t value);
void shuffle (void * array, size_t n, size_t size);

extern struct loguser * util_lu;

#endif /* util.h */
