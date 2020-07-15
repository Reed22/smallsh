#define  _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h> 
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_INPUT_LEN 2048
#define NUM_ARGS 512

/**********************************************************
*
*        void resetArgs(char* arguments[])
*
* This clears the argument array using memset
***********************************************************/
void resetArgs(char* arguments[], int number_of_args){
    for(int j = 0; j < number_of_args; j++){
        memset(arguments[j], '\0', 256);
    }
}

/**********************************************************
*
*        void extractArgs(char* line, char* arguments[])
*
* This takes the input line from the shell and grabs all
* arguments. An argument is a series of nonwhite space
* characters.
***********************************************************/
int extractArgs(char* line, char* arguments[], int start_index){
    int i = start_index;
    //index of arguments
    int arg_num_ind = -1; //Set to -1 so first space will increment to 0
    
    //index of char in arguments[arg_num_ind]
    int arg_char_ind = 0;

    //Go through each character to determine number of arguments
    //While loop ends when every character in line is examined
    while(line[i] != '\0'){
        //If character is a space
        if(isspace(line[i])) { 
            //If subsequent char is also space, simply advance
            while(isspace(line[i+1]))
                i++;
            
            //non whitespace character encountered, must be arg
            arg_char_ind = 0;
            arg_num_ind++;
            i++;
            continue;
        }

        arguments[arg_num_ind][arg_char_ind] = line[i];
        i++;
        arg_char_ind++;
    }
    return arg_num_ind;
}

/**********************************************************
*
*        void handleCd(char* line, char* arguments[])
*
* This is the built-in command to handle the cd command.
*
* Takes 3 parameters
*      1. input line from user
*      2. arguments array to store arguments
*      3. An integer value representing where the extractArgs
*         function should begin
***********************************************************/
void handleCd(char* line, char* arguments[], int start_index){
    //Get arguments from input line
    int num_args = extractArgs(line, arguments, start_index);
    //If too many arguments were passed
    if(num_args > 1){
        printf("cd: Too many arguments\n");
        fflush(stdout);

    }
    //If no arguments are passed (just "cd")
    else if(num_args == 0)
        chdir(getenv("HOME"));
    
    //Below is supposed to handle any other circumstance 
    //Does not handle other things.
    else if(chdir(arguments[0]) == -1){
        perror("cd");
        fflush(stdout);
    }

    resetArgs(arguments, num_args);
}

