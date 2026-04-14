#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "arraylist.h"
#include "linestream.h"
#include <fcntl.h>
#include <ctype.h>
#include "fileutils.h"

// Function to check if a file ends with desired ending; returns 1 if it does; 0 otherwise
int endsWith(char *string, char *end){

    // get string length of both strings
    int string_length = strlen(string);
    int end_length = strlen(end);

    // if suffix is greater than string, return false
    if (end_length > string_length){
        return false;
    }

    // check to ensure that string ends with the suffix
    for (int i = 0; i < end_length; i++){
        if (end[end_length - i] != string[string_length - i]){
            return false;
        }
    }

    return true;
}

// function to recursively traverse desired directories
void recursive_traversal(arraylist_t * FileList, char *path){
    
    // open directory and get DIR pointer
    DIR *dir = opendir(path);

    // if null, directory doesn't exist, so perror and return
    if (dir == NULL){
        perror(path);
        return;
    }

    // helper variable to keep track of path length
    int path_len = strlen(path);

    struct dirent *de;

    while ((de = readdir(dir))){

        // ignore files starting with '.'
        if (de->d_name[0] == '.'){
            continue;
        }

        // update path 
        int name_len = strlen(de->d_name);
        char *file_path = malloc(path_len + name_len + 2);

        memcpy(file_path, path, path_len);
        file_path[path_len] = '/';
        memcpy(file_path + path_len + 1, de->d_name, name_len + 1);

        struct stat sb;
        int res = stat(file_path, &sb);

        // if stat returns -1, ignore and continue
        if (res == -1){
            free(file_path);
            continue;
        }

        // if path is a directory, keep traversing recursively
        if (S_ISDIR(sb.st_mode)){
            recursive_traversal(FileList, file_path);
        // else, confirm it ends with .txt and add to FileList
        } else if (endsWith(file_path, ".txt")){
            al_push(FileList, file_path);
        }
        
        free(file_path);

    }

    closedir(dir);
}