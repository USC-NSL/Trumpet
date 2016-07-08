#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <signal.h>
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
#include <rte_memzone.h>
#include <rte_errno.h>
#include <rte_ring.h>
#include <rte_kni.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "loguser.h"
#include "stdbool.h"
#include "flatreport.h"
#include "hashmap.h"
//#include "measurementthread.h"
#include "util.h"
#include "ddostable2.h"
#include "client.h"

/* Total octets in ethernet header */
#define KNI_ENET_HEADER_SIZE    14

/* Total octets in the FCS */
#define KNI_ENET_FCS_SIZE       4

/* Macros for printing using RTE_LOG */
#define RTE_LOGTYPE_APP RTE_LOGTYPE_USER1

/* Max size of a single packet */
#define MAX_PACKET_SZ           2048

/* Size of the data buffer in each mbuf */
#define MBUF_DATA_SZ (MAX_PACKET_SZ + RTE_PKTMBUF_HEADROOM)

#define RX_RING_SIZE 2048
#define TX_RING_SIZE 512
#define WORKER_BURST_SIZE 32
#define WORKER_RING_SIZE (1<<10)

#define NUM_MBUFS (8192*8)
//#define MBUF_SIZE (1600 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define MBUF_CACHE_SIZE 32
#define BURST_SIZE (RX_RING_SIZE/2)

#define NOWORKER    0
#define NOMEASURE	0
#define MIRROR    0
#define USEKNI 0
#define FOURTYG 0

#if USEKNI || FOURTYG || MIRROR
//force no multiport and one thread in special cases
	#define MEASUREMENTTHREAD_NUM	1
	#define MULTIPORT 0
#endif

//change this if you need multi thread or multi port
#ifndef MEASUREMENTTHREAD_NUM
	#define MEASUREMENTTHREAD_NUM	1
#endif
#ifndef MULTIPORT
	#define MULTIPORT 0
#endif

/*#if MIRROR || USEKNI
	#define NOWORKER 1
#endif*/

#define rte_pktmbuf_mtod_offset(m, t, o)	\
	((t)((char *)(m)->buf_addr + (m)->data_off + (o)))

#define rte_pktmbuf_offset(m, o)	\
	(((char *)(m)->buf_addr + (m)->data_off + (o)))


struct workerinfo{
        uint32_t inbuf_full;
        struct rte_mbuf * inbufs [WORKER_BURST_SIZE];
        struct rte_ring * ring;
        uint32_t pkts_num;
        uint8_t core;
        uint8_t finished;
} __rte_cache_aligned;

struct measurementthread{
	struct flatreport * fr;
	uint32_t pkts_num;
	uint16_t id;
	struct flow lastflow;
	struct rte_kni * kni;
	uint32_t last_flowhash;
	uint32_t stat_readq;
	uint32_t target;
	uint64_t start_tsc;
	uint64_t end_tsc;
	uint32_t expected_seq;	
	uint32_t endq_fill;
	uint32_t startq_fill;
	uint32_t stat_loss;
	uint32_t stat_mstepsum;
	uint32_t stat_mstepnum;
	uint32_t stat_notfinishedsweep;
	uint64_t stat_mstepduration;
	uint64_t stat_zerots;
	uint64_t stat_pktprocesstick;
	uint8_t port;
	uint8_t queue;
	bool finish;
	bool kni_ifup;
};

struct workerlookup{
	hashmap_elem e;
	struct workerinfo * worker;
	uint32_t ip;
};

struct mirroring_data{
	uint16_t vmdq_queue_base;
	uint16_t num_queues;
	uint16_t num_pools;
	uint16_t queues_per_pool;
};


struct global_arg{
	uint16_t worker_num;
	struct workerinfo workers[8];
	struct hashmap * workermap;
	struct measurementthread mts[MEASUREMENTTHREAD_NUM];
	double maxdelay;
	uint16_t controllerport;
	char controllerip[16];
	uint32_t trigger_num; 
	uint32_t trigger_perpkt;
	uint16_t trigger_patterns;
	uint16_t ddos_threshold;
	uint32_t ddos_tablecapacity;
	uint32_t nb_ports;
	uint32_t reportinterval;
	struct mirroring_data mirror;
	char log_prefix [50];
};
static struct global_arg g;

static const uint16_t data_offset =  sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr);
volatile uint8_t worker_quit_signal;
       
#if MULTIPORT
static const struct rte_eth_conf port_conf_default = {
	.rxmode = { 
		.split_hdr_size = 0,
                .header_split   = 0, 
                .hw_ip_checksum = 0, 
                .hw_vlan_filter = 0, 
                .jumbo_frame    = 0, 
                .hw_strip_crc   = 0,
		.max_rx_pkt_len = ETHER_MAX_LEN 
	},
};
#elif USEKNI
static const struct rte_eth_conf port_conf_default = {
	.rxmode = {
                .header_split = 0,      /* Header Split disabled */
                .hw_ip_checksum = 0,    /* IP checksum offload disabled */
                .hw_vlan_filter = 0,    /* VLAN filtering disabled */
                .jumbo_frame = 0,       /* Jumbo Frame Support disabled */
                .hw_strip_crc = 0,      /* CRC stripped by hardware */
        },
        .txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
};
#elif MIRROR
static const struct rte_eth_conf port_conf_default = {
        .rxmode = {
                .mq_mode        = ETH_MQ_RX_VMDQ_ONLY,
                .split_hdr_size = 0,
                .header_split   = 0, /**< Header Split disabled */
                .hw_ip_checksum = 0, /**< IP checksum offload disabled */
                .hw_vlan_filter = 0, /**< VLAN filtering disabled */
                .jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
        },

        .txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
        .rx_adv_conf = {
                /*
                 * should be overridden separately in code with
                 * appropriate values
                 */
                .vmdq_rx_conf = {
                        .nb_queue_pools = ETH_8_POOLS,
                        .enable_default_pool = 0,
                        .default_pool = 0,
                        .nb_pool_maps = 0,
                        .pool_map = {{0, 0},},
                },
        },
};
#else
static const struct rte_eth_conf port_conf_default = {
	.rxmode = { 
		.mq_mode        = ETH_MQ_RX_RSS,
		.split_hdr_size = 0,
                .header_split   = 0, 
                .hw_ip_checksum = 0, 
                .hw_vlan_filter = 0, 
                .jumbo_frame    = 0, 
                .hw_strip_crc   = 0,
		.max_rx_pkt_len = ETHER_MAX_LEN 
		},
	.rx_adv_conf = {
                .rss_conf = {
                        .rss_key = NULL,
                        .rss_hf = ETH_RSS_IP,
                },
        },
	.txmode = {
                .mq_mode = ETH_MQ_TX_NONE,
        },
};
#endif

int app_lcore_main_loop(void *_);
void printstats(struct measurementthread * mt);
static int kni_change_mtu(uint8_t port_id, unsigned new_mtu);
static int kni_config_network_interface(uint8_t port_id, uint8_t if_up);
static inline void readpackets(struct rte_mbuf ** bufs, uint16_t nb_rx, struct measurementthread *mt);
static bool islost(struct rte_mbuf * m, struct measurementthread * mt);
int mirror_lcore_main_loop(void *_qs);

