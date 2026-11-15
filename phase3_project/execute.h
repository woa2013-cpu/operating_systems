#ifndef EXECUTE_H
#define EXECUTE_H

#include "parser.h"

// Runs one already-parsed command in the current (child) process: applies its redirections
// in order, then replaces the process with the program using execvp().
// Never returns: it either becomes the program or exits with an error code
// (127 = command not found, 126 = command could not be run, 1 = redirection failed).
void execute_single_command(Command *cmd, int in_pipeline);

#endif
