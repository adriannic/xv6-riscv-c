#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

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
  int expected_code;
  int code;

  expected_code = 1;
  clone(thread_fn, &expected_code);
  wait(&code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code, code);
    exit(1);
  }

  expected_code = 2;
  clone(thread_fn, &expected_code);
  wait(&code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code, code);
    exit(1);
  }

  printf("TEST PASSED\n");
  exit(0);
}
