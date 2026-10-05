#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_LINE 1024
#define MAX_CMDS 64

int parse_line(char *line, char *cmds[]);
void execute_single_command(char *cmd_str, int in_fd, int out_fd);

int main(void) {
    char line[MAX_LINE];
    char *cmds[MAX_CMDS];

    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0';

        int num_cmds = parse_line(line, cmds);
        if (num_cmds == 0) {
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

        // Create all necessary pipes
        for (int i = 0; i < num_pipes; i++) {
            if (pipe(pipefds[i]) < 0) {
                perror("pipe");
                break;
            }
        }

        pid_t pids[MAX_CMDS];

        // Fork and run each command in the pipeline
        for (int i = 0; i < num_cmds; i++) {
            pids[i] = fork();

            if (pids[i] < 0) {
                perror("fork");
                break;
            }

            if (pids[i] == 0) {
                // Child: determine input and output descriptors
                int in_fd = (i == 0) ? -1 : pipefds[i - 1][0];
                int out_fd = (i == num_cmds - 1) ? -1 : pipefds[i][1];

                // Close all pipe descriptors in child after setting redirection targets
                // (execute_single_command will dup2 what it needs)
                execute_single_command(cmds[i], in_fd, out_fd);
            }

            // In parent: close the write end of the pipe that was just written to
            // and the read end that is no longer needed
            if (i > 0) {
                close(pipefds[i - 1][0]);
            }
            if (i < num_cmds - 1) {
                close(pipefds[i][1]);
            }
        }

        // Wait for all spawned processes in the pipeline to terminate
        for (int i = 0; i < num_cmds; i++) {
            waitpid(pids[i], NULL, 0);
        }
    }

    return 0;
}