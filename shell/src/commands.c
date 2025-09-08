// reveal command implementation
#include "../include/commands.h"
#include "../include/parser.h"
#include <dirent.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#define LOG_MAX 15
#define LOG_FILE ".minishell_log"

static char prev_dir[PATH_MAX] = "";
static char shell_home[PATH_MAX] = ""; // Store shell's home directory
static bool hop_has_run = false;       // Track if hop has ever been run
static char log_buffer[LOG_MAX][1024];
static int log_count = 0;
static int log_start = 0;

// Function to set the shell's home directory (called from main)
void set_shell_home(const char *home_path)
{
    strncpy(shell_home, home_path, sizeof(shell_home) - 1);
    shell_home[sizeof(shell_home) - 1] = '\0';
}

void load_log()
{
    FILE *f = fopen(LOG_FILE, "r");
    if (!f)
        return;
    log_count = 0;
    log_start = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\n")] = 0;
        strncpy(log_buffer[log_count++], line, sizeof(log_buffer[0]));
        if (log_count == LOG_MAX)
            break;
    }
    fclose(f);
}

void save_log()
{
    FILE *f = fopen(LOG_FILE, "w");
    if (!f)
        return;
    for (int i = 0; i < log_count; ++i)
    {
        int idx = (log_start + i) % LOG_MAX;
        fprintf(f, "%s\n", log_buffer[idx]);
    }
    fclose(f);
}

void add_log(const char *cmd)
{
    if (!cmd || !*cmd)
        return;

    // Skip if identical to the previously stored command (exact match)
    if (log_count > 0)
    {
        int last_idx = (log_start + log_count - 1) % LOG_MAX;
        if (strcmp(log_buffer[last_idx], cmd) == 0)
            return;
    }

    // Do not store any shell_cmd if the command name of an atomic command is "log"
    if (contains_atomic_command_name(cmd, "log"))
        return;

    // Save the full shell_cmd string (oldest -> newest), overwriting the oldest when full
    if (log_count < LOG_MAX)
    {
        int pos = (log_start + log_count) % LOG_MAX;
        strncpy(log_buffer[pos], cmd, sizeof(log_buffer[0]) - 1);
        log_buffer[pos][sizeof(log_buffer[0]) - 1] = '\0';
        log_count++;
    }
    else
    {
        strncpy(log_buffer[log_start], cmd, sizeof(log_buffer[0]) - 1);
        log_buffer[log_start][sizeof(log_buffer[0]) - 1] = '\0';
        log_start = (log_start + 1) % LOG_MAX;
    }
    save_log();
}

int log_command(int argc, char **argv)
{
    if (argc == 1)
    {
        // Print log oldest to newest
        for (int i = 0; i < log_count; ++i)
        {
            int idx = (log_start + i) % LOG_MAX;
            printf("%s\n", log_buffer[idx]);
        }
        return 0;
    }
    else if (argc == 2 && strcmp(argv[1], "purge") == 0)
    {
        log_count = 0;
        log_start = 0;
        save_log();
        return 0;
    }
    else if (argc == 3 && strcmp(argv[1], "execute") == 0)
    {
        int idx = atoi(argv[2]);
        if (idx < 1 || idx > log_count)
        {
            printf("log: Invalid index!\n");
            return 1;
        }
        int log_idx = (log_start + log_count - idx) % LOG_MAX;
        // Parse and execute the command
        char cmdline[1024];
        strncpy(cmdline, log_buffer[log_idx], sizeof(cmdline));
        // Tokenize
        char *args[64];
        int ac = 0;
        char *tok = strtok(cmdline, " ");
        while (tok && ac < 63)
        {
            args[ac++] = tok;
            tok = strtok(NULL, " ");
        }
        args[ac] = NULL;
        // Do not log this execution
        if (strcmp(args[0], "hop") == 0)
            return hop(ac, args);
        if (strcmp(args[0], "reveal") == 0)
            return reveal(ac, args);
        // Fallback: execute
        pid_t pid = fork();
        if (pid == 0)
        {
            execvp(args[0], args);
            printf("Command not found!\n");
            exit(1);
        }
        else if (pid > 0)
        {
            int status;
            waitpid(pid, &status, 0);
            return 0;
        }
        else
        {
            perror("fork");
            return 1;
        }
    }
    else
    {
        printf("log: Invalid Syntax!\n");
        return 1;
    }
}

