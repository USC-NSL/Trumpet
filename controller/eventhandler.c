#include "eventhandler.h"
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h> //for offsetof
#include <inttypes.h>
#include <string.h>
#include <time.h> 
#include <sys/time.h> 
#include <unistd.h>
#include "serverdata.h"
#include "util.h"

static bool proudpeventon = false;
static bool udpeventon = false;
typedef void (*dc_func)(struct eventhandler * eh, uint32_t param);

enum delayedcommand_type{
        dct_addevent,
        dct_delevent,
        dct_func,
};

struct delayedcommand{
	struct event * e;
	uint64_t timeus;
	void * next;
	dc_func func;
	uint32_t param;
	enum delayedcommand_type type;
};


int event_checkcondition(struct eventhandler * eh, struct event * e, struct eventhistory * es);
int findtrigger(struct eventhandler * eh, uint16_t eventid, struct serverdata * server, struct event ** e, struct trigger **t);
uint32_t event_hash(void * data);
bool event_equal(void * data1, void * data2, void * aux);
bool event_finish(void * data, void * aux);
bool event_equalid(void * data1, void * data2, void * aux);
void eventhistory_finish(struct eventhistory * es);
void resetevents(struct eventhandler * eh);
bool event_print(void * data, void * aux);
uint16_t getservers_forevent(struct eventhandler * eh, struct event * e, struct serverdata ** servers);
bool eventhistory_triggerhistory_init(void * data, void * aux);
bool findeventhistory(struct event * e, uint32_t time, struct eventhistory ** es2);
void getaneventhistory(struct event * e, struct trigger * t2, struct eventhistory **es2, uint32_t time);
uint64_t eventhandler_gettime_(struct eventhandler *eh);
void * delaycommand_thread (void *_);
void lossaction(struct eventhandler * eh, uint32_t removedelay);

void eventhandler_addevent(struct eventhandler * eh, uint16_t eventsnum);
void eventhandler_addlossevent(struct eventhandler * eh);
struct event * event_init(struct eventhandler * eh);
void eventhandler_adddc(struct eventhandler * eh, struct delayedcommand * dc2);

struct eventhandler * eventhandler_init(void){
	struct eventhandler * eh = malloc(sizeof (struct eventhandler));
	eh->events_num = 0;
	memset(eh->servers, 0, sizeof(eh->servers));
	eh->eventsmap = hashmap_init(1024, 1024, sizeof(struct event), offsetof(struct event, elem), NULL);
	pthread_mutex_init(&eh->global_mutex, NULL);
	eh->dc_finish = false;
	eh->dc = NULL;
	sem_init(&eh->dc_sem, 0, 0);

	int i;
	for (i = 0; i < MAX_SERVERS; i++){
		eh->servers[i] = NULL;
	}

	eventhandler_syncepoch(10);
	clock_gettime(CLOCK_MONOTONIC, &eh->inittime);
	pthread_create(&eh->dc_pth, NULL, (void *)delaycommand_thread, (void *)eh);
	return eh;
}

void eventhandler_finish(struct eventhandler * eh){
	eh->dc_finish = true;
	sem_post(&eh->dc_sem);
	pthread_join(eh->dc_pth, NULL);
	sem_destroy(&eh->dc_sem);

	resetevents(eh);

	pthread_mutex_lock(&eh->global_mutex);
        hashmap_finish(eh->eventsmap);
	struct delayedcommand * dc2;
	for (; eh->dc != NULL; eh->dc = dc2){
		dc2 = eh->dc->next;
		free(eh->dc);
	}
	pthread_mutex_unlock(&eh->global_mutex);

	pthread_mutex_destroy(&eh->global_mutex);
	uint16_t i;
	for (i = 0; i < MAX_SERVERS; i++){
		if (eh->servers[i] != NULL){
			serverdata_finish(eh->servers[i]);
		}
	}

	free(eh);	
}

void eventhandler_adddc(struct eventhandler * eh, struct delayedcommand * dc2){
	struct delayedcommand * dc_prev, * dc;
	pthread_mutex_lock(&eh->global_mutex);
	for (dc_prev = NULL, dc = eh->dc; dc != NULL && dc->timeus <= dc2->timeus; dc_prev = dc, dc = dc->next); //find dc
	if (dc_prev == NULL){
		eh->dc = dc2;
	}else{
		dc2->next = dc_prev->next;
		dc_prev->next = dc2;
	}
	pthread_mutex_unlock(&eh->global_mutex);
	sem_post(&eh->dc_sem);
}

