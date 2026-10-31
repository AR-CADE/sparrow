#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <wayland-client.h>

#include "build/proto/libwl_client_protos.a.p/sparrow-ipc-v1-client-protocol.h"

static struct sparrow_ipc_manager_v1 *g_ipc_manager = NULL;
static int g_ipc_fd = -1;

static void ipc_channel_handler(void *data, struct sparrow_ipc_manager_v1 *manager, int32_t fd)
{
    (void)data;
    (void)manager;
    printf("[test_ipc] Received private IPC socketpair FD: %d\n", fd);
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
        printf("[test_ipc] Found sparrow_ipc_manager_v1 interface (id=%u)\n", id);
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

    write(fd, "\n", 1);

    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, 3000);
    if (ret <= 0)
    {
        fprintf(stderr, "[test_ipc] Timeout waiting for RPC response\n");
        return -1;
    }

    ssize_t n = read(fd, resp_buf, buf_size - 1);
    if (n <= 0)
    {
        fprintf(stderr, "[test_ipc] Error or EOF reading RPC response\n");
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
        fprintf(stderr, "[test_ipc] Cannot connect to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!g_ipc_manager)
    {
        fprintf(stderr, "[test_ipc] Error: sparrow_ipc_manager_v1 not advertised\n");
        wl_display_disconnect(display);
        return 1;
    }

    sparrow_ipc_manager_v1_get_ipc_channel(g_ipc_manager, "test_supervisor_client");
    wl_display_roundtrip(display);

    if (g_ipc_fd < 0)
    {
        fprintf(stderr, "[test_ipc] Error: Did not receive IPC socket descriptor\n");
        wl_display_disconnect(display);
        return 1;
    }

    char resp[8192];

    // 1. Ping
    printf("\n--- Test 1: ping ---\n");
    if (send_rpc_and_wait(g_ipc_fd, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}", resp,
        sizeof(resp)) == 0)
    {
        printf("Response: %s\n", resp);
    }

    // 2. listServices
    printf("\n--- Test 2: listServices ---\n");
    if (send_rpc_and_wait(g_ipc_fd, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"listServices\"}", resp,
        sizeof(resp)) == 0)
    {
        printf("Response: %s\n", resp);
    }

    // 3. Optional action from CLI args (e.g. stop, start, restart)
    if (argc >= 3)
    {
        const char *action = argv[1];
        const char *svc_id = argv[2];
        char req[512];
        snprintf(req, sizeof(req),
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"%s\",\"params\":{\"id\":\"%s\"}}",
            action, svc_id);
        printf("\n--- Test Custom Action: %s on '%s' ---\n", action, svc_id);
        if (send_rpc_and_wait(g_ipc_fd, req, resp, sizeof(resp)) == 0)
        {
            printf("Response: %s\n", resp);
        }
    }

    close(g_ipc_fd);
    sparrow_ipc_manager_v1_destroy(g_ipc_manager);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    printf("[test_ipc] Done!\n");
    return 0;
}
