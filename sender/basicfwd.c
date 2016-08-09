#include <stdint.h>
#include <signal.h>
#include <inttypes.h>
#include <stdio.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <sys/time.h>
#include <unistd.h>
#include <rte_malloc.h>
#include <math.h>
#include <pthread.h>
#include "stdbool.h"
#include <netinet/in.h>
#include <arpa/inet.h>

#define RX_RING_SIZE 256
#define TX_RING_SIZE 512

#define NUM_MBUFS 8192
#define MBUF_SIZE (1600 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_CACHE_SIZE 0
#define BURST_SIZE 128
#define SEQPERFLOW  1024
#define MAXRATE 14880000 
#define RANDSEQNUM 1024
#define MACSFILENAME "dstmac.txt"

#define MULTIPORT 1

int initsenderthreads(int ports);

struct randomgenerator{
	double * w;
	uint32_t * nums;
	uint16_t num;
};

struct randomgenerator * randomgenerator_init(void);
void randomgenerator_finish(struct randomgenerator * rg);
uint32_t randomgenerator_get (struct randomgenerator * rg);

struct packettemplate{
	int len;
	char  data[1514];
};

struct flow{
	uint32_t ip;
	uint32_t ip2;
	uint32_t target;
	uint32_t * seq;
	uint32_t lifetime;
	uint32_t num;
	struct packettemplate * pkt;
};

struct packetgenerator;

typedef void (*pg_finish_func)(struct packetgenerator * pg);
typedef void (*pg_finishflow_func)(struct packetgenerator * pg, struct flow * f);
typedef struct flow * (*pg_getflow_func)(struct packetgenerator * pg);

struct senderthread;

struct packetgenerator{
	void * moredata;
	pg_getflow_func getflow;
	pg_finish_func finish;
	struct senderthread * st;
	uint32_t num;
	uint32_t pkts_num;
	uint32_t stat_flownum;
};

struct advancedpg{
	struct packetgenerator pg;
	uint32_t num;
	uint32_t head;
	uint32_t index;
	uint32_t tail;
	struct flow * buffer;
	uint32_t nextarrival;
	uint32_t arrivaldelay;
	uint32_t initactiveflows;
	bool pktorlifetime;
};

struct fixedactivepg{
	struct packetgenerator pg;
	uint32_t num;
	uint32_t index;
	struct flow * buffer;
	uint64_t * ippairs;
	uint32_t ippairsnum;
	uint32_t ippairsindex;
	uint16_t loop;
};

struct simplepg{
	struct packetgenerator pg;
	int num;
	uint32_t index;
	struct flow * buffer;
};

struct ddospg{
	struct packetgenerator pg;
	int num;
	uint32_t index;
	struct flow * buffer;
};

struct senderthread{
	int port;
	int queue;
	struct packetgenerator * pg;
	struct packetgenerator * pg2;
	uint8_t bufpkt[60];
	uint64_t start_tsc;
	uint32_t pkts_num;
	uint32_t start;
	uint32_t start2;
	uint32_t iprange;
	void * next;
	uint32_t stat_activeflowsnum;
	uint32_t stat_activeflows;
	uint8_t id;
	bool finish;
};

struct global_args{
	struct senderthread * sth;
	int rate;
	struct rte_mempool *mbuf_pool;
	uint32_t target;
	uint32_t g_smallburst;
	uint32_t * randseq;
	uint32_t ddoslen;
	uint32_t ddosrandpercent;
	uint64_t activeflows;
	uint32_t arrivalrate;
	uint32_t iprange;
	uint16_t pg1size;
	uint16_t startshift;
	struct ether_addr ** dstmacs;
	pthread_mutex_t stat_mutex;
};

static struct global_args g;

static const struct rte_eth_conf port_conf_default = {
	.rxmode = { .max_rx_pkt_len = ETHER_MAX_LEN },
	.txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
};

static const uint8_t udp_dataoffset =  sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct udp_hdr);
static const uint8_t tcp_dataoffset =  sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr);
static double lograndmax;
static double lograndmax;

static __attribute__((unused)) uint32_t expon(double x){
	uint32_t z;                     // Uniform random number (0 < z < 1)
	do{
		z = rand();
	}while ((z == 0) || (z == RAND_MAX));
	return (uint32_t) (-x * (log(z) - lograndmax));
}


static inline  void change1(uint32_t * ip, uint32_t start, uint32_t end){
//	return;
//	struct ipv4_hdr * iph = buf + sizeof(struct ether_hdr);
	*ip = ntohl(*ip) + 1;
	if (*ip > end){
		*ip = htonl(start);
	}else{
		*ip = htonl(*ip);
	}
}

/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static int port_init(uint8_t port, struct rte_mempool *mbuf_pool, int nb_cores __attribute__((unused))){
	struct rte_eth_conf port_conf = port_conf_default;
	const uint16_t rx_rings = 1;
#if MULTIPORT
	const uint16_t tx_rings = 1;
#else
	const uint16_t tx_rings = nb_cores;
#endif
	int retval;
	uint16_t q;

	if (port >= rte_eth_dev_count())
		return -1;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, RX_RING_SIZE,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, TX_RING_SIZE,
				rte_eth_dev_socket_id(port), NULL);
		if (retval < 0)
			return retval;
	}

	/* Start the Ethernet port. */
	retval = rte_eth_dev_start(port);
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct ether_addr addr;
	rte_eth_macaddr_get(port, &addr);
	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			(unsigned)port,
			addr.addr_bytes[0], addr.addr_bytes[1],
			addr.addr_bytes[2], addr.addr_bytes[3],
			addr.addr_bytes[4], addr.addr_bytes[5]);

	/* Enable RX in promiscuous mode for the Ethernet device. */
