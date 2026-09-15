#include "ipc_client.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

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

    pending_requests_.clear();
    read_buffer_.clear();
}

void IpcClient::send_request(const std::string & method,
    const rapidjson::Document & params,
    ResponseCallback callback)
{
    if (fd_ < 0)
    {
        printf("[sparrow-ipc-client] send_request failed: client not connected (fd=%d)\n", fd_);
        fflush(stdout);
        if (callback)
        {
            rapidjson::Document err;
            err.SetObject();
            auto & alloc = err.GetAllocator();
            err.AddMember("error", "IPC client not connected", alloc);
            callback(false, err);
        }

        return;
    }

    int64_t id = next_id_++;
    if (callback)
    {
        pending_requests_[id] = {std::move(callback)};
    }

    rapidjson::StringBuffer s;
    rapidjson::Writer<rapidjson::StringBuffer> writer(s);
    writer.StartObject();
    writer.Key("jsonrpc");
    writer.String("2.0");
    writer.Key("id");
    writer.Int64(id);
    writer.Key("method");
    writer.String(method.c_str());
    writer.Key("params");

    // Write params object directly
    params.Accept(writer);

    writer.EndObject();

    std::string msg = s.GetString();
    msg += '\n';

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
                rapidjson::Document err;
                err.SetObject();
                auto & alloc = err.GetAllocator();
                err.AddMember("error", "Write failed", alloc);
                cb(false, err);
            }
        }
    }
}

void IpcClient::send_request(const std::string & method, ResponseCallback callback)
{
    rapidjson::Document empty_params;
    empty_params.SetObject();
    send_request(method, empty_params, std::move(callback));
}

void IpcClient::dispatch_read()
{
    if (fd_ < 0)
    {
        return;
    }

    char buffer[4096];
    while (true)
    {
        ssize_t bytes_read = read(fd_, buffer, sizeof(buffer));
        if (bytes_read > 0)
        {
            read_buffer_.append(buffer, bytes_read);
        } else if ((bytes_read < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
        {
            break;
        } else if ((bytes_read < 0) && (errno == EINTR))
        {
            continue;
        } else
        {
            // Server closed connection or error
            printf("[sparrow-ipc-client] Connection closed or read error on fd %d (read=%zd, errno=%d: %s)\n",
                fd_, bytes_read, errno, strerror(errno));
            fflush(stdout);
            close();
            return;
        }
    }

    process_lines();
}

void IpcClient::process_lines()
{
    size_t newline_pos;
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

    rapidjson::Document doc;
    doc.Parse(line.c_str());

    if (doc.HasParseError() || !doc.IsObject())
    {
        printf("[sparrow-ipc-client] JSON parse error on received line\n");
        fflush(stdout);
        return;
    }

    if (doc.HasMember("id") && doc["id"].IsInt64())
    {
        int64_t id = doc["id"].GetInt64();
        auto it    = pending_requests_.find(id);
        if (it != pending_requests_.end())
        {
            auto cb = std::move(it->second.callback);
            pending_requests_.erase(it);

            if (doc.HasMember("error") && !doc["error"].IsNull())
            {
                cb(false, doc["error"]);
            } else if (doc.HasMember("result"))
            {
                cb(true, doc["result"]);
            } else
            {
                rapidjson::Value null_val;
                cb(true, null_val);
            }
        }
    } else if (doc.HasMember("method") && doc["method"].IsString())
    {
        if (on_notification_)
        {
            const std::string & method = doc["method"].GetString();
            if (doc.HasMember("params"))
            {
                on_notification_(method, doc["params"]);
            } else
            {
                rapidjson::Value null_params;
                on_notification_(method, null_params);
            }
        }
    }
}
