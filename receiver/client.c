#include "client.h"
#include <sys/socket.h>
#include <sys/types.h>          
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <rte_ring.h>
#include <rte_memcpy.h>
#include <rte_mempool.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "util.h"
#include "messages.h"
#include "triggertable2.h"
#include "flatreport.h"

#define MESSAGESIZE(H) (sizeof(struct messageheader) + H->length)

void disconnect_from_server(struct client * c);
int connect_to_server(struct client * c, char * ip, uint16_t port);
int client_sendtocontroller2(struct client * c, uint32_t size, void * buf);
void addtrigger(struct client * c, struct message_addtrigger * m);
void deltrigger(struct client * c, struct message_deltrigger * m2);
void gettrigger(struct client * c, struct message_triggerquery * m);
void gettrigger2(struct client * c, struct message_triggerquery * m);
void * client_mainloop(void * c);
void * client_senderworker(void * c);
bool ReadXBytes(int fd, uint32_t x, char * buffer, bool force, uint16_t * bytesRead);
bool readamessage(struct client * c);
struct requestlist * findrequestlist(struct client * c, uint32_t time);
void queuerequest(struct client *c, struct message_triggerquery *m);
bool getmessagebuffer(struct client * c, struct messageheader **h, void ** buf, int bufsize);
bool readbuffer(struct client * c);
bool onlyreadonemessage(struct client * c);
inline void resetinbuf(struct client * c);
int client_flushtocontroller(struct client * c);
int sendtocontrollerwait(struct client *c, uint16_t size, void * buf);

inline bool getmessagebuffer(struct client * c, struct messageheader **h, void ** buf, int bufsize){
	int i;
	for  (i = 0; i < 10 && c->outbuf_tail + bufsize >= CLIENT_BUFSIZE; i++){
		LOG("%"PRIu64": Client mandator flush! %d\n", rte_rdtsc(), c->outbuf_tail);
		client_flushtocontroller(c);
	}
	if (c->outbuf_tail + bufsize >= CLIENT_BUFSIZE){
		return false;
	}
	*h = (struct messageheader *)(c->output_buffer + c->outbuf_tail);
	*buf = c->output_buffer + c->outbuf_tail + sizeof(struct messageheader);
	c->outbuf_tail += sizeof (struct messageheader) + bufsize;
	return true;
}

//ASSUMES: message_triggersatisfaction is the largest message type client sends
struct client * client_init(char * ip, uint16_t port, uint8_t core, bool onlysync){
	struct client * c = MALLOC(sizeof(struct client));
        c->finish = false;
	c->delay = 500;
	c->core = core;
	c->inbuf_head = 0;
	c->inbuf_tail = 0;
	c->outbuf_tail = 0;
	c->hasdatatoread = true;
	c->readseqnum = 0;
	c->rl_lastindex = -1;	
	connect_to_server(c, ip, port);

