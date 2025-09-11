#define _DEFAULT_SOURCE
#include "../include/shell.h"
#include "../include/parser.h"
#include "../include/commands.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

// Process tracking (for both background and foreground processes)
// MAX_PROCESSES, ProcessState, and ProcessJob are defined in shell.h

ProcessJob process_jobs[MAX_PROCESSES];
static int next_job_number = 1;
static int process_count = 0;

// Signal handling variables
pid_t foreground_pgid = 0;  // Process group ID of current foreground process
volatile sig_atomic_t sigint_received = 0;  // Remove static to make accessible
volatile sig_atomic_t sigtstp_received = 0; // Remove static to make accessible

// Signal handler functions
void sigint_handler(int sig)
{
    sigint_received = 1;  // Set the flag
    // Only send SIGINT to the foreground process group if it exists
    if (foreground_pgid > 0)
    {
        kill(-foreground_pgid, SIGINT);
    }
    // No newline needed - the interrupted process or shell prompt will handle it
    // The shell itself ignores SIGINT
}

void sigtstp_handler(int sig)
{
    sigtstp_received = 1;  // Set the flag
    // Send SIGTSTP to the foreground process group if it exists
    if (foreground_pgid > 0)
    {
        kill(-foreground_pgid, SIGTSTP);
        
        // Find the process in our job list and mark it as stopped
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (process_jobs[i].active && process_jobs[i].pgid == foreground_pgid)
            {
                process_jobs[i].state = PROCESS_STOPPED;
                process_jobs[i].is_background = true;
                write(STDOUT_FILENO, "\n", 1);  // Single newline
                printf("[%d] Stopped %s\n", process_jobs[i].job_number, process_jobs[i].command_name);
                break;
            }
        }
    }
    // No else clause needed - if no foreground process, just ignore Ctrl+Z
    // The shell itself ignores SIGTSTP
}

// Function to install signal handlers
void setup_signal_handlers()
{
    struct sigaction sa_int, sa_tstp;
    
    // Set up SIGINT handler (Ctrl-C)
    sa_int.sa_handler = sigint_handler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0; // Don't restart interrupted system calls
    sigaction(SIGINT, &sa_int, NULL);
    
    // Set up SIGTSTP handler (Ctrl-Z)
    sa_tstp.sa_handler = sigtstp_handler;
    sigemptyset(&sa_tstp.sa_mask);
    sa_tstp.sa_flags = 0; // Don't restart interrupted system calls
    sigaction(SIGTSTP, &sa_tstp, NULL);
}

// Function to clean up all child processes (for Ctrl-D)
void cleanup_all_processes()
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active)
        {
            kill(process_jobs[i].pid, SIGKILL);
            waitpid(process_jobs[i].pid, NULL, 0);  // Reap the child
        }
    }
}

// Helper functions for signal handling
int get_job_number_by_pid(pid_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active && process_jobs[i].pid == pid)
        {
            return process_jobs[i].job_number;
        }
    }
    return -1;
}

const char *get_command_name_by_pid(pid_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active && process_jobs[i].pid == pid)
        {
            return process_jobs[i].command_name;
        }
    }
    return NULL;
}

void mark_job_stopped(pid_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active && process_jobs[i].pid == pid)
        {
            process_jobs[i].state = PROCESS_STOPPED;
            process_jobs[i].is_background = true; // Move to background when stopped
            break;
        }
    }
}

// Helper functions for fg/bg commands
struct ProcessJob *get_job_by_number(int job_number)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active && process_jobs[i].job_number == job_number)
        {
            return &process_jobs[i];
        }
    }
    return NULL;
}

struct ProcessJob *get_most_recent_job(void)
{
    struct ProcessJob *most_recent_stopped = NULL;
    struct ProcessJob *most_recent_background = NULL;
    int max_stopped_job_number = 0;
    int max_background_job_number = 0;
    
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active)
        {
            // Prioritize stopped jobs over background running jobs
            if (process_jobs[i].state == PROCESS_STOPPED &&
                process_jobs[i].job_number > max_stopped_job_number)
            {
                most_recent_stopped = &process_jobs[i];
                max_stopped_job_number = process_jobs[i].job_number;
            }
            else if (process_jobs[i].is_background && 
                     process_jobs[i].state == PROCESS_RUNNING &&
                     process_jobs[i].job_number > max_background_job_number)
            {
                most_recent_background = &process_jobs[i];
                max_background_job_number = process_jobs[i].job_number;
            }
        }
    }
    
    // Return stopped job if available, otherwise return background job
    return most_recent_stopped ? most_recent_stopped : most_recent_background;
}