//	rte_eth_promiscuous_enable(port);
	 struct rte_eth_link  link;
        do{
                rte_eth_link_get(port, &link);
        }while(link.link_status == 0);
	rte_delay_ms(4000);
	return 0;
}

static inline void append(void * buf, unsigned len, struct rte_mbuf*pkt){
        uint8_t * d = (uint8_t * )rte_pktmbuf_append(pkt, len);
        rte_memcpy(d, buf, len);
}

static __attribute__((unused)) void shuffle(void *array, size_t n, size_t size) {
    char tmp[size];
    char *arr = array;
    size_t stride = size * sizeof(char);
    if (n > 1) {
        size_t i;
        for (i = 0; i < n - 1; ++i) {
            size_t rnd = (size_t) rand();
            size_t j = i + rnd / (RAND_MAX / (n - i) + 1);

            memcpy(tmp, arr + j * stride, size);
            memcpy(arr + j * stride, arr + i * stride, size);
            memcpy(arr + i * stride, tmp, size);
        }
    }
}

static inline int advancedpg_flowfinished(struct advancedpg * pg2, struct flow * f){
	if (pg2->pktorlifetime){
		return f->num >= f->target;
	}
	return pg2->pg.pkts_num >= f->target;	
}

static inline uint32_t advancedpg_gettarget(struct advancedpg * pg2, struct flow * f){
	if (pg2->pktorlifetime){
		return f->lifetime;
	}
	return pg2->pg.pkts_num + f->lifetime;
}


static void advancedpg_movetohead (struct packetgenerator * pg , struct flow * f2 __attribute__((unused))){
	struct advancedpg * pg2 = (struct advancedpg *) pg->moredata;
//	printf("2,%d,%d\n", pg2->index, g.pkts_num);
	//	printf("1->advance head %d %d\n", pg2->head, pg2->tail);
	if (pg2->index != pg2->head){
		return;
	}
	struct flow * f;
	if (pg2->tail > pg2->head){
		for (f = pg2->buffer + pg2->head; advancedpg_flowfinished(pg2, f) &&  pg2->head < pg2->tail; f++){
			f->num = 0;
			pg2->head++;
		}
	}else{
		for (f = pg2->buffer + pg2->head; advancedpg_flowfinished(pg2, f) &&  pg2->head < pg2->num; f++){
			f->num = 0;
			pg2->head++;
		}
		if (pg2->head == pg2->num){
//			printf("%"PRIu32" rotate head\n", g.pkts_num);
			pg2->head = 0;
			for (f = pg2->buffer + pg2->head; advancedpg_flowfinished(pg2,f) &&  pg2->head < pg2->tail; f++){
				f->num = 0;
				pg2->head++;
			}
		}else{
			pg2->index = pg2->head; 
			//this is special case that head>=tail is ok
			return;
		}

	}
	if (unlikely(pg2->head >= pg2->tail)){
		printf("head %d reached tail %d at %"PRIu32"! next arrival %"PRIu32" ", pg2->head, pg2->tail, pg2->pg.pkts_num, pg2->nextarrival );
		if (pg2->head == 0){
			pg2->head = pg2->num - 1;
		}else{
			pg2->head --;
		}
		printf("keep sending %d\n", pg2->head);
	}
//	printf("1->advance head %d %d %"PRIu32"\n", pg2->tail, pg2->tail, g.pkts_num);
	pg2->index = pg2->head; 
}

static __attribute__((unused)) uint32_t advancedpg_getactiveflows (struct advancedpg * pg2){
	struct flow * f;
	uint32_t index = pg2->head;
	uint32_t activeflows = 0;
	if (pg2->tail > pg2->head){
	 	for (f = pg2->buffer + index; index < pg2->tail; f++, index++){
			if (!advancedpg_flowfinished(pg2, f)) activeflows++;
		}
	}else if (index < pg2->head){
	 	for (f = pg2->buffer + index; index < pg2->tail; f++, index++){
			if (!advancedpg_flowfinished(pg2, f)) activeflows++;
		}
	}else{
	 	for (f = pg2->buffer + pg2->index; index < pg2->num; f++, index++){
			if (!advancedpg_flowfinished(pg2, f)) activeflows++;
		}
		if (index == pg2->num){
			index = 0;
		 	for (f = pg2->buffer + index; index < pg2->tail; f++, index++){
				if (!advancedpg_flowfinished(pg2, f)) activeflows++;
			}
		}
	}
	return activeflows;
}

static struct flow * advancedpg_getflow (struct packetgenerator * pg){
	struct flow * f;
	struct advancedpg * pg2 = (struct advancedpg *) pg->moredata;

