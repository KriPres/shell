#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "arraylist.h"
#define DEBUG 1

int main(int argc, char** argv){

    enum Mode {
        INTERACTIVE,
        BATCH,
        TBD
    };

    enum Mode mode = TBD;

    // Case 1: ./mysh + 1 argument: read commands from specified file
    // will always run in batch mode
    if (argc == 2){ 

        char * command_file_path = argv[1];
        if (DEBUG) printf("Command File Path: %s\n", command_file_path);
        mode = BATCH;
    
    // Case 2: ./mysh + 0 argument: use isatty() to determine
    //  whether to run in interactive or batch mode
    } else if (argc == 1){

        if (isatty(STDIN_FILENO)){
            
            if (DEBUG) printf("is a terminal - interactive mode\n");
            mode = INTERACTIVE; 

        } else {

            if (DEBUG) printf("not a terminal - batch mode\n");
            mode = BATCH;

        }

    // Case 3: > 1 argument: show error message
    } else {
        fprintf(stderr, "./mysh takes at most 1 arg: %d given\n", argc);
    }

    if (mode == INTERACTIVE){
        printf("Welcome to my shell!\n");
    } 

}