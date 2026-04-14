// header file for arraylist

// Structure representing information to store an array list - pointer to list, length, capacity
typedef struct {
    char ** array;
    unsigned length;
    unsigned capacity;
} arraylist_t;

// Function to initialize arraylist
void al_init(arraylist_t *, unsigned int);

// Function to free arraylist
void al_destroy(arraylist_t *);

// Function to get length of arraylist
unsigned al_length(arraylist_t *);

// Function to append item to end of arraylist
int al_push(arraylist_t *, char *);

// Function to pop and return last element of arraylist
char * al_pop (arraylist_t *);

int has_word(arraylist_t * L, char * target_word);