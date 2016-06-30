#define _GNU_SOURCE
#include <sched.h>   //cpu_set_t , CPU_SET
#include "util.h"
#include <string.h> 
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "stdbool.h"

struct loguser * util_lu = NULL;

/*static pthread_mutex_t log_lock;

void util_init(void){
	pthread_mutexattr_t Attr;
	pthread_mutexattr_init(&Attr);
	pthread_mutexattr_settype(&Attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&log_lock, &Attr);
}

void util_finish(void){
	pthread_mutex_destroy(&log_lock);
}

void util_loglock(void){
	pthread_mutex_lock(&log_lock);
}

void util_logunlock(void){
	pthread_mutex_unlock(&log_lock);
}*/

// from http://stackoverflow.com/questions/1493936/faster-approach-to-checking-for-an-all-zero-buffer-in-c
inline bool is_empty2(void *buf2, uint32_t size){
    char * buf = (char *) buf2;
    return buf[0] == 0 && !memcmp(buf, buf + 1, size - 1);
}

//from http://stackoverflow.com/questions/21413565/create-a-mask-that-marks-the-most-significant-set-bit-using-only-bitwise-operat
uint32_t gbp(uint32_t m) {
 // return most significant bit of n
 //uint32_t m;
 //m = n;
  m = m | m >> 1;
  m = m | m >> 2;
  m = m | m >> 4;
  m = m | m >> 8;
  m = m | m >> 16;
  m = m & (~m >> 1);
  return m;
}



// from http://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers

int log2_32 (uint32_t value){
    static const int tab32[32] = {
     0,  9,  1, 10, 13, 21,  2, 29,
    11, 14, 16, 18, 22, 25,  3, 30,
     8, 12, 20, 28, 15, 17, 24,  7,
    19, 27, 23,  6, 26,  5,  4, 31};
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    return tab32[(uint32_t)(value*0x07C4ACDD) >> 27];
}

int log2_64(uint64_t n){
    static const int table[64] = {
        0, 58, 1, 59, 47, 53, 2, 60, 39, 48, 27, 54, 33, 42, 3, 61,
        51, 37, 40, 49, 18, 28, 20, 55, 30, 34, 11, 43, 14, 22, 4, 62,
        57, 46, 52, 38, 26, 32, 41, 50, 36, 17, 19, 29, 10, 13, 21, 56,
        45, 25, 31, 35, 16, 9, 12, 44, 24, 15, 8, 23, 7, 6, 5, 63 };

    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;

    return table[(n * 0x03f6eaf2cd271461) >> 58];
}

//return 0 for both 0 and 1
//find first bit that equals to 1 
unsigned int countTrailing0M(uint64_t v) {
 static const char multiplyDeBruijnBitPosition[64] = {
            0, 1, 2, 56, 3, 32, 57, 46, 29, 4, 20, 33, 7, 58, 11, 47, 
            62, 30, 18, 5, 16, 21, 34, 23, 53, 8, 59, 36, 25, 12, 48, 39, 
            63, 55, 31, 45, 28, 19, 6, 10, 61, 17, 15, 22, 52, 35, 24, 38, 
            54, 44, 27, 9, 60, 14, 51, 37, 43, 26, 13, 50, 42, 49, 41, 40
        };
        return multiplyDeBruijnBitPosition[((uint64_t)((v & -v) * 0x26752B916FC7B0DULL)) >> 58];
}

/*unsigned long long rdtscl(void){
	unsigned long long int lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return lo|( hi<<32 );
}*/

uint16_t entry_size_64(uint16_t m){
	uint32_t entry_size = gbp(m);
	if (entry_size<m && m<64){
		return m;
	}
	while (entry_size<m && m>64){
		entry_size+=64;
	}
	return entry_size;
}

void set_CPU(int cpu ){
        cpu_set_t cpuset;

  //the CPU we whant to use

          CPU_ZERO(&cpuset);       //clears the cpuset
          CPU_SET( cpu , &cpuset); //set CPU 2 on cpuset


  /*
   * cpu affinity for the calling thread
   * first parameter is the pid, 0 = calling thread
   * second parameter is the size of your cpuset
   * third param is the cpuset in which your thread will be
   * placed. Each bit represents a CPU
   */
          sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

void * myalign(int size, int align){
        void * ptr;
        if (posix_memalign(&ptr, align, size)){
		fprintf(stderr, "util:cannot allocate memory\n");
		return NULL;
	}
        return ptr;
}

#define NELEMS(x)  (sizeof(x) / sizeof(x[0]))
//from http://stackoverflow.com/questions/6127503/shuffle-array-in-c

/* arrange the N elements of ARRAY in random order.
 * Only effective if N is much smaller than RAND_MAX;
 * if this may not be the case, use a better random
 * number generator. */
void shuffle(void *array, size_t n, size_t size) {
    char tmp[size];
    char *arr = array;
    size_t stride = size * sizeof(char);
    srand(0x23a82bc0);
    if (n > 1) {
        size_t i;
        for (i = 0; i < n - 1; ++i) {
            size_t rnd = (size_t) rand();
            size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

            memcpy(tmp, arr + j * stride, size);
            memcpy(arr + j * stride, arr + i * stride, size);
            memcpy(arr + i * stride, tmp, size);
        }
    }
}
