/**
 * SLOsh - San Luis Obispo Shell
 * CSC 453 - Operating Systems
 *
 * TODO: Complete the implementation according to the comments
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <unistd.h>
 #include <sys/wait.h>
 #include <sys/types.h>
 #include <fcntl.h>
 #include <signal.h>
 #include <limits.h>
 #include <errno.h>

/* Define PATH_MAX if it's not available */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64

/* Global variable for signal handling */
volatile sig_atomic_t child_running = 0;

/* Forward declarations */
void display_prompt(void);

/**
 * Signal handler for SIGINT (Ctrl+C)
 *
 * TODO: Handle Ctrl+C appropriately. Think about what behavior makes sense
 * when the user presses Ctrl+C - should the shell exit? should a child process
 * be interrupted?
 * Hint: The global variable tracks important state.
 */
void sigint_handler(int sig) {
    (void) sig;

    if (child_running) return;
    else {
        const char line = '\n';
        write(STDOUT_FILENO, &line, 1);
    }
}

/**
 * Display the command prompt with current directory
 */
void display_prompt(void) {
    char cwd[PATH_MAX];
    char prompt_buf[PATH_MAX + 3];
    int len;

    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        len = snprintf(prompt_buf, sizeof(prompt_buf), "%s> ", cwd);
    } else {
        len = snprintf(prompt_buf, sizeof(prompt_buf), "SLOsh> ");
    }

    if (len > 0 && len < (int)sizeof(prompt_buf)) {
        write(STDOUT_FILENO, prompt_buf, len);
    }
}

/**
 * Parse the input line into command arguments
 *
 * TODO: Extract tokens from the input string. What should you do with special
 * characters like pipes and redirections? How will the rest of the code know
 * what to execute?
 * Hint: You'll need to handle more than just splitting on spaces.
 *
 * @param input The input string to parse
 * @param args Array to store parsed arguments
 * @return Number of arguments parsed
 */

int special(char c) {
    return c == '|' || c == '>';
}

// this is so leet code esque i love it (2 pointers!)

int parse_input(char *input, char **args) {
    int arg_i = 0;
    int in_i = 0;

    while (input[in_i] != '\0') {
        // Skip whitespace
        while (input[in_i] == ' ' || input[in_i] == '\t' || input[in_i] == '\n') in_i++;
        if (input[in_i] == '\0') break;

        // Handle special tokens
        if (special(input[in_i])) {
            if (input[in_i] == '>' && input[in_i + 1] == '>') {
                args[arg_i] = malloc(3);
                strcpy(args[arg_i], ">>");
                arg_i++;
                in_i += 2;
            } else {
                args[arg_i] = malloc(2);
                args[arg_i][0] = input[in_i];
                args[arg_i][1] = '\0';
                arg_i++;
                in_i++;
            }
        } else {
            // Handle regular tokens (alphanumeric and others)
            int start = in_i;
            while (input[in_i] != '\0' && !special(input[in_i]) && input[in_i] != ' ' && input[in_i] != '\t' && input[in_i] != '\n') in_i++;
            int len = in_i - start;
            args[arg_i] = malloc(len + 1);
            strncpy(args[arg_i], &input[start], len);
            args[arg_i][len] = '\0';
            arg_i++;
        }
    }

    args[arg_i] = NULL;

    // // Debug print of args array
    // printf("ARGS: ");
    // for (int j = 0; j < arg_i; j++) {
    //     printf("[%s] ", args[j]);
    // }
    // printf("\n");

    return arg_i;
}

/**
 * Execute the given command with its arguments
 *
 * TODO: Run the command. Your implementation should handle:
 * - Basic command execution
 * - Pipes (|)
 * - Output redirection (> and >>)
 *
 * What system calls will you need? How do you connect processes together?
 * How do you redirect file descriptors?
 *
 * @param args Array of command arguments (NULL-terminated)
 */
