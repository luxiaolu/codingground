#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#define COMMAND_LENGTH 1024
#define NUM_TOKENS (COMMAND_LENGTH / 2 + 1)

/* Counter for the number of commands given */
int commandsGiven = 0;

/* Returns the number of digits in an integer.
 */
int digitCount(int c)
{
    int digits = 0;

    while(c != 0) {
        c = c/10;
        digits++;
    }

    return digits;
}

/**
 * Command Input and Processing
 */

/*
 * Tokenize the string in 'buff' into 'tokens'.
 * returns: number of tokens.
 */
int tokenize_command(char *buff, char *tokens[])
{
    int token_count = 0;
    _Bool in_token = false;
    //int num_chars = strnlen(buff, COMMAND_LENGTH);
    int num_chars = strlen(buff);
    for (int i = 0; i < num_chars; i++) {
        switch (buff[i]) {
        // Handle token delimiters (ends):
        case ' ':
        case '\t':
        case '\n':
            buff[i] = '\0';
            in_token = false;
            break;

        // Handle other characters (may be start)
        default:
            if (!in_token) {
                tokens[token_count] = &buff[i];
                token_count++;
                in_token = true;
            }
        }
    }
    tokens[token_count] = NULL;
    return token_count;
}

/**
 * Read a command from the keyboard into the buffer 'buff' and tokenize it
 */
void read_command(char *buff, char *tokens[], _Bool *in_background)
{
    *in_background = false;
    int length;

    length = strlen(buff);

    /*
     * Use calloc to set allocated memory to 0
     * This is implemented in order to avoid bad pointers from accessing
     * unitialized values of buff
     */
    char *input = calloc(length + 1, 1);
    for(int i = 0; i < length; i++) {
        input[i] = buff[i];
    }

    // if not signalling
    if(length > 0) {

        // If the user entered input
        if(input[0] != '\n') {

            commandsGiven ++;
            if ((length < 0) && (errno != EINTR)) {
                perror("Unable to read command from keyboard. Terminating.\n");
                exit(-1);
            }

            // Null terminate and strip \n.
            buff[length] = '\0';
            if (buff[strlen(buff) - 1] == '\n') {
                buff[strlen(buff) - 1] = '\0';
            }

            // Tokenize (saving original command string)
            int token_count = tokenize_command(buff, tokens);
            if (token_count == 0) {
                return;
            }
            
            if ( token_count > 0 ) {
                char* last_token = tokens[token_count-1];
                if(last_token[strlen(last_token) - 1] == '&') {
                    *in_background = true;
                    last_token[strlen(last_token) - 1] = '\0';
                }
            }

            // Extract if running in background:
            else if (token_count > 0 && strcmp(tokens[token_count - 1], "&") == 0) {
                *in_background = true;
                tokens[token_count - 1] = 0;
            }
            
        }
        // user entered no input, program should output nothing
        else {
            tokens[0] = 0;
        }
    }
    // signalling, program should do nothing more than signal
    else {
        tokens[0] = 0;
    }

    free(input);
}

char *read_line(void)
{
  int bufsize = COMMAND_LENGTH;
  int position = 0;
  char *buffer = malloc(sizeof(char) * bufsize);
  int c;

  if (!buffer) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  while (1) {
    // Read a character
    c = getchar();

    // If we hit EOF, replace it with a null character and return.
    if (c == EOF || c == '\n') {
      buffer[position] = '\0';
      return buffer;
    } else {
      buffer[position] = c;
    }
    position++;

    // If we have exceeded the buffer, reallocate.
    if (position >= bufsize) {
      bufsize += COMMAND_LENGTH;
      buffer = realloc(buffer, bufsize);
      if (!buffer) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
      }
    }
  }
}

/*
 * Main and Execute Commands
 */
int main(int argc, char* argv[])
{
    char* p = getenv("PATH");
    setenv("PATH", strcat(p, ":."), 1);
    char *tokens[NUM_TOKENS];

    while (true) {
        char *cmd = NULL;
        _Bool freeCmd = false;

        _Bool in_background = false;
        
        printf("> ");
        char* line = read_line();

        read_command(line, tokens, &in_background);

        // no input entered
        if(tokens[0] == 0) {
            ; //Do nothing
        }

        /* Internal Commands
         * quit:    Exit the program. Does not matter how many arguments the user
         *		    enters; they are all ignored.
         */

        // exit command
        else if (strcmp(tokens[0],"quit") == 0) {
            exit(0);
        }

        else {

            /* Fork a child process */
            pid_t pid = fork();

            /* Error occured */
            if (pid < 0) {
                write(2, "fork() Failed.\n", strlen("fork() Failed.\n"));
                exit(-1);
            }
            /* Child process invokes execvp() using results in token array. */
            else if (pid == 0) {
                //print error if error occurred in execvp
                if(execvp(tokens[0], tokens) == -1){
                    write(STDOUT_FILENO, tokens[0], strlen(tokens[0]));
                    write(STDOUT_FILENO, ": Unknown command.\n", strlen(": Unknown command.\n"));
                    exit(0);
                }

            }
            /* Parent process
             *    If in_background is false, parent waits for
             *    child to finish. Otherwise, parent loops back to
             *    read_command() again immediately.
             */
            else {

                //Child not running in background, wait
                if (!in_background) {
                    waitpid(pid, NULL, 0);
                }
                else {
                    ; //If child running in background, don't wait
                }
            }
        }
        /* Cleanup any previously exited background child processes (zombies) */
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            ; //do nothing
        }

        // Free cmd, if it has been allocated.
        if(freeCmd) {
            free(cmd);
        }
    }

    return 0;
}
