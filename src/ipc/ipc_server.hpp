#ifndef IPC_SERVER_HPP
#define IPC_SERVER_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Core;

class IpcServer
{
  public:
    explicit IpcServer(Core *core);
    ~IpcServer();

    bool init();
    void shutdown();

    void broadcast_notification(const std::string & method, const std::string & params_json);

    Core * get_core() const
    {
        return core_;
    }

    struct ClientConnection;
    void add_client(std::unique_ptr<ClientConnection> conn);
    void remove_client(ClientConnection *conn);

  public:
    struct ClientConnection
    {
        int fd = -1;
        struct wl_event_source *event_source = nullptr;
        std::string read_buffer;
        std::string app_id;
        pid_t peer_pid    = 0;
        uid_t peer_uid    = 0;
        gid_t peer_gid    = 0;
        IpcServer *server = nullptr;
    };

    static void handle_client_destroy(struct wl_client *client, struct wl_resource *resource);
    static void handle_get_ipc_channel(struct wl_client *client, struct wl_resource *resource,
        const char *app_id);
    static void bind_sparrow_ipc(struct wl_client *client, void *data, uint32_t version, uint32_t id);

    static int on_client_socket_event(int fd, uint32_t mask, void *data);

    void process_client_data(ClientConnection *conn);
    void handle_json_message(ClientConnection *conn, const std::string & line);
    void send_response(ClientConnection *conn, int64_t id, const std::string & result_json,
        bool is_error = false);

    Core *core_ = nullptr;
    struct wl_global *global_ = nullptr;
    std::vector<std::unique_ptr<ClientConnection>> clients_;
};

#endif // SPARROW_IPC_SERVER_HPP
