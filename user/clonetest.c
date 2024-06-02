#include "user/user.h"

union lock_test_args {
  int *var;
  struct lock *lk;
};

int thread_fn(void *args) {
  int value = *(int *)args;
  if (value & 1) {
    printf("This thread will call exit with code %d\n", value);
    exit(value);
  }
  printf("This thread will return with code %d\n", value);
  return value;
}

int lock_test(void *args) {
  int *var = ((union lock_test_args *)args)[0].var;
  struct lock *lk = ((union lock_test_args *)args)[1].lk;
  for (int i = 0; i < 1000000; i++) {
    acquire(lk);
    ++*var;
    release(lk);
  }

  return 0;
}

int main(void) {
  int expected_code = 1;
  int code = 0;
  int tid;

  printf("Testing thread return values...\n");

  tid = clone(thread_fn, &expected_code);
  join(tid, &code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code, code);
    exit(1);
  }

  expected_code = 2;
  tid = clone(thread_fn, &expected_code);
  join(tid, &code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code, code);
    exit(1);
  }

  printf("\nTesting locks...\n");

  int *var = malloc(sizeof(int));
  struct lock *lk = malloc(sizeof(struct lock));
  int tid1 =
      clone(lock_test, (union lock_test_args[]){{.var = var}, {.lk = lk}});
  int tid2 =
      clone(lock_test, (union lock_test_args[]){{.var = var}, {.lk = lk}});
  join(tid1, NULL);
  join(tid2, NULL);
  printf("Value of var: %d\n", *var);
  if (*var != 2000000) {
    fprintf(2, "Expected: 2000000\nReturned: %d\n\n", *var);
    exit(1);
  }
  free(lk);
  free(var);

  printf("\nTEST PASSED\n");
  exit(0);
}
