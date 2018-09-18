/*=============================================================================
|	I hereby certify that this collective work is my own
|	and none of it is the work of any other person or entity.
|	______________________________________  Jose A. Toledo, Jasmine Batten.
|  +-----------------------------------------------------------------------------
|  Description:  This program is an interactive shell capable of executing single
|                unix commands and multiple unix commands.
|  *=============================================================================*/
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_ARGS 20
#define BUFFSIZ 1024

typedef struct cmd_struct {
    char *argv[MAX_ARGS];
    int nargs;
    int async;
} CMD;

int backGroundProcesses[BUFFSIZ];
int forGroundProcesses[MAX_ARGS];
int bIndex = 0;
int fIndex = 0;

void execute(CMD cmd);

int get_args(char *cmdline, CMD commands[]);

void exec_processes(char *cmdline);


/*---------------------------- get_args ---------------------------
        |  function int get_args(char *cmdline, CMD commands[])
        |
        |  Purpose: Splits the command line into smaller execution blocks
        |           saved into an array of CMD struct.
        |
        |  @param   *cmdline(IN) Command line to be processed
        |           commands[](IN) Array of CMD to store individual
        |           programs.
        |
        |
        |  @return  Total number of processes.
        *-------------------------------------------------------------*/
int get_args(char *cmdline, CMD commands[]) {
    int i = 0;
    int j = 0;
    int programCount = 0;

    /* if no args */
    if ((commands[j].argv[i] = strtok(cmdline, "\n\t ")) == NULL)
        return 0;
    programCount = 1;
    commands[j].nargs = 1;
    commands[j].async = 0;

    while ((commands[j].argv[++i] = strtok(NULL, "\n\t ")) != NULL) {
        if (i >= MAX_ARGS) {
            printf("Too many arguments!\n");
            exit(1);
        }
        commands[j].nargs = i + 1;

        if (strcmp(commands[j].argv[i], "&") == 0) {
            commands[j].argv[i] = 0;
            commands[j].nargs = i;
            commands[j].async = 1;
        } else if (strcmp(commands[j].argv[i], ";") == 0) {
            commands[j].argv[i] = 0;
            commands[j].nargs = i;
            programCount++;
            j++;
            i = -1;
        }
    }
    return programCount;
}

/*---------------------------- exec_processes ---------------------------
        |  function void exec_processes(char *cmdline)
        |
        |  Purpose: Executes the processes passed in the command line.
        |
        |  @param   char *cmdline String containing command line
        *-------------------------------------------------------------*/
void exec_processes(char *cmdline) {
    CMD commands[MAX_ARGS];

    int numbProcesses = get_args(cmdline, commands);

    int i = 0;
    for (; i < numbProcesses; i++) {
        execute(commands[i]);
    }

    for (i = 0; i < fIndex; i++) {
        waitpid(forGroundProcesses[i], NULL, 0);
    }
    fIndex = 0;
}

/*---------------------------- execute ---------------------------
        |  function void execute(CMD cmd)
        |
        |  Purpose: Executes command line entered by user.
        |
        |  @param   CMD cmd(IN) Command line to be executed.
        *-------------------------------------------------------------*/
void execute(CMD cmd) {
    int pid, async;
    char *args[MAX_ARGS];

    if (!strcmp(cmd.argv[0], "quit") || !strcmp(cmd.argv[0], "exit")) {
        // wait for background processes before closing shell
        int i;
        for (i = 0; i < bIndex; i++) {
            waitpid(backGroundProcesses[i], NULL, 0);
        }
        exit(0);
    }

    pid = fork();
    if (pid == 0) { /* child process */
        execvp(cmd.argv[0], cmd.argv);
        /* return only when exec fails */
        perror("exec failed");
        exit(-1);
    } else if (pid > 0) { /* parent process */
        if (cmd.async == 1) {
            printf("ID %d\n", pid);
            backGroundProcesses[bIndex] = pid;
            bIndex++;
        } else {
            forGroundProcesses[fIndex] = pid;
            fIndex++;
        }
    } else { /* error occurred */
        perror("fork failed");
        exit(1);
    }
}


int main(int argc, char *argv[]) {
    char cmdline[BUFFSIZ];

    for (;;) {
        printf("Batten_Toledo> ");
        if (fgets(cmdline, BUFFSIZ, stdin) == NULL) {
            perror("fgets failed");
            exit(1);
        }
        exec_processes(cmdline);
    }
}
