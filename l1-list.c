#include <stdio.h>
#include <stdlib.h>
#include "l1-list.h"

l1* l1_insert(l1** l1_head, void *value) {
  l1* p;
  if((p = (struct l1*)malloc(sizeof(struct l1))) == NULL ) {
    fprintf( stderr, "Not enough memory\n" );
    exit(EXIT_FAILURE);
  }
  p->value = value;
  p->next = *l1_head;
  return *l1_head = p;
}

void l1_remove(l1** l1_head, void *value) {
  l1* p;
  l1** q;
  for(q = l1_head; *q != NULL && (*q)->value != value; q = &((*q)->next));
  if(*q != NULL) { p = *q; *q = (*q)->next; free(p); }
}
