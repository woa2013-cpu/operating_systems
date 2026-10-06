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
            if (!command_found) {// If we hit a pipe but havent seen any command yet
                if (!pipe_found) { // If this is the first pipe we've seen, then no inital command
                    fprintf(stderr, "Command missing before pipe.\n");
                }
                
                else { // If we've already seen a pipe, that means there are two pipes back to back
                    fprintf(stderr, "Empty command between pipes.\n");
                }
                return -1;
        }
        
        // Reset for next pipeline stage and mark a pipe was found
        pipe_found = 1;
        command_found = 0;
    }
        // If the character is not a space (and not a pipe), we found a valid command string
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
    if (validate_pipes(line) == -1) {  // Validate pipe syntax before modifying the string
        return -1;
    }

    char *token = strtok(line, "|"); // Extract the first command separated by a pipe

    while (token != NULL && count < MAX_CMDS - 1) { // Loop to extract remaining commands and store them in the array
        cmds[count++] = token;
        token = strtok(NULL, "|"); // Continue fetching tokens
    }
    cmds[count] = NULL; // NULL-terminate the array for future processing
    return count; 
}
