#include "serverdata.h"
#include <stdlib.h> 
#include <stdint.h>
#include <stdio.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h> // for nanosleep
#include <time.h> // timespec
#include <unistd.h>
#include <poll.h> // poll
#include <fcntl.h>
#include <rte_launch.h>
#include <errno.h>

#include "stdbool.h"
#include "flow.h"
#include "util.h"

#define MESSAGESIZE(H) (sizeof(struct messageheader)+H->length)

bool readmessage(struct serverdata * server);
bool readbuffer(struct serverdata  * server);
bool onlyreadonemessage(struct serverdata * server);
void resetinbuf(struct serverdata * server);
bool ReadXBytes(int fd, uint32_t x, char * buffer, bool force, uint16_t * bytesRead);
int sendtoserver2(struct serverdata * server, uint32_t size, void * buf);
void read_addtrigger_return(struct serverdata * server, struct message_addtrigger_return * m);
void read_deltrigger_return(struct serverdata * server, struct message_deltrigger_return * m);
void read_satisfaction(struct serverdata * server, struct message_triggersatisfaction * m);
void read_triggerquery_return(struct serverdata * server, struct message_triggersatisfaction * m);
void serverdata_hello(struct serverdata * server, struct message_hello * m);
void serverdata_print(struct serverdata * server);
bool getmessagebuffer(struct serverdata * server, struct messageheader **h, void ** buf, int bufsize);
void flushtoserver(struct serverdata * server);
bool sendtoserver(struct serverdata * server, void * m);
void * serverdata_read(void * _);

inline bool getmessagebuffer(struct serverdata * server, struct messageheader **h, void ** buf, int bufsize __attribute__((unused))){
	char * m;
        int ret0 = rte_mempool_get(server->outmem, (void **) &m);
        if (ret0 != 0 ){
                fprintf(stderr, "server %d cannot get memory for a message\n", server->id);
                return false;
        }
        /*int ret1 = rte_ring_enqueue(lu->ring, m);
        if (ret1 != 0){
                fprintf(stderr, "loguser ring is full\n");
                rte_mempool_put(lu->mem, m);
                return;
        }*/
        *h = (struct messageheader *)m;
        *buf = m + sizeof(struct messageheader);
        return true;
}

void flushtoserver(struct serverdata * server){
	char * obj_table [16];
	int i, n;
	struct messageheader * h;
	uint16_t sum = 0;
	do{
		n = rte_ring_dequeue_burst(server->outring, (void **)obj_table, 16);
		for (i = 0; i < n; i++){
			char * m = obj_table[i];
			h = (struct messageheader *) m;
			if (sum + MESSAGESIZE(h) > SERVERDATA_BUFSIZE){
	//			LOG("%d flush %d\n", server->id, sum);
				sendtoserver2(server, sum, server->output_buffer); //TODO if buffer is full put items back in ring and return!
				sum = 0;
			}
			memcpy(server->output_buffer + sum, m, MESSAGESIZE(h));
			sum += MESSAGESIZE(h);
		}
		if (n > 0){
			rte_mempool_put_bulk(server->outmem, (void **)obj_table, n);
		}
	}while (n > 0);

	if (sum > 0){
	//	LOG("%d flush %d\n", server->id, sum);
		sendtoserver2(server, sum, server->output_buffer);
	}
}

bool sendtoserver(struct serverdata * server, void * m){
/*	//JUST FOR TEST
	sendtoserver2(server, MESSAGESIZE(((struct messageheader *)m)), m);
	rte_mempool_put(server->outmem, m);*/
	int ret1 = rte_ring_enqueue(server->outring, m);
        if (ret1 != 0){
		do{
			LOG("%d forced flush\n", server->id);
			flushtoserver(server);
		}while(rte_ring_enqueue(server->outring, m)!=0);
/*		if (ret1 != 0){
	                fprintf(stderr, "Server %d outring is full\n", server->id);
        	        rte_mempool_put(server->outmem, m);
			return false;
		}*/
        }
	return true;
}


int sendtoserver2(struct serverdata * server, uint32_t size, void * buf){
	int result;
        uint32_t sum = 0;
        pthread_mutex_lock(&server->sendsocket_mutex);
        do{
                result = write(server->fd, (char *)buf + sum, size - sum);
                if (result < 0){
			if (errno != EAGAIN && errno != EWOULDBLOCK){
				fprintf(stderr, "Server %d: Cannot write to socket %s\n", server->id, strerror(errno));
                        	pthread_mutex_unlock(&server->sendsocket_mutex);
                                return -1;
                        }
		}else{
                	sum += result;
		}
        }while (sum < size);
        pthread_mutex_unlock(&server->sendsocket_mutex);
	LOG("%d out %d\n", server->id, size);
        return 0;
}


