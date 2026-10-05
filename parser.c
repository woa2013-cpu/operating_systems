#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

#define MAX_CMDS 64

int parse_line(char *line, char *cmds[]) {
    int count = 0;
    char *token = strtok(line, "|");

    while (token != NULL && count < MAX_CMDS - 1) {
        cmds[count++] = token;
        token = strtok(NULL, "|");
    }
    cmds[count] = NULL;
    return count; 
}