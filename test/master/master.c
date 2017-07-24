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
#include <stdbool.h>
#include <errno.h>
#include <libxl.h>
#include <libxl_utils.h>
#include <xenctrl.h>

#define CLOCK_ID CLOCK_MONOTONIC

#define SCHED_POOLID 0
#define VCPU_ID 0

#define max(a,b) ((a > b) ? a : b)

int num_sockets;
int maxfds;
int sock_serv = -1, sock_fds[256];

struct timespec last_stamp[256];

void close_peer_socket(int sockfd)
{
#define BUFSIZE 4096
	int ret;
	char buf[BUFSIZE];

	while ((ret = read(sockfd, buf, BUFSIZE)) > 0);
	close(sockfd);
#undef BUFSIZE
}

void sigint_handler(int signo)
{
	int i;
	uint64_t dummy_period = 0;
	struct sigaction act = {
		.sa_handler = SIG_DFL,
		.sa_flags = 0
	};

	/* send dummy period */
	for ( i = 0; i < num_sockets; i++ ) {
		send(sock_fds[i], &dummy_period, sizeof(dummy_period), 0);
	}
	puts("Cleaning sockets before terminating . . .");
	for ( i = 0; i < num_sockets; i++ ) {
		close_peer_socket(sock_fds[i]);
	}

	if (sock_serv != -1) {
		close(sock_serv);
	}

	puts("Terminating . . .");
	sigaction(signo, &act, NULL);
	raise(signo);
}

void sigint_finish_test(int signo)
{
	int i;
	bool done = true;
	puts("Sending finish packet . . .");
	for ( i = 0; i < num_sockets; ++i ) {
		send(sock_fds[i], &done, sizeof(done), 0);
	}
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
	int vcpu_id;
} sched_provider_t;

typedef struct sched_entry_s {
	int service_id;
	sched_provider_t * providers;
	int num_providers;
	int64_t runtime;
} sched_entry_t;

typedef struct schedule_s {
	uint64_t major_frame;
	sched_entry_t *entries;
	int num_entries;
} schedule_t;

void sched_free(schedule_t * sched)
{
	if (sched->entries != NULL) {
		int i;
		for ( i = 0; i < sched->num_entries; i++ ) {
			if (sched->entries[i].providers != NULL) {
				free(sched->entries[i].providers);
			}
		}
		free(sched->entries);
	}
}

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

char * readall(char *file)
{
	size_t size;
	char * out;
	FILE *fp;
	
	fp = fopen(file, "r");
	if (fp == NULL)
		return NULL;

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	/* +1 for null bytes */
	out = malloc((size+1) * sizeof(char));
	fread(out, sizeof(char), size, fp);
	out[size] = '\0';
	fclose(fp);

	return out;
}
int extract_schedule(json_t *root, schedule_t *sched)
{
	json_t *cur_obj, *arr_entries;
	unsigned int i, j;

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
	sched->entries = calloc(1, sched->num_entries * sizeof(sched_entry_t));

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
		sched->entries[i].providers = calloc(1, sched->entries[i].num_providers * sizeof(sched_provider_t));

		json_array_foreach(arr_providers, j, cur_obj) {
			json_t *cur_provider = cur_obj;

			if (!cur_provider || !json_is_object(cur_provider))
				goto fail;

			cur_obj = json_object_get(cur_provider, "dom_name");
			if (!cur_obj || !json_is_string(cur_obj))
				goto fail;
			sched->entries[i].providers[j].dom_name = json_string_value(cur_obj);
			sched->entries[i].providers[j].vcpu_id = VCPU_ID;
		}
	}

	return 0;

fail:
	sched_free(sched);
	sched = NULL;
	json_decref(root);
	return 1;
}

void setup_master_slave(int sock_master, int num_slaves)
{
	num_sockets = 0;
	maxfds = 0;

	while (num_slaves) {
		int sock_cli;
		struct sockaddr_in addr_cli;
		socklen_t addr_len_cli;

		printf("Waiting for connections from slaves (%d more) . . .\n", num_slaves);
		addr_len_cli = sizeof(addr_cli);
		sock_cli = accept(sock_master, (struct sockaddr *)&addr_cli, &addr_len_cli);
		list_add_socket(sock_cli);

		num_slaves--;
	}
}

