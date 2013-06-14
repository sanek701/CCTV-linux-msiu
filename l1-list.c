#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "l1-list.h"

l1* l1_insert(l1** l1_head, pthread_mutex_t *lock, void *value) {
  l1* p;
  if((p = (struct l1*)malloc(sizeof(struct l1))) == NULL ) {
    fprintf( stderr, "Not enough memory\n" );
    exit(EXIT_FAILURE);
  }
  p->value = value;
  pthread_mutex_lock(lock);
  p->next = *l1_head;
  *l1_head = p;
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
  l1* p;
  l1** q;
  pthread_mutex_lock(lock);
  for(q = l1_head; *q != NULL && (*q)->value != value; q = &((*q)->next));
  if(*q != NULL) { p = *q; *q = (*q)->next; free(p); }
  pthread_mutex_unlock(lock);
}
