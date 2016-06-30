//got first version from https://gist.github.com/martinkunev/1365481
#ifndef HEAP_H
#define HEAP_H 1
#include <stdint.h>
#include "stdbool.h"

typedef void* heap_type;
typedef bool (*heap_cmp_func)(void * a, void * b);
typedef void (*heap_index_func)(void * a, uint16_t index);

struct heap{
	unsigned int size; // Size of the allocated memory (in number of items)
	unsigned int count; // Count of the elements in the heap
	heap_cmp_func cmp_func;
	heap_index_func index_func;
	heap_type data; // Array with the elements
};
 
struct heap * heap_init(uint16_t size, heap_cmp_func cmp_func, heap_index_func index_func);
void heap_finish(struct heap *);
void heap_push(struct heap * h, heap_type value);
heap_type heap_pop(struct heap * h);
heap_type heap_push_pop (struct heap * h, heap_type value);
uint16_t heap_size(struct heap * h);
 
void heapify(heap_type data[], unsigned int count);

#endif /* heap.h */
