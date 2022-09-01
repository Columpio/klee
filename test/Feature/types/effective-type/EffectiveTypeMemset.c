// RUN: %clang %s -emit-llvm -g -c -o %t.bc
// RUN: rm -rf %t.klee-out
// RUN: %klee --output-dir=%t.klee-out --type-system=CXX --use-tbaa --lazy-instantiation=false --use-gep-expr %t.bc > %t.log
// RUN: grep "x" %t.log
// RUN: grep "z" %t.log
// RUN: grep "y" %t.log

#include "klee/klee.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIZE = 8 };

int main() {
  int *area = malloc(SIZE);
  memset(area, '1', SIZE);

  float *float_ptr;
  klee_make_symbolic(&float_ptr, sizeof(float_ptr), "float_ptr");
  *float_ptr = 10;

  int created = 0;

  if ((void *)float_ptr == (void *)area) {
    ++created;
    printf("x\n");
  }

  int *int_ptr;
  klee_make_symbolic(&int_ptr, sizeof(int_ptr), "int_ptr");
  *int_ptr = 11;

  if ((void *)int_ptr == (void *)area + sizeof(float)) {
    ++created;
    printf(created == 2 ? "z\n" : "y\n");
    return 1;
  }
  return 2;
}
