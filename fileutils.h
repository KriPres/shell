// Header file for fileutils - useful for file + directory handling

// Function that returns 1, if string ends with end; 0 otherwise
int endsWith(char *string, char *end);

// Function that recursively traverses path and adds desired files to FileList
void recursive_traversal(arraylist_t * FileList, char *path);