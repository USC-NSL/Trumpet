#ifndef LOOKUP3_H
#define LOOKUP3_H 1

#include <stdint.h>
#include <stdio.h>

#define hashsize(n) ((uint32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))

uint32_t hashlittle( const void *key, size_t length, uint32_t initval);
uint32_t jhash_3words(uint32_t a, uint32_t b, uint32_t c, uint32_t initval);

#endif /* lookup3.h */