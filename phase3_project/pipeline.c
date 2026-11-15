#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include "pipeline.h"
#include "parser.h"
#include "execute.h"

// Executes commands by creating pipes, forking processes and waiting for them to finish
int run_command_line(char *line) {
    Command cmds[MAX_CMDS];
    char words[strlen(line) + 1]; // Storage for the parsed arguments (never longer than the line itself)

    // Parse and check the whole line (quotes, pipes, redirections) before running anything
    int num_cmds = parse_line(line, cmds, words);
    if (num_cmds == 0) {
        return 0; // Empty line: nothing to run
    }
    if (num_cmds < 0) {
        return -1; // Invalid pipe syntax (the parser already printed the error)
    }

    int num_pipes = num_cmds - 1;
    int pipefds[num_pipes > 0 ? num_pipes : 1][2];

    // Create all necessary pipes
    for (int i = 0; i < num_pipes; i++) {
        if (pipe(pipefds[i]) < 0) { // Create pipe and check if failed
            perror("pipe");

            // Close any pipes that were successfully created earlier
            for (int j = 0; j < i; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }
            return -1; // Skip executing this command line
        }
    }

    pid_t pids[MAX_CMDS];
    int children_created = 0;

    // Fork and run each command in the pipeline
    for (int i = 0; i < num_cmds; i++) {
        pids[i] = fork();

        if (pids[i] < 0) {
            perror("fork");
            break;
        }

        if (pids[i] == 0) {
            // Redirect input from the previous pipe, if needed
            if (i > 0) {
                if (dup2(pipefds[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2 input pipe");
                    exit(1);
                }
            }

            // Redirect output to the next pipe
            if (i < num_cmds - 1) {
                if (dup2(pipefds[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2 output pipe");
                    exit(1);
                }
            }

            // The child no longer needs any of the original pipe descriptors
            for (int j = 0; j < num_pipes; j++) {
                close(pipefds[j][0]);
                close(pipefds[j][1]);
            }

            execute_single_command(&cmds[i], num_cmds > 1);
        }

        children_created++;
    }

    // Parent no longer needs any pipe descriptors
    for (int i = 0; i < num_pipes; i++) {
        close(pipefds[i][0]);
        close(pipefds[i][1]);
    }

    int not_found = 0; // Becomes 1 if any command in the line could not be found

    // Wait for all spawned processes in the pipeline to terminate
    for (int i = 0; i < children_created; i++) {
        int status;
        waitpid(pids[i], &status, 0);

        // execute_single_command exits with 127 when execvp could not find the command
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127) {
            not_found = 1;
        }
    }

    if (not_found) {
        return 1;
    }
    return 0;
}