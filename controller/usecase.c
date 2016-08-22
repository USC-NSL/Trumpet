#include "usecase.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <regex.h>
#include "util.h"
#include "serverdata.h"

void lossaction(struct usecase_congestion * u2, uint32_t removedelay);
void lossaction2(struct eventhandler * eh, struct dc_param * removedelay1);
void usecase_netwide_finish(struct usecase * u);
void usecase_congestion_finish(struct usecase * u);
void usecase_congestion_delevent2(struct eventhandler *eh, struct dc_param * param);
void usecase_netwide_addevent(struct eventhandler * eh, struct dc_param * eventsnum1);

static void usecase_finish(struct usecase * u){
	FREE(u->u2);
}


static void usecase_start(struct usecase * u __attribute__((unused))){
}

static void usecase_init(struct usecase * u){
	u->finish = usecase_finish;
	u->start = usecase_start;	
}




/*------------------------------------- network-wide usecase ---------------*/
/*
* Adds eventsnum number of events to all servers.
* This is used to implement the network-wide usecase
*/
void usecase_netwide_addevent(struct eventhandler * eh, struct dc_param * eventsnum1){
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
                struct event * e = eventhandler_getevent(eh);

                e->mask.srcip = 0x00000000;
                e->mask.dstip = mask;
                e->mask.ports = 0xffffffff;
		e->mask.protocol = 0x00000000;
                e->f.srcip = ntohl(e->mask.srcip &((((((10<<8)+0)<<8)+5)<<8)+4));
                e->f.dstip = ntohl(e->mask.dstip & (((((((10<<8)+0)<<8)+4+0)<<8)+0) + (eh->events_num << (generatoripbits-log2_32(eventsnum)))));
                e->f.ports = (ntohs((e->mask.ports>>16) & 58513)<<16) | ntohs((e->mask.ports & 0xffff) & 2500);
		e->f.protocol = 0;
                e->timeinterval = 10; 
		e->mask.srcip = ntohl(e->mask.srcip);
                e->mask.dstip = ntohl(e->mask.dstip);
                e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
                e->threshold = 8; //small threshold to always report
                e->type = 0; //trigger types are installed beforehand at the recevier. type 0 is packet count
		e->flowgranularity = FLOW_NOFG;
		flow_fill(&e->fgmask, &e->mask);

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
                dc->func = usecase_netwide_addevent;
                dc->param.type = dcpt_num;
                dc->param.num = u2->eventsnum;
                eventhandler_adddc(u->eh, dc);
	}
}

static void usecase_netwide_ateventsatisfaction(struct usecase * u __attribute__((unused)), struct event * e __attribute__((unused)), struct eventhistory * es __attribute__((unused))){
//	struct usecase_netwide * u2 = (struct usecase_netwide *) u->u2;
}


