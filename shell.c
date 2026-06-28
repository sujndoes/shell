#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<signal.h>
#include<errno.h>

#define MAX_INPUT 1024 //max user input
#define MAX_ARGS 64   // max arguments 

int takeInput(char *input)
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
    fflush(stdout);
    printf("\033[0m"); // reset color 
    while(1)
    {
        if(fgets(input,MAX_INPUT,stdin) != NULL)
        {
            break;
        }
        if(errno == EINTR)
        {
            clearerr(stdin);
            continue;
        }
        exit(1);

    }
    

    input[strcspn(input,"\n")] = 0; // remove newline 

    if (input[strlen(input) - 1] == '&')
    {
        input[strlen(input) - 1] = '\0';
        return 1;
    }
    return 0;

}

void parseInput(char *input , char **args)
{
    char *token = strtok(input, " ");
    int argc = 0;
    while(token!=NULL)
    {
        args[argc++] = token;
        token = strtok(NULL," ");
    }
    args[argc] = NULL;
}

int builtInCommands(char **args) //for handling cd , exit ,help 
{
    if(args[0] == NULL)
    {
        return 1;
    }
    //exit command
    if(strcmp(args[0],"exit") == 0)
    {
        exit(0);
    }
    //for changing directory 
    if(strcmp(args[0],"cd") == 0)
    {
        if (args[1] == NULL)
        {
            chdir(getenv("HOME"));
        } else {
            if(chdir(args[1]) != 0)
            {
                perror("cd failed");
            }
        }
        return 1;
    }

    //for help command
    if(strcmp(args[0],"help") == 0)
    {
        printf("COMING SOON");
        return 1;
    }
    return 0;
}

//executing a non piped command
void executeCommand(char **args , int background)
{
    pid_t pid = fork();

    if(pid < 0)
    {
        perror("process failed");
        exit(1);
    }

    if(pid == 0)
    {
        execvp(args[0],args);
        perror("command not found!");
        exit(1);
    } else {
        if(background == 0){
            wait(NULL);
        }
    }
}

//executing piped command 
void executePiped(char **arg1 , char **arg2)
{
    int pipes[2];
    pipe(pipes);
    
    pid_t p1 = fork();
    
    if(p1 < 0)
    {
        perror("process failed");
        exit(1);
    }

    if(p1 == 0)
    {
        dup2(pipes[1] , STDOUT_FILENO);
        close(pipes[0]); //close read end
        close(pipes[1]);  
        execvp(arg1[0],arg1);
        perror("pipe command 1 failed");
        exit(1);
    }

    pid_t p2 = fork();

    if(p2 < 0)
    {
        perror("process failed");
        exit(1);
    }

    if(p2 == 0)
    {
        dup2(pipes[0],STDIN_FILENO);
        close(pipes[1]);
        close(pipes[0]);
        execvp(arg2[0],arg2);
        perror("pipe command 2 failed");
        exit(1);
    }

    close(pipes[0]);
    close(pipes[1]);

    wait(NULL);
    wait(NULL);
}

int parsePipe(char *input,char **left,char **right)
{
    char *pipeFound = strchr(input , '|');
    if(!pipeFound) 
    {
        return 1; // pipe doesnt exist // then we execute non piped 
    }
    *pipeFound = '\0';
    pipeFound++;

    while(*pipeFound == ' ') // skip front spaces in right
    {
        pipeFound++;
    }

    *left = input;
    *right = pipeFound;

    return 0; //pipe exists
}

int checkRedirect(char *input,char **left,char **right)
{
    char *append = strstr(input , ">>");
    char *output = NULL;

    if(!append)
    {
        output = strchr(input , '>');
    }

    char *inputR = strchr(input , '<');
    
    if(output)
    {
        *output = '\0';
        output++;
        while(*output == ' ')
        {
            output++;
        }
        *left = input;
        *right = output;
        return 1;
    } else if(inputR)
    {
        *inputR = '\0';
        inputR++;
        while(*inputR == ' ')
        {
            inputR++;
        }
        *left = input;
        *right = inputR;
        return 2;
    } else if(append)
    {
        *append = '\0';
        append += 2;
        while(*append == ' ')
        {
            append++;
        }
        *left = input;
        *right = append;
        return 3;
    } else 
    {
        return 0;
    }
}

void handleRedirect(char *command,char *filename,int type)
{
    int fileD;
    pid_t pid = fork();
    if(pid < 0)
    {
        perror("fork failed");
        exit(1);
    }
    if(pid == 0)
    {
        if(type == 1)
        {
            fileD = open(filename , O_CREAT | O_TRUNC | O_WRONLY , 0644);

            if(fileD < 0) { 
                perror("open failed");
                exit(1);
            }
            dup2(fileD , STDOUT_FILENO);
            close(fileD);
        }
        else if(type == 2)
        {
            fileD = open(filename, O_RDONLY , 0644);
            if(fileD < 0) { 
                perror("open failed");
                exit(1);
            }
            dup2(fileD , STDIN_FILENO);
            close(fileD);
        }
        else if(type == 3)
        {
            fileD = open(filename , O_APPEND | O_CREAT | O_WRONLY , 0644);
            if(fileD < 0) { 
                perror("open failed");
                exit(1);
            }
            dup2(fileD , STDOUT_FILENO);
            close(fileD);
        }
        else 
        {
            perror("unknown redirect type");
            exit(1);
        }

        char *args[MAX_ARGS];
        parseInput(command, args);
        execvp(args[0], args);
        perror("command failed");
        exit(1);
    }
    wait(NULL);
}

void handleSigint(int sig)
{
    printf("\033[1;32m"); 
    printf("\nDIR:%s" , getenv("PWD"));
    
    printf(">> ");
    printf("\033[0m"); 
    fflush(stdout);
    
}

void handleSigchld(int sig)
{
    // reap all dead children without blocking
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

int main()
{
    char input[MAX_INPUT];
    printf("\t\t\tWELCOME TO MY SHELL!!!!\n");
    signal(SIGINT , handleSigint);
    signal(SIGCHLD , handleSigchld);
    while(1)
    {
        
        int bg = takeInput(input);
        if(strlen(input) == 0)
        {
            continue;
        }
        char *left , *right;
        char *arg1[MAX_ARGS] , *arg2[MAX_ARGS];
        char *args[MAX_ARGS];

        if(parsePipe(input,&left,&right) == 0)
        {
            parseInput(left , arg1);
            parseInput(right,arg2);

            executePiped(arg1,arg2);
        } else{
            int rd = checkRedirect(input, &left , &right);
            if(rd)
            {
                handleRedirect(left,right,rd);
                continue;
            }

            parseInput(input, args);

            if(builtInCommands(args) == 1)
            {
                continue;
            }
            
            executeCommand(args,bg);
        }
    }
    return 0;
}