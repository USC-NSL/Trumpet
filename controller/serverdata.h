#ifndef SERVERDATA_H
#define SERVERDATA_H 1
#include <netinet/in.h>
#include <stdio.h>
#include "stdbool.h"
#include "eventhandler.h"
#include "messages.h"

#define SERVERDATA_BUFSIZE 1500

/*
* The struct that is responsible for interacting witht he server
* It is implemented as a polling loop that reads out of the server and if no data is available it will write any message in the queue to the server. So each serverdata will use a core
* The queue is implemented as a ring that can be written by multiple threads.
* If a thread sees a full thread, it will cause a flush to the output socket (spending its own cycles).
* This uses non-blocking sockets.
*/
struct serverdata{
	struct eventhandler * eh;
	struct rte_ring * outring;
	struct rte_mempool * outmem;
	pthread_mutex_t sendsocket_mutex;//need the mutex because of unexpected flushes
	uint32_t id;
	int fd; //socket handle
	uint32_t joiningtime;
	uint16_t inbuf_head;
	uint16_t inbuf_tail;
	bool finish;
	uint8_t core;
	struct sockaddr_in addr;
	char input_buffer[SERVERDATA_BUFSIZE];
	char output_buffer[SERVERDATA_BUFSIZE];
};

struct serverdata * serverdata_init(struct eventhandler * eh, int childfd, struct sockaddr_in clientaddr, uint32_t id, uint8_t core);
void serverdata_finish(struct serverdata * server);

/*
* Send add trigger message to the server
*/
void serverdata_addtrigger(struct serverdata * server, struct event * e, struct trigger * t);

/*
* Send delete trigger message to the server
*/
void serverdata_deltrigger(struct serverdata * server, struct event * e, struct trigger * t);

/*
* Query a server about an event at a specific time
*/
void serverdata_triggerquery(struct serverdata * server, struct event * e, uint32_t time, struct flow * f);

bool serverdata_equal(struct serverdata * server1, struct serverdata * server2);

/*
* Convert server epoch num to the controller epoch number
*/
uint32_t serverdata_s2ctime(struct serverdata * server, uint32_t time);

/*
* Convert controller epoch number to the server epoch number
*/
uint32_t serverdata_c2stime(struct serverdata * server, uint32_t time);

#endif /* serverdata.h */
