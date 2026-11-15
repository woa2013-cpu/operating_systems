#include <stdio.h>
#include <string.h>
#include "pipeline.h"

#define MAX_LINE 1024

int main(void) {
    char line[MAX_LINE];

    // Main loop to read and execute commands
    while (1) {
        printf("$ ");
        fflush(stdout); // Forces the prompt to print to the terminal immediately

        // Take input
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }

        line[strcspn(line, "\n")] = '\0'; // Removes the trailing newline character

        // Handle 'exit': check the first word on a copy, because strtok changes the string
        char temp[MAX_LINE];
        strcpy(temp, line);
        char *first_word = strtok(temp, " \t");
        if (first_word != NULL && strcmp(first_word, "exit") == 0) {
            break;
        }

        // Parse and run the whole line, which can contain multiple commands
        run_command_line(line);
    }

    return 0;
}