// Process management functions
int add_process_job_with_command(pid_t pid, const char *command_name, const char *full_command, bool is_background)
{
    if (process_count >= MAX_PROCESSES)
    {
        return -1;
    }

    // Find the lowest available job number
    int job_number = 1;
    bool found_available = false;
    
    while (!found_available)
    {
        found_available = true;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (process_jobs[i].active && process_jobs[i].job_number == job_number)
            {
                found_available = false;
                job_number++;
                break;
            }
        }
    }

    // Find empty slot
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (!process_jobs[i].active)
        {
            process_jobs[i].job_number = job_number;
            process_jobs[i].pid = pid;
            process_jobs[i].pgid = pid;  // By default, pgid equals pid
            process_jobs[i].command_name = strdup(command_name);
            process_jobs[i].full_command = strdup(full_command ? full_command : command_name);
            process_jobs[i].active = true;
            process_jobs[i].is_background = is_background;
            process_jobs[i].state = PROCESS_RUNNING;
            process_count++;
            
            // Update next_job_number to be at least one more than the highest assigned job number
            if (job_number >= next_job_number)
            {
                next_job_number = job_number + 1;
            }
            
            return job_number;
        }
    }
    return -1;
}

// Backward compatibility wrapper
int add_process_job(pid_t pid, const char *command_name, bool is_background)
{
    return add_process_job_with_command(pid, command_name, command_name, is_background);
}

void remove_process_job(pid_t pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active && process_jobs[i].pid == pid)
        {
            free(process_jobs[i].command_name);
            free(process_jobs[i].full_command);
            process_jobs[i].active = false;
            process_count--;
            break;
        }
    }
}

// Check process state using kill(pid, 0)
ProcessState get_process_state(pid_t pid)
{
    int status;
    pid_t result = waitpid(pid, &status, WNOHANG | WUNTRACED);

    if (result == pid)
    {
        // Process has changed state
        if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            return PROCESS_TERMINATED;
        }
        else if (WIFSTOPPED(status))
        {
            return PROCESS_STOPPED;
        }
        // If we get here, the process state changed but not in expected ways
        // Check if process still exists
        if (kill(pid, 0) == 0)
        {
            return PROCESS_RUNNING;
        }
        else
        {
            return PROCESS_TERMINATED;
        }
    }
    else if (result == 0)
    {
        // No state change, process should still be running
        if (kill(pid, 0) == 0)
        {
            return PROCESS_RUNNING;
        }
        else
        {
            // Process no longer exists but we didn't get status from waitpid
            return PROCESS_TERMINATED;
        }
    }
    else if (result == -1)
    {
        // waitpid failed, check if process exists with kill
        if (errno == ECHILD)
        {
            // No child processes, so this one is gone
            return PROCESS_TERMINATED;
        }
        else if (kill(pid, 0) == 0)
        {
            // Process exists but waitpid failed for other reason
            return PROCESS_RUNNING;
        }
        else
        {
            return PROCESS_TERMINATED;
        }
    }

    return PROCESS_TERMINATED;
}

