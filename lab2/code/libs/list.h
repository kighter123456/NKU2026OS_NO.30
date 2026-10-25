/* lab2: 2414067 - intrusive circular doubly linked list. */
#ifndef LAB2_LIST_H
#define LAB2_LIST_H
#include <defs.h>
typedef struct list_entry { struct list_entry *prev, *next; } list_entry_t;
static inline void list_init(list_entry_t *h) { h->prev = h->next = h; }
static inline bool list_empty(const list_entry_t *h) { return h->next == h; }
static inline void list_add_before(list_entry_t *next, list_entry_t *node) {
    node->prev = next->prev; node->next = next;
    next->prev->next = node; next->prev = node;
}
static inline void list_add(list_entry_t *h, list_entry_t *node) {
    list_add_before(h->next, node);
}
static inline void list_del(list_entry_t *node) {
    node->prev->next = node->next; node->next->prev = node->prev;
    list_init(node);
}
#define list_next(node) ((node)->next)
#define list_prev(node) ((node)->prev)
#endif
