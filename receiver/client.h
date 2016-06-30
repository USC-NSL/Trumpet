#ifndef CLIENT_H
#define CLIENT_H 1
#include <rte_ring.h>
#include <pthread.h>
#include "stdbool.h"
#include "triggertable2.h"
#include "messages.h"

#define CLIENT_BUFSIZE 1500
#define REQUESTLIST_BUFSIZE 10

struct flatreport;

struct requestlist{
	uint32_t time;
	struct message_triggerquery buf[REQUESTLIST_BUFSIZE];
	uint16_t filled;
	struct requestlist * next;
};

struct client{
	struct flatreport * fr;
	struct requestlist * rl;
	struct requestlist * freerl;
	int rl_lastindex;
	int fd;
	uint16_t inbuf_tail;
	uint16_t inbuf_head;
	uint16_t outbuf_tail;
	uint16_t delay;
	uint8_t core;
	bool onlysync;
	bool finish;
	bool hasdatatoread;
	uint8_t readseqnum;
	pthread_t sender_thread;
	pthread_t receiver_thread;
	pthread_mutex_t sendsocket_mutex;
	struct rte_ring * ring;
	struct rte_mempool * mem;
	char input_buffer[CLIENT_BUFSIZE];
	char output_buffer[CLIENT_BUFSIZE];
};

struct client * client_init(char * ip, uint16_t port, uint8_t core, bool onlysync);
void client_finish(struct client * c);
void client_sendtriggersync(struct client * c, struct trigger *t, uint32_t time, bool satisfaction_or_query);
void client_sendtriggerasync(struct client * c, struct trigger * t, uint32_t time);

void client_test(struct client * c);
void client_readsync(struct client * c, uint64_t timebudget, uint64_t start);
void client_hello(struct client * c, uint32_t id, uint32_t time);
void client_waitforhello(struct client * c);
void client_sendsatisfactionsync(struct client * c, struct trigger *t, uint32_t time);
#endif /* client.c */
