#include <iostream>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <optional>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <poll.h>
#include <algorithm>
#include <arpa/inet.h>
#include <ctime>
#include <signal.h>

void fatal(const char* msg)
{
    printf("%s", msg);
    exit(-1);
}

void print_timestamp()
{
    auto timepoint = std::time(0);
    std::tm* now = std::localtime(&timepoint);
    printf("[%02d:%02d:%02d] ", now->tm_hour, now->tm_min, now->tm_sec);
}

struct Message
{
    std::string username;
    std::string message;
    int32_t timestamp;
};

struct MessageQueue
{
    std::vector<Message> messages;
    std::mutex mutex;
    std::condition_variable cv;
};
MessageQueue message_queue;

class ClientThread
{
public:
    ClientThread(int sock, const std::string& id_string)
    {
        client_socket = sock;
        ip_addr = id_string;

        {
            std::lock_guard lock(message_queue.mutex);
            auto num_messages = message_queue.messages.size();
            if (num_messages > 10) last_msg_id_sent = num_messages - 9;
        }

        recv_thread.emplace(&ClientThread::run_recv, this);
        send_thread.emplace(&ClientThread::run_send, this);
    }

    void run_recv()
    {
        std::vector<char> message_buf;
        char recv_buf[1024];

        // Get login message
        while (true)
        {
            int bytes = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
            if (bytes == 0)
            {
                disconnect();
                printf("\033[31m");
                print_timestamp();
                printf("%s: Client disconnected before login message\033[39m\n", ip_addr.c_str());
                return;
            }
            if (bytes < 0)
            {
                disconnect();
                printf("\033[31m");
                print_timestamp();
                printf("%s: Client error before login message\033[39m\n", ip_addr.c_str());
                return;
            }

            auto orig_size = message_buf.size();
            message_buf.resize(message_buf.size() + bytes);
            memcpy(&message_buf[orig_size], recv_buf, bytes);

            if (message_buf[0] == 0x01)
            {
                auto found = std::find(message_buf.begin(), message_buf.end(), '\n');
                if (found != message_buf.end())
                {
                    if (message_buf.size() <= 2)
                    {
                        disconnect();
                        printf("\033[31m");
                        print_timestamp();
                        printf("%s: Client sent malformed login message\033[39m\n", ip_addr.c_str());
                        return;
                    }

                    username = std::string(message_buf.begin() + 1, found);
                    message_buf.erase(message_buf.begin(), found + 1);
                    protocol_version = 1;
                    break;
                }
            }
            else if (message_buf[0] == 0x02)
            {
                if (message_buf.size() < 2) continue;

                char username_len = message_buf[1];
                if (username_len == 0)
                {
                    disconnect();
                    printf("\033[31m");
                    print_timestamp();
                    printf("%s: Client sent malformed login packet\033[39m\n", ip_addr.c_str());
                    return;
                }

                if (message_buf.size() < (3 + username_len)) continue;

                char password_len = message_buf[username_len + 2];
                if (password_len == 0)
                {
                    disconnect();
                    printf("\033[31m");
                    print_timestamp();
                    printf("%s: Client sent malformed login packet\033[39m\n", ip_addr.c_str());
                    return;
                }

                if (message_buf.size() < (3 + username_len + password_len)) continue;

                username = std::string(&message_buf[2], username_len);
                std::string password = std::string(&message_buf[3 + username_len], password_len);

                print_timestamp();
                printf("%s: Client attempting to connect with username=%s\n", ip_addr.c_str(), username.c_str());
                message_buf.erase(message_buf.begin(), message_buf.begin() + 3 + username_len + password_len);
                protocol_version = 2;
                break;
            }
            else
            {
                disconnect();
                printf("\033[31m");
                print_timestamp();
                printf("%s: Client tried to connect with unsupported version\033[39m\n", ip_addr.c_str());
                return;
            }
        }

        if (protocol_version == 2)
        {
            char resp[] = { 0x00 };
            send(client_socket, resp, 1, 0);
        }

        {
            char msg_buf[1024];
            snprintf(msg_buf, sizeof(msg_buf), "%s connected.", username.c_str());
            printf("\033[34m");
            print_timestamp();
            printf("%s: connected as %s with protocol version %d.\033[39m\n", ip_addr.c_str(), username.c_str(), protocol_version);

            std::lock_guard lock(message_queue.mutex);
            message_queue.messages.push_back({"", msg_buf, std::time(0)});
            message_queue.cv.notify_all();
        }

        pollfd pfds{};
        pfds.fd = client_socket;
        pfds.events = POLLIN;

        while (true)
        {
            while (true)
            {
                if (protocol_version == 1)
                {
                    auto found = std::find(message_buf.begin(), message_buf.end(), '\n');
                    if (found != message_buf.end())
                    {
                        auto msg = std::string(message_buf.begin(), found);
                        message_buf.erase(message_buf.begin(), found + 1);

                        char msg_buf[1500];
                        snprintf(msg_buf, sizeof(msg_buf), "[%s] %s\n", username.c_str(), msg.data());
                        printf("\033[37m");
                        print_timestamp();
                        printf("%s\033[39m", msg_buf);

                        std::lock_guard lock(message_queue.mutex);
                        message_queue.messages.push_back({username, msg, std::time(0)});
                        message_queue.cv.notify_all();
                    }
                    else
                    {
                        break;
                    }
                }
                else if (protocol_version == 2)
                {
                    if (message_buf.size() < 2) break;
                    char msg_len = (message_buf[0] << 8) + message_buf[1];
                    if (message_buf.size() < (2 + msg_len)) break;

                    auto msg = std::string(message_buf.begin() + 2, message_buf.begin() + 2 + msg_len);
                    message_buf.erase(message_buf.begin(), message_buf.begin() + 2 + msg_len);

                    char msg_buf[1500];
                    snprintf(msg_buf, sizeof(msg_buf), "[%s] %s\n", username.c_str(), msg.data());
                    printf("\033[37m");
                    print_timestamp();
                    printf("%s\033[39m", msg_buf);

                    std::lock_guard lock(message_queue.mutex);
                    message_queue.messages.push_back({username, msg, std::time(0)});
                    message_queue.cv.notify_all();
                }
            }
            
            int poll_result = poll(&pfds, 1, -1);

            if (pfds.revents & POLLIN)
            {
                int bytes = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
                if (bytes == 0) break;
                if (bytes < 0) break;

                if (protocol_version == 1)
                {
                    // Basic sanitisation
                    for (int i = 0; i < bytes; i++)
                    {
                        if (recv_buf[i] == '\b') recv_buf[i] = ' ';
                        if (recv_buf[i] == '\0') recv_buf[i] = ' ';
                    }
                }

                auto orig_size = message_buf.size();
                message_buf.resize(message_buf.size() + bytes);
                memcpy(&message_buf[orig_size], recv_buf, bytes);
            }

            if(pfds.revents & (POLLERR | POLLHUP))
            {
                break;
            }
        }

        close(client_socket);
        dead = true;

        {
            char msg_buf[1024];
            snprintf(msg_buf, sizeof(msg_buf), "%s disconnected.", username.c_str());
            printf("\033[34m");
            print_timestamp();
            printf("%s\033[39m\n", msg_buf);

            std::lock_guard lock(message_queue.mutex);
            message_queue.messages.push_back({"", msg_buf, std::time(0)});
            message_queue.cv.notify_all();
        }
    }

