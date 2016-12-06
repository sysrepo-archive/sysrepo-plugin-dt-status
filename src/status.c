#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <uci.h>
#include <libubus.h>
#include <libubox/blobmsg.h>
#include <libubox/blobmsg_json.h>
#include <libubox/list.h>
#include <json-c/json.h>
#include "sysrepo.h"
#include "sysrepo/plugins.h"
#include "status.h"

#define XPATH_MAX_LEN 100

static const char *config_file = "wireless";
static const char *lease_file_path = "/tmp/dhcp.leases";

struct  board *board;
/* Remove quotes from a string: */
/* Example: '"kernel"' -> 'kernel' */
static char *
remove_quotes(const char *str)
{
    char *unquoted;
    unquoted = strdup(str);
    unquoted = unquoted + 1;
    unquoted[strlen(unquoted) - 1] = '\0';

    return strdup(unquoted);
}

/* Take pointer to string and return new string without quotes. */
/* ubus call returns quotes strings and unquoted are needed. */
static void
fill_board(char **ref, char *name, struct json_object *r)
{
    struct json_object *o;

    json_object_object_get_ex(r, name, &o);
    *ref = remove_quotes(json_object_to_json_string(o));
}

/* Fill board (and release) by reacting on ubus call request. */
static void
system_board_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
    char *json_string;
    struct json_object *r, *t;

    fprintf(stderr, "systemboard cb\n");
    if (!msg) {
        return;
    }

    board = calloc(1, sizeof(*board));
    json_string = blobmsg_format_json(msg, true);
    r = json_tokener_parse(json_string);

    fill_board(&board->kernel, "kernel", r);
    fill_board(&board->hostname, "hostname", r);
    fill_board(&board->system, "system", r);

    struct release *release;
    release = calloc(1, sizeof(*release));

    json_object_object_get_ex(r, "release", &t);

    fill_board(&release->distribution, "distribution", t);
    fill_board(&release->version, "version", t);
    fill_board(&release->revision, "revision", t);
    fill_board(&release->codename, "codename", t);
    fill_board(&release->target, "target", t);
    fill_board(&release->description, "description", t);

    board->release = release;

    print_board(board);

    /* json_object_put(r); */
    /* free(json_string); */
}

/* Fill board with ubus information. */
static int
parse_board(struct ubus_context *ctx, struct board *board)
{
    uint32_t id = 0;
    struct blob_buf buf = {0,};
    int rc;

    blob_buf_init(&buf, 0);

    rc = ubus_lookup_id(ctx, "system", &id);

    if (rc) {
        fprintf(stderr, "ubus [%d]: no object system\n", rc);
        goto out;
    }
    rc = ubus_invoke(ctx, id, "board", buf.head, system_board_cb, NULL, 5000);
    if (rc) {
        fprintf(stderr, "ubus [%d]: no object board\n", rc);
        goto out;
    }

 out:
    /* blob_buf_free(&buf); */

    return rc;
}

static int
parse_lease(char *line, struct dhcp_lease *leases)
{
    int rc;
    char *ptr;
    char *tokens[5];
    int i;

    ptr = strtok(line, " ");
    tokens[0] = ptr;
    for(i = 1; ptr != NULL; i++) {
        ptr = strtok(NULL, " ");
        if (i >= 5) {
            rc = -1;
            break;
        }
        tokens[i] = ptr;
    }

    i = 0;
    leases->lease_expirey = strdup(tokens[i++]);
    leases->mac = strdup(tokens[i++]);
    leases->ip = strdup(tokens[i++]);
    leases->name = strdup(tokens[i++]);
    leases->id = strdup(tokens[i++]);
    printf("fix leaseid %s\n", leases->id);
    leases->id[strcspn(leases->id, "\n")] = 0;
    printf("fix leaseid %s\n", leases->id);
    return rc;
}

static int
parse_leases_file(FILE *lease_fd, struct list_head *leases)
{
    struct dhcp_lease *lease;
    size_t n_lease = 0;
    char *line = NULL;
    int n_read = 0;
    size_t len = 0;

    while((n_read = getline(&line, &len, lease_fd)) > 0) {
        lease = calloc(1, sizeof(*lease));
        parse_lease(line, lease);
        list_add(&lease->head, leases);
        n_lease++;
    }

    if (n_lease < 1) {
        printf("Lease file is empty.");
    }

    if (line) {
        free(line);
    }

    /* struct dhcp_lease *l; */
    /* list_for_each_entry(l, leases, head) { */
    /*   print_dhcp_lease(l); */
    /* } */

    /* if (lease) { */
    /*     free(lease); */
    /* } */

    return 0;
}

