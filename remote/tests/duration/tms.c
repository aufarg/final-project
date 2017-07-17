#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <gsl/gsl_statistics_double.h>

#define MAXPASS 10000

#define NS_TO_MS(ns) ((double)(ns)/(1000.0 * 1000.0))
#define MILLISECS(ms) ((ms) * 1000 * 1000)

#define _STAT_VARNAME(_var) _stats##_var
#define _STAT_IDXNAME(_var) _STAT_VARNAME(_idx##_var)

#define DECLARE_STATVAR(_var) \
    static double _STAT_VARNAME(_var)[MAXPASS]; \
    static size_t _STAT_IDXNAME(_var) = 0

#define PUT_STATVAR(_var,_val) \
    _STAT_VARNAME(_var)[ _STAT_IDXNAME(_var) ] = (_val); \
    _STAT_IDXNAME(_var)++

#define STAT_VARNAME(_var) _STAT_VARNAME(_var)
#define STAT_VARSIZE(_var) _STAT_IDXNAME(_var)

#define _VAR_GSLARGS(_var) STAT_VARNAME(_var), 1, STAT_VARSIZE(_var)

#define GET_STATVAR(_msg, _var) \
    printf(_msg ": avg = %lf, var = %lf, min %lf, max = %lf\n", \
            gsl_stats_mean(_VAR_GSLARGS(_var)), \
            gsl_stats_variance(_VAR_GSLARGS(_var)), \
            gsl_stats_min(_VAR_GSLARGS(_var)), \
            gsl_stats_max(_VAR_GSLARGS(_var)))


int main(int argc, char *argv[])
{
    struct timespec tp, tp_prev;
    unsigned total_pass = 0;
    unsigned pass = 0;
    long long tms = 0LL;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "Usage [tms] [number of pass(es)]\n");
        return 1;
    }

    total_pass = atoi(argv[1]);

    if (total_pass > MAXPASS) {
        fprintf(stderr, "Too much pass(es)\n");
        return 1;
    }

    DECLARE_STATVAR(tms);
    DECLARE_STATVAR(dms);

    fprintf(stderr, "Running for %u pass(es)\n", total_pass);
    ret = clock_gettime(CLOCK_MONOTONIC_RAW, &tp_prev);
    while (pass < total_pass) {
        ret = clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
        if (ret) {
            fprintf(stderr, "An error occurred.\n");
            return 1;
        }
        long long dms = tp.tv_nsec-tp_prev.tv_nsec;
        if (dms < 0)
            dms += MILLISECS(1000);

        if (NS_TO_MS(dms) >= 10.0) {
            double mdms = NS_TO_MS(dms);
            double mtms = NS_TO_MS(tms);
            PUT_STATVAR(dms, mdms);
            PUT_STATVAR(tms, mtms);
            pass++;
            printf("Pass %u done (tms = %lf, dms = %lf).\n", pass, mtms, mdms);

            tms = (tms + dms) % MILLISECS(10);
        }
        else {
            tms += dms;
        }
        tp_prev = tp;
    }
    GET_STATVAR("Runtime (ms)", tms);
    GET_STATVAR("Delta (ms)", dms);
    return 0;
}
