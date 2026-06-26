#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>

#define MAX_INPUT 1024 //max user input
#define MAX_ARGS 64   // max arguments 

void takeInput(char *input)
{
    char cwd[MAX_INPUT];

    if (getcwd(cwd,sizeof(cwd)) == NULL)
    {
        perror("getcwd failed");
        exit(1);
    }

    printf("\nDIR:%s" , cwd);
    
    printf(">> ");
    fflush(stdout);

    if(fgets(input,MAX_INPUT,stdin) == NULL)
    {
        exit(1);
    }

    input[strcspn(input,"\n")] = 0; // remove newline 

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
void executeCommand(char **args)
{
    pid_t pid = fork();

    if(pid == 0)
    {
        execvp(args[0],args);
        perror("command not found!");
        exit(1);
    } else {
        wait(NULL);
    }
}

//executing piped command 
void executePiped(char **arg1 , char **arg2)
{
    int pipes[2];
    pipe(pipes);

    pid_t p1 = fork();
    
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

    return 0; 
}

int main()
{
    char input[MAX_INPUT];
    
    printf("WELCOME TO MY SHELL!!!!\n");
    while(1)
    {
        
        takeInput(input);

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
            parseInput(input, args);

            if(builtInCommands(args) == 1)
            {
                continue;
            }

            executeCommand(args);
        }
        
    }
    return 0;
}