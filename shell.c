#define _POSIX_C_SOURCE 200809L
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<signal.h>
#include<errno.h>
#include<ctype.h>

#define MAX_INPUT 1024 //max user input
#define MAX_ARGS 64   // max arguments 
#define MAX_CMDS 16   // max commands in one pipeline

//one command of a pipeline
typedef struct
{
    char *args[MAX_ARGS];
    int argc;
    char *inFile;   // for <
    char *outFile;  // for > and >>
    int outType;    // 1 = > , 3 = >>
} Command;

typedef struct
{
    Command cmds[MAX_CMDS];
    int count;
    int background;
} Pipeline;

//a word that is being built (quotes and $VAR can make it longer than the input)
typedef struct
{
    char data[MAX_INPUT];
    int len;
    int overflow;
} Word;

int lastStatus = 0; // exit status of last command , for $?
int interactive = 0; // 1 if stdin is a terminal

void printPrompt()
{
    char cwd[MAX_INPUT];

    if (getcwd(cwd,sizeof(cwd)) == NULL)
    {
        perror("getcwd failed");
        exit(1);
    }
    printf("\033[1;32m"); // change color to green 
    printf("\nDIR:%s" , cwd);
    
    printf(">> ");
    printf("\033[0m"); // reset color 
    fflush(stdout);
}

void takeInput(char *input)
{
    if(interactive)
    {
        printPrompt();
    }
    while(1)
    {
        if(fgets(input,MAX_INPUT,stdin) != NULL)
        {
            break;
        }
        if(ferror(stdin) && errno == EINTR) // ctrl+c , draw the prompt again
        {
            clearerr(stdin);
            if(interactive)
            {
                printPrompt();
            }
            continue;
        }
        if(interactive)
        {
            printf("exit\n");
        }
        exit(lastStatus); // ctrl+d

    }

    if(strchr(input,'\n') == NULL && !feof(stdin)) // line was too long , throw away the rest
    {
        int ch;
        while((ch = getchar()) != '\n' && ch != EOF);
        fprintf(stderr,"shell: input too long (max %d characters)\n",MAX_INPUT - 1);
        input[0] = '\0';
    }

    input[strcspn(input,"\n")] = 0; // remove newline 
}

// ---------- parsing ----------

void addChar(Word *w , char c)
{
    if(w->len < MAX_INPUT - 1)
    {
        w->data[w->len++] = c;
    } else {
        w->overflow = 1;
    }
}

void addString(Word *w , char *s)
{
    while(*s)
    {
        addChar(w,*s++);
    }
}

int isMeta(char c) // chars that end a word
{
    return c != '\0' && strchr("|<>&",c) != NULL;
}

int isWordEnd(char c)
{
    return c == '\0' || isspace((unsigned char)c) || isMeta(c);
}

// *s is right after a $ , adds the value to the word and moves *s
void expandVar(char **s , Word *w)
{
    char *p = *s;

    if(*p == '?')
    {
        char num[16];
        snprintf(num,sizeof(num),"%d",lastStatus);
        addString(w,num);
        *s = p + 1;
    }
    else if(isalpha((unsigned char)*p) || *p == '_')
    {
        char name[128];
        int n = 0;
        while(isalnum((unsigned char)*p) || *p == '_')
        {
            if(n < (int)sizeof(name) - 1)
            {
                name[n++] = *p;
            }
            p++;
        }
        name[n] = '\0';
        char *val = getenv(name);
        if(val)
        {
            addString(w,val);
        }
        *s = p;
    } else {
        addChar(w,'$'); // lone $ stays as it is
    }
}

