#include "flow.h"
#include "lookup3.h"
#include <arpa/inet.h>
#include <string.h>

inline bool flow_equal(struct flow * f1, struct flow * f2){ 
	return  *(((uint64_t *)f1)) == *(((uint64_t *)f2)) && *(((uint64_t *)f1) +1) == *(((uint64_t *)f2) +1);
}

inline bool flow_equal3(void * d1, void * d2){ 
	return  *(((uint64_t *)d1)) == *(((uint64_t *)d2)) && *(((uint64_t *)d1) +1) == *(((uint64_t *)d2) +1);
/*	struct flow * f1 = (struct flow *) d1;
	struct flow * f2 = (struct flow *) d2;
	return f1->srcip == f2->srcip && f1->dstip == f2->dstip && f1->ports == f2->ports && f1->protocol == f2->protocol;*/
}

inline uint32_t flow_hash (struct flow * f){
//	return ntohl(f->dstip);
//	return ntohl(f->srcip^f->dstip)^f->ports;
//	return ntohl(f->srcip)+ntohl(f->dstip)+f->ports;
	return jhash_3words(f->srcip, f->dstip, f->ports, 0x13572468);
}

inline void flow_fill(struct flow * dst, struct flow * src){
	*((uint64_t *)dst) = *((uint64_t *)src);
	*(((uint64_t *)dst) + 1) = *(((uint64_t *)src) +1);
/*	dst->srcip = src->srcip;
	dst->dstip = src->dstip;
	dst->ports = src->ports;
	dst->protocol = src->protocol;*/
}

inline bool flow_equal2(struct flow * f1, uint32_t srcip, uint32_t dstip, uint32_t ports, uint32_t protocol){
	return f1->srcip == srcip && f1->dstip == dstip && f1->ports == ports && f1->protocol == protocol;
}

void flow_print(struct flow * f){
  flow_inlineprint(f);
  printf("\n");
}

void flow_inlineprint2(struct flow * f, char * buf){
  struct in_addr srcip;
  struct in_addr dstip;
  srcip.s_addr = f->srcip;
  dstip.s_addr = f->dstip;

  sprintf(buf,"%s:%u, ", inet_ntoa(srcip), ntohs(f->ports>>16));
  sprintf(buf+strlen(buf)-1,"%s:%u", inet_ntoa(dstip), ntohs(f->ports & 0xFFFF));
}

void flow_inlineprint(struct flow * f){
  struct in_addr srcip;
  struct in_addr dstip;
  srcip.s_addr = f->srcip;
  dstip.s_addr = f->dstip;

  printf("%s:%u, ", inet_ntoa(srcip), ntohs(f->ports>>16));
  printf("%s:%u", inet_ntoa(dstip), ntohs(f->ports & 0xFFFF));
}


inline void flow_mask(struct flow * dst, struct flow * src, struct flow * maskflow){
	*((uint64_t *)dst) = *((uint64_t *)src) & *(((uint64_t *)maskflow));
	*(((uint64_t *)dst) + 1) = *(((uint64_t *)src) +1) & *(((uint64_t *)maskflow) +1);
/*	dst->srcip = src->srcip & maskflow->srcip;
	dst->dstip = src->dstip & maskflow->dstip;
	dst->ports = src->ports & maskflow->ports;
	dst->protocol = src->protocol && maskflow->protocol; */
}

void flow_parseflowgranularity(uint32_t flowgranularity, uint8_t* srcip_len, uint8_t* dstip_len, uint8_t* srcport_len, uint8_t* dstport_len, uint8_t* protocol_len){
        *protocol_len = flowgranularity & 0x3f;
        if (*protocol_len > 8){
                *protocol_len = 8;
        }
        flowgranularity >>= 6;
        *dstport_len = flowgranularity & 0x3f;
        if (*dstport_len > 16){
                *dstport_len = 16;
        }
        flowgranularity >>= 6;
        *srcport_len = flowgranularity & 0x3f;
        if (*srcport_len > 16){
                *srcport_len = 16;
        }
        flowgranularity >>= 6;
        *dstip_len = flowgranularity & 0x3f;
        if (*dstip_len > 32){
                *dstip_len = 32;
        }
        flowgranularity >>= 6;
        *srcip_len = flowgranularity & 0x3f;
        if (*srcip_len > 32){
                *srcip_len = 32;
        }
}


uint32_t flow_makeflowgranularity(uint8_t srcip_len, uint8_t dstip_len, uint8_t srcport_len, uint8_t dstport_len, uint8_t protocol_len){
	return (((((((srcip_len<<6) | dstip_len)<<6) | srcport_len)<<6) | dstport_len)<<6) | protocol_len;
}

void flow_makemask(struct flow * mask, uint8_t srcip_len, uint8_t dstip_len, uint8_t srcport_len, uint8_t dstport_len, uint8_t protocol_len){
	mask->srcip = 0x00ffffffffUL << (32-srcip_len);
        mask->dstip = 0x00ffffffffUL << (32-dstip_len);
        mask->ports = ((0x00000000ffffUL << (32-srcport_len))&0xffff0000) | ((0x0000ffff << (16-dstport_len)) & 0x0000ffff);
        mask->protocol = (0x000000ff <<(8-protocol_len)) & 0x000000ff;

	mask->srcip = ntohl(mask->srcip);
        mask->dstip = ntohl(mask->dstip);
        mask->ports = (ntohs(mask->ports>>16)<<16)|ntohs(mask->ports & 0xffff);
}
