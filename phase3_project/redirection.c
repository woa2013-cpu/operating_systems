#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "redirection.h"

// Opens the file for one redirection and connects it to stdin, stdout or stderr using dup2()
// Returns 0 on success and -1 on failure (after printing an error message)
static int apply_one_redirection(const Redirection *redir){
    int fd;
    int target_fd; // The standard stream the file replaces (0 = stdin, 1 = stdout, 2 = stderr)

    if (redir->type == REDIR_INPUT){
        // Input redirection (<): the file must already exist
        fd = open(redir->file, O_RDONLY);

        if (fd == -1){
            if (errno == ENOENT){
                fprintf(stderr, "File not found.\n");
            }
            else{
                perror("Failed to open input file");
            }
            return -1;
        }

        target_fd = STDIN_FILENO;
    }

    else{
        int flags = O_WRONLY | O_CREAT; // Open for writing, create the file if it doesn't exist

        // >> and 2>> add to the end of the file; > and 2> erase its old contents first
        if (redir->type == REDIR_APPEND || redir->type == REDIR_ERROR_APPEND){
            flags |= O_APPEND;
        }
        else{
            flags |= O_TRUNC;
        }

        fd = open(redir->file, flags, 0644);

        if (fd == -1){
            if (redir->type == REDIR_OUTPUT || redir->type == REDIR_APPEND){
                perror("Failed to open output file");
            }
            else{
                perror("Failed to open error file");
            }
            return -1;
        }

        // > and >> replace stdout; 2> and 2>> replace stderr
        if (redir->type == REDIR_OUTPUT || redir->type == REDIR_APPEND){
            target_fd = STDOUT_FILENO;
        }
        else{
            target_fd = STDERR_FILENO;
        }
    }

    // Make the standard stream point to the opened file
    if (dup2(fd, target_fd) == -1){
        perror("Failed to redirect");
        close(fd);
        return -1;
    }

    close(fd); // The stream now refers to the file, so the original descriptor is not needed
    return 0;
}


// Applies all redirections of one command from left to right, stopping at the first one that fails.
// Doing them in order is what makes these cases behave like bash:
//   cmd > first.txt > out.txt      -> first.txt is created (empty), output goes to out.txt
//   cmd < missing.txt < input.txt  -> fails on missing.txt, input.txt is never read
//   cmd 2> err.txt < missing.txt   -> stderr already points to err.txt, so the error goes there
//   cmd > first.txt < missing.txt  -> first.txt is created, then the missing file is reported
int apply_redirections(const Redirection redirs[], int count){
    for (int i = 0; i < count; i++){
        if (apply_one_redirection(&redirs[i]) == -1){
            return -1;
        }
    }
    return 0;
}
