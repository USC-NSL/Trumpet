/* 
 * tcpserver.c - A simple TCP echo server 
 * usage: tcpserver <port>
 */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <time.h>
#include <poll.h>

#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_common.h>

#include "serverdata.h"
#include "eventhandler.h"
#include "util.h"
#include "loguser.h"


struct globalconfig{
	uint16_t servernum;
	uint16_t eventsnum;
	bool finish;
	char log_prefix[50];
};

static struct globalconfig g;

/*static void * addevent_delay (void *_){
	struct eventhandler * eh = (struct eventhandler *) _;
 	sleep(1);

	eventhandler_addlossevent(eh);
	return NULL;
}*/

static void int_handler(int sig_num){
	printf("signal %d\n", sig_num);
	g.finish = true;
}

int main(int argc, char **argv) {
  int ret = rte_eal_init(argc, argv);
        if (ret < 0)
                rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

        argc -= ret;
        argv += ret;
  //util_init();

///////////////////// handle connections
  int addeventforservers = 4;

  int parentfd; /* parent socket */
  int childfd; /* child socket */
  int portno = 5000; /* port to listen on */
  socklen_t clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* server's addr */
  int optval; /* flag value for setsockopt */
  snprintf(g.log_prefix, 50, "log");
  g.eventsnum = 16;

  /* 
   * check command line arguments 
   */
  int opt;

  enum usecase_type u = usecase_networkwide;

  while ((opt = getopt(argc, argv, "p:l:s:e:u:")) != -1){
    switch (opt) {
      case 'p':
	portno = atof(optarg);
	break;
      case 'l':
         snprintf(g.log_prefix, 50, "%s", optarg);
         break;
      case 's':
	 addeventforservers = atof(optarg);
	break;
      case 'e':
	 g.eventsnum = atof(optarg);
	break;
      case 'u':
	 switch(atoi(optarg)){
	   case 0:
	     u = usecase_tcp;
	     break;
	   case 1:
 	     u = usecase_congestion;
	     break;
	   case 2:
	     u = usecase_networkwide;
	     break;
	   default:
	     printf("Unknown usecase option %d\n", atoi(optarg));
	     abort();
	}
	 g.eventsnum = atof(optarg);
	break;
      default:
         printf("Unknown option %d\n", optopt);
         abort();
      }
  }
  util_lu = loguser_init(1<<12, g.log_prefix, 14);
  struct eventhandler * eh = eventhandler_init(u);

  /* 
   * socket: create the parent socket 
   */
  parentfd = socket(AF_INET, SOCK_STREAM, 0);
  if (parentfd < 0) 
    fprintf(stderr, "ERROR opening socket");

  /* setsockopt: Handy debugging trick that lets 
   * us rerun the server immediately after we kill it; 
   * otherwise we have to wait about 20 secs. 
   * Eliminates "ERROR on binding: Address already in use" error. 
   */
  optval = 1;
  setsockopt(parentfd, SOL_SOCKET, SO_REUSEADDR, 
	     (const void *)&optval , sizeof(int));
  int on = 1;
	int rc = ioctl(parentfd, FIONBIO, (char *)&on);
  if (rc < 0)
  {
    perror("ioctl() failed");
    close(parentfd);
    exit(-1);
  }

  /*
   * build the server's Internet address
   */
  bzero((char *) &serveraddr, sizeof(serveraddr));

  /* this is an Internet address */
  serveraddr.sin_family = AF_INET;

  /* let the system figure out our IP address */
  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);

  /* this is the port we will listen on */
  serveraddr.sin_port = htons((unsigned short)portno);

  /* 
   * bind: associate the parent socket with a port 
   */
  if (bind(parentfd, (struct sockaddr *) &serveraddr, 
	   sizeof(serveraddr)) < 0) 
    fprintf(stderr, "ERROR on binding");

  /* 
   * listen: make this socket ready to accept connection requests 
   */
  if (listen(parentfd, 5) < 0) /* allow 5 requests to queue up */ 
    fprintf(stderr, "ERROR on listen");
	
  /*
   * create reading threads
   */
   g.servernum = 0;

  /* 
   * main loop: wait for a connection request, echo input line, 
   * then close connection.
   */
  clientlen = sizeof(clientaddr);
	g.finish = false;
   signal(SIGINT, int_handler);
// polling from http://www-01.ibm.com/support/knowledgecenter/ssw_ibm_i_71/rzab6/poll.htm

 struct pollfd fd;
  fd.fd = parentfd; // your socket handler
  fd.events = POLLIN;
  while (!g.finish) {
     int ret = poll(&fd, 1, 1000);
     if (ret < 0){
	   fprintf(stderr, "tcpserver cannot poll\n");
	   break;
	}
    if (ret == 0){
	continue;
    }
    if(fd.revents != POLLIN){
        printf("  Error! revents = %d\n", fd.revents);
        break;
    }
    /* 
     * accept: wait for a connection request 
     */
    childfd = accept(parentfd, (struct sockaddr *) &clientaddr, &clientlen);
    if (childfd < 0 && g.servernum < MAX_SERVERS){
	fprintf(stderr, "ERROR on accept");
    }else if (g.servernum >= MAX_SERVERS){
	fprintf(stderr, "Can only support %d connections", MAX_SERVERS);
    }else{
	int one = 1;
        setsockopt(childfd, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
	struct serverdata * server =  serverdata_init(eh, childfd, clientaddr,g.servernum, 2+g.servernum*2);
	g.servernum++;
	eventhandler_addserver(eh, server);
    }
	if (eventhandler_activeservers(eh) == addeventforservers){
	  /*  pthread_t p;
	    pthread_create(&p, NULL, (void *)addevent_delay, (void *)eh); */
		//add the event after some delay so that the servers are setup correctly 
		eventhandler_addeventdelay(eh, g.eventsnum, 1000000);
	}
  }

////////////////////////////////////////////////// close
  close(parentfd);
   
  eventhandler_finish(eh);
//  util_finish();
  loguser_finish(util_lu);

  return 0;
}
