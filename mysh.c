#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "arraylist.h"
#include "linestream.h"


extern char *strdup(const char *);

// one stage in a pipeline: its argv, input redirection file, output redirection file
typedef struct {
    arraylist_t argv;
    char *redir_in;
    char *redir_out;
} subcmd_t;

static const char *SEARCH_DIRS[] = { "/usr/local/bin", "/usr/bin", "/bin", NULL };
static const char *BUILTINS[]    = { "cd", "pwd", "which", "exit", NULL };
 
// print prompt in interactive mode: "~$ ", "~/subdir$ ", or "/full/path$ "
static void print_prompt(void) {

    char cwd[4096];
    
    // handle case where getcwd returns NULL
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        write(STDOUT_FILENO, "?$ ", 3);
        return;
    }
    
    // get HOME environment variable
    const char *home = getenv("HOME");
    char prompt[4096 + 4]; // + 4 to accomodate for  extra space needed for "$ " and '\0'
    
    // if home exists, try to shorten the path
    if (home != NULL) {

        // length of home string
        size_t hlen = strlen(home);

        // if cwd is inside home
        if (strncmp(cwd, home, hlen) == 0) {

            // examine next character after home directory path
            const char *rest = cwd + hlen;

            // if no more characters, we are at home directory
            if (*rest == '\0')
                // snprintf is used to specify where to write prompt, max size, char* format, ... (format params)
                snprintf(prompt, sizeof(prompt), "~$ "); 
            
            // if rest is inside a further directory, print accordingly
            else if (*rest == '/')
                snprintf(prompt, sizeof(prompt), "~%s$ ", rest);
            
            // prefix matches HOME but cwd is not exactly HOME or a subdirectory;
            // avoid incorrect '~' substitution due to partial string match
            else
                snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
        } 
        
        // if cwd not inside home, print entire path to cwd
        else {
            snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
        }
    } 
    
    // if no home directory, directly print cwd path
    else {
        snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
    }
    
    // write prompt out to STDOUT_FILENO
    write(STDOUT_FILENO, prompt, strlen(prompt));
}
 
// split line into tokens; '>', '<', '|' are solo tokens; '#' starts a comment
// takes in a string (char *) line as input, and populates an arraylist (*tokens)
// with the tokens after parsing line
static void tokenise(const char *line, arraylist_t *tokens) {
    
    // initialize constant pointer to start of line, p
    const char *p = line;
 
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        
        // stop if end of line
        if (*p == '\0') {
            break;
        }

        // stop parsing if start of comment
        if (*p == '#') {
            break;
        }

        // if it is one of the redirection / pipe tokens, add to tokens list
        if (*p == '>' || *p == '<' || *p == '|') {
            char tok[2] = { *p, '\0' };
            al_push_nocheck(tokens, tok);
            p++;
            continue;
        }
        
        // mark start of argument
        const char *start = p;

        // loop until not iterating through argument anymore
        while (*p != '\0' && *p != ' ' && *p != '\t' &&
               *p != '>' && *p != '<' && *p != '|') {
            p++;
        }
        
        // determine length of argument
        int len = (int)(p - start);

        // allocate space for the token and add it to arraylist
        char *tok = malloc(len + 1);
        memcpy(tok, start, len);
        tok[len] = '\0';
        
        // add argument to tokens list
        al_push_nocheck(tokens, tok);
        free(tok);
    }
}
 
// comparator for qsort - compare 2 strings alphabetically
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}
 