void execute_command(char **args) {
    int i = 0;
    int pipefd[2];
    int prev = -1;
    pid_t pid;
    int status;

    child_running = 1;

    while (args[i] != NULL) {
        char *cmd[MAX_ARGS];
        int index = 0, out_fd = -1, append = 0;
        // out_fd mainly for tracking whether this command's output should go to a pipe or a file

        // keep the append to ensure for truncating or appending to the file

        // keep going until we hit a pipe, indicated by that | thingy
        while (args[i] != NULL && strcmp(args[i], "|") != 0) {
            if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0) {
                append = (args[i][1] == '>');
                i++;
                if (args[i] == NULL) break;

                int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                // append if we wanna either delete or append to existing files
                out_fd = open(args[i], flags, 0644);

                // error checks required
                if (out_fd < 0) {
                    perror("open");
                    child_running = 0;
                    return;
                }
                i++;
            }
            else cmd[index++] = args[i++];
        }

        cmd[index] = NULL;
        if (cmd[0] == NULL) {
            child_running = 0;
            return;
        }

        if (args[i] && strcmp(args[i], "|") == 0) i++;
        // don't put in the arguments and stuff

        // have to create a pipe here because of how there's another command, the output isn't redirected to a file
        if (args[i] != NULL && out_fd == -1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                child_running = 0;
                return;
            }
        }

        // will only happen when there isn't a > or >> this is correct behavior

        pid = fork();
        if (pid < 0) {
            perror("fork");
            child_running = 0;
            return;
        }

        if (pid == 0) {
            // ctrl+c will now kill the child, but not the shell
            signal(SIGINT, SIG_DFL);

            // if prev isn't -1 this means that it holds the read end of the previous pipe, so we can start passing info i think
            // yeah read from previous pipe instead of terminal
            if (prev != -1) {
                if (dup2(prev, STDIN_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(prev);
            }

            if (args[i] != NULL && out_fd == -1) {
                // output is written into the pipe to be placed onto the terminal
                if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(pipefd[0]);
                close(pipefd[1]);
            }

            if (out_fd != -1) {
                // redirected to the file now
                if (dup2(out_fd, STDOUT_FILENO) < 0) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
                close(out_fd);
            }

            // replace the child process
            // load the actual program
            // won't return on success like quiz says
            execvp(cmd[0], cmd);
            perror("execvp");
            exit(EXIT_FAILURE);
        }

        if (prev != -1) close(prev);
        if (args[i] != NULL && out_fd == -1) {
            close(pipefd[1]);
            prev = pipefd[0];
        }

        if (out_fd != -1) close(out_fd);

        if (args[i] == NULL) break;
    }

    while (1) {
        pid_t w = waitpid(-1, &status, 0);
        if (w > 0) {
            if (WIFSIGNALED(status))
                printf("terminated by signal %d\n", WTERMSIG(status));
            else if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
                printf("exit status %d\n", WEXITSTATUS(status));
        } 
        else {
            if (errno == ECHILD) break;
            perror("waitpid");
            break;
        }
    }

    child_running = 0;
}


/**
 * Check for and handle built-in commands
 *
 * TODO: Implement support for built-in commands:
 * - exit: Exit the shell
 * - cd: Change directory
 *
 * @param args Array of command arguments (NULL-terminated)
 * @return 0 to exit shell, 1 to continue, -1 if not a built-in command
 */
int handle_builtin(char **args) {
    if (strcmp(args[0], "exit") == 0) return 0;
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) chdir(getenv("HOME"));
        else {
            if (chdir(args[1]) != 0) perror("cd");
        }
        return 1;
    }

    return -1;
}

int main(void) {
    char input[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];
    int status = 1;
    int builtin_result;

    /* TODO: Set up signal handling. Which signals matter to a shell? */
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    while (status) {
        display_prompt();

        /* Read input and handle signal interruption */
        // Close shell on EOF (Ctrl+D) or error
        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            if (feof(stdin)) break;
            break;
        }

        /* Parse input */
        parse_input(input, args);

        /* Handle empty command */
        if (args[0] == NULL) {
            continue;
        }

        /* Check for built-in commands */
        builtin_result = handle_builtin(args);
        if (builtin_result >= 0) {
            status = builtin_result;
            continue;
        }

        /* Execute external command */
        execute_command(args);

        // Free each allocated string in args
        for (int i = 0; i < MAX_ARGS && args[i] != NULL; i++) {
            free(args[i]);
        }
    }

    printf("SLOsh exiting...\n");
    return EXIT_SUCCESS;
}