void update_process_states()
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (process_jobs[i].active)
        {
            int status;
            pid_t result = waitpid(process_jobs[i].pid, &status, WNOHANG | WUNTRACED);
            
            if (result == process_jobs[i].pid)
            {
                // Process has changed state
                if (WIFEXITED(status) || WIFSIGNALED(status))
                {
                    // Process terminated
                    if (process_jobs[i].is_background)
                    {
                        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
                        {
                            printf("%s with pid %d exited normally\n", process_jobs[i].command_name, process_jobs[i].pid);
                        }
                        else
                        {
                            printf("%s with pid %d exited abnormally\n", process_jobs[i].command_name, process_jobs[i].pid);
                        }
                    }
                    remove_process_job(process_jobs[i].pid);
                }
                else if (WIFSTOPPED(status))
                {
                    // Process was stopped
                    process_jobs[i].state = PROCESS_STOPPED;
                }
            }
            else if (result == 0)
            {
                // No state change, process might still be running or stopped
                if (kill(process_jobs[i].pid, 0) == 0)
                {
                    // Process exists, but no state change was detected by waitpid
                    // Keep the current state unchanged - if it was stopped, it's still stopped
                    // if it was running, it's still running
                }
                else
                {
                    // Process no longer exists
                    if (process_jobs[i].is_background)
                    {
                        printf("%s with pid %d exited abnormally\n", process_jobs[i].command_name, process_jobs[i].pid);
                    }
                    remove_process_job(process_jobs[i].pid);
                }
            }
            else if (result == -1 && errno == ECHILD)
            {
                // Process was already reaped or doesn't exist
                if (process_jobs[i].is_background)
                {
                    printf("%s with pid %d exited abnormally\n", process_jobs[i].command_name, process_jobs[i].pid);
                }
                remove_process_job(process_jobs[i].pid);
            }
            // For other errors, leave the process in tracking
        }
    }
}

// Backward compatibility functions
int add_background_job(pid_t pid, const char *command_name)
{
    return add_process_job(pid, command_name, true);
}

void remove_background_job(pid_t pid)
{
    remove_process_job(pid);
}

void check_background_jobs()
{
    update_process_states();
}

// activities_command is now defined in commands.c

// REMOVED: run_command function - it's already defined in commands.c

int is_home_path(char *path, char *home)
{
    int n = strlen(home);
    if (strncmp(path, home, n) == 0 && (path[n] == '/' || path[n] == '\0')) // '/' if the path is successor of home, '\0' if it's exactly home
    {
        return 1;
    }
    return 0;
}

char *format_path(char *path, char *home)
{
    if (is_home_path(path, home))
    {
        int suffix_len = strlen(path) - strlen(home);
        char *result = malloc(1 + suffix_len + 1);
        if (!result)
            return NULL;
        result[0] = '~';
        strcpy(result + 1, path + strlen(home)); // Copy the suffix
        return result;
    }
    else
    {
        return strdup(path); // Return a copy of the original path
    }
}

// Helper function to create a new Command
Command *create_command()
{
    Command *cmd = malloc(sizeof(Command));
    if (!cmd)
        return NULL;

    cmd->name = NULL;
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->input_file = NULL;
    cmd->input_files = NULL;
    cmd->input_file_count = 0;
    cmd->output_file = NULL;
    cmd->output_files = NULL;
    cmd->output_file_count = 0;
    cmd->append = false;
    cmd->background = false;
    cmd->pipe_next = NULL;
    cmd->next = NULL;

    return cmd;
}
// ############### LLM Generated Code Begins ##############
// Function to free a single Command struct
void free_command(Command *cmd)
{
    if (!cmd)
        return;

    if (cmd->argv)
    {
        for (int i = 0; i < cmd->argc; i++)
        {
            free(cmd->argv[i]);
        }
        free(cmd->argv);
    }

    free(cmd->input_file);
    if (cmd->input_files) {
        for (int i = 0; i < cmd->input_file_count; i++) {
            free(cmd->input_files[i]);
        }
        free(cmd->input_files);
    }
    free(cmd->output_file);
    if (cmd->output_files) {
        for (int i = 0; i < cmd->output_file_count; i++) {
            free(cmd->output_files[i]);
        }
        free(cmd->output_files);
    }
    free(cmd);
}

// Function to free a chain of Command structs
void free_command_chain(Command *cmd)
{
    while (cmd)
    {
        Command *next = cmd->next;

        // Free piped commands
        if (cmd->pipe_next)
        {
            free_command_chain(cmd->pipe_next);
        }

        free_command(cmd);
        cmd = next;
    }
}

