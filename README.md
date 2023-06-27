# Chat Server

Starter kit for the practical component of the lesson on sockets.

## Protocol

1. Client connects to the server and sends one byte to specify version of the protocol. At the moment this must be `0x01` - the byte with value `1`, not the ascii char '1'.

1. Next the client sends the username to log in as, terminated by \n.

1. Thereafter the client sends messages to send, terminated by \n, and the server sends messages to display, terminated by \n.