// reveal command implementation
#include "../include/commands.h"
#include "../include/parser.h"
#include <dirent.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>  // For nanosleep
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <signal.h>

#define LOG_MAX 15
#define LOG_FILE "logs.txt"

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
        
        // Check if we're in a pipe context by checking if stdout is not a terminal
        // or if we're being called recursively (avoid infinite loops)
        static int executing_log = 0;
        if (executing_log || !isatty(STDOUT_FILENO)) {
            // We're in a pipe or recursive call, just output the command text
            printf("%s", log_buffer[log_idx]);
            return 0;
        }
        
        executing_log = 1;  // Mark that we're executing a log command
        
        // Parse and execute the command using the new Command struct system
        int result = 1;
        if (validate_command(log_buffer[log_idx]))
        {
            Command *cmd_chain = parse_input_to_commands(log_buffer[log_idx]);
            if (cmd_chain)
            {
                execute_command_chain(cmd_chain);
                free_command_chain(cmd_chain);
                result = 0;
            }
        }
        else
        {
            printf("Invalid Syntax!\n");
        }
        
        executing_log = 0;  // Reset the flag
        return result;
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
    if (dir_args == 0)
    {
        // No arguments - show current directory
        strncpy(target, cwd, sizeof(target));
    }
    else if (dir_args == 1 && strcmp(argv[arg_start], "~") == 0)
    {
        // Explicit ~ argument - show home directory
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

// External declarations needed for activities_command
#define MAX_PROCESSES 100
extern struct ProcessJob process_jobs[MAX_PROCESSES];

int activities_command(void)
{
    // Update process states first
    update_process_states();

    // Collect active processes
    typedef struct {
        pid_t pid;
        char *command_name;
        ProcessState state;
    } ProcessInfo;

    ProcessInfo *processes = malloc(MAX_PROCESSES * sizeof(ProcessInfo));
    int process_count = 0;

    // First collect active processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_jobs[i].active &&
            (process_jobs[i].state == PROCESS_RUNNING || 
             process_jobs[i].state == PROCESS_STOPPED)) {
            processes[process_count].pid = process_jobs[i].pid;
            processes[process_count].command_name = strdup(process_jobs[i].command_name);
            processes[process_count].state = process_jobs[i].state;
            process_count++;
        }
    }

    // Sort processes by command name
    for (int i = 0; i < process_count - 1; i++) {
        for (int j = 0; j < process_count - i - 1; j++) {
            if (strcmp(processes[j].command_name, processes[j + 1].command_name) > 0) {
                ProcessInfo temp = processes[j];
                processes[j] = processes[j + 1];
                processes[j + 1] = temp;
            }
        }
    }

    // Print sorted processes
    for (int i = 0; i < process_count; i++) {
        printf("%s - %s\n", 
               processes[i].command_name,
               processes[i].state == PROCESS_RUNNING ? "Running" : "Stopped");
        free(processes[i].command_name);
    }

    free(processes);
    return 0;
}

int ping_command(int argc, char **argv)
{
    if (argc != 3) {
        printf("ping: Invalid Syntax!\n");
        return 1;
    }

    // Parse pid
    char *endptr;
    pid_t pid = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0') {
        printf("ping: Invalid Syntax!\n");
        return 1;
    }

    // Parse signal number
    int signal_num = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0') {
        printf("ping: Invalid Syntax!\n");
        return 1;
    }

    // Take modulo 32 of signal number
    signal_num = signal_num % 32;

    // Try to send the signal
    if (kill(pid, signal_num) == -1) {
        if (errno == ESRCH) {
            printf("No such process found\n");
        } else {
            perror("kill");
        }
        return 1;
    }

    printf("Sent signal %d to process with pid %d\n", signal_num, pid);
    return 0;
}

