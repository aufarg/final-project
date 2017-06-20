#include <stdio.h>
#include <xenctrl.h>
#include <libxl.h>
#include <uuid/uuid.h>

#define MILLISECS(_ms)  ((_ms) * 1000000ULL)

#define SCHED_POOLID 0
#define VCPU_ID 0
#define RUNTIME MILLISECS(10)

struct sched_entry {
    const char *uuid;
    int vcpu_id;
    int64_t runtime;
};

int main(int argc, char *argv[])
{
    struct xen_sysctl_arinc653_schedule sched;
    libxl_ctx *ctx;
    libxl_dominfo *dominfo;
    int nb_domain;
    int i, ret;

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL);
    dominfo = libxl_list_domain(ctx, &nb_domain);

    sched.major_frame = 0;
    sched.num_sched_entries = nb_domain;
    printf("Schedule %d domain(s)\n", nb_domain);
    for (i = 0; i < sched.num_sched_entries; ++i)
    {
        char uuid_str[40];
        libxl_uuid_copy(ctx, (libxl_uuid *)&sched.sched_entries[i].dom_handle,
                &(dominfo[i].uuid));
        uuid_unparse((const char *)&sched.sched_entries[i].dom_handle, uuid_str);
        printf("- %s\n", uuid_str);
        sched.sched_entries[i].vcpu_id = VCPU_ID;
        sched.sched_entries[i].runtime = RUNTIME;
        printf("Domain runtime is %lu\n", sched.sched_entries[i].runtime);
        sched.major_frame += sched.sched_entries[i].runtime;
    }

    printf("Major frame is %lu\n", sched.major_frame);
    ret = xc_sched_arinc653_schedule_set(xci, SCHED_POOLID, &sched);

    if (ret) {
        puts("Failed to set arinc653 schedule");
        return ret;
    }

    return 0;
}
