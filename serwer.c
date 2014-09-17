#include <event2/event.h>
#include <event2/util.h>

#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>

#include "err.h"
#include "constants.h"

#define MAX_CLIENTS 20
#define BUF_SIZE 1024
#define TRUE 1
#define FALSE 0
#define DEBUG 1

uint16_t PORT = 10000 + (282146%10000);
size_t FIFO_SIZE = 10560;
int FIFO_LOW_WATERMARK = 0;
int FIFO_HIGH_WATERMARK;
int BUF_LEN = 10;
int TX_INTERVAL = 5;

struct event_base *base;

/* Obsługa sygnału kończenia */
static void catch_int (int sig) {
	fprintf(stderr, "Signal %d caught. Ending server's work.\n", sig);
	event_base_loopbreak(base);
}

struct connection_description {
	struct sockaddr_in address;
	evutil_socket_t sock;
	struct event *ev;
};

struct connection_description clients[MAX_CLIENTS];

struct client_fifo {
	void * data;
};

struct client_fifo fifo[MAX_CLIENTS];

void handle_commandline_args(int argc, char *argv[]){
	int is_hi_wtrmrk_set = FALSE;
	int c;
	while ((c = getopt(argc, argv, "p:F:L:H:X:i:")) != -1) {
		switch (c) {
		case 'p':
			PORT = atoi(optarg);
			break;
		case 'F':
			FIFO_SIZE = atoi(optarg);
			break;
		case 'L':
			FIFO_LOW_WATERMARK = atoi(optarg);
			break;
		case 'H':
			is_hi_wtrmrk_set = TRUE;
			FIFO_HIGH_WATERMARK = atoi(optarg);
			break;
		case 'X':
			BUF_LEN = atoi(optarg);
			break;
		case 'i':
			TX_INTERVAL = atoi(optarg);
			break;
		default:
			abort ();
		}
	}
	if (!is_hi_wtrmrk_set) FIFO_HIGH_WATERMARK = FIFO_SIZE;
}

void init_clients(void){
	memset(clients, 0, sizeof(clients));
}

int get_client_slot(void){
	int i;
	for(i = 0; i < MAX_CLIENTS; i++)
		if(!clients[i].ev) 
			return i;
	return -1;
}

void free_client_slot(int slot){
	event_free(clients[slot].ev);
	free(fifo[slot].data);
}

void client_socket_cb(evutil_socket_t sock, short ev, void *arg){
	struct connection_description *cl = (struct connection_description *)arg;
	char buf[BUF_SIZE+1];

	int r = read(sock, buf, BUF_SIZE);
	if(r <= 0) {
		if(r < 0) {
			fprintf(stderr, "Error (%s) while reading data from %s:%d. Closing connection.\n",
				strerror(errno), inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port));
		} else {
			fprintf(stderr, "Connection from %s:%d closed.\n",
				inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port));
		}
		if(event_del(cl->ev) == -1) syserr("Can't delete the event.");
		event_free(cl->ev);
		if(close(sock) == -1) syserr("Error closing socket.");
		cl->ev = NULL;
		return;
	}
	buf[r] = 0;

	printf("%s:%d FIFO: %s", inet_ntoa(cl->address.sin_addr), ntohs(cl->address.sin_port), buf);
	if(write(sock, buf, r) == -1) syserr("bufferevent_write");

}

void tcp_socket_cb(evutil_socket_t sock, short ev, void *arg){
	struct event_base *base = (struct event_base *)arg;

	struct sockaddr_in sin;
	socklen_t addr_size = sizeof(struct sockaddr_in);
	evutil_socket_t connection_socket = accept(sock, (struct sockaddr *)&sin, &addr_size);

	if(connection_socket == -1) syserr("Error accepting connection.");

	int slot = get_client_slot();
	
	if(slot < 0) {
		close(connection_socket);
		fprintf(stderr, "Ignoring connection attempt from %s:%d due to lack of space.\n",
			inet_ntoa(sin.sin_addr), ntohs(sin.sin_port));
		return;
	}
	struct connection_description *cl = &clients[slot];

	memcpy(&(cl->address), &sin, sizeof(struct sockaddr_in));
	cl->sock = connection_socket;
	/*send client ID */
	char buffer[BUF_SIZE];
	memset(buffer, 0, BUF_SIZE);
	sprintf(buffer, "CLIENT %d\n", slot); 
	uint16_t len = strlen(buffer) + 1;
	if (write(connection_socket, buffer, len) != len) 
		free_client_slot(slot);

	memset(fifo[slot], 0, FIFO_SIZE);
	struct event *an_event =
		event_new(base, connection_socket, EV_READ|EV_PERSIST, client_socket_cb, (void *)cl);
	if(!an_event) syserr("Error creating event.");
	cl->ev = an_event;
	if(event_add(an_event, NULL) == -1) syserr("Error adding an event to a base.");
}

void udp_socket_cb(evutil_socket_t sock, short ev, void *arg){
	//TODO
}

int main(int argc, char *argv[])
{ 
	if (signal(SIGINT, catch_int) == SIG_ERR) {
		syserr("Unable to change signal handler\n");
	}
	
	init_clients();

	handle_commandline_args(argc, argv);

	base = event_base_new();
	if(!base) syserr("Error creating base.");

	evutil_socket_t tcp_socket, udp_socket;
	tcp_socket = socket(PF_INET, SOCK_STREAM, 0);
	if(tcp_socket == -1 ||
		 evutil_make_listen_socket_reuseable(tcp_socket) ||
		 evutil_make_socket_nonblocking(tcp_socket)) {
		syserr("Error preparing tcp socket.");
	}
	udp_socket = socket(PF_INET, SOCK_DGRAM, 0);
	if(udp_socket == -1 ||
		 evutil_make_listen_socket_reuseable(udp_socket) ||
		 evutil_make_socket_nonblocking(udp_socket)) {
		syserr("Error preparing udp socket.");
	}

	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	sin.sin_port = htons(PORT);
	if(bind(tcp_socket, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		syserr("bind tcp");
	}
	if(bind(udp_socket, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		syserr("bind udp");
	}
	int rc;
	socklen_t len;
	len = (socklen_t)sizeof(sin);
	rc = getsockname(tcp_socket, (struct sockaddr *)&sin, &len);
	if (rc == -1) syserr("getsockname tcp");

	rc = getsockname(udp_socket, (struct sockaddr *)&sin, &len);
	if (rc == -1) syserr("getsockname udp");

	printf("Listening at port %d\n", (int)ntohs(sin.sin_port));

	if(listen(tcp_socket, 10) == -1) syserr("listen tcp");
	//if(listen(udp_socket, 10) == -1) syserr("listen udp");

	struct event *tcp_socket_event = 
		event_new(base, tcp_socket, EV_READ|EV_PERSIST, tcp_socket_cb, (void *)base);
	if(!tcp_socket_event) syserr("Error creating event for a tcp listener socket.");
	struct event *udp_socket_event = 
		event_new(base, udp_socket, EV_READ|EV_PERSIST, udp_socket_cb, (void *)base);
	if(!udp_socket_event) syserr("Error creating event for a udp listener socket.");

	if(event_add(tcp_socket_event, NULL) == -1) syserr("Error adding tcp_socket event.");

	printf("Entering dispatch loop.\n");
	if(event_base_dispatch(base) == -1) syserr("Error running dispatch loop.");
	printf("Dispatch loop finished.\n");

	event_free(tcp_socket_event);
	event_base_free(base);

	return 0;
}
