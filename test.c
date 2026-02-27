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

  chirp_number n = rhs >> chirp_extra_width;

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

int
main()
{
  static chirp_value stack[chirp_reccomended_stack_size];
  static struct chirp_word heap[chirp_reccomended_heap_size];
  struct chirp_vm vm;

  chirp_init(&vm,
             stack,
             chirp_reccomended_stack_size,
             heap,
             chirp_reccomended_heap_size,
             0);

  chirp_add_foreign(&vm, "+", add);
  chirp_add_foreign(&vm, "display", display);
  chirp_run(&vm, ": test 8 ; .");
  chirp_run(&vm, "test display .");
}
