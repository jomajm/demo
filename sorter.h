#ifndef __demo__
#define demo

#define MYPORT 55555
#define ASIO_MAX_FDS 128

struct client
{
	int fd, ip;

	char *rtbuff;
	size_t rtposition;
	size_t rtsize;

	unsigned int access_time;
	int can_close; // can close connection after transfer ?
	int send_done;
};

void sock_send(struct client *cl, char *s, unsigned int num);
void sock_recv(struct client *cl, char *s, unsigned int num);

#endif //demo
