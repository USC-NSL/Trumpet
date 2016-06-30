#ifndef HASHMAP_H
#define HASHMAP_H 1

#include <stdint.h>
#include "stdbool.h"
#include "bitmap.h"
#define NULL_NEXT 0
#define CHAIN_END_NEXT 1

typedef struct {
	uint32_t next;
	uint32_t prev;
} hashmap_elem;

#define list_entry(HASH_ELEM, STRUCT, MEMBER)           \
        ((STRUCT *) ((uint8_t *) &(HASH_ELEM)->next     \
                     - offsetof (STRUCT, MEMBER.next)))

typedef uint32_t (*hashmap_hash_func)(void * data);
//data1 is your input, data2 will be whatever in map
typedef bool (*hashmap_equal_func)(void * data1, void * data2, void * aux);
typedef bool (*hashmap_apply_func)(void * data, void * aux);
typedef int (*hashmap_comparison_func)(void * data1, void * data2);
typedef void (*hashmap_init_func)(void * newdata, void * data, void * aux);
typedef bool (*hashmap_replace_func)(void * newdata, void * data, void * aux);


struct hashmap{
	uint8_t * buffer;	
	uint32_t hashmask;
	uint32_t entry_size;
	uint32_t size;
	uint32_t filled;
	uint32_t collisions;
	uint16_t elem_offset; //the offset of element in the data put in this hashmap
	hashmap_equal_func equal_func;
	hashmap_replace_func replace_func;
	uint8_t * last_collision_pointer;
	uint8_t * bufferlastentry;
	uint8_t * collision_start;
	uint32_t collision_size;
	struct bitmap* bm;
	hashmap_comparison_func lru_func;
};

struct hashmap_iterator{
	struct hashmap * h2;
	uint8_t * data2;
	struct bitmap_iterator *bi;
	uint32_t seen_collisions;
	uint32_t seen_filled;
	uint32_t tosee_filled;
};

// struct hashmap_elem{
	// uint16_t next; //is 0 for not full items, 1 for full items, and >1 if in a chain (map size>2);
	// uint16_t data; // this is actually a buffer that can be larger than this data type. so always put it at the end of struct. i put uint16_t to be the same as next pointer
// };

uint16_t hashmap_fullpercent (struct hashmap * h2);
struct hashmap * hashmap_init(uint32_t size, uint32_t collision_size, uint32_t entry_size, uint16_t elem_offset, hashmap_replace_func replace_func);
void hashmap_print(struct hashmap * h2);
void hashmap_finish(struct hashmap * h2);
uint32_t hashmap_byte_size(struct hashmap * h2);
void hashmap_clear(struct hashmap * h2);
void hashmap_remove(struct hashmap * h2, void * data2);
void hashmap_prefetch(struct hashmap * h2, uint32_t h);
void hashmap_apply(struct hashmap * h2, hashmap_apply_func func, void * aux);

/**
 * returns 0 if cannot add
 * assume data is the first entry to equal_func
 */
void * hashmap_add2 (struct hashmap* h2, void * data, uint32_t h, hashmap_equal_func equal_func, hashmap_init_func init, void * aux);
/**
 * assume data is the first entry to equal_func, this allows just give the key as data
*/
void * hashmap_get2 (struct hashmap * h2, void * data, uint32_t h, hashmap_equal_func equal_func, void * aux);
void hashmap_setlru(struct hashmap * h2, hashmap_comparison_func func);

bool hashmap_iterator_next(struct hashmap_iterator * hi, void ** data, bool removed);
void hashmap_iterator_init(struct hashmap * h2, struct hashmap_iterator * hi);
void hashmap_iterator_finish(struct hashmap_iterator  * hi);


#endif /* hashmap.h */
