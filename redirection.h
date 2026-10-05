#ifndef REDIRECTION_H
#define REDIRECTION_H


// Stores output and error redirection information associated with one command
typedef struct{
    char *input_file;
    char *output_file;
    char *error_file;

    int output_append;
    int error_append;
} Redirection;


// Resets all redirection information before processing a new command
void init_redirection(Redirection *redir);


// Finds redirection operators in the argument array
// Stores the filenames and removes the redirection from the arguments
int extract_redirection(char *args[], Redirection *redir);


// Applies extracted output/error redirections to current process using open() and dup2()
int apply_redirection(const Redirection *redir);

#endif