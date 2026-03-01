#include "chirp.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
  inbuf_size = 128,
};

typedef char inbuf[inbuf_size];
typedef inbuf(*restrict inbufptr);

static void
display(chirp_vm_param vm)
{
  chirp_value const val = chirp_pop(*vm);
  printf("PRINT: %lu\n", val);
}

static void
displaycstr(chirp_vm_param vm)
{
  char const* str = chirp_popptr(*vm);
  chirp_value const len = chirp_pop(*vm);

  printf("PRINT: %.*s\n", len, str);
}

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

static void
load_prelude(chirp_vm_param vm)
{
  FILE* file = fopen("./prelude.4th", "r");
  fseek(file, 0, SEEK_END);
  unsigned const size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char* ptr = malloc(size + 1);
  (void)fread(ptr, 1, size, file);
  ptr[size] = 0;
  for (unsigned i = 0; i < size; i++)
    if (ptr[i] == '\n')
      ptr[i] = ' ';
  chirp_run(vm, ptr);
  fclose(file);
  free(ptr);
}

int
main()
{
  chirp_quickstart(vm);
  chirp_add_foreign(&vm, "display", display, 0);
  chirp_add_foreign(&vm, "displaycstr", displaycstr, 0);

  load_prelude(&vm);

  /*
    : var CREATE derefinstr , 0 , drop ;
  */

  // assert(chirp_run(&vm,
  //                  ": OK 0 ;"
  //                  "8 allocate ' OK !"
  //                  "OK @ 0 ->@"));

  // chirp_value const volatile* ok = (chirp_value const
  // volatile*)chirp_pop(vm);

  inbuf buf;
  while (get_input(&buf)) {
    if (buf[0] == 0)
      break;
    chirp_reset(vm);

    if (!chirp_run(&vm, buf)) {
      printf("compile failure.\n");
      continue;
    }

    chirp_value* val = vm.stack;
    while (val-- > vm.stack_start)
      printf("  %lu\n", *val);

    // chirp_value val = *ok;
    printf("ok.\n");
  }
}
