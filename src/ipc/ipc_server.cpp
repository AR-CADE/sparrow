#include "ipc_server.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <simdjson.h>

#include "core.hpp"
#include "sparrow-ipc-v1-protocol.h"
#include "surface/view.hpp"
#include "util/trace.hpp"
#include <sparrow/nonstd/wlroots-full.hpp>

static const struct sparrow_ipc_manager_v1_interface sparrow_ipc_manager_impl = {
    .destroy = [] (struct wl_client *client, struct wl_resource *resource)
    {
        wl_resource_destroy(resource);
    },
    .get_ipc_channel = [] (struct wl_client *client, struct wl_resource *resource, const char *app_id)
    {
        IpcServer *server = static_cast<IpcServer*>(wl_resource_get_user_data(resource));
        if (!server)
        {
            return;
        }

        pid_t peer_pid = 0;
        uid_t peer_uid = 0;
        gid_t peer_gid = 0;
        wl_client_get_credentials(client, &peer_pid, &peer_uid, &peer_gid);

        // Security check: must match current user UID
        if (peer_uid != getuid())
        {
            wlr_log(WLR_ERROR,
                "[sparrow-ipc] Denied IPC channel request for app '%s' from untrusted UID: %u",
                app_id ? app_id : "unknown", peer_uid);
            return;
        }

        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds) != 0)
        {
            wlr_log(WLR_ERROR, "[sparrow-ipc] socketpair creation failed: %s", strerror(errno));
            return;
        }

        wlr_log(WLR_INFO,
            "[sparrow-ipc] Created secure socketpair (server_fd=%d, client_fd=%d) for app '%s' (PID %d)",
            fds[0], fds[1], app_id ? app_id : "unnamed", peer_pid);

        // Send client-side FD over Wayland SCM_RIGHTS
        sparrow_ipc_manager_v1_send_ipc_channel(resource, fds[1]);
        close(fds[1]); // Server closes client-side copy after transfer

        // Register server-side FD in IpcServer
        auto conn    = std::make_unique<IpcServer::ClientConnection>();
        conn->fd     = fds[0];
        conn->app_id = app_id ? app_id : "";
        conn->peer_pid = peer_pid;
        conn->peer_uid = peer_uid;
        conn->peer_gid = peer_gid;
        conn->server   = server;

        struct wl_event_loop *loop = wl_display_get_event_loop(server->get_core()->wl_display);
        conn->event_source = wl_event_loop_add_fd(loop, fds[0],
            WL_EVENT_READABLE | WL_EVENT_HANGUP | WL_EVENT_ERROR,
            &IpcServer::on_client_socket_event, conn.get());

        server->add_client(std::move(conn));
    },
};

IpcServer::IpcServer(Core *core) :
    core_(core)
{}

IpcServer::~IpcServer()
{
    shutdown();
}

bool IpcServer::init()
{
    if (!core_ || !core_->wl_display)
    {
        return false;
    }

    global_ = wl_global_create(core_->wl_display, &sparrow_ipc_manager_v1_interface, 1, this,
        &IpcServer::bind_sparrow_ipc);

    if (!global_)
    {
        wlr_log(WLR_ERROR, "[sparrow-ipc] Failed to create sparrow_ipc_manager_v1 global");
        return false;
    }

    wlr_log(WLR_INFO, "[sparrow-ipc] Initialized sparrow_ipc_manager_v1 protocol (Zero-Trust socketpair)");
    return true;
}

void IpcServer::shutdown()
{
    clients_.clear();

    if (global_)
    {
        wl_global_destroy(global_);
        global_ = nullptr;
    }
}

void IpcServer::bind_sparrow_ipc(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    IpcServer *server = static_cast<IpcServer*>(data);
    struct wl_resource *resource =
        wl_resource_create(client, &sparrow_ipc_manager_v1_interface, version, id);

    if (!resource)
    {
        wl_client_post_no_memory(client);
        return;
    }

    wl_resource_set_implementation(resource, &sparrow_ipc_manager_impl, server, nullptr);
}

