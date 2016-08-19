#ifndef HASHMAP_H
#define HASHMAP_H 1

#include <stdint.h>
#include "stdbool.h"
#include "bitmap.h"
#define NULL_NEXT 0
#define CHAIN_END_NEXT 1

/*
* prev element is to facilitate the removal algorithm, we don't have previous item in the linked list and don't want to hash & search to find the entry
*/
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

/*
* A general hashmap implementation that keeps collided items in a linked list. It divides the memory into two parts ordinary entries and collisions. Upon collision a slot from collision part will be allocated, so first entry of the linked list is in the ordinary part others are in the collision part (shared among all entriess).
* The structures should have at least one hashmap_elem field. This design comparing to adding that by hashmap itself, helps the user pack and align that hashmap_elem entry inside the datastructure.
* Some additional capabilities are to replace obsolete entries and sort the linked list (using give functions).
* The use can pass a function for initializing the new entry. So during addition, the user can just pass the key not the whole entry
* This is not multi-thread safe
*/
struct hashmap{
	uint8_t * buffer;	
	uint32_t hashmask; //mask according to the hashmap size
	uint32_t entry_size;
	uint32_t size;
	uint32_t filled; //how many ordinary entries are filled
	uint32_t collisions; // how may collision entries are filled
	uint16_t elem_offset; //the offset of element in the data put in this hashmap
	hashmap_equal_func equal_func;
	hashmap_replace_func replace_func;
	uint8_t * last_collision_pointer; //the next empty entry in the collision part
	uint8_t * bufferlastentry; //just a pointer to the last entry
	uint8_t * collision_start; //a pointer to the start of collision part
	uint32_t collision_size;
	struct bitmap* bm; //filled entries bitmap. Can be null
	hashmap_comparison_func lru_func;// if it set, it tries to sort collided entries based on most recently used
};

/*
* An iterator over the hashmap. 
* It is not safe to change the hashmap during iteration and it is not checked. 
* You can only remove entries and pass that event to the hashmap_iterator_next function.
*/
struct hashmap_iterator{
	struct hashmap * h2;
	uint8_t * data2;
	struct bitmap_iterator *bi;
	uint32_t seen_collisions;
	uint32_t seen_filled;
	uint32_t tosee_filled;
};

/*
* elem_offset: the offset of the hashmap_elem entry in the data structure that is going to be saved in the map. You can find it using offsetof function.
* entry_size: It is recommended to use a size multiple of 4 and even better multiple of 64 (cache line size)
* replace_func: a function that returns true/false to replace an entry upon collision. This can be null
*/
struct hashmap * hashmap_init(uint32_t size, uint32_t collision_size, uint32_t entry_size, uint16_t elem_offset, hashmap_replace_func replace_func);
void hashmap_finish(struct hashmap * h2);

/*
* What percent of ordinary slots are full.
*/
uint16_t hashmap_fullpercent (struct hashmap * h2);

/*
* Returns tthe number of items saved in the hashmap
*/
uint32_t hashmap_getfull(struct hashmap * h2);

/*
* return the memory usage of the hashmap
*/
uint32_t hashmap_byte_size(struct hashmap * h2);

/*
* Upon walking the linkedlist of an entry while adding an entry, the hashmap can swap entries to sort entries (say keep most recently used one close to the head of the linked list for faster retrieval)
*/
void hashmap_setlru(struct hashmap * h2, hashmap_comparison_func func);

void hashmap_print(struct hashmap * h2);

/*
* remove all entries
*/
void hashmap_clear(struct hashmap * h2);

/*
* apply a function on all hashmap entries
*/
void hashmap_apply(struct hashmap * h2, hashmap_apply_func func, void * aux);

/**
* returns 0 if cannot add
* equal_func: assume the parameter data is the first entry to equal_func. So data can only have the key not the value
* init_func: the function to initialize the entry in the hashmap. If it is null the parameter data will be memcpy to the entry
* aux: a data that will be passsed to equal, init and replace functions
* returns a pointer to the entry in the hashmap, or NULL if it ccannot add it.
 */
void * hashmap_add2 (struct hashmap* h2, void * data, uint32_t h, hashmap_equal_func equal_func, hashmap_init_func init, void * aux);

/**
 * assume data is the first entry to equal_func, this allows just give the key as data
 * aux is passed to the equal function
*/
void * hashmap_get2 (struct hashmap * h2, void * data, uint32_t h, hashmap_equal_func equal_func, void * aux);

/*
* Remove an entry already saved in the hashmap
* After remove, the entry will be zeroed
*/
void hashmap_remove(struct hashmap * h2, void * data2);

/*
* Prefetch the ordinary slot corresponding to the hash h
*/
void hashmap_prefetch(struct hashmap * h2, uint32_t h);

/*
* data: the pointer to the next available entry
* removed: should be set to true, if the last entry that is returned is removed from the map
* returns false if no entries available
*/
bool hashmap_iterator_next(struct hashmap_iterator * hi, void ** data, bool removed);
void hashmap_iterator_init(struct hashmap * h2, struct hashmap_iterator * hi);
void hashmap_iterator_finish(struct hashmap_iterator  * hi);


#endif /* hashmap.h */