void serverdata_print(struct serverdata * server){
    char *hostaddrp; /* dotted decimal host addr string */
	/* 
    * gethostbyaddr: determine who sent the message 
    */
    struct sockaddr_in addr = server->addr;
    hostaddrp = inet_ntoa(addr.sin_addr);
    if (hostaddrp == NULL)
	fprintf(stderr, "ERROR on inet_ntoa\n");
    else
	    printf("%s:%d", hostaddrp, addr.sin_port);
}

// This assumes buffer is at least x bytes long,
// and that the socket is blocking.
bool ReadXBytes(int fd, uint32_t x, char * buffer, bool force, uint16_t * bytesRead) {
    uint32_t bytesRead2 = 0;
    int result;
    while (bytesRead2 < x){
        result = read(fd, buffer + bytesRead2, x - bytesRead2);
        if (result < 0){ //if already read something continue
                int errno2 = errno;
                *bytesRead = bytesRead2;
                if (force){
                        if (errno2 != EAGAIN && errno2 != EWOULDBLOCK){
                                fprintf(stderr,"Client: Error in reading the socket %s \n", strerror(errno2)); //socket will be inconsistent
                        }
                }else{
                        if (errno2 != EAGAIN && errno2 != EWOULDBLOCK){
                                return false;
                        }else{
                                return true;
                        }
                }
        }
        if (!force){
                if (result == 0){
                        *bytesRead = bytesRead2;
                        return true;
                }
        }
        if (result > 0){
                bytesRead2 += result;
        }
    }
    *bytesRead = bytesRead2;
    return true;
}


bool readbuffer(struct serverdata  * server){
        uint16_t bytesRead;
        if (SERVERDATA_BUFSIZE - server->inbuf_tail < 64){
                printf("serverdata %d: small bufsize %d %d\n", server->id, server->inbuf_head, server->inbuf_tail);
                return false;
        }
        if (!ReadXBytes(server->fd, SERVERDATA_BUFSIZE - server->inbuf_tail, server->input_buffer + server->inbuf_tail, false, & bytesRead)){
                return false;
        }
        server->inbuf_tail += bytesRead;
	if (bytesRead > 0){
                LOG("%d in %d\n", server->id, bytesRead);
/*		char buf [10240];
		char * buf2 = buf;
		int i;
		for (i = 0; i < server->inbuf_tail; i++){
			sprintf(buf2,"%02x", server->input_buffer[i]);
			buf2 += 2;
		}
		LOG("%d: %s\n", server->id, buf);*/
        }

        return (server->inbuf_tail - server->inbuf_head) >= (int)sizeof(struct messageheader);
}

void * serverdata_read(void * _){
	struct serverdata * server = (struct serverdata *)_;
	while (!server->finish){
/*		if (!onlyreadonemessage(server)){ //JUST FOR TEST
			flushtoserver(server);
		}*/
		if (!readmessage(server)){
			if (!readbuffer(server)){
				flushtoserver(server);
			}
		}
	}
	
	//printf("%d, %ld.%06ld\n", loaded, (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);
	return NULL;
}

inline void resetinbuf(struct serverdata * server){
        if (server->inbuf_head > 0 && server->inbuf_tail == server->inbuf_head){
                server->inbuf_head = 0;
                server->inbuf_tail = 0;
        }else if ((server->inbuf_tail - server->inbuf_head) < server->inbuf_head){
                memcpy(server->input_buffer, server->input_buffer + server->inbuf_head, server->inbuf_tail - server->inbuf_head);
                server->inbuf_tail = server->inbuf_tail - server->inbuf_head;
                server->inbuf_head = 0;
        }
}

//this is just to test reading one message at a time from socket
bool onlyreadonemessage(struct serverdata * server){
        uint16_t bytesRead;
        struct messageheader *  h;
        if ((server->inbuf_tail - server->inbuf_head) < (int)sizeof(struct messageheader)){
                resetinbuf(server);
                if (!ReadXBytes(server->fd, sizeof(struct messageheader), server->input_buffer + server->inbuf_tail, false, &bytesRead)){
                       return false;
                }
                server->inbuf_tail += bytesRead;
                if ((server->inbuf_tail - server->inbuf_head) < (int)sizeof(struct messageheader)){
                        return false;
                }
        }
        h = (struct messageheader *) (server->input_buffer + server->inbuf_head);
        if (server->inbuf_tail - server->inbuf_head  < (int) MESSAGESIZE(h)){
                resetinbuf(server);
                if (!ReadXBytes(server->fd, h->length, server->input_buffer + server->inbuf_tail, false, &bytesRead)){
                       return false;
                }
                server->inbuf_tail += bytesRead;
                if ((server->inbuf_tail - server->inbuf_head) < (int)MESSAGESIZE(h)){
                        return false;
                }
        }
        return readmessage(server);
}

