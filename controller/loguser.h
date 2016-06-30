#ifndef LOGUSER_H
#define LOGUSER_H 1

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <rte_ring.h>
#include <rte_mempool.h>

#define LOGUSER_GETSIZE(n) (n*64)
#define LOGLINEBUFFER_LEN 252

struct logfile{
	char filename [100];
	int tid;
	FILE *fd;
	void * next;
};

struct loguser{
	struct rte_ring * ring;
	struct rte_mempool * mem;
	pthread_t pth __attribute__((aligned(64)));
	bool finish __attribute__((aligned(64)));
	uint16_t delay;
	uint16_t core;
	struct logfile * lfh;
};

struct loguser * loguser_init(uint32_t size, const char * filename, uint16_t core);
void loguser_add(struct loguser * lu, const char * format, ...);
void loguser_finish(struct loguser * lu);
bool loguser_registerthread(struct loguser * lu,  const char * filename);

#endif /* loguser.h */
