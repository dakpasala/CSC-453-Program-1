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

// this is so leet code esque i love it

int parse_input(char *input, char **args) {
    int index = 0;
    int i = 0;

    while (input[i] != '\0') {
        while (input[i] == ' ' || input[i] == '\t' || input[i] == '\n') input[i++] = '\0';

        if (input[i] == '\0') break;
        if (special(input[i])) {
            if (input[i] == '>' && input[i + 1] == '>') {
                args[index++] = ">>";
                i += 2;
            } 
            else {
                char *op = &input[i];
                input[i + 1] = '\0';
                args[index++] = op;
                i++;
            }
        }
        else {
            args[index++] = &input[i];
            while (input[i] != '\0' && input[i] != ' ' && !special(input[i])) i++;
        }
    }

    args[index] = NULL;
    return index;
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

        while (args[i] != NULL && strcmp(args[i], "|") != 0) {
            if (strcmp(args[i], ">") == 0 || strcmp(args[i], ">>") == 0) {
                append = (args[i][1] == '>');
                i++;
                if (args[i] == NULL) break;

                int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
                out_fd = open(args[i], flags, 0644);
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

        if (args[i] != NULL && out_fd == -1) {
            if (pipe(pipefd) < 0) {
                perror("pipe");
                child_running = 0;
                return;
            }
        }

        pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);

            if (prev != -1) {
                dup2(prev, STDIN_FILENO);
                close(prev);
            }

            if (args[i] != NULL && out_fd == -1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            if (out_fd != -1) {
                dup2(out_fd, STDOUT_FILENO);
                close(out_fd);
            }

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

    while (waitpid(-1, &status, 0) > 0) {
        if (WIFSIGNALED(status)) printf("terminated by signal %d\n", WTERMSIG(status));
        else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) printf("exit status %d\n", WEXITSTATUS(status));
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
    /* TODO: Your implementation here */
    return -1;  /* Not a builtin command */
}

int main(void) {
    char input[MAX_INPUT_SIZE];
    char *args[MAX_ARGS];
    int status = 1;
    int builtin_result;

    /* TODO: Set up signal handling. Which signals matter to a shell? */

    while (status) {
        display_prompt();

        /* Read input and handle signal interruption */
        if (fgets(input, MAX_INPUT_SIZE, stdin) == NULL) {
            /* TODO: Handle the case when fgets returns NULL. When does this happen? */
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
    }

    printf("SLOsh exiting...\n");
    return EXIT_SUCCESS;
}
