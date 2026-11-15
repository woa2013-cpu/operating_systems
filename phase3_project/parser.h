#ifndef PARSER_H
#define PARSER_H

#include "redirection.h"

#define MAX_CMDS 64   // Maximum number of commands in one pipeline ("a | b | c" has 3)
#define MAX_ARGS 64   // Maximum number of arguments for one command, including the NULL at the end


// One command of a pipeline after parsing, e.g. for  grep "alpha beta" < input.txt :
//   args   = { "grep", "alpha beta", NULL }     (quotes removed, ready for execvp)
//   redirs = { {REDIR_INPUT, "input.txt"} }     (kept in the order they were written)
typedef struct {
    char *args[MAX_ARGS];
    int num_args;
    Redirection redirs[MAX_REDIRS];
    int num_redirs;
} Command;


// Parses a whole command line into an array of commands, one per pipeline stage.
// The ENTIRE line is checked before anything runs, so a syntax error anywhere in the line
// (e.g. "echo SIDE > side.txt | cat >") stops the whole line and no command is executed.
//
// line  : the command line, without the trailing newline (not modified)
// cmds  : array of at least MAX_CMDS commands, filled in by this function
// words : buffer of at least strlen(line) + 1 bytes where the argument strings are stored;
//         the pointers in cmds point into it, so it must stay alive while cmds is used
//
// Returns the number of commands (> 0), 0 for an empty line, or -1 for a syntax error
// (the error message has already been printed to stderr).
int parse_line(const char *line, Command cmds[], char *words);

#endif