	c->rl = NULL;
	c->freerl = NULL;
	int i;
	struct requestlist * rl;
	for (i = 0; i < 100; i++){
		rl = MALLOC(sizeof(struct requestlist));
		rl->filled = 0;
		rl->next = c->freerl;
		c->freerl = rl;
	}
	if (onlysync){
		int flags = fcntl(c->fd, F_GETFL, 0); 
		fcntl(c->fd, F_SETFL, flags | O_NONBLOCK);	
		c->ring = NULL;
		c->mem = NULL;
		
	}else{
		c->ring = rte_ring_create("clientring", 1024, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
		if (c->ring == NULL) {
        		rte_panic("Cannot create ring for client\n");
	        }
		c->mem = rte_mempool_create("Message_pool",
                                       1024,
                                       sizeof(struct message_triggersatisfaction),
                                       32,
                                       0,
                                       NULL, NULL,
                                       NULL,      NULL,
                                       rte_socket_id(),
                                       MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET);

		pthread_mutex_init(&c->sendsocket_mutex, NULL);
		int ret0 = pthread_create(&c->sender_thread, NULL, client_senderworker, c);
		int ret1 = pthread_create(&c->receiver_thread, NULL, client_mainloop, c);
		if (ret0 != 0 || ret1 != 0){
			fprintf(stderr, "client cannot create threads\n");
		}
	}
	return c;
}

void client_finish(struct client * c){
//	rte_ring_free(c->ring); not available in dpdk 2.0
	c->finish = true;
	if (!c->onlysync){
		pthread_join(c->sender_thread, NULL);
		pthread_join(c->receiver_thread, NULL);
		disconnect_from_server(c);
		pthread_mutex_destroy(&c->sendsocket_mutex);
	}else{
		disconnect_from_server(c);
	}
	struct requestlist * rl, *rl2;
	for (rl = c->rl; rl != NULL; rl = rl2){
		rl2 = rl->next;
		free(rl);
	}
	for (rl = c->freerl; rl != NULL; rl = rl2){
		rl2 = rl->next;
		free(rl);
	}
	
	FREE(c);
}

void client_hello(struct client * c, uint32_t id, uint32_t time){
	if (c->fd == 0){
		return;
	}
	struct messageheader * h;
	struct message_hello * m;
	char buf [sizeof(struct messageheader) + sizeof(struct message_hello)];
	h = (struct messageheader *) buf;
	m = (struct message_hello *) (buf + sizeof (struct message_hello));
	h->type = mt_hello;
	h->length = sizeof (struct message_hello);
	m->id = id;
	m->time = time;
	sendtocontrollerwait(c, MESSAGESIZE(h), h);
}


int connect_to_server(struct client * c, char * ip, uint16_t port){
    int sockfd = 0;
    struct sockaddr_in serv_addr;

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        fprintf(stderr, "Error : Could not create socket \n");
        c->fd = 0;
        return 1;
    }

    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, ip, &serv_addr.sin_addr)<=0){
        c->fd = 0;
        fprintf(stderr, "inet_pton error occured\n");
        return 1;
    }

    if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
       fprintf(stderr, "Connection to the controller failed \n");
        c->fd = 0;
       return 1;
    }
        c->fd = sockfd;
	int one = 1;
	setsockopt(c->fd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
        return 0;
}

void disconnect_from_server(struct client * c){
	if (c->fd > 0){
		struct messageheader * h;
		struct message_hello * m;
		if (getmessagebuffer(c, &h, (void **)&m, 0)){
			h->length = 0;
			h->type = mt_bye;
			client_flushtocontroller(c);	
		}
                close(c->fd);
        }
	c->fd = 0;
}

int client_flushtocontroller(struct client * c){
	if (c->outbuf_tail == 0 || c->fd == 0){
		return 0;
	}
	int written = client_sendtocontroller2(c, c->outbuf_tail, c->output_buffer);
	if (likely(written > 0)) {
		c->outbuf_tail -= written;
		if (c->outbuf_tail > 0){ //not all of data written
			memmove(c->output_buffer, c->output_buffer + written, c->outbuf_tail); //memmove instead of memcpy for overlapping buffer
		}
//		LOG("out %d\n", written);
	}
	return written;
}

int client_sendtocontroller2(struct client * c, uint32_t size, void * buf){
	if (c->fd == 0){
		return 0;
	}
        int result;
	uint32_t sum = 0;
	if (!c->onlysync) pthread_mutex_lock(&c->sendsocket_mutex);
	do{
	        result = write(c->fd, (char *)buf + sum, size - sum);
        	if (result < 0){
			if (!c->onlysync) pthread_mutex_unlock(&c->sendsocket_mutex);
			if (errno != EAGAIN && errno != EWOULDBLOCK){
				int errno2 = errno;
				fprintf(stderr, "Client: cannto write at %d error %s\n", c->fr->step, strerror(errno2));
				return -1;
			}else{
				return sum;
			}
		}else{
			sum += result;
		}
	}while (sum < size);

	if (!c->onlysync) pthread_mutex_unlock(&c->sendsocket_mutex);
        return sum;
}

void __attribute__((unused)) client_test(struct client * c){
	struct messageheader * h;
        struct message_addtrigger_return * mm;
	if (!getmessagebuffer(c, &h, (void **)&mm, sizeof(struct message_addtrigger_return))){
		return;
	}
        h->type = mt_addtrigger_return;
        h->length = sizeof(struct message_addtrigger_return);
        mm->success = true;
        mm->eventid = 2;
	client_flushtocontroller(c);	
        struct message_triggersatisfaction * m;
	if (!getmessagebuffer(c, &h, (void **)&m, sizeof(struct message_triggersatisfaction))){
		return;
	}

        h->type = mt_triggerquery_return;
        h->length = sizeof (struct message_triggersatisfaction);
        m->time = 0;
        m->eventid = 1;
//                        trigger_getreport(t, m.buf, time);
	while (c->outbuf_tail > 0 && client_flushtocontroller(c) >= 0);	
}

