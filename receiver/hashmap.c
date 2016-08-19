#include "hashmap.h"
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "util.h"

#define getdata(H2, EZ) (((uint8_t *)EZ)-H2->elem_offset)

#define getelem(H2, DZ) ((hashmap_elem *)(((uint8_t *)DZ)+H2->elem_offset))
#define getelem2(H2, OFFSET) ((hashmap_elem *)((H2->buffer + OFFSET * H2->entry_size)+H2->elem_offset))
#define getindex(H2, e) ((((uint8_t *)e)-H2->buffer)/H2->entry_size)

#define NULL_PREV 0xffffffff //don't rely on this to check if prev exits or not, it is just for debugging

//TODO: using bitmap is not well tested in the new implementation

int findemptycollisionentry(struct hashmap * h2, void * data, void * aux);
void swapentries(struct hashmap * h2, void * data1, void * data2, hashmap_elem * e_last2);
bool replace(hashmap_elem * e, void * data, void * data2, hashmap_replace_func replace_func, void * aux);
void copyelem(hashmap_elem * e1, hashmap_elem *e2);

uint16_t hashmap_fullpercent(struct hashmap * h2){
	return h2->filled * 100 / h2->size;
}


uint32_t hashmap_getfull(struct hashmap * h2){
	return h2->filled + h2->collisions;
}

void hashmap_remove(struct hashmap * h2, void * data2){
	if ((uint8_t *)data2 < h2->buffer || (uint8_t *)data2 > h2->bufferlastentry){
		fprintf(stderr, "hashmap_remove: invalid pointer %p not in [%p, %p]\n", data2, h2->buffer, h2->bufferlastentry);
		return;
	}
	hashmap_elem * e = getelem(h2, data2);
	if (e->next == NULL_NEXT){
		fprintf(stderr, "hashmap_remove: null entry %d %p\n", e->next, data2);
		return;
	}
	if ((uint8_t *) data2 < h2->collision_start){
		if (e->next != CHAIN_END_NEXT){
			//must replace an entry here
			hashmap_elem * e2 = e;
			//find the last element of this linked list
			do {
				e2 = getelem2(h2, e2->next);
			}while (e2->next != CHAIN_END_NEXT);

			if (h2->bm != NULL && !bitmap_unset(h2->bm, e2->next)){
				fprintf(stderr, "hashmap: invalid bitmap unset for remove %lu", (long unsigned)e2->next);
			}
			hashmap_elem * e_prev = getelem2(h2, e2->prev);
			e_prev->next = CHAIN_END_NEXT;
		
			e2->next = e->next; //for copy
			e2->prev = NULL_PREV;

			data2 = getdata(h2, e2);
			memcpy(getdata(h2, e), data2, h2->entry_size);
			memset(data2, 0, h2->entry_size);
			e2->next = NULL_NEXT;
			e2->prev = NULL_PREV;
			h2->collisions--;
			return;
		}else{
			h2->filled--;
			if (h2->bm != NULL){
				uint32_t index = getindex(h2,e); //use e instead of data2 as data2 isn't updated
				if (!bitmap_unset(h2->bm, index)){
					fprintf(stderr, "hashmap: invalid bitmap unset for remove %lu", (long unsigned)index);
				}
			}
		}
	}else{
		h2->collisions--;
		if (e->prev == NULL_PREV){
			fprintf(stderr, "hashamp: invalid prev at %ld\n", getindex(h2, e));
		}
		hashmap_elem * e_prev = getelem2(h2, e->prev);
		if (h2->bm != NULL && !bitmap_unset(h2->bm, e_prev->next)){
			fprintf(stderr, "hashmap: invalid bitmap unset for remove %lu", (long unsigned)e_prev->next);
		}
		e_prev->next = e->next;
		if (e->next != CHAIN_END_NEXT){
			hashmap_elem * e_next = getelem2(h2, e->next);
			e_next->prev = e->prev;
		}
	}
	// just zero this entry
	memset(data2, 0, h2->entry_size);
	e->next = NULL_NEXT;
	e->prev = NULL_PREV;
}

