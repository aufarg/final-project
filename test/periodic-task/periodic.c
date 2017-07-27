#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sched.h>

#define CLOCK_ID CLOCK_MONOTONIC

#include <gsl/gsl_statistics_double.h>
#include <gsl/gsl_rstat.h>

gsl_rstat_workspace * acc_delta;
gsl_rstat_workspace * acc_error;
gsl_rstat_workspace * acc_drift;

void end(int signo)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL
	};
	printf("\n");
	printf("Statistics:\n");
	printf("delta(s): mean=%.09lf, median=%0.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_delta),
			gsl_rstat_median(acc_delta),
			gsl_rstat_sd(acc_delta),
			gsl_rstat_min(acc_delta),
			gsl_rstat_max(acc_delta),
			gsl_rstat_n(acc_delta));

	printf("error(s): mean=%.09lf, median=%0.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_error),
			gsl_rstat_median(acc_error),
			gsl_rstat_sd(acc_error),
			gsl_rstat_min(acc_error),
			gsl_rstat_max(acc_error),
			gsl_rstat_n(acc_error));
	printf("drift(s): mean=%.09lf, median=%.09lf, sd=%.09lf, min=%0.9lf, max=%0.9lf (%lu samples)\n",
			gsl_rstat_mean(acc_drift),
			gsl_rstat_median(acc_drift),
			gsl_rstat_sd(acc_drift),
			gsl_rstat_min(acc_drift),
			gsl_rstat_max(acc_drift),
			gsl_rstat_n(acc_drift));
	printf("\n");

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

void routine(union sigval val)
{
	int ret;
	double d;

	struct routine_t * routine_data = ((struct routine_t *)val.sival_ptr);
	struct timespec saved_timesp = routine_data->saved_timesp;
	struct timespec timesp, delta, error;
	static struct timespec drift = {0};
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

		drift.tv_sec  += delta.tv_sec - routine_data->interval.tv_sec;
		drift.tv_nsec += delta.tv_nsec - routine_data->interval.tv_nsec;
		if ( (drift.tv_sec > 0 && drift.tv_nsec >= 1000 * 1000 * 1000)
				|| (drift.tv_sec < 0 && drift.tv_nsec > 0) ) {
			drift.tv_sec++;
			drift.tv_nsec -= 1000 * 1000 * 1000;
		}
		else if ( (drift.tv_sec > 0 && drift.tv_nsec < 0)
				|| (drift.tv_sec < 0 && drift.tv_nsec <= -1000 * 1000 * 1000) ) {
			drift.tv_sec--;
			drift.tv_nsec += 1000 * 1000 * 1000;
		}

		/* stats */
		d = ((double)delta.tv_sec) + ((double)delta.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		gsl_rstat_add(d, acc_delta);
		d = ((double)error.tv_sec) + ((double)error.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		gsl_rstat_add(d, acc_error);
		d = ((double)drift.tv_sec) + ((double)drift.tv_nsec) / (1000.0 * 1000.0 * 1000.0);
		gsl_rstat_add(d, acc_drift);

		printf("Current time = %ld.%09lds, delta = %ld.%09lds, error = %ld.%09lds drift = %s%ld.%09lds.\n",
				timesp.tv_sec, timesp.tv_nsec, delta.tv_sec, delta.tv_nsec, error.tv_sec, error.tv_nsec,
				(drift.tv_sec < 0 || drift.tv_nsec < 0) ? "-" : "", labs(drift.tv_sec), labs(drift.tv_nsec));

	}
	/* save time */
	routine_data->saved_timesp = timesp;
}

int create_periodic_task(int period)
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
		.tv_sec  = period / (1000 * 1000 * 1000),
		.tv_nsec = period % (1000 * 1000 * 1000)
	};
	const struct itimerspec timersp = {
		.it_interval = interval,
		.it_value    = {
			.tv_sec = 0,
			.tv_nsec = 1
		}
	};
	struct itimerspec saved_timersp;

	ret = timer_create(CLOCK_ID, &sevp, &timerid);

	if (ret == -1) {
		fprintf(stderr, "Error creating timer.\n");
		return -1;
	}

	/* initialize routine data */
	routine_data->timerid = timerid;
	routine_data->interval = interval;
	routine_data->saved_timesp.tv_sec = 0;
	routine_data->saved_timesp.tv_nsec = 0;

	ret = timer_settime(timerid, flags, &timersp, &saved_timersp);

	if (ret == -1) {
		fprintf(stderr, "Error settings timer interval.\n");
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	int period;
	int ret;
	sigset_t mask, oldmask;
	struct sched_param param;
	int prio;

	install_sigint();

	if (argc != 2) {
		fprintf(stderr, "Usage: %s [period]\n", argv[0]);
		return 1;
	}

	prio = sched_get_priority_min(SCHED_FIFO);
	param.sched_priority = prio;
	sched_setscheduler(0, SCHED_FIFO, &param);

	period = atoi(argv[1]);

	struct timespec res;
	clock_getres(CLOCK_ID, &res);
	printf("clock resolution = %ld.%09lds.\n", res.tv_sec, res.tv_nsec);

	acc_delta = gsl_rstat_alloc();
	acc_error = gsl_rstat_alloc();
	acc_drift = gsl_rstat_alloc();

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	ret = create_periodic_task(period);

	if (!ret) {
		sigsuspend(&oldmask);
		return 0;
	}
	else {
		return 1;
	}
}
