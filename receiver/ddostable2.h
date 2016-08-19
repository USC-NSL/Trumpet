#ifndef DDOSTABLE2_H
#define DDOSTABLE2_H 1

#include <stdint.h>
#include "stdbool.h"

struct flatreport;
struct flatreport_pkt;

/*
* A counterarray table (hashtable of only counter values that collisions just accumulte). 
* The counterss will reset every stepperiod and if 128k packets (~10m packets in 10G) are processed. The limit on the number of packets is to throttle checking time.
* Keeps track of size of flows as multiple of 64
*/
struct ddostable2{
	struct flatreport * fr;
	uint32_t threshold_1;
	uint32_t pktnum; //number of processed packets
	uint32_t countarraymask; //bitwise mask due to the size of countarray
	uint16_t step; // the current epoch
	uint64_t laststeptime;
	uint64_t stepperiod; //epoch length
	uint16_t bufferstart; //put this at the end
	////////////// DON'T PUT IT!
};

/*
* 0 < Threshold < 127*64
* counterarraysize must be power of 2
* Set fr after initialization
*/
struct ddostable2 * ddostable2_init(uint16_t threshold, uint32_t countarraysize);
void ddostable2_finish(struct ddostable2 * dt);

/*
* Returns true if the flow (with pkt->hash) have sent more than the threshold
*/
bool ddostable2_add(struct ddostable2 * dt, struct flatreport_pkt * pkt);
void ddostable2_incpktnum(struct ddostable2 * dt, int inc);

#endif /* ddostable2.h */

