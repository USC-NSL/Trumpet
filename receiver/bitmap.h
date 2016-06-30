#ifndef BITMAP_H
#define BITMAP_H 1
#include <stdint.h>
#include "stdbool.h"

#define BITMAP_ENTRY_SIZE 64
#define BITMAP_ENTRY_SIZE_LOG 6
#define BITMAP_ENTRY_SIZE_MOD 0x003F
#define BITMAP_MAX_ENTRY(x) ((x >> BITMAP_ENTRY_SIZE_LOG) + (((x & BITMAP_ENTRY_SIZE_MOD) > 0) ? 1 : 0))
#define BITMAP_SIZE(x) (sizeof(struct bitmap)+(BITMAP_MAX_ENTRY(x)-1)*BITMAP_ENTRY_SIZE/8)

typedef void (*bitmap_apply_func)(uint32_t id, void * aux);

struct bitmap{
	uint32_t maxid;
	uint32_t filled;
	bool hasbuffer;
	uint64_t data;//must be last
};

struct bitmap_iterator{
	struct bitmap * bm;
	uint64_t mask;
	uint32_t id;
};

bool bitmap_set(struct bitmap * bm, uint32_t id);
bool bitmap_unset(struct bitmap * bm, uint32_t id);
bool bitmap_get(struct bitmap * bm, uint32_t id);
void bitmap_print(struct bitmap * bm);
void bitmap_apply(struct bitmap * bm, bitmap_apply_func func, void * aux);
int bitmap_getfirstzero(struct bitmap * bm, uint32_t size, uint32_t* id);
uint32_t bitmap_getfilled(struct bitmap * bm);

struct bitmap_iterator * bitmap_iterator_init(struct bitmap * bm, struct bitmap_iterator * bi);
bool bitmap_iterator_next(struct bitmap_iterator * bi, uint32_t* id);

struct bitmap * bitmap_init(uint32_t maxid, uint8_t * buffer);
void bitmap_finish(struct bitmap * bm);
void bitmap_clear(struct bitmap * bm);
void bitmap_setall(struct bitmap * bm);

#endif /* bitmap.h */
