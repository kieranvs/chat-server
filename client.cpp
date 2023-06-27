#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

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

    char buf[1024];

    while (true)
    {
        memset(buf, 0, sizeof(buf));

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

            editline[editline_len] = c;
            editline_len += 1;

            if (c == '\n')
            {
                write_msg("Pressed enter...");

                editline_len = 0;
                memset(editline, 0, sizeof(editline));
            }

            refresh_editline();
            fflush(0);
        }
    }
}