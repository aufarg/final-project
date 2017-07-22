#include <stdio.h>
#include <stdlib.h>
#include <jansson.h>
#include <string.h>
#include <uuid/uuid.h>
#include <xenctrl.h>
#include <libxl_utils.h>
#include <libxl.h>

#define MILLISECS(_ms)  ((_ms) * 1000000ULL)

#define SCHED_POOLID 0
#define VCPU_ID 0

struct sched_provider_t {
    const char *dom_name;
    int vcpu_id;
};

struct sched_entry_t {
    int service_id;
    struct sched_provider_t * providers;
    int num_providers;
    int64_t runtime;
};

struct schedule_t {
    int64_t major_frame;
    struct sched_entry_t *entries;
    int num_entries;
};

struct dominfo_t {
    const char *dom_name;
    libxl_uuid uuid;
    domid_t domid;
};

int extract_config(struct schedule_t *sched, char *config_path)
{
    json_t *root, *cur_obj, *arr_entries;
    json_error_t error;
    char *config_json;
    size_t size;
    unsigned int i, j;

    FILE *fp = fopen(config_path, "r");
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    config_json = malloc(size * sizeof(*config_json));
    fread(config_json, sizeof(*config_json), size, fp);

    root = json_loads(config_json, 0, &error);
    free(config_json);

    if (!root) {
        fprintf(stderr, "error: on line %d: %s\n", error.line, error.text);
        return 1;
    }

    memset(sched, 0, sizeof(*sched));

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
    sched->entries = malloc(sched->num_entries * sizeof(*sched->entries));

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
        sched->entries[i].providers = malloc(sched->entries[i].num_providers * sizeof(*sched->entries[i].providers));

        json_array_foreach(arr_providers, j, cur_obj) {
            json_t *cur_provider = cur_obj;

            if (!cur_provider || !json_is_object(cur_provider))
                goto fail;

            cur_obj = json_object_get(cur_provider, "dom_name");
            if (!cur_obj || !json_is_string(cur_obj))
                goto fail;

            sched->entries[i].providers[j].dom_name = json_string_value(cur_obj);
            sched->entries[i].providers[j].vcpu_id = VCPU_ID;
	    printf("i,j = %d,%d vcpu = %d\n", i, j, sched->entries[i].providers[j].vcpu_id);
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

int main(int argc, char *argv[])
{
    struct xen_sysctl_arinc653_schedule a653sched;
    struct schedule_t sched;
    struct dominfo_t *domlist;
    xc_interface *xci;
    libxl_ctx *ctx;
    libxl_dominfo *dominfo;
    int64_t total_runtime = 0;
    int i, ret;
    int num_domains;

    if (argc != 2) {
    	fprintf(stderr, "Usage: manual-schedule [config]\n");
    	return 1;
    }

    libxl_ctx_alloc(&ctx, LIBXL_VERSION, 0, NULL);
    dominfo = libxl_list_domain(ctx, &num_domains);

    domlist = malloc(num_domains * sizeof(*domlist));
    for (i = 0; i < num_domains; i++) {
        const char *name = libxl_domid_to_name(ctx, dominfo[i].domid);
	libxl_uuid_copy(ctx, &domlist[i].uuid, &dominfo[i].uuid);
        domlist[i].dom_name = name;
        domlist[i].domid = dominfo[i].domid;
    }

    ret = extract_config(&sched, argv[1]);

    if (ret) {
        fprintf(stderr, "error parsing JSON.\n");
        return 1;
    }

    for (i = 0; i < sched.num_entries; i++) {
        total_runtime += sched.entries[i].runtime;
    }

    a653sched.major_frame = sched.major_frame;
    a653sched.num_sched_entries = sched.num_entries;
    for (i = 0; i < sched.num_entries; i++) {
        int j;

        a653sched.sched_entries[i].service_id = sched.entries[i].service_id;
        a653sched.sched_entries[i].runtime = sched.entries[i].runtime;
        a653sched.sched_entries[i].num_providers = sched.entries[i].num_providers;

        printf("Service %d\n", a653sched.sched_entries[i].service_id);
        printf("Service runtime is %ld\n", a653sched.sched_entries[i].runtime);
        printf("There %d domain(s) providing this service\n", a653sched.sched_entries[i].num_providers);

        for (j = 0; j < sched.entries[i].num_providers; j++) {
            const char *entry_name = sched.entries[i].providers[j].dom_name;
            char uuid_str[40];
	    int k;

	    for (k = 0; k < num_domains && strcmp(entry_name, domlist[k].dom_name); k++);
            if (k >= num_domains) {
                fprintf(stderr, "domain name %s is not found.\n", entry_name);
                return 1;
            }

            libxl_uuid_copy(ctx, (libxl_uuid *)&a653sched.sched_entries[i].service_providers[j].dom_handle,
                    &domlist[k].uuid);
            a653sched.sched_entries[i].service_providers[j].vcpu_id = sched.entries[i].providers[j].vcpu_id;

            uuid_unparse((const unsigned char *)&a653sched.sched_entries[i].service_providers[j].dom_handle, uuid_str);
            printf("- %s (%d,%d) at VCPU %d (from %d)\n", uuid_str, i, j, a653sched.sched_entries[i].service_providers[j].vcpu_id,
			    sched.entries[i].providers[j].vcpu_id);
        }
    }
    printf("Major frame is %ld.\n", a653sched.major_frame);
    printf("Scheduling %d domain(s) ... \n", a653sched.num_sched_entries);

    xci = xc_interface_open(NULL, NULL, 0);

    ret = xc_sched_arinc653_schedule_set(xci, SCHED_POOLID, &a653sched);
    if (ret) {
        puts("Failed to set arinc653 schedule");
        return ret;
    }
    return 0;
}
