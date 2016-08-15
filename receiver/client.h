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

/*
* Keeps a batch of unanswered polls (polls for an epoch that came before the epoch finishes because of synchronization issues across end-hosts)
* Each requestlist has a time (epoch number), and they are stored in a sorted linked list
*/
struct requestlist{
	uint32_t time;
	struct message_triggerquery buf[REQUESTLIST_BUFSIZE];
	uint16_t filled;
	struct requestlist * next;
};

/*
* This struct handles the communication with the controller.
* It runs in synchronized mode that handles controller commands (read/write) by reading/writing 
* a buffer using unblocking system calls given some free CPU cycles and a time-budget
*/
struct client{
	struct flatreport * fr;
	struct requestlist * rl;
	struct requestlist * freerl;
	int rl_lastindex;
	int fd;
	uint16_t inbuf_tail;
	uint16_t inbuf_head;
	uint16_t outbuf_tail;
	bool finish;
	bool hasdatatoread;
	uint8_t readseqnum; // for debug message to optimize poll time & to know if client actually checked the input buffer
	char input_buffer[CLIENT_BUFSIZE]; // buffer for messages from the controller
	char output_buffer[CLIENT_BUFSIZE];// buffer for messages to the controller
};
/*
* instantiates the client object and connects to the server. 
*/
struct client * client_init(char * ip, uint16_t port);

void client_finish(struct client * c);

/*
* creates the message in a buffer to be sent later if client_readsync got free cycles
*/
void client_sendtriggersync(struct client * c, struct trigger *t, uint32_t time, bool satisfaction_or_query);

/*
* if onlysync is false, creates a message out for saatissfaction of a trigger for the controller and schedules that to be sent.
*/
void client_sendtriggerasync(struct client * c, struct trigger * t, uint32_t time);

void client_test(struct client * c);

/*
* The main method that tells the client to take the CPU core to send/receive messages to/from the controller.
*/
void client_readsync(struct client * c, uint64_t timebudget, uint64_t start);

/*
* Sends hello message to the controller and blocks until it is sent
*/
void client_hello(struct client * c, uint32_t id, uint32_t time);

/*
* waits until it receives a hello reply from the controller. Note that the client should have said hello first
*/
void client_waitforhello(struct client * c);

/*
* sends satisfaction of a trigger to the controller and waits until it is sent (no buffer or wait till readsync is called during free cycles)
*/
void client_sendsatisfactionsync(struct client * c, struct trigger *t, uint32_t time);
#endif /* client.c */
