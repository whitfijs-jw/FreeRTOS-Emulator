
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>

#include "udp.h"

#include "task.h"
#include "semphr.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%s:%d: ", __func__, __LINE__);        \
			perror(#x);                                            \
			exit(-1);                                              \
		}                                                              \
	} while (0)

typedef struct async_callback {
	int fd;
	QueueHandle_t queue;
	struct async_callback *next;
	int type;
	int protocol;
	struct sockaddr_in addr;
} async_callback_t;

struct callback_registry {
	SemaphoreHandle_t lock;
	async_callback_t *head;
	unsigned char initialized : 1;
} cr = { 0 };

void addAsyncCallback(async_callback_t *cb)
{
	async_callback_t *iterator;

	for (iterator = (async_callback_t *)&cr.head; iterator->next;
	     iterator = iterator->next)
		;

	iterator->next = cb;
}

//TODO delete cb

void sigactionHandler(int signal, siginfo_t *info, void *context)
{
	async_callback_t *iterator;
	int fd;
	ssize_t read_len;
	char rx[200];
	portBASE_TYPE false = pdFALSE;

	CHECK(info);

	/** socket file descriptor */
	fd = info->si_fd;

	/** find appropriate udp socket callback structure */
	xSemaphoreTake(cr.lock, portMAX_DELAY);
	for (iterator = (async_callback_t *)&cr.head;
	     iterator->next && (iterator->fd != fd); iterator = iterator->next)
		;

	read_len = read(fd, &rx, 200);

	if (read_len && iterator->queue)
		for (unsigned int i = 0; i < read_len; i++)
			xQueueSendFromISR(iterator->queue, &rx[i], &false);

	xSemaphoreGive(cr.lock);
}

void udpInit(void)
{
	struct sigaction sa = { 0 };

	if (!cr.lock) {
		cr.lock = xSemaphoreCreateMutex();

		CHECK(cr.lock);

		/** register signal handler for incomming udp signals */
		sa.sa_flags = SA_SIGINFO;
		sa.sa_sigaction = sigactionHandler;

		/** only interested in SIGIO */
		sigfillset(&sa.sa_mask);
		sigdelset(&sa.sa_mask, SIGIO);

		CHECK(!sigaction(SIGIO, &sa, NULL));

		cr.initialized = 1;
	}
}

void makeAsync(int *fd)
{
	int file_status = fcntl(*fd, F_GETFL);

	CHECK(file_status);

	file_status |= O_ASYNC | O_NONBLOCK;

	/** set async flags */
	CHECK(!fcntl(*fd, F_SETFL, file_status));

	/** set I/O signal to SIGIO */
	CHECK(!fcntl(*fd, F_SETSIG, SIGIO));

	/** send I/O signals to current process */
	CHECK(!fcntl(*fd, F_SETOWN, getpid()));
}

void udpOpenSocket(char *ip, unsigned short port, int con_type,
		   xQueueHandle queue)
{
	//TODO error handling that doesn't exit
	taskENTER_CRITICAL();

	async_callback_t *cb = calloc(1, sizeof(async_callback_t));

	CHECK(cb);

	cb->queue = queue;

	if (con_type == SOCKET_TYPE_TCP) {
		cb->type = SOCK_STREAM;
		cb->protocol = IPPROTO_TCP;
	} else {
		cb->type = SOCK_DGRAM;
		cb->protocol = IPPROTO_UDP;
	}

	cb->addr.sin_family = AF_INET;
	if (ip)
		cb->addr.sin_addr.s_addr = inet_addr(ip);
	else
		cb->addr.sin_addr.s_addr = INADDR_ANY;
	cb->addr.sin_port = htons((in_port_t)port);

	CHECK(cb->fd = socket(cb->addr.sin_family, cb->type, 0));

	makeAsync(&cb->fd);

	CHECK(!bind(cb->fd, (struct sockaddr *)&cb->addr, sizeof(cb->addr)));

	addAsyncCallback(cb);

	taskEXIT_CRITICAL();
}
