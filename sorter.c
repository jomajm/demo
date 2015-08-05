#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "sorter.h"

int epoll_fd;

unsigned int NumClients = 0, time_wait = -1;
struct client *klient[ASIO_MAX_FDS + 3];

int cmpfunc(const void * a, const void * b)
{
	return (*(int*) a - *(int*) b);
}

void parse(char *mystr, char *outstr)
{
	int i;
	unsigned int *numbers;

	int length = strlen(mystr);
	int number_count;

	for (i = 0; i < length; i++)
		if (mystr[i] == ' ')
			mystr[i] = 0;

	number_count = atoi(mystr);
	mystr += strlen(mystr) + 1;
	numbers = (unsigned int *) malloc(sizeof(unsigned int) * number_count);

	for (i = 0; i < number_count; i++)
	{
		numbers[i] = atoi(mystr);
		mystr += strlen(mystr) + 1;
	}

	qsort(numbers, number_count, sizeof(unsigned int), cmpfunc);

	int j = sprintf((char *) outstr, "%u ", number_count);
	for (i = 0; i < number_count; i++)
	{
		outstr += j;
		j = sprintf((char *) outstr, "%u ", numbers[i]);
	}
	outstr += (j - 1);
	*outstr++ = '\n';
	*outstr = 0;
}

void signal_pipe(int signal)
{
}

void signal_stop(int signal)
{
	printf("\nExiting !!! \n");
	exit(0);
}

void send_done_callback(struct client *cl)
{
	struct epoll_event ev;
	struct in_addr a;

	int pos = cl->fd;
	cl->send_done = 1;
	free(cl->rtbuff);
	cl->rtbuff = NULL;
	cl->rtsize = cl->rtposition = 0;

	if (!cl->can_close)
	{
		return;
	}

	if (cl->fd < 0 || cl->fd >= ASIO_MAX_FDS)
		return;

	ev.data.fd = cl->fd;
	ev.events |= EPOLLIN | EPOLLPRI | EPOLLOUT;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, cl->fd, &ev);

	a.s_addr = klient[cl->fd]->ip;
	printf("Closing connection from %s fd: %u\n", inet_ntoa(a), klient[cl->fd]->fd);

	close(cl->fd);

	free(cl);
	NumClients--;
	klient[pos] = NULL;
}

void line_break_callback(struct client *cl)
{
	cl->can_close++;
	send_done_callback(cl);
}

void recv_data_callback(struct client *cl)
{
	if (cl->rtbuff[cl->rtposition - 1] == '\n')
	{
		parse(cl->rtbuff, cl->rtbuff);
		sock_send(cl, cl->rtbuff, strlen(cl->rtbuff));
	}
}

void sock_send(struct client *cl, char *s, unsigned int num)
{
	struct epoll_event ev;

	ev.data.fd = cl->fd;
	ev.events = EPOLLOUT | EPOLLERR | EPOLLHUP;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cl->fd, &ev) != 0)
	{
		perror("epoll_ctl()");
	}

	cl->rtsize = num;
	cl->rtbuff = s;
	cl->rtposition = 0;
	cl->send_done = 0;
}

void sock_recv(struct client *cl, char *s, unsigned int num)
{
	struct epoll_event ev;

	ev.data.fd = cl->fd;
	ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cl->fd, &ev) != 0)
	{
		perror("epoll_ctl()");
	}

	if (s == NULL)
		s = calloc(1, num);

	cl->rtsize = num;
	cl->rtbuff = s;
	cl->rtposition = 0;
}

void handle_send(struct client *cl)
{
	int numwritten;

	//numwritten = write(cl->fd, cl->rtbuff + cl->rtposition, cl->rtsize - cl->rtposition);
	numwritten = write(cl->fd, cl->rtbuff + cl->rtposition, 1);

	if (numwritten == -1)
	{
		line_break_callback(cl);
		return;
	}

	cl->rtposition += numwritten;

	if (cl->rtposition == cl->rtsize)
	{
		struct epoll_event ev;

		ev.data.fd = cl->fd;
		ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;

		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, cl->fd, &ev) != 0)
		{
			perror("epoll_ctl()");
		}
		send_done_callback(cl);
	}
}

