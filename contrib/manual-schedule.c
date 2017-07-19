#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include <uuid/uuid.h>
/* #include <xenctrl.h> */
/* #include <libxl_utils.h> */
/* #include <libxl.h> */

#define MILLISECS(_ms)  ((_ms) * 1000000ULL)

#define SCHED_POOLID 0
#define VCPU_ID 0

struct schedule {
    int64_t major_frame;
    struct sched_entry *entries;
    int num_entries;
};

struct sched_entry {
    const char *dom_name;
    int vcpu_id;
    int64_t runtime;
};

struct dominfo {
    char *dom_name;
    uuid_t uuid;
    domid_t domid;
};

int extract_config(struct schedule *sched, char *config_path)
{
    json_t *root, *cur_obj, *json_arr;
    json_error_t error;
    char *config_json;
    size_t size;
    int i;

    FILE *fp = fopen(config_path, "r");
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

    sched = calloc(1, sizeof(sched));

    /* get major time frame */
    cur_obj = json_object_get(root, "major_frame");
    if (!cur_obj || !json_is_integer(cur_obj))
        goto fail;
    sched->major_frame = json_integer_value(cur_obj);

    /* get schedule list */
    cur_obj = json_object_get(root, "entries");
    if (!cur_obj || !json_is_array(cur_obj))
        goto fail;
    json_arr = cur_obj;

    sched->num_entries = json_array_size(cur_obj);
    sched->entries = malloc(sched->num_entries);

    json_array_foreach(json_arr, i, cur_obj) {
        json_t *cur_entry = cur_obj;
        if (!cur_entry || !json_is_object(cur_entry))
            goto fail;

        cur_obj = json_object_get(cur_entry, "dom_name");
        if (!cur_obj || !json_is_string(cur_obj))
            goto fail;
        sched->entries[i].dom_name = json_string_value(cur_obj);

        cur_obj = json_object_get(cur_entry, "runtime");
        if (!cur_obj || !json_is_integer(cur_obj))
            goto fail;
        sched->entries[i].runtime = json_integer_value(cur_obj);
    }

    return 0;
fail:
    if (sched->entries)
        free(sched->entries);
    free(sched);
    sched = NULL;
    return 1;
}

int main(int argc, char *argv[])
{
    struct xen_sysctl_arinc653_schedule a653sched;
    struct schedule sched;
    struct dominfo *domlist;
    libxl_ctx *ctx;
    libxl_dominfo *dominfo;
    int64_t total_runtime = 0;
    int i, ret;
    int num_entries, num_domains;

    if (argc != 2) {
    	fprintf(stderr, "Usage: manual-schedule [config]");
    	return 1;
    }

    xc_interface *xci = xc_interface_open(NULL, NULL, 0);

    libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL);
    dominfo = libxl_list_domain(ctx, &num_domains);

    domlist = malloc(num_domains * sizeof(dominfo));
    for (i = 0; i < num_domains; i++) {
        char *name = libxl_domid_to_name(ctx, dominfo[i].domid);
        domlist[i].dom_name = name;
        domlist[i].domid = dominfo[i].domid;
        domlist[i].uuid = dominfo[i].uuid;
    }

    extract_config(&sched, argv[1]);

    for (i = 0; i < sched.num_entries; i++) {
        total_runtime += sched.entries[i].runtime;
    }

    if (total_runtime > sched.major_frame) {
        fprintf(stderr, "major frame (%ld) should be longer than total runtime (%ld).\n", sched.major_frame, total_runtime);
        return 1;
    }

    a653sched.major_frame = sched.major_frame;
    a653sched.num_sched_entries = sched.num_entries;
    for (i = 0; i < sched.num_entries; i++) {
        char *entry_name = sched.entries[i].dom_name;
        int j;
        for (j = 0; j < num_domains && strcmp(entry_name, domlist[j].dom_name); j++);

        libxl_uuid_copy(ctx, (libxl_uuid *)&a653sched.sched_entries[i].dom_handle,
                &(sched.entries[i].uuid));
        a653sched.sched_entries[i].vcpu_id = VCPU_ID;
        a653sched.sched_entries[i].runtime = sched.entries[i].runtime;
    }

    ret = xc_sched_arinc653_schedule_set(xci, SCHED_POOLID, &a653sched);
    if (ret) {
        puts("Failed to set arinc653 schedule");
        return ret;
    }
    return 0;
}
