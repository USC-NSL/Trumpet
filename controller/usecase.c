#include "usecase.h"
#include <stdlib.h>
#include <stdio.h>
#include "util.h"
#include "serverdata.h"

void lossaction(struct usecase_congestion * u2, uint32_t removedelay);
void lossaction2(struct eventhandler * eh, struct dc_param * removedelay1);
void usecase_netwide_finish(struct usecase * u);
void usecase_congestion_finish(struct usecase * u);
void usecase_congestion_delevent2(struct eventhandler *eh, struct dc_param * param);

/*------------------------------------- network-wide usecase ---------------*/
/*
* Adds eventsnum number of events to all servers.
* This is used to implement the network-wide usecase
*/
static void eventhandler_addevent(struct eventhandler * eh, struct dc_param * eventsnum1){
/*      struct timespec tv;
        clock_gettime(CLOCK_MONOTONIC, &tv);*/
        uint16_t eventsnum = eventsnum1->num;
        if (gbp(eventsnum) != eventsnum){
                fprintf(stderr, "Eventhandler: Eventsnum must be power of 2 but it is %d\n", eventsnum);
                return;
        }
        struct serverdata * servers[MAX_SERVERS];
        int i, j;
        uint32_t index;
        struct trigger * t;
        const uint32_t generatoripbits = 16;
        const uint32_t mask = 0xffffffff << (generatoripbits-log2_32(eventsnum));
        for (i = 0; i < eventsnum; i++){
                LOG("%"PRIu64": addevent event %d ctime %d\n", rdtscl(), eh->events_num, eventhandler_gettime(eh));
                struct event * e = event_init(eh);

                e->mask.srcip = 0x00000000;
                e->mask.dstip = mask;
                e->mask.ports = 0xffffffff;
                e->f.srcip = ntohl(e->mask.srcip &((((((10<<8)+0)<<8)+5)<<8)+4));
                e->f.dstip = ntohl(e->mask.dstip & (((((((10<<8)+0)<<8)+4+0)<<8)+0) + (eh->events_num << (generatoripbits-log2_32(eventsnum)))));
                e->f.ports = (ntohs((e->mask.ports>>16) & 58513)<<16) | ntohs((e->mask.ports & 0xffff) & 2500);
                
		e->mask.srcip = ntohl(e->mask.srcip);
                e->mask.dstip = ntohl(e->mask.dstip);
                e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
                e->threshold = 8; //small threshold to always report
                e->type = 0; //trigger types are installed beforehand at the recevier. type 0 is packet count

                //find servers & install
                uint16_t servers_num = eventhandler_getserversforevent(eh, e, servers);
                for (j = 0; j < servers_num; j++){
                        index = servers[j]->id;
                        t = &e->triggers[index];
                        t->server = servers[j];
                        t->threshold =  e->threshold / servers_num;
                        serverdata_addtrigger(t->server, e, t);
                }
        }
}

static void usecase_netwide_atserverjoin(struct usecase * u, struct serverdata * server __attribute__((unused))){
	struct usecase_netwide * u2 = (struct usecase_netwide *) u->u2;
	u2->serversnum ++;
	if (u2->serversnum == u2->targetservers){
		uint64_t timeus = eventhandler_gettime_(u->eh)/1000 + 1000000;
        	struct delayedcommand * dc = delayedcommand_init(timeus);
                dc->func = eventhandler_addevent;
                dc->param.type = dcpt_num;
                dc->param.num = u2->eventsnum;
                eventhandler_adddc(u->eh, dc);
	}
}

static void usecase_netwide_ateventsatisfaction(struct usecase * u __attribute__((unused)), struct event * e __attribute__((unused)), struct eventhistory * es __attribute__((unused))){
//	struct usecase_netwide * u2 = (struct usecase_netwide *) u->u2;
}

void usecase_netwide_finish(struct usecase * u){
	struct usecase_netwide * u2 = (struct usecase_netwide *) u->u2;
	FREE(u2);
}

struct usecase * usecase_netwide_init(uint16_t targetservers, uint16_t eventsnum){
	struct usecase_netwide * u2 = MALLOC(sizeof(struct usecase_netwide));
	u2->u.u2 = u2;
	u2->u.atserverjoin = usecase_netwide_atserverjoin;
	u2->u.ateventsatisfaction = usecase_netwide_ateventsatisfaction;
	u2->u.finish = usecase_netwide_finish;
	u2->targetservers = targetservers;
	u2->eventsnum = eventsnum;
	u2->serversnum = 0;
	return &u2->u;
}

