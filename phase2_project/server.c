#define _GNU_SOURCE // Needed for accept4(), which is a Linux extension

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include "protocol.h"
#include "capture.h"


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
static void log_result(const char *cmd, cmd_status_t status, const char *output, size_t output_len){
    if (status == CMD_OK){
        printf("[OUTPUT] Sending output to client:\n");

        // fwrite prints exactly output_len bytes (NULL when there is nothing to print)
        if (output_len > 0){
            fwrite(output, 1, output_len, stdout);
        }
    }

    else{
        // Specifying error ttype
        if (status == CMD_NOT_FOUND){
            printf("[ERROR] Command not found: \"%s\"\n", cmd);
        }
        else{
            printf("[ERROR] Server failed to execute: \"%s\"\n", cmd);
        }

        // %.*s prints exactly msg_len characters
        int msg_len = (int)output_len;
        if (msg_len > 0 && output[msg_len - 1] == '\n'){
            msg_len--;
        }
        printf("[OUTPUT] Sending error message to client: \"%.*s\"\n", msg_len, output);
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

    // Start listening for connections with 1 as the size of the waiting queue
    if (listen(server_fd, 1) == -1){
        perror("listen");
        close(server_fd);
        return -1;
    }

    return server_fd;
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
    
    // Create the listening socket; stop if anything in the setup failed
    int server_fd = create_server_socket(port);
    if (server_fd == -1){
        return 1;
    }

    printf("[INFO] Server started, waiting for client connections...\n");

    // Wait until a client connects accept4 returns a NEW socket that is used only for this client
    int client_fd = accept4(server_fd, NULL, NULL, SOCK_CLOEXEC);
    if (client_fd == -1){
        perror("accept");
        close(server_fd);
        return 1;
    }

    // One client maximum so the listening socket is not needed
    close(server_fd);

    printf("[INFO] Client connected.\n");

    // Main loop: handle one command at a time until the client sends "exit" or disconnects
    while (1){
        char *cmd = NULL;     // Will point to the received command (malloc'd by recv_msg)
        uint32_t cmd_len = 0; // Will hold the command's length in bytes

        // Wait for the next command from the client
        int result = recv_msg(client_fd, &cmd, &cmd_len);

        // 0 means the client closed the connection (CTRL C)
        if (result == 0){
            printf("[INFO] Client disconnected.\n");
            break;
        }

        // -1 means something went wrong while receiving
        if (result == -1){
            fprintf(stderr, "[ERROR] Failed to receive command from client.\n");
            break;
        }

        // Reject commands longer than a client is allowed to send (MAX_CMD_LEN - 1 characters).
        // Our client never sends one, but a different program could, and running a huge line
        // would make the parser use a very large buffer. Reply with an error and keep serving.
        if (cmd_len >= MAX_CMD_LEN){
            printf("[ERROR] Received command of %u bytes, longer than the limit of %d.\n\n", cmd_len, MAX_CMD_LEN - 1);
            const char *msg = "Command too long.\n";
            int too_long_result = send_msg(client_fd, msg, (uint32_t)strlen(msg));
            free(cmd);
            if (too_long_result == -1){
                perror("[ERROR] Failed to send output to client");
                break;
            }
            continue;
        }

        printf("[RECEIVED] Received command: \"%s\" from client.\n", cmd);

        // Stop the server if the client asked to exit
        if (is_exit_command(cmd)){
            printf("[INFO] Client sent exit. Shutting down server.\n");
            free(cmd);
            break;
        }

        printf("[EXECUTING] Executing command: \"%s\"\n", cmd);

        char *output = NULL;   // Will point to the command's output (malloc'd by capture)
        size_t output_len = 0; // Will hold the output's length in bytes

        // Run the command and collect everything it printed
        cmd_status_t status = capture_command_line(cmd, &output, &output_len);

        // Print the [OUTPUT] / [ERROR] log lines for this command
        log_result(cmd, status, output, output_len);

        // Send the output back to the client as one message (even if empty)
        int send_result = send_msg(client_fd, output, (uint32_t)output_len);

        free(output); // capture allocated output, so we free it after sending
        free(cmd);    // recv_msg allocated cmd, so we free it after every command

        // If sending failed, the client is gone, so there is no one left to serve
        if (send_result == -1){
            perror("[ERROR] Failed to send output to client");
            break;
        }
    }

    close(client_fd); // Close the connection to the client before exiting
    return 0;
}