/*--------------------------------- KNI -------------------------------------*/

/* Callback for request of changing MTU */
static int kni_change_mtu(uint8_t port_id, unsigned new_mtu){
        int ret;
        struct rte_eth_conf conf;

        if (port_id >= rte_eth_dev_count()) {
                RTE_LOG(ERR, APP, "Invalid port id %d\n", port_id);
                return -EINVAL;
        }

        LOG("KNI: Change MTU of port %d to %u\n", port_id, new_mtu);

        /* Stop specific port */
        rte_eth_dev_stop(port_id);

        memcpy(&conf, &port_conf_default, sizeof(conf));
        /* Set new MTU */
        if (new_mtu > ETHER_MAX_LEN)
                conf.rxmode.jumbo_frame = 1;
        else
                conf.rxmode.jumbo_frame = 0;

        /* mtu + length of header + length of FCS = max pkt length */
        conf.rxmode.max_rx_pkt_len = new_mtu + KNI_ENET_HEADER_SIZE +
                                                        KNI_ENET_FCS_SIZE;
        ret = rte_eth_dev_configure(port_id, 1, 1, &conf);
        if (ret < 0) {
                LOG("KNI: Fail to reconfigure port %d\n", port_id);
                return ret;
        }

        /* Restart specific port */
        ret = rte_eth_dev_start(port_id);
        if (ret < 0) {
                LOG("KNI: Fail to restart port %d\n", port_id);
                return ret;
        }

        return 0;
}

/* Callback for request of configuring network interface up/down */
static int kni_config_network_interface(uint8_t port_id, uint8_t if_up){
        int ret = 0;
        printf("KNI Configure network interface of %d %s\n",
                                        port_id, if_up ? "up" : "down");

        if (port_id >= rte_eth_dev_count()) {
                LOG("Invalid port id %d\n", port_id);
                return -EINVAL;
        }


        if (if_up != 0) { /* Configure network interface up */
                rte_eth_dev_stop(port_id); //FIXME FOR ONE PORT
                ret = rte_eth_dev_start(port_id);
        }else{ /* Configure network interface down */
                rte_eth_dev_stop(port_id);
	}

        if (ret < 0)
                LOG("KNI Failed to start port %d\n", port_id);
	g.mts[port_id].kni_ifup = if_up != 0;
        return ret;
}


/**
 * Interface to burst rx and enqueue mbufs into rx_q
 */
static inline uint16_t kni_ingress(struct measurementthread *mt, struct rte_mbuf ** pkts_burst){
        uint16_t nb_rx, num;
        nb_rx = rte_eth_rx_burst(mt->port, mt->queue, pkts_burst, BURST_SIZE);
//	if (nb_rx == 0) return 0; // 
#if !NOMEASURE
	if (nb_rx > 0) { 
		readpackets(pkts_burst, nb_rx, mt);
	}
#endif
        num = 0;
        do{
		//AT LEAST GO THROUGH THIS ONCE TO HANLDE IFCONFIG UP EVEN NO PKT!!!!
                num += rte_kni_tx_burst(mt->kni, pkts_burst + num, nb_rx - num);  
                rte_kni_handle_request(mt->kni);
        }while (unlikely(num < nb_rx));
	return nb_rx;
}


/**
 * Interface to dequeue mbufs from tx_q and burst tx
 */
static inline uint16_t kni_egress(struct measurementthread * mt, struct rte_mbuf ** pkts_burst){
        uint16_t nb_tx, num;
        num = rte_kni_rx_burst(mt->kni, pkts_burst, BURST_SIZE);
	if (num == 0) return 0;
#if !NOMEASURE
	readpackets(pkts_burst, num, mt);
#endif
        nb_tx = 0;
        do{
                nb_tx += rte_eth_tx_burst(mt->port, mt->queue, pkts_burst + nb_tx, num - nb_tx);
        }while (unlikely(nb_tx < num));
	return num;
}


__attribute__((unused)) static struct rte_kni * kni_alloc(uint8_t port, struct rte_mempool *  pktmbuf_pool){
        struct rte_kni *kni;
        struct rte_kni_conf conf;
        /* Clear conf at first */
        memset(&conf, 0, sizeof(conf));
        snprintf(conf.name, RTE_KNI_NAMESIZE, "vEth%u", port);
        //conf.core_id = kcore;
        conf.group_id = (uint16_t)port;
        conf.mbuf_size = MAX_PACKET_SZ;
        struct rte_kni_ops ops;
        struct rte_eth_dev_info dev_info;

        memset(&dev_info, 0, sizeof(dev_info));
        rte_eth_dev_info_get(port, &dev_info);
        conf.addr = dev_info.pci_dev->addr;
        conf.id = dev_info.pci_dev->id;

        memset(&ops, 0, sizeof(ops));
        ops.port_id = port;
        ops.change_mtu = kni_change_mtu;
        ops.config_network_if = kni_config_network_interface;

        kni = rte_kni_alloc(pktmbuf_pool, &conf, &ops);

        if (!kni) rte_exit(EXIT_FAILURE, "Fail to create kni for port: %d\n", port);
	printf("KNI created for port %d\n", port);
        return kni;
}


/*----------------------------------------- workers ---------------------------- */

static void workerbuf2queue(struct workerinfo * worker){
	int ret = rte_ring_sp_enqueue_bulk(worker->ring, (void **) worker->inbufs, worker->inbuf_full);
        if (unlikely(ret == -ENOBUFS)) {
		LOG("Worker %d ring is full\n", worker->core);
        	uint32_t k;
                for (k = 0; k < worker->inbuf_full; k++) {
                	struct rte_mbuf *m = worker->inbufs[k];
                        rte_pktmbuf_free(m);
                }
       	}
        worker->inbuf_full = 0;
}

static bool workerlookup_equal(void * data1, void * data2, void * aux __attribute__((unused))){
	return  ((struct workerlookup *) data1)->ip == ((struct workerlookup *) data2)->ip;
}

static bool workeriplookup_equal(void * data1, void * data2, void * aux __attribute__((unused))){
	uint32_t ip = *(uint32_t *)data1;
	return  ip == ((struct workerlookup *) data2)->ip;
}


static void initworkers(void){
	worker_quit_signal = 0;
	uint8_t workercores[] = {7, 9, 11, 13, 15, 17};
if (MIRROR){
	int qs [2];
	qs[0]= g.mirror.vmdq_queue_base + 1*g.mirror.queues_per_pool;
	qs[1]= g.mirror.vmdq_queue_base + 2*g.mirror.queues_per_pool;
	rte_eal_remote_launch(mirror_lcore_main_loop, &qs, 5);
}else{
	char name [24];
        uint16_t i;
	struct workerlookup wl;
	g.workermap = hashmap_init(128, 128, sizeof (struct workerlookup), offsetof(struct workerlookup, e), NULL);
        for (i = 0; i < g.worker_num; i++){
                struct workerinfo * worker = g.workers + i;
                snprintf(name, sizeof(name), "Worker %d", workercores[i]);
                worker->ring = rte_ring_create(name, WORKER_RING_SIZE, rte_socket_id(), RING_F_SP_ENQ | RING_F_SC_DEQ);
                if (worker->ring == NULL) {
                     rte_panic("Cannot create ring with worker core %u\n", workercores[i]);
                }
                worker->inbuf_full = 0;
                worker->core = workercores[i];
		worker->pkts_num = 0;
        	worker->finished = false;
		wl.ip = htonl((10<<24)+(0<<16)+(4<<8) + i);
		wl.worker = worker;
//		if (0)
		hashmap_add2(g.workermap, &wl, wl.ip, workerlookup_equal, NULL, NULL);

                rte_eal_remote_launch(app_lcore_main_loop, worker, workercores[i]);
	//	printf("Launched %d %s at %d\n", i, name, workercores[i]);
        }
  }
}


