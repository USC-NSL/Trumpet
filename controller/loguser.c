#include "loguser.h"
#include <stdlib.h> 
#include <stdint.h>
#include <string.h> 
#include <unistd.h>
#include "util.h"

#include <rte_ring.h>
#include <rte_mempool.h>

void * loguser_report(void * data);


struct logline{
	int tid;
	char buffer[LOGLINEBUFFER_LEN];
};

bool loguser_registerthread(struct loguser * lu,  const char * filename){
	struct logfile * lf = MALLOC(sizeof(struct logfile));
	lf->next = lu->lfh;
	lu->lfh = lf;
	snprintf(lf->filename, 100, "%s.txt", filename);
	lf->tid = (int) pthread_self();
	lf->fd = fopen(lf->filename, "w+");
	if (lf->fd == NULL){
		fprintf(stderr, "cannot write to log file %s\n", lf->filename);
		return false;
	}
	return true;
}

void * loguser_report(void * data){ //for two threads
	struct loguser * lu = (struct loguser * ) data;
	set_CPU(lu->core);
	int n, len;
	struct logline * m = NULL;
	struct logfile * lf;
	while (true){
		n = rte_ring_sc_dequeue(lu->ring, (void **)&m);
		if (n >= 0){
			if (m==NULL){continue;}
			//find logfile
			for (lf = lu->lfh; lf->next != NULL && lf->tid != m->tid; lf = lf->next); //last is the default
	
			len = strnlen(m->buffer, LOGLINEBUFFER_LEN);
			fwrite(m->buffer, 1, len, lf->fd);
			if (lu->delay > 10){ //AIMD
				lu->delay /= 2;
			}
			rte_mempool_put(lu->mem, m);
		}else{
			if (lu->finish){
				for (lf = lu->lfh; lf != NULL; lf = lf->next){
					fflush(lf->fd); //this lets empty ring before finish
				}
				break;
			}
			usleep(lu->delay);
			if (lu->delay < 500){ //AIMD
				lu->delay+=10;
			}

			for (lf = lu->lfh; lf != NULL; lf = lf->next){
				fflush(lf->fd); //this lets empty ring before finish
			}
		}
	}
	return NULL;
}

struct loguser * loguser_init(uint32_t size, const char * filename, uint16_t core){
	struct loguser * lu = MALLOC (sizeof(struct loguser));
	struct logfile * lfh = MALLOC(sizeof(struct logfile));
	lu->lfh = lfh;
	lfh->next = NULL;
	snprintf(lfh->filename, 100, "%s.txt", filename);
	lfh->tid = 0;
	lfh->fd = fopen(lfh->filename, "w+");
	if (lfh->fd == NULL){
		fprintf(stderr, "cannot write to log file %s\n",lfh->filename);
		return lu;
	}

	lu->ring = rte_ring_create("logring", size, rte_socket_id(), RING_F_SC_DEQ);
        if (lu->ring == NULL) {
        	rte_panic("Cannot create ring for client\n");
        }
        lu->mem = rte_mempool_create("log mem",
                                       size,
                                       sizeof(struct logline),
                                       32,
                                       0,
                                       NULL, NULL,
                                       NULL,      NULL,
                                       rte_socket_id(), 0);

	lu->delay = 500;
	lu->core = core;
	lu->finish = false;
	pthread_create(&lu->pth, NULL, loguser_report, lu);
	return lu;
}

void loguser_finish(struct loguser * lu){
	lu->finish = true;
	pthread_join(lu->pth, NULL);
	struct logfile * lf2;
	for (; lu->lfh != NULL; lu->lfh = lf2){
		lf2 = lu->lfh->next;
		fclose (lu->lfh->fd);
		free(lu->lfh);
	}
	//rte_mempool_finish
	rte_ring_free(lu->ring);
	FREE(lu);
}

void loguser_add(struct loguser * lu, const char * format, ...){
	struct logline * m;
	va_list argptr;
	va_start(argptr, format);
        int ret0 = rte_mempool_get(lu->mem, (void **) &m);
        if (ret0 != 0 ){
		char buf [LOGLINEBUFFER_LEN];
		vsnprintf(buf, LOGLINEBUFFER_LEN, format, argptr);
		va_end(argptr);
                fprintf(stderr, "loguser cannot get memory for a message: %s\n", buf);
                return;
        }
	vsnprintf(m->buffer, LOGLINEBUFFER_LEN, format, argptr);
	va_end(argptr);
	m->tid = (int)pthread_self();
	int ret1 = rte_ring_enqueue(lu->ring, m);
        if (ret1 != 0){
                fprintf(stderr, "loguser ring is full\n");
                rte_mempool_put(lu->mem, m);
                return;
        }
}
