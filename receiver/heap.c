#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "heap.h"

// TODO: may do optimizations from  http://stackoverflow.com/questions/6531543/efficient-implementation-of-binary-heaps
 
// Prepares the heap for use
struct heap * heap_init(uint16_t size, heap_cmp_func cmp_func, heap_index_func index_func){
	struct heap * h = (struct heap *) malloc (sizeof (struct heap) + sizeof(heap_type)*(size-1));
	h->size = size;
	h->count = 0;
	h->cmp_func = cmp_func;
	h->index_func = index_func;
	h->data = NULL;
	return h;
}

void heap_finish(struct heap * h){
	free(h);
}
 
// Inserts element to the heap
void heap_push(struct heap * h, heap_type value){
	unsigned int index, parent;
	if (h->count >= h->size){ //check size
		return;
	}
	
	// Find out where to put the element and put it
	heap_type * data = &h->data;
	for (index = h->count++; index; index = parent){
		parent = (index - 1) >> 1;
		if (h->cmp_func(data[parent], value)) {
			break;
		}
		data[index] = data[parent];
		h->index_func(data[index], index);
	}
	data[index] = value;
	h->index_func(data[index], index);
}

void reorder(struct heap * h, heap_type candidate, uint16_t index){
	uint16_t swap, other;
	heap_type * data = &h->data;
	// Reorder the elements
	for(; 1; index = swap){
		// Find the child to swap with
		swap = (index << 1) + 1;
		if (swap >= h->count) { // If there are no children, the heap is reordered
			break;
		} 
		other = swap + 1;
		if ((other < h->count) && h->cmp_func(data[other], data[swap])) { //pick the best child to become parent
			swap = other;
		}
		if (h->cmp_func(candidate, data[swap])) { // If the bigger child is less than or equal to its parent, the heap is reordered
			break;
		}
		data[index] = data[swap];
		h->index_func(data[index], index);
	}
	data[index] = candidate;
	h->index_func(data[index], index);
}

void heap_replace(struct heap * h, heap_type value, uint16_t oldIndex){
	reorder(h, value, oldIndex);
}

// avoid heapify twice
heap_type heap_push_pop (struct heap * h, heap_type value){
	if (h->cmp_func(value, h->data) || h->count == 0){
		return value; //no need to add, new value is already min
	}
	//replace value instead of the root element
	heap_type output = h->data;
	reorder(h, value, 0);
	return output;
}
 
// Removes the root element from the heap
heap_type heap_pop(struct heap * h){
	// Remove the root element
	heap_type output = h->data;
	heap_type * data = &h->data;
	reorder(h, data[--h->count], 0);
	return output;
}

uint16_t heap_size(struct heap * h){
	return h->count;
}

bool num_less_func(void * a, void * b){
	return (*((uint32_t *)a)) <= (*((uint32_t *)b));
}

bool num_less_func2(void * a, void * b){
	return (((uint32_t *)a)) <= (((uint32_t *)b));
}

void void_index_func(void * a, uint16_t index){
}

/*int main(int argc, char **argv) {
	int size = 128;
	int num = 1000000;
	if (argc>1){
		size = atoi(argv[1]);
		if (argc>2){
			num = atoi(argv[2]);
		}
	}
	printf("%d: ", size);
	struct heap * h = heap_init(size, num_less_func, void_index_func);
	srand(0x1238f2a9);
	uint32_t data0[num];
	int i;
	{
		for (i=0; i<num; i++){		
			data0[i]= (uint32_t)rand();
		}
		
		for (i=0; i<num; i++){
//          printf("%lu, ", r);
			heap_push(h, &data0[i]);
		}
	}
	
	uint32_t data1[num];
	for (i=0; i<num; i++){		
		data1[i]= (uint32_t)rand();
	}
	struct timeval tic, toc, tdiff;
	gettimeofday(&tic, NULL);
	for (i=0; i<num; i++){
		heap_push_pop(h, &data1[i]);
	}
	gettimeofday(&toc, NULL);
	timersub(&toc, &tic, &tdiff);
	double f = tdiff.tv_sec*1e6+tdiff.tv_usec;
	printf("%f count %d\n", f/num, h->count);
	uint64_t sum=0;
	while (h->count>0){
		sum+=*((uint32_t *)heap_pop(h));
	}
	printf("%llu\n", (long long unsigned)sum);
	
	heap_finish(h);
}*/
