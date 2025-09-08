#include "../include/shell.h"
#include "../include/parser.h"
#include "../include/commands.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

// REMOVED: run_command function - it's already defined in commands.c

int is_home_path(char *path, char *home) {
    int n = strlen(home);
    if (strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == '\0')) {
        return 1;
    }
    return 0;
}

char *format_path(char *path, char *home) {
    if (is_home_path(path, home)) {
        int suffix_len = strlen(path) - strlen(home);
        char *result = malloc(1 + suffix_len + 1);
        if (!result)
            return NULL;
        result[0] = '~';
        strcpy(result + 1, path + strlen(home));
        return result;
    } else {
        return strdup(path);
    }
}

int main()
{
    char cwd[PATH_MAX];
    char hostname[HOST_NAME_MAX];
    char *username;
    char *home;
    // ############## LLM Generated Code Begins ##############
    username = getenv("USER");
    if (!username)
    {
        username = getlogin();
    }
    // ############## LLM Generated Code Ends ################
    gethostname(hostname, sizeof(hostname));

    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        perror("getcwd");
        exit(1);
    }
    home = strdup(cwd);
    
    // Set the shell's home directory for the hop command
    set_shell_home(home);
    
    // Load persistent log on startup
    load_log();

    while (true)
    {
        if (getcwd(cwd, sizeof(cwd)) == NULL)
        {
            perror("getcwd");
            exit(1);
        }

        char *display_path = format_path(cwd, home);

        printf("<%s@%s:%s> ", username, hostname, display_path);
        fflush(stdout);

        // ############## LLM Generated Code Begins ##############
        char input[1024];
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            printf("\n");
            break;
        }

        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "exit") == 0)
        {
            break;
        }

        // Preserve the full command line before tokenization for logging
        char full_cmd[1024];
        strncpy(full_cmd, input, sizeof(full_cmd) - 1);
        full_cmd[sizeof(full_cmd) - 1] = '\0';

        if (validate_command(full_cmd))
        {
            // Tokenize from a separate mutable copy
            char cmdline[1024];
            strncpy(cmdline, full_cmd, sizeof(cmdline) - 1);
            cmdline[sizeof(cmdline) - 1] = '\0';

            char *args[64];
            int argc = 0;
            char *token = strtok(cmdline, " ");
            while (token && argc < 63)
            {
                args[argc++] = token;
                token = strtok(NULL, " ");
            }
            args[argc] = NULL;

            struct Command cmd;
            cmd.name = args[0];
            cmd.argv = args;
            cmd.argc = argc;
            cmd.input_file = NULL;
            cmd.output_file = NULL;
            cmd.append = false;
            cmd.pipe_next = NULL;

            // Always delegate skip rules to add_log (it will avoid storing 'log' atomics and duplicates)
            add_log(full_cmd);

            run_command(&cmd);  // This will call the function from commands.c
        }
        else
        {
            printf("Invalid Syntax!\n");
        }
        free(display_path);
        // ############## LLM Generated Code Ends ################
    }

    free(home);
    return 0;
}