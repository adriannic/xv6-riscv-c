#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "../user/user.h"

int thread_fn(void *args) {
  sleep(100);
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

  clone(thread_fn, &expected_code_1);
  // wait(&code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code_1) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code_1, code);
    exit(1);
  }
  sleep(10);

  expected_code_2 = 2;
  clone(thread_fn, &expected_code_2);
  // wait(&code);
  printf("Thread exit code: %d\n", code);
  if (code != expected_code_2) {
    fprintf(2, "Expected: %d\nReturned: %d\n\n", expected_code_2, code);
    exit(1);
  }
  sleep(10);

  printf("TEST PASSED\n");
  exit(0);
}