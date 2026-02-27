#include "chirp.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// [[gnu::optimize("align-functions=8")]]
static void
add(struct chirp_vm* vm)
{
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);

  chirp_push(*vm) = lhs + rhs;
}

static void
display(struct chirp_vm* vm)
{
  chirp_value const what = chirp_pop(*vm);
  printf("NUM: %lu\n", chirp_to_num(what));
}

static void
allocate(struct chirp_vm* vm)
{
  chirp_value const what = chirp_pop(*vm);
  void* ptr = chirp_here(*vm);
  chirp_allot(*vm, what);
  chirp_push(*vm) = (chirp_value)ptr;
}

static void
unalloc(struct chirp_vm* vm)
{
  chirp_value const what = chirp_pop(*vm);
  chirp_allot(*vm, -what);
}

static void
getstr(struct chirp_vm* vm)
{
  void* const strptr = (void*)chirp_pop(*vm);
  strcpy(strptr, "hello, world!\n");
}

static void
dup(struct chirp_vm* vm)
{
  chirp_push(*vm) = chirp_top(*vm);
}

static void
pstr(struct chirp_vm* vm)
{
  void* const strptr = (void*)chirp_pop(*vm);
  printf("%s", (char const*)strptr);
}

/* (name bytesize -- ) */
static void
varset(struct chirp_vm* vm)
{
  chirp_value const name = *(chirp_value*)chirp_here(*vm);
  struct word_header* word = chirp_find_word(vm, name);
  assert(word);
  word->instructions[0].operand = chirp_pop(*vm);
}

enum
{
  inbuf_size = 128,
};

typedef char inbuf[inbuf_size];
typedef inbuf(*restrict inbufptr);

static int
get_input(inbufptr out)
{
  int i = 0;
  int c = 0;
  while ((c = getchar()) && i < (inbuf_size - 1)) {
    if (c == EOF)
      return 0;
    if (c == '\n')
      break;
    (*out)[i++] = (char)c;
  }
  (*out)[i] = 0;
  return 1;
}

int
main()
{
  chirp_quickstart(vm);
  chirp_add_foreign(&vm, "+", add, 0);
  chirp_add_foreign(&vm, "dup", dup, 0);
  chirp_add_foreign(&vm, "display", display, 0);
  chirp_add_foreign(&vm, "allocate", allocate, 0);
  chirp_add_foreign(&vm, "unalloc", unalloc, 0);
  chirp_add_foreign(&vm, "getstr", getstr, 0);
  chirp_add_foreign(&vm, "pstr", pstr, 0);
  chirp_add_foreign(&vm, "!", varset, 0);

  inbuf buf;
  while (get_input(&buf)) {
    if (buf[0] == 0)
      break;
    chirp_reset(vm);

    if (!chirp_run(&vm, buf))
      printf("failed.\n");
    else
      printf("ok.\n");
  }

  // if (!chirp_run(&vm, "24 allocate dup getstr pstr 24 unalloc"))
  //   printf("failed\n");
}
