#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>

#define MAX_INPUT 1024

int main()
{
    char input[MAX_INPUT];

    while(1)
    {
        printf("myshell> ");
        fflush(stdout);

        if(fgets(input,MAX_INPUT,stdin) == NULL)
        {
             break;
        }

        input[strcspn(input,"\n")] = 0;

        if(strlen(input) == 0)
        {
            continue;
        }

        char *args[64];
        int argc = 0;

        char *token = strtok(input, " ");
        while(token!=NULL)
        {
            args[argc++] = token;
            token = strtok(NULL," ");
        }
        args[argc] = NULL;

        __pid_t pid = fork();

        if(pid == 0)
        {
            execvp(args[0],args);
            printf("Command not found: %s\n" , args[0]);
            exit(1);
        } else {
            wait(NULL);
        }
    }
    return 0;
}