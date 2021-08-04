#ifndef CACHE_H
#define CACHE_H

#include "csapp.h"

/* MACROS */
#define MAX_CACHE_SIZE 1048576
#define MAX_OBJECT_SIZE 102400

typedef struct cache_node{
    char *key;          // full http request path
    char *content;      // hold the content from the server
    struct cache_node *prev;
    struct cache_node *next;
    unsigned int size;  // the length of a cache node
} cache_node;

typedef struct cache_list{
    cache_node *head;
    cache_node *tail;
    sem_t *readcnt_mutex;       // for reader (get_node)
    sem_t *write_mutex;         // for writer (insert_node, delete, refresh)
    int read_cnt;
    unsigned int size_total;
} cache_list;

/* RULES 
   1. a new node is inserted into the tail of the list
   2. to evict a node, evict the head
   3. each time a node is read, it is placed in the tail
   4. tail favors recently read
*/

// helper routines
void refresh_node(cache_list *list, cache_node *node);      // refresh the node's position when it is read
void free_node(cache_node *node);                           // fill a new node with key and content
cache_node *evicted(cache_list *list);                      // find the evicted node and refresh the list info
cache_node *search_list(cache_list *list, char *key);       // find the node with the key in the list   
void reader_locker(cache_list *list);                       // lock the readcnt_mutex
void reader_unlocker(cache_list *list);                     // unlock the readcnt_mutex

// interfaces exposed to proxy.c
cache_list *init_list();                                      // init the list
void insert_node(cache_list *list, char *key, char *content); // insert the node into the list
int get_node(cache_list *list, char *key, char *content_buf); // get the content from the node


#endif