static void
parse_wifi_device(struct uci_section *s, struct wifi_device *wifi_dev)
{
    struct uci_element *e;
    struct uci_option *o;
    char *name, *value;

    uci_foreach_element(&s->options, e) {
        o = uci_to_option(e);
        name = e->name;
        value = o->v.string;
        if        (!strcmp("name", name)) {
            wifi_dev->name = strdup(value);
        } else if (!strcmp("type", name)) {
            wifi_dev->type = strdup(value);
        } else if (!strcmp("channel", name)) {
            wifi_dev->channel = strdup(value);
        } else if (!strcmp("macaddr", name)) {
            wifi_dev->macaddr = strdup(value);
        } else if (!strcmp("hwmode", name)) {
            wifi_dev->hwmode = strdup(value);
        } else if (!strcmp("disabled", name)) {
            wifi_dev->disabled = strdup(value);
        }
    }
}

static void
parse_wifi_iface(struct uci_section *s, struct wifi_iface *wifi_if)
{
    struct uci_element *e;
    struct uci_option *o;
    char *name, *value;

    uci_foreach_element(&s->options, e) {
        o = uci_to_option(e);
        name = o->e.name;
        value = o->v.string;
        if        (!strcmp("name", name)) {
            wifi_if->name = strdup(value);
        } else if (!strcmp("device", name)) {
            wifi_if->device= strdup(value);
        } else if (!strcmp("network", name)) {
            wifi_if->network = strdup(value);
        } else if (!strcmp("mode", name)) {
            wifi_if->mode = strdup(value);
        } else if (!strcmp("ssid", name)) {
            wifi_if->ssid = strdup(value);
        } else if (!strcmp("encryption", name)) {
            wifi_if->encryption = strdup(value);
        } else if (!strcmp("maclist", name)) {
            wifi_if->maclist = strdup(value);
        } else if (!strcmp("macfilter", name)) {
            wifi_if->macfilter = strdup(value);
        } else if (!strcmp("key", name)) {
            wifi_if->key = strdup(value);
        } else {
            printf("unexpected option: %s:%s\n", name, value);
        }
    }
}

static int
status_wifi(struct uci_context *ctx, struct list_head *ifs, struct list_head *devs)
{
  struct uci_package *package = NULL;
  struct wifi_iface *wifi_if;
  struct wifi_device *wifi_dev;
	struct uci_element *e;
	struct uci_section *s;
  int rc;


  rc = uci_load(ctx, config_file, &package);
  if (rc != UCI_OK)
    fprintf(stderr, "No configuration (package): %s\n", config_file);
		goto out;

  uci_foreach_element(&package->sections, e) {
  	s = uci_to_section(e);
    wifi_if = calloc(1, sizeof(*wifi_if));
    wifi_dev = calloc(1, sizeof(*wifi_dev));

  	if (!strcmp(s->type, "wifi-iface")) {
        parse_wifi_iface(s, wifi_if);
        list_add(&wifi_if->head, ifs);
    } else if (!strcmp("wifi-device", s->type)) {
        parse_wifi_device(s, wifi_dev);
        list_add(&wifi_dev->head, devs);

    } else {
        printf("Unexpected section: %s\n", s->type);
    }
  }

  struct wifi_iface *if_it;
  list_for_each_entry(if_it, ifs, head) {
      print_wifi_device(wifi_dev);
  }
  struct wifi_device *dev_it;
  list_for_each_entry(dev_it, devs, head) {
      print_wifi_iface(wifi_if);
  }

 out:
  if (package) {
  	uci_unload(ctx, package);
  }

	return true;
}

set_value_str(sr_session_ctx_t *sess, char *val_str, char *set_path)
{
    printf("setting %s to %s\n", set_path, val_str);
    sr_val_t val = { 0, };

    val.type = SR_STRING_T;
    val.data.string_val = val_str;

    printf("start to set %s %s %d\n", val_str, set_path, sess);
    int rc = sr_set_item(sess, set_path, &val, SR_EDIT_DEFAULT);
    printf("done to set\n");

    return rc;
}