struct hashmap * hashmap_init(uint32_t size, uint32_t collision_size, uint32_t entry_size, uint16_t elem_offset,  hashmap_replace_func replace_func){
	struct hashmap * h2 = MALLOC (sizeof (struct hashmap));
	int totalsize = (size + collision_size) * entry_size;
	if (totalsize < (1<<16)){
		h2->buffer = MALLOC(totalsize);
	}else{
		h2->buffer = BIGMALLOC(totalsize);
	}
	if (h2->buffer == NULL){
		fprintf(stderr, "HASHMAP: Cannot get buffer memory\n");
		FREE(h2);
		return NULL;
	}
	h2->entry_size = entry_size; 
	h2->elem_offset = elem_offset;
	h2->size = size;
	h2->filled = 0;
	h2->collisions = 0;
	h2->collision_size = collision_size;
	h2->hashmask = gbp(size)-1;
	h2->collision_start = h2->buffer + h2->size * h2->entry_size;
	h2->bufferlastentry = h2->buffer + (h2->size + h2->collision_size - 2) * h2->entry_size; // keep one entry at the end for swapping contents
//	h->bm = bitmap_init(h->size + h->collision_size, NULL);
	h2->bm = NULL;
	hashmap_clear(h2);
	h2->lru_func = NULL;
	h2->replace_func = replace_func;
	return h2;
}

void hashmap_print(struct hashmap * h2){
	printf ("size=%"PRIu32", collision_size=%"PRIu32", entry_size=%d, collision_end=%"PRIu32", filled=%"PRIu32", collisions=%"PRIu32"\n", 
		h2->size, h2->collision_size, h2->entry_size, (uint32_t)( (h2->last_collision_pointer-h2->buffer)/h2->entry_size-h2->size), h2->filled, h2->collisions);
	if (h2->bm != NULL){
		bitmap_print(h2->bm);
	}
}
void hashmap_finish(struct hashmap * h2){
//	hashmap_print(h2);
	if (h2->bm != NULL){
		bitmap_finish(h2->bm);
	}
	int totalsize = (h2->size + h2->collision_size) * h2->entry_size;
	if (totalsize < (1<<16)){
		FREE(h2->buffer);
	}else{
		BIGFREE(h2->buffer);
	}
	FREE(h2);
}

void hashmap_clear(struct hashmap * h){
	memset(h->buffer, 0, (h->size + h->collision_size)  * h->entry_size);
	h->last_collision_pointer = h->buffer + h->size * h->entry_size;
	if (h->bm != NULL){
		bitmap_clear(h->bm);
	}
	h->filled = 0;
	h->collisions = 0;
}

uint32_t hashmap_byte_size(struct hashmap * h){
	return (h->size + h->collision_size) * h->entry_size;
}

inline void hashmap_prefetch(struct hashmap * h2, uint32_t h){
	h &= h2->hashmask;
	HASH_PREFETCH0(h2->buffer + h2->entry_size * h);
}

void hashmap_setlru(struct hashmap * h2, hashmap_comparison_func func){
	h2->lru_func = func;
}

// 0 found empty
// 1 error
// 2 replace

int findemptycollisionentry(struct hashmap * h2, void * data, void * aux){
	//find an empty entry in the collision list
	if (h2->replace_func != NULL){
		//go linearly over the memory
		uint8_t * d_col = h2->last_collision_pointer;
		uint8_t * d_origin = d_col;
		hashmap_elem * e = getelem(h2, d_col);
		while (e->next != NULL_NEXT){
			if (replace(e, data, d_col, h2->replace_func, aux)){
				h2->last_collision_pointer = d_col;
				return 2;
			}else{
				//printf("H %d %d \n", e->next, e->prev);
			}
			d_col = d_col + h2->entry_size;
			if (d_col > h2->bufferlastentry){
				d_col = h2->collision_start;
			}
			if (d_col == d_origin){
				fprintf(stderr, "ERR, Hashmap, filled collision part\n");
				hashmap_print(h2);
				exit(1);
				return 1;
			}
			e = getelem(h2, d_col);
		}
		h2->last_collision_pointer = d_col;

		return 0;
	}

	if (h2->collisions==h2->collision_size){
		//need new memory
		LOG("hashmap %p: filled collision\n", h2);
		exit(1);
		return 1;
	}
	if (h2->bm != NULL){
		uint32_t index;
		if (!bitmap_getfirstzero(h2->bm, h2->size, &index)){
			h2->last_collision_pointer = h2->buffer + index * h2->entry_size;
		}else{
			fprintf(stderr, "ERR, Hashmap, filled collision part\n");
			return 1;
		}
	}else{
		//go linearly over the memory
		uint8_t * d_col = h2->last_collision_pointer;
		uint8_t * d_origin = d_col;
		while (getelem(h2, d_col)->next != NULL_NEXT){
			d_col = d_col + h2->entry_size;
			if (d_col > h2->bufferlastentry){
				d_col = h2->collision_start;
			}
			if (d_col == d_origin){
				fprintf(stderr, "ERR, Hashmap, filled collision part\n");
				return 1;
			}
		}
		h2->last_collision_pointer = d_col;
	}
	return 0;
}

