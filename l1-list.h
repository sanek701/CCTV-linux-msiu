typedef struct l1 {
  struct l1* next;
  void *value;
} l1;

l1* l1_insert(l1** l1_head, pthread_mutex_t *lock, void *value);
void* l1_shift(l1** l1_head, pthread_mutex_t *lock);
void l1_remove(l1** l1_head, pthread_mutex_t *lock, void *value);