static void finishworkers(void){
	if (MIRROR){
		worker_quit_signal = 1;
		while (rte_eal_get_lcore_state(5) != FINISHED)       
        		usleep(500);
	}else{
		uint16_t i;
		uint32_t sum = 0;
		for (i = 0; i < g.worker_num; i++){
       	        	struct workerinfo * worker = g.workers + i;
       	         	if (worker->inbuf_full > 0){
       	         		workerbuf2queue(worker);        
       	         	}
      	  	}
		worker_quit_signal = 1;
        	for (i = 0; i < g.worker_num; i++){
        		struct workerinfo * worker = g.workers + i;
			printf("wait for %d\n", i);
        		while (!worker->finished) {usleep(500);}
        		sum += worker->pkts_num;
        	}
		printf("Workers processed %"PRIu32"\n", sum);
		hashmap_finish(g.workermap);
	}
}


//find using a table (-1 not found)
static inline __attribute__((unused)) struct workerinfo * findworker(struct rte_mbuf * m) {
	struct ipv4_hdr * ip_hdr = rte_pktmbuf_mtod_offset(m, struct ipv4_hdr *, sizeof(struct ether_hdr));
	struct workerlookup * wl = hashmap_get2(g.workermap, &ip_hdr->dst_addr, ip_hdr->dst_addr, workeriplookup_equal, NULL);
/*	struct in_addr dstip;
	dstip.s_addr = ip_hdr->dst_addr;
	if (wl == NULL) {
		LOG("worker not found for dst ip %s\n", inet_ntoa(dstip));
		return -1;
	}
	fprintf(stderr, "found worker %d for %s\n", wl->workerindex, inet_ntoa(dstip)); */
	return wl == NULL ? NULL : wl->worker;
}

int app_lcore_main_loop(void *_){
        struct workerinfo * wii = (struct workerinfo *) _;

/*        wii->finished = 1;
        for(;;){ 
//		 rte_delay_us(1000);
	}

	return 0;
*/
        const uint16_t burst = WORKER_RING_SIZE;
        struct rte_mbuf * table [burst];
        uint32_t j;
	uint32_t nb_rx;
        while (!worker_quit_signal){
                nb_rx =  rte_ring_sc_dequeue_burst(wii->ring, (void **)table, burst);
                wii->pkts_num += nb_rx;
/*              if (nb_rx>50){
                        usleep(nb_rx/50);
                }*/
//              usleep(nb_rx);
                for (j = 0; j < nb_rx; j++) {
                        rte_pktmbuf_free(table[j]);
                }
        }
        //drain the buffer
        while (rte_ring_count(wii->ring) > 0){
                nb_rx = rte_ring_sc_dequeue_burst(wii->ring, (void **)table, burst);
                wii->pkts_num += nb_rx;
                for (j = 0; j < nb_rx; j ++) {
                        rte_pktmbuf_free(table[j]);
                }
        }

        printf("core %u processed %u packets\n", wii->core, wii->pkts_num);
        wii->finished = 1;
        return 0;
}

/* -----------------------------------------MIRROR--------------------------------- */
__attribute__((unused)) int mirror_lcore_main_loop(void *_qs){
	int * qs = (int *) _qs; //start (inclusive) and end (exclusive)
	//just read the packets and ignore them
	struct rte_mbuf *buf[BURST_SIZE];
	uint32_t rxPackets[qs[1]-qs[0]];
	int q, p, i;
	uint16_t rxCount;
	p = 0;

	printf("reading from queues %d to %d\n", qs[0], qs[1]);

        while (!worker_quit_signal) {
		for (q = qs[0]; q < qs[1]; q++){
                	rxCount = rte_eth_rx_burst(p, q, buf, BURST_SIZE);
                	rxPackets[q-qs[0]] += rxCount;
                	for (i = 0; i < rxCount; i++)
                        	rte_pktmbuf_free(buf[i]);
		}
        }

	LOG("MIRROR\n");
	for (q = qs[0]; q < qs[1]; q++){
		LOG("q %d processed %d packets.\n", q, rxPackets[q-qs[0]]);
	}
	return 0;
}

/*
 * Builds up the correct configuration for vmdq based on the vlan tags array
 * given above, and determine the queue number and pool map number according to
 * valid pool number
 */
static int get_eth_conf(struct rte_eth_conf *eth_conf, uint32_t num_pools){
        struct rte_eth_vmdq_rx_conf conf;
        unsigned i;

        conf.nb_queue_pools = (enum rte_eth_nb_pools)num_pools;
        conf.nb_pool_maps = num_pools;
        conf.enable_default_pool = 1;
        conf.default_pool = 1; /* set explicit value, even if not used */
        //I enabled the default so that the traffic generator doesn't need to set vlans

        for (i = 0; i < conf.nb_pool_maps; i++) {
                conf.pool_map[i].vlan_id = 0;//vlan_tags[i];
                conf.pool_map[i].pools = (1UL)<<1; //send to pool 1 //for worker
        }

        (void)(rte_memcpy(eth_conf, &port_conf_default, sizeof(*eth_conf)));
        (void)(rte_memcpy(&eth_conf->rx_adv_conf.vmdq_rx_conf, &conf,
                   sizeof(eth_conf->rx_adv_conf.vmdq_rx_conf)));
        return 0;
}

/*
 * Initialises a given port using global settings and with the rx buffers
 * coming from the mbuf_pool passed as parameter
 */
