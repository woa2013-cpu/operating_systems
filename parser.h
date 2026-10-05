#ifndef PARSER_H
#define PARSER_H

#define MAX_CMDS 64

// Parses a command line into individual commands separated by pipes
// Returns the number of commands parsed, or -1 if there's a syntax error with pipes
int parse_line(char *line, char *cmds[]);

#endif