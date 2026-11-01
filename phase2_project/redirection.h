#ifndef REDIRECTION_H
#define REDIRECTION_H

// Maximum number of redirections allowed on one command (e.g. "cmd < a > b 2> c" uses 3)
#define MAX_REDIRS 16


// The five redirection operators supported by the shell
typedef enum {
    REDIR_INPUT,         // <    read stdin from a file
    REDIR_OUTPUT,        // >    write stdout to a file (overwrite)
    REDIR_APPEND,        // >>   write stdout to a file (append)
    REDIR_ERROR,         // 2>   write stderr to a file (overwrite)
    REDIR_ERROR_APPEND   // 2>>  write stderr to a file (append)
} RedirType;


// One redirection exactly as it was written in the command: the operator and its filename
typedef struct {
    RedirType type;
    char *file;
} Redirection;


// Applies the redirections of one command IN THE ORDER THEY WERE WRITTEN, the same way bash does:
// each file is opened and connected (dup2) before the next one is looked at, and the first failure stops
// everything. So "cmd > a.txt > b.txt" creates a.txt (empty) and writes to b.txt, and in
// "cmd 2> err.txt < missing.txt" the "File not found." message lands in err.txt.
// Runs in the child process before exec(). Returns 0 on success, -1 on failure (message already printed).
int apply_redirections(const Redirection redirs[], int count);

#endif