/*---------------------------------------- congestion usecase -----------------*/

/*
* Adds the congestion detection event for two servers for the congestion usecase.
* It assumes the two TCP sender servers are the first two
*/
static void eventhandler_addlossevent(struct eventhandler * eh, __attribute__((unused))struct dc_param * param){
	struct serverdata * servers[MAX_SERVERS]; 
	int i, j;
	uint32_t index;

	struct trigger * t; 
	for (i = 0; i < 2; i++){
		LOG("%"PRIu64": addevent event %d ctime %d\n", rdtscl(), eh->events_num, eventhandler_gettime(eh));
		struct event * e = event_init(eh);
		if (i == 0){
			e->mask.srcip = 0x00000000;
			e->mask.dstip = 0xffffffff;
			e->mask.ports = 0x0000ffff;
			e->f.srcip = ntohl(e->mask.srcip &((((((192<<8)+168)<<8)+1)<<8)+1));
			e->f.dstip = ntohl(e->mask.dstip & ((((((192<<8)+168)<<8)+1)<<8)+3));
			e->f.ports = (ntohs((e->mask.ports>>16) & 58513)<<16) | ntohs((e->mask.ports & 0xffff) & 2500);
		}else{
			e->mask.srcip = 0xffffffff;
			e->mask.dstip = 0x00000000;
			e->mask.ports = 0xffff0000; //both receive and send
			e->f.srcip = ntohl(e->mask.srcip & ((((((192<<8)+168)<<8)+1)<<8)+3));
			e->f.dstip = ntohl(e->mask.dstip &((((((192<<8)+168)<<8)+1)<<8)+1));
			e->f.ports = (ntohs((e->mask.ports>>16) & 2500)<<16) | ntohs((e->mask.ports & 0xffff) & 58513);
		}

		e->mask.srcip = ntohl(e->mask.srcip);
		e->mask.dstip = ntohl(e->mask.dstip);
		e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
		e->threshold = 100; //TODO
		e->type = 3; //types are added at the receivers before
		//find servers
		uint16_t servers_num = eventhandler_getserversforevent(eh, e, servers);
		if (servers_num < 2){
			fprintf(stderr, "Eventhandler: this scenario should have 2 servers but has %d\n", servers_num);
			continue;
		}
		//install triggers on the servers with half threshold
		for (j = 0; j < 2; j++){ //TODO
			index = servers[j]->id;
			t = &e->triggers[index];
			t->server = servers[j];
			t->threshold =  e->threshold / servers_num;
			serverdata_addtrigger(t->server, e, t);
		}
	}
}

struct usecase_congestion_delparam{
	struct event * e;
	struct usecase_congestion * u2;
};

struct usecase_congestion_lossactionparam{
	struct usecase_congestion * u2;
	uint32_t removedelay;
};

void lossaction2(struct eventhandler * eh __attribute__((unused)), struct dc_param * param){
	struct usecase_congestion_lossactionparam * p = (struct usecase_congestion_lossactionparam *)param->pointer;
	lossaction(p->u2, p->removedelay);
	FREE(p);
}


void usecase_congestion_delevent2(struct eventhandler * eh, struct dc_param * param){
	struct usecase_congestion_delparam * p = (struct usecase_congestion_delparam *) param->pointer;
        p->u2->reactive_udpeventon = false;
        eventhandler_delevent(eh, p->e);
	FREE(p);
}