int IpcServer::on_client_socket_event(int fd, uint32_t mask, void *data)
{
    ClientConnection *conn = static_cast<ClientConnection*>(data);
    if (!conn || !conn->server)
    {
        return 0;
    }

    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))
    {
        wlr_log(WLR_INFO, "[sparrow-ipc] Client disconnected (app '%s', PID %d)",
            conn->app_id.c_str(), conn->peer_pid);
        conn->server->remove_client(conn);
        return 0;
    }

    if (mask & WL_EVENT_READABLE)
    {
        char buffer[4096];
        while (true)
        {
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
            if (bytes_read > 0)
            {
                conn->read_buffer.append(buffer, bytes_read);
            } else if ((bytes_read < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
            {
                break;
            } else
            {
                // Disconnected
                conn->server->remove_client(conn);
                return 0;
            }
        }

        conn->server->process_client_data(conn);
    }

    return 1;
}

void IpcServer::process_client_data(ClientConnection *conn)
{
    size_t newline_pos;
    while ((newline_pos = conn->read_buffer.find('\n')) != std::string::npos)
    {
        std::string line = conn->read_buffer.substr(0, newline_pos);
        conn->read_buffer.erase(0, newline_pos + 1);

        if (!line.empty())
        {
            handle_json_message(conn, line);
        }
    }
}

static std::string escape_json_str(const char *src)
{
    if (!src)
    {
        return "";
    }

    std::string out;
    out.reserve(strlen(src) + 8);
    for (const char *p = src; *p; ++p)
    {
        switch (*p)
        {
          case '"':
            out += "\\\"";
            break;

          case '\\':
            out += "\\\\";
            break;

          case '\b':
            out += "\\b";
            break;

          case '\f':
            out += "\\f";
            break;

          case '\n':
            out += "\\n";
            break;

          case '\r':
            out += "\\r";
            break;

          case '\t':
            out += "\\t";
            break;

          default:
            if (static_cast<unsigned char>(*p) < 0x20)
            {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(*p));
                out += hex;
            } else
            {
                out += *p;
            }

            break;
        }
    }

    return out;
}

