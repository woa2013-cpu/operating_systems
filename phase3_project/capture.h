#ifndef CAPTURE_H
#define CAPTURE_H

#include <stddef.h>

// Result of running one command line. Only failures of the SHELL count as errors
typedef enum {
    CMD_OK = 0,        // the line ran
    CMD_NOT_FOUND,     // a command in the line could not be found
    CMD_SYS_ERROR      // pipe/fork/malloc/read failed in capture itself
} cmd_status_t;

// Runs one command line in a child process and captures everything it prints to stdout/stderr
cmd_status_t capture_command_line(const char *line, char **out, size_t *out_len);

#endif 