inline void copyelem(hashmap_elem * e1, hashmap_elem * e2){
	e1->next = e2->next;
	e1->prev = e2->prev;
}

//data1 and data2 are consecutive entries in a collision list
void swapentries(struct hashmap * h2, void * data1, void * data2, hashmap_elem * e_last2){
	if (e_last2 == NULL){
		// e1 is the head. There is no way other than swapping the content!
		hashmap_elem e1, e2; 
		copyelem(&e1, getelem(h2, data1));
		copyelem(&e2, getelem(h2, data2));
		memcpy(h2->bufferlastentry+h2->entry_size, data1, h2->entry_size);
		memcpy(data1, data2, h2->entry_size);
		memcpy(data2, h2->bufferlastentry+h2->entry_size, h2->entry_size);
		copyelem(getelem(h2, data1), &e1);
		copyelem(getelem(h2, data2), &e2);
	}else{
		hashmap_elem * e1 = getelem(h2, data1);
		hashmap_elem * e2 = getelem(h2, data2);
		//just change pointers
		// e_last2, e1, e2, x
		// e_last2, e2, e1, x
		hashmap_elem temp;
		copyelem(&temp, e1);
		e1->prev = e1->next;//e2
		e1->next = e2->next;//x
		if (e2->next != CHAIN_END_NEXT){
			hashmap_elem * x = getelem2(h2, e2->next);
			x->prev = e2->prev; //e1
		}
		e2->next = e2->prev; //e1
		e2->prev = temp.prev;//last
		e_last2->next = temp.next;//e2
	}
	
}

inline bool replace(hashmap_elem * e, void * data, void * data2, hashmap_replace_func replace_func, void * aux){
	hashmap_elem e2;
	copyelem(&e2, e);
	if (replace_func(data, data2, aux)){
		copyelem(e, &e2);
		return true;
	}
	return false;
}

static inline void initentry(struct hashmap* h2, void * data, void * data2, hashmap_init_func init, void * aux){
	if (init == NULL){
		memcpy(data2, data, h2->entry_size); //update elem after this
	}else{
		init(data, data2, aux);
	}
}

/**
 * returns 0 if cannot add
 * on equal does nothing
 */
