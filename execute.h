#ifndef EXECUTE_H
#define EXECUTE_H

#define MAX_ARGS 64


// void execute_single_command(char *cmd_str, int in_fd, int out_fd, int in_pipeline);
void execute_single_command(char *cmd_str, int in_pipeline);

#endif