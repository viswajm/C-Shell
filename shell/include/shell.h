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

typedef struct Command {
    char *name;
    char **argv;
    int argc;
    char *input_file;
    char *output_file;
    bool append;
    struct Command *pipe_next;
} Command;

#endif // SHELL_H