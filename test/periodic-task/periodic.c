#define _POSIX_C_SOURCE 199309L

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>

#define SIGRT1 SIGRTMIN
#define MAXFSIZE 8192

#define CLOCK_ID CLOCK_MONOTONIC

#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_rstat.h>

gsl_rstat_workspace * acc_delta;
gsl_rstat_workspace * acc_error;
gsl_rstat_workspace * acc_drift;

char pathbuf[4096];
const char * logpath;
FILE * fp = NULL;

void end(int signo)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL
	};

	sprintf(pathbuf, "%s.stat", logpath);
	FILE * f = fopen(pathbuf,"w");

	fprintf(f, "Statistics:\n");
	fprintf(f, "delta(s): mean=%.09lf, median=%0.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_delta),
			gsl_rstat_median(acc_delta),
			gsl_rstat_sd(acc_delta),
			gsl_rstat_min(acc_delta),
			gsl_rstat_max(acc_delta),
			gsl_rstat_n(acc_delta));

	fprintf(f, "error(s): mean=%.09lf, median=%0.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_error),
			gsl_rstat_median(acc_error),
			gsl_rstat_sd(acc_error),
			gsl_rstat_min(acc_error),
			gsl_rstat_max(acc_error),
			gsl_rstat_n(acc_error));
	fprintf(f, "drift(s): mean=%.09lf, median=%.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_drift),
			gsl_rstat_median(acc_drift),
			gsl_rstat_sd(acc_drift),
			gsl_rstat_min(acc_drift),
			gsl_rstat_max(acc_drift),
			gsl_rstat_n(acc_drift));

	fclose(f);

	if (fp != NULL) {
		fflush(fp);
		fclose(fp);
	}

	sigaction(SIGINT, &act, NULL);
	raise(SIGINT);
}

void install_sigint()
{
	struct sigaction act = {
		.sa_handler = end
	};
	sigaction(SIGINT, &act, NULL);
}

struct routine_t {
	timer_t timerid;
	struct timespec saved_timesp, interval;
};

void routine(int signo, siginfo_t *siginfo, void *ctx)
{
	int ret;
	double d;

	static long long sample = 0;
	static struct timespec start_time;
	struct routine_t * routine_data = ((struct routine_t *)siginfo->si_value.sival_ptr);
	struct timespec saved_timesp = routine_data->saved_timesp;
	struct timespec timesp, delta, error;
	double drift, interval_d;
	ret = clock_gettime(CLOCK_ID, &timesp);

	if (ret == -1) {
		fprintf(stderr, "Error getting current time.\n");
		return;
	}

	if (saved_timesp.tv_sec != 0 || saved_timesp.tv_nsec != 0) {

		delta.tv_sec  = timesp.tv_sec - saved_timesp.tv_sec;
		delta.tv_nsec = timesp.tv_nsec - saved_timesp.tv_nsec;

		if (delta.tv_nsec < 0) {
			delta.tv_sec  -= 1;
			delta.tv_nsec += 1000 * 1000 * 1000;
		}

		if ( delta.tv_sec > routine_data->interval.tv_sec
				|| (delta.tv_sec == routine_data->interval.tv_sec
					&& delta.tv_nsec > routine_data->interval.tv_nsec) ) {
			error.tv_sec = delta.tv_sec - routine_data->interval.tv_sec;
			error.tv_nsec = delta.tv_nsec - routine_data->interval.tv_nsec;
		}
		else {
			error.tv_sec = routine_data->interval.tv_sec - delta.tv_sec;
			error.tv_nsec = routine_data->interval.tv_nsec - delta.tv_nsec;
		}

		if (error.tv_nsec < 0) {
			error.tv_sec -= 1;
			error.tv_nsec += 1000 * 1000 * 1000;
		}

		drift = (double)(timesp.tv_sec + timesp.tv_nsec / (1000.0 * 1000.0 * 1000.0))
			- (double)(start_time.tv_sec + start_time.tv_nsec / (1000.0 * 1000.0 * 1000.0));
		interval_d = ((double)routine_data->interval.tv_sec)
			     + ((double)routine_data->interval.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		if (sample % MAXFSIZE == 0) {
			if (fp != NULL) {
				fflush(fp);
				fclose(fp);
			}
			long long rotate_no = sample / MAXFSIZE;
			sprintf(pathbuf, "%s.log.%lld", logpath, rotate_no);
			printf("Change log file to %s\n", pathbuf);
			fp = fopen(pathbuf, "w");
		}
		sample++;

		drift -= (sample) * interval_d;

		/* stats */
		d = ((double)delta.tv_sec) + ((double)delta.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		gsl_rstat_add(d, acc_delta);
		d = ((double)error.tv_sec) + ((double)error.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		gsl_rstat_add(d, acc_error);
		d = drift;
		gsl_rstat_add(d, acc_drift);

		fprintf(fp, "%ld.%09ld,%ld.%09ld,%ld.%09ld,%.09lf\n",
                       timesp.tv_sec, timesp.tv_nsec, delta.tv_sec, delta.tv_nsec, error.tv_sec, error.tv_nsec, drift);

	}
	else {
		start_time = timesp;
	}
	/* save time */
	routine_data->saved_timesp = timesp;
}

struct routine_t * setup_periodic_heartbeat(int64_t period)
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
	const struct timespec interval = {
		.tv_sec  = period / (1000L * 1000L * 1000L),
		.tv_nsec = period % (1000L * 1000L * 1000L)
	};

	sigaction(SIGRT1, &act, NULL);

	ret = timer_create(CLOCK_ID, &sevp, &timerid);

	if (ret == -1) {
		fprintf(stderr, "Error creating timer.\n");
		return NULL;
	}

	/* initialize routine data */
	routine_data->timerid = timerid;
	routine_data->interval = interval;
	routine_data->saved_timesp.tv_sec = 0;
	routine_data->saved_timesp.tv_nsec = 0;

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
	int period;
	sigset_t mask;
	struct sched_param param;
	int prio;
	struct routine_t * handle;

	install_sigint();

	if (argc != 3) {
		fprintf(stderr, "Usage: %s [period] [logpath]\n", argv[0]);
		return 1;
	}

	prio = sched_get_priority_max(SCHED_FIFO)-1;
	param.sched_priority = prio;
	sched_setscheduler(0, SCHED_FIFO, &param);

	period = atoi(argv[1]);
	logpath = argv[2];

	struct timespec res;
	clock_getres(CLOCK_ID, &res);
	printf("clock resolution = %ld.%09lds.\n", res.tv_sec, res.tv_nsec);

	acc_delta = gsl_rstat_alloc();
	acc_error = gsl_rstat_alloc();
	acc_drift = gsl_rstat_alloc();

	handle = setup_periodic_heartbeat(period);
	periodic_heartbeat(handle->timerid, period);

	for (;;)
		sigsuspend(&mask);
	return 0;
}