// reads one word , handles quotes \ $ and ~ . returns NULL on error
char *readWord(char **sp)
{
    char *s = *sp;
    char *start = s;
    Word w;
    w.len = 0;
    w.overflow = 0;

    while(!isWordEnd(*s))
    {
        if(*s == '\'') // single quotes , everything stays as it is
        {
            s++;
            while(*s && *s != '\'')
            {
                addChar(&w,*s++);
            }
            if(*s == '\0')
            {
                fprintf(stderr,"shell: unterminated quote\n");
                return NULL;
            }
            s++;
        }
        else if(*s == '"') // double quotes , only $ and \ are special
        {
            s++;
            while(*s && *s != '"')
            {
                if(*s == '\\' && (s[1] == '"' || s[1] == '\\' || s[1] == '$'))
                {
                    addChar(&w,s[1]);
                    s += 2;
                }
                else if(*s == '$')
                {
                    s++;
                    expandVar(&s,&w);
                } else {
                    addChar(&w,*s++);
                }
            }
            if(*s == '\0')
            {
                fprintf(stderr,"shell: unterminated quote\n");
                return NULL;
            }
            s++;
        }
        else if(*s == '\\') // next char is taken as it is
        {
            s++;
            if(*s)
            {
                addChar(&w,*s++);
            }
        }
        else if(*s == '$')
        {
            s++;
            expandVar(&s,&w);
        }
        else if(*s == '~' && s == start && isWordEnd(s[1] == '/' ? '\0' : s[1]))
        {
            char *home = getenv("HOME");
            if(home)
            {
                addString(&w,home);
            } else {
                addChar(&w,'~');
            }
            s++;
        } else {
            addChar(&w,*s++);
        }
    }

    if(w.overflow)
    {
        fprintf(stderr,"shell: word too long (max %d characters)\n",MAX_INPUT - 1);
        return NULL;
    }
    w.data[w.len] = '\0';
    *sp = s;
    return strdup(w.data);
}

void freePipeline(Pipeline *p)
{
    for(int i = 0 ; i < p->count ; i++)
    {
        for(int j = 0 ; j < p->cmds[i].argc ; j++)
        {
            free(p->cmds[i].args[j]);
        }
        free(p->cmds[i].inFile);
        free(p->cmds[i].outFile);
    }
    p->count = 0;
}

// msg is NULL if the error was already printed
int parseFail(Pipeline *p , char *msg)
{
    if(msg)
    {
        fprintf(stderr,"shell: syntax error: %s\n",msg);
    }
    freePipeline(p);
    return -1;
}

// splits the line into commands for | , each one with its own args and redirects
// returns 0 ok , 1 empty line , -1 error
int parseInput(char *input , Pipeline *p)
{
    memset(p,0,sizeof(*p));
    p->count = 1;
    Command *c = &p->cmds[0];
    int pending = 0; // waiting for a filename after < > >>

    while(1)
    {
        while(isspace((unsigned char)*input)) // skip spaces
        {
            input++;
        }
        if(*input == '\0')
        {
            break;
        }

        if(*input == '|')
        {
            if(c->argc == 0 || pending)
            {
                return parseFail(p,"unexpected '|'");
            }
            if(p->count == MAX_CMDS)
            {
                return parseFail(p,"too many commands in pipeline");
            }
            c = &p->cmds[p->count++];
            input++;
        }
        else if(*input == '<' || *input == '>')
        {
            if(pending)
            {
                return parseFail(p,"missing filename after redirection");
            }
            if(*input == '<')
            {
                pending = 2;
            }
            else if(input[1] == '>')
            {
                pending = 3;
                input++;
            } else {
                pending = 1;
            }
            input++;
        }
        else if(*input == '&')
        {
            input++;
            while(isspace((unsigned char)*input))
            {
                input++;
            }
            if(*input != '\0')
            {
                return parseFail(p,"'&' must be at the end of the line");
            }
            p->background = 1;
            break;
        } else {
            char *word = readWord(&input);
            if(word == NULL)
            {
                return parseFail(p,NULL);
            }
            if(pending == 2)
            {
                free(c->inFile);
                c->inFile = word;
                pending = 0;
            }
            else if(pending)
            {
                free(c->outFile);
                c->outFile = word;
                c->outType = pending;
                pending = 0;
            } else {
                if(c->argc >= MAX_ARGS - 1) // last slot is for NULL
                {
                    free(word);
                    return parseFail(p,"too many arguments");
                }
                c->args[c->argc++] = word;
                c->args[c->argc] = NULL;
            }
        }
    }

    if(pending)
    {
        return parseFail(p,"missing filename after redirection");
    }
    if(p->count == 1 && c->argc == 0 && !c->inFile && !c->outFile && !p->background)
    {
        freePipeline(p);
        return 1; // empty line
    }
    for(int i = 0 ; i < p->count ; i++)
    {
        if(p->cmds[i].argc == 0)
        {
            return parseFail(p,"missing command");
        }
    }
    return 0;
}

