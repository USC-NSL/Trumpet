#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H 1

#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include "flow.h"
#include "stdbool.h"
#include "hashmap.h"

#define MAX_SERVERS 4
#define MAX_EVENTHISTORY 4


struct serverdata;
struct delayedcommand;

struct trigger{
	struct serverdata * server;
	hashmap_elem elem;
	uint32_t threshold;
};

struct eventhistory;

struct event{
	struct flow f;
	uint32_t id;
	struct flow mask;
	uint32_t threshold;
	hashmap_elem elem;
	struct eventhistory * eventhistory_head;
	struct trigger triggers [MAX_SERVERS];
	pthread_rwlock_t lock;
	uint8_t type;
};

struct triggerhistory{
	uint32_t value;
	bool valuereceived;
};

struct eventhistory{
	uint32_t time;
	bool valid;	
	struct event * e;
	struct eventhistory * next;
	struct eventhistory * prev;
	struct trigger * inittrigger;
	struct triggerhistory triggersmap [MAX_SERVERS];
};

struct eventhandler{
	struct hashmap * eventsmap;
	struct timespec inittime;
	uint16_t events_num;
	pthread_mutex_t global_mutex;
	sem_t dc_sem;
	struct delayedcommand * dc;
	bool dc_finish;
	pthread_t dc_pth;
	struct serverdata * servers[MAX_SERVERS];
};
void eventhandler_syncepoch(int ms);

struct eventhandler * eventhandler_init(void);
void eventhandler_finish(struct eventhandler * eh);
void eventhandler_addeventdelay(struct eventhandler * eh, uint32_t eventnum, uint32_t delayus);
void eventhandler_delevent(struct eventhandler * eh, struct event * e);
void eventhandler_addtrigger_return(struct eventhandler * eh, uint16_t eventid, bool success, struct serverdata *server, uint32_t time);
void eventhandler_notify(struct eventhandler * eh, uint16_t eventid, struct serverdata * server, uint32_t time, char * buf, bool satisfaction_or_query, uint16_t code);
uint32_t eventhandler_gettime(struct eventhandler * eh);
void eventhandler_addserver(struct eventhandler *eh, struct serverdata *server);
void eventhandler_removeserver(struct eventhandler *eh, struct serverdata * server);
uint16_t eventhandler_activeservers(struct eventhandler *eh);

void event_fill(struct event * e, struct trigger * t, char * buf);

#endif /* eventhandler.h */