void eventhandler_addeventdelay(struct eventhandler * eh, uint32_t eventnum, uint32_t delayus){
//	int i; 
	uint64_t timeus = eventhandler_gettime_(eh)/1000 + delayus;
	struct delayedcommand * dc;
	
//	for (i = 0; i < eventnum; i++){
		dc = MALLOC(sizeof (struct delayedcommand));
		dc->type = dct_addevent;
		dc->e = NULL;
		dc->func = NULL;
		dc->timeus = timeus;
		dc->next = NULL;
		dc->param = eventnum;
		
		eventhandler_adddc(eh, dc);
//	}
}

void eventhandler_syncepoch(int ms){
        struct timeval tv;
        int ret;
        do{
                ret = gettimeofday(&tv, NULL);
                if (ret != 0){
                        fprintf(stderr, "Cannot get current time\n");
                        break;
                }
        }while (tv.tv_usec/ (ms * 1000) != 0);
}


void * delaycommand_thread (void *_){
        struct eventhandler * eh = (struct eventhandler *)_;
	uint64_t time;
	while (!eh->dc_finish){
		struct delayedcommand * dc = eh->dc;
		if (dc == NULL){
//			printf("wait\n");
			sem_wait(&eh->dc_sem);
			continue;
		}
		time = eventhandler_gettime_(eh)/1000;	
//		printf("%"PRIu64" vs %"PRIu64"\n", time, dc->timeus);
		if (time >= dc->timeus){
			//run the command
			switch(dc->type){
				case dct_addevent:
					eventhandler_addlossevent(eh);					
				break;
				case dct_delevent:
					udpeventon = false;
					eventhandler_delevent(eh, dc->e);	
				break;
				case dct_func:
					dc->func(eh, dc->param);
				break;	
				default:
					fprintf(stderr, "Eventhandler: Unrecognized delayedcommand type %d\n", dc->type);
				break;
			}
			
			pthread_mutex_lock(&eh->global_mutex);
			eh->dc = dc->next;
			pthread_mutex_unlock(&eh->global_mutex);
		}else{
			if (dc->timeus - time >= 100){
				usleep(dc->timeus - time-100);
			}else{
				while (eventhandler_gettime_(eh)/1000 < dc->timeus);
			}
		}
	}

        return NULL;
}


void resetevents(struct eventhandler * eh){
	pthread_mutex_lock(&eh->global_mutex);
	hashmap_apply(eh->eventsmap, event_finish, eh);
	hashmap_clear(eh->eventsmap);
	pthread_mutex_unlock(&eh->global_mutex);
}



bool event_equalid(void * data1, void * data2, void * aux __attribute__((unused))){
	uint16_t id = *(uint16_t *)data1;
	struct event * e = (struct event *)data2;
	return id == e->id;
}

void eventhandler_addserver(struct eventhandler *eh, struct serverdata *server){
	if (server->id < MAX_SERVERS && eh->servers[server->id] == NULL){
		eh->servers[server->id] = server;

		if (server->id == 2){
			if (!proudpeventon){
				proudpeventon = true;
				uint64_t timeus = eventhandler_gettime_(eh)/1000 + 1000000;
				struct delayedcommand * dc = MALLOC(sizeof (struct delayedcommand));
				dc->type = dct_func;
				dc->e = NULL;
				dc->func = lossaction;
				dc->timeus = timeus;
				dc->next = NULL;
				dc->param = 0;
				eventhandler_adddc(eh, dc);
			}
		}

	}else{
		fprintf(stderr, "Eventhandler: Cannot add server %d\n", server->id);
	}
}

void eventhandler_removeserver(struct eventhandler *eh, struct serverdata * server){
	uint16_t i;
	for (i = 0; i < MAX_SERVERS; i++){
		if (eh->servers[i]!= NULL && serverdata_equal(eh->servers[i], server)){
			eh->servers[i] = NULL;
			//TODO Just for paper experiments
			resetevents(eh);
			return;
		}
	}
	fprintf(stderr, "Eventhandler: Cannot find server %d to remove\n", server->id);
}

