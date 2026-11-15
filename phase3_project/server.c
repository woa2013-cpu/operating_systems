#define _GNU_SOURCE // Needed for accept4(), which is a Linux extension

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>     // pthread_create, pthread_detach, pthread_mutex_*
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "protocol.h"
#include "capture.h"


// Everything a client thread needs to know about its client. pthread_create() can only pass ONE
// pointer to the new thread, so all of it is bundled in this struct. main() allocates one per client
// and the thread frees it when the client's session ends.
typedef struct {
    int fd;        // Connected socket for this client (returned by accept4)
    int id;        // Client number, also used as the thread number: "Client #2", "Thread-2"
    char tag[64];  // Log prefix built once, e.g. "[Client #2 - 192.168.1.101:56790]"
} client_info_t;


// All client threads print to the SAME stdout. Without a lock, two threads printing at the same time
// can mix their lines together (e.g. half of one [RECEIVED] line, then half of another). Every block
// of log lines is printed while holding this mutex, so each block appears in one piece.
// PTHREAD_MUTEX_INITIALIZER sets it up at compile time, so no pthread_mutex_init() call is needed.
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;


// Returns 1 if the command is "exit", 0 in other cases
// Only the first word is checked (spaces around it are ignored)
static int is_exit_command(const char *cmd){
    char first_word[MAX_CMD_LEN];

    // sscanf with %s skips leading spaces and copies the first word, %1023s so no overflow
    if (sscanf(cmd, "%1023s", first_word) != 1){
        return 0; // The command was empty or only spaces
    }

    return strcmp(first_word, EXIT_COMMAND) == 0;
}

// Prints the server log lines that describe the result of one command
// A command that ran prints its whole output; a failed one prints [ERROR] and the error message
// tag is the client's log prefix, e.g. "[Client #2 - 192.168.1.101:56790]"
// The caller must hold log_mutex, so the whole block is printed without other threads in between
static void log_result(const char *tag, const char *cmd, cmd_status_t status, const char *output, size_t output_len){
    if (status == CMD_OK){
        printf("[OUTPUT] %s Sending output to client:\n", tag);

        // fwrite prints exactly output_len bytes (NULL when there is nothing to print)
        if (output_len > 0){
            fwrite(output, 1, output_len, stdout);
        }
    }

    else{
        // Specifying error ttype
        if (status == CMD_NOT_FOUND){
            printf("[ERROR] %s Command not found: \"%s\"\n", tag, cmd);
        }
        else{
            printf("[ERROR] %s Server failed to execute: \"%s\"\n", tag, cmd);
        }

        // %.*s prints exactly msg_len characters
        int msg_len = (int)output_len;
        if (msg_len > 0 && output[msg_len - 1] == '\n'){
            msg_len--;
        }
        printf("[OUTPUT] %s Sending error message to client: \"%.*s\"\n", tag, msg_len, output);
    }

    printf("\n");
}

// Creates the server's listening socket on the given port
static int create_server_socket(int port){
    // Create a TCP socket (SOCK_STREAM) that uses IPv4 addresses (AF_INET).
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd == -1){
        perror("socket");
        return -1;
    }

    // Allows the port to be reused right away when the server is restarted
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1){
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    // Fill in the address the server listens on
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));            // Start with every field set to 0
    addr.sin_family = AF_INET;                 // IPv4 address
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Accept connections on any of this machine's addresses
    addr.sin_port = htons(port);               // Port number, converted to network byte order

    // Attach the socket to that address and port
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1){
        perror("bind");
        close(server_fd);
        return -1;
    }

    // Start listening for connections. SOMAXCONN (the system maximum) is the size of the queue of
    // clients that have connected but not been accept()ed yet. Phase 2 used 1 because it only
    // served one client; with several clients connecting at the same moment, a queue of 1 could
    // make some of them get refused.
    if (listen(server_fd, SOMAXCONN) == -1){
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
}


