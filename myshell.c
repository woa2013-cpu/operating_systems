#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "execute.h"
#include "parser.h"

#define MAX_LINE 1024

int main(void) {
    char line[MAX_LINE];
    char *cmds[MAX_CMDS];

    // Main loop to read and execute commands
    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        // Skip empty lines
        int num_cmds = parse_line(line, cmds);
        if (num_cmds <= 0) {
            continue;
        }

        // Handle 'exit' if it's the only command
        if (num_cmds == 1) {
            char temp[MAX_LINE];
            strcpy(temp, cmds[0]);
            char *first_word = strtok(temp, " \t");
            if (first_word != NULL && strcmp(first_word, "exit") == 0) {
                break;
            }
        }

        int num_pipes = num_cmds - 1;
        int pipefds[num_pipes > 0 ? num_pipes : 1][2];

        int pipe_failed = 0;

        // Create all necessary pipes
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(pipefds[i]) < 0) {
                perror("pipe");
                pipe_failed = 1;

                // Close any pipes that were successfully created earlier.
                for (int j = 0; j < i; j++) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }
                break;
            }
        }

        // If pipe creation failed, skip executing this command line
        if (pipe_failed) {
            continue;
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
                // Redirect input from the previous pipe, if needed.
                if (i > 0) {
                    if (dup2(pipefds[i - 1][0], STDIN_FILENO) < 0) {
                        perror("dup2 input pipe");
                        exit(1);
                    }
                }

                // Redirect output to the next pipe, if needed.
                if (i < num_cmds - 1) {
                    if (dup2(pipefds[i][1], STDOUT_FILENO) < 0) {
                        perror("dup2 output pipe");
                        exit(1);
                    }
                }

                // The child no longer needs any of the original pipe descriptors.
                for (int j = 0; j < num_pipes; j++) {
                    close(pipefds[j][0]);
                    close(pipefds[j][1]);
                }

                execute_single_command(cmds[i], num_cmds > 1);
            }

            children_created++;
        }

        // Parent no longer needs any pipe descriptors.
        for (int i = 0; i < num_pipes; i++) {
            close(pipefds[i][0]);
            close(pipefds[i][1]);
        }

        // Wait for all spawned processes in the pipeline to terminate
        for (int i = 0; i < children_created; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }

    return 0;
}