void IpcServer::handle_json_message(ClientConnection *conn, const std::string & line)
{
    SPARROW_TRACE_SCOPE("ipc", "IpcServer::handle_json_message");
    thread_local simdjson::dom::parser parser;
    auto doc_res = parser.parse_unpadded(line);

    if (doc_res.error() || !doc_res.value().is_object())
    {
        wlr_log(WLR_ERROR, "[sparrow-ipc] Invalid JSON received from client: %s", line.c_str());
        return;
    }

    auto doc = doc_res.value();

    int64_t msg_id = 0;
    auto id_res    = doc["id"];
    if (!id_res.error() && (id_res.value().is_int64() || id_res.value().is_uint64()))
    {
        msg_id = id_res.value().get_int64().value();
    }

    std::string method;
    auto method_res = doc["method"];
    if (!method_res.error() && method_res.value().is_string())
    {
        method = std::string(method_res.value().get_string().value());
    }

    wlr_log(WLR_INFO, "[sparrow-ipc] Request [%s] (id=%" PRId64 ") from app '%s'",
        method.c_str(), msg_id, conn->app_id.c_str());

    if (method == "ping")
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint64_t now_us = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;

        std::string s = "{\"pong\":true,\"timestamp_us\":" + std::to_string(now_us) +
            ",\"peer_pid\":" + std::to_string(conn->peer_pid) + "}";
        send_response(conn, msg_id, s);
    } else if (method == "getCompositorInfo")
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint64_t now_us = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;

        size_t surface_count = 0;
        SparrowView *view;
        wl_list_for_each(view, &core_->views_list, link)
        {
            surface_count++;
        }

        char fps_buf[32];
        snprintf(fps_buf, sizeof(fps_buf), "%.2f", core_->get_client_fps(now_us));

        std::string s = "{\"compositor\":\"sparrow\",\"version\":\"0.2.0\",\"ipc_channel\":"
                        "\"anonymous_socketpair (kernel isolated, zero disk file)\","
                        "\"surfaces_count\":" + std::to_string(surface_count) + ","
                                                                                "\"client_fps\":" + fps_buf +
            ","
            "\"app_id\":\"" +
            escape_json_str(conn->app_id.c_str()) + "\","
                                                    "\"peer_pid\":" +
            std::to_string(conn->peer_pid) + "}";
        send_response(conn, msg_id, s);
    } else if (method == "listSurfaces")
    {
        std::string s = "[";
        bool first    = true;

        SparrowView *view;
        wl_list_for_each(view, &core_->views_list, link)
        {
            if (view->toplevel)
            {
                if (!first)
                {
                    s += ",";
                }

                first = false;
                s    += "{\"handle\":" + std::to_string(view->handle) +
                    ",\"title\":\"" + escape_json_str(view->toplevel->title ? view->toplevel->title : "") +
                    "\"" +
                    ",\"app_id\":\"" + escape_json_str(view->toplevel->app_id ? view->toplevel->app_id : "") +
                    "\"" +
                    ",\"width\":" + std::to_string(view->width) +
                    ",\"height\":" + std::to_string(view->height) +
                    ",\"maximized\":" + (view->maximized ? "true" : "false") +
                    ",\"fullscreen\":" + (view->fullscreen ? "true" : "false") +
                    ",\"activated\":" + (view->activated ? "true" : "false") + "}";
            }
        }

        s += "]";
        send_response(conn, msg_id, s);
    } else if (method == "closeSurface")
    {
        bool found = false;
        auto params_res = doc["params"];
        if (!params_res.error() && params_res.value().is_object())
        {
            auto handle_res = params_res.value()["handle"];
            if (!handle_res.error() && (handle_res.value().is_int64() || handle_res.value().is_uint64()))
            {
                uint32_t handle = static_cast<uint32_t>(handle_res.value().get_uint64().value());
                SparrowView *view;
                wl_list_for_each(view, &core_->views_list, link)
                {
                    if ((view->handle == handle) && view->toplevel)
                    {
                        wlr_xdg_toplevel_send_close(view->toplevel);
                        found = true;
                        break;
                    }
                }
            }
        }

        std::string s = std::string("{\"closed\":") + (found ? "true" : "false") + "}";
        send_response(conn, msg_id, s);
    } else
    {
        send_response(conn, msg_id, "{\"error\": \"Method not found\"}", true);
    }
}

void IpcServer::send_response(ClientConnection *conn, int64_t id, const std::string & result_json,
    bool is_error)
{
    SPARROW_TRACE_SCOPE("ipc", "IpcServer::send_response");
    if (!conn || (conn->fd < 0))
    {
        return;
    }

    std::string response;
    response.reserve(result_json.length() + 64);
    response += "{\"jsonrpc\":\"2.0\",\"id\":";
    response += std::to_string(id);
    if (is_error)
    {
        response += ",\"error\":";
        response += result_json;
    } else
    {
        response += ",\"result\":";
        response += result_json;
    }

    response += "}\n";

    ssize_t written = write(conn->fd, response.data(), response.size());
    (void)written;
}

void IpcServer::broadcast_notification(const std::string & method, const std::string & params_json)
{
    SPARROW_TRACE_SCOPE("ipc", "IpcServer::broadcast_notification");
    std::string msg = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method + "\",\"params\":" + params_json + "}\n";
    for (auto & conn : clients_)
    {
        if (conn && (conn->fd >= 0))
        {
            ssize_t written = write(conn->fd, msg.data(), msg.size());
            (void)written;
        }
    }
}

void IpcServer::add_client(std::unique_ptr<ClientConnection> conn)
{
    clients_.push_back(std::move(conn));
}

void IpcServer::remove_client(ClientConnection *target)
{
    for (auto it = clients_.begin(); it != clients_.end(); ++it)
    {
        if (it->get() == target)
        {
            if (target->event_source)
            {
                wl_event_source_remove(target->event_source);
                target->event_source = nullptr;
            }

            if (target->fd >= 0)
            {
                close(target->fd);
                target->fd = -1;
            }

            clients_.erase(it);
            break;
        }
    }
}