/**********************************************************
*
*               int main(int argc, char** argv)
*
***********************************************************/
int main(int argc, char** argv){
    char* buffer;
    size_t bufsize = 2048;
    char buf[2048];
    char* args[NUM_ARGS];
    int status = 0;
    char input_file[256];
    char output_file[256];
    int process_array[30];
    int num_processes = 0;
    
    buffer = (char *) malloc(bufsize * sizeof(char));

    //malloc arguments
    for(int j = 0; j < NUM_ARGS; j++){
        args[j] = (char *) malloc(256 * sizeof(char));
    }

    //Continually run
    do {
        //Reset buf var
        //If user enters blank line, the last input will be in buf
        //and then whatever that command is will be repeated.
        memset(buf,'\0',strlen(buf));

        printf(": ");
        fflush(stdout);
        getline(&buffer, &bufsize, stdin);

        //buf takes the first word from buffer
        //i.e buffer = 'cd somedir' -- buf = 'cd'
        //also removes any extra whitespace 
        sscanf(buffer, "%s", buf);
        fflush(stdout);
        fflush(stdout);
        //Tests if #comment or blank line is added
        if(buf[0] == '#' || strlen(buf) == 0){
            //continue;
        }
        //cd
        else if(strcmp(buf, "cd") == 0) {
           handleCd(buffer, args, strlen(buf));
        }

        //status
        else if(strcmp(buf, "status") == 0){
            if(WIFEXITED(status)){
                printf("exit value %d\n", WEXITSTATUS(status));
                fflush(stdout);
            }
            else{
                printf("terminated by signal %d\n", WTERMSIG(status));
                fflush(stdout);
            }
        }

        //exit
        else if(strcmp(buf, "exit") == 0){
            printf("we are about to exit\n");
            for(int i = 0; i < num_processes; i++){
                int kill_sucess = kill(process_array[i], SIGTERM);
                if(kill_sucess == 0){
                    printf("kill of pid %d succesfull\n", process_array[i]);
                    fflush(stdout);
                }
            }
        }

        //This should work for rest of the stuff
        else {
            int num_args = extractArgs(buffer, args, strlen(buf));
            int index_redirect = -1;
            int saved_out = dup(1);
            bool error = false;
            bool is_bg_process = false;
            int saved_in = dup(0);

            char* newargv[num_args + 2];
            newargv[0] = buf;

            //Setting each value after initial command to NULL
            //I have been getting garbage values sometimes that
            //has made my program behavior poorly.
            for(int i = 1; i < num_args + 2; i ++)
                newargv[i] = NULL;

            //ASSIGN ARGS
            int args_index = 0;
            int j = 1;
            for(int j = 1; j < num_args + 1; j++){
                //If argument is a redirect for output, do not add any other
                //arguments to the arguments being sent to exec(break)
                //EX: 'ls > file' should only have 'ls' passed to arg
                if(strcmp(args[args_index], ">") == 0) {
                    //redirect_out = true;
                    strcpy(output_file,args[args_index + 1]);

                    //file descriptor for output file
                    int fd_out = open(output_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                    dup2(fd_out, 1);
                    close(fd_out);
                    break;
                } 

                //if redirect input
                else if(strcmp(args[args_index], "<") == 0) {
                    //input file will be the next argument
                    strcpy(input_file, args[args_index + 1]);

                    //file descriptor for input file
                    int fd_in = open(input_file, O_RDONLY);

                    if(fd_in == -1){
                        error = true;
                        printf("cannot open %s for input\n", input_file);
                        fflush(stdout);
                    }
                    else{
                        dup2(fd_in, 0);
                        close(fd_in);
                    }

                    //If also redirect out coupled with redirect in 
                    if(strcmp(args[args_index + 2], ">") == 0 && !error){
                        //output file will be argument after '>'
                        strcpy(output_file, args[args_index + 3]);

                        //file descriptor for output file
                        int fd_out = open(output_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
                        dup2(fd_out, 1);
                        close(fd_out);
                    }
                    
                    break;
                }                
                //Check to see if last argument is '&'
                //j == num_args <- if j is equal to num_args, then we are at the end of args
                if(strcmp(args[args_index],"&") == 0 && j == num_args){
                    is_bg_process = true;
                    break;
                }
                newargv[j] = args[args_index];
                args_index++;
            }

            if(!error){
                //fork a child and execute cd program
                pid_t parent = getpid();
                pid_t child_pid = fork();
                //if parent, wait for child to terminate
                if(child_pid > 0){
                    if(is_bg_process){
                        //do nothing? or
                        //waitpid()
                        //add child pid to thing
                        process_array[num_processes] = child_pid;
                        num_processes++;
                        waitpid(child_pid, &status, WNOHANG);
                    }
                    else {
                        waitpid(child_pid, &status, 0);
                    }
                }
                //if child, execute
                else {
                    execvp(newargv[0], newargv); 
                }
                resetArgs(args, num_args);

                //Reset stdout
                dup2(saved_out, 1);
                close(saved_out);

                //Reset stdin
                dup2(saved_in, 0);
                close(saved_in);
            }
        }

        //Check for completed child processes.
        for(int i = 0; i < num_processes; i++){
            int this_pid = waitpid(process_array[i], &status, WNOHANG);
            if(this_pid > 0){
                kill(this_pid, SIGTERM);
                printf("backgroud pid %d is done, exit value %d\n", this_pid, status);
                fflush(stdout);

                //Need to "delete" killed pid and decrement number of process
                for(int j = i; i < num_processes - 1; i++){
                    process_array[j] = process_array[j + 1];
                }
                num_processes--;
            }
        }
    } while(strcmp(buf, "exit") != 0);
    
    return 0;
}
