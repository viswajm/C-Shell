# C-Shell

C-Shell is a custom interactive shell written in C. It provides a prompt, command parsing, built-in commands, pipelines, input and output redirection, background execution, and basic job control.

The shell starts in the current working directory and treats that directory as its home location for `hop ~`. It also stores command history in `logs.txt` so the log survives between runs.

## Features

- Interactive prompt with user, host, and path display
- Built-in navigation command `hop`
- Directory listing command `reveal`
- Command history with `log`
- Process tracking with `activities`
- Foreground and background job control with `fg` and `bg`
- Network utility command `ping`
- Sequential execution with `;`
- Background execution with `&`
- Pipelines with `|`
- Input redirection with `<`
- Output redirection with `>` and append redirection with `>>`

## Build

Use the provided Makefile to build the shell.

```bash
make
```

This produces the executable `shell.out` in the project root.

To remove the compiled binary, run:

```bash
make clean
```

## Run

After building, start the shell with:

```bash
./shell.out
```

You will see a prompt similar to:

```bash
<user@host:/current/path>
```

Type commands at the prompt and press Enter to execute them.

## Built-in Commands

### hop

Changes the current working directory.

Common forms include:

```bash
hop
hop <directory>
hop ~
hop .
hop ..
hop -
```

`hop -` switches back to the previous directory. The shell also remembers its startup directory as the home location for `hop ~`.

### reveal

Lists directory contents.

Supported flags:

- `-a` shows hidden files
- `-l` prints detailed information line by line

Examples:

```bash
reveal
reveal -a
reveal -l
reveal ~/projects
```

### log

Displays recent commands saved by the shell.

```bash
log
```

Other supported forms:

```bash
log purge
log execute <index>
```

`log purge` clears the saved history. `log execute <index>` runs a previous command from the stored history.

### activities

Shows tracked processes and their current state.

```bash
activities
```

### fg

Brings a stopped or background job into the foreground.

```bash
fg
fg <job_number>
```

### bg

Resumes a stopped job in the background.

```bash
bg
bg <job_number>
```

### ping

Runs a simple ping command.

```bash
ping <arguments>
```

### logout

Exits the shell after cleaning up tracked child processes.

```bash
logout
```

## Command Syntax

C-Shell accepts a small command language with the following operators:

- `;` runs commands sequentially
- `&` runs a command in the background
- `|` connects commands with a pipeline
- `<` reads input from a file
- `>` writes output to a file
- `>>` appends output to a file

Examples:

```bash
reveal ; log
sleep 5 &
cat input.txt | grep hello
sort < input.txt > output.txt
echo done >> output.txt
```

## Project Layout

```text
Makefile
include/
  commands.h
  parser.h
  shell.h
src/
  commands.c
  parser.c
  shell.c
```

`shell.c` contains the main loop and signal handling. `parser.c` validates and tokenizes shell input. `commands.c` implements the built-in commands and command execution logic.

## Notes

- The shell handles `Ctrl-C` and `Ctrl-Z` for foreground job control.
- Background process updates are checked before each prompt.
- Command history is written to `logs.txt` in the repository root.
- The build uses `gcc` with C99 and strict warning flags.