static __attribute__((unused)) int mirror_port_init(uint8_t port, struct rte_mempool *mbuf_pool){
        struct rte_eth_dev_info dev_info;
        struct rte_eth_rxconf *rxconf;
        struct rte_eth_conf port_conf;
        uint16_t rxRings, txRings;
        const uint16_t rxRingSize = RX_RING_SIZE, txRingSize = TX_RING_SIZE;
        int retval;
        uint16_t q;
        uint16_t queues_per_pool;
        uint32_t max_nb_pools;
	
	uint16_t num_pools = g.mirror.num_pools;
	uint16_t num_queues, vmdq_queue_base;

	uint16_t vmdq_pool_base, num_vmdq_queues, num_pf_queues;

	/* pool mac addr template, pool mac addr is like: 52 54 00 12 port# pool# */
	struct ether_addr pool_addr_template = {
        	.addr_bytes = {0x52, 0x54, 0x00, 0x12, 0x00, 0x00}
	};


        /*
         * The max pool number from dev_info will be used to validate the pool
         * number specified in cmd line
         */
        rte_eth_dev_info_get(port, &dev_info);
        max_nb_pools = (uint32_t)dev_info.max_vmdq_pools;
        
	/*
         * We allow to process part of VMDQ pools specified by num_pools in
         * command line.
         */
        if (num_pools > max_nb_pools) {
                printf("num_pools %d >max_nb_pools %d\n",
                        num_pools, max_nb_pools);
                return -1;
        }
        retval = get_eth_conf(&port_conf, max_nb_pools);
        if (retval < 0)
                return retval;

        /*
         * NIC queues are divided into pf queues and vmdq queues.
         */
        /* There is assumption here all ports have the same configuration! */
        num_pf_queues = dev_info.max_rx_queues - dev_info.vmdq_queue_num;
        queues_per_pool = dev_info.vmdq_queue_num / dev_info.max_vmdq_pools;
        num_vmdq_queues = num_pools * queues_per_pool;
        num_queues = num_pf_queues + num_vmdq_queues;
        vmdq_queue_base = dev_info.vmdq_queue_base;
        vmdq_pool_base  = dev_info.vmdq_pool_base;

        printf("pf queue num: %u, configured vmdq pool num: %u,"
                " each vmdq pool has %u queues\n",
                num_pf_queues, num_pools, queues_per_pool);
        printf("vmdq queue base: %d pool base %d\n",
                vmdq_queue_base, vmdq_pool_base);
        if (port >= rte_eth_dev_count())
                return -1;
        
        /*
         * Though in this example, we only receive packets from the first queue
         * of each pool and send packets through first rte_lcore_count() tx
         * queues of vmdq queues, all queues including pf queues are setup.
         * This is because VMDQ queues doesn't always start from zero, and the
         * PMD layer doesn't support selectively initialising part of rx/tx
         * queues.
         */
        rxRings = num_queues;//(uint16_t)dev_info.max_rx_queues;
        txRings = num_queues; //(uint16_t)dev_info.max_tx_queues;
        retval = rte_eth_dev_configure(port, rxRings, txRings, &port_conf);
        if (retval != 0)
                return retval;

        rte_eth_dev_info_get(port, &dev_info);
        rxconf = &dev_info.default_rxconf;
        rxconf->rx_drop_en = 1;
        for (q = 0; q < rxRings; q++) {
                retval = rte_eth_rx_queue_setup(port, q, rxRingSize,
                                        rte_eth_dev_socket_id(port),
                                        rxconf,
                                        mbuf_pool);
                if (retval < 0) {
                        printf("initialise rx queue %d failed\n", q);
                        return retval;
                }
        }

        for (q = 0; q < txRings; q++) {
                retval = rte_eth_tx_queue_setup(port, q, txRingSize,
                                        rte_eth_dev_socket_id(port),
                                        NULL);
                if (retval < 0) {
                        printf("initialise tx queue %d failed\n", q);
                        return retval;
                }
        }

        retval  = rte_eth_dev_start(port);
        if (retval < 0) {
                printf("port %d start failed\n", port);
                return retval;
        }

	struct ether_addr addr;
        rte_eth_macaddr_get(port, &addr);
        printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
                           " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
                        (unsigned)port,
                        addr.addr_bytes[0], addr.addr_bytes[1],
                        addr.addr_bytes[2], addr.addr_bytes[3],
                        addr.addr_bytes[4], addr.addr_bytes[5]);

        /*
         * Set mac for each pool.
         * There is no default mac for the pools in i40.
         * Removes this after i40e fixes this issue.
         */
        for (q = 0; q < num_pools; q++) {
                struct ether_addr mac;
                mac = pool_addr_template;
                mac.addr_bytes[4] = port;
                mac.addr_bytes[5] = q;
                printf("Port %u vmdq pool %u set mac %02x:%02x:%02x:%02x:%02x:%02x\n",
                        port, q,
                        mac.addr_bytes[0], mac.addr_bytes[1],
                        mac.addr_bytes[2], mac.addr_bytes[3],
                        mac.addr_bytes[4], mac.addr_bytes[5]);
                retval = rte_eth_dev_mac_addr_add(port, &mac,
                                q + vmdq_pool_base);
                if (retval) {
                        printf("mac addr add failed at pool %d\n", q);
                        return retval;
                }
        }
	g.mirror.num_queues = num_queues;
	g.mirror.vmdq_queue_base = vmdq_queue_base;
	g.mirror.queues_per_pool = queues_per_pool;

        return 0;
} 


/* -------------------------------------------------------------------------------- */

/* basicfwd.c: Basic DPDK skeleton forwarding example. */

/*
 * Initializes a given port using global settings and with the RX buffers
 * coming from the mbuf_pool passed as a parameter.
 */
static __attribute__((unused)) int port_init(uint8_t port, struct rte_mempool *mbuf_pool){
	struct rte_eth_conf port_conf = port_conf_default;
	uint16_t rx_rings = MULTIPORT ? 1 : MEASUREMENTTHREAD_NUM;
	const uint16_t tx_rings = 1;
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
	int i = 0;
	do{
		rte_eth_link_get(port, &link);
		i++;
	}while(link.link_status == 0 && i < 10);
	if (i==10){
		fprintf(stderr, "Could not initiate port %d\n", port);
	}
	return 0;
}

inline static __attribute__((unused)) bool islost(struct rte_mbuf * m, struct measurementthread * mt){
		 uint32_t seq;
		int proto = (uint8_t)rte_pktmbuf_mtod_offset(m, struct ipv4_hdr *, sizeof(struct ether_hdr))->next_proto_id;
                if (proto == IPPROTO_TCP){
               		seq= *rte_pktmbuf_mtod_offset(m, uint32_t *, data_offset + sizeof (struct tcp_hdr));
		}else{
               		seq= *rte_pktmbuf_mtod_offset(m, uint32_t *, data_offset + sizeof (struct udp_hdr));
		}
                if (unlikely(mt->expected_seq != seq)){//393473 testbed problem
                    LOG("%"PRIu64" Loss %d, %d = %d,  loss %u pktcount %"PRIu32" %"PRIu32" %"PRIu32"\n",rte_rdtsc(), mt->expected_seq, seq, seq-mt->expected_seq, mt->stat_loss, mt->pkts_num, mt->startq_fill, mt->endq_fill);
		    mt->stat_loss += seq - mt->expected_seq;
    	            mt->expected_seq = seq + 1;
		    return true;
                }else{
                    mt->expected_seq++;
		    return false;
                }
}