	//flow arrival
	for (; pg2->pg.pkts_num >= pg2->nextarrival; ){
		pg->stat_flownum++;
//		printf("advance tail %d %d %"PRIu32"\n",pg2->head, pg2->tail, pg->st->pkts_num);
//		printf("1,%d,%d\n", pg2->tail, g.pkts_num);
		if (pg2->tail < pg2->num){
			if (pg2->tail + 1 != pg2->head){
				f = pg2->buffer + pg2->tail;
				f->target = advancedpg_gettarget(pg2, f); 
//				f->ip2 = htonl(ntohl(f->ip2)+1);
				pg2->tail++;
			}else{
				printf("tail %d reached head %d at %"PRIu32", skip flow add\n", pg2->tail, pg2->head, pg2->pg.pkts_num);
			}
		}else{
			if (pg2->head > 0){
				f = pg2->buffer;
				f->target = advancedpg_gettarget(pg2, f); 
				f->ip2 = htonl(ntohl(f->ip2)+1);
				pg2->tail = 1;
//				printf("%"PRIu32" rotate tail\n", g.pkts_num);
			}else{
				printf("tail %d reached head %d at %"PRIu32", skip flow add\n", pg2->tail, pg2->head, pg2->pg.pkts_num);
			}
		}
		if (pg2->initactiveflows > 0){
			pg2->initactiveflows --;
		}else{
			pg2->nextarrival += expon(pg2->arrivaldelay);
		}
/*		uint32_t x = advancedpg_getactiveflows(pg2);
		pg->st>stat_activeflows+=x;	
		pg->st->stat_activeflows_num++;*/
		//printf("%"PRIu32", active %d hn %d head %d tail %d num %d\n",g.pkts_num, x, (pg2->buffer+pg2->head)->num, pg2->head, pg2->tail, g.flownum);
	}


	
	//pick a flow
	pg2->index++;
	if (pg2->tail > pg2->head){
	 	for (f = pg2->buffer + pg2->index; advancedpg_flowfinished(pg2, f) &&  pg2->index < pg2->tail; f++){
			pg2->index++;
		}
	}else if (pg2->index < pg2->head){
		for (f = pg2->buffer + pg2->index; advancedpg_flowfinished(pg2, f) &&  pg2->index < pg2->tail; f++){
			pg2->index++;
		}
	}else{
	 	for (f = pg2->buffer + pg2->index; advancedpg_flowfinished(pg2, f) &&  pg2->index < pg2->num; f++){
			pg2->index++;
		}
		if (pg2->index == pg2->num){
			pg2->index = 0;
		 	for (f = pg2->buffer + pg2->index; advancedpg_flowfinished(pg2,f) &&  pg2->index < pg2->tail; f++){
				pg2->index++;
			}
		}
	}

	if (unlikely(pg2->index == pg2->tail)){
		pg2->index = pg2->head; //assumes head is good
		f = pg2->buffer +pg2->index;
		if (advancedpg_flowfinished(pg2, f)){
			advancedpg_movetohead(pg, f);
		}
	}
	return  pg2->buffer + pg2->index;
}

static void advancedpg_finish(struct packetgenerator * pg){
	struct advancedpg * pg2 = (struct advancedpg *) pg->moredata;
	rte_free(pg2->buffer);
	free(pg2);
}

static int binarysearch(double * array, int n, double search){
   int first, last, middle;
   first = 0;
   last = n - 1;
   middle = (first+last)/2;

   while (first <= last) {
      if (array[middle] == search) {
	 return middle;
      }
      if (middle < n-1 && array[middle] <= search && array[middle+1]>search){
	return middle;
      }
      if (array[middle] < search){
         first = middle + 1;    
      }else{
         last = middle - 1;
	}
 
      middle = (first + last)/2;
   }
   return -1;
}

struct randomgenerator * randomgenerator_init(void){
	struct randomgenerator * rg = malloc(sizeof(struct randomgenerator));
	rg->num = 51;
	rg->nums = malloc (sizeof(uint32_t) * rg->num);
	rg->w = malloc (sizeof(double) * rg->num);
	uint32_t nums[] = {100,121,146,176,212,256,309,373,450,543,655,791,954,1151,1389,1677,2024,2442,2947,3556,4292,5179,6251,7543,9103,10985,13257,15999,19307,23300,28118,33932,40949,49417,59636,71969,86851,104811,126486,152642,184207,222300,268270,323746,390694,471487,568987,686649,828643,1000000, 0};
	double w[] = {0,5,21,20,19,18,17,15,15,14,13,12,11,11,10,9,9,8,8,7,7,6,6,6,5,5,5,4,4,4,4,3,3,3,3,3,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1}; //add 0 in the beginning
	int num = sizeof(w)/sizeof(w[0]);

	//make commulative weights
	int i; 
	double sum = 0;
	for (i = 0; i < num; i++){
		sum+=w[i];
		rg->w[i]=sum;
		rg->num = nums[i];
	}
	return rg;
}

void randomgenerator_finish(struct randomgenerator * rg){
	free(rg->nums);
	free(rg->w);
	free(rg);
}

uint32_t randomgenerator_get (struct randomgenerator * rg){
	uint32_t x2;
	double x;
	do {x2=rand();}while (x2==RAND_MAX);
	x = (double)x2*rg->w[rg->num-1]/RAND_MAX;
	int index = binarysearch(rg->w, rg->num, x);
//		printf("%f, %d, %f, %f, %d\n", x, index, w[index], w[index+1], nums[index-1]);
	return rg->nums[index];
}
/**
* if pktorlifetime is true (pkt duration) then set the pktperflow. This will ignore arrivalrate
*/
static __attribute__((unused)) struct packetgenerator *  advancedpg_init(struct senderthread * st, uint16_t targetactiveflows, bool pktorlifetime, int pktperflow, struct packettemplate * pkt, uint32_t start, uint32_t start2, uint32_t iprange){
	struct advancedpg * pg2 = malloc (sizeof (struct advancedpg));
	pg2->pg.moredata = pg2;
	pg2->pg.st = st;
	pg2->pg.finish = advancedpg_finish;
	pg2->pg.getflow = advancedpg_getflow;
	pg2->pg.stat_flownum = 0;
	pg2->pg.pkts_num = 0;
	pg2->head = 0;	
	pg2->tail = 0;
	pg2->index = pg2->head - 1;
	pg2->num = iprange;
	pg2->nextarrival = 0;
	pg2->arrivaldelay = g.rate / g.arrivalrate; //1000 per second
	if (pg2->arrivaldelay == 0){
		fprintf(stderr, "arrivaldelay is 0=%d/%d \n", g.rate, g.arrivalrate);
		exit(1);
	}
//	const uint16_t targetactiveflows = 300;
	uint32_t flowpktduration; 
	if (pktorlifetime){
		pg2->initactiveflows = targetactiveflows;
		flowpktduration = pktperflow;
	}else{
		pg2->initactiveflows = 0;
		flowpktduration = targetactiveflows * pg2->arrivaldelay; //g.rate;
	}
	lograndmax = log(RAND_MAX);
	
	pg2->buffer = rte_malloc("advancedflows", pg2->num * sizeof(struct flow), 0);
//	uint32_t start = (((((10<<8)+0)<<8)+4)<<8)+0;
//	uint32_t start2 = (((((10<<8)+0)<<8)+5)<<8)+4;
	uint32_t i;
	struct flow * f;

	for (i = 0, f = pg2->buffer; i < pg2->num; i++, f++){
		f->ip = htonl(start++);
		f->ip2 = htonl(start2);
		f->num = 0;
		f->lifetime = flowpktduration; //(uint32_t)(rand()/(RAND_MAX/(1.1 * flowpktduration))); //Uniform distribution
		f->seq = g.randseq + SEQPERFLOW * (i % RANDSEQNUM);
		f->target = 0;
		f->pkt = pkt;
	}
	shuffle(pg2->buffer, pg2->num, sizeof(struct flow));
	return &pg2->pg;
}

