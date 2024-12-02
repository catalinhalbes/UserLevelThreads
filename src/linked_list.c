#include "linked_list.h"

////////////////////// GENERIC LIST //////////////////////

void init_linked_list(generic_linked_list_t* list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void insert_first(generic_linked_list_t* list, void* data) {
    generic_node_t* new_node = (generic_node_t*) malloc(sizeof(generic_node_t));
    new_node->data = data;
    new_node->next = list->head;
    new_node->prev = NULL;

    if (list->head != NULL) {
        list->head->prev = new_node;
    }

    list->head = new_node;

    if (list->tail == NULL) { // empty list
        list->tail = new_node;
    }

    list->size += 1;
}

void insert_last(generic_linked_list_t* list, void* data) {
    generic_node_t* new_node = (generic_node_t*)malloc(sizeof(generic_node_t));
    new_node->data = data;
    new_node->next = NULL;
    new_node->prev = list->tail;

    if (list->tail != NULL) {
        list->tail->next = new_node;
    }

    list->tail = new_node;

    if (list->head == NULL) { // empty list
        list->head = new_node;
    }

    list->size += 1;
}

generic_node_t* find_node(generic_linked_list_t* list, filter_func condition) {
    generic_node_t* current = list->head;
    while (current != NULL) {
        if (condition(current->data)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

void delete_node(generic_linked_list_t* list, generic_node_t* node) {
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } 
    else {
        // the node should be the head, if it isn't this will break the list
        list->head = node->next;
    }
    
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } 
    else {
        // the node should be the tail, if it isn't this will break the list (but sligthly less than in the case of the head)
        list->tail = node->prev;
    }

    free(node);

    list->size -= 1;
}

void delete_first(generic_linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    generic_node_t* temp = list->head;
    list->head = list->head->next;

    if (list->head != NULL) {
        list->head->prev = NULL;
    }
    else { // list is now empty
        list->tail = NULL;
    }

    free(temp);
    list->size -= 1;
}

void delete_last(generic_linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    generic_node_t* temp = list->tail;
    list->tail = list->tail->prev;

    if (list->tail != NULL) {
        list->tail->next = NULL;
    } 
    else {
        list->head = NULL; // list is now empty
    }

    free(temp);
    list->size -= 1;
}

int delete_by_condition(generic_linked_list_t* list, filter_func condition) {
    if (list->size == 0) {
        return -1;
    }

    generic_node_t* current = list->head;
    int nodes_deleted = 0;

    while (current != NULL) {
        generic_node_t* next_node = current->next;
        generic_node_t* prev_node = current->prev;

        if (condition(current->data)) {
            if (prev_node == NULL) {
                // current is the head
                list->head = next_node;
            }
            else {
                prev_node->next = next_node;
            }

            if (next_node == NULL) {
                // current is the tail
                list->tail = prev_node;
            } 
            else {
                next_node->prev = prev_node;
                
            }

            free(current);
            nodes_deleted += 1;
            list->size -= 1;
        }

        current = next_node;
    }

    return nodes_deleted;
}

void rotate_front_to_back(generic_linked_list_t* list) {
    if (list->size <= 1) {
        return;
    }

    generic_node_t* old_head = list->head;
    list->head = old_head->next;
    list->head->prev = NULL;

    old_head->next = NULL;
    old_head->prev = list->tail;
    list->tail->next = old_head;
    list->tail = old_head;
}

void rotate_back_to_front(generic_linked_list_t* list) {
    if (list->size <= 1) {
        return;
    }

    generic_node_t* old_tail = list->tail;
    list->tail = old_tail->prev;
    list->tail->next = NULL;

    old_tail->prev = NULL;
    old_tail->next = list->head;
    list->head->prev = old_tail;
    list->head = old_tail;
}

void destroy_list(generic_linked_list_t* list) {
    generic_node_t* current = list->head;

    while (current != NULL) {
        generic_node_t* temp = current;
        current = current->next;
        free(temp);
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

////////////////////// USER LEVEL THREAD LIST //////////////////////

void init_ult_linked_list(ult_linked_list_t* list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void insert_ult_first(ult_linked_list_t* list, ult_t* ult) {
    ult_node_t* new_node = (ult_node_t*) malloc(sizeof(ult_node_t));
    new_node->ult = ult;
    new_node->next = list->head;
    new_node->prev = NULL;

    if (list->head != NULL) {
        list->head->prev = new_node;
    }

    list->head = new_node;

    if (list->tail == NULL) { // empty list
        list->tail = new_node;
    }

    list->size += 1;
}

void insert_ult_last(ult_linked_list_t* list, ult_t* ult) {
    ult_node_t* new_node = (ult_node_t*) malloc(sizeof(ult_node_t));
    new_node->ult = ult;
    new_node->next = NULL;
    new_node->prev = list->tail;

    if (list->tail != NULL) {
        list->tail->next = new_node;
    }

    list->tail = new_node;

    if (list->head == NULL) { // empty list
        list->head = new_node;
    }

    list->size += 1;
}

void delete_ult_node(ult_linked_list_t* list, ult_node_t* node) {
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } 
    else {
        // the node should be the head, if it isn't this will break the list
        list->head = node->next;
    }
    
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } 
    else {
        // the node should be the tail, if it isn't this will break the list (but sligthly less than in the case of the head)
        list->tail = node->prev;
    }

    free(node);

    list->size -= 1;
}

void delete_ult_first(ult_linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    ult_node_t* temp = list->head;
    list->head = list->head->next;

    if (list->head != NULL) {
        list->head->prev = NULL;
    }
    else { // list is now empty
        list->tail = NULL;
    }

    free(temp);
    list->size -= 1;
}

void delete_ult_last(ult_linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    ult_node_t* temp = list->tail;
    list->tail = list->tail->prev;

    if (list->tail != NULL) {
        list->tail->next = NULL;
    } 
    else {
        list->head = NULL; // list is now empty
    }

    free(temp);
    list->size -= 1;
}

void rotate_ult_front_to_back(ult_linked_list_t* list) {
    if (list->size <= 1) {
        return;
    }

    ult_node_t* old_head = list->head;
    list->head = old_head->next;
    list->head->prev = NULL;

    old_head->next = NULL;
    old_head->prev = list->tail;
    list->tail->next = old_head;
    list->tail = old_head;
}

void destroy_ult_list(ult_linked_list_t* list) {
    ult_node_t* current = list->head;

    while (current != NULL) {
        ult_node_t* temp = current;
        current = current->next;
        free(temp);
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

////////////////////// MUTEX LIST //////////////////////

void init_mutex_linked_list(mutex_linked_list_t* list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void insert_mutex_last(mutex_linked_list_t* list, ult_mutex_t* mutex) {
    mutex_node_t* new_node = (mutex_node_t*) malloc(sizeof(mutex_node_t));
    new_node->mutex = mutex;
    new_node->next = NULL;
    new_node->prev = list->tail;

    if (list->tail != NULL) {
        list->tail->next = new_node;
    }

    list->tail = new_node;

    if (list->head == NULL) { // empty list
        list->head = new_node;
    }

    list->size += 1;
}

void delete_mutex_node(mutex_linked_list_t* list, mutex_node_t* node) {
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } 
    else {
        // the node should be the head, if it isn't this will break the list
        list->head = node->next;
    }
    
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } 
    else {
        // the node should be the tail, if it isn't this will break the list (but sligthly less than in the case of the head)
        list->tail = node->prev;
    }

    free(node);

    list->size -= 1;
}

void destroy_mutex_list(mutex_linked_list_t* list) {
    mutex_node_t* current = list->head;

    while (current != NULL) {
        mutex_node_t* temp = current;
        current = current->next;
        free(temp);
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}