// ---------- running ----------

int builtInCommands(char **args) //for handling cd , exit ,help , export
{
    if(args[0] == NULL)
    {
        return 1;
    }
    //exit command
    if(strcmp(args[0],"exit") == 0)
    {
        exit(args[1] ? atoi(args[1]) : lastStatus);
    }
    //for changing directory 
    if(strcmp(args[0],"cd") == 0)
    {
        char old[MAX_INPUT];
        char cwd[MAX_INPUT];
        char *target = args[1];

        if(getcwd(old,sizeof(old)) == NULL)
        {
            old[0] = '\0';
        }
        if (target == NULL)
        {
            target = getenv("HOME");
            if(target == NULL)
            {
                fprintf(stderr,"cd: HOME not set\n");
                lastStatus = 1;
                return 1;
            }
        }
        else if(strcmp(target,"-") == 0) // go back to the old dir
        {
            target = getenv("OLDPWD");
            if(target == NULL)
            {
                fprintf(stderr,"cd: OLDPWD not set\n");
                lastStatus = 1;
                return 1;
            }
            printf("%s\n",target);
        }

        if(chdir(target) != 0)
        {
            perror("cd failed");
            lastStatus = 1;
        } else {
            if(old[0])
            {
                setenv("OLDPWD",old,1);
            }
            if(getcwd(cwd,sizeof(cwd)))
            {
                setenv("PWD",cwd,1);
            }
            lastStatus = 0;
        }
        return 1;
    }

    //for setting env variables , export NAME=VALUE
    if(strcmp(args[0],"export") == 0)
    {
        lastStatus = 0;
        if(args[1] == NULL)
        {
            fprintf(stderr,"usage: export NAME=VALUE\n");
            lastStatus = 1;
        }
        for(int i = 1 ; args[i] != NULL ; i++)
        {
            char *eq = strchr(args[i],'=');
            if(eq == NULL || eq == args[i])
            {
                fprintf(stderr,"export: '%s': expected NAME=VALUE\n",args[i]);
                lastStatus = 1;
                continue;
            }
            *eq = '\0';
            if(setenv(args[i],eq + 1,1) != 0)
            {
                perror("export failed");
                lastStatus = 1;
            }
        }
        return 1;
    }

    //for help command
    if(strcmp(args[0],"help") == 0)
    {
        printf("built in commands:\n"
               "  cd [dir]           change directory (cd - goes back)\n"
               "  export NAME=VALUE  set an environment variable\n"
               "  exit [code]        quit the shell\n"
               "  help               show this\n"
               "\n"
               "a | b | c           pipes\n"
               "< file  > file  >> file    redirection\n"
               "cmd &               run in background\n"
               "'text' \"text\" \\c     quotes\n"
               "$VAR  $?  ~         expansion\n");
        lastStatus = 0;
        return 1;
    }
    return 0;
}

// opens the file and puts it on stdin or stdout , runs inside the child
void applyRedirect(char *filename,int type)
{
    int fileD;

    if(type == 1)
    {
        fileD = open(filename , O_CREAT | O_TRUNC | O_WRONLY , 0644);

        if(fileD < 0) { 
            perror(filename);
            exit(1);
        }
        dup2(fileD , STDOUT_FILENO);
        close(fileD);
    }
    else if(type == 2)
    {
        fileD = open(filename, O_RDONLY , 0644);
        if(fileD < 0) { 
            perror(filename);
            exit(1);
        }
        dup2(fileD , STDIN_FILENO);
        close(fileD);
    }
    else if(type == 3)
    {
        fileD = open(filename , O_APPEND | O_CREAT | O_WRONLY , 0644);
        if(fileD < 0) { 
            perror(filename);
            exit(1);
        }
        dup2(fileD , STDOUT_FILENO);
        close(fileD);
    }
}

