#ifndef FLOW_H
#define FLOW_H 1

#include <stdint.h>
#include "stdbool.h"

struct flow{
    uint32_t srcip;
    uint32_t dstip;
    uint32_t ports;
};

void flow_fill(struct flow * dst, struct flow * src);
bool flow_equal(struct flow * data1, struct flow * data2);
bool flow_equal3(void* data1, void * data2);
bool flow_equal2(struct flow * data1, uint32_t srcip, uint32_t dstip, uint32_t ports);
uint32_t flow_hash (struct flow * f);
void flow_print(struct flow * f);
void flow_inlineprint(struct flow * f);
void flow_inlineprint2(struct flow * f, char * buf);
void flow_mask(struct flow * dst, struct flow * src, struct flow * maskflow);


#endif /* flow.h */
