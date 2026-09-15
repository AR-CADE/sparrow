#ifndef SPARROW_IPC_CLIENT_HPP
#define SPARROW_IPC_CLIENT_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <rapidjson/document.h>

class IpcClient
{
  public:
    using ResponseCallback =
        std::function<void (bool success, const rapidjson::Value & result_or_error)>;
    using NotificationCallback =
        std::function<void (const std::string & method, const rapidjson::Value & params)>;

    IpcClient();
    ~IpcClient();

    void set_fd(int fd);
    int get_fd() const
    {
        return fd_;
    }

    bool is_connected() const
    {
        return fd_ >= 0;
    }

    void close();

    void send_request(const std::string & method,
        const rapidjson::Document & params,
        ResponseCallback callback);

    void send_request(const std::string & method,
        ResponseCallback callback);

    void set_notification_handler(NotificationCallback handler)
    {
        on_notification_ = std::move(handler);
    }

    void dispatch_read();

  private:
    void process_lines();
    void handle_json_line(const std::string & line);

    int fd_ = -1;
    int64_t next_id_ = 1;
    std::string read_buffer_;

    struct PendingRequest
    {
        ResponseCallback callback;
    };

    std::map<int64_t, PendingRequest> pending_requests_;
    NotificationCallback on_notification_;
};

#endif // SPARROW_IPC_CLIENT_HPP