// Parse an atomic command (no pipes, no semicolons)
Command *parse_atomic_command(char *input)
{
    if (!input || !*input)
        return NULL;

    Command *cmd = create_command();
    if (!cmd)
        return NULL;

    // Temporary storage for arguments
    char *args[64];
    int argc = 0;

    char *token = strtok(input, " \t");

    while (token && argc < 63)
    {
        // Check for input redirection
        if (token[0] == '<')
        {
            char *filename = NULL;
            if (strlen(token) > 1)
            {
                // <filename format
                filename = strdup(token + 1);
            }
            else
            {
                // < filename format
                token = strtok(NULL, " \t");
                if (token)
                {
                    filename = strdup(token);
                }
            }
            
            if (filename) {
                // Add to input_files array for validation
                cmd->input_files = realloc(cmd->input_files, (cmd->input_file_count + 1) * sizeof(char*));
                cmd->input_files[cmd->input_file_count] = filename;
                cmd->input_file_count++;
                
                // Update the final input file (last one wins)
                free(cmd->input_file);
                cmd->input_file = strdup(filename);
            }
        }
        // Check for output redirection
        else if (token[0] == '>')
        {
            char *filename = NULL;
            bool is_append = false;
            
            if (token[1] == '>')
            {
                // Append redirection
                is_append = true;
                if (strlen(token) > 2)
                {
                    // >>filename format
                    filename = strdup(token + 2);
                }
                else
                {
                    // >> filename format
                    token = strtok(NULL, " \t");
                    if (token)
                    {
                        filename = strdup(token);
                    }
                }
            }
            else
            {
                // Normal output redirection
                if (strlen(token) > 1)
                {
                    // >filename format
                    filename = strdup(token + 1);
                }
                else
                {
                    // > filename format
                    token = strtok(NULL, " \t");
                    if (token)
                    {
                        filename = strdup(token);
                    }
                }
            }
            
            if (filename) {
                // Add to output_files array for validation
                cmd->output_files = realloc(cmd->output_files, (cmd->output_file_count + 1) * sizeof(char*));
                cmd->output_files[cmd->output_file_count] = filename;
                cmd->output_file_count++;
                
                // Update the final output file and append flag (last one wins)
                free(cmd->output_file);
                cmd->output_file = strdup(filename);
                cmd->append = is_append;
            }
        }
        // Regular argument
        else
        {
            args[argc] = strdup(token);
            argc++;
        }

        token = strtok(NULL, " \t");
    }

    if (argc == 0)
    {
        free_command(cmd);
        return NULL;
    }

    // Allocate and copy arguments
    cmd->argv = malloc((argc + 1) * sizeof(char *));
    if (!cmd->argv)
    {
        for (int i = 0; i < argc; i++)
        {
            free(args[i]);
        }
        free_command(cmd);
        return NULL;
    }

    for (int i = 0; i < argc; i++)
    {
        cmd->argv[i] = args[i];
    }
    cmd->argv[argc] = NULL;
    cmd->argc = argc;
    cmd->name = strdup(cmd->argv[0]);

    return cmd;
}

// Parse a single command (may contain pipes)
Command *parse_single_command(char *input)
{
    if (!input || !*input)
        return NULL;

    // Check for trailing ampersand and remove it
    bool is_background = false;
    char *end = input + strlen(input) - 1;
    while (end >= input && isspace(*end))
        end--;
    if (end >= input && *end == '&')
    {
        is_background = true;
        *end = '\0';
        // Remove trailing whitespace before the &
        end--;
        while (end >= input && isspace(*end))
        {
            *end = '\0';
            end--;
        }
    }

    // Split by pipes
    Command *first_cmd = NULL;
    Command *current_cmd = NULL;

    char *pipe_start = input;
    char *ptr = input;

    while (*ptr)
    {
        if (*ptr == '|')
        {
            *ptr = '\0';

            Command *cmd = parse_atomic_command(pipe_start);
            if (cmd)
            {
                if (!first_cmd)
                {
                    first_cmd = current_cmd = cmd;
                }
                else
                {
                    current_cmd->pipe_next = cmd;
                    current_cmd = cmd;
                }
            }

            pipe_start = ptr + 1;
            // Skip whitespace
            while (*pipe_start && isspace(*pipe_start))
                pipe_start++;
        }
        ptr++;
    }

    // Parse the last pipe segment
    if (*pipe_start)
    {
        Command *cmd = parse_atomic_command(pipe_start);
        if (cmd)
        {
            if (!first_cmd)
            {
                first_cmd = cmd;
            }
            else
            {
                current_cmd->pipe_next = cmd;
            }
        }
    }

    // Set background flag for the first command in the chain
    if (is_background && first_cmd)
    {
        first_cmd->background = true;
    }

    return first_cmd;
}

