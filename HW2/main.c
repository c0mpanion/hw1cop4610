/*=============================================================================
|   Source code:  myshell.c
|        Author:  Jose A. Toledo
|    Student ID:  6069060
| Assignment #3:  Unix shell with redirects and pipes.
|        Course:  COP 4338
|       Section:  U02
|    Instructor:  Caryl Rahn
|      Due Date:  4/22
|
|	I hereby certify that this collective work is my own
|	and none of it is the work of any other person or entity.
|	______________________________________  Jose A. Toledo.
|  +-----------------------------------------------------------------------------
|
|  Description:  This program is an interactive shell capable of executing single
|                unix commands and multiple unix commands by use of pipes.
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
#define ILLEGAL_OUT "Illegal output to file.\nMust output to next command.\n"
#define ILLEGAL_IN "Illegal input file.\nMust get input from previous command.\n"

typedef struct cmd_struct {
    char *argv[MAX_ARGS];
    int nargs;
    int asyn;
} CMD;

/*---------------------------- get_args ---------------------------
        |  function int get_args(char *cmdline, CMD commands[], int *async)
        |
        |  Purpose: Splits the command line into smaller execution blocks
        |           saved into an array of CMD struct.
        |
        |  @param   *cmdline(IN) Command line to be processed
        |           commands[](IN) Array of CMD to store individual
        |           programs.
        |           *async(IN) Asynchronous flag.
        |
        |  @return  Total number of processes.
        *-------------------------------------------------------------*/
int get_args(char *cmdline, CMD commands[], int *async) {
    int i = 0;
    int j = 0;
    int programCount = 0;

    /* if no args */
    if ((commands[j].argv[i] = strtok(cmdline, "\n\t ")) == NULL)
        return 0;
    programCount = 1;
    commands[j].nargs = 1;
    commands[j].asyn = 0;

    while ((commands[j].argv[++i] = strtok(NULL, "\n\t ")) != NULL) {
        if (i >= MAX_ARGS) {
            printf("Too many arguments!\n");
            exit(1);
        }
        commands[j].nargs = i + 1;
        if (strcmp(commands[j].argv[i], "&") == 0) {
            commands[j].argv[i] = '\0';
            commands[j].nargs = i;
            commands[j].asyn = 1;
        }
        if (commands[j].argv[i] != '\0') {
            if (strcmp(commands[j].argv[i], ";") == 0) {
                commands[j].argv[i] = '\0';
                commands[j].nargs = i;
                programCount++;
                j++;
                i = -1;
            }
        }
    }
    return programCount;
}

/*---------------------------- exec_procceses ---------------------------
        |  function void exec_procceses(int programCount, CMD commands[], int async)
        |
        |  Purpose: Executes the processes stored in CMD array.
        |
        |  @param   programCount(IN) Total number of processes to execute.
        |           commands[](IN) Array of CMD containning processes to be
        |                          executed.
        |           programs.
        |           *async(IN) Asynchronous flag.
        *-------------------------------------------------------------*/
void exec_procceses(int programCount, CMD commands[], int async) {
    int i, k;
    int pid;
    int outFile;
    int inFile;
    int fd[2];
    int fdPrev; // track previous read-end of pipe

    for (i = 0; i < programCount; i++) {

        if (pipe(fd) < 0) {
            perror("Pipe Failed.");
            exit(-1);
        }
        pid = fork();
        if (pid == 0) { /* child process */

            // if async call, print background process ID;
            if (commands[i].asyn == 1) {
                printf("Background Process %d\n", getpid());
            }

            // check arguments
            for (k = 0; k < commands[i].nargs; k++) {

                if (!(strcmp(commands[i].argv[k], ">"))) {
                    if (programCount > 1 && i != programCount - 1) {
                        fprintf(stderr, ILLEGAL_OUT);
                        exit(-1);
                    }
                    commands[i].argv[k] = '\0';
                    outFile = open(commands[i].argv[k + 1], O_CREAT | O_RDONLY | O_WRONLY, S_IWUSR | S_IREAD);
                    dup2(outFile, STDOUT_FILENO);
                    close(outFile);
                } else if (!(strcmp(commands[i].argv[k], ">>"))) {
                    if (programCount > 1 && i != programCount - 1) {
                        fprintf(stderr, ILLEGAL_OUT);
                        exit(-1);
                    }
                    commands[i].argv[k] = '\0';
                    outFile = open(commands[i].argv[k + 1], O_CREAT | O_APPEND | O_WRONLY | O_WRONLY,
                                   S_IWUSR | S_IREAD);
                    dup2(outFile, STDOUT_FILENO);
                    close(outFile);
                } else if (!(strcmp(commands[i].argv[k], "<"))) {
                    if (programCount > 1 && i == programCount - 1 || (i != 0 && i != programCount - 1)) {
                        fprintf(stderr, ILLEGAL_IN);
                        exit(-1);
                    }
                    commands[i].argv[k] = '\0';
                    inFile = open(commands[i].argv[k + 1], O_RDONLY, S_IREAD);
                    dup2(inFile, STDIN_FILENO);
                    close(inFile);
                }
            }

            close(fd[0]);
            execvp(commands[i].argv[0], commands[i].argv);
            /* return only when exec fails */
            perror("Exec Failed");
            exit(-1);
        } else if (pid > 0) { /* parent process */
            if (commands[i].asyn != 1) {
                waitpid(pid, NULL, 0);
                close(fd[1]);
                fdPrev = fd[0];
            } else {
                printf("this is an async call\n");
            }
        } else { /* error occurred */
            perror("Fork Failed");
            exit(-1);
        }
    }
}

/*---------------------------- execute ---------------------------
        |  function void execute(char *cmdline)
        |
        |  Purpose: Executes command line entered by user.
        |
        |  @param   *cmdline(IN) Command line to be executed.
        *-------------------------------------------------------------*/
void execute(char *cmdline) {
    int async = 0;
    int programCount = 1;
    // get number of commands to execute
    int j;
    for (j = 0; j < strlen(cmdline); j++) {
        if ((cmdline[j] == ';')) {
            programCount++;
        }
    }
    CMD commands[programCount];
    programCount = get_args(cmdline, commands, &async);
    if (programCount <= 0) return;
    if (!strcmp(commands[0].argv[0], "quit") || !strcmp(commands[0].argv[0], "exit")) {
        exit(0);
    }
    exec_procceses(programCount, commands, async);
}


int main(int argc, char *argv[]) {
    char cmdline[BUFFSIZ];

    for (;;) {
        printf("Batten_Toledo> ");
        if (fgets(cmdline, BUFFSIZ, stdin) == NULL) {
            perror("fgets failed");
            exit(1);
        }
        execute(cmdline);
    }
}
