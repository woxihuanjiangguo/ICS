#include "cache.h"


/*
 *  init_list - malloc a region for the list, initialize it
 *              the list is a global variable in proxy.c
 */
cache_list *init_list(){
    cache_list* result = Malloc(sizeof(*result));
    // 2 int
    result->size_total =  0;
    result->read_cnt = 0;
    // 2 ptr
    result->head =  NULL;
    result->tail =  NULL;
    // init mutex
    result->readcnt_mutex = Malloc(sizeof(*(result->readcnt_mutex)));
    result->write_mutex = Malloc(sizeof(*(result->write_mutex)));
    Sem_init(result->readcnt_mutex, 0, 1);
    Sem_init(result->write_mutex, 0, 1);
    return result;
}
/*
 * free_node - free the pointers of a node we don't need
 * 
 */
void free_node(cache_node *node){
    if(node){
        if(node->content){
            Free(node->content);
        }
        if(node->key){
            Free(node->key);
        }
        Free(node);
    }
}

/*
 * reader_locker - lock the readcnt_mutex of the list
 * 
 */
void reader_locker(cache_list *list){
    P(list->readcnt_mutex);
    list->read_cnt ++;
    if(list->read_cnt == 1){
        P(list->write_mutex);
    }
    V(list->readcnt_mutex);
}

/*
 * reader_unlocker - unlock the readcnt_mutex of the list
 * 
 */
void reader_unlocker(cache_list *list){
    P(list->readcnt_mutex);
    list->read_cnt --;
    if(list->read_cnt == 0){
        V(list->write_mutex);
    }
    V(list->readcnt_mutex);
}

/*
 *  evicted - find the node that should be evicted
 *            refresh head, tail, size_total of the list
 */
cache_node *evicted(cache_list *list){
    cache_node *result;
    if((result = list->head) == NULL){      // nothing
        return NULL;
    }
    list->size_total -= result->size;
    if(result->next){                       // head has next
        result->next->prev = NULL;
    }
    if(list->head == list->tail){           // 1 node
        list->tail = list->head = NULL;
    }
    list->head = result->next;
    return result;
}

/*
 *  search_list - find the node with the key in the list
 *        
 */
cache_node *search_list(cache_list *list, char *key){
    cache_node *temp = list->head;
    while(temp != NULL){
        if(!strcmp(key, temp->key)){
            return temp;
        }
        temp = temp->next;
    }
    return temp;
}


/*
 *  refresh_node - when a node is read, put it to the tail
 *                 the size of the list remains unchanged     
 */
void refresh_node(cache_list *list, cache_node *node){
    if(node == list->tail){     // case 1 - node is tail, return
        return;
    }
    if(node == list->head){     // case 2 - node is head, and is not tail, so the list has more than 1 node
        list->head = node->next;
        list->head->prev = NULL;
    } else {                    // case 3 - node is in the middle
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    node->prev = list->tail;
    node->next = NULL;
    list->tail->next = node;
    list->tail = node;  
}

/*
 *  insert_node - when the key does not appear in the list
 *                init a node, then insert it into the list
 */
void insert_node(cache_list *list, char *key, char *content){
    int content_length = sizeof(char) * strlen(content);

    if(!list){
        unix_error("insert_node error, list unavailable");
        return;
    }

    if(content_length > (unsigned int) MAX_OBJECT_SIZE){
        unix_error("insert_node error, node bigger than MAX_OBJECT_SIZE");
        return;
    }

    int key_length = sizeof(char) * strlen(key);
    

    // init the node, init the ptrs, init the key & content
    cache_node *node = Malloc(sizeof(*node));
    node->size = content_length;
    node->prev = node->next = NULL;

    node->key = Malloc(key_length);                 // init key
    memcpy(node->key, key, key_length);
    
    node->content = Malloc(content_length);         // init content
    memcpy(node->content, content, content_length);

    if(node == NULL){
        unix_error("insert_node error, node malloc failed");
        return;
    }

    // insert it into the list
    P((list->write_mutex));     // lock the writer mutex
    while((MAX_CACHE_SIZE - list->size_total) < node->size){   //evict until we have enough room for the new node
        free_node(evicted(list));
    }
    if(list->tail){
        // non-empty list
        list->tail->next = node;
        node->prev = list->tail;
        list->tail = node;
    }else{
        // empty list
        list->head = list->tail = node;
    }
    // set the size of the list
    list->size_total += node->size;
    V((list->write_mutex));     // unlock the writer mutex

}

/*
 *  get_node - get the proper node from the list according to the key
 *             write the content to the content_buf
 *             return node's size if the key is valid, 0 if invalid
 *             refresh the node's position in the list
 */
int get_node(cache_list *list, char *key, char *content_buf){
    if(!list){
        unix_error("insert_node error, list unavailable");
        return 0;
    }

    // combination of locks for the reader (start)
    reader_locker(list);


    /*start reading*/
    cache_node *result = search_list(list, key);
    if(result == NULL){
        reader_unlocker(list);
        return 0;
    }
    memcpy(content_buf, result->content, result->size);
    /*end reading*/


    // combination of locks for the reader (end)
    reader_unlocker(list);

    // lock the writer (refresh node position)
    P(list->write_mutex);

    /*start writing*/
    refresh_node(list, result);
    /*end writing*/

    // unlock the writer (refresh node position)
    V(list->write_mutex);


    return result->size;
}