#ifndef TIMEHIST_H
#define TIMEHIST_H 1
#include <stdint.h>
#include "heap.h"
#include "stdbool.h"

struct timehist_bucket{
	uint32_t ts;
	uint32_t size;
	uint32_t pair;
	uint16_t heap_index;
	uint16_t next;
	uint16_t prev;
};

typedef void (*timehist_bucket_apply_func)(struct timehist_bucket * b1, void * aux);
typedef uint32_t (*timehist_pair_func)(struct timehist_bucket * b1, struct timehist_bucket * b2);

struct timehist{
	struct heap * h;
	struct timehist_bucket * buckets;
	uint64_t ts;
	timehist_pair_func pair_func;
	uint16_t size;
	uint16_t filled;
	uint16_t emptybucket_index;
	uint16_t lastbucket_index;
};

#endif /* timehist.h */
