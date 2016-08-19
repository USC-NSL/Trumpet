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
struct usecase;
struct eventhandler;

enum dc_param_type{
	dcpt_num,
	dcpt_pointer,
};

//carry a number or a pointer as parameter to a dc function
struct dc_param{
	union{
		uint64_t num;
		void * pointer;
	};
	enum dc_param_type type;
};

typedef void (*dc_func)(struct eventhandler * eh, struct dc_param * param);

/*
* The data structure to represent a delayed command. 
* The command action is specified by its type. The general one dtc_func can run any function 
* with a param. A simple improvement could be to remove the type and just use the function.
*/
struct delayedcommand{
	uint64_t timeus;
	void * next;
	dc_func func;
	struct dc_param param;
};


struct trigger{
	struct serverdata * server; // unique among triggers of an event
	hashmap_elem elem; //not used
	uint32_t threshold; //triggers inside an event can have different thresholds
};

struct eventhistory;

struct event{
	struct flow f;
	uint32_t id; //unique among events
	uint32_t threshold;
	struct flow mask;
	hashmap_elem elem;
	struct eventhistory * eventhistory_head; // a link list of the event occurances
	struct trigger triggers [MAX_SERVERS]; //keep a trigger position for each server. it is filled if the server ponter is not null. A simple & fast implementation just as a prototype, it could be a hashmap in production
	pthread_rwlock_t lock; // The lock is used for accessing the eventhistory linked list
	uint16_t timeinterval;
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

struct eventhandler{
	struct hashmap * eventsmap;
	struct timespec inittime; // the initiation time of the controller. All epochs at the controller must be calculated based ont this initiation time
	pthread_mutex_t global_mutex; //To coontrol the access to eventsmapping and delayed commands
	struct serverdata * servers[MAX_SERVERS]; //An array of servers just for prototype. An entry could be null if the server with that ID is not yet contacted the controller.
	struct usecase * u;
	struct delayedcommand * dc; //a sorted linked ist of delayed commands
	pthread_t dc_pth; // just runs delayed commands
	sem_t dc_sem; // to wake up the delay command thread 
	uint16_t events_num;
	bool dc_finish; //tell the delaycommand thread to stop
};
struct delayedcommand * delayedcommand_init(uint64_t timeus);
void delayedcommand_finish(struct delayedcommand * dc);


struct eventhandler * eventhandler_init(struct usecase * u);
void eventhandler_finish(struct eventhandler * eh);
	
void eventhandler_delevent(struct eventhandler * eh, struct event * e);

/*
* Get a new event
*/
struct event * eventhandler_getevent(struct eventhandler * eh);

/*
* The server said if it could/coundn't add the trigger. What should the event do?
*/
void eventhandler_addtrigger_return(struct eventhandler * eh, uint16_t eventid, bool success, struct serverdata *server, uint32_t time);

/*
* Notify the receipt of a trigger satisfacction or poll reply from a server
*/
void eventhandler_notify(struct eventhandler * eh, uint16_t eventid, struct serverdata * server, uint32_t time, char * buf, bool satisfaction_or_query, uint16_t code);

/*
* Get epoch number
*/
uint32_t eventhandler_gettime(struct eventhandler * eh);

/*
* Get time from the eventhandle init time in nanosecond
*/
uint64_t eventhandler_gettime_(struct eventhandler *eh);

void eventhandler_addserver(struct eventhandler *eh, struct serverdata *server);
void eventhandler_removeserver(struct eventhandler *eh, struct serverdata * server);

/*
* Get number of active servers
*/
uint16_t eventhandler_activeservers(struct eventhandler *eh);

/*
* Add a delayed command
*/
void eventhandler_adddc(struct eventhandler * eh, struct delayedcommand * dc2);

/*
* Ge the servers that may receive or send traffic that matters to the event e
* servers memory must be allocated before calling the function
* returns the number of found servers
*/
uint16_t eventhandler_getserversforevent(struct eventhandler * eh, struct event * e, struct serverdata ** servers);

/*
* returns until the time is a multiple of passed ms
*/
void eventhandler_syncepoch(int ms);


/*
* Fills the buffer based on the event information (e.g., threshold) to be passed with trigger install command to the server
*/
void event_fill(struct event * e, struct trigger * t, char * buf);
bool event_print(void * data, void * aux);

#endif /* eventhandler.h */
