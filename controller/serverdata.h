#ifndef SERVERDATA_H
#define SERVERDATA_H 1
#include <netinet/in.h>
#include <stdio.h>
#include "stdbool.h"
#include "eventhandler.h"
#include "messages.h"

#define SERVERDATA_BUFSIZE 1500

struct serverdata{
	struct eventhandler * eh;
	struct rte_ring * outring;
	struct rte_mempool * outmem;
	pthread_mutex_t sendsocket_mutex;
	uint32_t id;
	int fd;
	uint32_t joiningtime;
	uint16_t inbuf_head;
	uint16_t inbuf_tail;
	bool finish;
	uint8_t core;
	struct sockaddr_in addr;
	char input_buffer[SERVERDATA_BUFSIZE];
	char output_buffer[SERVERDATA_BUFSIZE];
};

void * serverdata_read(void * _);
void serverdata_addtrigger(struct serverdata * server, struct event * e, struct trigger * t);
void serverdata_deltrigger(struct serverdata * server, struct event * e, struct trigger * t);
void serverdata_triggerquery(struct serverdata * server, struct event * e, uint32_t time);

bool serverdata_equal(struct serverdata * server1, struct serverdata * server2);

struct serverdata * serverdata_init(struct eventhandler * eh, int childfd, struct sockaddr_in clientaddr, uint32_t id, uint8_t core);
void serverdata_finish(struct serverdata * server);
uint32_t serverdata_s2ctime(struct serverdata * server, uint32_t time);
uint32_t serverdata_c2stime(struct serverdata * server, uint32_t time);

#endif /* serverdata.h */
