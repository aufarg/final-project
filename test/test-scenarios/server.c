#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/ip.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <jansson.h>
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#define CLOCK_ID CLOCK_MONOTONIC

#define max(a,b) ((a > b) ? a : b)

int num_sockets;
int maxfds;
int sock_serv, sock_fds[256];

struct timespec last_stamp[256];

void close_socket(int sockfd)
{
#define BUFSIZE 4096
	char buf[BUFSIZE];

	shutdown(sockfd, SHUT_RDWR);
	while (!read(sockfd, buf, BUFSIZE));
	close(sockfd);
}

void sigint_handler(int signo)
{
	int i;
	struct sigaction act = {
		.sa_handler = SIG_DFL,
		.sa_flags = 0
	};

	puts("Cleaning sockets before terminating . . .");
	close_socket(sock_serv);
	for ( i = 0 ; i < num_sockets; i++ )
		close_socket(sock_serv);

	puts("Terminating . . .");
	sigaction(signo, &act, NULL);
	raise(signo);
}

void list_add_socket(int sockfd)
{
	maxfds = max(maxfds, sockfd);
	sock_fds[num_sockets] = sockfd;
	num_sockets++;
}

int socket_init(void)
{
	int sock_serv;
	const int num_backlogs = 300;
	struct sockaddr_in addr_serv;

	sock_serv = socket(AF_INET, SOCK_STREAM, 0);

	addr_serv.sin_family      = AF_INET;
	addr_serv.sin_port        = htons(33333);
	addr_serv.sin_addr.s_addr = INADDR_ANY;

	while (bind(sock_serv, (const struct sockaddr *)&addr_serv, sizeof(addr_serv)) == -1) {
		usleep(100 * 1000);
	}
	listen(sock_serv, num_backlogs);

	return sock_serv;
}

typedef struct sched_provider_s {
	const char *dom_name;
} sched_provider_t;

typedef struct sched_entry_s {
	int service_id;
	sched_provider_t * providers;
	int num_providers;
	int64_t runtime;
} sched_entry_t;

typedef struct schedule_s {
	int64_t major_frame;
	sched_entry_t *entries;
	int num_entries;
} schedule_t;

int lookup_service_provider(const char * hostname, schedule_t * sched)
{
	int i, j, num_entry = sched->num_entries;

	for (i = 0; i < num_entry; ++i) {
		int num_providers = sched->entries[i].num_providers;
		for (j = 0; j < num_providers; ++j) {
			if (!strcmp(hostname, sched->entries[i].providers[j].dom_name)) {
				return i;
			}
		}
	}
	return -1;

}

int extract_config(schedule_t *sched, char *config_path)
{
	json_t *root, *cur_obj, *arr_entries;
	json_error_t error;
	char *config_json;
	size_t size;
	unsigned int i, j;

	FILE *fp = fopen(config_path, "r");
	if (fp == NULL)
		return 1;
	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	config_json = malloc(size * sizeof(char));
	fread(config_json, sizeof(char), size, fp);

	root = json_loads(config_json, 0, &error);
	free(config_json);

	if (!root) {
		fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
		return 1;
	}

	memset(sched, 0, sizeof(schedule_t));

	/* get major time frame */
	cur_obj = json_object_get(root, "major_frame");
	if (!cur_obj || !json_is_integer(cur_obj))
		goto fail;
	sched->major_frame = json_integer_value(cur_obj);

	/* get schedule list */
	cur_obj = json_object_get(root, "entries");
	if (!cur_obj || !json_is_array(cur_obj))
		goto fail;
	arr_entries = cur_obj;

	sched->num_entries = json_array_size(arr_entries);
	sched->entries = malloc(sched->num_entries * sizeof(sched_entry_t));

	json_array_foreach(arr_entries, i, cur_obj) {
		json_t *cur_entry = cur_obj;
		json_t *arr_providers;
		if (!cur_entry || !json_is_object(cur_entry))
			goto fail;

		cur_obj = json_object_get(cur_entry, "service_id");
		if (!cur_obj || !json_is_integer(cur_obj))
			goto fail;
		sched->entries[i].service_id = json_integer_value(cur_obj);

		cur_obj = json_object_get(cur_entry, "runtime");
		if (!cur_obj || !json_is_integer(cur_obj))
			goto fail;
		sched->entries[i].runtime = json_integer_value(cur_obj);

		cur_obj = json_object_get(cur_entry, "providers");
		if (!cur_obj || !json_is_array(cur_obj))
			goto fail;
		arr_providers = cur_obj;

		sched->entries[i].num_providers = json_array_size(arr_providers);
		sched->entries[i].providers = malloc(sched->entries[i].num_providers * sizeof(sched_provider_t));

		json_array_foreach(arr_providers, j, cur_obj) {
			json_t *cur_provider = cur_obj;

			if (!cur_provider || !json_is_object(cur_provider))
				goto fail;

			cur_obj = json_object_get(cur_provider, "dom_name");
			if (!cur_obj || !json_is_string(cur_obj))
				goto fail;
			sched->entries[i].providers[j].dom_name = json_string_value(cur_obj);
		}
	}

	return 0;

fail:
	if (sched->entries)
		free(sched->entries);
	free(sched);
	sched = NULL;
	return 1;
}

