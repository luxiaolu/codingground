#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#define COMMAND_LENGTH 1024
#define MAX_BACK_NUM 1024
#define MAX_TOKENS_NUM (COMMAND_LENGTH / 2 + 1)
#define DEAD "dead"
#define PROMPT ">>>"

typedef struct rusage USAGE;
typedef struct timeval TIME;

char* builtin_cmd[] = {
    "cd",
    "jobs",
    "exit"
};

pid_t background_pid_list[MAX_BACK_NUM];
char* background_cmd_list[MAX_BACK_NUM];
USAGE* background_usage_list[MAX_BACK_NUM];
TIME* background_time_list[MAX_BACK_NUM];
int who = RUSAGE_CHILDREN;

//------------------ built-in function ------------------
int cd_func(char** args);
int jobs_func(char** args);
int exit_func(char** args);

int (*builtin_func[]) (char **) = {
    &cd_func,
    &jobs_func,
    &exit_func
};

int cd_func(char** args) {
    if( args[1] == NULL ) {
        fprintf(stderr, "ERROR: need argument for \"cd\"\n");
    }
    else {
        if(chdir(args[1]) != 0) {
            perror("doit");
        }
    }
    return 1;
}

int jobs_func(char** args) {
    int i;
    for(i=0; i<MAX_BACK_NUM; i++) {
        if(background_pid_list[i] > 0) {
            printf("[%d] %d %s\n", i+1, background_pid_list[i], background_cmd_list[i]);
        }
    }
    return 1;
}

int exit_func(char** args) {
    return 0;
}


//------------------- utils -----------------
int show_prompt() {
    printf("%s", PROMPT);
    return 0;
}

void output_usage(struct rusage *before, struct rusage *after, struct timeval *start, struct timeval *end) {
    double wall_time = ((*end).tv_sec - (*start).tv_sec) * 1000
            + ((*end).tv_usec - (*start).tv_usec) / 1000;
    double user_time = (after->ru_utime.tv_sec*1000 + after->ru_utime.tv_usec/1000) -
            (before->ru_utime.tv_sec*1000 + before->ru_utime.tv_usec/1000);

    double system_time = (after->ru_stime.tv_sec*1000 + after->ru_stime.tv_usec/1000) -
            (before->ru_stime.tv_sec*1000 + before->ru_stime.tv_usec/1000);

    long page_faults = after->ru_majflt - before->ru_majflt;
    long soft_faults = after->ru_minflt - before->ru_minflt;
    long invol = after->ru_nivcsw - before->ru_nivcsw;
    long vol = after->ru_nvcsw - before->ru_nvcsw;
    long resdent = after->ru_maxrss - before->ru_maxrss;

    printf("%-20f: CPU time used.\n", user_time + system_time);
    printf("%-20f: wall-clock time.\n", wall_time);
    printf("%-20ld: Number of times preempted involumntarily.\n", invol);
    printf("%-20ld: Number of times preempted volumntarily.\n", vol);
    printf("%-20ld: Number of page faults.\n", page_faults);
    printf("%-20ld: Number of page reclaims.\n", soft_faults);
    printf("%-20ld: Number of residend set size used.\n", resdent);
    return;
}

char* get_input_line() {
    char *line = NULL;
    ssize_t buf_size = 0;
    getline(&line, &buf_size, stdin);
    return line;
}

void child_dead(int sig) {
    int i;
    pid_t pid;
    for(i=0; i<MAX_BACK_NUM; i++) {
        if(background_pid_list[i] != 0) {
            pid = waitpid(background_pid_list[i], NULL, WNOHANG);
            if(pid > 0) {
                // mark for dead

                printf("\n[%d] %d Completed\n", i+1, background_pid_list[i]);
                TIME end;
                USAGE after;
                TIME* start = background_time_list[i];
                USAGE* before = background_usage_list[i];
                gettimeofday(&end, NULL);
                getrusage(who, &after);
                printf("***************** %s ****************\n", background_cmd_list[i]);
                output_usage(before, &after, start, &end);

                free(background_cmd_list[i]);
                free(background_time_list[i]);
                free(background_usage_list[i]);
                background_pid_list[i] = 0;
                background_cmd_list[i] = 0;
                background_time_list[i] = 0;
                background_usage_list[i] = 0;
            }
        }
    }
    return;
}

