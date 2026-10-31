#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <wayland-client.h>

#include "sparrow-ipc-v1-client-protocol.h"

static struct sparrow_ipc_manager_v1 *g_ipc_manager = NULL;
static int g_ipc_fd = -1;

static void ipc_channel_handler(void *data, struct sparrow_ipc_manager_v1 *manager, int32_t fd)
{
    (void)data;
    (void)manager;
    g_ipc_fd = fd;
}

static const struct sparrow_ipc_manager_v1_listener ipc_listener = {
    .ipc_channel = ipc_channel_handler,
};

static void registry_handler(void *data, struct wl_registry *registry, uint32_t id,
    const char *interface, uint32_t version)
{
    (void)data;
    (void)version;
    if (strcmp(interface, sparrow_ipc_manager_v1_interface.name) == 0)
    {
        g_ipc_manager = wl_registry_bind(registry, id, &sparrow_ipc_manager_v1_interface, 1);
        sparrow_ipc_manager_v1_add_listener(g_ipc_manager, &ipc_listener, NULL);
    }
}

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    (void)data;
    (void)registry;
    (void)id;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover,
};

static int send_rpc_and_wait(int fd, const char *req_json, char *resp_buf, size_t buf_size)
{
    size_t len = strlen(req_json);
    if (write(fd, req_json, len) != (ssize_t)len)
    {
        perror("write");
        return -1;
    }

    if (write(fd, "\n", 1) != 1)
    {
        perror("write newline");
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 3000);
    if (ret <= 0)
    {
        fprintf(stderr, "sparrow-services-monitor: Timeout waiting for compositor response\n");
        return -1;
    }

    ssize_t n = read(fd, resp_buf, buf_size - 1);
    if (n <= 0)
    {
        fprintf(stderr, "sparrow-services-monitor: Error or EOF reading compositor response\n");
        return -1;
    }

    resp_buf[n] = '\0';
    return 0;
}

int main(int argc, char **argv)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
    {
        fprintf(stderr, "sparrow-services-monitor: Cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!g_ipc_manager)
    {
        fprintf(stderr,
            "sparrow-services-monitor: Protocol sparrow_ipc_manager_v1 not advertised by compositor\n");
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return 1;
    }

    sparrow_ipc_manager_v1_get_ipc_channel(g_ipc_manager, "sparrow-services-monitor-cli");
    wl_display_roundtrip(display);

    if (g_ipc_fd < 0)
    {
        fprintf(stderr, "sparrow-services-monitor: Did not receive private socketpair file descriptor\n");
        sparrow_ipc_manager_v1_destroy(g_ipc_manager);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return 1;
    }

    char req[1024];
    if ((argc <= 1) || (strcmp(argv[1], "list") == 0) || (strcmp(argv[1], "listServices") == 0))
    {
        snprintf(req, sizeof(req), "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"listServices\"}");
    } else if (strcmp(argv[1], "ping") == 0)
    {
        snprintf(req, sizeof(req), "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}");
    } else if (((strcmp(argv[1], "start") == 0) || (strcmp(argv[1], "startService") == 0)) && (argc >= 3))
    {
        snprintf(req, sizeof(req),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"startService\",\"params\":{\"id\":\"%s\"}}",
            argv[2]);
    } else if (((strcmp(argv[1], "stop") == 0) || (strcmp(argv[1], "stopService") == 0)) && (argc >= 3))
    {
        int sig = (argc >= 4) ? atoi(argv[3]) : 15;
        snprintf(req, sizeof(req),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"stopService\",\"params\":{\"id\":\"%s\",\"signal\":%d}}",
            argv[2], sig);
    } else if (((strcmp(argv[1], "restart") == 0) || (strcmp(argv[1], "restartService") == 0)) && (argc >= 3))
    {
        snprintf(req, sizeof(req),
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"restartService\",\"params\":{\"id\":\"%s\"}}",
            argv[2]);
    } else if ((strcmp(argv[1], "raw") == 0) && (argc >= 3))
    {
        snprintf(req, sizeof(req), "%s", argv[2]);
    } else
    {
        fprintf(stderr,
            "Usage: sparrow-services-monitor [list|start <id>|stop <id> [sig]|restart <id>|ping|raw <json>]\n");
        close(g_ipc_fd);
        sparrow_ipc_manager_v1_destroy(g_ipc_manager);
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return 1;
    }

    char resp[32768];
    int ret = send_rpc_and_wait(g_ipc_fd, req, resp, sizeof(resp));

    close(g_ipc_fd);
    sparrow_ipc_manager_v1_destroy(g_ipc_manager);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    if (ret != 0)
    {
        return 1;
    }

    // Print response to stdout
    fputs(resp, stdout);
    if (resp[strlen(resp) - 1] != '\n')
    {
        putchar('\n');
    }

    return 0;
}
