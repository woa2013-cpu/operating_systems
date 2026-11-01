/*
 * protocol.h
 * ----------------------------------------------------------------------------
 * Message framing shared by the client and the server (Phase 2).
 *
 * WHY FRAMING IS NEEDED
 *   TCP is a byte stream, not a message stream. One send() of 100 bytes may
 *   arrive as several smaller recv() calls, and two consecutive sends may be
 *   merged into one recv(). The client therefore cannot tell where the output
 *   of one command ends just by calling recv() once. To fix this, every
 *   message on the socket has a fixed-size header that gives its length:
 *
 *       +-----------------------+----------------------------------+
 *       | length (4 bytes)      | payload (exactly `length` bytes) |
 *       | uint32, network order |  raw bytes, NOT null-terminated  |
 *       +-----------------------+----------------------------------+
 *
 *   The length is in network byte order (big-endian, via htonl/ntohl) so the
 *   protocol works between machines with different endianness.
 *
 * CONVERSATION RULES
 *   - client -> server: one message per command line, without the newline.
 *     The client never sends empty lines; it skips them locally, the same
 *     way myshell.c's main loop does.
 *   - server -> client: exactly ONE reply per command, holding the command's
 *     combined stdout + stderr (see capture.h). A zero-length reply is valid
 *     and is used when the command printed nothing (e.g. "ls > out.txt").
 *     Because every command gets exactly one reply, the client can simply
 *     alternate send -> receive -> print without ever blocking forever.
 *   - EXIT_COMMAND ("exit"): the client sends it and then
 *     closes and quits without waiting for a reply. The server, on receiving
 *     it, logs it, closes the connection and shuts down. It does not run it.
 *   - Client disconnect: recv_msg() returns 0 and the server shuts down,
 *     as the Phase 2 spec requires.
 *
 * SOCKET RULES
 *   - The server creates its sockets with SOCK_CLOEXEC, i.e.
 *     socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0) and
 *     accept4(..., SOCK_CLOEXEC). Otherwise every program the server
 *     exec()s would inherit the listening and client sockets. (The client
 *     never runs other programs, so it does not need this.)
 *   - The server sets SO_REUSEADDR before bind(). Otherwise restarting it
 *     right after a run fails with "Address already in use" for ~60 s
 *     (the TIME_WAIT state).
 *   - Do NOT call signal(SIGPIPE, SIG_IGN) to survive a vanished client.
 *     An ignored signal stays ignored across exec(), so every command the
 *     server runs would inherit it and pipelines like "yes | head -1" would
 *     print write errors instead of ending quietly. Instead, send_msg()
 *     passes MSG_NOSIGNAL to send(), which affects only that one call.
 * ----------------------------------------------------------------------------
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* Port used when none is given on the command line:
 *   ./server [port]        ./client [server_ip] [port]
 * The course's remote Linux server is shared with other groups, so pass
 * your own port if 5050 is already taken. */
#define DEFAULT_PORT 5050

/* Server address the client uses when none is given (same machine). */
#define DEFAULT_HOST "127.0.0.1"

/* Command that ends the session (same rule as Phase 1's exit). The client
 * sends it and quits; the server shuts down on receiving it. */
#define EXIT_COMMAND "exit"

/* Longest command line the client sends, including the '\0'. Matches
 * MAX_LINE in Phase 1's myshell.c. The server rejects anything longer. */
#define MAX_CMD_LEN 1024

/* Upper bound on a single payload. recv_msg rejects anything larger. This
 * stops a corrupted length header from making us malloc gigabytes, and
 * capture.c truncates command output at this size. 16 MiB is far more than
 * any realistic command output here. */
#define MAX_MSG_SIZE (16u * 1024u * 1024u)

/*
 * send_msg
 *   Sends one framed message: a 4-byte length header, then `len` bytes from
 *   `buf`. It loops until every byte is written, because send() may write
 *   fewer bytes than requested, and retries if send() is interrupted (EINTR).
 *   It uses MSG_NOSIGNAL, so a closed peer gives -1/EPIPE instead of killing
 *   the process with SIGPIPE.
 *
 *   fd   : connected socket descriptor
 *   buf  : payload to send (may be NULL only if len == 0)
 *   len  : payload length in bytes, 0 <= len <= MAX_MSG_SIZE
 *
 *   Returns  0 on success
 *           -1 on error (errno set; EPIPE/ECONNRESET if the peer closed,
 *              EMSGSIZE if len > MAX_MSG_SIZE)
 */
int send_msg(int fd, const char *buf, uint32_t len);

/*
 * recv_msg
 *   Receives one framed message. It first reads exactly 4 header bytes, then
 *   exactly `length` payload bytes, looping over short reads and EINTR. The
 *   payload is copied into a newly malloc'd buffer with one extra '\0'
 *   appended. That byte is not counted in *len; it lets the server pass a
 *   received command straight to functions that expect a C string.
 *
 *   fd   : connected socket descriptor
 *   buf  : out-param. On success, *buf points to the malloc'd payload
 *          (never NULL, even for len 0). The CALLER must free(*buf).
 *   len  : out-param. On success, the payload length (without the '\0').
 *
 *   Returns  1 if a full message was received
 *            0 if the peer closed the connection cleanly before a header
 *              (normal disconnect; the server shuts down on this)
 *           -1 on error: a socket error, a connection closed mid-message,
 *              or a length above MAX_MSG_SIZE. *buf is set to NULL.
 */
int recv_msg(int fd, char **buf, uint32_t *len);

#endif /* PROTOCOL_H */
