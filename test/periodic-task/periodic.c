#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define CLOCK_ID CLOCK_MONOTONIC
#define _STRINGIFY(s) #s
#define STRINGIFY(s) _STRINGIFY(s)
#define MILLISECS(s) ((s) * 1000 * 1000)

#define MAX_SAMPLE_SIZE 100000

#ifdef STATS
#include <gsl/gsl_statistics_double.h>
#define _STAT_VARNAME(_var) _stats##_var
#define _STAT_IDXNAME(_var) _STAT_VARNAME(_idx##_var)

#define DECLARE_STATVAR(_var) \
    static double _STAT_VARNAME(_var)[MAX_SAMPLE_SIZE]; \
    static size_t _STAT_IDXNAME(_var) = 0

#define PUT_STATVAR(_var,_val) \
    _STAT_VARNAME(_var)[ _STAT_IDXNAME(_var) ] = (_val); \
    _STAT_IDXNAME(_var)++

#define STAT_VARNAME(_var) _STAT_VARNAME(_var)
#define STAT_VARSIZE(_var) _STAT_IDXNAME(_var)

#define _VAR_GSLARGS(_var) STAT_VARNAME(_var), 1, STAT_VARSIZE(_var)

#define GET_STATVAR(_msg, _var) \
    printf(_msg ": avg = %lf, var = %lf, sd = %lf, min %lf, max = %lf\n", \
            gsl_stats_mean(_VAR_GSLARGS(_var)), \
            gsl_stats_variance(_VAR_GSLARGS(_var)), \
            gsl_stats_sd(_VAR_GSLARGS(_var)), \
            gsl_stats_min(_VAR_GSLARGS(_var)), \
            gsl_stats_max(_VAR_GSLARGS(_var)))
#else
#define DECLARE_STATVAR(_var)
#define PUT_STATVAR(_var, _val)
#define GET_STATVAR(_msg, _var)
#endif

DECLARE_STATVAR(deltatime);

void end()
{
#ifdef STATS
	puts("Statistics:");
	GET_STATVAR("delta (s)", deltatime);
#endif
}

void install_sigusr1()
{
	struct sigaction act = {
		.sa_handler = end
	};
	sigaction(SIGUSR1, &act, NULL);
}

struct routine_t {
	timer_t timerid;
	struct timespec saved_timesp;
	int samples;
};

void routine(union sigval val)
{
	int ret;

	struct routine_t * routine_data = ((struct routine_t *)val.sival_ptr);
	struct timespec saved_timesp = routine_data->saved_timesp;
	struct timespec timesp, delta;
	ret = clock_gettime(CLOCK_ID, &timesp);

	if (ret == -1) {
		fprintf(stderr, "Error getting current time.\n");
		return;
	}
	
	delta.tv_sec  = timesp.tv_sec - saved_timesp.tv_sec;
	delta.tv_nsec = timesp.tv_nsec - saved_timesp.tv_nsec;

	if (delta.tv_nsec < 0) {
		delta.tv_sec  -= 1;
		delta.tv_nsec += 1000 * 1000 * 1000;
	}
	/* save time */
	routine_data->saved_timesp = timesp;

	/* stats */
#ifdef STATS
	double d;
	d = ((double)delta.tv_sec) + ((double)delta.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
#endif
	PUT_STATVAR(deltatime, d);
	routine_data->samples--;

	printf("Current time = %ld.%09lds with delta = %ld.%09lds.\n", timesp.tv_sec, timesp.tv_nsec, delta.tv_sec, delta.tv_nsec);

	if (!routine_data->samples) {
		struct itimerspec disarm;
		memset(&disarm, 0, sizeof(disarm));

		ret = timer_settime(routine_data->timerid, 0, &disarm, NULL);

		if (ret == -1) {
			fprintf(stderr, "Error disarming timer.\n");
			return;
		}

		free(routine_data);
		kill(getpid(), SIGUSR1);
	}
}

int create_periodic_task(int num_samples, int period)
{
	int ret, flags = 0;
	timer_t timerid;
	struct routine_t * routine_data = malloc(sizeof(struct routine_t));
	struct sigevent sevp = {
		.sigev_notify          = SIGEV_THREAD,
		.sigev_notify_function = routine,
		.sigev_value           = { .sival_ptr = (void *)routine_data }
	};
	const struct timespec interval = {
		.tv_sec  = 0,
		.tv_nsec = MILLISECS(period)
	};
	const struct itimerspec timersp = {
		.it_interval = interval,
		.it_value    = interval
	};
	struct itimerspec saved_timersp;

	if (num_samples > MAX_SAMPLE_SIZE) {
		fprintf(stderr, "Too many samples specified.\n");
		return -1;
	}

	ret = timer_create(CLOCK_ID, &sevp, &timerid);

	if (ret == -1) {
		fprintf(stderr, "Error creating timer.\n");
		return -1;
	}

	/* initialize routine data */
	routine_data->timerid = timerid;
	routine_data->samples = num_samples;
	ret = clock_gettime(CLOCK_ID, &routine_data->saved_timesp);

	if (ret == -1) {
		fprintf(stderr, "Error cannot get current time.\n");
		return -1;
	}

	ret = timer_settime(timerid, flags, &timersp, &saved_timersp);

	if (ret == -1) {
		fprintf(stderr, "Error settings timer interval.\n");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int num_samples, period;
	int ret;
	sigset_t mask, oldmask;

	install_sigusr1();

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [period] [num_samples]\n", argv[0]);
		return 1;
	}

	period = atoi(argv[1]);
	num_samples = atoi(argv[2]);

	struct timespec res;
	clock_getres(CLOCK_ID, &res);
	printf("clock resolution = %ld.%09lds.\n", res.tv_sec, res.tv_nsec);

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	ret = create_periodic_task(num_samples, period);

	if (!ret) {
		sigsuspend(&oldmask);
		return 0;
	}
	else {
		return 1;
	}
}