// Thread function: serves ONE client from connection until exit/disconnect.
// This is the Phase 2 main loop, moved into its own function so that each client runs it in its
// own thread. When the client leaves, only this thread ends; the server keeps accepting others.
static void *client_thread(void *arg){
    client_info_t *info = (client_info_t *)arg;  // pthread passes the argument as void *
    int client_fd = info->fd;
    const char *tag = info->tag;

    // Main loop: handle one command at a time until the client sends "exit" or disconnects
    while (1){
        char *cmd = NULL;     // Will point to the received command (malloc'd by recv_msg)
        uint32_t cmd_len = 0; // Will hold the command's length in bytes

        // Wait for the next command from the client. Only this thread waits here: the other
        // threads keep serving their own clients at the same time.
        int result = recv_msg(client_fd, &cmd, &cmd_len);

        // 0 means the client closed the connection (CTRL C)
        if (result == 0){
            break; // "Client #N disconnected." is printed after the loop
        }

        // -1 means something went wrong while receiving
        if (result == -1){
            pthread_mutex_lock(&log_mutex);
            fprintf(stderr, "[ERROR] %s Failed to receive command from client.\n", tag);
            pthread_mutex_unlock(&log_mutex);
            break;
        }

        // Reject commands longer than a client is allowed to send (MAX_CMD_LEN - 1 characters).
        // Our client never sends one, but a different program could, and running a huge line
        // would make the parser use a very large buffer. Reply with an error and keep serving.
        if (cmd_len >= MAX_CMD_LEN){
            pthread_mutex_lock(&log_mutex);
            printf("[ERROR] %s Received command of %u bytes, longer than the limit of %d.\n\n", tag, cmd_len, MAX_CMD_LEN - 1);
            pthread_mutex_unlock(&log_mutex);

            const char *msg = "Command too long.\n";
            int too_long_result = send_msg(client_fd, msg, (uint32_t)strlen(msg));
            free(cmd);
            if (too_long_result == -1){
                break;
            }
            continue;
        }

        // Stop serving THIS client if it asked to exit (the server itself keeps running)
        if (is_exit_command(cmd)){
            pthread_mutex_lock(&log_mutex);
            printf("[RECEIVED] %s Received command: \"%s\"\n", tag, cmd);
            printf("[INFO] %s Client requested disconnect. Closing connection.\n", tag);
            pthread_mutex_unlock(&log_mutex);
            free(cmd);
            break;
        }

        pthread_mutex_lock(&log_mutex);
        printf("[RECEIVED] %s Received command: \"%s\"\n", tag, cmd);
        printf("[EXECUTING] %s Executing command: \"%s\"\n", tag, cmd);
        pthread_mutex_unlock(&log_mutex);

        char *output = NULL;   // Will point to the command's output (malloc'd by capture)
        size_t output_len = 0; // Will hold the output's length in bytes

        // Run the command and collect everything it printed. The mutex is NOT held here:
        // a slow command (e.g. "sleep 5") must not stop the other clients' commands or logs.
        // capture_command_line only uses local variables and waits for its own child (by PID),
        // so several threads can run it at the same time safely.
        cmd_status_t status = capture_command_line(cmd, &output, &output_len);

        // Print the [OUTPUT] / [ERROR] log lines for this command as one uninterrupted block
        pthread_mutex_lock(&log_mutex);
        log_result(tag, cmd, status, output, output_len);
        pthread_mutex_unlock(&log_mutex);

        // Send the output back to the client as one message (even if empty)
        int send_result = send_msg(client_fd, output, (uint32_t)output_len);

        free(output); // capture allocated output, so we free it after sending
        free(cmd);    // recv_msg allocated cmd, so we free it after every command

        // If sending failed, the client is gone, so there is no one left to serve
        if (send_result == -1){
            break;
        }
    }

    close(client_fd); // Close the connection to this client only

    pthread_mutex_lock(&log_mutex);
    printf("[INFO] Client #%d disconnected.\n\n", info->id);
    pthread_mutex_unlock(&log_mutex);

    free(info); // main() allocated it for this thread, and nothing else uses it any more
    return NULL;
}


