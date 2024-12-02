#include "linked_list.h"

void init_linked_list(linked_list_t* list) {
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void insert_first(linked_list_t* list, void* data) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
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

void insert_last(linked_list_t* list, void* data) {
    node_t* new_node = (node_t*)malloc(sizeof(node_t));
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

node_t* find_node(linked_list_t* list, filter_func condition) {
    node_t* current = list->head;
    while (current != NULL) {
        if (condition(current->data)) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

void delete_node(linked_list_t* list, node_t* node) {
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

void delete_first(linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    node_t* temp = list->head;
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

void delete_last(linked_list_t* list) {
    if (list->size == 0) {
        return;
    }

    node_t* temp = list->tail;
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

int delete_by_condition(linked_list_t* list, filter_func condition) {
    if (list->size == 0) {
        return -1;
    }

    node_t* current = list->head;
    int nodes_deleted = 0;

    while (current != NULL) {
        node_t* next_node = current->next;
        node_t* prev_node = current->prev;

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

void rotate_front_to_back(linked_list_t* list) {
    if (list->size <= 1) {
        return;
    }

    node_t* old_head = list->head;
    list->head = old_head->next;
    list->head->prev = NULL;

    old_head->next = NULL;
    old_head->prev = list->tail;
    list->tail->next = old_head;
    list->tail = old_head;
}

void rotate_back_to_front(linked_list_t* list) {
    if (list->size <= 1) {
        return;
    }

    node_t* old_tail = list->tail;
    list->tail = old_tail->prev;
    list->tail->next = NULL;

    old_tail->prev = NULL;
    old_tail->next = list->head;
    list->head->prev = old_tail;
    list->head = old_tail;
}

void destroy_list(linked_list_t* list) {
    node_t* current = list->head;

    while (current != NULL) {
        node_t* temp = current;
        current = current->next;
        free(temp);
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}