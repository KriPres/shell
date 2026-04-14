#include <stdio.h>
#include <stdlib.h>
#include "arraylist.h"
#define DEBUG 1

int main(int argc, char** argv){

    // Case 1: ./mysh + 1 argument: read commands from specified file
    // will always run in batch mode
    if (argc == 2){ 

        char * command_file_path = argv[1];
        if (DEBUG) printf("Command File Path: %s\n", command_file_path);
    
    // Case 2: ./mysh + 0 argument: use isatty() to determine
    //  whether to run in interactive or batch mode
    } else if (argc == 1){

    } else {
        fprintf(stderr, "./mysh takes at most 1 arg: %d given\n", argc);
    }
}