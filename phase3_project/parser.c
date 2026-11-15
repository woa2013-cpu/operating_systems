#include <stdio.h>
#include <string.h>
#include "parser.h"

// The kinds of tokens a command line is made of
typedef enum {
    TOKEN_WORD,   // a command name, an argument or a filename
    TOKEN_PIPE,   // |
    TOKEN_REDIR,  // <, >, >>, 2> or 2>>
    TOKEN_END     // end of the line
} TokenType;

// Returns 1 if c ends a word when it is not inside quotes: a space, a tab, a pipe or a redirection
static int is_word_end(char c){
    return c == '\0' || c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>';
}

// The error message for a redirection operator that has no filename after it
static const char *missing_file_message(RedirType type){
    if (type == REDIR_INPUT){
        return "Input file not specified.";
    }
    if (type == REDIR_OUTPUT || type == REDIR_APPEND){
        return "Output file not specified.";
    }
    return "Error output file not specified.";
}

// Reads the next token starting at line[*pos] and moves *pos past it.
//   - Operators are recognised even without spaces around them ("echo NEW>out.txt", "ls 2>err.txt").
//   - "2>" and "2>>" only count as error redirection when the 2 starts a new token, like in bash
//     ("ls 2> f" redirects stderr, but in "echo a2>f" the word is "a2" and > redirects stdout).
//   - Inside single or double quotes every character is part of the word, including spaces and
//     operators, so  grep "alpha beta"  gives the single argument  alpha beta  and
//     echo "a | b"  prints  a | b . The quote characters themselves are removed.
// For a TOKEN_WORD, the word (without quotes) is copied to *out followed by '\0', and *out is moved
// past it. For a TOKEN_REDIR, *redir_type is set.
// Returns the token type, or -1 if a quote is never closed.
static int next_token(const char *line, int *pos, char **out, RedirType *redir_type){
    int i = *pos;

    // Skip spaces and tabs between tokens
    while (line[i] == ' ' || line[i] == '\t'){
        i++;
    }

    if (line[i] == '\0'){
        *pos = i;
        return TOKEN_END;
    }

    if (line[i] == '|'){
        *pos = i + 1;
        return TOKEN_PIPE;
    }

    if (line[i] == '<'){
        *redir_type = REDIR_INPUT;
        *pos = i + 1;
        return TOKEN_REDIR;
    }

    if (line[i] == '>'){
        if (line[i + 1] == '>'){
            *redir_type = REDIR_APPEND;
            *pos = i + 2;
        }
        else{
            *redir_type = REDIR_OUTPUT;
            *pos = i + 1;
        }
        return TOKEN_REDIR;
    }

    if (line[i] == '2' && line[i + 1] == '>'){
        if (line[i + 2] == '>'){
            *redir_type = REDIR_ERROR_APPEND;
            *pos = i + 3;
        }
        else{
            *redir_type = REDIR_ERROR;
            *pos = i + 2;
        }
        return TOKEN_REDIR;
    }

    // Otherwise this is a word: copy characters until an unquoted space or operator
    char *w = *out;
    while (!is_word_end(line[i])){
        if (line[i] == '"' || line[i] == '\''){
            char quote = line[i]; // Remember which quote opened, only the same one closes it
            i++;

            // Copy everything up to the closing quote, spaces and operators included
            while (line[i] != quote){
                if (line[i] == '\0'){
                    fprintf(stderr, "Unclosed quote.\n");
                    return -1;
                }
                *w++ = line[i++];
            }
            i++; // Skip the closing quote
        }
        else{
            *w++ = line[i++];
        }
    }

    *w++ = '\0';
    *out = w; // The next word will be stored right after this one
    *pos = i;
    return TOKEN_WORD;
}

// Parses the whole line into commands. Nothing is executed here: the complete line is checked
// first, so if any part of it is invalid, no command of the line runs.
int parse_line(const char *line, Command cmds[], char *words){
    int pos = 0;           // Current position in line
    char *out = words;     // Where the next word will be stored
    int count = 0;         // Index of the command currently being filled
    int pending = 0;       // 1 if the last token was a redirection still waiting for its filename
    RedirType pending_type = REDIR_INPUT;

    cmds[0].num_args = 0;
    cmds[0].num_redirs = 0;

    while (1){
        RedirType redir_type;
        char *word = out; // If the next token is a word, it will be stored here
        int token = next_token(line, &pos, &out, &redir_type);

        if (token == -1){
            return -1; // Unclosed quote (message already printed)
        }

        Command *cmd = &cmds[count];

        if (token == TOKEN_WORD){
            if (pending){
                // This word is the filename of the previous redirection operator
                cmd->redirs[cmd->num_redirs].type = pending_type;
                cmd->redirs[cmd->num_redirs].file = word;
                cmd->num_redirs++;
                pending = 0;
            }
            else{
                if (cmd->num_args >= MAX_ARGS - 1){ // Leave room for the NULL terminator
                    fprintf(stderr, "Too many arguments.\n");
                    return -1;
                }
                cmd->args[cmd->num_args++] = word;
            }
        }

        else if (token == TOKEN_REDIR){
            // Two operators in a row (e.g. "ls > < f"): the first one has no filename
            if (pending){
                fprintf(stderr, "%s\n", missing_file_message(pending_type));
                return -1;
            }
            if (cmd->num_redirs >= MAX_REDIRS){
                fprintf(stderr, "Too many redirections.\n");
                return -1;
            }
            pending = 1;
            pending_type = redir_type;
        }

        else if (token == TOKEN_PIPE){
            // "ls > | wc": the redirection before the pipe has no filename
            if (pending){
                fprintf(stderr, "%s\n", missing_file_message(pending_type));
                return -1;
            }
            // Nothing before this pipe
            if (cmd->num_args == 0 && cmd->num_redirs == 0){
                if (count == 0){
                    fprintf(stderr, "Command missing before pipe.\n");
                }
                else{
                    fprintf(stderr, "Empty command between pipes.\n");
                }
                return -1;
            }
            if (count >= MAX_CMDS - 1){
                fprintf(stderr, "Too many commands in pipeline.\n");
                return -1;
            }

            // Finish this command and start the next one
            cmd->args[cmd->num_args] = NULL;
            count++;
            cmds[count].num_args = 0;
            cmds[count].num_redirs = 0;
        }

        else{ // TOKEN_END
            // "ls >" at the end of the line: the last redirection has no filename
            if (pending){
                fprintf(stderr, "%s\n", missing_file_message(pending_type));
                return -1;
            }
            if (cmd->num_args == 0 && cmd->num_redirs == 0){
                if (count == 0){
                    return 0; // Empty line: nothing to run
                }
                fprintf(stderr, "Command missing after pipe.\n");
                return -1;
            }

            cmd->args[cmd->num_args] = NULL; // execvp() needs a NULL-terminated array
            return count + 1;
        }
    }
}
