#include "../include/shell.h"
#include "../include/parser.h"

int is_home_path(char *path, char *home)
{
    int n = strlen(home);
    if (strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == '\0'))
    {
        return 1;
    }
    return 0;
}

char *format_path(char *path, char *home)
{
    // ############## LLM Generated Code Begins ##############
    if (is_home_path(path, home))
    {
        int suffix_len = strlen(path) - strlen(home);
        char *result = malloc(1 + suffix_len + 1);
        if (!result)
            return NULL;
        result[0] = '~';
        strcpy(result + 1, path + strlen(home));
        return result;
    }
    else
    {
        return strdup(path);
    }
    // ############## LLM Generated Code Ends ################
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
        if (!validate_command(input))
        {
            printf("Invalid Syntax!\n");
        }
        free(display_path);
        // ############## LLM Generated Code Ends ################
    }

    free(home);
    return 0;
}
