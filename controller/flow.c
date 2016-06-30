#include "flow.h"
#include "lookup3.h"
#include <arpa/inet.h>
#include <string.h>

inline bool flow_equal(struct flow * f1, struct flow * f2){ 
	return f1->srcip == f2->srcip && f1->dstip == f2->dstip && f1->ports == f2->ports;
}

inline bool flow_equal3(void * d1, void * d2){ 
	struct flow * f1 = (struct flow *) d1;
	struct flow * f2 = (struct flow *) d2;
	return f1->srcip == f2->srcip && f1->dstip == f2->dstip && f1->ports == f2->ports;
}

inline uint32_t flow_hash (struct flow * f){
//	return ntohl(f->dstip);
//	return ntohl(f->srcip^f->dstip)^f->ports;
//	return ntohl(f->srcip)+ntohl(f->dstip)+f->ports;
	return jhash_3words(f->srcip, f->dstip, f->ports, 0x13572468);
}

inline void flow_fill(struct flow * dst, struct flow * src){
	dst->srcip = src->srcip;
	dst->dstip = src->dstip;
	dst->ports = src->ports;
}

inline bool flow_equal2(struct flow * f1, uint32_t srcip, uint32_t dstip, uint32_t ports){
	return f1->srcip == srcip && f1->dstip == dstip && f1->ports == ports;
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
	dst->srcip = src->srcip & maskflow->srcip;
	dst->dstip = src->dstip & maskflow->dstip;
	dst->ports = src->ports & maskflow->ports;
}