char * choose_test_scheme(char *config_base)
{
	int selection, num_opt, len;
	char * config_path;
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

	if (selection == 0)
		return NULL;

	dirp = opendir(config_base);
	while (selection > 0) {
		dir = readdir(dirp);
		if (dir == NULL)
			break;
		if (dir->d_name[0] != '.') {
			--selection;
		}
	}

	if (selection != 0) {
		return NULL;
	}
	else {
		len = strlen(config_base) + strlen(dir->d_name) + 2;
		config_path = malloc(len);
		snprintf(config_path, len, "%s/%s", config_base, dir->d_name);
		closedir(dirp);
	}

	return config_path;
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

	if (last.tv_sec != 0 && last.tv_nsec != 0) {
		delta.tv_sec  = now.tv_sec - last.tv_sec;
		delta.tv_nsec = now.tv_nsec - last.tv_nsec;

		if (delta.tv_nsec < 0) {
			delta.tv_sec  -= 1;
			delta.tv_nsec += 1000 * 1000 * 1000;
		}

		d = ((double)delta.tv_sec) + ((double)delta.tv_nsec) / (1000.0 * 1000.0 * 1000.0);

		printf("Entry %d Delta %.09lfs\n", entry_index, d);
	}
	last_stamp[entry_index] = now;
}

void initfds(fd_set *rfds)
{
	int i;

	FD_ZERO(rfds);
	/* add sockets to fd set */
	for ( i = 0; i < num_sockets; i++ ) {
		if (sock_fds[i] != -1) {
			FD_SET(sock_fds[i], rfds);
		}
	}

}

char * process_socket(int sock_rdy)
{
	ssize_t rdsize;
	uint32_t msgsize;
	char * msg;

	rdsize = recv(sock_rdy, &msgsize, sizeof(msgsize), MSG_WAITALL);
	if (rdsize == 0) {
		/* socket is closed */
		return NULL;
	}
	else {
		assert(rdsize == sizeof(msgsize));

		msg = malloc(msgsize);
		rdsize = recv(sock_rdy, msg, msgsize, MSG_WAITALL);

		if (rdsize == -1)
			return NULL;

		return msg;
	}
}

struct dominfo_t {
    const char *dom_name;
    libxl_uuid uuid;
    domid_t domid;
};

int set_schedule(schedule_t * sched)
{
    struct xen_sysctl_arinc653_schedule a653sched;
    struct dominfo_t *domlist;
    xc_interface *xci;
    libxl_ctx *ctx;
    libxl_dominfo *dominfo;
    int64_t total_runtime = 0;
    int i;
    int ret, num_domains;

    libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL);
    dominfo = libxl_list_domain(ctx, &num_domains);

    domlist = malloc(num_domains * sizeof(*domlist));
    for (i = 0; i < num_domains; i++) {
        const char *name = libxl_domid_to_name(ctx, dominfo[i].domid);
	libxl_uuid_copy(ctx, &domlist[i].uuid, &dominfo[i].uuid);
        domlist[i].dom_name = name;
        domlist[i].domid = dominfo[i].domid;
    }

    for (i = 0; i < sched->num_entries; i++)
        total_runtime += sched->entries[i].runtime;

    if (total_runtime > sched->major_frame)
    	    return 1;

    a653sched.major_frame = sched->major_frame;
    a653sched.num_sched_entries = sched->num_entries;
    for (i = 0; i < sched->num_entries; i++) {
        int j;

        a653sched.sched_entries[i].service_id = sched->entries[i].service_id;
        a653sched.sched_entries[i].runtime = sched->entries[i].runtime;
        a653sched.sched_entries[i].num_providers = sched->entries[i].num_providers;

        for (j = 0; j < sched->entries[i].num_providers; j++) {
            const char *entry_name = sched->entries[i].providers[j].dom_name;
	    int k;

	    for (k = 0; k < num_domains && strcmp(entry_name, domlist[k].dom_name); k++);
            if (k >= num_domains)
                return 1;

            libxl_uuid_copy(ctx, (libxl_uuid *)&a653sched.sched_entries[i].service_providers[j].dom_handle,
                    &domlist[k].uuid);
            a653sched.sched_entries[i].service_providers[j].vcpu_id = sched->entries[i].providers[j].vcpu_id;
        }
    }

    xci = xc_interface_open(NULL, NULL, 0);

    ret = xc_sched_arinc653_schedule_set(xci, SCHED_POOLID, &a653sched);
    if (ret)
        return ret;
    return 0;
}