int tokenize_command(char* buff, char* tokens[]) {
    int token_count = 0;
    int in_token = 0;
    int num_chars = strlen(buff);
    int i;
    for(i=0; i<num_chars; i++) {
        switch(buff[i]) {
        case ' ':
        case '\t':
        case '\n':
            buff[i] = '\0';
            in_token = 0;
            break;

        default:
            if(!in_token) {
                tokens[token_count] = &buff[i];
                token_count++;
                in_token = 1;
            }
        }
    }
    tokens[token_count] = 0;
    return token_count;
}

void read_command(char* buff, char *tokens[], int *background) {
    *background = 0;

    int length = strlen(buff);
    char* input = calloc(length + 1, 1);
    int i;
    for(i=0; i<length; i++) {
        input[i] = buff[i];
    }

    if(length > 0 && input[0] != '\n') {
        buff[length] = '\0';
        if(buff[strlen(buff) - 1] == '\n')
            buff[length-1] = '\0';

        int token_count = tokenize_command(buff, tokens);
        if(token_count == 0) {
            tokens[0] = 0;
            free(input);
            return;
        }

        //backgound?
        char* last_token = tokens[token_count-1];
        if(last_token[strlen(last_token) - 1] == '&') {
            *background = 1;
            if( strlen(last_token) == 1 ) {
                tokens[token_count - 1] = 0;
            }
            else {
                last_token[strlen(last_token) - 1] = '\0';
            }
        }
    }
    else {
        tokens[0] = NULL;
    }

    free(input);
}


int run_command(char** tokens, int backgound) {
    // is builtin command?
    size_t i;
    for(i=0; i<(sizeof(builtin_cmd) / sizeof(char*)); i++) {
        if(strcmp(tokens[0], builtin_cmd[i]) == 0) {
            return (*builtin_func[i])(tokens);
        }
    }

    USAGE* before = malloc(sizeof(USAGE));
    getrusage(who, before);
    TIME* start = malloc(sizeof(TIME));
    gettimeofday(start, NULL);

    // non builtin command
    pid_t pid;
    int status;
    pid = fork();
    if( pid == 0 ) {
        // child process
        if(execvp(tokens[0], tokens) == -1) {
            perror("doit");
        }
        exit(1);
    }
    else if( pid < 0 ) {
        // failed folking
        perror("doit");
    }
    else {
        // parent process
        if( !backgound ) {
            waitpid(pid, &status, 0);

            USAGE after;
            TIME end;
            gettimeofday(&end, NULL);
            getrusage(who, &after);
            printf("***************** %s ****************\n", tokens[0]);
            output_usage(before, &after, start, &end);
            free(before);
            free(start);
        }
        else {
            // add child pid to back_ground_list
            int j;
            for(j=0; j<MAX_BACK_NUM; j++) {
                if(background_pid_list[j] == 0) {
                    // usage
                    background_usage_list[j] = before;
                    background_time_list[j] = start;

                    // pid
                    background_pid_list[j] = pid;

                    // name
                    char* cmd_name = (char*)malloc(1024 * sizeof(char));
                    strcpy(cmd_name, tokens[0]);
                    background_cmd_list[j] = cmd_name;
                    printf("[%d] %d\n", j+1, pid);
                    break;
                }
            }
            ; // do noting
        }
    }
    return 1;
}

int main(int argc, char** argv) {
    // ini the background list
    int i;
    for(i=0; i<MAX_BACK_NUM; i++) {
        background_pid_list[i] = 0;
        background_cmd_list[i] = 0;
        background_usage_list[i] = 0;
        background_time_list[i] = 0;
    }

    int single_shot = 0;
    int status = 1;
    int backgound = 0;
    char** tokens = NULL;
    char* line;
    if( argc > 1 ) {
        tokens = ++argv;
        single_shot = 1;
    }
    else {
        char *tokens_[MAX_TOKENS_NUM];
        tokens = tokens_;
    }

    signal(SIGCHLD, child_dead);

    do {
        if( !single_shot ) {
            // status
            backgound = 0;
            // show prompt
            show_prompt();
            // get command
            line = get_input_line();

            read_command(line, tokens, &backgound);
            if(tokens[0] == 0) {
                continue;
            }
        }
        status = run_command(tokens, backgound);
        free(line);
    } while(!single_shot && status);

    return 0;
}