// Parse input string into Command structs
Command *parse_input_to_commands(char *input)
{
    if (!input || !*input)
        return NULL;

    // Make a working copy of input
    char *input_copy = strdup(input);
    if (!input_copy)
        return NULL;

    Command *first_cmd = NULL;
    Command *current_cmd = NULL;

    // Split by semicolons and ampersands for sequential commands
    char *cmd_start = input_copy;
    char *ptr = input_copy;

    while (*ptr)
    {
        if (*ptr == ';')
        {
            *ptr = '\0';

            // Parse this command segment
            Command *cmd = parse_single_command(cmd_start);
            if (cmd)
            {
                if (!first_cmd)
                {
                    first_cmd = current_cmd = cmd;
                }
                else
                {
                    current_cmd->next = cmd;
                    current_cmd = cmd;
                }
            }

            cmd_start = ptr + 1;
            // Skip whitespace
            while (*cmd_start && isspace(*cmd_start))
                cmd_start++;
        }
        else if (*ptr == '&')
        {
            // Check if this is at the end or followed by whitespace/semicolon
            char *next = ptr + 1;
            while (*next && isspace(*next))
                next++;

            if (*next == '\0' || *next == ';')
            {
                // This is a background operator
                *ptr = '\0';

                Command *cmd = parse_single_command(cmd_start);
                if (cmd)
                {
                    cmd->background = true;

                    if (!first_cmd)
                    {
                        first_cmd = current_cmd = cmd;
                    }
                    else
                    {
                        current_cmd->next = cmd;
                        current_cmd = cmd;
                    }
                }

                cmd_start = next;
                if (*next == ';')
                {
                    cmd_start++;
                }
                // Skip whitespace
                while (*cmd_start && isspace(*cmd_start))
                    cmd_start++;

                ptr = next;
                if (*ptr == ';')
                    ptr++;
                continue;
            }
        }
        ptr++;
    }

    // Parse the last command segment
    if (*cmd_start)
    {
        Command *cmd = parse_single_command(cmd_start);
        if (cmd)
        {
            if (!first_cmd)
            {
                first_cmd = cmd;
            }
            else
            {
                current_cmd->next = cmd;
            }
        }
    }

    free(input_copy);
    return first_cmd;
}
// ############### LLM Generated Code Ends ################

