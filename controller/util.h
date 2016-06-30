#ifndef UTIL_H
#define UTIL_H 1

#include <stdint.h>
#include "stdbool.h"
#include "loguser.h"

#include <rte_cycles.h>

#define MALLOC(s)     malloc(s)
#define FREE(p)       free(p)
#define MALLOC2(s,n) malloc(s)

#define BIGMALLOC(s)     malloc(s)
#define BIGFREE(p)       free(p)
#define BIGMALLOC2(s,n) malloc(s) //posix_memalign is bad

#define LOG(format, ...)                                          \
                loguser_add(util_lu, format, ##__VA_ARGS__);                     \


#define HASH_PREFETCH0(p)       

#define rdtscl()	(rte_rdtsc())

void * myalign(int size, int align);

bool is_empty2(void *buf2, uint32_t size);
void set_CPU(int cpu);
uint32_t gbp(uint32_t m);
uint16_t entry_size_64(uint16_t m);
//unsigned long long rdtscl(void);
unsigned int countTrailing0M(uint64_t v);
int log2_32 (uint32_t value);
int log2_64 (uint64_t value);

void shuffle (void * array, size_t n, size_t size);

extern struct loguser * util_lu; 

/*void util_init(void);
void util_finish(void);
void util_loglock(void);
void util_logunlock(void);*/


#endif /* util.h */