static struct flow * fixedactivepg_getflow (struct packetgenerator * pg){
	struct fixedactivepg * pg2 = (struct fixedactivepg *) pg->moredata;
	pg2->index = (pg2->index + 1) % pg2->num;
	struct flow * f = pg2->buffer + pg2->index;
	if (f->num >= f->target){
		//printf("new flow %d %d %d %d\n", pg2->index, g.pkts_num, f->num, f->target);
		f->num = 0;
		uint64_t x = pg2->ippairs[pg2->ippairsindex];
		f->ip = x >> 32;
		f->ip2 = (uint32_t) x;
		f->ip2 = htonl(ntohl(f->ip2)+pg2->loop);
/*struct in_addr srcip;
  struct in_addr dstip;
  srcip.s_addr = f->ip2;
  dstip.s_addr = f->ip;
 printf("%s, ", inet_ntoa(srcip));
  printf("%s ", inet_ntoa(dstip));
  printf("%d %p\n", pg2->ippairsindex, f);*/

		pg2->ippairsindex ++;
		if (pg2->ippairsindex >= pg2->ippairsnum){
			pg2->ippairsindex = 0;
			pg2->loop ++;
		}
		pg->stat_flownum++;
	}
	return f;
}

static void fixedactivepg_finish(struct packetgenerator * pg){
	struct fixedactivepg * pg2 = (struct fixedactivepg *) pg->moredata;
	rte_free(pg2->buffer);
	rte_free(pg2->ippairs);
	free(pg2);
}

static __attribute__((unused)) struct packetgenerator * fixedactivepg_init(struct senderthread * st, uint16_t activeflows, uint32_t iprange1, uint32_t iprange2, uint16_t pktperflow, struct packettemplate * pkt, uint32_t start, uint32_t start2 ){
	if (iprange1 * iprange2 < activeflows){
		fprintf(stderr, "fixedactivepg: too many active flows \n");
		return NULL;
	}
	struct fixedactivepg * pg2 = malloc (sizeof (struct fixedactivepg));
	pg2->pg.moredata = pg2;
	pg2->pg.st = st;
	pg2->pg.finish = fixedactivepg_finish;
	pg2->pg.getflow = fixedactivepg_getflow;	
	pg2->pg.stat_flownum = 0;
	pg2->pg.pkts_num = 0;
	pg2->num = activeflows;
	pg2->buffer = rte_malloc("fixedactiveflows", pg2->num * sizeof(struct flow), 0);		
	pg2->index = 0;
	pg2->ippairsnum = iprange1 * iprange2;
	pg2->ippairsindex = 0;
	pg2->loop = 0;

	
	pg2->ippairs = rte_malloc("ippairs", pg2->ippairsnum * sizeof(uint64_t), 0);
	uint32_t i, j;
	uint64_t * ippairs = pg2->ippairs;
	for (i = 0; i < iprange2; i++, start2++){
		uint32_t s = start;
		for (j = 0; j < iprange1; j++, s++, ippairs++){
			*ippairs = ((uint64_t)htonl(s)<<32)+htonl(start2);
		}
	}

	shuffle(pg2->ippairs, pg2->ippairsnum, sizeof(uint64_t));

	struct flow * f;
	for (i = 0, f = pg2->buffer; i < pg2->num; i++, f++){
		ippairs = pg2->ippairs + pg2->ippairsindex;
		f->ip = (*ippairs)>>32;
		f->ip2 = (uint32_t)*ippairs;
		f->ip2 = htonl(ntohl(f->ip2)+pg2->loop);
		pg2->ippairsindex ++;
		if (pg2->ippairsindex > pg2->ippairsnum){
			pg2->ippairsindex = 0;
			pg2->loop++;
		}
		f->num = 0;
		f->lifetime = g.target;
		f->seq = g.randseq + SEQPERFLOW * (i%RANDSEQNUM);
		f->target = pktperflow;
		f->pkt = pkt;
	}
	pg2->pg.stat_flownum += pg2->num;
	return &pg2->pg;
}


static struct flow * simplepg_getflow (struct packetgenerator * pg){
	struct simplepg * pg2 = (struct simplepg *) pg->moredata;
	pg2->index = (pg2->index + 1) % pg2->num;
	struct flow * f = pg2->buffer + pg2->index;
	if (f->num >= f->target){
		//printf("new flow %d %d %d %d\n", pg2->index, g.pkts_num, f->num, f->target);
		f->num = 0;
		f->ip2 = htonl(ntohl(f->ip2)+1);
		pg->stat_flownum++;
	}
	return f;
}

