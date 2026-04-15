#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        write(STDOUT_FILENO, "?$ ", 3);
        return;
    }
 
    const char *home = getenv("HOME");
    char prompt[4096 + 4];
 
    if (home != NULL) {
        size_t hlen = strlen(home);
        if (strncmp(cwd, home, hlen) == 0) {
            const char *rest = cwd + hlen;
            if (*rest == '\0')
                snprintf(prompt, sizeof(prompt), "~$ ");
            else if (*rest == '/')
                snprintf(prompt, sizeof(prompt), "~%s$ ", rest);
            else
                snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
        } 
        
        else {
            snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
        }
    } 
    
    else {
        snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
    }
 
    write(STDOUT_FILENO, prompt, strlen(prompt));
}
 
// split line into tokens; '>', '<', '|' are solo tokens; '#' starts a comment
static void tokenise(const char *line, arraylist_t *tokens) {
    const char *p = line;
 
    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') {
            break;
        }
        if (*p == '#') {
            break;
        }
        if (*p == '>' || *p == '<' || *p == '|') {
            char tok[2] = { *p, '\0' };
            al_push_nocheck(tokens, tok);
            p++;
            continue;
        }
 
        const char *start = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' &&
               *p != '>' && *p != '<' && *p != '|') {
            p++;
        }
 
        int len = (int)(p - start);
        char *tok = malloc(len + 1);
        memcpy(tok, start, len);
        tok[len] = '\0';
        al_push_nocheck(tokens, tok);
        free(tok);
    }
}
 
// comparator for qsort
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}
 
// expand a single wildcard token into out; pass through unchanged if no match
static void expand_wildcard(const char *pattern, arraylist_t *out) {
    const char *last_slash = strrchr(pattern, '/');
    char dir_buf[4096];
    const char *dir;
    const char *name_pat;
 
    if (last_slash != NULL) {
        size_t dlen = (size_t)(last_slash - pattern);
        if (dlen == 0) {
            dir = "/";
        } 
        
        else {
            memcpy(dir_buf, pattern, dlen);
            dir_buf[dlen] = '\0';
            dir = dir_buf;
        }
        name_pat = last_slash + 1;
    } 
    
    else {
        dir      = ".";
        name_pat = pattern;
    }
 
    const char *star = strchr(name_pat, '*');
    if (star == NULL) {
        al_push_nocheck(out, (char *)pattern);
        return;
    }
 
    size_t pre_len = (size_t)(star - name_pat);
    const char *suffix  = star + 1;
    size_t suf_len = strlen(suffix);
 
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        al_push_nocheck(out, (char *)pattern);
        return;
    }
 
    arraylist_t matches;
    al_init(&matches, 8);
 
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        size_t      nlen = strlen(name);
 
        if (name[0] == '.' && pre_len == 0) {
            continue;
        }

        if (nlen < pre_len + suf_len) {
            continue;
        }
        
        if (pre_len > 0 && strncmp(name, name_pat, pre_len) != 0) {
            continue;
        }
        if (suf_len > 0 && strcmp(name + nlen - suf_len, suffix) != 0) {
            continue;
        }

        char full[4096];
        if (last_slash != NULL){
            snprintf(full, sizeof(full), "%.*s/%s", (int)(last_slash - pattern), pattern, name);
        }
        else{
            snprintf(full, sizeof(full), "%s", name);
        }
        al_push_nocheck(&matches, full);
    }
    closedir(dp);
 
    if (matches.length == 0) {
        al_push_nocheck(out, (char *)pattern);
    } 
    
    else {
        qsort(matches.array, matches.length, sizeof(char *), cmp_str);
        for (unsigned i = 0; i < matches.length; i++){
            al_push_nocheck(out, matches.array[i]);
        }
    }
 
    al_destroy(&matches);
}
 
