# Chat Server

Starter kit for the practical component of the lesson on sockets.

## Protocol

### Version Select

1. Client connects to the server and sends one byte to specify version of the protocol. E.g. for version one, this must be `0x01` - the byte with value `1`, not the ascii char '1'. The protocol then proceeds according to the version as described below.

| Length | Contents         |
|--------|------------------|
| 1      | Version selector |

### Version 2

1. Next the client sends the login packet:

| Length   | Contents                        |
|----------|---------------------------------|
| 1        | Length in bytes of the username |
| Variable | Username encoded as UTF-8       |
| 1        | Length in bytes of the password |
| Variable | Password, encoding irrelevant   |

2. The server replies with the login response packet:

| Length | Contents       |
|--------|----------------|
| 1      | Login response |

where the login response has one of the following values:

| Response | Description |
|----------|-------------|
| 0        | OK          |
| 1        | Bad login   |

3. After a successful login, the client may send, at any time, client message packets:

| Length   | Contents                        |
|----------|---------------------------------|
| 2        | Length in bytes of the message  |
| Variable | Message encoded as UTF-8        |

4. The server may send, at any time, server message packets:

| Length   | Contents                              |
|----------|---------------------------------------|
| 1        | Length in bytes of the username       |
| Variable | Username encoded as UTF-8             |
| 4        | Unix timestamp, signed 32 bit integer |
| 2        | Length in bytes of the message        |
| Variable | Message encoded as UTF-8              |

### Version 1

1. Next the client sends the username to log in as, terminated by `\n`.

1. Thereafter the client sends messages to send, terminated by `\n`, and the server sends messages to display, terminated by `\n`.