void setup_master_slave(int sock_master, int num_slaves, fd_set *rfds)
{
	FD_ZERO(rfds);
	num_sockets = 0;
	maxfds = 0;

	while (num_slaves) {
		int sock_cli;
		struct sockaddr_in addr_cli;
		socklen_t addr_len_cli = sizeof(addr_cli);

		printf("Waiting for connections from slaves (%d more) . . .\n", num_slaves);
		sock_cli = accept(sock_master, (struct sockaddr *)&addr_cli, &addr_len_cli);
		list_add_socket(sock_cli);
		FD_SET(sock_cli, rfds);

		num_slaves--;
	}
}

schedule_t * choose_test_scheme(char * config_base)
{
	schedule_t * schedule;
	int ret, selection, num_opt;
	char config_path[512];
	DIR * dirp;
	struct dirent * dir;

	dirp = opendir(config_base);
	if (dirp == NULL) {
		fprintf(stderr, "Error opening directory: %s.\n", config_base);
		return NULL;
	}

	num_opt = 0;
	while ( (dir = readdir(dirp)) != NULL ) {
		if (dir->d_name[0] != '.') {
			++num_opt;
			printf("%d. %s\n", num_opt, dir->d_name);
		}
	}
	closedir(dirp);

	printf("Selection: ");
	scanf("%d", &selection);

	dirp = opendir(config_base);
	while (selection > 0) {
		dir = readdir(dirp);
		if (dir->d_name[0] != '.') {
			--selection;
		}
	}

	sprintf(config_path, "%s/%s", config_base, dir->d_name);

	schedule = malloc(sizeof(*schedule));
	ret = extract_config(schedule, config_path);

	if (ret) {
		fprintf(stderr, "cannot parse config on file %s.\n", config_path);
		free(schedule);
		return NULL;
	}

	return schedule;
}

void logtime(int entry_index)
{
	int ret;
	double d;
	struct timespec now, last, delta;
	ret = clock_gettime(CLOCK_ID, &now);
	if (ret == -1) {
		fprintf(stderr, "Error getting current time.\n");
		return;
	}
	last = last_stamp[entry_index];

	delta.tv_sec  = now.tv_sec - last.tv_sec;
	delta.tv_nsec = now.tv_nsec - last.tv_nsec;

	if (delta.tv_nsec < 0) {
		delta.tv_sec  -= 1;
		delta.tv_nsec += 1000 * 1000 * 1000;
	}

	d = ((double)delta.tv_sec) + ((double)delta.tv_nsec) / (1000.0 * 1000.0 * 1000.0);

	printf("Entry %d Delta %.09lfs\n", entry_index, d);
	last_stamp[entry_index] = now;
}

void do_testing(schedule_t * schedule, fd_set *rfds)
{
	int ret, i;
	
	puts("Start testing . . .");
	for ( i = 0; i < num_sockets; i++ ) {
		write(sock_fds[i], &schedule->major_frame, sizeof(schedule->major_frame));
	}

	i = 0;
	while ( (ret = select(maxfds+1, rfds, NULL, NULL, NULL)) > 0 ) {
		int sock_cli;
		ssize_t rdsize;
		uint32_t msgsize;
		char * hostname;
		int entry_idx;

		for (;; i = (i+1) % num_sockets) {
			if (FD_ISSET(sock_fds[i], rfds)) {
				break;
			}
		}

		sock_cli = sock_fds[i];
		/* one of client sockets ready */
		rdsize = recv(sock_cli, &msgsize, sizeof(msgsize), MSG_WAITALL);
		if (rdsize == 0) {
			/* socket is closed */
			FD_CLR(sock_fds[i], rfds);
		}
		else {
			assert(rdsize == sizeof(msgsize));

			hostname = malloc(msgsize);
			rdsize = recv(sock_cli, hostname, msgsize, MSG_WAITALL);
			assert(rdsize == msgsize);

			entry_idx = lookup_service_provider(hostname, schedule);
			logtime(entry_idx);
		}

	}

}

int main(int argc, char *argv[])
{
	int num_slaves;
	char * config_base;
	fd_set rfds;
	struct sigaction act = {
		.sa_handler = sigint_handler,
		.sa_flags = 0
	};

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [slaves number] [config base path]", argv[0]);
		return 1;
	}

	sigaction(SIGINT, &act, NULL);

	num_slaves = atoi(argv[1]);
	config_base = argv[2];

	sock_serv = socket_init();
	setup_master_slave(sock_serv, num_slaves, &rfds);

	for (;;) {
		schedule_t * schedule = choose_test_scheme(config_base);
		if (schedule == NULL)
			return 1;

		do_testing(schedule, &rfds);
		free(schedule);
	}

	return 0;
}
