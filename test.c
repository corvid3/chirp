#include "chirp.h"
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

// [[gnu::optimize("align-functions=8")]]
static void
add(struct chirp_vm* vm)
{
  chirp_value const rhs = chirp_pop(vm);
  chirp_value const lhs = chirp_pop(vm);

  enum chirp_type const rhs_type = chirp_type(rhs);
  enum chirp_type const lhs_type = chirp_type(lhs);

  if (rhs_type != chirp_num)
    exit(1);
  if (lhs_type != chirp_num)
    exit(1);

  chirp_push(vm, chirp_from_num(chirp_to_num(lhs) + chirp_to_num(rhs)));
}

static void
display(struct chirp_vm* vm)
{
  chirp_value const what = chirp_pop(vm);
  enum chirp_type const type = chirp_type(what);
  if (type == chirp_num)
    printf("NUM: %lu\n", chirp_to_num(what));
  else
    exit(1);
}

static void
alloc(struct chirp_vm* vm)
{
  chirp_value const what = chirp_pop(vm);
  unsigned size = chirp_to_num(what);
  chirp_push(vm, chirp_from_ptr(malloc(size)));
}

int
main()
{
  chirp_quickstart(vm);
  chirp_add_foreign(&vm, "+", add);
  chirp_add_foreign(&vm, "display", display);
  chirp_run(&vm, ": test 6 ;");
  chirp_run(&vm, ": add5 5 + ;");
  chirp_run(&vm, "test add5 display");
  chirp_run(&vm, "5 = if 2 else 3 fi");
  chirp_run(&vm, ": ^3 3 !dup ;");
  chirp_run(&vm, ": ^2 2 !dup ;");
  chirp_run(
    &vm,
    ": for (n to proc) n ^2 = if else memorize ^3 $ dropmem 1 sub for then;");
  chirp_run(&vm, ": someProc ; ")
}
