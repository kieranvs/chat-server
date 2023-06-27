#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <algorithm>
#include <vector>
#include <string>

#define ESC "\x1b"
#define CSI "\x1b["

void fatal(const char* msg)
{
    printf("%s", msg);
    exit(-1);
}

char editline[512];
int editline_len = 0;

int term_width;
int term_height;

int next_free_line = 1;

void write_msg(const char* msg)
{
    if (next_free_line == term_height)
    {
        printf(CSI "1;S"); // scroll up one
        printf(CSI "%d;1H", next_free_line - 1); // go to message line
        printf(CSI "2K"); // clear the line (had editline contents on it)
        printf("%.*s", term_width, msg);
    }
    else
    {
        printf(CSI "%d;1H", next_free_line); // go to free line
        printf("%.*s", term_width, msg);
        next_free_line += 1;
    }
}

void refresh_editline()
{
    printf(CSI "%d;1H", term_height); // go to last line
    printf(CSI "2K"); // clear line
    printf("> %.*s", term_width - 2, editline);
}

int main(int argc, char* argv[])
{
    // Parse arguments
    if(argc != 4) fatal("Usage: ip_address port username\n");
    char* serverIp = argv[1];
    char* port = argv[2];
    char* username = argv[3];

#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsaData;
    auto iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) fatal("WSAStartup failed\n");
#endif

    // Connect to the server
#ifdef _WIN32
    addrinfo* result = NULL;

    iResult = getaddrinfo(serverIp, port, nullptr, &result);
    if (iResult != 0) fatal("getaddrinfo failed\n");

    SOCKET clientSd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSd == INVALID_SOCKET) fatal("socket failed\n");

    iResult = connect(clientSd, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) fatal("Unable to connect to server\n");

    // unsigned long mode = 0; // 0 blocking, 1 non-blocking
    // ioctlsocket(clientSd, FIONBIO, &mode);

    freeaddrinfo(result);
#else
    auto host = gethostbyname(serverIp);
    sockaddr_in sendSockAddr{};
    sendSockAddr.sin_family = AF_INET;
    sendSockAddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)*host->h_addr_list));
    sendSockAddr.sin_port = htons(atoi(port));
    int clientSd = socket(AF_INET, SOCK_STREAM, 0);
    int status = connect(clientSd, (sockaddr*) &sendSockAddr, sizeof(sendSockAddr));
    if(status < 0) fatal("Error connecting to socket!\n");
#endif

    char buf[1024];

    // Log in
    {
        snprintf(buf, 1024, "%c%s\n", 0x01, username);
        send(clientSd, buf, strlen(buf), 0);
    }
    
    // Put the terminal in 'raw' mode
    {
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode &= ~ENABLE_ECHO_INPUT;
        dwMode &= ~ENABLE_INSERT_MODE;
        dwMode &= ~ENABLE_LINE_INPUT;
        dwMode &= ~ENABLE_MOUSE_INPUT;
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
#else
        struct termios old{};
        if (tcgetattr(0, &old) < 0) fatal("Error in tcsetattr");
        old.c_lflag &= ~ICANON;
        old.c_lflag &= ~ECHO;
        old.c_cc[VMIN] = 1;
        old.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &old) < 0) fatal("Error in tcsetattr");
        fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
#endif
    }

    // Get the terminal size
    {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        term_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        term_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
        winsize w{};
        ioctl(0, TIOCGWINSZ, &w);
        term_width = w.ws_col;
        term_height = w.ws_row;
#endif
    }
    
    // Set up screen
    printf(CSI "2J"); // clear screen
    refresh_editline();
    fflush(0);

#ifdef _WIN32
    auto socket_event = WSACreateEvent();
    iResult = WSAEventSelect(clientSd, socket_event, FD_READ);
    if (iResult != 0) fatal("Error in WSAEventSelect");

    HANDLE eventHandles[] =
    {
        GetStdHandle(STD_INPUT_HANDLE),
        socket_event
    };
#else
    pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = clientSd;
    pfds[1].events = POLLIN;
#endif

    std::vector<char> message_buf;

    while (true)
    {
        memset(buf, 0, sizeof(buf));
#ifdef _WIN32
        DWORD result = WSAWaitForMultipleEvents(sizeof(eventHandles)/sizeof(eventHandles[0]), &eventHandles[0], FALSE, WSA_INFINITE, FALSE);
#else
        int poll_result = poll(pfds, 2, -1); // inf timeout
#endif

#ifdef _WIN32
        if (result == WSA_WAIT_EVENT_0)
#else
        if (pfds[0].revents & POLLIN)
#endif
        {
            // STDIN ready
#ifdef _WIN32
            INPUT_RECORD record;
            DWORD numRead;
            if(!ReadConsoleInput(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &numRead))
            {
                fatal("stdin read error\n");
            }

            if(record.EventType != KEY_EVENT) continue;
            if(!record.Event.KeyEvent.bKeyDown) continue;

            char c = record.Event.KeyEvent.uChar.AsciiChar;
            if (c == '\r') c = '\n';
#else
            int numRead = read(0, buf, 1);
            if (numRead <= 0) continue;
            char c = buf[0];
#endif
            editline[editline_len] = c;
            editline_len += 1;

            if (c == '\n')
            {
                send(clientSd, editline, strlen(editline), 0);

                editline_len = 0;
                memset(editline, 0, sizeof(editline));
            }

            refresh_editline();
            fflush(0);
        }

#ifdef _WIN32
        if (result == WSA_WAIT_EVENT_0 + 1)
#else
        if (pfds[1].revents & POLLIN)
#endif
        {
            // Socket ready
            int bytes = recv(clientSd, buf, sizeof(buf), 0);
            if (bytes == 0)
            {
                fatal("Server disconnected\n");
            }
            if (bytes < 0)
            {
#ifdef _WIN32
                if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
#endif
                fatal("Server communication error\n");
            }

            auto orig_size = message_buf.size();
            message_buf.resize(message_buf.size() + bytes);
            memcpy(&message_buf[orig_size], buf, bytes);

            while (true)
            {
                auto found = std::find(message_buf.begin(), message_buf.end(), '\n');
                if (found != message_buf.end())
                {
                    auto msg = std::string(message_buf.begin(), found + 1);
                    message_buf.erase(message_buf.begin(), found + 1);
                    write_msg(msg.c_str());
                }
                else
                    break;
            }

            refresh_editline();
            fflush(0);
        }

#ifndef _WIN32
        if (pfds[1].revents & (POLLERR | POLLHUP))
            fatal("Server disconnected\n");
#endif
    }
}