#include "ipc_server.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

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

void IpcServer::handle_json_message(ClientConnection *conn, const std::string & line)
{
    SPARROW_TRACE_SCOPE("ipc", "IpcServer::handle_json_message");
    rapidjson::Document doc;
    doc.Parse(line.c_str());

    if (doc.HasParseError() || !doc.IsObject())
    {
        wlr_log(WLR_ERROR, "[sparrow-ipc] Invalid JSON received from client: %s", line.c_str());
        return;
    }

    int64_t msg_id = 0;
    if (doc.HasMember("id") && doc["id"].IsInt64())
    {
        msg_id = doc["id"].GetInt64();
    }

    std::string method;
    if (doc.HasMember("method") && doc["method"].IsString())
    {
        method = doc["method"].GetString();
    }

    wlr_log(WLR_INFO, "[sparrow-ipc] Request [%s] (id=%" PRId64 ") from app '%s'",
        method.c_str(), msg_id, conn->app_id.c_str());

    if (method == "ping")
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        uint64_t now_us = static_cast<uint64_t>(tv.tv_sec) * 1000000ULL + tv.tv_usec;

        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> writer(s);
        writer.StartObject();
        writer.Key("pong");
        writer.Bool(true);
        writer.Key("timestamp_us");
        writer.Uint64(now_us);
        writer.Key("peer_pid");
        writer.Int(conn->peer_pid);
        writer.EndObject();

        send_response(conn, msg_id, s.GetString());
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

        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> writer(s);
        writer.StartObject();
        writer.Key("compositor");
        writer.String("sparrow");
        writer.Key("version");
        writer.String("0.2.0");
        writer.Key("ipc_channel");
        writer.String("anonymous_socketpair (kernel isolated, zero disk file)");
        writer.Key("surfaces_count");
        writer.Uint64(surface_count);
        writer.Key("client_fps");
        writer.Double(core_->get_client_fps(now_us));
        writer.Key("app_id");
        writer.String(conn->app_id.c_str());
        writer.Key("peer_pid");
        writer.Int(conn->peer_pid);
        writer.EndObject();

        send_response(conn, msg_id, s.GetString());
    } else if (method == "listSurfaces")
    {
        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> writer(s);
        writer.StartArray();

        SparrowView *view;
        wl_list_for_each(view, &core_->views_list, link)
        {
            if (view->toplevel)
            {
                writer.StartObject();
                writer.Key("handle");
                writer.Uint(view->handle);
                writer.Key("title");
                writer.String(view->toplevel->title ? view->toplevel->title : "");
                writer.Key("app_id");
                writer.String(view->toplevel->app_id ? view->toplevel->app_id : "");
                writer.Key("width");
                writer.Int(view->width);
                writer.Key("height");
                writer.Int(view->height);
                writer.Key("maximized");
                writer.Bool(view->maximized);
                writer.Key("fullscreen");
                writer.Bool(view->fullscreen);
                writer.Key("activated");
                writer.Bool(view->activated);
                writer.EndObject();
            }
        }

        writer.EndArray();
        send_response(conn, msg_id, s.GetString());
    } else if (method == "closeSurface")
    {
        bool found = false;
        if (doc.HasMember("params") && doc["params"].IsObject())
        {
            const auto & params = doc["params"];
            if (params.HasMember("handle") && params["handle"].IsUint())
            {
                uint32_t handle = params["handle"].GetUint();
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

        rapidjson::StringBuffer s;
        rapidjson::Writer<rapidjson::StringBuffer> writer(s);
        writer.StartObject();
        writer.Key("closed");
        writer.Bool(found);
        writer.EndObject();
        send_response(conn, msg_id, s.GetString());
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
