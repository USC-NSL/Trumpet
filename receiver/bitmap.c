#include "bitmap.h"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "util.h"

#define  MASK_1 ((uint64_t)0x0000000000000001)

uint64_t * getdata(struct bitmap * bm, int i);

inline uint64_t * getdata(struct bitmap * bm, int i){
	return (&bm->data) + i;
}


bool bitmap_set(struct bitmap * bm, uint32_t id){
	if (id >= bm->maxid){
		printf("Too large id %u vs %u\n", id, bm->maxid);
		return false;
	}else{
		uint64_t mask = MASK_1<<(id & BITMAP_ENTRY_SIZE_MOD);
		uint64_t * entry = getdata(bm, id >> BITMAP_ENTRY_SIZE_LOG);
		bool out = (*entry) & mask;
		if (!out){
			bm->filled++;
		}
		*entry |= mask;
		return out;
	}
}


bool bitmap_unset(struct bitmap * bm, uint32_t id){
	if (id >= bm->maxid){
		printf("Too large id %u vs %u\n", id, bm->maxid);
	}else{
		uint64_t mask = MASK_1<<(id & BITMAP_ENTRY_SIZE_MOD);
		uint64_t * entry = getdata(bm, id >> BITMAP_ENTRY_SIZE_LOG);
		bool out = (*entry) & mask;
		if (out){
			bm->filled--;
		}
		*entry &= ~mask;
		return out;
	}
	return false;
}

bool bitmap_get(struct bitmap * bm, uint32_t id){
	if (id > bm->maxid){
		printf("Too large id %u vs %u\n", id, bm->maxid);
		return false;
	}else{
		return (*getdata(bm, id >> BITMAP_ENTRY_SIZE_LOG)) & (MASK_1<<(id & BITMAP_ENTRY_SIZE_MOD));
	}
}

void bitmap_print(struct bitmap * bm){
	int max = BITMAP_MAX_ENTRY(bm->maxid);
	int i;
	for (i = max-1; i >= 0; i--){
		printf("%016llx", (long long unsigned)*getdata(bm, i));
		if (i%64==0){
			printf("\n");
		}
	}
	printf("\n");
}

// start must be multiple of 64
int bitmap_getfirstzero(struct bitmap * bm, uint32_t start, uint32_t* id){
	uint32_t i;
	
	for (i = (start >> BITMAP_ENTRY_SIZE_LOG); i < BITMAP_MAX_ENTRY(bm->maxid); i++){
		uint64_t * entry = getdata(bm, i);
		if (*entry < 0xFFFFFFFFFFFFFFFF){
			*id = (i<<BITMAP_ENTRY_SIZE_LOG) + countTrailing0M(~ (*entry));
			return 0;
		}
	}
	*id = 0;
	return 1;
}


void bitmap_apply(struct bitmap * bm, bitmap_apply_func func, void * aux){
	uint32_t i;
	uint32_t id = 0;
	uint64_t mask = 1;
	for (i = 0; i < bm->maxid; i++){
		if ((*getdata(bm, i)) & mask){
			func(id++, aux);
		}
		mask <<= 1;
	}
}

struct bitmap_iterator * bitmap_iterator_init(struct bitmap * bm, struct bitmap_iterator * bi){
	bi->bm = bm;
	bi->id = 0;
	bi->mask = 1;
	return bi;
}

bool bitmap_iterator_next(struct bitmap_iterator * bi, uint32_t * id){
	//skip zero maps
	if (bi->mask == 1){
		while (bi->id < bi->bm->maxid && *getdata(bi->bm, bi->id >> BITMAP_ENTRY_SIZE_LOG) == 0){
			bi->id += BITMAP_ENTRY_SIZE;
		}
	}
	while (bi->id < bi->bm->maxid){	
		if (*getdata(bi->bm, bi->id >> BITMAP_ENTRY_SIZE_LOG) & bi->mask){
			*id = bi->id;
			bi->id++;
			bi->mask <<= 1;
			if (bi->mask == 0){
				bi->mask = 1;
			}
			return true;
		}
		bi->id++;
		bi->mask <<= 1;
		if (bi->mask == 0){
			bi->mask = 1;
		}
	}
	return false;
}

uint32_t bitmap_getfilled(struct bitmap * bm){
	return bm->filled;
}

void bitmap_clear(struct bitmap * bm){
	int max = BITMAP_MAX_ENTRY(bm->maxid);
	max *= BITMAP_ENTRY_SIZE / 8;
	memset(&bm->data, 0, max);
	bm->filled = 0;
}

void bitmap_setall(struct bitmap * bm){
	int max = BITMAP_MAX_ENTRY(bm->maxid);
	max *= BITMAP_ENTRY_SIZE / 8;
	memset(&bm->data, 0xff, max);
	bm->filled = bm->maxid;
}

struct bitmap * bitmap_init(uint32_t maxid, uint8_t * buffer){
	int max = BITMAP_MAX_ENTRY(maxid);
	struct bitmap * bm;
	if (buffer == NULL){
		if (maxid > 1<<14){
			bm = (struct bitmap *) BIGMALLOC2 (sizeof(struct bitmap) + (max - 1) * BITMAP_ENTRY_SIZE/8, 8);
		}else{
			bm = (struct bitmap *) MALLOC2 (sizeof(struct bitmap) + (max - 1) * BITMAP_ENTRY_SIZE/8, 8);
		}
	}else{
		bm = (struct bitmap *) buffer;
	}
	bm->hasbuffer = buffer != NULL;
	bm->maxid = maxid; 
	bitmap_clear(bm);
	return bm;
}
void bitmap_finish(struct bitmap * bm){
	if (!bm->hasbuffer){
		if (bm->maxid > 1<<14){
			BIGFREE(bm);
		}else{
			FREE(bm);
		}
	}
}