static void simplepg_finish(struct packetgenerator * pg){
	struct simplepg * pg2 = (struct simplepg *) pg->moredata;
	rte_free(pg2->buffer);
	free(pg2);
}

static __attribute__((unused)) struct packetgenerator * simplepg_init(struct senderthread * st, uint16_t activeflows, uint16_t pktperflow, uint32_t start, uint32_t start2, struct packettemplate * pkt){
	struct simplepg * pg2 = malloc (sizeof (struct simplepg));
	pg2->pg.moredata = pg2;
	pg2->pg.st = st;
	pg2->pg.finish = simplepg_finish;
	pg2->pg.getflow = simplepg_getflow;	
	pg2->pg.pkts_num = 0;
	pg2->pg.stat_flownum = 0;
	pg2->num = activeflows;
	pg2->buffer = rte_malloc("simpleflows", pg2->num * sizeof(struct flow), 0);		
	pg2->index = 0;

//	uint32_t start = (((((10<<8)+0)<<8)+4)<<8)+0;	
//        uint32_t start2 = (((((10<<8)+0)<<8)+5)<<8)+0;
	
	uint16_t i;
	struct flow * f;
	for (i = 0, f = pg2->buffer; i < pg2->num; i++, f++){
		f->ip = htonl(start++);
		f->ip2 = htonl(start2);
		f->num = 0;
		f->lifetime = g.target;
		f->seq = g.randseq + SEQPERFLOW * (i%RANDSEQNUM);
		f->target = pktperflow;
		f->pkt = pkt;
	}
	pg2->pg.stat_flownum = pg2->num;
	return &pg2->pg;
}

static struct flow * ddospg_getflow (struct packetgenerator * pg){
        struct ddospg * pg2 = (struct ddospg *) pg->moredata;
	struct flow * lastflow = pg2->buffer + pg2->index;
	if (lastflow != NULL && lastflow->num >= lastflow->lifetime){
		lastflow->ip2 = htonl(ntohl(lastflow->ip2) + pg2->num);
		pg->stat_flownum++;
		lastflow->num = 0;
	}
        pg2->index = (pg2->index + 1) % pg2->num;
        return pg2->buffer + pg2->index;
}

static void ddospg_finish(struct packetgenerator * pg){
        struct ddospg * pg2 = (struct ddospg *) pg->moredata;
        rte_free(pg2->buffer);
        free(pg2);
}

static __attribute__((unused)) struct packetgenerator * ddospg_init(struct senderthread * st, uint32_t start, struct packettemplate * pkt){
        struct ddospg * pg2 = malloc (sizeof (struct ddospg));
        pg2->pg.moredata = pg2;
	pg2->pg.st = st;
        pg2->pg.finish = ddospg_finish;
        pg2->pg.getflow = ddospg_getflow;
	pg2->pg.stat_flownum = 0;
	pg2->pg.pkts_num = 0;
        pg2->num = 1;
        pg2->buffer = rte_malloc("ddos", pg2->num * sizeof(struct flow), 0);
        pg2->index = 0;

//        uint32_t start = (((((192<<8)+0)<<8)+4)<<8)+0;
        //uint32_t start2 = (((((12<<8)+0)<<8)+5)<<8)+4;

        uint16_t i;
        struct flow * f;
        for (i = 0, f = pg2->buffer; i < pg2->num; i++, f++){
                f->ip = htonl(start++);
                f->num = 0;
                f->lifetime = g.ddoslen;
                f->seq = g.randseq + SEQPERFLOW * (i%RANDSEQNUM);
                f->target = 0;
		f->pkt = pkt;
		pg2->pg.stat_flownum++;
        }
        return &pg2->pg;
}

static void init_packet(struct senderthread * st){
	uint8_t b [60] = {
0x68, 0x05,  0xca, 0x1e,  0xa5, 0xe4,  0x68, 0x05,  0xca, 0x2a,  0x95, 0x62,  0x08, 0x00,  0x45, 0x00, 
//0x68, 0x05,  0xca, 0x1e,  0xa5, 0xd0,  0x68, 0x05,  0xca, 0x2a,  0x95, 0x63,  0x08, 0x00,  0x45, 0x00, 
0x00, 0x20,  0x8d, 0x4a,  0x40, 0x00,  0x40, 0x11,  0x8f, 0x7c,  0x0a, 0x00,  0x05, 0x04,  0x0a, 0x00, 
0x05, 0x03,  0xe4, 0x91,  0x09, 0xc4,  0x00, 0x0c,  0x8f, 0x3d,  //start data here
0x00, 0x00,  0x00, 0x00,  0x00, 0x00, 
 0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00,  0x00, 0x00};
	memcpy(st->bufpkt, b, 60);
	//st->srcip = &(((struct ipv4_hdr * )((uintptr_t)st->bufpkt + sizeof(struct ether_hdr)))->src_addr);
	//st->dstip = &(((struct ipv4_hdr * )((uintptr_t)st->bufpkt + sizeof(struct ether_hdr)))->dst_addr);
   struct ether_addr addr;
   rte_eth_macaddr_get(st->port, &addr);
   struct ether_hdr * e = (struct ether_hdr *)st->bufpkt;
   int i;
   for (i = 0; i < 6; i++){
       e->d_addr.addr_bytes[i] = g.dstmacs[st->id]->addr_bytes[i];
       e->s_addr.addr_bytes[i] = addr.addr_bytes[i];
   }
   
}