static void readpackets(struct rte_mbuf ** bufs, uint16_t nb_rx, struct measurementthread *mt) {
	uint16_t i,j;
	struct rte_mbuf * m;
	struct ipv4_hdr * ip_hdr;
	struct flatreport_pkt * pkt;
	struct tcp_hdr * tcp2;
	struct udp_hdr * udp2;
        uint16_t srcport, dstport, data_offset2;
        uint32_t pkt_seq = 0;
	uint32_t pkt_ack = 0;
	uint8_t proto;
	uint16_t burst = FLATREPORT_PKT_BURST; //NOTE THIS MUST BE <= FLATREPORT_PKT_BURST
	const uint16_t remainder = nb_rx % burst; 
	const uint16_t loops = nb_rx/burst + (remainder>0?1:0);
	struct flatreport * fr = mt->fr;
	__attribute__((unused)) struct flow * prevflow;
	for (j = 0; j < loops; j++, bufs += burst){
   	    if (remainder > 0 && j == loops-1){
		burst = remainder;
	    }
	    for (i = 0; i < burst; i++){
                m = bufs[i];
                PKT_PREFETCH0(m);
	    }
	    for (i = 0; i < burst; i++){
		m = bufs[i];
		PKT_PREFETCH0(rte_pktmbuf_offset(m, sizeof(struct ether_hdr))); //other headers are in the same cacheline anyway
	    }
	    PKT_PREFETCH0(fr->pkts);
	    prevflow = &mt->lastflow;
	    fr->pkt_q = 0;
	    for (i = 0; i < burst; i++){
        	m = bufs[i];
//		printf("%p\n",bufs+i);
/*                eth_hdr = rte_pktmbuf_mtod_offset(m, struct ether_hdr *, 0);
                if (eth_hdr->ether_type != rte_cpu_to_be_16(ETHER_TYPE_IPv4)){
                	continue;
                }*/
                ip_hdr = rte_pktmbuf_mtod_offset(m, struct ipv4_hdr *, sizeof(struct ether_hdr));
		if (ip_hdr->dst_addr == 0xffffffff){
			continue;
		}

#if !NOWORKER
        	struct workerinfo * worker = g.workers + ntohl(ip_hdr->dst_addr)%(g.worker_num); 
		rte_prefetch0(worker);
#endif 

                proto = (uint8_t)ip_hdr->next_proto_id;
                if (proto == IPPROTO_TCP){
                	tcp2 = rte_pktmbuf_mtod_offset(m, struct tcp_hdr *, sizeof (struct ether_hdr) + sizeof(struct ipv4_hdr));
                        srcport = tcp2->src_port;
                        dstport = tcp2->dst_port;
                        pkt_seq = (uint32_t)tcp2->sent_seq;
			pkt_ack = (uint32_t)tcp2->recv_ack;
			data_offset2 = data_offset + sizeof(struct tcp_hdr);
                }else if (proto == IPPROTO_UDP){
                        udp2 = rte_pktmbuf_mtod_offset(m, struct udp_hdr *, sizeof (struct ether_hdr) + sizeof(struct ipv4_hdr));
                        srcport = udp2->src_port;
                        dstport = udp2->dst_port;
			data_offset2 = data_offset + sizeof(struct udp_hdr);
                       	pkt_seq = *rte_pktmbuf_mtod_offset(m, uint32_t *, data_offset2 + sizeof(uint32_t)); // Generator puts some random sequence numbers
			pkt_ack = 0;
                }else{
                        continue;
                }
		pkt = fr->pkts + fr->pkt_q;
		fr->pkt_q++;
        	pkt->ip_p = proto;
		pkt->ts =  0;
                        //(uint32_t)((rte_rdtsc() - fr->epoch_ts) / rte_get_tsc_hz());
		pkt->f.srcip = ip_hdr->src_addr;
		pkt->f.dstip = ip_hdr->dst_addr; 
		pkt->f.ports = (srcport<<16) | dstport;
		pkt->ack = ntohl(pkt_ack);
        	pkt->seq = ntohl(pkt_seq);
        	pkt->length = ntohs(ip_hdr->total_length);
//		flow_print(&pkt->f);
#if !NOWORKER
		/*struct workerlookup * wl =  hashmap_get2(g.workermap, &ip_hdr->dst_addr, ip_hdr->dst_addr, workeriplookup_equal, NULL);
		if (wl != NULL){
			struct workerinfo * worker = wl->worker;
		*/
        //		struct workerinfo * worker = &g.workers[ip_hdr->dst_addr%g.worker_num]; 
	//		worker->inbufs[0] = m;
			
			worker->inbufs[worker->inbuf_full++] = m;
        		bufs[i] = NULL;
			if (worker->inbuf_full >= WORKER_BURST_SIZE){
				//LOG("%d %d\n", worker->core, mt->pkts_num);
				workerbuf2queue(worker);
			//	worker->inbuf_full = 0;
			}
		//}
#endif
#if !PACKETHISTORY
		pkt->sameaslast = flow_equal(prevflow, &pkt->f);
		if (pkt->sameaslast){
			pkt->hash = mt->last_flowhash;
		}else{
			pkt->hash = mt->last_flowhash = flow_hash(&pkt->f);
		}
		prevflow = &pkt->f;

    #if HASH_PREFETCH_ENABLE
		flatreport_readpacket_prefetch(fr, pkt);
    #else
		flatreport_process(fr, pkt);
    #endif				
#endif

	    	PKT_PREFETCH0(fr->pkts + fr->pkt_q);
           }
///////////////////////////// BATCH PROCESS
#if PACKETHISTORY
	   flatreport_historyprocess(fr);
#else
	   flow_fill(&mt->lastflow, prevflow); //must copy at the end of loop but inside, because prevvflow is a pointer to fr->pkts entry
  #if HASH_PREFETCH_ENABLE
	   flatreport_batchprocess(fr);
  #endif 
#endif
	}
}

static inline void __attribute__((unused))purereadpackets(struct rte_mbuf ** bufs, struct measurementthread *mt) {
	uint32_t nb_rx = rte_eth_rx_burst(mt->port, mt->queue, bufs, BURST_SIZE);
	mt->pkts_num+=nb_rx;
	uint32_t i;
	for (i = 0; i < nb_rx ; i++){
		rte_prefetch0(bufs[i]);
	}
	for (i = 0; i < nb_rx ; i++){
        	rte_pktmbuf_free(bufs[i]);
        }
}

static inline bool experiment_isstarted(struct measurementthread * mt){
	return mt->pkts_num > 10;
}

/*static void syncepoch(int ms){
	struct timeval tv;
	int ret;
	do{
		ret = gettimeofday(&tv, NULL);
		if (ret != 0){
			fprintf(stderr, "Cannot get current time\n");
			break;
		}
	}while (tv.tv_usec % (ms * 1000) != 0);
}*/

/*__attribute__((unused)) static int main_loop(void *arg){
        struct measurementthread * mt = (struct measurementthread *) arg;
        printf("start at %d\n", mt->port);
        while (1) {
                if (mt->finish)
                        break;
                kni_ingress(mt);
                kni_egress(mt);
        }

        return 0;
}*/


