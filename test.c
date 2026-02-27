#include "chirp.h"
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>

// [[gnu::optimize("align-functions=8")]]
static void
add(struct chirp_vm* vm, void* unused)
{
  (void)unused;
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);

  chirp_push(*vm) = lhs + rhs;
}

static void
display(struct chirp_vm* vm, void* unused)
{
  (void)unused;
  chirp_value const what = chirp_pop(*vm);
  printf("NUM: %lu\n", chirp_to_num(what));
}

int
main()
{
  chirp_quickstart(vm);
  chirp_add_foreign(&vm, "+", add, 0);
  chirp_add_foreign(&vm, "display", display, 0);
  chirp_run(&vm, ": inner 2 ; : test inner inner + ; test display");
}