// Validate input/output files before execution
int validate_command_files(Command *cmd)
{
    // Check ALL input files exist and are readable (validate in order)
    for (int i = 0; i < cmd->input_file_count; i++)
    {
        int fd = open(cmd->input_files[i], O_RDONLY);
        if (fd < 0)
        {
            printf("No such file or directory\n");
            return -1;
        }
        close(fd);
    }
    
    // Check ALL output files can be created/written (validate in order)
    for (int i = 0; i < cmd->output_file_count; i++)
    {
        int fd;
        // Use append flag from the last output file for consistency
        if (cmd->append && i == cmd->output_file_count - 1)
            fd = open(cmd->output_files[i], O_WRONLY | O_CREAT | O_APPEND, 0644);
        else
            fd = open(cmd->output_files[i], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            
        if (fd < 0)
        {
            printf("Unable to create file for writing\n");
            return -1;
        }
        close(fd);
    }

    return 0;
}

// Execute piped commands
int execute_piped_commands(Command *cmd)
{
    if (!cmd)
        return 1;
    if (!cmd->pipe_next)
        return run_command(cmd);

    int pipefd[2];
    pid_t pid1, pid2;
    int status = 0;

    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return 1;
    }

    // First command (writer)
    pid1 = fork();
    if (pid1 == -1)
    {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid1 == 0)
    {
        // Child process 1: writer
        setpgid(0, 0);  // Create new process group
        close(pipefd[0]);               // Close read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe
        close(pipefd[1]);

        // Handle input redirection for first command
        if (cmd->input_file)
        {
            freopen(cmd->input_file, "r", stdin);
        }

        // Execute the command
        if (strcmp(cmd->name, "hop") == 0)
        {
            exit(hop(cmd->argc, cmd->argv));
        }
        else if (strcmp(cmd->name, "reveal") == 0)
        {
            exit(reveal(cmd->argc, cmd->argv));
        }
        else if (strcmp(cmd->name, "log") == 0)
        {
            exit(log_command(cmd->argc, cmd->argv));
        }
        else
        {
            execvp(cmd->name, cmd->argv);
            printf("Command not found!\n");
            exit(1);
        }
    }

    // Second command (reader)
    pid2 = fork();
    if (pid2 == -1)
    {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid2 == 0)
    {
        // Child process 2: reader
        setpgid(0, pid1);  // Join the same process group as pid1
        close(pipefd[1]);              // Close write end
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin from pipe
        close(pipefd[0]);

        // Handle output redirection for second command
        if (cmd->pipe_next->output_file)
        {
            if (cmd->pipe_next->append)
            {
                freopen(cmd->pipe_next->output_file, "a", stdout);
            }
            else
            {
                freopen(cmd->pipe_next->output_file, "w", stdout);
            }
        }

        // If there are more pipes, recurse
        if (cmd->pipe_next->pipe_next)
        {
            exit(execute_piped_commands(cmd->pipe_next));
        }
        else
        {
            // Execute the final command
            if (strcmp(cmd->pipe_next->name, "hop") == 0)
            {
                exit(hop(cmd->pipe_next->argc, cmd->pipe_next->argv));
            }
            else if (strcmp(cmd->pipe_next->name, "reveal") == 0)
            {
                exit(reveal(cmd->pipe_next->argc, cmd->pipe_next->argv));
            }
            else if (strcmp(cmd->pipe_next->name, "log") == 0)
            {
                exit(log_command(cmd->pipe_next->argc, cmd->pipe_next->argv));
            }
            else
            {
                execvp(cmd->pipe_next->name, cmd->pipe_next->argv);
                printf("Command not found!\n");
                exit(1);
            }
        }
    }

    // Parent process
    close(pipefd[0]);
    close(pipefd[1]);

    // Set foreground process group to pid1's group
    foreground_pgid = pid1;

    // Add both processes to tracking (they are foreground processes in a pipe)
    add_process_job(pid1, cmd->name, false);
    add_process_job(pid2, cmd->pipe_next->name, false);

    // Wait for both children
    int status1;
    pid_t wait_result1, wait_result2;
    
    // Wait for first child
    do {
        wait_result1 = waitpid(pid1, &status1, WUNTRACED);
        if (wait_result1 == -1 && errno == EINTR) {
            if (kill(pid1, 0) == -1 && errno == ESRCH) {
                break; // Child no longer exists
            }
            continue;
        }
    } while (wait_result1 == -1 && errno == EINTR);
    
    // Wait for second child  
    do {
        wait_result2 = waitpid(pid2, &status, WUNTRACED);
        if (wait_result2 == -1 && errno == EINTR) {
            if (kill(pid2, 0) == -1 && errno == ESRCH) {
                break; // Child no longer exists
            }
            continue;
        }
    } while (wait_result2 == -1 && errno == EINTR);

    // Remove from tracking once completed
    remove_process_job(pid1);
    remove_process_job(pid2);

    // Clear foreground pgid
    foreground_pgid = 0;

    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// Execute command in background
void execute_background_command(Command *cmd)
{
    pid_t pid = fork();

    if (pid == -1)
    {
        perror("fork");
        return;
    }

    if (pid == 0)
    {
        // Child process - execute the command
        // Create new process group for background process
        setpgid(0, 0);

        // Background processes must not have access to terminal for input
        if (!cmd->input_file)
        {
            // Redirect stdin from /dev/null if no input file specified
            int dev_null = open("/dev/null", O_RDONLY);
            if (dev_null != -1)
            {
                dup2(dev_null, STDIN_FILENO);
                close(dev_null);
            }
        }
        else
        {
            // Handle input redirection
            freopen(cmd->input_file, "r", stdin);
        }

        // Handle output redirection
        if (cmd->output_file)
        {
            if (cmd->append)
            {
                freopen(cmd->output_file, "a", stdout);
            }
            else
            {
                freopen(cmd->output_file, "w", stdout);
            }
        }

        // Execute the command
        if (strcmp(cmd->name, "hop") == 0)
        {
            exit(hop(cmd->argc, cmd->argv));
        }
        else if (strcmp(cmd->name, "reveal") == 0)
        {
            exit(reveal(cmd->argc, cmd->argv));
        }
        else if (strcmp(cmd->name, "log") == 0)
        {
            exit(log_command(cmd->argc, cmd->argv));
        }
        else
        {
            execvp(cmd->name, cmd->argv);
            printf("Command not found!\n");
            exit(1);
        }
    }

    // Parent process - add to background job tracking and print job info
    // Reconstruct full command for background jobs
    char full_cmd[1024] = "";
    for (int i = 0; i < cmd->argc; i++) {
        if (i > 0) strcat(full_cmd, " ");
        strcat(full_cmd, cmd->argv[i]);
    }
    if (cmd->background) strcat(full_cmd, " &");
    
    int job_number = add_process_job_with_command(pid, cmd->name, full_cmd, true);
    if (job_number != -1)
    {
        printf("[%d] %d\n", job_number, pid);
    }
    else
    {
        printf("[Background] Process %d started\n", pid);
    }
}

// Execute a chain of commands
int execute_command_chain(Command *cmd)
{
    int last_status = 0;

    while (cmd)
    {
        // Validate files before execution
        if (validate_command_files(cmd) != 0)
        {
            cmd = cmd->next;
            continue;
        }

        if (cmd->pipe_next)
        {
            // Handle piped commands
            last_status = execute_piped_commands(cmd);
        }
        else
        {
            // Single command or background command
            if (cmd->background)
            {
                execute_background_command(cmd);
                last_status = 0; // Background commands don't affect status
            }
            else
            {
                last_status = run_command(cmd);
            }
        }

        cmd = cmd->next;
    }

    return last_status;
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

    // Install signal handlers for Ctrl-C and Ctrl-Z
    setup_signal_handlers();

    // Load persistent log on startup
    load_log();

    while (true)
    {
        // Reset signal flags
        sigint_received = 0;
        sigtstp_received = 0;
        
        // Check for completed background processes before parsing input
        check_background_jobs();

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
        
        // Clear any pending signals before reading input
        if (sigint_received || sigtstp_received) {
            sigint_received = 0;
            sigtstp_received = 0;
            printf("\n");
            continue;
        }
        
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            if (feof(stdin))
            {
                // EOF detected (Ctrl-D)
                printf("logout\n");
                // Kill all child processes before exiting
                for (int i = 0; i < MAX_PROCESSES; i++) {
                    if (process_jobs[i].active) {
                        kill(process_jobs[i].pid, SIGKILL);
                        waitpid(process_jobs[i].pid, NULL, 0); // Reap the child
                    }
                }
                exit(0); // Exit immediately with status 0
            }
            else if (errno == EINTR)
            {
                // Interrupted by signal (e.g., Ctrl-C). Clear error and continue.
                clearerr(stdin);
                printf("\n");
                continue;
            }
            else
            {
                // Other error; just continue
                clearerr(stdin);
                continue;
            }
        }

        input[strcspn(input, "\n")] = '\0';
        // if (strcmp(input, "exit") == 0)
        // {
        //     break;
        // }

        // Preserve the full command line before tokenization for logging
        char full_cmd[1024];
        strncpy(full_cmd, input, sizeof(full_cmd) - 1);
        full_cmd[sizeof(full_cmd) - 1] = '\0';

        if (validate_command(full_cmd))
        {
            // Parse input into Command structures
            Command *cmd_chain = parse_input_to_commands(full_cmd);

            if (cmd_chain)
            {
                // Always delegate skip rules to add_log (it will avoid storing 'log' atomics and duplicates)
                add_log(full_cmd);

                // Check for completed background jobs before command execution
                // This ensures completion messages appear before command output
                check_background_jobs();
                
                // Small delay to ensure background processes have time to complete
                usleep(10000); // 10ms delay
                check_background_jobs();
                
                // Execute the command chain
                execute_command_chain(cmd_chain);
                
                // Final check for any completed background jobs after command execution
                check_background_jobs();

                // Free the command chain
                free_command_chain(cmd_chain);
            }
        }
        else
        {
            printf("Invalid Syntax!\n");
        }
        free(display_path);
        // ############## LLM Generated Code Ends ################
    }

    free(home);
    return 0; // Exit with status 0 for normal termination
}
