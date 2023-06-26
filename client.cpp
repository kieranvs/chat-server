#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <cstring>
#include <termios.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
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
	int port = atoi(argv[2]);
	char* username = argv[3];

	// Connect to the server
	auto host = gethostbyname(serverIp); 
    sockaddr_in sendSockAddr{};
    sendSockAddr.sin_family = AF_INET; 
    sendSockAddr.sin_addr.s_addr = inet_addr(inet_ntoa(*(in_addr*)*host->h_addr_list));
    sendSockAddr.sin_port = htons(port);
    int clientSd = socket(AF_INET, SOCK_STREAM, 0);
    int status = connect(clientSd, (sockaddr*) &sendSockAddr, sizeof(sendSockAddr));
    if(status < 0) fatal("Error connecting to socket!\n");

	char buf[1024];

    // Log in
    {
    	snprintf(buf, 1024, "%s\n", username);
		send(clientSd, buf, strlen(buf), 0);
    }
    
    // Put the terminal in 'raw' mode
	struct termios old{};
    if (tcgetattr(0, &old) < 0) fatal("Error in tcsetattr");
    old.c_lflag &= ~ICANON;
    old.c_lflag &= ~ECHO;
    old.c_cc[VMIN] = 1;
    old.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &old) < 0) fatal("Error in tcsetattr");
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

    // Get the terminal size
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    term_width = w.ws_col;
    term_height = w.ws_row;

    // Set up screen
    printf(CSI "2J"); // clear screen
    refresh_editline();
    fflush(0);
	
	pollfd pfds[2];
	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = clientSd;
	pfds[1].events = POLLIN;

	std::vector<char> message_buf;

    while (true)
    {
    	memset(buf, 0, sizeof(buf));
    	int poll_result = poll(pfds, 2, -1); // inf timeout

    	if (pfds[0].revents & POLLIN)
    	{
		    // STDIN ready
	    	int numRead = read(0, buf, 1);
		    if (numRead > 0)
		    {
		    	char c = buf[0];

		    	editline[editline_len] = buf[0];
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
  		}
  
  		if (pfds[1].revents & POLLIN)
  		{
    		// Socket ready
    		int bytes = recv(clientSd, buf, sizeof(buf), 0);
            if (bytes == 0)
        	{
        		write_msg("Server disconnected");
        		fflush(0);
    			exit(0);
        	}
            if (bytes < 0) fatal("Server communication error\n");

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

  		if (pfds[1].revents & (POLLERR | POLLHUP))
  		{
  			write_msg("Server disconnected");
  			fflush(0);
    		exit(0);
  		}
    }
}