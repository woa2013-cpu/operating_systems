#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "capture.h"
#include "pipeline.h"   
#include "protocol.h"   // MAX_MSG_SIZE

// Builds the short error text returned for CMD_SYS_ERROR
static cmd_status_t sys_error(const char *what, char **out, size_t *out_len){
    // strerror_r instead of strerror: several server threads can be in here at once, and
    // strerror() may return a pointer to one shared buffer that another thread overwrites.
    // strerror_r writes the text into our own local buffer instead.
    char reason[128] = "unknown error";
    strerror_r(errno, reason, sizeof(reason));

    char msg[256];
    snprintf(msg, sizeof(msg), "Server error: %s failed: %s\n", what, reason);

    *out = strdup(msg);                            // Heap copy, so the caller can free() it
    *out_len = (*out != NULL) ? strlen(*out) : 0;  // If strdup failed, send an empty reply
    return CMD_SYS_ERROR;
}

// Reads everything from fd until EOF and stores it in a growing heap buffer up to MAX_MSG_SIZE
// Returns 0 on success, -1 if read() or realloc() failed.
static int read_all(int fd, char **out, size_t *out_len){
    char *buf = NULL;  // Holds all the output collected so far (grows when needed)
    size_t len = 0;    // How many bytes are stored in buf
    size_t cap = 0;    // How many bytes buf can hold before it has to grow
    char chunk[4096];  // Temporary space for one read() call

    while (1){
        ssize_t n = read(fd, chunk, sizeof(chunk));

        // 0 means EOF
        if (n == 0){
            break;
        }

        if (n == -1){
            if (errno == EINTR){
                continue; // Interrupted by a signal before reading anything, tries again
            }
            free(buf);
            return -1;
        }

        // Keep only the bytes that still fit under MAX_MSG_SIZE; drop the rest
        size_t keep = (size_t)n;
        if (len + keep > MAX_MSG_SIZE){
            keep = MAX_MSG_SIZE - len;
        }
        if (keep == 0){
            continue; // Limit reached: keep reading (to drain the pipe) but store nothing
        }

        // Make buf bigger if the new bytes don't fit
        if (len + keep > cap){
            size_t new_cap = (cap == 0) ? sizeof(chunk) : cap * 2;
            char *bigger = realloc(buf, new_cap);
            if (bigger == NULL){
                free(buf);
                return -1;
            }
            buf = bigger;
            cap = new_cap;
        }

        memcpy(buf + len, chunk, keep); // Add the new bytes after the ones we already have
        len += keep;
    }

    *out = buf;      // NULL if the command printed nothing
    *out_len = len;
    return 0;
}

// Runs one command line in a child process and captures everything it prints
cmd_status_t capture_command_line(const char *line, char **out, size_t *out_len){
    *out = NULL;
    *out_len = 0;

    // fds[0] is where server reads output; fds[1] is where the command writes its output
    int fds[2];
    if (pipe(fds) == -1){
        return sys_error("pipe", out, out_len);
    }

    // Print any server log text still waiting in stdout/stderr before forking
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid == -1){
        cmd_status_t result = sys_error("fork", out, out_len); // Build the message before close() can change errno
        close(fds[0]);
        close(fds[1]);
        return result;
    }

    // Child process
    if (pid == 0){
        close(fds[0]); // The child only writes, it never reads its own output

        // Send stdout AND stderr into the pipe
        if (dup2(fds[1], STDOUT_FILENO) == -1 || dup2(fds[1], STDERR_FILENO) == -1){
            _exit(1);
        }
        close(fds[1]);

        // Commands must not read from the server's keyboard
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1){
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        // The Phase 1 parser changes the string it parses (strtok), so give it a copy
        char *copy = strdup(line);
        if (copy == NULL){
            _exit(1);
        }

        // the exit code: 127 = a command was not found, 0 = anything else.
        // _exit (not exit) so the child doesn't run any cleanup that belongs to the server
        if (run_command_line(copy) == 1){
            _exit(127);
        }
        _exit(0);
    }


    // Parent process
    close(fds[1]); // The server only reads, this is closed otherwise read() never sees EOF

    // Read the output WHILE the child runs so the pipe doesn't fill up and block the child
    int read_result = read_all(fds[0], out, out_len);
    close(fds[0]);

    // Always wait for the child, so no zombie process is left behind
    int status;
    waitpid(pid, &status, 0);

    if (read_result == -1){
        return sys_error("read", out, out_len);
    }

    // Turn the child's exit code back into a cmd_status_t for the server
    if (WIFEXITED(status) && WEXITSTATUS(status) == 127){
        return CMD_NOT_FOUND;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0){
        return CMD_OK;
    }

    // Anything else means the capture child itself failed before running the line
    free(*out);
    *out = strdup("Server error: the command could not be run.\n");
    *out_len = (*out != NULL) ? strlen(*out) : 0;
    return CMD_SYS_ERROR;
}