void handle_recv(struct client *cl)
{
	int numread;

	if (cl->rtsize - cl->rtposition < 10)
	{
		cl->rtbuff = realloc(cl->rtbuff, cl->rtsize + 10);
		cl->rtsize += 10;
	}

	numread = read(cl->fd, cl->rtbuff + cl->rtposition, 10);
	cl->rtposition += numread;

	if (numread <= 0)
		line_break_callback(cl); // connection broken
	else
		recv_data_callback(cl);
}

int main(void)
{
	int i, temp, sockfd, maxfds;
	int sin_size = sizeof(struct sockaddr_in);
	struct sockaddr_in my_addr, their_addr;
	unsigned int tv = 0;
	struct rlimit rlim;
	struct epoll_event ev;
	struct epoll_event epoll_events[ASIO_MAX_FDS];
	int numevents;

	printf("Initializing...\n");
	getrlimit(RLIMIT_NOFILE, &rlim);
	maxfds = (int) rlim.rlim_max;
	printf("starting with %i fds available...\n", maxfds);

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(1);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &my_addr, sizeof(int)) == -1)
	{
		perror("setsockopt");
		exit(1);
	}

	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(MYPORT);
	my_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
	memset(&(my_addr.sin_zero), '\0', 8); // zero the rest of the struct

	if (bind(sockfd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr)) == -1)
	{
		perror("bind");
		exit(1);
	}

	if (listen(sockfd, 0xffff) == -1)
	{
		perror("listen");
		exit(1);
	}

	if ((epoll_fd = epoll_create(ASIO_MAX_FDS)) == -1)
	{
		perror("epoll_create()");
		exit(1);
	}

	ev.data.fd = sockfd;
	ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) != 0)
	{
		perror("epoll_ctl()");
		exit(1);
	}

	memset(klient, 0, ASIO_MAX_FDS + 10);

	signal(SIGINT, signal_stop);
	signal(SIGPIPE, signal_pipe);

	printf("Core started, ready to serve requests...\n");

	while (1)
	{
		numevents = epoll_wait(epoll_fd, &epoll_events, ASIO_MAX_FDS, -1);

		for (i = 0; i < numevents; i++)
		{
			tv = time(NULL);

			// accept new connections
			if ((epoll_events[i].events & (EPOLLIN | EPOLLPRI)) && (epoll_events[i].data.fd == sockfd))
			{
				NumClients++;
				temp = accept(sockfd, (struct sockaddr *) &their_addr, &sin_size);
				if (temp == -1)
				{
					perror("accept");
				}
				if (temp > ASIO_MAX_FDS)
				{
					printf("not enough fds ! ASIO_MAX_FDS==%i\n", ASIO_MAX_FDS);
					close(temp);
					NumClients--;
					continue;
				}

				ev.events = EPOLLIN | EPOLLPRI | EPOLLERR | EPOLLHUP;
				ev.data.fd = temp;
				if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, temp, &ev) != 0)
				{
					perror("epoll_ctl()");
					exit(1);
				}

				klient[temp] = (struct client *) calloc(1, sizeof(struct client));
				klient[temp]->fd = temp;
				klient[temp]->ip = their_addr.sin_addr.s_addr;
				printf("connection from %s fd: %u\n", inet_ntoa(their_addr.sin_addr), temp);
				sock_recv(klient[temp], klient[temp]->rtbuff, 10);
				klient[temp]->access_time = tv;
				continue;
			}

			// handle writes
			if (epoll_events[i].events & EPOLLOUT)
			{
				klient[epoll_events[i].data.fd]->access_time = tv;
				handle_send(klient[epoll_events[i].data.fd]);
				continue;
			}

			// handle reads
			if (epoll_events[i].events & (EPOLLIN | EPOLLPRI))
			{
				klient[epoll_events[i].data.fd]->access_time = tv;
				handle_recv(klient[epoll_events[i].data.fd]);
				continue;
			}
		}
	}
	return 0;
}