struct usecase * usecase_netwide_init(uint16_t targetservers, uint16_t eventsnum){
	struct usecase_netwide * u2 = MALLOC(sizeof(struct usecase_netwide));
	usecase_init(&u2->u);
	u2->u.u2 = u2;
	u2->u.atserverjoin = usecase_netwide_atserverjoin;
	u2->u.ateventsatisfaction = usecase_netwide_ateventsatisfaction;
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
static void addlossevent(struct eventhandler * eh, __attribute__((unused))struct dc_param * param){
	struct serverdata * servers[MAX_SERVERS]; 
	int i, j;
	uint32_t index;

	struct trigger * t; 
	for (i = 0; i < 2; i++){
		LOG("%"PRIu64": addevent event %d ctime %d\n", rdtscl(), eh->events_num, eventhandler_gettime(eh));
		struct event * e = eventhandler_getevent(eh);
		if (i == 0){
			e->mask.srcip = 0x00000000;
			e->mask.dstip = 0xffffffff;
			e->mask.ports = 0x0000ffff;
			e->mask.protocol = 0xffffffff;
			e->f.srcip = ntohl(e->mask.srcip &((((((192<<8)+168)<<8)+1)<<8)+1));
			e->f.dstip = ntohl(e->mask.dstip & ((((((192<<8)+168)<<8)+1)<<8)+3));
			e->f.ports = (ntohs((e->mask.ports>>16) & 58513)<<16) | ntohs((e->mask.ports & 0xffff) & 2500);
		}else{
			e->mask.srcip = 0xffffffff;
			e->mask.dstip = 0x00000000;
			e->mask.ports = 0xffff0000; //both receive and send
			e->mask.protocol = 0xffffffff;
			e->f.srcip = ntohl(e->mask.srcip & ((((((192<<8)+168)<<8)+1)<<8)+3));
			e->f.dstip = ntohl(e->mask.dstip &((((((192<<8)+168)<<8)+1)<<8)+1));
			e->f.ports = (ntohs((e->mask.ports>>16) & 2500)<<16) | ntohs((e->mask.ports & 0xffff) & 58513);
		}

		e->flowgranularity = FLOW_NOFG;
		e->f.protocol = IPPROTO_TCP; //TCP

		e->mask.srcip = ntohl(e->mask.srcip);
		e->mask.dstip = ntohl(e->mask.dstip);
		e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
		e->threshold = 100; //TODO
		e->timeinterval = 10;
		e->type = 3; //types are added at the receivers before
		flow_fill(&e->fgmask, &e->mask);
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
       struct event * e = eventhandler_getevent(eh);
       e->mask.srcip = 0x00000000;
       e->mask.dstip = 0xffffffff;
       e->mask.ports = 0x0000ffff;
       e->mask.protocol = 0xffffffff;
       e->f.srcip = ntohl(e->mask.srcip & ((((((192<<8)+168)<<8)+1)<<8)+0));
       e->f.dstip = ntohl(e->mask.dstip &((((((192<<8)+168)<<8)+1)<<8)+3));
       e->f.ports = (ntohs((e->mask.ports>>16) & 0)<<16) | ntohs((e->mask.ports & 0xffff) & 2501);
       e->f.protocol = IPPROTO_UDP; //UDP

       e->mask.srcip = ntohl(e->mask.srcip);
       e->mask.dstip = ntohl(e->mask.dstip);
       e->mask.ports = (ntohs(e->mask.ports>>16)<<16)|ntohs(e->mask.ports & 0xffff);
       e->threshold = 10000;
       e->timeinterval = 10;
       e->type = 1;
       e->flowgranularity = FLOW_NOFG;
       flow_fill(&e->fgmask, &e->mask);

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
		dc->func = addlossevent;
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

struct usecase * usecase_congestion_init(void){
	struct usecase_congestion * u2 = MALLOC(sizeof(struct usecase_congestion));
	usecase_init(&u2->u);
	u2->u.u2 = u2;
	u2->u.atserverjoin = usecase_congestion_atserverjoin;
	u2->u.ateventsatisfaction = usecase_congestion_ateventsatisfaction;
	u2->proactive_udpeventon = false;
	u2->reactive_udpeventon = false;
	u2->serversnum = 0;
	return &u2->u;
}

/*------------------------------- File usecase --------------------------*/
static void usecase_file_ateventsatisfaction(struct usecase * u __attribute__((unused)), struct event * e __attribute__((unused)), struct eventhistory * es __attribute__((unused))){
	//let's not do anything for now
}


static void usecase_file_installevents(struct eventhandler * eh, struct dc_param * param){
	struct usecase_file * u2 = (struct usecase_file *) param->pointer;
	uint32_t i, j;
	struct event * e;
	uint16_t servers_num;
	struct serverdata * servers[MAX_SERVERS];
	struct trigger * t;
	uint32_t index;
	for (i = 0; i < u2->events_num; i++){
        	LOG("%"PRIu64": addevent event %d ctime %d\n", rdtscl(), i, eventhandler_gettime(eh));
		e = u2->events[i];
		servers_num = eventhandler_getserversforevent(eh, e, servers);
		for (j = 0; j < servers_num; j++){
			index = servers[j]->id;
			t = &e->triggers[index];
	       	 	t->server = servers[j];
	       	 	t->threshold =  e->threshold / servers_num;
	       		serverdata_addtrigger(t->server, e, t);
		}
	}
}


static void usecase_file_atserverjoin(struct usecase * u, struct serverdata * server __attribute__((unused))){
	//install the types if necessary
	struct usecase_file * u2 = (struct usecase_file *) u->u2;
	u2->serversnum++;
	//find servers & install
	if (u2->serversnum != u2->targetservers){
		return;
	}
	uint64_t timeus = eventhandler_gettime_(u->eh)/1000 + 1000000;
        struct delayedcommand * dc = delayedcommand_init(timeus);
        dc->func = usecase_file_installevents;
       	dc->param.type = dcpt_pointer;
        dc->param.pointer = u2;
        eventhandler_adddc(u->eh, dc);
}

#define FIELD_MAXLEN 1023

static int parseField(char * cursor, regex_t * r, char ** output, size_t maxgroups){
	uint32_t g = 0;
	regmatch_t grouparray[maxgroups + 1]; //+1 becasue regexec first return is whole
	if (regexec(r, cursor, maxgroups + 1, grouparray, 0)){
		printf("No regex match for %s\n", cursor);
		return 0;
	}
	for (g = 0; g < maxgroups + 1; g++){
          if (grouparray[g].rm_so == -1)
            break;  // No more groups

	  if (g == 0){continue;}
	  char * cursorcopy = output[g - 1];
          strncpy(cursorcopy, cursor + grouparray[g].rm_so, grouparray[g].rm_eo - grouparray[g].rm_so);
          cursorcopy[grouparray[g].rm_eo - grouparray[g].rm_so] = 0;
         /* printf("Group %u: [%2u-%2u]: %s\n",
                  g, grouparray[g].rm_so, grouparray[g].rm_eo,
                 cursorcopy);*/
        }
	return g;
}

static struct event * usecase_file_readline(struct usecase_file * u2, char * line,
	regex_t * filter_regex, regex_t * predicate_regex, regex_t * fg_regex, regex_t * ti_regex, char ** filter_a, char ** predicate_a, char ** fg_a, char ** ti_a, size_t maxgroups){
	char * filter_s = strsep(&line, ";");
	if (filter_s == NULL) return NULL;
	char * predicate_s = strsep(&line, ";");
	if (predicate_s == NULL) return NULL;
	char * fg_s = strsep(&line, ";");
	if (fg_s == NULL) return NULL;
	char * ti_s = strsep(&line, ";");
	if (ti_s == NULL) return NULL;

	int filter_num, predicate_num, fg_num, ti_num;

	filter_num = parseField(filter_s, filter_regex, filter_a, maxgroups);
	predicate_num = parseField(predicate_s, predicate_regex, predicate_a, maxgroups);
	fg_num = parseField(fg_s, fg_regex, fg_a, maxgroups);
	ti_num = parseField(ti_s, ti_regex, ti_a, maxgroups);
		
	if (filter_num < 10 || predicate_num < 2 || fg_num < 5 || ti_num < 1){
		printf("Error in parsing. Found %d groups in filter, %d in predicate, %d in flow_granularity and %d in time_interval\n", filter_num, predicate_num, fg_num, ti_num);
		return NULL;
	}

	//Validate semantics of data

	uint32_t srcip, dstip;
	uint8_t protocol; 
	uint16_t srcport, dstport;
	uint16_t srcip_len, dstip_len, protocol_len, srcport_len, dstport_len;
	uint16_t fg_srcip_len, fg_dstip_len, fg_protocol_len, fg_srcport_len, fg_dstport_len;

	if (inet_pton(AF_INET, filter_a[0], &srcip)<=0){
		printf("invalid IP %s\n", filter_a[0]);
		return NULL;
	}
	srcip = ntohl(srcip);

	if (inet_pton(AF_INET, filter_a[2], &dstip)<=0){
		printf("invalid IP %s\n", filter_a[2]);
		return NULL;
	}
	dstip = ntohl(dstip);
	protocol = atoi(filter_a[4]);
	srcport = atoi(filter_a[6]);
	dstport = atoi(filter_a[8]);

	srcip_len = atoi(filter_a[1]);
	srcip_len = srcip_len > 32 ? 32 : srcip_len;
	dstip_len = atoi(filter_a[3]);
	dstip_len = dstip_len > 32 ? 32 : dstip_len;
	protocol_len = atoi(filter_a[5]);
	protocol_len = protocol_len > 8 ? 8 : protocol_len;
	srcport_len = atoi(filter_a[7]);
	srcport_len = srcport_len > 16 ? 16 : srcport_len;
	dstport_len = atoi(filter_a[9]);
	dstport_len = dstport_len > 16 ? 16 : dstport_len;

	fg_srcip_len = atoi(fg_a[1]);
	fg_srcip_len = fg_srcip_len > 32 ? 32 : fg_srcip_len;
	fg_dstip_len = atoi(fg_a[3]);
	fg_dstip_len = fg_dstip_len > 32 ? 32 : fg_dstip_len;
	fg_protocol_len = atoi(fg_a[5]);
	fg_protocol_len = fg_protocol_len > 8 ? 8 : fg_protocol_len;
	fg_srcport_len = atoi(fg_a[7]);
	fg_srcport_len = fg_srcport_len > 16 ? 16 : fg_srcport_len;
	fg_dstport_len = atoi(fg_a[9]);
	fg_dstport_len = fg_dstport_len > 16 ? 16 : fg_dstport_len;

	//create the event
	struct eventhandler * eh = u2->u.eh;
        struct event * e = eventhandler_getevent(eh);
       
	flow_makemask(&e->mask, srcip_len, dstip_len, srcport_len, dstport_len, protocol_len); 
        e->f.srcip = ntohl(srcip);
        e->f.dstip = ntohl(dstip);
        e->f.ports = (ntohs(srcport)<<16) | ntohs(dstport);
	e->f.protocol = protocol;
	flow_mask(&e->f, &e->f, &e->mask);
                
        e->threshold = atoi(predicate_a[1]);
	e->timeinterval = atoi(ti_a[0]);
	if (strcmp(predicate_a[0],"volume")==0){
		e->type = 1;
	}else{
		e->type = 0;
	}
	e->flowgranularity = flow_makeflowgranularity(fg_srcip_len, fg_dstip_len, fg_srcport_len, fg_dstport_len, fg_protocol_len);
	if (e->flowgranularity == FLOW_NOFG){
		flow_fill(&e->fgmask, &e->mask);	
	}else{
		flow_makemask(&e->fgmask, fg_srcip_len, fg_dstip_len, fg_srcport_len, fg_dstport_len, fg_protocol_len);
		e->fgmask.srcip |= e->mask.srcip;
		e->fgmask.dstip |= e->mask.dstip;
		e->fgmask.ports |= e->mask.ports;
		e->fgmask.protocol |= e->mask.protocol;
	}

 	return e;
}

static void compileregex(const char * s, regex_t * r){
	int reti = regcomp(r, s, REG_EXTENDED);
	if (reti) {
    		fprintf(stderr, "Could not compile regex %s\n", s);
		abort();
	}
}

static int usecase_file_readfile(struct usecase_file * u2, const char * f){
	const char * filter_regexstring = "([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)/([0-9]+),([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)/([0-9]+),([0-9]+)/([0-9]+),([0-9]+)/([0-9]+),([0-9]+)/([0-9]+)";
	const char * predicate_regexstring = "sum,(volume|packet),([0-9]+)";
	const char * fg_regexstring = "([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+)";
	//const char * ti_regexstring = "([0-9]+)";
	const char * ti_regexstring = "([1-9]+[0-9]*0)"; //multiple of 10 and >0
	uint16_t i;
	regex_t filter_regex;
	regex_t predicate_regex;
	regex_t fg_regex;
	regex_t ti_regex;
	u2->events_num = 0;
	char * line = NULL;
	size_t len = 0;
	ssize_t read;
	u2->events = NULL;

/*--------------------------------------------------------*/
	if (f == NULL){
		return 1;
	}
	FILE * file = fopen(f, "r");
	if (file == NULL){
		return 1;
	}
	
	while ((read = getline(&line, &len, file)) != -1) {
		u2->events_num++;
	}
	u2->events = MALLOC(u2->events_num * sizeof(struct event *));
	memset(u2->events, 0, u2->events_num * sizeof(struct event *));
	fseek(file, 0L, SEEK_SET);
/*-------------------------------------------------------*/

	/* Compile regular expression */
	compileregex(filter_regexstring, &filter_regex);
	compileregex(predicate_regexstring, &predicate_regex);
	compileregex(fg_regexstring, &fg_regex);
	compileregex(ti_regexstring, &ti_regex);

	size_t maxgroups = 10;
	const uint32_t maxlen = FIELD_MAXLEN;
	char ** filter_a = MALLOC(maxgroups * sizeof(char *));
	char ** predicate_a = MALLOC(maxgroups * sizeof(char *));
	char ** fg_a = MALLOC(maxgroups * sizeof(char *));
	char ** ti_a = MALLOC(maxgroups * sizeof(char *));
	for (i = 0; i < maxgroups; i++){
		filter_a[i] = MALLOC(maxlen); 
		predicate_a[i] = MALLOC(maxlen); 
		fg_a[i] = MALLOC(maxlen); 
		ti_a[i] = MALLOC(maxlen); 
	}


	int lineno = 0;
	while ((read = getline(&line, &len, file)) != -1) {
     		//printf("Retrieved line of length %zu :\n", read);
		if (len >= maxlen){
			line[maxlen-1] = 0;
		}
        	//printf("%s", line);
		struct event * e = usecase_file_readline(u2, line, &filter_regex, &predicate_regex, &fg_regex, &ti_regex, filter_a, predicate_a, fg_a, ti_a, maxgroups);
		if (e == NULL){
			printf("error in reading line %d\n", lineno);
			break;
		}
		//event_print(e, NULL);
		//printf("%d, %d\n", e->threshold, e->type);
		//eventhandler_delevent(u2->u.eh, e);
		u2->events[lineno] = e;

		lineno++;
    	}	

	fclose(file);
	if (line != NULL){
		free(line);
	}

	for (i = 0; i < maxgroups; i++){
		FREE(filter_a[i]);
		FREE(predicate_a[i]);
		FREE(fg_a[i]);
		FREE(ti_a[i]);
	}
	FREE(filter_a);
	FREE(predicate_a);
	FREE(fg_a);
	FREE(ti_a);

	printf("Parsed %d lines.\n", lineno);

	return lineno;
}

static void usecase_file_start(struct usecase * u){
	struct usecase_file * u2 = (struct usecase_file *) u->u2;
	usecase_file_readfile(u2, u2->f);
}

struct usecase * usecase_file_init(const char * f, uint16_t targetservers){
	struct usecase_file * u2 = MALLOC(sizeof(struct usecase_file));
	usecase_init(&u2->u);
	u2->u.u2 = u2;
	u2->u.atserverjoin = usecase_file_atserverjoin;
	u2->u.ateventsatisfaction = usecase_file_ateventsatisfaction;
	u2->u.start = usecase_file_start;
	strncpy(u2->f, f, 127);
	u2->f[127] = 0;
	u2->targetservers = targetservers;
	u2->serversnum = 0;

	return &u2->u;
}