int main(int argc, char *argv[]){
    int port = DEFAULT_PORT; // Use the default port defined unless the user gives one

    // The server accepts at most one argument: ./server [port]
    if (argc > 2){
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return 1;
    }

    // If a port was given, convert it from text to a number and check it is valid
    if (argc == 2){
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535){
            fprintf(stderr, "Invalid port: %s\n", argv[1]);
            return 1;
        }
    }

    // Make stdout unbuffered. capture_command_line() forks, and fork() copies stdout's buffer into
    // the child. With several threads, another thread may be in the middle of printing a log line
    // at that moment; that half-line would be copied into the child and could be flushed into the
    // command's output and sent to a client. With no buffer, there is never anything to copy.
    setvbuf(stdout, NULL, _IONBF, 0);

    // Create the listening socket; stop if anything in the setup failed
    int server_fd = create_server_socket(port);
    if (server_fd == -1){
        return 1;
    }

    printf("[INFO] Server started, waiting for client connections...\n");

    int client_count = 0; // Number of clients accepted so far, used to number them #1, #2, ...

    // Accept clients forever. Each client gets its own thread, and the main thread goes straight
    // back to accept() so that new clients can connect while others are being served.
    // The server runs until it is stopped with CTRL C.
    while (1){
        // accept4 fills client_addr with the client's IP address and port
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr); // In: size of client_addr, out: size actually used

        // Wait until a client connects; accept4 returns a NEW socket that is used only for this client
        int client_fd = accept4(server_fd, (struct sockaddr *)&client_addr, &addr_len, SOCK_CLOEXEC);
        if (client_fd == -1){
            // A failed accept only affects that one connection attempt, so keep serving
            if (errno != EINTR){
                pthread_mutex_lock(&log_mutex);
                perror("[ERROR] accept");
                pthread_mutex_unlock(&log_mutex);
            }
            continue;
        }

        // Bundle everything the thread needs into one struct (pthread_create passes one pointer)
        client_info_t *info = malloc(sizeof(client_info_t));
        if (info == NULL){
            close(client_fd); // Cannot serve this client without memory, so drop the connection
            continue;
        }

        // Only the main thread changes client_count, so it needs no lock
        client_count++;
        info->fd = client_fd;
        info->id = client_count;

        // Convert the client's address to text: binary IP -> "192.168.1.101", port -> host byte order
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        // Build this client's log prefix once, so every log line can reuse it
        snprintf(info->tag, sizeof(info->tag), "[Client #%d - %s:%d]", info->id, client_ip, client_port);

        // Printed before the thread starts, so it always appears before that client's own logs
        pthread_mutex_lock(&log_mutex);
        printf("[INFO] Client #%d connected from %s:%d. Assigned to Thread-%d.\n", info->id, client_ip, client_port, info->id);
        pthread_mutex_unlock(&log_mutex);

        // Start a new thread that runs client_thread(info)
        pthread_t tid;
        int rc = pthread_create(&tid, NULL, client_thread, info);
        if (rc != 0){
            // pthread functions return the error number instead of setting errno
            pthread_mutex_lock(&log_mutex);
            fprintf(stderr, "[ERROR] Could not create a thread for client #%d: %s\n", info->id, strerror(rc));
            pthread_mutex_unlock(&log_mutex);
            close(client_fd);
            free(info);
            continue;
        }

        // Nobody waits for (joins) client threads, so detach it: its resources are released
        // automatically when it returns. Without this, every finished client would leak a thread.
        pthread_detach(tid);
    }

    // Not reached: the loop above only ends when the server is killed
    close(server_fd);
    return 0;
}
