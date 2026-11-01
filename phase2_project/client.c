#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "protocol.h"

// Checks whether the line is the exit command, using the same rule as
// myshell.c and server.c: the first word of the line is "exit".
// Works on a copy because strtok() modifies the string it is given.
static int is_exit_command(const char *line) {
    char temp[MAX_CMD_LEN];
    strncpy(temp, line, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *first_word = strtok(temp, " \t");
    return first_word != NULL && strcmp(first_word, EXIT_COMMAND) == 0;
}

// Returns 1 if the line contains only spaces/tabs (or nothing at all)
static int is_blank(const char *line) {
    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] != ' ' && line[i] != '\t') {
            return 0;
        }
    }
    return 1;
}

// Creates a TCP socket and connects it to the server at server_ip:port.
// Returns the connected socket descriptor, or -1 if anything failed.
static int connect_to_server(const char *server_ip, int port) {
    // Create a TCP socket (AF_INET = IPv4, SOCK_STREAM = TCP)
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Fill in the server's address (IPv4, port, IP)
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); // Convert port to network byte order

    // Convert the IP address from text (e.g. "127.0.0.1") to binary form
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address.\n");
        close(sockfd);
        return -1;
    }

    // Connect to the server (fails if no server is listening on that port)
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int main(int argc, char *argv[]) {
    const char *server_ip = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    // Usage: ./client [server_ip] [port]
    if (argc > 1) {
        server_ip = argv[1];
    }
    if (argc > 2) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number.\n");
            return 1;
        }
    }

    // Task 1: Client initialization (connect to the server)
    int sockfd = connect_to_server(server_ip, port);
    if (sockfd < 0) {
        return 1;
    }

    char line[MAX_CMD_LEN];

    // Task 7: Loop until the user types exit or the server goes away
    while (1) {
        // Task 2: Show the prompt and read a command, same as the Phase 1 shell
        printf("$ ");
        fflush(stdout); // Forces the prompt to print to the terminal immediately

        // Ctrl+D (end of input): tell the server we are leaving, then quit
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            send_msg(sockfd, EXIT_COMMAND, strlen(EXIT_COMMAND));
            break;
        }

        // If the line did not fit in the buffer, discard the rest of it so it
        // is not read as the next command
        if (strchr(line, '\n') == NULL && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            fprintf(stderr, "Command too long (max %d characters).\n", MAX_CMD_LEN - 1);
            continue;
        }

        line[strcspn(line, "\n")] = '\0'; // Removes the trailing newline character

        // Skip empty lines locally, there is nothing for the server to run
        if (is_blank(line)) {
            continue;
        }

        // Task 3: Send the command to the server
        if (send_msg(sockfd, line, strlen(line)) == -1) {
            // EPIPE / ECONNRESET mean the server is gone (crashed or was stopped)
            if (errno == EPIPE || errno == ECONNRESET) {
                fprintf(stderr, "Server closed the connection.\n");
            }
            else {
                perror("send");
            }
            break;
        }

        // On exit, the server shuts down by itself, so there is no reply to wait for
        if (is_exit_command(line)) {
            break;
        }

        // Task 6: Receive the command's output and display it
        char *output = NULL;
        uint32_t output_len = 0;
        int status = recv_msg(sockfd, &output, &output_len);

        if (status == 0) {
            fprintf(stderr, "Server closed the connection.\n");
            break;
        }
        if (status < 0) {
            perror("recv");
            break;
        }

        // fwrite instead of printf: prints exactly output_len bytes, even if the
        // output contains '\0' bytes or '%' characters
        fwrite(output, 1, output_len, stdout);
        fflush(stdout);
        free(output);
    }

    close(sockfd);
    return 0;
}