// expand a single wildcard token into out; pass through unchanged if no match
static void expand_wildcard(const char *pattern, arraylist_t *out) {

    // first, we want to separate directory we are in, with ending wildcard expansion part

    // pointer to last slash in pattern
    const char *last_slash = strrchr(pattern, '/');

    // buffer for directory string
    char dir_buf[4096];
    const char *dir; // directory
    const char *name_pat; // name pattern
    
    // if pattern contains a '/'
    if (last_slash != NULL) {

        // directory component length = last slash(last slash) - pattern(start of directory)
        size_t dlen = (size_t)(last_slash - pattern);

        // handle special case - root directory (one slash at beginning)
        if (dlen == 0) {
            dir = "/";
        } 
        // any other case: copy directory into buffer
        else {
            memcpy(dir_buf, pattern, dlen);
            dir_buf[dlen] = '\0';
            dir = dir_buf;
        }
        // pointer to just name pattern (+1 for after slash): smth like: "*.c"
        name_pat = last_slash + 1;
    } 

    // if pattern doesn't contain a '/'
    else {

        // specify current working directory if no specified one
        dir      = ".";

        // name_pat doesn't need any modification or string parsing in this case
        name_pat = pattern;
    }
    
    // find position of wildcard '*' in name_pat (substring that contains '*')
    const char *star = strchr(name_pat, '*');
    
    // if no star in the name_pat portion, do not expand
    if (star == NULL) {
        al_push_nocheck(out, (char *)pattern);
        return;
    }
    
    // determine prefix length based on pointer substraction
    // everything before the star
    size_t pre_len = (size_t)(star - name_pat);

    // pointer to suffix, and suffix length
    const char *suffix  = star + 1;
    size_t suf_len = strlen(suffix);
    
    // get DIR * to directory
    DIR *dp = opendir(dir);

    // if directory doesn't exist, literally append pattern
    if (dp == NULL) {
        al_push_nocheck(out, (char *)pattern);
        return;
    }
    
    // arraylist to store matches for '*' pattern matching
    arraylist_t matches;
    al_init(&matches, 8);
    
    // loop through directory entreis
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {

        // current file name
        const char *name = ent->d_name; 
        size_t      nlen = strlen(name);
        
        // ignore hidden files unless chance of a prefix that could allow them
        if (name[0] == '.' && pre_len == 0) {
            continue;
        }

        // ignore if length of string is less than prefix and suffix length's sum
        if (nlen < pre_len + suf_len) {
            continue;
        }
        
        // if prefix and current file prefix don't match, ignore
        if (pre_len > 0 && strncmp(name, name_pat, pre_len) != 0) {
            continue;
        }

        // if suffix and current file suffix don't match, ignore
        if (suf_len > 0 && strcmp(name + nlen - suf_len, suffix) != 0) {
            continue;
        }

        char full[4096];

        // in case of directory, 
        if (last_slash != NULL){
            // print file that matches pattern along with the directory name
            snprintf(full, sizeof(full), "%.*s/%s", (int)(last_slash - pattern), pattern, name);
        }
        // else, in case with no directory, print just the file name
        else{
            snprintf(full, sizeof(full), "%s", name);
        }

        // append matches to array list
        al_push_nocheck(&matches, full);
    }
    
    // close directory pointer
    closedir(dp);
    
    // if no matches, leave wildcard as is, and append to output array
    if (matches.length == 0) {
        al_push_nocheck(out, (char *)pattern);
    } 
    
    // otherwise, sort matches alphabetically, and append to arraylist result (out) from matches array
    else {
        qsort(matches.array, matches.length, sizeof(char *), cmp_str);
        for (unsigned i = 0; i < matches.length; i++){
            al_push_nocheck(out, matches.array[i]);
        }
    }
    
    // free match list
    al_destroy(&matches);
}
 
// walk raw token list and expand any wildcard tokens into expanded
// arraylist_t * raw - parsed list of raw rokens (.split() + trim whitespace Python equivalent)
// expand tokens and place them in the specified expanded arraylist_t
static void expand_tokens(arraylist_t *raw, arraylist_t *expanded) {

    // for each token in the array,
    for (unsigned i = 0; i < raw->length; i++) {
        
        char *tok = raw->array[i];

        // if it contains a wildcard - '*', 
        if (strchr(tok, '*') != NULL){
            // expand it and add it to the expanded array (result array)
            expand_wildcard(tok, expanded);
        }
        // otherwise, add it to the array_list
        else{
            al_push_nocheck(expanded, tok);
        }
    }
}

