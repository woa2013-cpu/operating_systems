#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "execute.h"

// Executes a single command in the child process.
// The parser has already split the arguments (with quotes removed) and collected the
// redirections, so all that is left is to apply the redirections and run the program.
void execute_single_command(Command *cmd, int in_pipeline) {
    // Open the redirection files in the order they were written and connect them using dup2()
    if (apply_redirections(cmd->redirs, cmd->num_redirs) == -1) {
        exit(1); // Terminate child process if a file could not be opened (message already printed)
    }

    // A command made only of redirections (e.g. "> empty.txt") just creates the files, like bash
    if (cmd->num_args == 0) {
        exit(0);
    }

    // Execute the command. args is NULL-terminated, as execvp() requires
    execvp(cmd->args[0], cmd->args);

    // execvp() only returns if execution failed.
    if (errno == ENOENT){
        if (in_pipeline){
            fprintf(stderr, "Command not found in pipe sequence.\n");
        }

        else{
            fprintf(stderr, "Command not found: %s\n", cmd->args[0]);
        }

        // 127 is the standard "command not found" exit code (same as bash).
        // run_command_line() looks for it so the server can log [ERROR]
        exit(127);
    }

    else{
        perror(cmd->args[0]);
    }

    exit(126); // The command exists but could not be run (e.g. permission denied)
}