    void disconnect()
    {
        close(client_socket);
        dead = true;

        std::lock_guard lock(message_queue.mutex);
        message_queue.cv.notify_all();
    }

    void run_send()
    {
        std::vector<char> send_output_buffer;
        while (true)
        {
            if (dead) break;

            {
                std::unique_lock lock(message_queue.mutex);
                while (message_queue.messages.size() > last_msg_id_sent + 1)
                {
                    const auto& msg = message_queue.messages[last_msg_id_sent + 1];

                    if (protocol_version == 1)
                    {
                        auto orig_size = send_output_buffer.size();
                        if (msg.username.size() != 0)
                        {
                            send_output_buffer.resize(send_output_buffer.size() + msg.username.size() + 3 + msg.message.size() + 1);
                            send_output_buffer[orig_size] = '[';
                            memcpy(&send_output_buffer[orig_size + 1], msg.username.data(), msg.username.size());
                            send_output_buffer[orig_size + 1 + msg.username.size()] = ']';
                            send_output_buffer[orig_size + 1 + msg.username.size() + 1] = ' ';
                            memcpy(&send_output_buffer[orig_size + 3 + msg.username.size()], msg.message.data(), msg.message.size());
                            send_output_buffer[orig_size + 3 + msg.username.size() + msg.message.size()] = '\n';
                        }
                        else
                        {
                            send_output_buffer.resize(send_output_buffer.size() + msg.message.size() + 1);
                            memcpy(&send_output_buffer[orig_size], msg.message.data(), msg.message.size());
                            send_output_buffer[orig_size + msg.message.size()] = '\n';
                        }
                    }
                    else if (protocol_version == 2)
                    {
                        auto orig_size = send_output_buffer.size();
                        send_output_buffer.resize(send_output_buffer.size() + 7 + msg.username.size() + msg.message.size());
                        uint8_t username_len = (uint8_t)(msg.username.size());
                        uint16_t msg_len = (uint16_t)(msg.message.size());
                        memcpy(&send_output_buffer[orig_size], &username_len, 1);
                        memcpy(&send_output_buffer[orig_size + 1], msg.username.data(), username_len);
                        memcpy(&send_output_buffer[orig_size + 1 + username_len], &msg.timestamp, 4);
                        memcpy(&send_output_buffer[orig_size + 5 + username_len], &msg_len, 2);
                        memcpy(&send_output_buffer[orig_size + 7 + username_len], msg.message.data(), msg_len);
                    }

                    last_msg_id_sent += 1;
                }
            }

            size_t bytes_sent = 0;
            while (bytes_sent < send_output_buffer.size())
            {
                int send_result = send(client_socket, &send_output_buffer[bytes_sent], send_output_buffer.size() - bytes_sent, 0);
                if (send_result == -1) return;
                bytes_sent += send_result;
            }
            send_output_buffer.clear();

            {
                std::unique_lock lock(message_queue.mutex);

                if (message_queue.messages.size() > last_msg_id_sent + 1)
                    continue;
                else
                    message_queue.cv.wait(lock);
            }
        }
    }

private:
    std::optional<std::thread> recv_thread;
    std::optional<std::thread> send_thread;
    
