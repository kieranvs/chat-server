#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <cstring>
#include <ctime>
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
    if(argc != 5) fatal("Usage: ip_address port username password\n");
    char* serverIp = argv[1];
    char* port = argv[2];
    char* username = argv[3];
    char* password = argv[4];

    // Initialize Winsock
    WSADATA wsaData;
    auto iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) fatal("WSAStartup failed\n");

    // Connect to the server
    addrinfo* result = NULL;

    iResult = getaddrinfo(serverIp, port, nullptr, &result);
    if (iResult != 0) fatal("getaddrinfo failed\n");

    SOCKET clientSd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSd == INVALID_SOCKET) fatal("socket failed\n");

    iResult = connect(clientSd, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) fatal("Unable to connect to server\n");

    freeaddrinfo(result);

    char buf[1024];

    // Log in
    {
        snprintf(buf, 1024, "%c%c%s%c%s", 0x02, (uint8_t)strlen(username), username, (uint8_t)strlen(password), password);
        send(clientSd, buf, strlen(buf), 0);

        recv(clientSd, buf, 1, 0);
        if (buf[0] != 0) fatal("Login rejected\n");
    }

    // Put the terminal in 'raw' mode
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD dwMode = 0;
        GetConsoleMode(hOut, &dwMode);
        dwMode &= ~ENABLE_ECHO_INPUT;
        dwMode &= ~ENABLE_INSERT_MODE;
        dwMode &= ~ENABLE_LINE_INPUT;
        dwMode &= ~ENABLE_MOUSE_INPUT;
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }

    // Get the terminal size
    {
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        term_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        term_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    
    // Set up screen
    printf(CSI "2J"); // clear screen
    refresh_editline();
    fflush(0);

    auto socket_event = WSACreateEvent();
    iResult = WSAEventSelect(clientSd, socket_event, FD_READ);
    if (iResult != 0) fatal("Error in WSAEventSelect");

    HANDLE eventHandles[] =
    {
        GetStdHandle(STD_INPUT_HANDLE),
        socket_event
    };

    std::vector<char> message_buf;

    while (true)
    {
        memset(buf, 0, sizeof(buf));

        DWORD result = WSAWaitForMultipleEvents(sizeof(eventHandles)/sizeof(eventHandles[0]), &eventHandles[0], FALSE, WSA_INFINITE, FALSE);

        if (result == WSA_WAIT_EVENT_0)
        {
            // STDIN ready
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

            if (c == '\n')
            {
                char editline_len_buf[2] = { 0x00, editline_len };
                send(clientSd, editline_len_buf, 2, 0);
                send(clientSd, editline, strlen(editline), 0);

                editline_len = 0;
                memset(editline, 0, sizeof(editline));
            }
            else
            {
                editline[editline_len] = c;
                editline_len += 1;
            }
        }

        if (result == WSA_WAIT_EVENT_0 + 1)
        {
            // Socket ready
            int bytes = recv(clientSd, buf, sizeof(buf), 0);
            if (bytes == 0)
            {
                fatal("Server disconnected\n");
            }
            if (bytes < 0)
            {

                if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
                fatal("Server communication error\n");
            }

            auto orig_size = message_buf.size();
            message_buf.resize(message_buf.size() + bytes);
            memcpy(&message_buf[orig_size], buf, bytes);

            while (true)
            {
                if (message_buf.size() < 7) break;
                uint8_t username_len = message_buf[0];
                if (message_buf.size() < 7 + username_len) break;
                auto username = std::string(message_buf.begin() + 1, message_buf.begin() + 1 + username_len);
                uint32_t timestamp = *(uint32_t*)(&message_buf[username_len + 1]);
                uint16_t msg_len = *(uint16_t*)(&message_buf[username_len + 5]);
                if (message_buf.size() < 7 + username_len + msg_len) break;
                auto msg = std::string(message_buf.begin() + 7 + username_len, message_buf.begin() + 7 + username_len + msg_len);

                message_buf.erase(message_buf.begin(), message_buf.begin() + 7 + username_len + msg_len);


                time_t tt = timestamp;
                auto tm = localtime(&tt);
                
                if (username.size() != 0)
                    snprintf(buf, 1024, "[%02d:%02d:%02d] [%s] %s", tm->tm_hour, tm->tm_min, tm->tm_sec, username.c_str(), msg.c_str());
                else
                    snprintf(buf, 1024, "[%02d:%02d:%02d] %s", tm->tm_hour, tm->tm_min, tm->tm_sec, msg.c_str());
                write_msg(buf);
            }
        }

        refresh_editline();
        fflush(0);
    }
}