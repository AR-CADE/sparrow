#include "ipc_client.hpp"

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

IpcClient::IpcClient()
{}

IpcClient::~IpcClient()
{
    close();
}

void IpcClient::set_fd(int fd)
{
    if (fd == fd_)
    {
        return;
    }

    close();
    fd_ = fd;
    if (fd_ >= 0)
    {
        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags >= 0)
        {
            fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
        }

        printf("[sparrow-ipc-client] Connected to secure private socketpair (FD %d)\n", fd_);
        fflush(stdout);
    }
}

void IpcClient::close()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
        fd_ = -1;
    }

    for (auto & pair : pending_requests_)
    {
        if (pair.second.callback)
        {
            simdjson::dom::element err_elem;
            pair.second.callback(false, err_elem);
        }
    }

    pending_requests_.clear();
    read_buffer_.clear();
}

void IpcClient::send_request(const std::string & method,
    const std::string & params_json,
    ResponseCallback callback)
{
    if (fd_ < 0)
    {
        printf("[sparrow-ipc-client] send_request failed: client not connected (fd=%d)\n", fd_);
        fflush(stdout);
        if (callback)
        {
            simdjson::dom::element err_elem;
            callback(false, err_elem);
        }

        return;
    }

    int64_t id = next_id_++;
    if (callback)
    {
        pending_requests_[id] = {std::move(callback)};
    }

    std::string msg = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
        ",\"method\":\"" + method + "\",\"params\":" +
        (params_json.empty() ? "{}" : params_json) + "}\n";

    printf("[sparrow-ipc-client] Sending request id=%" PRId64 " method='%s' to fd %d (%zu bytes)\n",
        id, method.c_str(), fd_, msg.size());
    fflush(stdout);

    ssize_t written = write(fd_, msg.data(), msg.size());
    if (written < 0)
    {
        printf("[sparrow-ipc-client] ERROR: write to fd %d failed: %s (errno=%d)\n",
            fd_, strerror(errno), errno);
        fflush(stdout);
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end())
        {
            auto cb = std::move(it->second.callback);
            pending_requests_.erase(it);
            if (cb)
            {
                simdjson::dom::element err_elem;
                cb(false, err_elem);
            }
        }
    }
}

void IpcClient::send_request(const std::string & method, ResponseCallback callback)
{
    send_request(method, "{}", std::move(callback));
}

void IpcClient::dispatch_read()
{
    if (fd_ < 0)
    {
        return;
    }

    char buf[4096];
    while (true)
    {
        ssize_t n = read(fd_, buf, sizeof(buf));
        if (n > 0)
        {
            read_buffer_.append(buf, static_cast<size_t>(n));
        } else if (n == 0)
        {
            printf("[sparrow-ipc-client] Compositor closed IPC connection\n");
            fflush(stdout);
            close();
            break;
        } else
        {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            {
                break;
            }

            printf("[sparrow-ipc-client] read error on fd %d: %s (errno=%d)\n",
                fd_, strerror(errno), errno);
            fflush(stdout);
            close();
            break;
        }
    }

    process_lines();
}

void IpcClient::process_lines()
{
    size_t newline_pos = 0;
    while ((newline_pos = read_buffer_.find('\n')) != std::string::npos)
    {
        std::string line = read_buffer_.substr(0, newline_pos);
        read_buffer_.erase(0, newline_pos + 1);

        if (!line.empty())
        {
            handle_json_line(line);
        }
    }
}

void IpcClient::handle_json_line(const std::string & line)
{
    printf("[sparrow-ipc-client] Received IPC line: %s\n", line.c_str());
    fflush(stdout);

    auto doc_res = parser_.parse_unpadded(line);

    if (doc_res.error() || !doc_res.value().is_object())
    {
        printf("[sparrow-ipc-client] JSON parse error on received line: %s\n",
            simdjson::error_message(doc_res.error()));
        fflush(stdout);
        return;
    }

    auto doc    = doc_res.value();
    auto id_res = doc["id"];
    if (!id_res.error() && (id_res.value().is_int64() || id_res.value().is_uint64()))
    {
        int64_t id = id_res.value().get_int64().value();
        auto it    = pending_requests_.find(id);
        if (it != pending_requests_.end())
        {
            auto cb = std::move(it->second.callback);
            pending_requests_.erase(it);

            auto err_res = doc["error"];
            if (!err_res.error() && !err_res.value().is_null())
            {
                cb(false, err_res.value());
            } else
            {
                auto res = doc["result"];
                if (!res.error())
                {
                    cb(true, res.value());
                } else
                {
                    simdjson::dom::element null_elem;
                    cb(true, null_elem);
                }
            }
        }
    } else
    {
        auto method_res = doc["method"];
        if (!method_res.error() && method_res.value().is_string())
        {
            if (on_notification_)
            {
                std::string method(method_res.value().get_string().value());
                auto params_res = doc["params"];
                if (!params_res.error())
                {
                    on_notification_(method, params_res.value());
                } else
                {
                    simdjson::dom::element null_params;
                    on_notification_(method, null_params);
                }
            }
        }
    }
}