static int
set_values(sr_session_ctx_t *sess, struct board *board,
           struct list_head *wifi_dev,
           struct list_head *wifi_if,
           struct list_head *leases)

{
    int rc = SR_ERR_OK;
    char xpath[XPATH_MAX_LEN];
    sr_val_t *val;

    printf("settingvalues\n");
    /* Setting kernel values. */
    if (board->kernel){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/kernel");
        if (SR_ERR_OK != set_value_str(sess, board->kernel, xpath)) {
            goto cleanup;
        }
    }

    if (board->hostname){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/hostname");
        if (SR_ERR_OK != set_value_str(sess, board->hostname, xpath)) {
            goto cleanup;
        }
    }

    if (board->system){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/system");
        if (SR_ERR_OK != set_value_str(sess, board->system, xpath)) {
            goto cleanup;
        }
    }

    if (board->release->distribution){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/release/distribution");
        if (SR_ERR_OK != set_value_str(sess, board->release->distribution, xpath)) {
            goto cleanup;
        }
    }

    if (board->release->version){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/release/version");
        if (SR_ERR_OK != set_value_str(sess, board->release->version, xpath)) {
            goto cleanup;
        }
    }

    if (board->release->revision){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/release/revision");
        if (SR_ERR_OK != set_value_str(sess, board->release->revision, xpath)) {
            goto cleanup;
        }
    }
    if (board->release->codename){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/release/codename");
        if (SR_ERR_OK != set_value_str(sess, board->release->codename, xpath)) {
            goto cleanup;
        }
    }

    if (board->release->target){
        snprintf(xpath, XPATH_MAX_LEN, "/status:board/release/target");
        if (SR_ERR_OK != set_value_str(sess, board->release->target, xpath)) {
            goto cleanup;
        }
    }

    /* Setting leases values. */
    struct dhcp_lease *l;
    list_for_each_entry(l, leases, head) {
        if (!l->id || !strcmp("", l->id) ) break;

        if (l->lease_expirey){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:dhcp/dhcp-leases[id='%s']/lease-expirey", l->id);
            if (SR_ERR_OK != set_value_str(sess, l->lease_expirey, xpath)) {
                goto cleanup;
            }
        }

        if (l->mac){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:dhcp/dhcp-leases[id='%s']/mac", l->id);
            if (SR_ERR_OK != set_value_str(sess, l->mac, xpath)) {
                goto cleanup;
            }
        }

        if (l->ip){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:dhcp/dhcp-leases[id='%s']/ip", l->id);
            if (SR_ERR_OK != set_value_str(sess, l->ip, xpath)) {
                goto cleanup;
            }
        }

        if (l->name){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:dhcp/dhcp-leases[id='%s']/name", l->id);
            if (SR_ERR_OK != set_value_str(sess, l->name, xpath)) {
                goto cleanup;
            }
        }

    }

    /* Setting wifi values. */
    struct wifi_device *d;
    list_for_each_entry(d, wifi_dev , head) {
        if (!d->name|| !strcmp("", d->name) ) break;

        print_wifi_device(d);
        if (d->type){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:wifi/wifi-device[name='%s']/type", d->name);
            if (SR_ERR_OK != set_value_str(sess, d->type, xpath)) {
                goto cleanup;
            }
        }

        if (d->channel){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:wifi/wifi-device[name='%s']/type", d->name);
            if (SR_ERR_OK != set_value_str(sess, d->channel, xpath)) {
                goto cleanup;
            }
        }

        if (d->macaddr){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:wifi/wifi-device[name='%s']/macaddr", d->name);
            if (SR_ERR_OK != set_value_str(sess, d->macaddr, xpath)) {
                goto cleanup;
            }
        }

        if (d->hwmode){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:wifi/wifi-device[name='%s']/hwmode", d->name);
            if (SR_ERR_OK != set_value_str(sess, d->hwmode, xpath)) {
                goto cleanup;
            }
        }

        if (d->disabled){
            snprintf(xpath, XPATH_MAX_LEN,
                     "/status:wifi/wifi-device[name='%s']/disabled", d->name);
            if (SR_ERR_OK != set_value_str(sess, d->disabled, xpath)) {
                goto cleanup;
            }
        }
    }