int reveal(int argc, char **argv)
{
    bool show_all = false, line_by_line = false;
    int arg_start = 1;
    // Parse flags
    while (arg_start < argc && argv[arg_start][0] == '-' && strlen(argv[arg_start]) > 1)
    {
        for (int j = 1; argv[arg_start][j]; ++j)
        {
            if (argv[arg_start][j] == 'a')
                show_all = true;
            else if (argv[arg_start][j] == 'l')
                line_by_line = true;
            else
            {
                printf("reveal: Invalid Syntax!\n");
                return 1;
            }
        }
        arg_start++;
    }
    int dir_args = argc - arg_start;
    if (dir_args > 1)
    {
        printf("reveal: Invalid Syntax!\n");
        return 1;
    }
    // Determine target directory
    char target[PATH_MAX];
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
    {
        perror("getcwd");
        return 1;
    }
    if (dir_args == 0 || (dir_args == 1 && strcmp(argv[arg_start], "~") == 0))
    {
        if (shell_home[0] != '\0')
            strncpy(target, shell_home, sizeof(target));
        else
        {
            char *home = getenv("HOME");
            if (!home)
                home = "/";
            strncpy(target, home, sizeof(target));
        }
    }
    else if (strcmp(argv[arg_start], ".") == 0)
    {
        strncpy(target, cwd, sizeof(target));
    }
    else if (strcmp(argv[arg_start], "..") == 0)
    {
        strncpy(target, cwd, sizeof(target));
        if (strcmp(cwd, "/") != 0)
        {
            if (chdir("..") == 0)
            {
                if (getcwd(target, sizeof(target)) == NULL)
                {
                    printf("No such directory!\n");
                    chdir(cwd);
                    return 1;
                }
                chdir(cwd);
            }
            else
            {
                printf("No such directory!\n");
                return 1;
            }
        }
    }
    else if (strcmp(argv[arg_start], "-") == 0)
    {
        if (!hop_has_run)
        {
            printf("No such directory!\n");
            return 1;
        }
        if (prev_dir[0] == '\0')
        {
            printf("No such directory!\n");
            return 1;
        }
        strncpy(target, prev_dir, sizeof(target));
    }
    else
    {
        strncpy(target, argv[arg_start], sizeof(target));
    }
    // Open directory
    DIR *dir = opendir(target);
    if (!dir)
    {
        printf("No such directory!\n");
        return 1;
    }
    // Collect entries
    struct dirent *entry;
    char *names[1024];
    int count = 0;
    while ((entry = readdir(dir)))
    {
        if (!show_all && entry->d_name[0] == '.')
            continue;
        names[count++] = strdup(entry->d_name);
    }
    closedir(dir);
    // Sort lexicographically (ASCII)
    for (int i = 0; i < count - 1; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            if (strcmp(names[i], names[j]) > 0)
            {
                char *tmp = names[i];
                names[i] = names[j];
                names[j] = tmp;
            }
        }
    }
    // Print
    if (line_by_line)
    {
        for (int i = 0; i < count; ++i)
            printf("%s\n", names[i]);
    }
    else
    {
        for (int i = 0; i < count; ++i)
            printf("%s%s", names[i], (i == count - 1) ? "\n" : " ");
    }
    for (int i = 0; i < count; ++i)
        free(names[i]);
    return 0;
}