/*
* The method runs as a result of seeing congestion for the congestion event.
* It is used to reactively install heavy hitter detection event at the TCP receiver
* This method assumes the TCP receiver server is the third server
* if the removedelay is > 0, it will add a delayed command to remove this event after that delay
*/
void lossaction(struct usecase_congestion * u2, uint32_t removedelay){
       //add udp detection on the third server
       struct eventhandler * eh = u2->u.eh;
       struct serverdata * server = eh->servers[2];
       if (server == NULL){
               fprintf(stderr, "Receiver server has not joint yet\n");
               return; //third server has not joint yet!
       }

       struct trigger * t;
       uint32_t index;

       LOG("%"PRIu64": addevent event %d ctime %d\n", rdtscl(), eh->events_num, eventhandler_gettime(eh));
       struct event * e = event_init(eh);
       e->mask.srcip = 0x00000000;
       e->mask.dstip = 0xffffffff;
       e->mask.ports = 0x0000ffff;
       e->f.srcip = ntohl(e->mask.srcip & ((((((192<<8)+168)<<8)+1)<<8)+0));
       e->f.dstip = ntohl(e->mask.dstip &((((((192<<8)+168)<<8)+1)<<8)+3));
       e->f.ports = (ntohs((e->mask.ports>>16) & 0)<<16) | ntohs((e->mask.ports & 0xffff) & 2501);

       e->mask.srcip = ntohl(e->mask.srcip);
       e->mask.dstip = ntohl(e->mask.dstip);
       e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
       e->threshold = 10000;
       e->type = 1;

       //at third server
       index = server->id;
       t = &e->triggers[index];
       t->server = server;
       t->threshold = e->threshold;
       serverdata_addtrigger(t->server, e, t);

       if (removedelay > 0){
               uint64_t timeus = eventhandler_gettime_(eh)/1000 + removedelay;
               struct delayedcommand * dc;

               dc = delayedcommand_init(timeus);
               dc->func = usecase_congestion_delevent2;
               dc->param.type = dcpt_pointer;
	       struct usecase_congestion_delparam * p = MALLOC(sizeof(struct usecase_congestion_delparam));
               p->e = e;
	       p->u2 = u2;
	       dc->param.pointer = p;
               eventhandler_adddc(eh, dc);
               LOG("add dc to remove %d at %"PRIu64"\n", e->id, timeus/10000);
       }
}

static void usecase_congestion_atserverjoin(struct usecase * u, struct serverdata * server){
	struct usecase_congestion * u2 = (struct usecase_congestion *) u->u2;
	u2->serversnum++;
	if (u2->serversnum == 2){
		uint64_t timeus = eventhandler_gettime_(u->eh)/1000 + 1000000;
        	struct delayedcommand * dc = delayedcommand_init(timeus);
		dc->func = eventhandler_addlossevent;
		dc->param.type = dcpt_num;
                dc->param.num = 0;

                eventhandler_adddc(u->eh, dc);
	}
	if (server->id == 2 && !u2->proactive_udpeventon){
		u2->proactive_udpeventon = true;
        	uint64_t timeus = eventhandler_gettime_(u->eh)/1000 + 1000000;
        	struct delayedcommand * dc = delayedcommand_init(timeus);
        	dc->func = lossaction2;
       		dc->param.type = dcpt_pointer;
		struct usecase_congestion_lossactionparam * p = MALLOC(sizeof(struct usecase_congestion_lossactionparam)); 
		p->u2 = u2;
		p->removedelay = 0;
        	dc->param.pointer = p;
        	eventhandler_adddc(u->eh, dc);
	}
}

static void usecase_congestion_ateventsatisfaction(struct usecase * u, struct event * e, struct eventhistory * es){
	struct usecase_congestion * u2 = (struct usecase_congestion *) u->u2;
	if (!u2->reactive_udpeventon && e->type == 3){
		bool allabovethreshold = true;
		int i;
                for (i = 0; i <  2; i++){
                        if (es->triggersmap[i].value < e->threshold/2){
                                allabovethreshold = false;
                                break;
                        }
                }
                if (allabovethreshold){
                        u2->reactive_udpeventon = true;
                        lossaction(u2, 100000);
                }
	}
}

void usecase_congestion_finish(struct usecase * u){
	struct usecase_congestion * u2 = (struct usecase_congestion *) u->u2;
	FREE(u2);
}

struct usecase * usecase_congestion_init(void){
	struct usecase_congestion * u2 = MALLOC(sizeof(struct usecase_congestion));
	u2->u.u2 = u2;
	u2->u.atserverjoin = usecase_congestion_atserverjoin;
	u2->u.ateventsatisfaction = usecase_congestion_ateventsatisfaction;
	u2->u.finish = usecase_congestion_finish;
	u2->proactive_udpeventon = false;
	u2->reactive_udpeventon = false;
	u2->serversnum = 0;
	return &u2->u;
}