void addtrigger(struct client * c, struct message_addtrigger * m2){
	//must return success/failure message to the controller
	LOG("%"PRIu64": add step %d event %d\n", rte_rdtsc(), c->fr->step, m2->eventid);

	struct trigger * t = NULL;
	uint8_t type_id = *((uint8_t*)m2->buf);
	struct triggertype * type = triggertable_gettype(c->fr->tt, type_id);
	bool success = true;
	if (type == NULL){
		fprintf(stderr, "client: type with id %d not found to add trigger \n", type_id);
		success = false;	
	}else{
		t = triggertable_gettrigger(c->fr->tt);
		uint32_t threshold = *((uint32_t *)((intptr_t)m2->buf + 1));
        	t = counter_trigger_init(t, m2->eventid, m2->eventid*100, &m2->f, &m2->mask, type, threshold);
	        triggertable_addtrigger(c->fr->tt, t);
		trigger_print(t, NULL);
		flatreport_matchforatrigger(c->fr, t);
	}

	struct messageheader * h;
	struct message_addtrigger_return * mm;
	if (!getmessagebuffer(c, &h, (void **)&mm, sizeof(struct message_addtrigger_return))){
		return;
	}

	h->type = mt_addtrigger_return;
	h->length = sizeof(struct message_addtrigger_return);
	mm->success = success;
	mm->eventid = m2->eventid;
	mm->time = c->fr->step;
//	clent_sendtocontroller (c, MESSAGESIZE(h), h);
	
	///////// TEST
/*	if (t!= NULL){
		usleep(100);
		client_sendtriggersync(c, t, 10, true);
	}*/
}


void deltrigger(struct client * c, struct message_deltrigger * m2){
	//must return success/failure message to the controller
	LOG("%"PRIu64": del step %d event %d\n", rte_rdtsc(), c->fr->step, m2->eventid);
	struct trigger * temptable[FLOWENTRY_TRIGGER_SIZE];
	uint16_t num = FLOWENTRY_TRIGGER_SIZE;
	triggertable_justmatch(c->fr->tt, &m2->f, &m2->mask, temptable, &num);
	uint16_t i;
	struct trigger * t;
	bool success = true;
	for (i = 0; i < num; i++){
		t = temptable[i];
		if (t == NULL){
			success = false;
			fprintf(stdout, "Client: NULL trigger among %d for deleting event %d ", num,  m2->eventid);
			flow_inlineprint(&m2->f);
			printf(" ");
			flow_inlineprint(&m2->mask);
			printf("\n");
			continue;
		}
		if (t->eventid == m2->eventid){
			trigger_print(t, NULL);
			success &= triggertable_removetrigger(c->fr->tt, t);
		}
	}

	struct messageheader * h;
	struct message_deltrigger_return * mm;
	if (!getmessagebuffer(c, &h, (void **)&mm, sizeof(struct message_deltrigger_return))){
		return;
	}

	h->type = mt_deltrigger_return;
	h->length = sizeof(struct message_deltrigger_return);
	mm->success = success;
	mm->eventid = m2->eventid;
	mm->time = c->fr->step;
//	clent_sendtocontroller (c, MESSAGESIZE(h), h);
}


inline int sendtocontrollerwait(struct client *c, uint16_t size, void * buf){
	int ret = 0;
	int sum = 0;
	do {
		ret = client_sendtocontroller2(c, size, (char*)buf + sum);
		sum += ret;
	}while (ret >= 0 && sum < size);
	return ret;
}