static void measurementthread_prepare(struct measurementthread * mt){
	char buf_name[50];
	snprintf(buf_name, 50, "%s_%d", g.log_prefix, mt->id);
	loguser_registerthread(util_lu, buf_name);
	mt->finish = false;
	struct ddostable2 * dt = NULL;
#if DDOS_TABLE
	if (g.ddos_threshold > 0){
	        dt = ddostable2_init(g.ddos_threshold, g.ddos_tablecapacity);
	}
#endif
	usleep(mt->id * 100); // just to order ids at the controller!
	struct client * c = client_init(g.controllerip, g.controllerport, 17, true);
	mt->fr = flatreport_init(dt, c);

	struct triggertype * types [4];
	flatreport_addtypes(mt->fr, types, 4);
	flatreport_addtriggers(mt->fr, g.trigger_num, g.trigger_perpkt, g.trigger_patterns, types, 3);

	/*
	 * Check that the port is on the same NUMA node as the polling thread
	 * for best performance.
	 */
	if (rte_eth_dev_socket_id(mt->port) > 0 &&
		rte_eth_dev_socket_id(mt->port) != (int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", mt->port);
	rte_eth_stats_reset(mt->port);

	printf("\nCore %u handling packets. [Ctrl+C to quit]\n", rte_lcore_id());
}

static void packethistoryloop(struct measurementthread * mt){
	const uint64_t onemsticks =  (uint64_t)rte_get_tsc_hz()/1e3;
	const uint64_t sweepperiod = 10 * onemsticks; //10ms
	uint64_t end;
	uint64_t start = rte_rdtsc();
	uint16_t nb_rx2 = 0;
	uint16_t nb_rx = 0;
	struct rte_mbuf ** bufs2;
	struct rte_mbuf *bufs[BURST_SIZE];
	while (!mt->finish) {
                        nb_rx = rte_eth_rx_burst(mt->port, mt->queue, bufs + nb_rx2, BURST_SIZE);
			nb_rx2 += nb_rx;
			bufs2 = bufs;
			while (nb_rx2 >= FLATREPORT_PKT_BURST){
	                        readpackets(bufs2, FLATREPORT_PKT_BURST, mt);
				bufs2 += FLATREPORT_PKT_BURST;
				nb_rx2 -= FLATREPORT_PKT_BURST;
			}
			if (nb_rx2 > 0 && bufs2 > bufs){
				rte_memcpy(bufs, bufs2, sizeof(struct rte_mbuf) * nb_rx2); //don't worry they cannot be overlapping
			}

                        mt->pkts_num += nb_rx;
                        mt->stat_readq++;
			end = rte_rdtsc();
                        if (end - start >= sweepperiod){
                                flatreport_naivesweep(mt->fr);
                                start = end;
                        }
        }
}


static __attribute__((unused)) void runForZerots(struct measurementthread * mt){
	struct flatreport * fr = mt->fr;
	int16_t todrain = 0;
	uint16_t nb_rx, i;
	struct rte_mbuf *bufs[BURST_SIZE];
	mt->stat_zerots = 0;
	__attribute__((unused)) uint64_t ts, ts2, ts3;
	ts3 = ts2 = ts = 0;
	const int target = 24;
//	int nb_rx2;
	__attribute__((unused))	int n, statn1, statn2;
	statn1 = statn2 = 0;
//	int readstatnum = 0;
//	uint64_t readstatsum = 0;
//	uint64_t queuecountoverhead = 0;
//	uint64_t queuecountoverheadnum = 0;
	__attribute__((unused)) const int call32delay = 1000;
        while (!mt->finish && mt->pkts_num < mt->target) {
        	// doesn't work as nb_rx can be small even though there are packets there!
//		ts = rte_rdtsc();
//		nb_rx = rte_eth_rx_burst(mt->port, mt->queue, bufs, BURST_SIZE);
//		if (nb_rx < target && experiment_isstarted(mt) && mt->pkts_num < mt->target - 32){
//			do{
//                     			nb_rx += rte_eth_rx_burst(mt->port, mt->queue, bufs + nb_rx, BURST_SIZE);
//			}while (nb_rx < target && !mt->finish);
//			ts2 = rte_rdtsc();
//			mt->stat_zerots += ts2 - ts;// - call32delay;
//		}
				
		if (todrain < target && experiment_isstarted(mt) && mt->pkts_num < mt->target-32){
			ts = rte_get_tsc_cycles();
			do{
				todrain = rte_eth_rx_queue_count(mt->port, mt->queue);
				if (todrain >= target){
					break;
				}
			}while (todrain < target && !mt->finish);
			mt->stat_zerots += rte_get_tsc_cycles() - ts + 45; //45 for rte_rdtsc 
		}
		nb_rx = rte_eth_rx_burst(mt->port, mt->queue, bufs, BURST_SIZE);
//		if ((todrain >= 32 && nb_rx<32) || (todrain<32 && nb_rx>0)){printf("error %d %d\n",todrain, nb_rx);}
		todrain -= nb_rx;
                readpackets(bufs, nb_rx, mt);
                mt->pkts_num += nb_rx;
		for (i = 0; i < nb_rx; i++){
	               	rte_pktmbuf_free(bufs[i]);
		}
	}
	LOG("profilezerots: %"PRIu64"\n", mt->stat_zerots); //, 1.0 * statn2/statn1, 1.0*readstatsum/readstatnum,readstatnum );
	printstats(mt);
	flatreport_finish(fr);
}

/*
 * The lcore main. This is the main thread that does the work, reading from
 * an input port and writing to an output port.
 */
static int lcore_main(void *_){
/*	int i;
	uint64_t x,y;
	x=0;
	y = rte_rdtsc();
	for (i = 0; i < 1<<25; i++){
		x+=rte_rdtsc();
	}
	printf("%f\n", 1.0*(rte_rdtsc()-y)/(1<<25));
	return 0;*/
	struct measurementthread * mt = (struct measurementthread *) _;
	measurementthread_prepare(mt);
/////////////////////////////////////
	
/*	flatreport_profilematching(mt->fr);		
	flatreport_finish(mt->fr);
	return 0;*/
///////////////////////////////////

	struct flatreport * fr = mt->fr;
	{	
		client_hello(fr->c, mt->id, fr->step);
//		syncepoch(10);

		client_waitforhello(fr->c);
		struct timeval tv;
		gettimeofday(&tv, NULL);
		LOG("Starting at %lu.%lu %"PRIu64"\n", (long unsigned)tv.tv_sec, (long unsigned)tv.tv_usec, rte_rdtsc());
	}

  if (PACKETHISTORY){
	packethistoryloop(mt);
  }else{
	uint16_t nb_rx = 0;

	uint64_t lastperiod_tick, tick1;
	struct rte_mbuf *bufs[BURST_SIZE];
	__attribute__((unused)) uint32_t end_pkts_num = 0;

	const uint64_t onemsticks =  (uint64_t)rte_get_tsc_hz()/1e3;
        const uint64_t maxdelaypacket = (uint64_t)rte_get_tsc_hz()*g.maxdelay; //100us
	const uint64_t sweepperiod = g.reportinterval * onemsticks; //10ms
        const uint64_t minsweepticks = maxdelaypacket / 10; 
	//const float alpha = 0.4;
        fr->minsweepticks = minsweepticks;
	
	mt->end_tsc = mt->start_tsc = 0;
	mt->expected_seq = 0;	
	mt->startq_fill = mt->endq_fill = 0;
        //ticksperpacket = (uint64_t)rte_get_tsc_hz()/1e6; //start with 1Mpps

	__attribute__((unused)) uint32_t workerturn = 0;

	int16_t skipped;
	__attribute__((unused)) int16_t  todrain, i;
	__attribute__((unused)) uint64_t lastsweepcheck = 0;
//	uint64_t lastread = tick1;
//	int pktprocessedinstepckeck;
	__attribute__((unused)) uint64_t lastzerots = 0;

	todrain = 0;
	tick1 = lastperiod_tick = rte_rdtsc();
        for (mt->pkts_num = 0; mt->pkts_num < mt->target && !mt->finish;) {
		if (unlikely(tick1 - lastperiod_tick >= sweepperiod)){ // start sweep
                	lastperiod_tick += sweepperiod;
			//calcualte skipped sweeps
			skipped = (tick1 - lastperiod_tick) / sweepperiod;
			if (unlikely(skipped > 0)){
//	                        printf("SKIP %d: startsweep %u at step %d with delay %"PRId64"\n",
//                                     skipped, mt->stat_mstepnum, fr->step, (int64_t)(tick1-lastperiod_tick)-(int64_t)sweepperiod);
				lastperiod_tick += sweepperiod * skipped;
				mt->stat_notfinishedsweep += skipped;
			}
			
			if (likely(flatreport_issweepfinished(fr))){
				if (experiment_isstarted(mt)){
					mt->stat_mstepnum++;
				}
        	               	flatreport_startsweep(fr);
		//		LOG("sweep %d, check %"PRIu64", now %"PRIu64"\n", fr->step, lastsweepcheck, tick1);
			}else{
	         //              	LOG("NOTFIN: %"PRIu64" startsweep %u at step %d with delay %"PRId64", %"PRId64"\n",
                  //   	               tick1, mt->stat_mstepnum, fr->step, (int64_t)(tick1-lastperiod_tick),(int64_t)sweepperiod);
				mt->stat_notfinishedsweep++;
        		}
                }
		lastsweepcheck = tick1;

		if (!flatreport_issweepfinished(fr)){  //run a sweep if still must be run
		//	LOG("%"PRIu64",%d,%d\n",tick1, mt->pkts_num, fr->tt->state.index);
                        flatreport_sweep(fr, maxdelaypacket, tick1);
		//	LOG("2,%"PRIu64",%"PRId32"\n", rte_rdtsc(), rte_eth_rx_queue_count(mt->port, mt->queue));
			if (experiment_isstarted(mt)){
				mt->stat_mstepsum++;
				mt->stat_mstepduration += rte_rdtsc() - tick1;
			}
		}else {
			bool found = false;
		//if (tick1 - lastread > onemsticks/10){
		//	lastread = tick1;
			if (!found){
				client_readsync(fr->c, maxdelaypacket, tick1);
			}
		}

		end_pkts_num = mt->pkts_num; //this is for timer stat
		fr->epoch_ts = rte_rdtsc();//FIXME this cannot be used as only packet ts as new packet come during processing current batch		
		todrain = 0;

///////////////////////////////////////////////////////////////////// PROFILE ZEROTS : Remember to change checkflow too
//		runForZerots(mt);
//		return 0;
/////////////////////////////////////////////////////////////////////////
#if USEKNI
                while (!mt->finish && mt->pkts_num < mt->target) {
			nb_rx = kni_ingress(mt, bufs);
			nb_rx += kni_egress(mt, bufs);
			if (nb_rx == 0){
				break;
			}
			if (unlikely(mt->start_tsc == 0 && experiment_isstarted(mt))){
                                mt->start_tsc = rte_rdtsc();
                        }
                        mt->pkts_num += nb_rx;
                        mt->stat_readq++;
		}
#else
                while (!mt->finish && mt->pkts_num < mt->target) {
#if FOURTYG
			//round robin on ports
			mt->port = (mt->port + 1) % g.nb_ports;
#endif
                        nb_rx = rte_eth_rx_burst(mt->port, mt->queue, bufs, BURST_SIZE);
#if !NOMEASURE
			if (nb_rx == 0){ 
				//to measure zerots for optimizations
/*				if (lastzerots > 0 && experiment_isstarted(mt)){
					uint64_t temp = rte_rdtsc(); //this is required to not include last epoch that packets may have arrived
					mt->stat_zerots += temp - lastzerots;
					lastzerots = temp;
				}else{
					lastzerots = rte_rdtsc();
				}*/
                                break;
                        }
//			lastzerots = 0;

                        readpackets(bufs, nb_rx, mt);
#endif
			for (i = 0; i < nb_rx; i++){
				//islost(bufs[i], mt);
				if (bufs[i] != NULL){ //a worker may got the packet
	                		rte_pktmbuf_free(bufs[i]);
				}
			}

			if (unlikely(mt->start_tsc == 0 && experiment_isstarted(mt))){
				mt->start_tsc = rte_rdtsc();
			} 
                        mt->pkts_num += nb_rx;
                        mt->stat_readq++;

                        todrain -= nb_rx;
                        if (todrain <= 0 && !NOMEASURE){
                                todrain = rte_eth_rx_queue_count(mt->port, mt->queue);
			
//				if (likely(flatreport_hasflow(fr))){ 
		//			LOG("1,%"PRIu64",%"PRId32"\n", rte_rdtsc(), todrain);
//				}

				
                                //if (todrain < RX_RING_SIZE - 200){ // 2*(5us+1us)*15
                                if (todrain < 40){ // 2*(5us+1us)*15
//					if (lastzerots == 0 && experiment_isstarted(mt)){
//						lastzerots = rte_rdtsc();
//					}
                                        break;
                                }
                        }
                }
#endif
//                g.startq_fill = todrain;
		
                tick1 = rte_rdtsc(); //needs a new tick
		if (end_pkts_num < mt->pkts_num){ //if it received any packet
	                mt->stat_pktprocesstick += tick1 - fr->epoch_ts;
		}
	}
}


//log and stats
	if (mt->end_tsc == 0){
		mt->end_tsc = rte_rdtsc();
	}

#if FOURTYG
	uint32_t i;
	for (i = 0; i < g.nb_ports; i++){
		mt->port = i;
		printstats(mt);
	}
#else
	printstats(mt);
#endif

	flatreport_finish(mt->fr);

 return 0;
}

void printstats(struct measurementthread * mt){
        struct flatreport * fr = mt->fr;
        uint64_t core_cycles = (mt->end_tsc - mt->start_tsc);
        LOG("Summary1 pkts:%"PRIu32 ", cycles:%" PRIu64 ", rate:%f, loss:%f\n", mt->pkts_num, core_cycles, 1.0*rte_get_tsc_hz() * mt->pkts_num/core_cycles, 100 * (1-1.0 * mt->pkts_num/mt->target));
        LOG("Summary2 perpkttick:%f, readq:%d, pktperq:%f, ticks:%"PRIu64" zerots:%f  flownum:%d\n", 1.0*mt->stat_pktprocesstick/mt->pkts_num, mt->stat_readq, 1.0*mt->pkts_num/mt->stat_readq, mt->stat_pktprocesstick, (double)mt->stat_zerots, fr->stat_flownum);
        LOG("Summary3 notfinished %"PRIu32" loss %f, steps %u, avgmstepnum %f, mstepticks %f\n", mt->stat_notfinishedsweep, 1.0*mt->stat_loss/mt->target, mt->stat_mstepnum, 1.0*mt->stat_mstepsum/mt->stat_mstepnum, 1.0*mt->stat_mstepduration/mt->stat_mstepnum);
        struct rte_eth_stats stats;
        rte_eth_stats_get(mt->port, &stats);

        double rx_loss = 100 * ((double)stats.ierrors /
            (double)(stats.ierrors+stats.ipackets+1));
        double tx_loss = 100 * ((double)stats.oerrors/
            (double)(stats.oerrors+stats.opackets+1));


LOG(         "-----------------------------------------------------------\n");
 LOG(        "| Port-%d Queue-%d\n", mt->port, mt->queue);
 LOG(        "-----------------------------------------------------------\n");
 LOG(        "|   RX Packets       : %" PRIu64 "/%" PRIu64 " (%" PRIu64 " bytes)\n", stats.ipackets, stats.ierrors, stats.ibytes);
 LOG(        "|      RX Packet Loss: %.2f \n", rx_loss);
 LOG(        "-----------------------------------------------------------\n");
 LOG(        "|   TX Packets       : %" PRIu64 "/%" PRIu64 " (%" PRIu64 " bytes)\n", stats.opackets, stats.oerrors, stats.obytes);
 LOG(        "|      TX Packet Loss: %.2f \n", tx_loss);
 LOG(        "-----------------------------------------------------------\n");

}


static void int_handler(int sig_num){
	printf("Exiting on signal %d\n", sig_num);
	/* set quit flag for rx thread to exit */
	int i;
	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		g.mts[i].finish = true;
	}
}

/*
 * The main function, which does initialization and calls the per-lcore
 * functions.
 */
int main(int argc, char *argv[]){

	struct rte_mempool *mbuf_pool;
	uint16_t i;

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	memset(&g, 0, sizeof(struct global_arg));
	
	g.trigger_num = 0; 
	g.trigger_perpkt = 0;
	g.trigger_patterns = 1;
	g.maxdelay = 0.000005;
	g.worker_num = 0;
	g.reportinterval = 10;

	snprintf(g.controllerip, 16, "127.0.0.1");
	g.controllerport = 5000;
	g.ddos_threshold = 0;
	g.ddos_tablecapacity = 1<<20;
	snprintf(g.log_prefix, 50, "log");
	uint32_t target = 100000000;

	int opt;

	while ((opt = getopt(argc, argv, "n:p:t:d:P:T:C:w:c:z:l:i:")) != -1) {
		switch (opt) {
			case 't':
				g.trigger_num = atof(optarg);
				break;
			case 'p':
				g.trigger_perpkt = atof(optarg);
				break;
			case 'n':
				target = atof(optarg);
				break;
			case 'd':
				g.maxdelay = atof(optarg);
				break;
			case 'P':
				g.trigger_patterns = atof(optarg);
				break;
			case 'T':
				g.ddos_threshold = atof(optarg);
				break;
			case 'z':
				g.ddos_tablecapacity = atof(optarg);
				break;
			case 'w':
				g.worker_num = atof(optarg);
				break;
			case 'c':
				snprintf(g.controllerip, 16, "%s", optarg);
				break;
			case 'C':
				g.controllerport = atof(optarg);
				break;
			case 'i':
				g.reportinterval = atof(optarg);
				break;
			case 'l':
				snprintf(g.log_prefix, 50, "%s", optarg);
				break;
		      default:
			printf("Unknown option %d\n", optopt);
		        abort();
    		}
	}
		

	
	signal(SIGINT, int_handler);

	/* Check that there is an even number of ports to send/receive on. */
	g.nb_ports = rte_eth_dev_count();
#if !FOURTYG
#if !MULTIPORT
	if (g.nb_ports > 1)
		printf("WARNING: more than 1 port. I use port 0\n");
#else
	if (g.nb_ports < MEASUREMENTTHREAD_NUM)
		rte_exit(EXIT_FAILURE,"not enough ports %d vs %d\n", g.nb_ports, MEASUREMENTTHREAD_NUM);
	else
		g.nb_ports = MEASUREMENTTHREAD_NUM;
#endif 
#endif

#if MIRROR
	//larger mempool
	mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", 2 * NUM_MBUFS * g.nb_ports,
						MBUF_CACHE_SIZE, 0, MBUF_DATA_SZ, rte_socket_id());
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	g.mirror.num_pools = 2;
	/* Initialize all ports. */
	i = 0;
	if (mirror_port_init(i, mbuf_pool) != 0)
		rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", i);

	 //this is from test-pmd to enable mirroring
        struct rte_eth_mirror_conf mr_conf;
        memset(&mr_conf, 0, sizeof(struct rte_eth_mirror_conf));
        mr_conf.rule_type = ETH_MIRROR_UPLINK_PORT;
        //mr_conf.pool_mask = strtoull(res->value, NULL, 16);
        mr_conf.dst_pool = 0;

        //enable on port 0
        int ret0 = rte_eth_mirror_rule_set(i, &mr_conf, 0, 1);
        printf("mirroring config ret %d\n", ret0);


#else
	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("mbuf_pool", NUM_MBUFS * g.nb_ports,
						MBUF_CACHE_SIZE, 0, MBUF_DATA_SZ, rte_socket_id());
	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");


	/* Initialize all ports. */
	for (i = 0; i < g.nb_ports; i++){
		if (port_init(i, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu8 "\n", i);
	}
#endif

	util_lu = loguser_init(1<<12, g.log_prefix, 19);

	struct measurementthread * mt;
	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		mt = &g.mts[i];
		mt->port = MULTIPORT ? i : 0;
		mt->queue = MULTIPORT? 0 : i;
		mt->stat_pktprocesstick = 0;
		mt->stat_readq = 0;
		mt->stat_loss = 0;
		mt->stat_mstepsum = 0;
		mt->stat_mstepduration = 0;
		mt->stat_mstepnum = 0;
		mt->stat_notfinishedsweep = 0;
		mt->stat_zerots = 0;
		mt->last_flowhash = 0;
		mt->id = i;
		mt->target = target;
		mt->kni = NULL;
	}

#if USEKNI
	/* Invoke rte KNI init to preallocate the ports */
        rte_kni_init(g.nb_ports);
	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		mt = &g.mts[i];
                mt->kni = kni_alloc(i, mbuf_pool);
		mt->kni_ifup = false;
	}
#endif


	initworkers();

	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		rte_eal_remote_launch(lcore_main, &g.mts[i], 3+i*2);
	}
	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		while ( rte_eal_get_lcore_state(3+i*2) != FINISHED){
			usleep(100000);
		}
	}


//clean up
	finishworkers();
#if USEKNI
	for (i = 0; i < MEASUREMENTTHREAD_NUM; i++){
		mt = &g.mts[i];
		rte_kni_release(mt->kni);
	}
	rte_kni_close();
#endif
	/* finish all ports. */
        for (i = 0; i < g.nb_ports; i++){
		rte_eth_dev_stop(i);
        }
	loguser_finish(util_lu);
	return 0;
}
