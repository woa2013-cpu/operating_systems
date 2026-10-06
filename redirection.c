#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "redirection.h"

// Checks if the given argument is a redirection operator
static int is_redirection_operator(const char *arg){
    return (strcmp(arg, "<") == 0 || strcmp(arg, ">") == 0 ||
            strcmp(arg, ">>") == 0 || strcmp(arg, "2>") == 0 ||
            strcmp(arg, "2>>") == 0);
}

// Removes a redirection operator/filename from the argument array by shifting arguments to the left
static void remove_redirection_args(char *args[], int index){
    int i = index;
    while (args[i + 2] != NULL){
        args[i] = args[i + 2];
        i++;
    }

    args[i] = NULL;
}


// Clears the redirection structure so that a new command does not reuse information
void init_redirection(Redirection *redir){
    redir->input_file = NULL;
    redir->output_file = NULL;
    redir->error_file = NULL;

    redir->output_append = 0;
    redir->error_append = 0;
}


// Finds redirection operators in an already tokenized argument array / stores their filenames
// The redirection operators and filenames are removed from args
int extract_redirection(char *args[], Redirection *redir){
    int i = 0;

    while (args[i] != NULL){
        // Input redirection (<)
        if (strcmp(args[i], "<") == 0){
            if (args[i + 1] == NULL || is_redirection_operator(args[i + 1])){
                fprintf(stderr, "Input file not specified.\n");
                return -1;
            }

            redir->input_file = args[i + 1];
            remove_redirection_args(args, i);
        }


        // Output redirection (>)
        else if (strcmp(args[i], ">") == 0){
            if (args[i + 1] == NULL || is_redirection_operator(args[i + 1])){
                fprintf(stderr, "Output file not specified.\n");
                return -1;
            }

            redir->output_file = args[i + 1];
            redir->output_append = 0;
            remove_redirection_args(args, i);
        }

        // Output redirection with append (>>)
        else if (strcmp(args[i], ">>") == 0){
            if (args[i + 1] == NULL || is_redirection_operator(args[i + 1])){
                fprintf(stderr, "Output file not specified.\n");
                return -1;
            }

            redir->output_file = args[i + 1];
            redir->output_append = 1;
            remove_redirection_args(args, i);
        }

        // Error redirection (2>)
        else if (strcmp(args[i], "2>") == 0){
            if (args[i + 1] == NULL || is_redirection_operator(args[i + 1])){
                fprintf(stderr, "Error output file not specified.\n");
                return -1;
            }

            redir->error_file = args[i + 1];
            redir->error_append = 0;
            remove_redirection_args(args, i);
        }

        // Error redirection with append (2>>)
        else if (strcmp(args[i], "2>>") == 0){
            if (args[i + 1] == NULL || is_redirection_operator(args[i + 1])){
                fprintf(stderr, "Error output file not specified.\n");
                return -1;
            }

            redir->error_file = args[i + 1];
            redir->error_append = 1;
            remove_redirection_args(args, i);
        }

        // Normal command argument to move to the next token
        else{
            i++;
        }
    }

    return 0;
}


// Opens the redirection files and replaces stdin, stdout or stderr with files using dup2()
// This function executes in the child process before exec() 
int apply_redirection(const Redirection *redir){
    int fd;
    int flags;

    // Processing input redirection if < was specified
    if (redir->input_file != NULL){
        fd = open(redir->input_file, O_RDONLY);

          if (fd == -1){
            if (errno == ENOENT){
                fprintf(stderr, "File not found.\n");
            }
            else{
                perror("Failed to open input file");
            }

            return -1;
        }

        if (dup2(fd, STDIN_FILENO) == -1){
            perror("Failed to redirect input");
            close(fd);
            return -1;
        }

        close(fd);
    }

    // Processing output redirection if > or >> was specified
    if (redir->output_file != NULL){
        flags = O_WRONLY | O_CREAT; // Open for writing, create if it doesn't exist

        if (redir->output_append){
            flags |= O_APPEND; // Append to the end of the file (>>)
        }

        else{
            flags |= O_TRUNC; // Overwrite existing file contents (>)
        }

        fd = open(redir->output_file, flags, 0644);

        if (fd == -1){
            perror("Failed to open output file");
            return -1;
        }

        // Replacing standard output (FD 1) with the opened file
        if (dup2(fd, STDOUT_FILENO) == -1){
            perror("Failed to redirect output");
            close(fd);
            return -1;
        }

        close(fd);
    }

    // Processing error redirection if 2> or 2>> was specified
    if (redir->error_file != NULL){
        flags = O_WRONLY | O_CREAT;

        if (redir->error_append){
            flags |= O_APPEND;
        }

        else{
            flags |= O_TRUNC;
        }

        fd = open(redir->error_file, flags, 0644);

        if (fd == -1){
            perror("Failed to open error file");
            return -1;
        }

        // Replacing standard error (FD 2) with the opened file
        if (dup2(fd, STDERR_FILENO) == -1){
            perror("Failed to redirect error output");
            close(fd);
            return -1;
        }

        close(fd);
    }

    return 0;
}