#ifndef FLOW_H
#define FLOW_H 1

#include <stdint.h>
#include "stdbool.h"

#define FLOW_NOFG 0x00000000

struct flow {
    uint32_t srcip;
    uint32_t dstip;
    uint32_t ports;
    uint32_t protocol; //32 bits instead of 8 to avoid not setting the three addtional bytes that are wasted anyway. Now, no need to reset the remaining bytes but can be ignored in 64bits operations
}__attribute__((aligned(8)));

void flow_fill(struct flow * dst, struct flow * src);
bool flow_equal(struct flow * data1, struct flow * data2);
bool flow_equal3(void* data1, void * data2);
bool flow_equal2(struct flow * data1, uint32_t srcip, uint32_t dstip, uint32_t ports, uint32_t protocol);
uint32_t flow_hash (struct flow * f);
void flow_print(struct flow * f);
void flow_inlineprint(struct flow * f);
void flow_inlineprint2(struct flow * f, char * buf);
void flow_mask(struct flow * dst, struct flow * src, struct flow * maskflow);

void flow_parseflowgranularity(uint32_t flowgranularity, uint8_t* srcip_len, uint8_t* dstip_len, uint8_t* srcport_len, uint8_t* dstport_len, uint8_t* protocol_len);
uint32_t flow_makeflowgranularity(uint8_t srcip_len, uint8_t dstip_len, uint8_t srcport_len, uint8_t dstport_len, uint8_t protocol_len);
void flow_makemask(struct flow * mask, uint8_t srcip_len, uint8_t dstip_len, uint8_t srcport_len, uint8_t dstport_len, uint8_t protocol_len);

#endif /* flow.h */