void do_testing(schedule_t * schedule, fd_set *rfds)
{
	int ret, i;
	struct sigaction oldact;
	struct timeval select_to;
	struct sigaction act = {
		.sa_handler = sigint_finish_test,
		.sa_flags = 0
	};
	int timeout_ns = schedule->major_frame * 10;
	const struct timeval timeout = {
		.tv_sec = timeout_ns / ( 1000 * 1000 * 1000 ),
		.tv_usec = (timeout_ns % ( 1000 * 1000 * 1000 )) / 1000
	};
	
	/* testing banner */
	puts("Start testing . . .");

	/* install new handler for SIGINT. SIGINT now cancel
	 * the test, but not terminate the process*/
	sigaction(SIGINT, &act, &oldact);

	/* reset time stamp */
	for ( int i = 0; i < schedule->num_entries; i++ ) {
		last_stamp[i].tv_sec = 0;
		last_stamp[i].tv_nsec = 0;
	}

	/* set schedule */
	ret = set_schedule(schedule);
	
	if (ret)
		return;

	/* send period to slaves */
	for ( i = 0; i < num_sockets; i++ ) {
		send(sock_fds[i], &schedule->major_frame, sizeof(schedule->major_frame), 0);
	}

	/* receive heartbeat from slaves
	 * from select(2), timeout is undefined after select returns
	 * we should reinitialize timeout before using select again */
	i = 0;
	while ( select_to = timeout, initfds(rfds),
		ret = select(maxfds+1, rfds, NULL, NULL, &select_to), ret > 0 ) {

		/* client sockets ready */
		for (i = 0; i < num_sockets; i++) {
			if (FD_ISSET(sock_fds[i], rfds)) {
				int entry_idx;
				char * hostname;
				hostname = process_socket(sock_fds[i]);

				if (hostname == NULL) {
					sock_fds[i] = -1;
					continue;
				}

				entry_idx = lookup_service_provider(hostname, schedule);
				logtime(entry_idx);

				free(hostname);
			}
		}
	}

	/* reinstall previous handler */
	if (ret == 0 || errno == EINTR)
		sigaction(SIGINT, &oldact, NULL);
	else
		perror("");
}

int main(int argc, char *argv[])
{
	int num_slaves;
	char * config_base;
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
	setup_master_slave(sock_serv, num_slaves);

	for (;;) {
		fd_set rfds;
		json_t * handle;
		json_error_t error;
		schedule_t * schedule;
		char *config_path, *config;
		int ret;

		config_path = choose_test_scheme(config_base);

		if (config_path == NULL) {
			goto fail;
		}

		config = readall(config_path);

		printf("%s\n", config);
		
		if (!config) {
			fprintf(stderr, "cannot read from %s\n", config_path);
			goto fail;
		}

		handle = json_loads(config, 0, &error);

		if (!handle) {
			fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
			goto fail;
		}

		schedule = calloc(1, sizeof(*schedule));

		ret = extract_schedule(handle, schedule);

		if (ret) {
			fprintf(stderr, "cannot parse config on file %s.\n", config_path);
			sched_free(schedule);
			goto fail;
		}

		do_testing(schedule, &rfds);

		json_decref(handle);
		sched_free(schedule);
		free(config);
		free(config_path);
	}

	return 0;
fail:
	
	raise(SIGINT);
	return 1;
}
