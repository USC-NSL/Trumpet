#ifndef DDOSTABLE2_H
#define DDOSTABLE2_H 1

#include <stdint.h>
#include "stdbool.h"

struct flatreport;
struct flatreport_pkt;

struct ddostable2{
	struct flatreport * fr;
	uint32_t threshold_1;
	uint32_t pktnum;
	uint32_t countarraymask;
	uint16_t step;
	uint64_t laststeptime;
	uint64_t stepperiod;
	uint16_t bufferstart; //put this at the end
	////////////// DON'T PUT IT!
};

//must set fr in flatreport
struct ddostable2 * ddostable2_init(uint16_t threshold, uint32_t countarraysize);
void ddostable2_finish(struct ddostable2 * dt);
bool ddostable2_add(struct ddostable2 * dt, struct flatreport_pkt * pkt);
void ddostable2_incpktnum(struct ddostable2 * dt, int inc);

#endif /* ddostable2.h */