int fg_command(int argc, char **argv)
{
    update_process_states(); // Update job states first

    struct ProcessJob *job = NULL;

    if (argc == 1)
    {
        // No job number provided - use most recent
        job = get_most_recent_job();
        if (!job)
        {
            printf("No such job\n");
            return 1;
        }
    }
    else if (argc == 2)
    {
        // Job number provided
        int job_num = atoi(argv[1]);
        job = get_job_by_number(job_num);
        if (!job)
        {
            printf("No such job\n");
            return 1;
        }
    }
    else
    {
        printf("fg: Invalid Syntax!\n");
        return 1;
    }

    // Print the full command when bringing to foreground
    printf("%s\n", job->full_command);

    // Set as foreground process
    extern pid_t foreground_pgid;
    foreground_pgid = job->pgid; // Use process group ID, not process ID
    job->is_background = false;

    // If stopped, send SIGCONT to resume
    if (job->state == PROCESS_STOPPED)
    {
        kill(-job->pgid, SIGCONT);  // Send to process group
        job->state = PROCESS_RUNNING;
    }

    // Wait for the job to complete or stop again
    int status;
    pid_t wait_result;
    int check_count = 0;
    
    do
    {
        // Use WNOHANG to avoid blocking and check for signals periodically
        wait_result = waitpid(job->pid, &status, WUNTRACED | WNOHANG);
        
        if (wait_result == 0) {
            // No status change yet, check for signals every few iterations
            if (++check_count % 100 == 0) {
                extern volatile sig_atomic_t sigint_received;
                extern volatile sig_atomic_t sigtstp_received;
                if (sigint_received || sigtstp_received) {
                    // Check if process still exists
                    if (kill(job->pid, 0) == -1 && errno == ESRCH) {
                        // Process no longer exists, was killed by signal
                        remove_process_job(job->pid);
                        foreground_pgid = 0;
                        sigint_received = 0;
                        sigtstp_received = 0;
                        return 0;
                    }
                }
            }
            // Small sleep to avoid busy waiting
            struct timespec ts = {0, 1000000}; // 1ms
            nanosleep(&ts, NULL);
            continue;
        }
        
        if (wait_result == -1)
        {
            if (errno == EINTR)
            {
                // waitpid was interrupted by a signal, check if child still exists
                if (kill(job->pid, 0) == -1 && errno == ESRCH)
                {
                    // Child no longer exists, it was terminated
                    remove_process_job(job->pid);
                    foreground_pgid = 0;
                    return 0;
                }
                // Child still exists, continue waiting
                continue;
            }
            else
            {
                // Other waitpid error
                perror("waitpid");
                remove_process_job(job->pid);
                foreground_pgid = 0;
                return 1;
            }
        }
        break; // Successful waitpid, exit the loop
    } while (1);

    if (wait_result > 0)
    {
        if (WIFSTOPPED(status))
        {
            // Process stopped again - move back to background
            job->state = PROCESS_STOPPED;
            job->is_background = true;
            printf("[%d] Stopped %s\n", job->job_number, job->command_name);
        }
        else if (WIFSIGNALED(status))
        {
            // Process was terminated by signal
            remove_process_job(job->pid);
        }
        else
        {
            // Process completed normally - remove from tracking
            remove_process_job(job->pid);
        }
    }

    // Clear foreground pgid
    foreground_pgid = 0;

    return 0;
}