int hop(int argc, char **argv)
{
    char cwd[PATH_MAX];

    // Get current working directory at the start
    if (!getcwd(cwd, sizeof(cwd)))
    {
        perror("getcwd");
        return 1;
    }

    // No arguments: change to shell's home directory
    if (argc == 1)
    {
        const char *home = (shell_home[0] != '\0') ? shell_home : getenv("HOME");
        if (!home)
        {
            printf("No such directory!\n");
            return 1;
        }
        if (strcmp(cwd, home) == 0)
        {
            return 0; // already at home
        }
        char old[PATH_MAX];
        strncpy(old, cwd, sizeof(old) - 1);
        old[sizeof(old) - 1] = '\0';
        if (chdir(home) != 0)
        {
            printf("No such directory!\n");
            return 1;
        }
        // update prev_dir to the directory we just left
        strncpy(prev_dir, old, sizeof(prev_dir) - 1);
        prev_dir[sizeof(prev_dir) - 1] = '\0';
        hop_has_run = true;
        return 0;
    }

    // Process each argument sequentially
    for (int i = 1; i < argc; ++i)
    {
        char *arg = argv[i];

        if (!getcwd(cwd, sizeof(cwd)))
        {
            perror("getcwd");
            return 1;
        }

        if (strcmp(arg, ".") == 0)
        {
            // stay
            continue;
        }
        else if (strcmp(arg, "~") == 0)
        {
            const char *home = (shell_home[0] != '\0') ? shell_home : getenv("HOME");
            if (!home)
            {
                printf("No such directory!\n");
                return 1;
            }
            if (strcmp(cwd, home) == 0)
            {
                continue; // no change
            }
            char old[PATH_MAX];
            strncpy(old, cwd, sizeof(old) - 1);
            old[sizeof(old) - 1] = '\0';
            if (chdir(home) != 0)
            {
                printf("No such directory!\n");
                return 1;
            }
            strncpy(prev_dir, old, sizeof(prev_dir) - 1);
            prev_dir[sizeof(prev_dir) - 1] = '\0';
            hop_has_run = true;
        }
        else if (strcmp(arg, "..") == 0)
        {
            // If at root, do nothing
            if (strcmp(cwd, "/") == 0)
            {
                continue;
            }
            char old[PATH_MAX];
            strncpy(old, cwd, sizeof(old) - 1);
            old[sizeof(old) - 1] = '\0';
            if (chdir("..") != 0)
            {
                printf("No such directory!\n");
                return 1;
            }
            strncpy(prev_dir, old, sizeof(prev_dir) - 1);
            prev_dir[sizeof(prev_dir) - 1] = '\0';
            hop_has_run = true;
        }
        else if (strcmp(arg, "-") == 0)
        {
            if (prev_dir[0] != '\0')
            {
                char old[PATH_MAX];
                strncpy(old, cwd, sizeof(old) - 1);
                old[sizeof(old) - 1] = '\0';
                if (chdir(prev_dir) != 0)
                {
                    printf("No such directory!\n");
                    return 1;
                }
                strncpy(prev_dir, old, sizeof(prev_dir) - 1);
                prev_dir[sizeof(prev_dir) - 1] = '\0';
                hop_has_run = true;
            }
            // else: no previous directory yet, do nothing
        }
        else
        {
            // named path
            char old[PATH_MAX];
            strncpy(old, cwd, sizeof(old) - 1);
            old[sizeof(old) - 1] = '\0';
            if (chdir(arg) != 0)
            {
                printf("No such directory!\n");
                return 1;
            }
            strncpy(prev_dir, old, sizeof(prev_dir) - 1);
            prev_dir[sizeof(prev_dir) - 1] = '\0';
            hop_has_run = true;
        }
    }

    return 0;
}

int run_command(struct Command *cmd)
{
    if (strcmp(cmd->name, "hop") == 0)
        return hop(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "reveal") == 0)
        return reveal(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "log") == 0)
        return log_command(cmd->argc, cmd->argv);
    else
    {
        // For other commands, fork and exec
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            return 1;
        }
        else if (pid == 0)
        {
            if (cmd->input_file)
                freopen(cmd->input_file, "r", stdin);
            if (cmd->output_file)
            {
                if (cmd->append)
                    freopen(cmd->output_file, "a", stdout);
                else
                    freopen(cmd->output_file, "w", stdout);
            }
            execvp(cmd->name, cmd->argv);
            printf("Command not found!\n");
            exit(1);
        }
        else
        {
            int status;
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
}