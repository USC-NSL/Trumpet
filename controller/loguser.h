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

/*
* The logger that logs lines into a file as a separate thread. 
* The thread has dynamic sleep time to save CPU
*/
struct loguser{
	struct rte_ring * ring;
	struct rte_mempool * mem;
	pthread_t pth __attribute__((aligned(64)));
	bool finish __attribute__((aligned(64)));
	uint16_t delay;
	uint16_t core;
	struct logfile * lfh;
	bool fullerror;
};

struct loguser * loguser_init(uint32_t size, const char * filename, uint16_t core);
void loguser_finish(struct loguser * lu);

/*
* Add a line to the log. The format is similar to printf
*/
void loguser_add(struct loguser * lu, const char * format, ...);

/*
* We can have multiple log files. Once a thread registers all loguser_add calls will write to the new file created for that thread
*/
bool loguser_registerthread(struct loguser * lu,  const char * filename);

#endif /* loguser.h */