int findtrigger(struct eventhandler * eh, uint16_t eventid, struct serverdata * server, struct event ** e, struct trigger **t){
	struct event * e2 = hashmap_get2(eh->eventsmap, &eventid, eventid, event_equalid, NULL); 
	if (e2 == NULL){
		*t = NULL;
		*e = NULL;
		fprintf(stderr, "Eventhandler: Cannot find event %d\n", eventid);
		hashmap_apply(eh->eventsmap, event_print, NULL);
		return -1;
	}
	*e = e2;

	if (server->id > MAX_SERVERS){
		fprintf(stderr, "Eventhandler: Cannot find event %d at server %d\n", eventid, server->id);
		return -1;
	}
	*t = &e2->triggers[server->id];
	if ((*t) == NULL || (*t)->server == NULL){ 
		fprintf(stderr, "Eventhandler: Cannot find event %d at server %d (%p)\n", eventid, server->id,(*t)->server);
		return -1;
	}
	return 0;
}

uint16_t getservers_forevent(struct eventhandler * eh, struct event * e __attribute__((unused)), struct serverdata ** servers){
	//return all servers for now
	uint16_t i;
	for (i = 0; i < MAX_SERVERS && eh->servers[i] != NULL; i++){
		servers[i] = eh->servers[i];
	}

	//set it NULL in the array if event is not for the server
	return i; 
}

void eventhandler_delevent(struct eventhandler * eh, struct event * e){
	LOG("%"PRIu64": delevent event %d ctime %d\n", rdtscl(), e->id, eventhandler_gettime(eh));
	int j;
	struct trigger * t;
	for (j = 0; j < MAX_SERVERS; j++){ //TODO
		t = &e->triggers[j];
		if (t->server != NULL){
			serverdata_deltrigger(t->server, e, t);
		}
	}
	
	pthread_mutex_lock(&eh->global_mutex);
	event_finish(e, eh);
	hashmap_remove(eh->eventsmap, e);
	pthread_mutex_unlock(&eh->global_mutex);
		
}

void eventhandler_addlossevent(struct eventhandler * eh){
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
		e->type = 3;
				//find servers
		uint16_t servers_num = getservers_forevent(eh, e, servers);
		if (servers_num < 2){
			fprintf(stderr, "Eventhandler: this scenario should have 2 servers but has %d\n", servers_num);
			continue;
		}
		for (j = 0; j < 2; j++){ //TODO
			index = servers[j]->id;
			t = &e->triggers[index];
			t->server = servers[j];
			t->threshold =  e->threshold / servers_num;
			serverdata_addtrigger(t->server, e, t);
		}

	}
}

void lossaction(struct eventhandler * eh, uint32_t removedelay){
	//add udp detection on the third server
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
	
		dc = MALLOC(sizeof (struct delayedcommand));
		dc->type = dct_delevent;
		dc->e = e;
		dc->func = NULL;
		dc->timeus = timeus;
		dc->next = NULL;
		dc->param = 0;
		
		eventhandler_adddc(eh, dc);
		LOG("add dc to remove %d at %"PRIu64"\n", e->id, timeus/10000);
	}	
}

void eventhandler_addevent(struct eventhandler * eh, uint16_t eventsnum){
/*	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC, &tv);*/
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
		e->threshold = 8;
		e->type = 0;

		//find servers
		uint16_t servers_num = getservers_forevent(eh, e, servers);
		for (j = 0; j < servers_num; j++){
			index = servers[j]->id;
			t = &e->triggers[index];
			t->server = servers[j];
			t->threshold =  e->threshold / servers_num;
			serverdata_addtrigger(t->server, e, t);
		}
	}
}


void eventhandler_addtrigger_return(struct eventhandler * eh, uint16_t eventid, bool success, struct serverdata *server, uint32_t time __attribute__((unused))){
	struct trigger * t;
	struct event * e;
	int ret = findtrigger(eh, eventid, server, &e, &t);
	if (ret < 0){
		return;
	}
	if (!success){
		fprintf(stderr, "Couldn't add trigger for event %d at server %d\n", eventid, server->id);
	}else{
	//	LOG("trigger is added successfully!\n");
		//TODO add success sate

		//TEST
//		serverdata_triggerquery(server, e, 100);
	}
}

uint16_t eventhandler_activeservers(struct eventhandler *eh){
	uint16_t i;
	uint16_t sum = 0;
	for (i = 0; i < MAX_SERVERS; i++){
		if (eh->servers[i] != NULL){
			sum++;
		}
	}
	return sum;
}

