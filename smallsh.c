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
#include <signal.h>

#define MAX_INPUT_LEN 2048
#define NUM_ARGS 512

//Global variables
//Need for signal handlers
bool in_fg_only_mode = false; //Allows main program to know if we are in foreground only mode
int status = 0; //status variable that will be used for status command.

/**********************************************************
*
*        void handle_SIGTSTP(int signo)
*
* This handles the Cntrl - Z signal, which will set shell
* to foreground only mode or exit foreground only mode.
***********************************************************/
//NEEDS TO WAIT FOR BG PROCESSES <--------------
void handle_SIGTSTP(int signo){

    //If we are not in fg only mode, enter fg only mode
    //Note: Encountering a bug that results in the next prompt (:) not being
    //printed after SIGTSTP. Added a prompt to the end of each message 
    //to fix this. 
    if(!in_fg_only_mode){
        char* message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(1, message, 53);
        fflush(stdout);
        in_fg_only_mode = true;
    }
    //Exit fg only mode if we are in it
    else if(in_fg_only_mode){
        char* message = "\nExiting foreground-only mode\n: ";
        write(1, message, 33);
        fflush(stdout);
        in_fg_only_mode = false;
    }
}

/**********************************************************
*
*        void handle_SIGINT(int signo)
*
* This handles the Cntrl - C signal. Will not terminate shell, 
* but will terminate any foreground processes
***********************************************************/
void handle_SIGINT(int signo){
    //Doing nothing seems to work....
    //But it also kills BG process, which is a no-no
    if(!WEXITSTATUS(status)){
        char* message = "terminated by signal ";
        write(1, message, 21);
        fflush(stdout);
        char sig[20]; 
        sprintf(sig, "%d\n", status);
        char* sig_msg = sig;
        write(1, sig_msg, 3);
        fflush(stdout);
    }

}
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
*
* Does this by extracting each character as it moves down
* input line. If it comes across whitespace, it ignores it
* all subsequent whitespaces. Also extracts pid when it 
* comes across '$$'
***********************************************************/
int extractArgs(char* line, char* arguments[], int start_index){
    //Starting index for extracting arguments
    //start index will be index after the command
    int i = start_index;
    //index of arguments
    int arg_num_ind = -1; //Set to -1 so first space will increment to 0
    
    //index of char in arguments[arg_num_ind], increments each time a character is found
    //and placed into arguments[], resets to 0 once whitespace is found.
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

        //If '$$' is found in argument, expand to shell pid
        if(line[i] == '$' && line[i+1] == '$'){
            int shell_pid_int = getpid();
            char shell_pid_str[20]; //Used to turn shell_pid into a string for output
            for(int j = 0; j < 20; j++){
                shell_pid_str[j] = '\0';
            }
            //copy int to a char[] using sprintf
            sprintf(shell_pid_str, "%d", shell_pid_int);

            int pid_str_index = 0; //Used to go down shell_pid_str[] to extract characters
            //Copy until null character found
            while(shell_pid_str[pid_str_index] != '\0'){
                arguments[arg_num_ind][arg_char_ind] = shell_pid_str[pid_str_index];
                arg_char_ind++;
                pid_str_index++;
            }
            //Since i and i+1 have both been examined, increment i by two to get to next character
            i += 2;
        }
        else {
            arguments[arg_num_ind][arg_char_ind] = line[i];
            i++;
            arg_char_ind++;
        }
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
    char* buffer;  //Used for getline to store user input
    size_t bufsize = 2048; //Size for allocating buffer
    char buf[2048]; //buf will hold the command (first word) of user input using sscanf()
    char* args[NUM_ARGS]; //Arguments array, will be used to place arguments from user input
    //int status = 0; 
    char input_file[256]; //File name for input redirection
    char output_file[256]; //File name for output redirection
    int process_array[30]; //Array that will hold PID of processes that have not been completed
    int num_processes = 0; //Number of processes in process array

    
    buffer = (char *) malloc(bufsize * sizeof(char));

    //malloc arguments
    for(int j = 0; j < NUM_ARGS; j++){
        args[j] = (char *) malloc(256 * sizeof(char));
    }

    //Setting up SIGTSTP signal handler
    struct sigaction SIGTSTP_action = {NULL};
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    //Setting up SIGINT signal handler
    struct sigaction SIGINT_action = {NULL};
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    //Continually run unit user enters "exit"
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
            //Don't do anything
        }
        //cd
        else if(strcmp(buf, "cd") == 0) {
           handleCd(buffer, args, strlen(buf));
        }

        //status
        //prints exit code or termination signal
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
        //kills all remaining processes that are still running
        else if(strcmp(buf, "exit") == 0){
            for(int i = 0; i < num_processes; i++){
                kill(process_array[i], SIGTERM);
            }
        }

        //This works for every other command using fork() and execvp()
        else {
            int num_args = extractArgs(buffer, args, strlen(buf)); //Extract arguments and get number of args
            int saved_out = dup(1); //Used to replace output to stdout for redirection
            int saved_in = dup(0); //Used to replace input to stdin for redirection
            bool error = false; //error flag
            bool is_bg_process = false; //Used for parent shell to know whether to wait for child process or not

            char* newargv[num_args + 2]; //Arguments that will be passed to execvp()
            newargv[0] = buf;


            //Setting each value after initial command to NULL
            //I have been getting garbage values sometimes that
            //has made my program behavior poorly.
            for(int i = 1; i < num_args + 2; i ++)
                newargv[i] = NULL;

            //Assign arguments from args[] to newargv[]
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

                    //If error opening file, print error and set error flag
                    if(fd_in == -1){
                        error = true;
                        printf("cannot open %s for input\n", input_file);
                        fflush(stdout);
                    }
                    //else, redirect input to input file
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
                    if(in_fg_only_mode == false){
                        is_bg_process = true;
                        int fd_dev_null = open("/dev/null", O_WRONLY, S_IRUSR | S_IWUSR);
                        dup2(fd_dev_null, 1);
                        break;
                    }
                }
                
                //If normal argument, simply copy
                else{
                    newargv[j] = args[args_index];
                    args_index++;
                }
            }

            if(!error){
                //fork a child and execute cd program
                pid_t parent = getpid();
                pid_t child_pid = fork();
                //if parent
                if(child_pid > 0){
                    //If background process, do not wait
                    if(is_bg_process){
                        //add child pid to array to keep track of later
                        process_array[num_processes] = child_pid;
                        num_processes++;
                        waitpid(child_pid, &status, WNOHANG);

                        //Reset stdout early so that pid can be outputted to terminal
                        dup2(saved_out, 1);
                        close(saved_out);
                        
                        printf("background pid is %d\n", child_pid); 
                        fflush(stdout);
                    }
                    //wait on child if not a background process
                    else {
                        //See if this works....used for signal handler
                        //fg_running = true;
                        waitpid(child_pid, &status, 0);
                        //fg_running = false;
                    }
                }
                //if child, execute
                else {
                    execvp(newargv[0], newargv);
                    //If error: print out the command and the error
                    //The lines below will not be executed if execvp() doesn't fail
                    printf("%s: ", buf);
                    fflush(stdout);
                    perror("");
                    fflush(stdout);
                    exit(EXIT_FAILURE);
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
            //If waitpid returns pid, kill that processes
            if(this_pid > 0){
                kill(this_pid, SIGTERM);                
                if(WIFEXITED(status)){
                    printf("backgroud pid %d is done: exit value %d\n", this_pid, status);
                    fflush(stdout);
                }
                else{
                    printf("backgroud pid %d is done: terminated by signal %d\n", this_pid, status);
                    fflush(stdout);
                }
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