// walk raw token list and expand any wildcard tokens into expanded
static void expand_tokens(arraylist_t *raw, arraylist_t *expanded) {
    for (unsigned i = 0; i < raw->length; i++) {
        char *tok = raw->array[i];
        if (strchr(tok, '*') != NULL){
            expand_wildcard(tok, expanded);
        }
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
    if (strchr(name, '/')){
        return access(name, X_OK) == 0 ? strdup(name) : NULL;
    }

    char path[4096];
    for (int i = 0; SEARCH_DIRS[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", SEARCH_DIRS[i], name);
        if (access(path, X_OK) == 0) {
            return strdup(path);
        }
    }
    return NULL;
}

// parse flat token list into subcmd array split on '|'; returns number of subcmds, -1 on error
static int parse(arraylist_t *tokens, subcmd_t cmds[], int max_cmds) {
    int n = 0;
    al_init(&cmds[n].argv, 4);
    cmds[n].redir_in  = NULL;
    cmds[n].redir_out = NULL;

    for (unsigned i = 0; i < tokens->length; i++) {
        char *t = tokens->array[i];

        if (strcmp(t, "|") == 0) {
            if (cmds[n].argv.length == 0 || n + 1 >= max_cmds) {
                write(STDERR_FILENO, "mysh: syntax error\n", 19);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }
            n++;
            al_init(&cmds[n].argv, 4);
            cmds[n].redir_in  = NULL;
            cmds[n].redir_out = NULL;

        } 
        
        else if (strcmp(t, "<") == 0) {
            if (i + 1 >= tokens->length) {
                write(STDERR_FILENO, "mysh: syntax error near '<'\n", 28);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }
            cmds[n].redir_in = tokens->array[++i];

        } 
        
        else if (strcmp(t, ">") == 0) {
            if (i + 1 >= tokens->length) {
                write(STDERR_FILENO, "mysh: syntax error near '>'\n", 28);
                for (int j = 0; j <= n; j++) {
                    al_destroy(&cmds[j].argv);
                }
                return -1;
            }
            cmds[n].redir_out = tokens->array[++i];

        } 
        
        else {
            al_push_nocheck(&cmds[n].argv, t);
        }
    }

    if (cmds[n].argv.length == 0) {
        write(STDERR_FILENO, "mysh: syntax error\n", 19);
        for (int j = 0; j <= n; j++) {
            al_destroy(&cmds[j].argv);
        }
        return -1;
    }
    return n + 1;
}

// run a built-in command; returns 0 on success, 1 on failure
static int run_builtin(subcmd_t *cmd, int out_fd) {
    char *name = cmd->argv.array[0];

    if (strcmp(name, "exit") == 0)
        return 0;

    if (strcmp(name, "pwd") == 0) {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)) == NULL) { perror("pwd"); return 1; }
        write(out_fd, cwd, strlen(cwd));
        write(out_fd, "\n", 1);
        return 0;
    }

    if (strcmp(name, "cd") == 0) {
        if (cmd->argv.length > 2) {
            write(STDERR_FILENO, "cd: too many arguments\n", 23);
            return 1;
        }
        char *dir = cmd->argv.length == 1 ? getenv("HOME") : cmd->argv.array[1];
        if (dir == NULL) { 
            write(STDERR_FILENO, "cd: HOME not set\n", 17); 
            return 1; 
        }
        if (chdir(dir) != 0) { 
            perror("cd"); 
            return 1; 
        }
        return 0;
    }

    if (strcmp(name, "which") == 0) {
        if (cmd->argv.length != 2) {
            return 1;
        }
        char *prog = cmd->argv.array[1];
        if (is_builtin(prog)) {
            return 1;
        }
        char *path = find_program(prog);
        if (path == NULL) {
            return 1;
        }
        write(out_fd, path, strlen(path));
        write(out_fd, "\n", 1);
        free(path);
        return 0;
    }

    return 1;
}

