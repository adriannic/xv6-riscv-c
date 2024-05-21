#include "user.h"

int thread_fn(void *args) {
  int value = *(int *)args;
  if (value & 1) {
    printf("This thread will call exit with code %d\n", value);
    exit(value);
  }
  printf("This thread will return with code %d\n", value);
  return value;
}

int main(void) {
  int expected_code_1 = 1;
  int expected_code_2 = 2;
  int code = 0;
  int tid;

  tid = clone(thread_fn, &expected_code_1);
  join(tid, &code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code_1) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code_1, code);
    exit(1);
  }

  expected_code_2 = 2;
  tid = clone(thread_fn, &expected_code_2);
  join(tid, &code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code_2) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code_2, code);
    exit(1);
  }

  printf("TEST PASSED\n");
  exit(0);
}