void client_sendsatisfactionsync(struct client * c, struct trigger *t, uint32_t time){
	if (c->fd == 0){
		return;
	}
	LOG("%"PRIu64": send time %d step %d event %d\n", rte_rdtsc(), time, c->fr->step, t->eventid);
	char buf [sizeof(struct message_triggersatisfaction) + sizeof(struct messageheader)];
	struct messageheader * h = (struct messageheader *) buf;
	struct message_triggersatisfaction * m2  = (struct message_triggersatisfaction *) (buf + sizeof(struct messageheader));
	h->type = mt_triggersatisfaction;
        h->length = sizeof (struct message_triggersatisfaction);
	m2->time = time;
	m2->eventid = t->eventid;
	bool code = triggertable_getreport(c->fr->tt, t, m2->buf, time);
	m2->code = code ? 0 : 1;
	sendtocontrollerwait(c, MESSAGESIZE(h), h);
}

void client_sendtriggersync(struct client * c, struct trigger *t, uint32_t time, bool satisfaction_or_query){
	if (c->fd == 0){
		return;
	}
	LOG("%"PRIu64": send time %d step %d event %d\n", rte_rdtsc(), time, c->fr->step, t->eventid);
	struct messageheader * h;
	struct message_triggersatisfaction * m2;
	if (!getmessagebuffer(c, &h, (void **)&m2, sizeof(struct message_triggersatisfaction))){
		return;
	}
	if (satisfaction_or_query){
	        h->type = mt_triggersatisfaction;
	}else{
	        h->type = mt_triggerquery_return;
	}
        h->length = sizeof (struct message_triggersatisfaction);
	m2->time = time;
	m2->eventid = t->eventid;
	bool code = triggertable_getreport(c->fr->tt, t, m2->buf, time);
	m2->code = code ? 0 : 1;
	

//	client_flushtocontroller(c);// JUST FOR TEST
}

void gettrigger2(struct client *c, struct message_triggerquery *m){
//return the value of the trigger at a specific time to the controller
	struct trigger * temptable[FLOWENTRY_TRIGGER_SIZE];
	uint16_t num = FLOWENTRY_TRIGGER_SIZE;
	triggertable_justmatch(c->fr->tt, &m->f, &m->mask, temptable, &num);
	uint16_t i;
	struct trigger * t;
	for (i = 0; i < num; i++){
		t = temptable[i];
//		trigger_print2(t, NULL);
		if (t->eventid == m->eventid){
			client_sendtriggersync(c, t, m->time, false);
		}
	}
}

struct requestlist * findrequestlist(struct client * c, uint32_t time){
	struct requestlist * rl, *rl_last;
	rl_last = NULL;
	for (rl = c->rl; rl != NULL; rl_last = rl, rl = rl->next){
		if (rl->time == time && rl->filled < REQUESTLIST_BUFSIZE){
			return rl;
		}else if (rl->time > time){
			return rl_last;
		}
	}
	return rl_last;
}

void queuerequest(struct client *c, struct message_triggerquery *m){
	struct requestlist * rl = findrequestlist(c, m->time);
	if (rl == NULL || rl->time != m->time || rl->filled == REQUESTLIST_BUFSIZE){
/*		while (rl != NULL && rl->time == m->time && rl->filled == REQUESTLIST_BUFSIZE && rl->next != NULL){
			rl = rl->next;
		}*/
		struct requestlist * rl2 = c->freerl;
		if (rl2 == NULL){
			fprintf(stderr, "Client has no more free request list for time %d\n", m->time);
			return;
		}
		c->freerl = (struct requestlist *)rl2->next;
		if (rl == NULL){
			//put at head
			rl2->next = c->rl;
			c->rl = rl2;
		}else{	
			//insert in list
			rl2->next = rl->next;
			rl->next = rl2;
		}
		rl2->time = m->time;
		rl2->filled = 0;

		rl = rl2;
	}
	struct message_triggerquery * m2 = &rl->buf[rl->filled++];
	memcpy(m2, m, sizeof(struct message_triggerquery));
}


void gettrigger(struct client * c, struct message_triggerquery * m){
	LOG("%"PRIu64": poll time %d step %d event %d seq %d\n", rte_rdtsc(), m->time, c->fr->step, m->eventid, c->readseqnum);
	if (c->fr->step < m->time){
//		LOG("wait time %d > step %d\n", m->time, c->fr->step);
		queuerequest(c, m);
		return;
	}
	gettrigger2(c, m);	
}



