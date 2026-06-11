#ifndef SHELL_H
#define SHELL_H

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>

typedef struct Command
{
    char *name;
    char **argv;
    int argc;
    char *input_file;        // Final input file to use
    char **input_files;      // Array of all input files for validation
    int input_file_count;
    char *output_file;       // Final output file to use  
    char **output_files;     // Array of all output files for validation
    int output_file_count;
    bool append;
    bool background;
    struct Command *pipe_next;
    struct Command *next; // For sequential commands (separated by ; or &)
} Command;

// Function declarations for Command operations
Command *parse_input_to_commands(char *input);
Command *parse_single_command(char *input);
Command *parse_atomic_command(char *input);
void free_command(Command *cmd);
void free_command_chain(Command *cmd);
int validate_command_files(Command *cmd);
int execute_command_chain(Command *cmd);
int execute_piped_commands(Command *cmd);
void execute_background_command(Command *cmd);

// Process management functions (for activities command)
int add_process_job(pid_t pid, const char *command_name, bool is_background);
void remove_process_job(pid_t pid);
void update_process_states(void);
int activities_command(void);

#define MAX_PROCESSES 100

// Process state enum
typedef enum {
    PROCESS_RUNNING,
    PROCESS_STOPPED,
    PROCESS_TERMINATED
} ProcessState;

// Process job structure
typedef struct ProcessJob {
    int job_number;
    pid_t pid;
    pid_t pgid;
    char *command_name;
    char *full_command;
    bool active;
    bool is_background;
    ProcessState state;
} ProcessJob;

// Process jobs array and helper functions
extern ProcessJob process_jobs[MAX_PROCESSES];
struct ProcessJob *get_job_by_number(int job_number);
struct ProcessJob *get_most_recent_job(void);

// Signal-related globals and helpers
extern pid_t foreground_pgid; // current foreground process group id
extern volatile sig_atomic_t sigint_received;  // SIGINT received flag
extern volatile sig_atomic_t sigtstp_received; // SIGTSTP received flag
void setup_signal_handlers(void);
void cleanup_all_processes(void);
int get_job_number_by_pid(pid_t pid);
const char *get_command_name_by_pid(pid_t pid);
void mark_job_stopped(pid_t pid);
int add_process_job_with_command(pid_t pid, const char *command_name, const char *full_command, bool is_background);

// Background job management functions (backward compatibility)
int add_background_job(pid_t pid, const char *command_name);
void remove_background_job(pid_t pid);
void check_background_jobs(void);

#endif // SHELL_H
