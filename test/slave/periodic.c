#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#define SIGRT1 SIGRTMIN

#define CLOCK_ID CLOCK_MONOTONIC

int sock_master;

void close_peer_socket(int sockfd)
{
	close(sockfd);
}

void sigint_handler(int signo)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL,
		.sa_flags = 0
	};

	puts("Cleaning sockets . . .");
	close_peer_socket(sock_master);

	puts("Terminating . . .");
	sigaction(signo, &act, NULL);
	raise(signo);
}

int connect_to_master(char * address, int port)
{
	int sock_master;
	int ret;

	sock_master = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr_master;

	addr_master.sin_family = AF_INET;
	addr_master.sin_port = htons(port);
	inet_aton(address, &addr_master.sin_addr);

	while ( (ret = connect(sock_master, (const struct sockaddr *)&addr_master,
                             sizeof(addr_master))) == -1) {
                usleep(100 * 1000);
                if (errno == ECONNREFUSED) {
                	return -1;
                }
        }

	return sock_master;

}

char hostname[256];
struct routine_t {
	timer_t timerid;
	int sockfd;
	uint32_t len_hostname;
	char * hostname;
	bool done;
};

void routine(int signo, siginfo_t *siginfo, void *ctx)
{
	struct routine_t * routine_data = ((struct routine_t *)siginfo->si_value.sival_ptr);
	static long long val = 0;
        static long long total_overrun = 0;
	struct timespec now;
	send(routine_data->sockfd, &routine_data->len_hostname,
                   sizeof(routine_data->len_hostname), MSG_MORE);
	send(routine_data->sockfd, routine_data->hostname, routine_data->len_hostname, 0);
	clock_gettime(CLOCK_ID, &now);
        total_overrun += timer_getoverrun(routine_data->timerid);
	printf("Send heartbeat [%lld] (%lld overrun(s) happened) at time %ld.%09ld\n",
                val++, total_overrun, now.tv_sec, now.tv_nsec);
}

struct routine_t * setup_periodic_heartbeat(int master_socket)
{
	int ret;
	timer_t timerid;
	struct routine_t * routine_data = malloc(sizeof(*routine_data));
	struct sigevent sevp = {
		.sigev_notify          = SIGEV_SIGNAL,
		.sigev_signo           = SIGRT1,
		.sigev_value           = { .sival_ptr = (void *)routine_data }
	};
	struct sigaction act = {
		.sa_sigaction = routine,
		.sa_flags = SA_SIGINFO
	};

	sigaction(SIGRT1, &act, NULL);

	ret = timer_create(CLOCK_ID, &sevp, &timerid);

	if (ret == -1) {
		fprintf(stderr, "Error creating timer.\n");
		return NULL;
	}

	/* initialize routine data */
	gethostname(hostname, 256);
	routine_data->timerid = timerid;
	routine_data->sockfd = master_socket;
	routine_data->hostname = hostname;
	routine_data->len_hostname = strlen(routine_data->hostname)+1;
	routine_data->done = false;

	return routine_data;
}

void periodic_heartbeat(timer_t timerid, int64_t period)
{
	int ret;
	sigset_t mask, oldmask;
	const struct timespec interval = {
		.tv_sec  = period / (1000L * 1000L * 1000L),
		.tv_nsec = period % (1000L * 1000L * 1000L)
	};
	const struct itimerspec timersp = {
		.it_interval = interval,
		/* the timer should right now, but giving zeroes to it_value means
		 * disarming the timer. thus we let 1 nsec to pass before setting
		 * up the timer. */
		.it_value    = {
			.tv_sec  = 0,
			.tv_nsec = 1
		}
	};

	printf("Sending heartbeat every %lu\n", period);
	sigemptyset(&mask);
	sigaddset(&mask, SIGRT1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	ret = timer_settime(timerid, 0, &timersp, NULL);

	if (ret == -1) {
		fprintf(stderr, "Error setting timer interval.\n");
		return;
	}

	sigprocmask(SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char *argv[])
{
	struct sigaction act = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};
	struct routine_t * handle;
	struct sched_param param;
	int prio;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [server address] [server port]\n", argv[0]);
		return 1;
	}

	prio = sched_get_priority_min(SCHED_FIFO);
	param.sched_priority = prio;
	sched_setscheduler(0, SCHED_FIFO, &param);

	sigaction(SIGINT, &act, NULL);

	sock_master = connect_to_master(argv[1], atoi(argv[2]));

	if (sock_master == -1) {
		fprintf(stderr, "cannot connect to %s:%s\n", argv[1], argv[2]);
		perror("");
		return 1;
	}

	handle = setup_periodic_heartbeat(sock_master);

	for (;;) {
		int rdsize;
		uint64_t period;

		rdsize = recv(sock_master, &period, sizeof(period), MSG_WAITALL);

		if (rdsize == 0) {
			puts("Socket closed abruptly");
			break;
		}
		else if (rdsize == -1) {
			perror("");
			break;
		}
		else {
			bool done = false;
			if (period == 0)
				break;

			periodic_heartbeat(handle->timerid, period);

			while (!handle->done) {
				rdsize = recv(sock_master, &done, sizeof(done), MSG_WAITALL);
				if (rdsize != -1) {
					struct itimerspec disarm = {0};
					puts("Disarming heartbeat");
					timer_settime(handle->timerid, 0, &disarm, NULL);
					assert(done);
					break;
				}
			}
		}
	}

	close_peer_socket(sock_master);

	return 0;
}