//you must free the memory yourself
static __attribute__((unused)) uint8_t * readhexfile(const char * filename){
   FILE * file = fopen(filename, "r");
   int size = 0;
   unsigned char val;
   int startpos = ftell(file);
   while (fscanf(file, "%hhx, ", &val) == 1){
      ++size;
   }
   uint8_t * retval = (uint8_t *) malloc(size);
   fseek(file, startpos, SEEK_SET); //if the file was not on the beginning when we started
   int pos = 0;
   while (fscanf(file, "%hhx, ", &val) == 1){
      retval[pos++] = (uint8_t) val;
   }
   fclose(file);
   return retval;
}

static void printstats(struct senderthread * st){	
	struct rte_eth_stats stats;
	uint64_t core_cycles = (rte_rdtsc() - st->start_tsc);
	pthread_mutex_lock(&g.stat_mutex);
	printf("pktnum %"PRIu32 " cycles %" PRIu64 " rate %f avgactiveflows %f flownum %d flownum2 %d\n", st->pkts_num, core_cycles, 1.0*rte_get_tsc_hz()*st->pkts_num/core_cycles, 1.0*st->stat_activeflows/st->stat_activeflowsnum, st->pg->stat_flownum, st->pg2->stat_flownum);
    rte_eth_stats_get(st->port, &stats);

    double rx_loss = 100 * ((double)stats.ierrors /
            (double)(stats.ierrors+stats.ipackets+1));
    double tx_loss = 100 * ((double)stats.oerrors/
            (double)(stats.oerrors+stats.opackets+1));

	printf(
         "-----------------------------------------------------------\n"
         "| Port-%d\n"
         "-----------------------------------------------------------\n"
         "|   RX Packets       : %" PRIu64 "/%" PRIu64 " (%" PRIu64 " bytes)\n"
         "|      RX Packet Loss: %.2f %%\n"
         "|      RX Rate       : %.2f\n"
         "-----------------------------------------------------------\n"
         "|   TX Packets       : %" PRIu64 "/%" PRIu64 " (%" PRIu64 " bytes)\n"
         "|      TX Packet Loss: %.2f %%\n"
         "|      TX Rate       : %.2f\n"
         "-----------------------------------------------------------\n",
         st->port,
         stats.ipackets, stats.ierrors, stats.ibytes,
         rx_loss, 0.0,
         stats.opackets, stats.oerrors, stats.obytes,
         tx_loss, 0.0
     );
	pthread_mutex_unlock(&g.stat_mutex);
}

static void sendpackets(struct senderthread * st){
	uint32_t i;
	int r;
	struct rte_mbuf * m;
	struct rte_mbuf * bufs[BURST_SIZE];
	static double cycles_per_packet;
	static uint64_t last_clock; 
	struct flow * f;
	if (g.rate < MAXRATE){
		cycles_per_packet = 0.998 * BURST_SIZE * rte_get_tsc_hz() / g.rate ;  
	}else{
		cycles_per_packet = 0;
	}
	f = NULL;
	last_clock = rte_rdtsc();
	struct packetgenerator * lastpg = NULL;
	uint16_t offset = 0;
	struct packettemplate * pkt = NULL;
	for (st->pkts_num = 0; st->pkts_num < g.target && !st->finish;) {
		/* Get burst of RX packets, from first port of pair. */
		r = rte_mempool_sc_get_bulk(g.mbuf_pool, (void**)&bufs, BURST_SIZE);
		if (r != 0){
			printf("WARNING, mempool is full\n");
			continue;
		}
		for (i = 0; i < BURST_SIZE; i++, st->pkts_num++){
			m = bufs[i];
			if (st->pkts_num % g.g_smallburst == 0){
				if (g.randseq[st->pkts_num % (SEQPERFLOW * RANDSEQNUM)] < g.ddosrandpercent){
					lastpg = st->pg2;
				}else{
					lastpg = st->pg;
				}				
				f = lastpg->getflow(lastpg);
				pkt = f->pkt;
			}
			lastpg->pkts_num++;

			m->data_len = pkt->len;
			m->pkt_len  = m->data_len;
			rte_memcpy((uint8_t *)m->buf_addr + m->data_off, pkt->data, m->data_len);
			struct ipv4_hdr * ip_hdr = (struct ipv4_hdr * )((uintptr_t)m->buf_addr + m->data_off + sizeof(struct ether_hdr));  	
   			ip_hdr->dst_addr = f->ip;
   			ip_hdr->src_addr = f->ip2;
			
			offset = udp_dataoffset;

			//set seq number inside the packet

			uint32_t * data = (uint32_t *)(rte_ctrlmbuf_data(m) + offset);
			*data = st->pkts_num;	
			*(data + 1) = f->seq[f->num % (SEQPERFLOW)];
			//rte_pktmbuf_dump(stdout, m, 0);
			f->num++;
		}

		for (i = 0; i < BURST_SIZE;){
			i += rte_eth_tx_burst(st->port, st->queue, bufs + i, BURST_SIZE - i);
		}
		rte_mempool_put_bulk(g.mbuf_pool, (void**)&bufs, BURST_SIZE);
		if (g.rate < MAXRATE){
			while ((rte_rdtsc() - last_clock) < cycles_per_packet);
			last_clock = rte_rdtsc();
		}
	}
}

static void initstats(struct senderthread * st){
	rte_eth_stats_reset(st->port);
	st->start_tsc = rte_rdtsc();
	st->stat_activeflows = 0;
	st->stat_activeflowsnum = 0;
}

