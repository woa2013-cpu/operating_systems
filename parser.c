#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_CMDS 64

// Checks the command line for missing commands around pipe operators.
// Returns -1 if the pipe syntax is invalid and 0 otherwise.
static int validate_pipes(const char *line) {
    int i = 0;
    int command_found = 0;
    int pipe_found = 0;

    while (line[i] != '\0') {
        if (line[i] == '|') {
            if (!command_found) {
                if (!pipe_found) {
                    fprintf(stderr, "Command missing before pipe.\n");
                }
                
                else {
                    fprintf(stderr, "Empty command between pipes.\n");
                }
                return -1;
        }
        
        pipe_found = 1;
        command_found = 0;
    }

        else if (!isspace((unsigned char)line[i])) {
            command_found = 1;
        }
        i++;
    }

    // If the last character is a pipe, it's an error
    if (!command_found && strchr(line, '|') != NULL) {
        fprintf(stderr, "Command missing after pipe.\n");
        return -1;
    }
    return 0;
}


int parse_line(char *line, char *cmds[]) {
    int count = 0;
    if (validate_pipes(line) == -1) {
        return -1;
    }

    char *token = strtok(line, "|");

    while (token != NULL && count < MAX_CMDS - 1) {
        cmds[count++] = token;
        token = strtok(NULL, "|");
    }
    cmds[count] = NULL;
    return count; 
}