    printf("prije comita\n");
    /* Commit values set. */
    rc = sr_commit(sess);
    if (SR_ERR_OK != rc) {
        printf("Error by sr_commit: %s\n", sr_strerror(rc));
        goto cleanup;
    }
    printf("poslije comita\n");

  cleanup:
    return rc;
}


static void
init_data(struct model *ctx)
{
    FILE *fd_lease = NULL;

    ctx->uci_ctx = uci_alloc_context();
    if (!ctx->uci_ctx) {
        printf("Cant allocate uci\n");
        goto out;
    }
    ctx->ubus_ctx = ubus_connect(NULL);
    if (!ctx->ubus_ctx) {
        printf("Cant allocate ubus\n");
        goto out;
    }

    parse_board(ctx->ubus_ctx, board);
    status_wifi(ctx->uci_ctx, ctx->wifi_ifs, ctx->wifi_devs);

    fd_lease = fopen(lease_file_path, "r");
    if (fd_lease == NULL) {
        goto out;
    }
    parse_leases_file(fd_lease, ctx->leases);

    printf("====\n");
    ctx->board = board;
    /* ctx->leases = &leases; */
    /* printf("====\n"); */
    /* ctx->wifi_ifs = &ifs; */
    /* ctx->wifi_devs = &devs; */

  out:
    if (fd_lease) fclose(fd_lease);
}

static void
validate_change(sr_change_oper_t oper, sr_val_t *old_value, sr_val_t *new_value)
{
    char *id = new_value->xpath;
    printf("id is %s\n", id);

    if (strstr(id, "board")) {
        printf("Can't change board information.");
    } else if (strstr(id, "dhcp")) {
        printf("Can't change lease file\n");
    }
}

static int
module_change_cb(sr_session_ctx_t *session, const char *module_name,
                 sr_notif_event_t event, void *private_ctx)
{
    static int cnt = 0;
    int rc = SR_ERR_OK;
    sr_change_oper_t oper;
    sr_val_t *old_value = NULL;
    sr_val_t *new_value = NULL;
    char change_path[XPATH_MAX_LEN] = {0,};
    sr_change_iter_t *it = NULL;

    fprintf(stderr, "=============== module has changed ================" "%d:%s\n", cnt++, module_name);

    snprintf(change_path, XPATH_MAX_LEN, "/%s:*", module_name);

    rc = sr_get_changes_iter(session, change_path , &it);
    if (SR_ERR_OK != rc) {
      printf("Get changes iter failed for xpath %s", change_path);
      goto cleanup;
    }

    while (SR_ERR_OK ==
           (rc = sr_get_change_next(session, it, &oper, &old_value, &new_value))) {
        validate_change(oper, old_value, new_value);

        sr_free_val(old_value);
        sr_free_val(new_value);
    }

  cleanup:
    sr_free_change_iter(it);

    return SR_ERR_OK;
}

int
sr_plugin_init_cb(sr_session_ctx_t *session, void **private_ctx)
{
    sr_subscription_ctx_t *subscription = NULL;
    int rc = SR_ERR_OK;

    rc = sr_module_change_subscribe(session, "status", module_change_cb, NULL,
            0, SR_SUBSCR_DEFAULT, &subscription);
    if (SR_ERR_OK != rc) {
        goto error;
    }

    struct model *model = calloc(1, sizeof(*model));
    struct list_head leases = LIST_HEAD_INIT(leases);
    struct list_head ifs = LIST_HEAD_INIT(ifs);
    struct list_head devs = LIST_HEAD_INIT(devs);

    model->leases = &leases;
    model->wifi_ifs = &ifs;
    model->wifi_devs = &devs;

    init_data(model);

    fprintf (stderr,"data initialized\n");
    set_values(session, model->board, model->wifi_devs, model->wifi_ifs, model->leases);

    struct dhcp_lease *l;
    printf("a lease!\n");
    list_for_each_entry(l, model->leases, head) {
        print_dhcp_lease(l);
    }


    /* set subscription as our private context */
    *private_ctx = model;

    return SR_ERR_OK;

error:
    if (subscription) {
        sr_unsubscribe(session, subscription);
    }
    return rc;
}

void
sr_plugin_cleanup_cb(sr_session_ctx_t *session, void *private_ctx)
{
    /* subscription was set as our private context */
    if (private_ctx) {
        sr_unsubscribe(session, private_ctx);
    }
}