    int client_socket;
    bool dead = false;

    int last_msg_id_sent = -1;

    std::string username;
    std::string ip_addr;

    int protocol_version;
};

void sigpipe_handler(int signum)
{
}

int main(int argc, char *argv[])
{
    if (argc != 2) fatal("Usage: port\n");

    sigaction(SIGPIPE, (const struct sigaction*)sigpipe_handler, NULL);

    int port = atoi(argv[1]);
    
    std::vector<ClientThread*> clients;
     
    sockaddr_in servAddr{};
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(port);
 
    int serverSd = socket(AF_INET, SOCK_STREAM, 0);
    if(serverSd < 0) fatal("Error establishing the server socket\n");
    
    int bindStatus = bind(serverSd, (struct sockaddr*) &servAddr, sizeof(servAddr));
    if (bindStatus < 0) fatal("Error binding socket to local address\n");

    while (true)
    {
        listen(serverSd, 5); // listen for up to 5 requests at a time

        sockaddr_in newSockAddr{};
        socklen_t newSockAddrSize = sizeof(newSockAddr);
        
        int newSd = accept(serverSd, (sockaddr *)&newSockAddr, &newSockAddrSize);
        if(newSd < 0)
        {
            printf("\033[31m");
            print_timestamp();
            printf("Error accepting request from client!\033[39m\n");
        }
        else
        {
            char ip_buf[512];
            snprintf(ip_buf, 512, "%s:%d", inet_ntoa(newSockAddr.sin_addr), (int)ntohs(newSockAddr.sin_port));
            print_timestamp();
            printf("Connection from %s\n", ip_buf);
            clients.push_back(new ClientThread(newSd, std::string(ip_buf)));
        }
    }
    
    close(serverSd);
    print_timestamp();
    printf("Server terminated\n");
    return 0;   
}