// returns 1 if name is a built-in command
static int is_builtin(const char *name) {
    for (int i = 0; BUILTINS[i]; i++){
        if (strcmp(name, BUILTINS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// returns malloc'd full path if found in search dirs, NULL otherwise
static char *find_program(const char *name) {

    // if command contains a slash, assume user is giving a direct path
    if (strchr(name, '/')){
        // return name as is if command exists and is executable (X_OK), NULL otherwise
        return access(name, X_OK) == 0 ? strdup(name) : NULL;
    }

    char path[4096];

    // loop through specified search directories in order
    for (int i = 0; SEARCH_DIRS[i]; i++) {
        // print and return complete path if found in that SEARCH_DIRS[i] 
        snprintf(path, sizeof(path), "%s/%s", SEARCH_DIRS[i], name);
        if (access(path, X_OK) == 0) {
            return strdup(path);
        }
    }
    return NULL;
}

// parse flat token list into subcmd array split on '|'; returns number of subcmds, -1 on error
// input: arraylist of parsed extracted tokens, max_cmds (int)
// output: place cmds into subcmd_t array
static int parse(arraylist_t *tokens, subcmd_t cmds[], int max_cmds) {

    // initialize cmds[0] with
    //  - argv: list of arguments
    //  - redir_in - string (char*)
    //  - redir_out - string (char*)
    //  - empty/NULL-like values for all

    int n = 0;
    // n: iterator variable for token list

    al_init(&cmds[n].argv, 4);
    cmds[n].redir_in  = NULL;
    cmds[n].redir_out = NULL;

    // iterate through each token in tokens (parsed and processed)

    for (unsigned i = 0; i < tokens->length; i++) {

        // *t = current token

        char *t = tokens->array[i];

        // if it is a pipe "|"

        if (strcmp(t, "|") == 0) {
            
            // if no commands yet or too many commands
            if (cmds[n].argv.length == 0 || n + 1 >= max_cmds) {

                // error, free memory, and return -1
                write(STDERR_FILENO, "mysh: syntax error\n", 19);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }
            
            // increment iterator variable, and update variables to start processing next command
            n++;
            al_init(&cmds[n].argv, 4);
            cmds[n].redir_in  = NULL;
            cmds[n].redir_out = NULL;

        } 
        
        // if a '<' b (read input for a from b instead of command line)
        else if (strcmp(t, "<") == 0) {
            
            // if no input because last token, display error
            if (i + 1 >= tokens->length) {
                write(STDERR_FILENO, "mysh: syntax error near '<'\n", 28);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }

            // specify redirection input to following token
            cmds[n].redir_in = tokens->array[++i];

        } 
        
        // if a '>' b (run a and write its outut to b)
        else if (strcmp(t, ">") == 0) {

            //  if no a because of last token, display error

            if (i + 1 >= tokens->length) {
                write(STDERR_FILENO, "mysh: syntax error near '>'\n", 28);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }

            // specify redirection output to following token
            cmds[n].redir_out = tokens->array[++i];

        } 

        // pass token as is to arraylist if it doesn't contain '|', '>', '<'
        
        else {
            al_push_nocheck(&cmds[n].argv, t);
        }

    }

    // if last command is empty, free and remove it
    if (cmds[n].argv.length == 0) {
        write(STDERR_FILENO, "mysh: syntax error\n", 19);
        for (int j = 0; j <= n; j++) {
            al_destroy(&cmds[j].argv);
        }
        return -1;
    }

    // return number of commands
    return n + 1;
}

// run a built-in command; returns 0 on success, 1 on failure
static int run_builtin(subcmd_t *cmd, int out_fd) {

    // extract command name
    char *name = cmd->argv.array[0];

    // if command is exit, do nothing, return 0
    if (strcmp(name, "exit") == 0)
        return 0;

    // if command is pwd, write the pwd name by calling getcwd to the specified output fd, return 0
    if (strcmp(name, "pwd") == 0) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) == NULL) { perror("pwd"); return 1; }
        write(out_fd, cwd, strlen(cwd));
        write(out_fd, "\n", 1);
        return 0;
    }

    // if command is cd
    if (strcmp(name, "cd") == 0) {

        // confirm only one argument is parsed, if notreturn 1
        if (cmd->argv.length > 2) {
            write(STDERR_FILENO, "cd: too many arguments\n", 23);
            return 1;
        }

        // if no argument besides cd, set directory string to HOME, otherwise set to second argument (arg after cd)
        char *dir = cmd->argv.length == 1 ? getenv("HOME") : cmd->argv.array[1];
        
        // if NULL directory, display error, return 1
        if (dir == NULL) { 
            write(STDERR_FILENO, "cd: HOME not set\n", 17); 
            return 1; 
        }

        // if chdir returns an error, perror and return -1
        if (chdir(dir) != 0) { 
            perror("cd"); 
            return 1; 
        }

        // return 0, indicating successful cd operation
        return 0;
    }

    // if command is which
    if (strcmp(name, "which") == 0) {

        // which must take exactly 1 additional argument, if it doesn't return 1
        if (cmd->argv.length != 2) {
            return 1;
        }

        // prog = 1 argument after which
        char *prog = cmd->argv.array[1];

        // if one of the built ins, do nothing
        if (is_builtin(prog)) {
            return 1;
        }

        // find program path using helper function
        char *path = find_program(prog);

        // return 1 if not found
        if (path == NULL) {
            return 1;
        }

        // display path if found
        write(out_fd, path, strlen(path));
        write(out_fd, "\n", 1);
        free(path);
        return 0;
    }

    return 1;
}

// fork and exec one subcmd; in_fd/out_fd are pipe ends (-1 = default)
static pid_t spawn(subcmd_t *cmd, int in_fd, int out_fd, int interactive) {

    // fork 
    pid_t pid = fork();

    // in the parent, we return child's PID, so we immediately return it
    if (pid != 0) {
        return pid;
    }

    // stdin for input redirection
    if (cmd->redir_in != NULL) {
        int fd = open(cmd->redir_in, O_RDONLY);
        if (fd < 0) { 
            perror(cmd->redir_in); 
            exit(1); 
        }
        dup2(fd, STDIN_FILENO); // replace stdin with file 
        close(fd);
    } 
    
    // if valid input file descriptor from previous command, use it for redirection
    else if (in_fd != -1) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    } 
    
    // in batch mode, use /dev/null as default input stream for child processes
    else if (!interactive) {
        int fd = open("/dev/null", O_RDONLY);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    // stdout - output redirection
    if (cmd->redir_out != NULL) {
        int fd = open(cmd->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (fd < 0) { 
            perror(cmd->redir_out); 
            exit(1); 
        }
        dup2(fd, STDOUT_FILENO); // replace stdout with file
        close(fd);
    } 
    
    // if command needs to send output to pipe or other command, do so
    else if (out_fd != -1) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    // close the other pipe end if passed in
    if (in_fd != -1) {
        close(in_fd);
    }

    // close output pipe end if passed in
    if (out_fd != -1) {
        close(out_fd);
    }

    // build argv as null-terminated array
    char **args = malloc((cmd->argv.length + 1) * sizeof(char *));
    for (unsigned i = 0; i < cmd->argv.length; i++){
        args[i] = cmd->argv.array[i];
    }
    args[cmd->argv.length] = NULL;
    
    if (is_builtin(args[0])) {
        // builtin in a pipeline: run inline in child
        run_builtin(cmd, STDOUT_FILENO);
        free(args);
        exit(0);
    }

    char *path = find_program(args[0]);
    if (path == NULL) {
        fprintf(stderr, "%s: command not found\n", args[0]);
        free(args);
        exit(1);
    }

    execv(path, args);
    perror(path);
    free(path);
    free(args);
    exit(1);
}
 
int main(int argc, char **argv) {

    // enum to describe mode - interactive or batch
    enum { INTERACTIVE, BATCH } mode;
    int input_fd = STDIN_FILENO;
    
    // if I have 2 arguments, open the argument and assign input_fd if possible, perror otherwise
    if (argc == 2) {
        
        input_fd = open(argv[1], O_RDONLY);
        
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }

        // if I have an argument, we are in batch mode
        mode = BATCH;
    } 
    
    // if there are no arguments, determine interactive or batch mode based on isatty - file associated with terminal
    else if (argc == 1) {
        mode = isatty(STDIN_FILENO) ? INTERACTIVE : BATCH;
    } 
    
    // incorrect number of arguments passed in, so print error
    else {
        fprintf(stderr, "Usage: ./mysh takes in <= 1 argument, %d given\n", argc - 1);
        return EXIT_FAILURE;
    }
    
    // if interactive mode, print welcome message
    if (mode == INTERACTIVE)
        write(STDOUT_FILENO, "Welcome to my shell!\n", 21);
    
    // specify a reader lines_t struct for reading lines in a unified way regardless of mode
    lines_t reader;
    lines_init(&reader, input_fd);
    
    // main execution loop used for both interactive and batch mode
    while (1) {

        // print prompt if in interactive mode
        if (mode == INTERACTIVE)
            print_prompt();
        
        // parse next line
        char *line = lines_next(&reader);

        // if line is NULL, break (typically end of file, could be read failure)
        if (line == NULL)
            break;
 
        // tokenise the line
        arraylist_t raw_tokens;

        // create an empty arraylist of capacity 8
        al_init(&raw_tokens, 8);

        // calls tokenise function and parses lines into tokens list -> stored in raw_tokens
        tokenise(line, &raw_tokens);
        
        // if empty line, free tokens list and continue
        if (raw_tokens.length == 0) {
            al_destroy(&raw_tokens);
            continue;
        }
 
        // expand wildcards using expand_tokens function
        arraylist_t tokens;
        al_init(&tokens, raw_tokens.length);
        expand_tokens(&raw_tokens, &tokens);
        al_destroy(&raw_tokens);
 
        // parse tokens into subcmd array
        subcmd_t cmds[4096];
        int ncmds = parse(&tokens, cmds, 4096);

        // if no commands, free storage for tokens list, and continue to next line
        if (ncmds < 0) {
            al_destroy(&tokens);
            continue;
        }

        // check for exit before doing any work
        // if any of the commands == "exit", change should_exit marker to 1 (true)
        int should_exit = 0;
        for (int i = 0; i < ncmds; i++) {
            if (strcmp(cmds[i].argv.array[0], "exit") == 0)
                should_exit = 1;
        }

        // execute the command

        // case 1: 1 command, and first word is a built in command, and no exit
        if (ncmds == 1 && is_builtin(cmds[0].argv.array[0]) && !should_exit) {

            // specify STDOUT_FILENO as output file descriptor
            int out_fd = STDOUT_FILENO;
            int fd_to_close = -1;

            // if there is a different output file descriptor, specify STDOUT_FILENO to be that
            if (cmds[0].redir_out != NULL) {
                
                out_fd = open(cmds[0].redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                
                // if there is an error with opening that file, output error and reset STDOUT_FILENO
                if (out_fd < 0) { 
                    perror(cmds[0].redir_out); 
                    out_fd = STDOUT_FILENO; // fall back to screen output
                }
                
                // specify fd_to_close
                else fd_to_close = out_fd;
            }

            // call helper function to run in command based on how we saved info about it to the output file descriptor
            run_builtin(&cmds[0], out_fd);
            if (fd_to_close != -1) {
                close(fd_to_close);
            }
        } 
        
        // more than 1 commands or not built in commands, but still no should exit
        else if (!should_exit) {

            // external commands and pipelines - setup
            int prev_read = -1;
            pid_t pids[4096];

            // for each command
            for (int i = 0; i < ncmds; i++) {

                // create a pipe, if needed

                int pipefd[2] = {-1, -1};
                if (i < ncmds - 1) {
                    if (pipe(pipefd) < 0) {
                        perror("pipe");
                        break;
                    }
                }

                // spawn the process - each process gets:
                //  - prev_read - input from previous command
                //  - pipefd[1] - output to next command
                pids[i] = spawn(&cmds[i], prev_read, pipefd[1], mode == INTERACTIVE);

                // parent closes its copies (unused pipe ends)
                if (prev_read != -1) {
                    close(prev_read);
                }
                if (pipefd[1] != -1) {
                    close(pipefd[1]);
                }

                // update prev_red
                prev_read = pipefd[0];
            }

            // cleanup leftover pipes to ensure no leaks
            if (prev_read != -1) {
                close(prev_read);
            }

            // wait for all children; track last exit status
            int last_status = 0;
            for (int i = 0; i < ncmds; i++) {

                // skip invalid processes
                if (pids[i] <= 0) {
                    continue;
                }

                // pause shell until that process finishes, and collects status
                int status;
                waitpid(pids[i], &status, 0);

                // store status of last command in pipeline
                if (i == ncmds - 1) {
                    last_status = status;
                }
            }

            // report exit status in interactive mode
            if (mode == INTERACTIVE) {
                if (WIFEXITED(last_status) && WEXITSTATUS(last_status) != 0){
                    fprintf(stderr, "Exited with status %d\n", WEXITSTATUS(last_status));
                }
                else if (WIFSIGNALED(last_status)) {
                    int sig = WTERMSIG(last_status);
                    fprintf(stderr, "Terminated by signal %d: %s\n", sig, strsignal(sig));
                }
            }
        }

        // free subcmd argvs
        for (int i = 0; i < ncmds; i++){
            al_destroy(&cmds[i].argv);
        }
        al_destroy(&tokens);

        // exit if should exit
        if (should_exit) {
            break;
        }
    }
    
    // print exit message once loop is over
    if (mode == INTERACTIVE){
        write(STDOUT_FILENO, "Exiting my shell.\n", 18);
    }
    
    // free memory and close open input_fd if necessary
    lines_destroy(&reader);
    if (input_fd != STDIN_FILENO){
        close(input_fd);
    }
 
    return EXIT_SUCCESS;
}