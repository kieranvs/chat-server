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

void fatal(const char* msg)
{
    printf("%s", msg);
    exit(-1);
}

struct MessageQueue
{
    std::vector<std::string> messages;
    std::mutex mutex;
    std::condition_variable cv;
};
MessageQueue message_queue;

class ClientThread
{
public:
    ClientThread(int sock)
    {
        client_socket = sock;
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
            // if (bytes == 0) die();
            // if (bytes < 0) die();
            auto orig_size = message_buf.size();
            message_buf.resize(message_buf.size() + bytes);
            memcpy(&message_buf[orig_size], recv_buf, bytes);

            auto found = std::find(message_buf.begin(), message_buf.end(), '\n');
            if (found != message_buf.end())
            {
                username = std::string(message_buf.begin(), found);
                message_buf.erase(message_buf.begin(), found + 1);
                break;
            }
        }

        {
            char msg_buf[1024];
            snprintf(msg_buf, sizeof(msg_buf), "%s connected.\n", username.c_str());

            std::lock_guard lock(message_queue.mutex);
            message_queue.messages.emplace_back(msg_buf);
            message_queue.cv.notify_all();
        }

        pollfd pfds{};
        pfds.fd = client_socket;
        pfds.events = POLLIN;

        while (true)
        {
            while (true)
            {
                auto found = std::find(message_buf.begin(), message_buf.end(), '\n');
                if (found != message_buf.end())
                {
                    auto msg = std::string(message_buf.begin(), found);
                    message_buf.erase(message_buf.begin(), found + 1);

                    char msg_buf[1500];
                    snprintf(msg_buf, sizeof(msg_buf), "[%s] %s\n", username.c_str(), msg.data());

                    std::lock_guard lock(message_queue.mutex);
                    message_queue.messages.emplace_back(msg_buf);
                    message_queue.cv.notify_all();
                }
                else
                {
                    break;
                }
            }
            
            int poll_result = poll(&pfds, 1, -1);

            if (pfds.revents & POLLIN)
            {
                int bytes = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
                if (bytes == 0) break;
                if (bytes < 0) break;
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
            snprintf(msg_buf, sizeof(msg_buf), "%s disconnected.\n", username.c_str());

            std::lock_guard lock(message_queue.mutex);
            message_queue.messages.emplace_back(msg_buf);
            message_queue.cv.notify_all();
        }
    }

    void run_send()
    {
        while (true)
        {
            if (dead) break;
            std::unique_lock lock(message_queue.mutex);
            while (message_queue.messages.size() > last_msg_id_sent + 1)
            {
                const auto& msg = message_queue.messages[last_msg_id_sent + 1];
                int send_result = send(client_socket, msg.data(), msg.length(), 0);
                last_msg_id_sent += 1;
            }
            message_queue.cv.wait(lock);
        }
    }

private:
    std::optional<std::thread> recv_thread;
    std::optional<std::thread> send_thread;
    
    int client_socket;
    bool dead = false;

    int last_msg_id_sent = -1;

    std::string username;
};

int main(int argc, char *argv[])
{
    if (argc != 2) fatal("Usage: port\n");

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
        printf("Waiting for a client to connect...\n");
        listen(serverSd, 5); // listen for up to 5 requests at a time

        sockaddr_in newSockAddr{};
        socklen_t newSockAddrSize = sizeof(newSockAddr);
        
        int newSd = accept(serverSd, (sockaddr *)&newSockAddr, &newSockAddrSize);
        if(newSd < 0)
        {
            printf("Error accepting request from client!\n");
        }
        else
        {
            printf("Connected with client!\n");
            clients.push_back(new ClientThread(newSd));
        }
    }
    
    close(serverSd);
    printf("Server terminated\n");
    return 0;   
}