bool readmessage(struct serverdata * server){
	struct messageheader *  h;
        if ((server->inbuf_tail - server->inbuf_head) < (int)sizeof(struct messageheader)){
                resetinbuf(server);
                return false;
        }
        h = (struct messageheader *) (server->input_buffer + server->inbuf_head);
        if (server->inbuf_tail - server->inbuf_head  < (int) MESSAGESIZE(h)){
                resetinbuf(server);
                return false;
        }
	switch (h->type){
		case mt_addtrigger_return:
			read_addtrigger_return(server, (struct message_addtrigger_return *)(server->input_buffer + server->inbuf_head + sizeof(struct messageheader)));
		break;

		case mt_deltrigger_return:
			read_deltrigger_return(server, (struct message_deltrigger_return *)(server->input_buffer + server->inbuf_head + sizeof(struct messageheader)));
		break;

		case mt_triggersatisfaction:
			read_satisfaction(server, (struct message_triggersatisfaction *)(server->input_buffer + server->inbuf_head + sizeof(struct messageheader)));
		break;

		case mt_triggerquery_return:
			read_triggerquery_return(server, (struct message_triggersatisfaction *)(server->input_buffer + server->inbuf_head + sizeof(struct messageheader)));
		break;

		case mt_bye:
			printf("serverdata %d: finished\n", server->id);
			server->finish = true;
			eventhandler_removeserver(server->eh, server);
//			serverdata_finish(server);
		break;

		case mt_hello:
			serverdata_hello(server, (struct message_hello *)(server->input_buffer + server->inbuf_head + sizeof(struct messageheader)));
		break;

		default:
			fprintf(stderr, "serverdata %d: Message type %d not found\n", server->id, h->type);
	}
	server->inbuf_head += MESSAGESIZE(h);
        return true;
}



void serverdata_hello(struct serverdata * server, struct message_hello * m){
	//reply
        struct messageheader * h;
	struct message_hello * m2;
	if (!getmessagebuffer(server, &h, (void **)&m2, sizeof(struct message_hello))){
                return;
        }
        h->type = mt_hello;
        h->length = sizeof(struct message_hello);
	m2->id = 0;
	m2->time = 0;
	eventhandler_syncepoch(10); //sync the reply to the server with the epoch at controller
	sendtoserver(server, h);
	flushtoserver(server);
	server->joiningtime = eventhandler_gettime(server->eh);
	LOG("%"PRIu64": serverdata %d: hello %d \n", rdtscl(), server->id, m->id);
}

void read_addtrigger_return(struct serverdata * server, struct message_addtrigger_return * m){
	LOG("%"PRIu64": serverdata %d: add_return time %d event %d ctime %d\n", rdtscl(), server->id, m->time, m->eventid, serverdata_s2ctime(server, m->time));	
	eventhandler_addtrigger_return(server->eh, m->eventid, m->success, server, m->time);
}

void read_deltrigger_return(struct serverdata * server, struct message_deltrigger_return * m){
	LOG("%"PRIu64": serverdata %d: del_return time %d event %d ctime %d\n", rdtscl(), server->id, m->time, m->eventid, serverdata_s2ctime(server, m->time));	
//	eventhandler_addtrigger_return(server->eh, m->eventid, m->success, server, m->time);
}


void read_satisfaction(struct serverdata * server, struct message_triggersatisfaction * m){
	LOG("%"PRIu64": serverdata %d: sat time %d event %d ctime %d\n", rdtscl(), server->id, m->time, m->eventid, serverdata_s2ctime(server, m->time));
	eventhandler_notify(server->eh, m->eventid, server, m->time, m->buf, true, m->code);
}

void read_triggerquery_return(struct serverdata * server, struct message_triggersatisfaction * m){
	LOG("%"PRIu64": serverdata %d: poll_return time %d event %d ctime %d\n", rdtscl(), server->id, m->time, m->eventid, serverdata_s2ctime(server, m->time));	
	eventhandler_notify(server->eh, m->eventid, server, m->time, m->buf, false, m->code);
}


void serverdata_addtrigger(struct serverdata * server, struct event * e, struct trigger * t){
	//send a message to the server
	struct message_addtrigger * m;
	struct messageheader * h;

	if (!getmessagebuffer(server, &h, (void **)&m, sizeof(struct message_addtrigger))){
                return;
        }	
	h->type = mt_addtrigger;
	h->length = sizeof (struct message_addtrigger);

	m->eventid = e->id;
	flow_fill(&m->f, &e->f);
	flow_fill(&m->mask, &e->mask);
	event_fill(e, t, m->buf);
	
	sendtoserver(server, h);
}


