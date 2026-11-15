#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"

// Sends exactly `len` bytes from `data` over the socket.
// send() is allowed to write fewer bytes than we ask for (for example when the
// socket's kernel buffer is almost full), so we keep calling it until every
// byte has gone out. Returns 0 on success and -1 on error.
static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0; // Number of bytes written so far

    while (sent < len) {
        // MSG_NOSIGNAL: if the other side has closed the connection, send()
        // returns -1 with errno EPIPE instead of killing our process with SIGPIPE
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);

        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by a signal before sending anything, so try again
            }
            return -1; // Real error (e.g. connection reset)
        }

        sent += n; // Move forward past the bytes that were written
    }

    return 0;
}

// Reads exactly `len` bytes from the socket into `data`.
// Like send(), recv() can return fewer bytes than requested because TCP
// delivers data in pieces, so we loop until the full amount has arrived.
// Returns 1 if all bytes were read, 0 if the peer closed the connection before
// sending anything, and -1 on error or if the connection closed halfway.
static int recv_all(int fd, char *data, size_t len) {
    size_t received = 0; // Number of bytes read so far

    while (received < len) {
        ssize_t n = recv(fd, data + received, len - received, 0);

        if (n < 0) {
            if (errno == EINTR) {
                continue; // Interrupted by a signal, so try again
            }
            return -1;
        }

        // recv() returns 0 when the other side has closed the connection
        if (n == 0) {
            if (received == 0) {
                return 0; // Clean close: nothing of this message had arrived yet
            }
            errno = ECONNRESET;
            return -1; // Closed in the middle of a message, so the data is incomplete
        }

        received += n;
    }

    return 1;
}

// Sends one message: a 4-byte length header followed by the payload.
// The header lets the receiver know exactly how many bytes belong to this
// message, since TCP itself has no message boundaries.
int send_msg(int fd, const char *buf, uint32_t len) {
    // Refuse messages larger than the receiver is willing to accept
    if (len > MAX_MSG_SIZE) {
        errno = EMSGSIZE;
        return -1;
    }

    // Convert the length to network byte order (big-endian) so that both
    // machines read the same number regardless of their own byte order
    uint32_t header = htonl(len);

    if (send_all(fd, (const char *)&header, sizeof(header)) == -1) {
        return -1;
    }

    // A zero-length message is just the header (used for commands with no output)
    if (len > 0 && send_all(fd, buf, len) == -1) {
        return -1;
    }

    return 0;
}

// Receives one message sent by send_msg().
// The payload is returned in a newly allocated buffer with an extra '\0' at
// the end, so text can be used directly as a C string. The caller must free it.
int recv_msg(int fd, char **buf, uint32_t *len) {
    uint32_t header;
    *buf = NULL;

    // Step 1: read the 4-byte length header
    int status = recv_all(fd, (char *)&header, sizeof(header));
    if (status <= 0) {
        return status; // 0 = peer disconnected cleanly, -1 = error
    }

    // Convert the length back from network byte order to the host's order
    uint32_t msg_len = ntohl(header);

    // Reject absurd lengths so a corrupted header cannot make us allocate gigabytes
    if (msg_len > MAX_MSG_SIZE) {
        errno = EMSGSIZE;
        return -1;
    }

    // Step 2: allocate room for the payload plus the terminating '\0'
    char *data = malloc(msg_len + 1);
    if (data == NULL) {
        return -1;
    }

    // Step 3: read exactly msg_len payload bytes
    if (msg_len > 0) {
        status = recv_all(fd, data, msg_len);
        if (status != 1) {
            free(data);
            if (status == 0) {
                errno = ECONNRESET; // Peer closed after the header but before the payload
            }
            return -1; // The message is incomplete either way
        }
    }

    data[msg_len] = '\0';
    *buf = data;
    *len = msg_len;
    return 1;
}