void client_readsync(struct client * c, uint64_t timebudget, uint64_t start){
	if (c->fd == 0){
		return;
	}
	c->readseqnum++;
	if (c->rl != NULL && c->rl->time <= c->fr->step){ //process some queued requests
		struct requestlist * rl;
		if (c->rl_lastindex < 0){
			c->rl_lastindex	 = 0;
		}
		rl = c->rl;
		do{
			gettrigger2(c, rl->buf + c->rl_lastindex);
			c->rl_lastindex++;
			if (c->rl_lastindex >= rl->filled){
				rl->filled = 0;
				c->rl = rl->next;
				//add to free list
				rl->next = c->freerl;
				c->freerl = rl;
				
				client_flushtocontroller(c);// FIXME: can exceed time budget
				rl = c->rl;
				if (rl == NULL){
					c->rl_lastindex = -1;	
					break;
				}else{
					c->rl_lastindex = 0;	
				}
			}
				
		}while (!c->finish && (rte_rdtsc() - start) < timebudget);
	}else{	//read new requests
		do{
		/*	if (!onlyreadonemessage(c)){  //JUST FOR TEST
				if (c->outbuf_tail > 0){
					client_flushtocontroller(c);
				}else{break;}
			}*/
			if (!readamessage(c)){
				if (c->outbuf_tail > 0){
					client_flushtocontroller(c);
				}else if (!readbuffer(c)){
					break;
				}
			}
		}while (!c->finish && (rte_rdtsc() - start) < timebudget);
	}
}

//this is the first message to receive
void client_waitforhello(struct client * c){
	if (c->fd == 0){
		return;
	}
	printf("waitforhello\n");
    	uint16_t bytesRead = 0;
	if (!ReadXBytes(c->fd, sizeof(struct messageheader), c->input_buffer, true, &bytesRead)){
		fprintf(stderr, "Client: cannot wait for hello\n");
		return;
	}
	struct messageheader * h = (struct messageheader *) c->input_buffer;
	if (!ReadXBytes(c->fd, h->length, c->input_buffer + bytesRead, true, &bytesRead)){
		fprintf(stderr, "Client: cannot wait for hello\n");
		return;
	}
	if (h->type != mt_hello){
		fprintf(stderr, "Client:  Waiting for Hello, Didn't expect message of type %d\n", h->type);	
	}
}


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

bool readbuffer(struct client * c){
	uint16_t bytesRead;
	if (CLIENT_BUFSIZE - c->inbuf_tail < 64){
		printf("Client: small bufsize %d %d\n", c->inbuf_head, c->inbuf_tail);
		return false;
	}
	if (!ReadXBytes(c->fd, CLIENT_BUFSIZE - c->inbuf_tail, c->input_buffer + c->inbuf_tail, false, &bytesRead)){
		return false;	
	}
/*	if (bytesRead > 0){
		LOG("in %d\n", bytesRead);
	}*/
	c->inbuf_tail += bytesRead;
	return (c->inbuf_tail - c->inbuf_head) >= (int)sizeof(struct messageheader);
}

inline void resetinbuf(struct client * c){
	if (c->inbuf_head > 0 && c->inbuf_tail == c->inbuf_head){
		c->inbuf_head = 0;
		c->inbuf_tail = 0;
	}else if ((c->inbuf_tail - c->inbuf_head) < c->inbuf_head){
		memcpy(c->input_buffer, c->input_buffer + c->inbuf_head, c->inbuf_tail - c->inbuf_head);
		c->inbuf_tail = c->inbuf_tail - c->inbuf_head;
		c->inbuf_head = 0;
	}

}

//this is just to test reading one message at a time from socket
bool onlyreadonemessage(struct client * c){
	uint16_t bytesRead;
	struct messageheader *  h;
	if ((c->inbuf_tail - c->inbuf_head) < (int)sizeof(struct messageheader)){
		resetinbuf(c);
		if (!ReadXBytes(c->fd, sizeof(struct messageheader), c->input_buffer + c->inbuf_tail, false, &bytesRead)){
         	       return false;
	        }
		c->inbuf_tail += bytesRead;
		if ((c->inbuf_tail - c->inbuf_head) < (int)sizeof(struct messageheader)){
			return false;
		}
	}
	h = (struct messageheader *) (c->input_buffer + c->inbuf_head);
	if (c->inbuf_tail - c->inbuf_head  < (int) MESSAGESIZE(h)){
		resetinbuf(c);
		if (!ReadXBytes(c->fd, h->length, c->input_buffer + c->inbuf_tail, false, &bytesRead)){
         	       return false;
	        }
		c->inbuf_tail += bytesRead;
		if ((c->inbuf_tail - c->inbuf_head) < (int)MESSAGESIZE(h)){
			return false;
		}
	}
	return readamessage(c);
}