//executing a pipeline , one command or many
void executePipeline(Pipeline *p)
{
    pid_t pids[MAX_CMDS];
    int started = 0;
    int prevRead = -1; // read end of the previous pipe
    pid_t pgid = 0;    // process group for bg jobs

    fflush(stdout);

    for(int i = 0 ; i < p->count ; i++)
    {
        int pipes[2] = {-1 , -1};

        if(i < p->count - 1 && pipe(pipes) < 0)
        {
            perror("pipe failed");
            break;
        }

        pid_t pid = fork();

        if(pid < 0)
        {
            perror("process failed");
            if(pipes[0] >= 0) close(pipes[0]);
            if(pipes[1] >= 0) close(pipes[1]);
            break;
        }

        if(pid == 0)
        {
            signal(SIGINT , SIG_DFL);
            if(p->background)
            {
                setpgid(0 , pgid); // so ctrl+c doesnt kill bg jobs
            }
            if(prevRead >= 0)
            {
                dup2(prevRead , STDIN_FILENO);
                close(prevRead);
            }
            if(pipes[1] >= 0)
            {
                dup2(pipes[1] , STDOUT_FILENO);
                close(pipes[0]); //close read end
                close(pipes[1]);
            }

            Command *c = &p->cmds[i];
            if(c->inFile)
            {
                applyRedirect(c->inFile,2);
            }
            if(c->outFile)
            {
                applyRedirect(c->outFile,c->outType);
            }
            execvp(c->args[0],c->args);
            int err = errno;
            fprintf(stderr,"shell: %s: %s\n",c->args[0],strerror(err));
            _exit(err == ENOENT ? 127 : 126);
        }

        if(p->background && i == 0)
        {
            pgid = pid;
        }
        if(prevRead >= 0)
        {
            close(prevRead);
        }
        if(pipes[1] >= 0)
        {
            close(pipes[1]);
        }
        prevRead = pipes[0];
        pids[started++] = pid;
    }

    if(prevRead >= 0)
    {
        close(prevRead);
    }

    if(p->background)
    {
        if(started > 0 && interactive)
        {
            printf("[background] pid %d\n",(int)pids[started - 1]);
        }
        lastStatus = 0;
        return;
    }

    // wait for our own children only , wait(NULL) can return for a bg job
    for(int i = 0 ; i < started ; i++)
    {
        int status = 0;
        while(waitpid(pids[i],&status,0) < 0 && errno == EINTR);

        if(i == started - 1)
        {
            if(WIFEXITED(status))
            {
                lastStatus = WEXITSTATUS(status);
            }
            else if(WIFSIGNALED(status))
            {
                lastStatus = 128 + WTERMSIG(status);
                if(interactive && WTERMSIG(status) != SIGINT)
                {
                    fprintf(stderr,"%s\n",strsignal(WTERMSIG(status)));
                }
            }
        }
    }
    if(started < p->count)
    {
        lastStatus = 1;
    }
}

// only here to break fgets , takeInput draws the prompt again
void handleSigint(int sig)
{
    (void)sig;
}

// reap finished bg jobs , called between commands so it cant steal a foreground child
void reapBackground()
{
    int status;
    pid_t pid;

    while((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if(interactive)
        {
            printf("[done] pid %d\n",(int)pid);
        }
    }
}

int main()
{
    char input[MAX_INPUT];
    interactive = isatty(STDIN_FILENO);
    if(interactive)
    {
        printf("\t\t\tWELCOME TO MY SHELL!!!!\n");
    }

    struct sigaction sa;
    memset(&sa,0,sizeof(sa));
    sa.sa_handler = handleSigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART , we want fgets to stop on ctrl+c
    sigaction(SIGINT , &sa , NULL);

    while(1)
    {
        reapBackground();
        takeInput(input);

        Pipeline p;
        int rc = parseInput(input,&p);
        if(rc == 1)
        {
            continue;
        }
        if(rc < 0)
        {
            lastStatus = 2;
            continue;
        }

        Command *first = &p.cmds[0];
        if(p.count == 1 && !p.background && !first->inFile && !first->outFile
           && builtInCommands(first->args) == 1)
        {
            freePipeline(&p);
            fflush(stdout);
            continue;
        }

        executePipeline(&p);
        freePipeline(&p);
        fflush(stdout);
    }
    return 0;
}
