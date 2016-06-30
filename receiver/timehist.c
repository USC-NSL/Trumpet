#include "timehist.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#define ONE_BW_NS 8

uint32_t timehist_bucket_sum_pairfunc(struct timehist_bucket * b1, struct timehist_bucket * b2){
	return b1->size + b2->size;
}

bool timehist_bucket_paircmp(void * a, void * b){
	return ((struct timehist_bucket *)a)->pair<=((struct timehist_bucket *)b)->pair;
}

void timehist_bucket_index(void * a, uint16_t index){
	((struct timehist_bucket *)a)->heap_index = index;
}

struct timehist * timehist_init(uint16_t size){
	struct timehist * th = malloc(sizeof (struct timehist));
	th->h = heap_init(size, timehist_bucket_paircmp, timehist_bucket_index);
	th->buckets = malloc (sizeof(struct timehist_bucket)*(size+1)); //one for m+1
	th->ts = 0;
	th->pair_func = timehist_bucket_sum_pairfunc;
	th->size = size;
	th->filled = 0;
	th->emptybucket_index = 0;
	th->lastbucket_index = 0;
	return th;
}

void timehist_finish(struct timehist * th){
	heap_finish(th->h);
	free(th->buckets);
	free(th);
}

void timehist_add(struct timehist * th, uint64_t ts, uint16_t size){
	if (size == 0){
		return;
	}
	th->filled++;//must be before anything else
		
	if (th->filled == 1){
		th->ts = ts - size * ONE_BW_NS;
	}
	
	struct timehist_bucket * b_new = &th->buckets[th->emptybucket_index];
	b_new->ts = ts - th->ts;
	b_new->size = size;
	b_new->pair = 0;
	b_new->next = 0x0000FFFF;
	b_new->prev = 0x0000FFFF;
	b_new->heap_index = 0x0000FFFF;
	if (th->filled == 1){
		th->lastbucket_index = th->emptybucket_index;
		th->emptybucket_index++;
		return;
	}
	
	struct timehist_bucket * b_last = &th->buckets[th->lastbucket_index];
	b_last->pair = th->pair_func(b_last, b_new);
	b_last->next = th->emptybucket_index;
	b_new->prev = th->lastbucket_index;
	if (th->filled <= th->size){
		heap_push(th->h, b_last);//b_new doesn't need to be in heap
		th->lastbucket_index = th->emptybucket_index;
		th->emptybucket_index++;
	}else{//two buckets must merge
		struct timehist_bucket * bm1 = (struct timehist_bucket *)heap_push_pop(th->h, b_last);
		struct timehist_bucket * bm2 = &th->buckets[bm1->next];
		bm1->size += bm2->size;
		bm1->ts = bm2->ts;
		uint16_t nextempty = bm1->next; //bm2 address
		bm1->next = bm2->next;
		if (bm2->next<0x0000FFFF){ //Otherwise, it wasn't in the heap, because the last entry has no pair
			struct timehist_bucket * bm3 = &th->buckets[bm2->next];
			bm1->pair = th->pair_func(bm1, bm3);
			bm3->prev = bm2->prev;
			heap_replace(th->h, bm1, bm2->heap_index);//bm1 will replace bm2
		}
		if (bm1->prev<0x0000FFFF){
			struct timehist_bucket * bm_1 = &th->buckets[bm1->prev];
			bm_1->pair = th->pair_func(bm_1, bm1);
			//bm_1->next = bm2->prev; no need for this
			heap_replace(th->h, bm_1, bm_1->heap_index);
		}
		if (th->emptybucket_index != nextempty){
			th->lastbucket_index = th->emptybucket_index;
			th->emptybucket_index = nextempty;
		}
		
		th->filled--;//do it at last
	}
}

void timehist_bucket_print(struct timehist_bucket * b, void * aux){
	printf("ts: %lu, size: %lu, pair %lu, prev: %u, next: %u\n", (long unsigned)b->ts, (long unsigned)b->size, (long unsigned) b->pair, 
	(unsigned)b->prev, (unsigned)b->next);
}

void timehist_apply(struct timehist * th, timehist_bucket_apply_func apply_func, void * aux){
	if (th->filled == 0){
		return;
	}
	struct timehist_bucket * b = &th->buckets[0];//if we pushback bm1, 0 is always there
	while (true){
		apply_func(b, aux);
		if (b->next<0x0000FFFF){
			b = &th->buckets[b->next];
		}else{
			break;
		}
	}
}

int main(int argc, char **argv) {
	int i;
	int num = 100000;
	struct timehist * th = timehist_init(32);
	uint64_t ts = 0;
	srand(0x1238f2a9);
	struct timeval tic, toc, tdiff;
	gettimeofday(&tic, NULL);
	for (i=0; i<num; i++){
		timehist_add(th, ts, 1500);
		ts+=((uint32_t)rand())>>16;
	}
	gettimeofday(&toc, NULL);
	timersub(&toc, &tic, &tdiff);
        double f = tdiff.tv_sec*1e6+tdiff.tv_usec;
        printf("%f\n", f/num);

	timehist_apply(th, timehist_bucket_print , NULL);
	timehist_finish(th);
}
