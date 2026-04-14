// header file for linestream

// Structure to represent and store lines processed in a file
typedef struct {
    char *buf;
    int fd;
    int len;
    unsigned cap;
    unsigned pos;
} lines_t;

// Function to initialize attributes of lines_t
void lines_init (lines_t *l, int fd);

// Function to free attributes of lines_t
void lines_destroy (lines_t *l);

// Function that returns char * pointer to next lines
char *lines_next (lines_t *l);