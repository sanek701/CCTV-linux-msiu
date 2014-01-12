#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "l1-list.h"

l1* l1_insert(l1** l1_head, pthread_mutex_t *lock, void *value) {
  l1* p;
  if((p = (struct l1*)malloc(sizeof(struct l1))) == NULL ) {
    fprintf(stderr, "Not enough memory\n");
    exit(EXIT_FAILURE);
  }
  p->value = value;
  if(lock != NULL)
    pthread_mutex_lock(lock);
  p->next = *l1_head;
  *l1_head = p;
  if(lock != NULL)
    pthread_mutex_unlock(lock);
  return p;
}

void* l1_shift(l1** l1_head, pthread_mutex_t *lock) {
  if(*l1_head == NULL) return NULL;
  pthread_mutex_lock(lock);
  void *value = (*l1_head)->value;
  *l1_head = (*l1_head)->next;
  pthread_mutex_unlock(lock);
  return value;
}

void l1_remove(l1** l1_head, pthread_mutex_t *lock, void *value) {
  l1 *p, **q;
  if(lock != NULL)
    pthread_mutex_lock(lock);
  for(q = l1_head; *q != NULL && (*q)->value != value; q = &((*q)->next));
  if(*q != NULL) { p = *q; *q = (*q)->next; free(p); }
  if(lock != NULL)
    pthread_mutex_unlock(lock);
}

void* l1_find(l1** l1_head, pthread_mutex_t *lock, int (*accept_func) (void *, void *), void *arg) {
  l1** q;
  pthread_mutex_lock(lock);
  for(q = l1_head; *q != NULL && !accept_func((*q)->value, arg); q = &((*q)->next));
  pthread_mutex_unlock(lock);
  if(*q == NULL) return NULL;
  return (*q)->value;
}

void l1_filter(l1** l1_head, pthread_mutex_t *lock, int (*filter_func) (void *, void *), void *arg) {
  l1 *p, **q;
  for(q = l1_head; *q != NULL;) {
    if(!filter_func((*q)->value, arg)) {
      p = *q; *q = (*q)->next; free(p);
    } else {
      q = &((*q)->next);
    }
  }
}
