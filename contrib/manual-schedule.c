#include <stdio.h>
#include <xenctrl.h>
#include <libxl_utils.h>
#include <libxl.h>

#define MILLISECS(_ms)  ((_ms) * 1000000ULL)

#define SCHED_POOLID 0
#define VCPU_ID 0

struct sched_entry {
    const char *dom_name;
    int vcpu_id;
    int64_t runtime;
};

int main(int argc, char *argv[])
{
    struct xen_sysctl_arinc653_schedule sched;
    libxl_ctx *ctx;
    libxl_dominfo *dominfo;
    int i, ret;
    int num_entries, num_domains;

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL);
    dominfo = libxl_list_domain(ctx, &num_domains);

    puts("Dom info retrieved");
    for (i = 0; i < num_domains; i++) {
        char *name = libxl_domid_to_name(ctx, dominfo[i].domid);
        printf("Domain name with id %d is %s\n", dominfo[i].domid, name);
    }

    ret = xc_sched_arinc653_schedule_set(xci, SCHED_POOLID, &sched);
    if (ret) {
        puts("Failed to set arinc653 schedule");
        return ret;
    }
    return 0;
}