int bg_command(int argc, char **argv)
{
    update_process_states(); // Update job states first

    struct ProcessJob *job = NULL;

    if (argc == 1)
    {
        // No job number provided - use most recent
        job = get_most_recent_job();
        if (!job)
        {
            printf("No such job\n");
            return 1;
        }
    }
    else if (argc == 2)
    {
        // Job number provided
        int job_num = atoi(argv[1]);
        job = get_job_by_number(job_num);
        if (!job)
        {
            printf("No such job\n");
            return 1;
        }
    }
    else
    {
        printf("bg: Invalid Syntax!\n");
        return 1;
    }

    if (job->state == PROCESS_RUNNING)
    {
        printf("Job already running\n");
        return 1;
    }

    if (job->state == PROCESS_STOPPED)
    {
        // Resume the stopped job in background
        kill(-job->pgid, SIGCONT);  // Send to process group
        job->state = PROCESS_RUNNING;
        job->is_background = true;

        printf("[%d] %s &\n", job->job_number, job->command_name);
        return 0;
    }

    printf("No such job\n");
    return 1;
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
    {
        // Handle output redirection for reveal using POSIX functions
        int original_stdout = -1;
        int output_fd = -1;

        if (cmd->output_file)
        {
            // Save original stdout
            original_stdout = dup(STDOUT_FILENO);
            if (original_stdout == -1)
            {
                perror("dup");
                return 1;
            }

            // Open output file
            if (cmd->append)
                output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            else
                output_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (output_fd == -1)
            {
                perror("open");
                close(original_stdout);
                return 1;
            }

            // Redirect stdout to file
            if (dup2(output_fd, STDOUT_FILENO) == -1)
            {
                perror("dup2");
                close(output_fd);
                close(original_stdout);
                return 1;
            }

            close(output_fd); // Close the file descriptor as it's duplicated
        }

        int result = reveal(cmd->argc, cmd->argv);

        // Restore original stdout if redirected
        if (original_stdout != -1)
        {
            if (dup2(original_stdout, STDOUT_FILENO) == -1)
            {
                perror("dup2 restore");
            }
            close(original_stdout);
        }

        return result;
    }
    else if (strcmp(cmd->name, "log") == 0)
        return log_command(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "activities") == 0)
        return activities_command();
    else if (strcmp(cmd->name, "fg") == 0)
        return fg_command(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "bg") == 0)
        return bg_command(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "ping") == 0)
        return ping_command(cmd->argc, cmd->argv);
    else if (strcmp(cmd->name, "logout") == 0)
    {
        printf("logout\n");
        extern void cleanup_all_processes(void);
        cleanup_all_processes();
        exit(0);
    }
    else
    {
        // Validate input file existence before forking
        if (cmd->input_file && access(cmd->input_file, R_OK) != 0)
        {
            printf("No such file or directory\n");
            return 1;
        }
        
        // Validate output file can be created/written before forking
        if (cmd->output_file)
        {
            int test_fd;
            if (cmd->append)
                test_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
            else
                test_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                
            if (test_fd == -1)
            {
                printf("Unable to create file for writing\n");
                return 1;
            }
            close(test_fd);
        }

        // For other commands, fork and exec
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            return 1;
        }
        else if (pid == 0)
        {
            // Child process: create a new process group and set it as its own
            setpgid(0, 0);

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
            // Parent process: set foreground pgid and track
            extern pid_t foreground_pgid;
            foreground_pgid = pid; // child's pgid equals pid

            // Add process to tracking (foreground process)
            // Reconstruct full command
            char full_cmd[1024] = "";
            for (int i = 0; i < cmd->argc; i++)
            {
                if (i > 0)
                    strcat(full_cmd, " ");
                strcat(full_cmd, cmd->argv[i]);
            }
            add_process_job_with_command(pid, cmd->name, full_cmd, false);
            
            // Set the pgid in the parent as well to avoid race conditions
            setpgid(pid, pid);

            int status;
            pid_t wait_result;
            // Wait for child to finish or stop
            do
            {
                wait_result = waitpid(pid, &status, WUNTRACED);
                if (wait_result == -1)
                {
                    if (errno == EINTR)
                    {
                        // waitpid was interrupted by signal - check if child is still alive
                        if (kill(pid, 0) == -1 && errno == ESRCH)
                        {
                            // Child no longer exists, it was terminated
                            remove_process_job(pid);
                            foreground_pgid = 0;
                            return 0;  // Return normally when child is killed by signal
                        }
                        // Child still exists, continue waiting
                        continue;
                    }
                    else
                    {
                        // Other waitpid error
                        perror("waitpid");
                        remove_process_job(pid);
                        foreground_pgid = 0;
                        return 1;
                    }
                }
                break; // Successful waitpid, exit the loop
            } while (1);

            // Handle the child process status
            if (wait_result > 0)
            {
                if (WIFSTOPPED(status))
                {
                    // Child was stopped (Ctrl+Z)
                    mark_job_stopped(pid);

                    // Print required message: [job_number] Stopped command_name
                    int job_no = get_job_number_by_pid(pid);
                    const char *cmd_name = get_command_name_by_pid(pid);
                    if (job_no != -1 && cmd_name)
                    {
                        printf("[%d] Stopped %s\n", job_no, cmd_name);
                    }
                }
                else if (WIFSIGNALED(status))
                {
                    // Child was terminated by signal (e.g., Ctrl+C)
                    remove_process_job(pid);
                    // Don't print anything for SIGINT as per shell convention
                }
                else
                {
                    // Child exited normally
                    remove_process_job(pid);
                }
            }

            // Clear foreground pgid
            foreground_pgid = 0;

            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
    }
}