inline uint64_t eventhandler_gettime_(struct eventhandler *eh){
	const uint32_t onesec = 1000000000;
	struct timespec tv;
	clock_gettime(CLOCK_MONOTONIC, &tv);
	long a_sec = (long)tv.tv_sec - (long)eh->inittime.tv_sec;
	long a_nsec = (long)tv.tv_nsec - (long)eh->inittime.tv_nsec;
	if (a_nsec < 0){
		a_nsec += onesec;
		a_sec --;
	}
	return (uint64_t) a_sec * onesec  + a_nsec;
}

uint32_t eventhandler_gettime(struct eventhandler * eh){
	return eventhandler_gettime_(eh)/10000000;
}

bool findeventhistory(struct event * e, uint32_t time, struct eventhistory ** es2){
	struct eventhistory * es;
	for (es = e->eventhistory_head; es != NULL; es = es->next){
		if (es->valid && es->time == time){
			*es2 = es;
			return true;
		}
	}
	*es2 = NULL;
	return false;
}

void getaneventhistory(struct event * e, struct trigger * t2, struct eventhistory **es2, uint32_t time){
	struct eventhistory * es;
	pthread_rwlock_unlock(&e->lock);
	pthread_rwlock_wrlock(&e->lock);
	if (!findeventhistory(e, time, &es)){ //check again as before getting the lock somebody else could have added that!
		//I already know it is satisfaction
		if (e->eventhistory_head != NULL && !e->eventhistory_head->valid){ //most of the time only one history per event is there
			es = e->eventhistory_head;
//			printf("event %d time %d thread %d use %p\n", e->id, time, (int) pthread_self(), es);
		}else{
			es = malloc(sizeof (struct eventhistory));
//			printf("event %d time %d thread %d malloc %p\n", e->id, time, (int) pthread_self(), es);
			es->next = e->eventhistory_head; //stack it
			e->eventhistory_head = es;
			es->prev = NULL;
			if (es->next != NULL){
				es->next->prev = es;
			}
		}
		es->inittrigger = t2;
		es->time = time;	
		es->valid = true;
		es->e = e;
		uint16_t i;
		for (i = 0; i < MAX_SERVERS; i++){
			struct trigger * t = &e->triggers[i];
			if (t->server != NULL){
				struct triggerhistory * th = &es->triggersmap[i];
				th->value = 0;
				if (!serverdata_equal(t->server, es->inittrigger->server)){
					th->valuereceived = false;
					serverdata_triggerquery(t->server, es->e, serverdata_c2stime(t->server, es->time));
				}
			}
		}
	}
	*es2 = es;
	pthread_rwlock_unlock(&e->lock);
	pthread_rwlock_rdlock(&e->lock);
	//pthread_rwlock_wrlock(&e->lock); // JUST FOR TEST
}

void eventhandler_notify(struct eventhandler * eh, uint16_t eventid, struct serverdata * server, uint32_t time, char * buf, bool satisfaction_or_query, uint16_t code){
	time = serverdata_s2ctime(server, time);
	struct trigger * t;
	struct event * e;
	int ret = findtrigger(eh, eventid, server, &e, &t);
	if (ret < 0){
		return;
	}
	if (code != 0){
		fprintf(stderr, "Server %d could not find history for event %d at time %d (%d)\n", server->id, eventid, time, serverdata_c2stime(server, time));
		return;
	}
	pthread_rwlock_rdlock(&e->lock);
	//pthread_rwlock_wrlock(&e->lock); // JUST FOR TEST
	struct eventhistory * es;
	if (!findeventhistory(e, time, &es)){	
		if (satisfaction_or_query){
			getaneventhistory(e, t, &es, time); //make one
		}else{
			pthread_rwlock_unlock(&e->lock);
			return;
		}
	}
	//if there is an eventhistory at the same time, process it as a query return
	struct triggerhistory * th = &es->triggersmap[server->id];
	th->valuereceived = true;
	th->value = *((uint32_t *)buf);
	int check = event_checkcondition(eh, e, es);
	pthread_rwlock_unlock(&e->lock); //release lock after check
	if (check >= 0){
		//I'm done with this eventhistory
		pthread_rwlock_wrlock(&e->lock);
		//make sure es is not freed!
		//check if it is still in the list!
		if (findeventhistory(e, time, &es)){
			if (e->eventhistory_head == es && es->next == NULL){
				//don't remove it
				es->valid = false;
//				printf("event %d time %d thread %d notuse %p\n", es->e->id, es->time, (int) pthread_self(), es);
			}else{
				if (es->prev != NULL){
					es->prev->next = es->next;
				}else{
					e->eventhistory_head = es->next;
				}
				if (es->next != NULL){
					es->next->prev = es->prev;
				}
				eventhistory_finish(es);
			}
		}
		pthread_rwlock_unlock(&e->lock);
	}
}

