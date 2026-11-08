# shell

A small Unix shell written in C. It runs programs, supports a single pipe, handles input and output redirection, and can run commands in the background.

## Features

- Runs any program found on your `PATH`
- Built-in commands: `cd`, `exit`, `help`
- Pipes between two commands: `ls | wc -l`
- Redirection: `>` (overwrite), `>>` (append), `<` (input)
- Background execution with a trailing `&`
- Colored prompt that shows the current directory
- `Ctrl+C` redraws the prompt instead of closing the shell
- Finished background jobs are cleaned up automatically

## Requirements

- A Unix-like system (Linux or macOS)
- `gcc` or any C compiler with POSIX support

## Build and run

```bash
git clone https://github.com/sujndoes/shell.git
cd shell
gcc -Wall -o shell shell.c
./shell
```

Adjust `shell.c` to match the name of the source file in the repository.

## Usage

```
DIR:/home/user>> ls -l
DIR:/home/user>> cd projects
DIR:/home/user/projects>> cat notes.txt | grep todo
DIR:/home/user/projects>> echo hello > out.txt
DIR:/home/user/projects>> echo world >> out.txt
DIR:/home/user/projects>> sort < out.txt
DIR:/home/user/projects>> sleep 30 &
DIR:/home/user/projects>> exit
```

### Built-in commands

| Command | Description |
|---------|-------------|
| `cd [dir]` | Change directory. With no argument, goes to `$HOME`. |
| `exit` | Quit the shell. |
| `help` | Placeholder for now. |

## How it works

| Function | Role |
|----------|------|
| `takeInput` | Prints the prompt, reads a line, and detects a trailing `&` |
| `parseInput` | Splits a command line into arguments with `strtok` |
| `builtInCommands` | Handles `cd`, `exit`, and `help` in the shell process |
| `executeCommand` | Forks and runs a command with `execvp` |
| `parsePipe` / `executePiped` | Splits a line on `\|` and connects two children with `pipe` and `dup2` |
| `checkRedirect` / `handleRedirect` | Finds `>`, `>>`, or `<` and rewires stdout or stdin before `execvp` |
| `handleSigint` / `handleSigchld` | Redraws the prompt on `Ctrl+C` and reaps finished child processes |

## Limitations

- Only one pipe per line, so `a | b | c` is not supported
- Pipes and redirection cannot be combined in the same command
- One redirection per command
- Arguments are split on spaces only. Quotes, escapes, wildcards, and environment variable expansion are not handled.
- Background mode with `&` works for simple commands only
- Input is limited to 1024 characters and 64 arguments per command

## Contributing

Issues and pull requests are welcome. For larger changes, open an issue first to discuss the idea.
