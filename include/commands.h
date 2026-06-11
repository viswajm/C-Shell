#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <unistd.h>
#include "shell.h"  // Include shell.h to get struct Command definition

int hop(int argc, char **argv);
int reveal(int argc, char **argv);
int run_command(struct Command *cmd);
void set_shell_home(const char *home_path);
void add_log(const char *cmd);
int log_command(int argc, char **argv);
void load_log(void);
int activities_command(void);
int fg_command(int argc, char **argv);
int bg_command(int argc, char **argv);
int ping_command(int argc, char **argv);

#endif // COMMANDS_H