//return -1 if cannot decide yet
int event_checkcondition(struct eventhandler * eh __attribute__((unused)), struct event * e, struct eventhistory * es){
	//TODO
	//f"ow just check if all values are received
	uint16_t i;
	for (i = 0; i < MAX_SERVERS; i++){
		if (e->triggers[i].server != NULL && !es->triggersmap[i].valuereceived){
			return -1;	
		}
	}

	//print
	char buf [LOGLINEBUFFER_LEN];
	snprintf(buf, LOGLINEBUFFER_LEN, "%"PRIu64": all_data event %d ctime %d ", rdtscl(), e->id, es->time);
	char buf2[64];
	for (i = 0; i < MAX_SERVERS; i++){
		if (e->triggers[i].server != NULL){
			snprintf(buf2, 64, "%d:%d ", e->triggers[i].server->id, es->triggersmap[i].value);
			strncat(buf, buf2, LOGLINEBUFFER_LEN - strnlen(buf, LOGLINEBUFFER_LEN));
		}
	}
	LOG("%s\n", buf);
	
	if (!udpeventon && e->type == 3){ //only if this is a congestion event and we do not have a reactive yet
		//check if all values > threshold /2
		bool allabovethreshold = true;
		for (i = 0; i <  2; i++){
			if (es->triggersmap[i].value < e->threshold/2){
				allabovethreshold = false;
				break;
			}
		}
		if (allabovethreshold){
			udpeventon = true;
			lossaction(eh, 100000);
		}
	}

	return 0;
}

uint32_t event_hash(void * data){
	struct event * e = (struct event *) data;
	return e->id;
}

bool event_equal(void * data1, void * data2, void * aux __attribute__((unused))){
	struct event * e1 = (struct event *) data1;
	struct event * e2 = (struct event *) data2;
	return e1->id == e2->id;
}

void eventhistory_finish(struct eventhistory * es){
//	printf("event %d time %d thread %d free %p\n", es->e->id, es->time, (int) pthread_self(), es);
	free (es);
}

struct event * event_init(struct eventhandler * eh){
	struct event * e;
	struct event e2e;
	e = &e2e;
//		printf("%d vssssss %d\n", e2->id, eh->events_num);

	pthread_rwlock_init(&e->lock, NULL);
	pthread_rwlock_wrlock(&e->lock);
		
	e->eventhistory_head = malloc(sizeof(struct eventhistory));
	e->eventhistory_head->valid = false;
	int j;
	for (j = 0; j < MAX_SERVERS; j++){
		e->triggers[j].server = NULL;
	}

	pthread_rwlock_unlock(&e->lock);

	pthread_mutex_lock(&eh->global_mutex);	
	e->id = eh->events_num++;
	e = hashmap_add2(eh->eventsmap, e, event_hash(e), event_equal, NULL, NULL);//add it to the map before telling the servers
	pthread_mutex_unlock(&eh->global_mutex);	
	
	return e;
}



//get a global lock when you call this function
bool event_finish(void * data, void * aux __attribute__((unused))){
	struct event * e = (struct event *) data;
	pthread_rwlock_wrlock(&e->lock);
	struct eventhistory * es, *es2;
//	printf("finish event %d\n", e->id);
	for (es = e->eventhistory_head; es != NULL; es = es2){
		es2 = es->next;
		eventhistory_finish(es);
	}
	pthread_rwlock_unlock(&e->lock);
	pthread_rwlock_destroy(&e->lock);
	return true;
}

bool event_print(void * data, void * aux __attribute__((unused))){
	struct event * e = (struct event *) data;
	printf("%d ->", e->id);
	flow_inlineprint(&e->f);
	printf(" ");
	flow_inlineprint(&e->mask);
	printf("\n");
	return true;
}

void event_fill(struct event * e __attribute__((unused)), struct trigger * t, char * buf){
	*buf = e->type; //type
	buf++;
	*(uint32_t *)buf = t->threshold;
}