bool readamessage(struct client * c){
	struct messageheader *  h;
	if ((c->inbuf_tail - c->inbuf_head) < (int)sizeof(struct messageheader)){
		resetinbuf(c);
		return false;
	}
	h = (struct messageheader *) (c->input_buffer + c->inbuf_head);
	if (c->inbuf_tail - c->inbuf_head  < (int) MESSAGESIZE(h)){
		resetinbuf(c);
		return false;
	}
	switch (h->type){
		case mt_addtrigger:
			addtrigger(c, (struct message_addtrigger *)(c->input_buffer + c->inbuf_head + sizeof(struct messageheader)));	
		break;
		case mt_deltrigger:
			deltrigger(c, (struct message_deltrigger *)(c->input_buffer + c->inbuf_head + sizeof(struct messageheader)));	
		break;
	        case mt_triggerquery:
               		gettrigger(c, (struct message_triggerquery *)(c->input_buffer + c->inbuf_head + sizeof(struct messageheader)));
	        break;
		case mt_bye:
	                printf("client: controller disconnected. I close the connection too.\n");
	                c->finish = true;
			disconnect_from_server(c);
		break;
		case mt_hello:
			LOG("ignore hello\n");
		break;
        	default:
        	        fprintf(stderr, "Message type %d not found\n", h->type);
	}
	c->inbuf_head += MESSAGESIZE(h);
	return true;
}

/* ------------------------------- Async --------------------------- */

void * client_mainloop(void * _){
	struct client * c = (struct client *) _;
	set_CPU(c->core);

	while (!c->finish){
		if (readamessage(c)){ //FIXME
			break;
		}
	}

	return NULL;
}

void * client_senderworker(void * _){
	struct client * c = (struct client *)_;
	set_CPU(c->core);
	void * obj_table [32];
	int i, n;
	struct message_triggersatisfaction * m;
	struct messageheader * h;
	char buffer [1024];
	const int message_size = sizeof(struct messageheader) + sizeof(struct message_triggersatisfaction);
	while (c->finish){
		char * buf = buffer;
		n = rte_ring_sc_dequeue_bulk(c->ring, obj_table, sizeof(buffer)/message_size);
		if (n < 0){
			fprintf(stderr, "client cannot read message from ring");
		}
		if (n > 0){
			for (i = 0; i < n; i++){
				h = (struct messageheader *)buf;
				m = (struct message_triggersatisfaction *)(buf + sizeof(struct messageheader));
				h->type = mt_triggersatisfaction;
				h->length = sizeof (struct message_triggersatisfaction);
				memcpy(m, obj_table[i], h->length);
				buf += message_size;
				if (c->delay > 10){ //AIMD
	                                c->delay /= 2;
        	                }
			}
			sendtocontrollerwait(c, message_size * n, buffer);
			rte_mempool_sp_put_bulk(c->mem, obj_table, n);
		}else{
			usleep(c->delay);
                        if (c->delay < 500){ //AIMD
                                c->delay += 10;
                        }
		}
		
	}	
	return NULL;
}

void client_sendtriggerasync(struct client * c, struct trigger * t, uint32_t time){
	if (c->onlysync){
		fprintf(stderr, "clinet: only sync operations are supported");
	}
	struct message_triggersatisfaction * m;
	int ret0 = rte_mempool_sc_get(c->mem, (void **) &m);
	if (ret0 != 0 ){
		fprintf(stderr, "client cannot get memory for a message\n");
		return;
	}
	m->time = time;
	m->eventid = t->eventid;

	bool ret = triggertable_getreport(c->fr->tt, t, m->buf, time);
	m->code = ret ? 0 : 1;
	int ret1 = rte_ring_sp_enqueue(c->ring, m);
	if (ret1 != 0){
		fprintf(stderr, "client ring is full\n");
		rte_mempool_sp_put(c->mem, m);
		return;
	}
}
