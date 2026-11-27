# shell

[![CI](https://github.com/sujndoes/shell/actions/workflows/ci.yml/badge.svg)](https://github.com/sujndoes/shell/actions/workflows/ci.yml)

A small Unix shell written in C. It runs programs, supports pipelines of any length, combines pipes with redirection, expands variables, understands quoting, and runs commands in the background.

<!-- TODO: add a terminal GIF here (record with asciinema or vhs) -->

## Features

- Runs any program found on your `PATH`
- Built-in commands: `cd` (including `cd -`), `export`, `exit [code]`, `help`
- **Pipelines of any length**: `cat log.txt | grep error | sort | uniq -c`
- **Redirection**: `>` (overwrite), `>>` (append), `<` (input), usable together and combined with pipes: `sort < in.txt | uniq > out.txt`
- **Quoting and escapes**: `echo "two   spaces"`, `echo 'literal $HOME'`, `echo a\ b`
- **Expansion**: `$VAR`, `$?` (exit status of the last command), `~`
- Background execution with a trailing `&`; finished jobs are reported and cleaned up
- Ctrl+C stops the running command but not the shell; Ctrl+D exits
- Colored prompt showing the current directory
- Clear error messages (`shell: foo: No such file or directory`) and standard exit codes (127 not found, 126 not executable)

## Requirements

- A Unix-like system (Linux or macOS)
- `gcc` or any C compiler with POSIX support

## Build and run

```sh
git clone https://github.com/sujndoes/shell.git
cd shell
make
./shell
```

## Usage

```
DIR:/home/user>> ls -l | wc -l
DIR:/home/user>> cd projects
DIR:/home/user/projects>> cat notes.txt | grep todo | sort > todos.txt
DIR:/home/user/projects>> export NAME=world
DIR:/home/user/projects>> echo "hello $NAME" >> greeting.txt
DIR:/home/user/projects>> false
DIR:/home/user/projects>> echo $?
1
DIR:/home/user/projects>> sleep 30 &
[background] pid 4242
DIR:/home/user/projects>> exit
```

## How it works

Each line goes through three stages: **parse**, **execute**, **clean up**.

| Function | Role |
|---|---|
| `takeInput` | Prints the prompt, reads a line, rejects over-long input, handles Ctrl+C / Ctrl+D |
| `parseInput` / `readWord` | Tokenizes the line (quotes, escapes, `$VAR`, `~`) into a `Pipeline`: a list of `Command`s, each with its own `argv`, input file and output file |
| `builtInCommands` | Runs `cd`, `export`, `exit`, `help` inside the shell process (they must change the shell's own state) |
| `executePipeline` | Forks one child per command, connects neighbours with `pipe` + `dup2`, and waits for exactly those children with `waitpid` |
| `applyRedirect` | In the child: opens the file and puts it on stdin or stdout with `dup2` |
| `reapBackground` | Collects finished background jobs between commands |

### Design notes

- **Parse into a struct first, then execute.** Pipes and redirections are properties of each `Command`, so any combination works without special cases.
- **Foreground children are waited for by PID**, never with a blanket `wait()`. A finishing background job can therefore never be mistaken for the foreground command.
- **No `SIGCHLD` handler.** Background jobs are reaped between commands instead, which avoids racing with the foreground `waitpid`.
- **The Ctrl+C handler does nothing.** Its only job is to interrupt the blocked read so the shell redraws the prompt; it is trivially async-signal-safe. Children restore the default action, so Ctrl+C still kills them. Background jobs run in their own process group, so they survive it.
- **Every limit is bounds-checked** (line length, word length, argument count, pipeline length) and reports an error instead of overflowing a buffer.

## Testing

```sh
make test   # 25 end-to-end tests
make asan   # the same tests under AddressSanitizer + UBSan
```

The tests feed commands to the shell on stdin and compare the output. They cover pipelines, every redirection form, quoting, expansion, exit codes, `cd`, error handling, and recovery after syntax errors. CI runs both targets on every push.

## Limitations and roadmap

- [x] Pipelines of any length
- [x] Pipes and redirection in the same command, multiple redirections per command
- [x] Quotes, escapes, `$VAR`, `$?`, `~`
- [x] Automated tests, AddressSanitizer, CI
- [ ] `;`, `&&`, `||`
- [ ] Wildcard expansion (`*.c`)
- [ ] Command substitution `$(...)`
- [ ] Command history and line editing
- [ ] Job control (`jobs`, `fg`, `bg`, Ctrl+Z)
- [ ] File-descriptor redirection (`2>`, `2>&1`)
- [ ] Pipes and redirection on built-ins (`cd`, `export`, ... only work as plain commands)

Limits: 1023 characters per line, 63 arguments per command, 16 commands per pipeline.

## Contributing

Issues and pull requests are welcome. For larger changes, open an issue first to discuss the idea.
