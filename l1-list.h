typedef struct l1 {
  struct l1* next;
  void *value;
} l1;

l1* l1_insert(l1** l1_head, void *value);
void l1_remove(l1** l1_head, void *value);