void serverdata_deltrigger(struct serverdata * server, struct event * e, struct trigger * t __attribute__((unused))){
	//send a message to the server
	struct message_addtrigger * m;
	struct messageheader * h;

	if (!getmessagebuffer(server, &h, (void **)&m, sizeof(struct message_deltrigger))){
                return;
        }	
	h->type = mt_deltrigger;
	h->length = sizeof (struct message_deltrigger);

	m->eventid = e->id;
	flow_fill(&m->f, &e->f);
	flow_fill(&m->mask, &e->mask);
	
	sendtoserver(server, h);
}

void serverdata_triggerquery(struct serverdata * server, struct event * e, uint32_t time){
	LOG("%"PRIu64": serverdata %d: poll time %d event %d ctime %d\n", rdtscl(), server->id, time, e->id, serverdata_s2ctime(server, time));
	struct message_triggerquery * m;
	struct messageheader * h;
	if (!getmessagebuffer(server, &h, (void **)&m, sizeof(struct message_triggerquery))){
                return;
        }
	h->type = mt_triggerquery;
	h->length = sizeof(struct message_triggerquery);
	
	m->eventid = e->id;
	flow_fill(&m->f, &e->f);
	flow_fill(&m->mask, &e->mask);
	m->time = time;
	
	sendtoserver(server, h);
}

inline uint32_t serverdata_s2ctime(struct serverdata * server, uint32_t time){
	return time + server->joiningtime;
}


inline uint32_t serverdata_c2stime(struct serverdata * server, uint32_t time){
	return time - server->joiningtime;
}


bool serverdata_equal(struct serverdata * server1, struct serverdata * server2){
	return server1->id == server2->id;
}

struct serverdata * serverdata_init(struct eventhandler * eh, int fd, struct sockaddr_in addr, uint32_t id, uint8_t core){
	struct serverdata * server  =  malloc (sizeof (struct serverdata));	
	server->finish = false;
	server->fd = fd;
	server->inbuf_tail = 0;
	server->inbuf_head = 0;
	server->core = core;
	int flags = fcntl(server->fd, F_GETFL, 0);
        fcntl(server->fd, F_SETFL, flags | O_NONBLOCK);
	server->id = id;
	server->addr = addr;
	server->joiningtime = eventhandler_gettime(eh);
	printf ("serverdata %d: Connection established at %d from ", server->id, server->joiningtime);
	serverdata_print(server);	
	printf ("\n");
	server->eh = eh;
	pthread_mutex_init(&server->sendsocket_mutex, NULL);
	char buf[64];
	snprintf(buf, 32, "server outring %d", server->id);
	uint32_t itemsize = sizeof(struct message_hello);
	if (itemsize < sizeof(struct message_addtrigger)) itemsize = sizeof(struct message_addtrigger);
	if (itemsize < sizeof(struct message_addtrigger_return)) itemsize = sizeof (struct message_addtrigger_return);
	if (itemsize < sizeof(struct message_triggersatisfaction)) itemsize = sizeof(struct message_triggersatisfaction);
	if (itemsize < sizeof(struct message_triggerquery)) itemsize = sizeof (struct message_triggerquery);

	itemsize += sizeof(struct messageheader);
	
	int ringsize = gbp(SERVERDATA_BUFSIZE/itemsize);
/*	if (ringsize < SERVERDATA_BUFSIZE/itemsize){
		ringsize *= 2;
	}*/
	server->outring = rte_ring_create(buf, ringsize, rte_socket_id(), 0);
        if (server->outring == NULL) {
                rte_panic("Cannot create ring for server %d\n", server->id);
        }
	snprintf(buf, 32, "server outmem %d", server->id);
        server->outmem = rte_mempool_create(buf,
                                       ringsize*2+32*8,
                                       itemsize,
                                       32,
                                       0,
                                       NULL, NULL,
                                       NULL,      NULL,
                                       rte_socket_id(), 0);
	if (server->outmem == NULL){
                rte_panic("Cannot create mempool for server %d size %d\n", server->id, ringsize);
	}
	//pthread_create(&server->pth, NULL, (void *)serverdata_read, server);	
	rte_eal_remote_launch((void *) serverdata_read, server, core);
	return server;
}

void serverdata_finish(struct serverdata * server){
	server->finish = true;
	printf ("serverdata %d: Connection closed from ", server->id);
	serverdata_print(server);
	printf ("\n");
	rte_eal_wait_lcore(server->core);
//	pthread_join(server->pth, NULL);
	if (server->fd > 0){
		struct messageheader * h;
		struct message_hello * m;
		if (getmessagebuffer(server, &h, (void **)&m, 0)){
			h->length = 0;
			h->type = mt_bye;
			sendtoserver(server, h);
			flushtoserver(server);
		}
		close(server->fd);
		server->fd = 0;
	}
	pthread_mutex_destroy(&server->sendsocket_mutex);
	rte_ring_free(server->outring);
	free(server);
}
