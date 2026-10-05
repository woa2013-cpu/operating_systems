#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 64

// Executes a single command in the child process.
// in_fd: file descriptor to read from (or -1 for standard input)
// out_fd: file descriptor to write to (or -1 for standard output)
void execute_single_command(char *cmd_str, int in_fd, int out_fd) {
    char *args[MAX_ARGS];
    int i = 0;

    // Tokenize command string into arguments
    char *token = strtok(cmd_str, " \t\r\n");
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token;
        token = strtok(NULL, " \t\r\n");
    }
    args[i] = NULL;

    if (args[0] == NULL) {
        exit(0);
    }

    for (int j = 0; args[j] != NULL; j++) {
        int len = strlen(args[j]);
        if (len >= 2 && ((args[j][0] == '"' && args[j][len - 1] == '"') ||
                         (args[j][0] == '\'' && args[j][len - 1] == '\''))) {
            args[j][len - 1] = '\0'; // Replace trailing quote with null terminator
            args[j]++;               // Advance pointer past leading quote
        }
    }

    // Redirect standard input if an input descriptor is provided
    if (in_fd != -1) {
        if (dup2(in_fd, STDIN_FILENO) < 0) {
            perror("dup2 in");
            exit(1);
        }
    }

    // Redirect standard output if an output descriptor is provided
    if (out_fd != -1) {
        if (dup2(out_fd, STDOUT_FILENO) < 0) {
            perror("dup2 out");
            exit(1);
        }
    }

    // Execute the command
    execvp(args[0], args);
    perror("Command not found");
    exit(1);
}