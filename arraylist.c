#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "arraylist.h"

// implementation of arraylist

// declaring external useful method for duplicating string for storage
extern char* strdup(const char*);

// initialize arraylist based on pointer to list and initial capacity
void al_init(arraylist_t * L, unsigned int init_capacity){
    L->array = malloc(sizeof(char*) * init_capacity);
    L->length = 0;
    L->capacity = init_capacity;
}

// returns 1 if arraylist contains target_word, 0 otherwise
int has_word(arraylist_t *L, char *word) {
    for (unsigned i = 0; i < L->length; i++)
        if (strcmp(L->array[i], word) == 0) return 1;
    return 0;
}

// free all items in arraylist and then free arraylist itself
void al_destroy(arraylist_t * L){
    for (int i = 0; i < L->length; i++){
        free(L->array[i]);
    }
    free(L->array);
}

// get length of arraylist
unsigned al_length(arraylist_t * L){
    return L->length;
}

// push item onto arraylist if it does not already exist in arraylist
int al_push(arraylist_t * L, char * item){

    if (L->length > 0){
        for (int i = 0; i < L->length; i++){
            if (strcmp(L->array[i], item) == 0){
                return 0;
            }
        }
    }

    if (L->length == L->capacity){
        unsigned newcap = L->capacity * 2;
        char **new_array = realloc(L->array, sizeof(char*) * newcap);
        if (!new_array){
            return 1;
        }
        L->array = new_array;
        L->capacity = newcap;
    }
    L->array[L->length] = strdup(item);
    L->length++;
    return 0;
}

// append without dedup (for argv / token lists)
// allows duplicate entries in the array list
int al_push_nocheck(arraylist_t * L, char * item){
    if (L->length == L->capacity){
        unsigned newcap = L->capacity * 2;
        char **new_array = realloc(L->array, sizeof(char*) * newcap);
        if (!new_array){
            return 1;
        }
        L->array = new_array;
        L->capacity = newcap;
    }
    L->array[L->length] = strdup(item);
    L->length++;
    return 0;
}

// pop and return last element in arraylist
char * al_pop(arraylist_t * L){
    if (L->length == 0){
        return NULL;
    }
    --L->length;
    return L->array[L->length];
}