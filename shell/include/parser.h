#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

// Returns true if the input string is a valid shell_cmd according to the CFG
bool validate_command(const char *input);

// Returns true if any atomic command's command name equals `name`.
bool contains_atomic_command_name(const char *input, const char *name);

#endif