void * hashmap_add2 (struct hashmap* h2, void * data, uint32_t h, hashmap_equal_func equal_func, hashmap_init_func init, void * aux){
	void * data_last, * data2;
	hashmap_elem * e_last, * e_last2;
	h &= h2->hashmask;
	data2 = h2->buffer + h2->entry_size * h;
	hashmap_elem * e = getelem(h2, data2);
	if (e->next == NULL_NEXT){
		initentry(h2, data, data2, init, aux); //update elem after this
		e->next = CHAIN_END_NEXT;
		e->prev = NULL_PREV;
		h2->filled++;
		if (h2->bm != NULL && bitmap_set(h2->bm, h)){
			fprintf(stderr, "hashmap: invalid bitmap set for new entry %lu", (long unsigned)h);
		}
		return data2;
	}else if (equal_func(data, data2, aux)){
		  //do nothing
		return data2;
	}else{
		if (h2->replace_func != NULL && replace(e, data, data2, h2->replace_func, aux)){
			return data2;
		}


		e_last = NULL;
	//	uint32_t chainlen = 1;
		while (e->next != CHAIN_END_NEXT) { //walk over the chain
			e_last2 = e_last;
			e_last = e;
			data_last = data2;
			e = getelem2(h2, e->next);
			data2 = getdata(h2, e);
			if (equal_func(data, data2, aux)){
				if (h2->lru_func != NULL){
					if (h2->lru_func(data_last, data2) < 0){
						swapentries(h2, data_last, data2, e_last2);
					}
				}
				return data2;
			}else if (h2->replace_func != NULL && replace(e, data, data2, h2->replace_func, aux)){
				return data2;
			}
/*			else if (chainlen>2){
				//FIXME: needs to finish entries
				initentry(h2, data, data2, init, aux);
				return data2;
			}
			chainlen++;*/
		}	
		//didn't find in the chain. Add it to the end of chain
		if (getelem(h2, h2->last_collision_pointer)->next != NULL_NEXT){// is finding an empty entry easy?
			int x = findemptycollisionentry(h2, data, aux);
			if (x == 1){
				return NULL;
			}else if (x == 2){//it is replace!
				hashmap_elem * e2 = getelem(h2, h2->last_collision_pointer);
				if (e2->prev == NULL_PREV){
					fprintf(stderr, "hashamp: invalid prev at %ld\n", getindex(h2, e2));
				}
				hashmap_elem * e2_prev = getelem2(h2, e2->prev); //it must have a prev
				uint32_t index = e2_prev->next;
//				printf("r %ld prev %ld %ld \n", getindex(h2, e2), getindex(h2, e2_prev), getindex(h2, e));
				e2_prev->next = e2->next;
				if (e2->next != CHAIN_END_NEXT){
					hashmap_elem * e2_next = getelem2(h2, e2->next);
					e2_next->prev = e2->prev;
				}
				e->next = index;
				e2->prev = getindex(h2, e);
				e2->next = CHAIN_END_NEXT;
				return getdata(h2, e2);
			}
		}

		e->next = getindex(h2, h2->last_collision_pointer);
		if (h2->bm != NULL && bitmap_set(h2->bm, e->next)){
			fprintf(stderr, "hashmap: invalid bitmap set for new entry in collision %lu", (long unsigned)e->next);
		}

		uint32_t prev = getindex(h2, e);
		e = getelem(h2, h2->last_collision_pointer);
		data2 = getdata(h2, e);
		initentry(h2, data, data2, init, aux); //update elem after this
		e->next = CHAIN_END_NEXT;
		e->prev = prev;

		h2->last_collision_pointer += h2->entry_size;
		if (h2->last_collision_pointer > h2->bufferlastentry){
			h2->last_collision_pointer = h2->collision_start;
		}
		h2->collisions++;
		return data2;
	}
}

void * hashmap_get2 (struct hashmap * h2, void * data, uint32_t h, hashmap_equal_func equal_func, void * aux){
	h &= h2->hashmask;
	void * data2; 
	hashmap_elem * e = getelem2(h2, h);
	if (e->next != NULL_NEXT){
		while (true){
			data2 = getdata(h2, e);
			if (equal_func(data, data2, aux)){
				return data2;
			}
			if (e->next == CHAIN_END_NEXT){
				break;
			}
			e = getelem2(h2, e->next);
		}
	}
	return NULL;
}