static void packettemplate_init(struct packettemplate * pt, int size, char * template){
	if (size <= 60){
		size = 60;
	}
	if (size > 1514){
		size = 1514;
	}
	pt->len = size;
	memset(pt->data, 0, 1514);
	memcpy(pt->data, template, 60);
   	((struct ipv4_hdr * )((uintptr_t)pt->data + sizeof(struct ether_hdr)))->total_length = htons(size-14);
}

static int lcore_main(void *_) {
	struct senderthread * st = (struct senderthread *) _;

	/* Display the port MAC address. */
	struct ether_addr addr0;
	struct ether_addr * addr = &addr0;
	rte_eth_macaddr_get(st->port, addr);
	printf("sender thread %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " --> ",
			st->id,
			addr->addr_bytes[0], addr->addr_bytes[1],
			addr->addr_bytes[2], addr->addr_bytes[3],
			addr->addr_bytes[4], addr->addr_bytes[5]);

	/* Display the port MAC address. */
	addr = g.dstmacs[st->id];
	printf("MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			addr->addr_bytes[0], addr->addr_bytes[1],
			addr->addr_bytes[2], addr->addr_bytes[3],
			addr->addr_bytes[4], addr->addr_bytes[5]);


	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	if (rte_eth_dev_socket_id(st->port) > 0 &&
			rte_eth_dev_socket_id(st->port) !=
					(int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", st->port);
	printf("\nCore %u sending packets. [Ctrl+C to quit]\n", rte_lcore_id());
	init_packet(st);
        //init_packet1500(st);

	uint32_t start, start2;
	bool matching = true;
	if (matching){
		start = st->start;	
        	start2 = st->start2;
	}else{
		start = (((((120<<8)+0)<<8)+4)<<8)+0;	
		start2 = (((((128<<8)+0)<<8)+5)<<8)+4;
	}

	struct packettemplate pg1pkt;
	packettemplate_init(&pg1pkt, g.pg1size, (char*) st->bufpkt);



	st->pg = //ddospg_init(st);
//		simplepg_init(st,g.activeflows, 10);
		advancedpg_init(st, g.activeflows, false, 10, &pg1pkt , start, start2, st->iprange);
		//fixedactivepg_init(st, g.activeflows, st->iprange, 1, 10, false, start, start2);

	matching = false;
	if (matching){
		start = st->start;	
        	start2 = st->start2;
	}else{
		start = (((((120<<8)+0)<<8)+4)<<8)+0;	
		start2 = (((((128<<8)+0)<<8)+5)<<8)+4;
	}


	struct packettemplate pg2pkt;
	packettemplate_init(&pg2pkt, 60, (char*)st->bufpkt);
	st->pg2 = 
		fixedactivepg_init(st, 2, 1<<16, 1, g.ddoslen, &pg2pkt, start, start2);
//		ddospg_init(st);
	initstats(st);
	sendpackets(st);
	printstats(st);
	//finish_packet(st);
	st->pg->finish(st->pg);
	if (st->pg2 != NULL){
		st->pg2->finish(st->pg2);
	}
	return 0;
}

static void int_handler(int sig_num){
        printf("Exiting on signal %d\n", sig_num);
        /* set quit flag for rx thread to exit */
	struct senderthread * st;
	for (st = g.sth; st != NULL; st = st->next){
		st->finish = true;
	}
}

int initsenderthreads(int ports){
	g.sth = NULL;
	int i;
	int num = 0;
	int ports2 = ports;
	for (i = 0; ports2 > 0; ports2>>=1, i++){
		if ((ports2 & 0x01) == 0){
			continue;
		}
		num++;
	}
	int id = 0;
	__attribute__((unused)) int q = 0;
	for (i = 0; ports > 0; ports>>=1, i++){
		if ((ports & 0x01) == 0){
			continue;
		}
		struct senderthread * st = malloc (sizeof(struct senderthread));
		st->next = g.sth;
		g.sth = st;
		st->id = id++;
#if MULTIPORT
		st->port = i;
		st->queue = 0;
#else
		st->port = 0;
		st->queue = q++;
#endif
		st->finish = false;
		st->pg = NULL;
		st->pg2 = NULL;
		st->start = (((((10<<8)+0)<<8)+4)<<8)+0; //+ g.iprange/num * st->id;
		st->start2 = (((((10<<8)+0)<<8)+5)<<8)+4 + (st->id+g.startshift) * (1<<24);
		st->iprange = g.iprange;///num;
	}
	return num;
}

static void freedstmacs(int num){
	int i;
	for (i = 0; i < num; i++){
		if (g.dstmacs[i] != NULL){
			free(g.dstmacs[i]);
		}
	}
	free(g.dstmacs);
	g.dstmacs = NULL;
}

static void readdstmacs(int nb_ports){
	FILE * file = fopen(MACSFILENAME, "r");
   unsigned char val;
   int i,j;
   g.dstmacs = malloc (sizeof (struct ether_addr *)*nb_ports);
   for (j = 0; j < nb_ports; j++){
	   g.dstmacs[j] = NULL;
   }
   
   for (j = 0; j < nb_ports; j++){
	   g.dstmacs[j] = malloc(sizeof (struct ether_addr));
	   for (i = 0; i < 6; i++){
		if (fscanf(file, "%hhx, ", &val) == 0){
			fprintf(stderr, "Could not read the dst mac file (line %d). I will not change macs!\n", j);
			freedstmacs(nb_ports);
		}else{
			g.dstmacs[j]->addr_bytes[i] = (uint8_t)val;
		}
	   }
	   if (j < nb_ports - 1 && fscanf(file, "\n") != 0){
		fprintf(stderr, "Could not read the dst mac file (newline) in line %d. I will not change macs!\n", j);
			freedstmacs(nb_ports);
		}
   }
   fclose(file);
}

static void printhelp(void){
	printf("This is help for the commandline parameters for the traffic generator (sender). The generator is developed to test Trumpet performance metrics using 10G ports\n");
	printf("\t -r <num>     : packet rate (default 14880000)\n");
	printf("\t -t <num>     : the number of packets (default 500000000)\n");
	printf("\t -f <num>     : the average number of active flows (default 300)\n");
	printf("\t -a <num>     : average flow arrival rate (default 1000)\n");
	printf("\t -S <num>     : packet size in bytes (default 64)\n");
	printf("\t -b <num>     : the number of consecutive packets comming from the same flow (burst) (default 1)\n");
	printf("\t -D <double>  : the fraction of DoS packets (default 0)\n");
	printf("\t -l <num>     : the number of packets per DoS flow. Each packet is 64 bytes. (default 1)\n");
	printf("\t -R <num>     : random  seed selection between 0 and 15 (default 0)\n");
	printf("\t -p <num>     : port mask (default 1)\n");
	printf("\t -s <num>     : to shift the source IP address of generated packets in the third octet, so one shifts 1<<24 (default 0)\n");
	printf("\t -h           : prints this help\n");
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[]) {
	uint32_t srandarray []= {2019676133, 532334716, 944274866, 225660576, 302466631, 1544922000, 798407844, 781801084, 9240362, 1819284288, 528283700, 1405902435, 79097732, 892733040, 255882320, 549098745, 281930777, 1022347477, 1161684139};


	unsigned nb_ports;
	struct senderthread * st;

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;
	
	g.target = (uint32_t) 5e8;
	g.rate = MAXRATE;
	g.g_smallburst = 1;
	g.ddosrandpercent = 0;
	g.ddoslen = 1;
	g.activeflows = 300;
	g.arrivalrate = 1000;
	g.iprange = 1<<16;
	g.startshift = 0;
	g.pg1size = 1514;
	int srandindex = 0;
	int ports = 1;

	int opt;

	while ((opt = getopt(argc, argv, "r:t:b:D:l:f:R:a:p:s:S:h")) != -1) {
		switch (opt) {
		case 'r':
			g.rate = atof(optarg);
			break;
		case 't':
			g.target = atof(optarg);
			break;
		case 'f':
			g.activeflows = atof(optarg);
			break;
		case 'b':
			g.g_smallburst = atof(optarg);
			break;
		case 'D':
			g.ddosrandpercent = RAND_MAX * atof(optarg); //what fraction come from the ddos gnerator
			break;
		case 'l':
			g.ddoslen = atof(optarg); //number of consecutive packets for ddos generator
			break;
		case 'a':
			g.arrivalrate = atof(optarg);
			break;
		case 'R':
			srandindex = atof(optarg);
			break;
		case 'p':
			ports = atof(optarg);
			break;
		case 's':
			g.startshift = atof(optarg); //shift the ip address of packets
			break;
		case 'S':
			g.pg1size = atof(optarg);
			break;
		case 'h':
			printhelp();
			abort();
			break;
		default:
			printf("Unknown option %d\n", optopt);
			abort();
		}
	}

	srand(srandarray[srandindex]);

	nb_ports = initsenderthreads(ports);


	/* Check that there is an even number of ports to send/receive on. */
//	nb_ports = rte_eth_dev_count();

	/* Creates a new mempool in memory to hold the mbufs. */
	g.mbuf_pool = rte_mempool_create("MBUF_POOL",
				       NUM_MBUFS * nb_ports,
				       MBUF_SIZE,
				       MBUF_CACHE_SIZE,
				       sizeof(struct rte_pktmbuf_pool_private),
				       rte_pktmbuf_pool_init, NULL,
				       rte_pktmbuf_init,      NULL,
				       1,
				       0);

	if (g.mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initialize all ports. */
#if MULTIPORT
	for (st = g.sth; st != NULL; st = st->next){	
		if (port_init(st->port, g.mbuf_pool, 1) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", st->port);
	}
#else
	if (port_init(0, g.mbuf_pool, nb_ports) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", 0);
#endif
	printf("ports set up\n");

	if (rte_lcore_count() > nb_ports)
		printf("\nWARNING: Too many lcores enabled. Only %d used.\n", nb_ports);
	
	signal(SIGINT, int_handler);

	{//load random sequence numbers
		g.randseq = rte_malloc("randseq", SEQPERFLOW * RANDSEQNUM* sizeof (uint32_t), 64);
		if (g.randseq == NULL){
			rte_exit(EXIT_FAILURE, "Not enough memory for randseq\n");
		}
		unsigned int i,j;
	        for (i = 0; i < RANDSEQNUM; i++){
			int base = i * SEQPERFLOW;
			for (j = 0; j < SEQPERFLOW; j++){
	                	g.randseq[base+j] = (uint32_t)rand();
			}
//			for (j=0; j<seqperip/10; j++){
//				randseq[base+rand()%seqperip]=randseq[base+rand()%seqperip];
//			}
	        }
	}


	pthread_mutex_init(&g.stat_mutex, NULL);
	//read dstmacs
	readdstmacs(nb_ports);
	/* Call lcore_main on the master core only. */
	int i = 0;
	for (st = g.sth; st != NULL; st = st->next){
		rte_eal_remote_launch(lcore_main, st, 3+i*2);
		i++;
	}

  while (true) {

    if (rte_eal_get_lcore_state(3) != RUNNING) {
      break;
    }
    rte_delay_ms(500);
  }
	rte_free(g.randseq);
	for (; g.sth != NULL; g.sth = st){
		st = g.sth->next;
		free(g.sth);
	}
	pthread_mutex_destroy(&g.stat_mutex);
	freedstmacs(nb_ports);
	return 0;
}