// fork and exec one subcmd; in_fd/out_fd are pipe ends (-1 = default)
static pid_t spawn(subcmd_t *cmd, int in_fd, int out_fd, int interactive) {
    pid_t pid = fork();
    if (pid != 0) {
        return pid;
    }

    // stdin
    if (cmd->redir_in != NULL) {
        int fd = open(cmd->redir_in, O_RDONLY);
        if (fd < 0) { perror(cmd->redir_in); exit(1); }
        dup2(fd, STDIN_FILENO);
        close(fd);
    } 
    
    else if (in_fd != -1) {
        dup2(in_fd, STDIN_FILENO);
        close(in_fd);
    } 
    
    else if (!interactive) {
        int fd = open("/dev/null", O_RDONLY);
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    // stdout
    if (cmd->redir_out != NULL) {
        int fd = open(cmd->redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0640);
        if (fd < 0) { perror(cmd->redir_out); exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    } 
    
    else if (out_fd != -1) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    // close the other pipe end if passed in
    if (in_fd != -1) {
        close(in_fd);
    }
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
 
    enum { INTERACTIVE, BATCH } mode;
    int input_fd = STDIN_FILENO;
 
    if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        mode = BATCH;
    } 
    
    else if (argc == 1) {
        mode = isatty(STDIN_FILENO) ? INTERACTIVE : BATCH;
    } 
    
    else {
        fprintf(stderr, "Usage: mysh [script]\n");
        return EXIT_FAILURE;
    }
 
    if (mode == INTERACTIVE)
        write(STDOUT_FILENO, "Welcome to my shell!\n", 21);
 
    lines_t reader;
    lines_init(&reader, input_fd);
 
    while (1) {
        if (mode == INTERACTIVE)
            print_prompt();
 
        char *line = lines_next(&reader);
        if (line == NULL)
            break;
 
        // tokenise the line
        arraylist_t raw_tokens;
        al_init(&raw_tokens, 8);
        tokenise(line, &raw_tokens);
 
        if (raw_tokens.length == 0) {
            al_destroy(&raw_tokens);
            continue;
        }
 
        // expand wildcards
        arraylist_t tokens;
        al_init(&tokens, raw_tokens.length);
        expand_tokens(&raw_tokens, &tokens);
        al_destroy(&raw_tokens);
 
        // parse tokens into subcmd array
        subcmd_t cmds[16];
        int ncmds = parse(&tokens, cmds, 16);

        if (ncmds < 0) {
            al_destroy(&tokens);
            continue;
        }

        // check for exit before doing any work
        int should_exit = 0;
        for (int i = 0; i < ncmds; i++) {
            if (strcmp(cmds[i].argv.array[0], "exit") == 0)
                should_exit = 1;
        }

        // execute
        if (ncmds == 1 && is_builtin(cmds[0].argv.array[0]) && !should_exit) {
            // single builtin: handle redirection then run inline
            int out_fd = STDOUT_FILENO;
            int fd_to_close = -1;
            if (cmds[0].redir_out != NULL) {
                out_fd = open(cmds[0].redir_out, O_WRONLY | O_CREAT | O_TRUNC, 0640);
                if (out_fd < 0) { perror(cmds[0].redir_out); out_fd = STDOUT_FILENO; }
                else fd_to_close = out_fd;
            }
            run_builtin(&cmds[0], out_fd);
            if (fd_to_close != -1) {
                close(fd_to_close);
            }
        } 
        
        else if (!should_exit) {
            // external commands and pipelines
            int prev_read = -1;
            pid_t pids[16];

            for (int i = 0; i < ncmds; i++) {
                int pipefd[2] = {-1, -1};
                if (i < ncmds - 1) {
                    if (pipe(pipefd) < 0) {
                        perror("pipe");
                        break;
                    }
                }

                pids[i] = spawn(&cmds[i], prev_read, pipefd[1], mode == INTERACTIVE);

                // parent closes its copies
                if (prev_read != -1) {
                    close(prev_read);
                }
                if (pipefd[1] != -1) {
                    close(pipefd[1]);
                }
                prev_read = pipefd[0];
            }

            if (prev_read != -1) {
                close(prev_read);
            }

            // wait for all children; track last exit status
            int last_status = 0;
            for (int i = 0; i < ncmds; i++) {
                if (pids[i] <= 0) {
                    continue;
                }
                int status;
                waitpid(pids[i], &status, 0);
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
                    fprintf(stderr, "Terminated by signal %d\n", WTERMSIG(last_status));
                }
            }
        }

        // free subcmd argvs
        for (int i = 0; i < ncmds; i++){
            al_destroy(&cmds[i].argv);
        }
        al_destroy(&tokens);

        if (should_exit) {
            break;
        }
    }
 
    if (mode == INTERACTIVE){
        write(STDOUT_FILENO, "Exiting my shell.\n", 18);
    }
 
    lines_destroy(&reader);
    if (input_fd != STDIN_FILENO){
        close(input_fd);
    }
 
    return EXIT_SUCCESS;
}
