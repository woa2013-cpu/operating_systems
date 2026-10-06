#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "redirection.h"

#define MAX_ARGS 64

// Executes a single command in the child process.
void execute_single_command(char *cmd_str, int in_pipeline) {
    char *args[MAX_ARGS];
    int i = 0;

    // Tokenize command string into arguments
    char *token = strtok(cmd_str, " \t\r\n"); // Stop argument when found a space or tab or carriage or newline
    while (token != NULL && i < MAX_ARGS - 1) {
        args[i++] = token; // Add the argument into the array
        token = strtok(NULL, " \t\r\n"); // Continue looking for the next argument
    }
    args[i] = NULL; // Set last argument as NULL

    if (args[0] == NULL) {
        exit(0);
    }

    Redirection redir;
    init_redirection(&redir);

    // Scans arguments for redirection operators, stores filenames, and removes them from the array
    if (extract_redirection(args, &redir) == -1){
        exit(1); // Terminate child process if redirection syntax is invalid
    }

    for (int j = 0; args[j] != NULL; j++) {
        int len = strlen(args[j]);
        if (len >= 2 && ((args[j][0] == '"' && args[j][len - 1] == '"') ||
                         (args[j][0] == '\'' && args[j][len - 1] == '\''))) {
            args[j][len - 1] = '\0'; // Replace trailing quote with null terminator
            args[j]++;               // Advance pointer past leading quote
        }
    }

    // Opens the extracted files and maps them to standard streams using dup2()
    if (apply_redirection(&redir) == -1) {
        exit(1); // Terminate child process if file opening or redirection fails
    }

    // Execute the command.
    execvp(args[0], args);

    // execvp() only returns if execution failed.
    if (errno == ENOENT){
        if (in_pipeline){
            fprintf(stderr, "Command not found in pipe sequence.\n");
        }

        else{
            fprintf(stderr, "Command not found.\n");
        }
    }

    else{
        perror(args[0]);
    }

    exit(126);
}
