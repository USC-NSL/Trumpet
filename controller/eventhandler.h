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
#define EPOCH_NS 10000000


struct serverdata;
struct delayedcommand;

struct trigger{
	struct serverdata * server; // unique among triggers of an event
	hashmap_elem elem; //not used
	uint32_t threshold; //triggers inside an event can have different thresholds
};

struct eventhistory;

struct event{
	struct flow f;
	uint32_t id; //unique among events
	struct flow mask;
	uint32_t threshold;
	hashmap_elem elem;
	struct eventhistory * eventhistory_head; // a link list of the event occurances
	struct trigger triggers [MAX_SERVERS]; //keep a trigger position for each server. it is filled if the server ponter is not null. A simple & fast implementation just as a prototype, it could be a hashmap in production
	pthread_rwlock_t lock; // The lock is used for accessing the eventhistory linked list
	uint8_t type;
};

struct triggerhistory{
	uint32_t value;
	bool valuereceived;
};

struct eventhistory{
	uint32_t time;
	bool valid; // if the eventhistory is valid or not. This is for an optimization to not malloc/free to often
	struct event * e;
	struct eventhistory * next;
	struct eventhistory * prev;
	struct trigger * inittrigger; // which triggeer was the first one that made us create this event history
	struct triggerhistory triggersmap [MAX_SERVERS]; //a triggerhisotry per server (as a prototype)
};

/*
* 0: tcp
* 2: congestion
* 3: network wide
*/
enum usecase_type{
        usecase_tcp,
        usecase_congestion,
	usecase_networkwide,
};

struct eventhandler{
	struct hashmap * eventsmap;
	struct timespec inittime; // the initiation time of the controller. All epochs at the controller must be calculated based ont this initiation time
	uint16_t events_num;
	pthread_mutex_t global_mutex; //To coontrol the access to eventsmapping and delayed commands
	sem_t dc_sem; // to wake up the delay command thread 
	struct delayedcommand * dc; //a sorted linked ist of delayed commands
	bool dc_finish; //tell the delaycommand thread to stop
	pthread_t dc_pth; // just runs delayed commands
	struct serverdata * servers[MAX_SERVERS]; //An array of servers just for prototype. An entry could be null if the server with that ID is not yet contacted the controller.
	enum usecase_type usecase;
};
void eventhandler_syncepoch(int ms);

struct eventhandler * eventhandler_init(enum usecase_type u);
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