void hashmap_apply(struct hashmap * h2, hashmap_apply_func func, void * aux){
	uint8_t * data2;
	if (h2->bm != NULL && h2->filled < h2->size * 0.75){
		struct bitmap_iterator bi;
		bitmap_iterator_init(h2->bm, &bi);
		uint32_t seen = h2->filled + h2->collisions;
		uint32_t id;
		while (bitmap_iterator_next(&bi, &id) && seen > 0){
			data2 = h2->buffer + id * h2->entry_size;
			hashmap_elem * e = getelem(h2, data2);
			if (e->next == NULL_NEXT){
				printf("hashmap: bitmap iterator returns null entry %u %u!\n", id, h2->size);
				bitmap_print(h2->bm);			
			}
			do {
				seen--;
			}while (!func(data2, aux));
		}
		if (bitmap_iterator_next(&bi, &id)){
			printf("hashmap: want to see %u but seen %u for filled %u\n", id, seen, h2->filled);
			printf("%u %lx\n", bi.id, bi.mask);
		}
	}else{
		uint32_t h;
		uint32_t seen = 0;
		uint32_t tosee = h2->filled; //this may change over the loop
		uint32_t seen_filled = 0;
		data2 = h2->buffer;
		for (h = 0; (h < h2->size) && (seen_filled < tosee); h++){
			hashmap_elem  * e = getelem(h2, data2);
			if (e->next != NULL_NEXT){
				seen_filled++;
				seen++;
				while (!func(data2, aux)){
					//func may have deleted the element or replaced it with another
					if (e->next == NULL_NEXT){
						break;
					} 
					seen++;
				}
			}
			data2 += h2->entry_size;
		}
		if (seen_filled < tosee){
			fprintf(stderr, "hashmap: %p did not see all items %d %d %d\n", h2, seen, tosee, seen_filled);
		}
/*			//this can easily happen as collisions can move to here
		if (seen >tosee){			
			fprintf(stdout, "hashmap: %p saw too many %d %d\n", h2, seen, tosee);
		}*/
		
		//COLLISION PART
		uint32_t collisions = h2->collision_size;
		uint32_t seen_collisions = 0;
		uint32_t tosee_collisions = h2->collisions; //this may change over the loop
		data2 = h2->buffer + h2->entry_size * h2->size;
		for (h = h2->size; h < h2->size + collisions && seen_collisions < tosee_collisions; h++){
			hashmap_elem  * e = getelem(h2, data2);
			do {	
				if (e->next == NULL_NEXT){
					break;
				}
				seen_collisions++;
			}while(!func(data2, aux));
			data2 += h2->entry_size;
		}
		if (seen_collisions > tosee_collisions){
			fprintf(stderr, "hashmap: %p seen more collisions than should %u %u %u\n", h2, collisions, seen_collisions, tosee_collisions);
		}
		if (seen_collisions < tosee_collisions){
			fprintf(stderr, "hashmap: %p couldn't find enough collision entries %u %u %u\n", h2, collisions, seen_collisions, h2->collisions);
		}
	}
}

void hashmap_iterator_init(struct hashmap * h2, struct hashmap_iterator * hi){
	hi->h2 = h2;
	hi->data2 = h2->buffer;
	if (h2->bm != NULL && h2->filled < h2->size * 0.75){
		hi->bi = MALLOC(sizeof(struct bitmap_iterator));
		bitmap_iterator_init(h2->bm, hi->bi);
	}else{
		hi->bi = NULL;
		hi->seen_collisions = 0;
		hi->seen_filled = 0;
		hi->tosee_filled = h2->filled;
	}
}

void hashmap_iterator_finish(struct hashmap_iterator  * hi){
	if (hi->bi != NULL){
		FREE(hi->bi);
	}
}

bool hashmap_iterator_next(struct hashmap_iterator * hi, void ** data, bool removed){
	struct hashmap * h2 = hi->h2;
	if (removed){
		//look at previous element again
		uint8_t * data3 = hi->data2 - h2->entry_size;
		if (data3 < h2->buffer){
			printf("hashmap_iterator: invalid removed flag");
			return false;
		}
		hashmap_elem * e = getelem(h2, data3);
		if (e->next != NULL_NEXT){
			if (hi->bi == NULL && data3 >= h2->collision_start){
				hi->seen_collisions++;
			}
			*data = data3;
			return true;
		}
	}
	if (hi->bi != NULL){
		uint32_t h;
		if (bitmap_iterator_next(hi->bi, &h)){
			hi->data2 = (void *)(h2->buffer + h * h2->entry_size);
			hashmap_elem * e = getelem(h2, hi->data2);
			if (e->next == NULL_NEXT){
				printf("hashmap: bitmap iterator returns null entry %u %u!\n", h, h2->size);
				bitmap_print(h2->bm);			
			}
			*data = hi->data2;
			return true;
		}else{
			return false;
		}
	}else{
		while (hi->data2 < h2->collision_start && hi->seen_filled < hi->tosee_filled){ //h2->filled can change
			hashmap_elem * e = getelem(h2, hi->data2);
			if (e->next != NULL_NEXT){
				hi->seen_filled++;
				*data = hi->data2;
				hi->data2 += h2->entry_size;
				return true;
			}
			hi->data2 += h2->entry_size;
		}
		if (hi->data2 < h2->collision_start){
			hi->data2 = h2->collision_start;
		}
		while (hi->data2 <= h2->bufferlastentry && hi->seen_collisions < h2->collisions){
			hashmap_elem * e = getelem(h2, hi->data2);
			if (e->next != NULL_NEXT){
				hi->seen_collisions++;				
				*data = hi->data2;
				hi->data2 += h2->entry_size;
				return true;
			}
			hi->data2 += h2->entry_size;
		}
		return false;
	}
}
