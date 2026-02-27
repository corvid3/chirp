#include "chirp.h"
#include <stdio.h>

#ifndef __gnu_linux__
static int
strncmp(char const* restrict lhs, char const* restrict rhs, size_t const len)
{
  for (unsigned i = 0; i < len; i++) {
    char const lhsc = lhs[i];
    char const rhsc = rhs[i];
    if ((unsigned)lhsc ^ (unsigned)!rhsc)
      return 0;
    if (lhsc != rhsc)
      return 0;
  }

  return 1;
}

static size_t
strlen(char const* restrict in)
{
  size_t out = 0;
  while (in[out++])
    ;
  return out;
}
#else
#define strncmp __builtin_strncmp
#define strlen __builtin_strlen
#define memcpy __builtin_memcpy
#endif

#define forword(word, iter, c)                                                 \
  for (unsigned i = 0, (c) = ((unsigned)(word)[i]); i < wordcap && (word)[i];  \
       (c) = (unsigned)(word)[i++])

enum
{
  wordcap = 16,
};

typedef char wordbuf[wordcap];
typedef wordbuf(*wordbufptr);

static inline int
word_is_free(struct chirp_word const* restrict in)
{
  return in->hash == (chirp_value)-1;
}

static void
cstrtowordbuf(char const* in, wordbufptr restrict out)
{
  for (unsigned i = 0; i < wordcap && in[i]; i++)
    (*out)[i] = in[i];
}

static unsigned
next_word(char const* restrict in, unsigned const len, wordbufptr out)
{
  unsigned i = 0;
  for (; i < wordcap && i < len && !__builtin_isspace(in[i]); i++)
    (*out)[i] = in[i];

  if (i != wordcap)
    (*out)[i] = 0;

  while (i < len && __builtin_isspace(in[i]))
    i++;

  return i;
}

static int
is_number(wordbufptr restrict ptr)
{
  forword(*ptr, i, c) if (!__builtin_isdigit(c)) return 0;
  return 1;
}

static signed
atoi(wordbufptr restrict ptr)
{
  enum
  {
    base = 10
  };

  signed out = 0;
  for (unsigned i = 0, (c) = ((unsigned)(*ptr)[i]); i < wordcap && (*ptr)[i];
       (c) = (unsigned)(*ptr)[i++])
    out *= base, out += (signed)(c - '0');

  return out;
}

chirp_value
chirp_pop(struct chirp_vm* restrict vm)
{
  return vm->stack.ptr[--vm->stack.size];
}

void
chirp_push(struct chirp_vm* restrict vm, chirp_value val)
{
  vm->stack.ptr[vm->stack.size++] = val;
}

/* jenkins one-at-a-time hash */
static chirp_value
hash(wordbufptr restrict ptr)
{
  // enum : unsigned int
  // {
  //   magic_0 = 10,
  //   magic_1 = 6,
  //   magic_2 = 3,
  //   magic_3 = 11,
  //   magic_4 = 15
  // };

  // chirp_value hash = 0;

  // forword(*ptr, i, c)
  // {
  //   hash += c;
  //   hash += hash << magic_0;
  //   hash ^= hash >> magic_1;
  // }

  // hash += hash << magic_2;
  // hash ^= hash >> magic_3;
  // hash += hash << magic_4;
  // if (hash == (chirp_value)-1)
  //   hash -= 1;
  // return hash;

  enum
  {
    poly = 31
  };
  chirp_value out = 0;
  for (unsigned i = 0, (c) = ((unsigned)(*ptr)[i]); i < wordcap && (*ptr)[i];
       (c) = (unsigned)(*ptr)[i++])
    out += out * poly + c;
  out &= chirp_data_mask;
  // if (out == (chirp_value)-1 || out == (chirp_value)-2)
  //   out -= 2;
  return out;
}

static unsigned
next_forwarding_free(struct chirp_vm* restrict vm)
{
  for (; !word_is_free(&vm->heap.ptr[vm->heap.forward_free]);
       vm->heap.forward_free++)
    ;
  return vm->heap.forward_free;
}

static void
collect(struct chirp_vm* restrict vm)
{
  unsigned newsize = vm->heap.size;

  for (unsigned i = 0; i < vm->heap.size; i++) {
    struct chirp_word* restrict word = &vm->heap.ptr[i];
    enum chirp_type const word_type = chirp_type(word->value);

    /* procs are never destroyed unless asked for */
    if (word_type == chirp_chain || word_type == chirp_fnptr)
      continue;

    word->hash = -1U;
    newsize -= 1;
  }

  /* we ran out of memory */
  if (newsize == vm->heap.size && newsize == vm->heap.capacity)
    __builtin_abort();

  /* find the first gap in the new heap */
  unsigned first_gap = 0;
  for (; first_gap < vm->heap.size; first_gap++)
    if (word_is_free(&vm->heap.ptr[first_gap]))
      break;

  /* if nothing was freed in the middle, then theres nothing to compact */
  if (first_gap == vm->heap.size)
    return;

  /* compute forwarding addresses */
  vm->heap.forward_free = first_gap;
  for (unsigned i = 0; i < vm->heap.capacity; i++) {
    struct chirp_word* restrict word = &vm->heap.ptr[i];
    if (word_is_free(word))
      continue;
    word->forwarding_address = next_forwarding_free(vm);
  }

  /* compact */
  for (unsigned i = 0; i < vm->heap.capacity; i++) {
    struct chirp_word* restrict word = &vm->heap.ptr[i];

    if (word_is_free(word))
      continue;

    enum chirp_type const word_type = chirp_type(word->value);

#define slotsize (chirp_pair_size / 2)
#define slotmask ((1U << slotsize) - 1U)
#define car(value) (((value) >> slotsize) & slotmask)
#define cdr(value) ((value) & ((1U << slotsize) - 1U))
#define topair(car, cdr) ((car) << slotsize | ((cdr) & slotmask))

    switch (word_type) {
      case chirp_ref:
      case chirp_num:
      case chirp_fnptr:
      case chirp_str:
      case chirp_foreign:
        break;

      case chirp_chain: {
        chirp_value const num = chirp_to_num(word->value);
        chirp_value const car = car(num);
        chirp_value const cdr = cdr(num);

        chirp_value const car_fwd = vm->heap.ptr[car].forwarding_address;
        chirp_value const cdr_fwd = vm->heap.ptr[cdr].forwarding_address;

        chirp_value const new_num = chirp_from_num(topair(car_fwd, cdr_fwd));
        word->value = new_num;
      } break;

      case chirp_sentinel:
        __builtin_abort();
        break;
    }

    memcpy(&vm->heap.ptr[word->forwarding_address], word, sizeof(*word));
    word->hash = -1U;
  }

  vm->heap.size = newsize;
}

/* 0 creates an unnamed word chain */
static struct chirp_word*
add_word(struct chirp_vm* restrict vm, wordbufptr name)
{
  chirp_value const hashname = (name == 0) ? -2U : hash(name);
  if (vm->heap.size + 1 >= vm->heap.capacity)
    collect(vm);
  struct chirp_word* restrict new_word = &vm->heap.ptr[vm->heap.size++];
  new_word->hash = hashname;
  return new_word;
}

struct chirp_word*
find_word(struct chirp_vm* restrict vm, chirp_value const hashname)
{
  for (unsigned i = 0; i < vm->heap.size; i++)
    if (vm->heap.ptr[i].hash == hashname)
      return &vm->heap.ptr[i];

  return 0;
}

static void
remover(struct chirp_vm* restrict /*unused*/)
{
}

void
chirp_init(struct chirp_vm* restrict vm,
           chirp_value* stack,
           unsigned stack_capacity,
           struct chirp_word* heap_ptr,
           unsigned heap_capacity,
           chirp_allocate allocator)
{
  vm->heap.ptr = heap_ptr;
  vm->heap.capacity = heap_capacity;
  vm->heap.size = 0;

  vm->stack.ptr = stack;
  vm->stack.size = 0;
  vm->stack.capacity = stack_capacity;

  vm->wordstack.size = 0;

  vm->allocator = allocator;

  chirp_add_foreign(vm, "remove", remover);
}

void
chirp_uninit(struct chirp_vm* restrict vm)
{
  /* actually free things that need to be freed */
  vm->heap.ptr = 0;
  vm->heap.capacity = 0;
  vm->stack.ptr = 0;
  vm->stack.capacity = 0;
}

static void
dispatch(struct chirp_vm* restrict vm, chirp_value value)
{
  enum chirp_type const type = chirp_type(value);

begin:
  switch (type) {
    case chirp_str:
    case chirp_num:
      chirp_push(vm, value);
      break;

    case chirp_ref: {
      struct chirp_word* word = find_word(vm, chirp_to_num(value));
      value = word->value;
      goto begin;
    }
    case chirp_chain:
      if (vm->wordstack.size >= chirp_max_exec_stack)
        __builtin_abort();
      vm->wordstack.ptr[vm->wordstack.size++] =
        vm->heap.ptr[chirp_to_num(value)].value;
      break;

    case chirp_fnptr:
      ((chirp_foreign_fn)chirp_to_ptr(value))(vm);
      break;

    case chirp_foreign:
    case chirp_sentinel:
      __builtin_abort();
      break;
  }
}

static chirp_value
parse(wordbufptr restrict ptr)
{
  if (is_number(ptr))
    return chirp_from_num((chirp_value)atoi(ptr));

  chirp_value const out = chirp_from_ref((chirp_value)hash(ptr));
  return out;
}

int
chirp_run(struct chirp_vm* restrict vm, char const* code)
{
  unsigned idx = 0;
  unsigned const len = strlen(code);

  wordbuf buf = { 0 };

  do {
    if (len - idx == 0)
      break;

    idx += next_word(code + idx, len - idx, &buf);
    if (strncmp(buf, ".", wordcap) == 0)
      break;

    if (strncmp(buf, ":", wordcap) == 0) {
      idx += next_word(code + idx, len - idx, &buf);

      struct chirp_word* word = add_word(vm, &buf);
      struct chirp_word* cons = 0;
      unsigned previdx = -1U;

      while (1) {
        idx += next_word(code + idx, len - idx, &buf);
        if (strncmp(buf, ";", wordcap) == 0)
          break;

        /* create an unnamed word */
        struct chirp_word* car = add_word(vm, 0);
        car->value = parse(&buf);
        cons = add_word(vm, 0);
        unsigned caridx = car - vm->heap.ptr;
        cons->value = chirp_from_pair(topair(caridx, previdx));
        unsigned recona = car(cons->value);
        unsigned reconb = car(topair(caridx, previdx));
        unsigned reconc = car(chirp_from_pair(topair(caridx, previdx)));
        (void)recona;
        (void)reconb;
        (void)reconc;

        previdx = cons - vm->heap.ptr;
      }

      word->value = previdx;

      continue;
    }

    dispatch(vm, parse(&buf));

    while (vm->wordstack.size != 0) {
      chirp_value val = vm->wordstack.ptr[vm->wordstack.size - 1];
      while (1) {
        volatile chirp_value const cari = car(val);
        volatile chirp_value const cdri = cdr(val);
        chirp_value const car = vm->heap.ptr[cari].value;
        dispatch(vm, car);
        if (cdri == cdr(-1U))
          break;
        val = vm->heap.ptr[cdri].value;
      }
      vm->wordstack.size--;
    }
  } while (1);

  return 1;
}

void
chirp_add_foreign(struct chirp_vm* restrict vm,
                  char const* name,
                  chirp_foreign_fn fn)
{
  wordbuf buf = { 0 };
  cstrtowordbuf(name, &buf);
  struct chirp_word* word = add_word(vm, &buf);
  word->value = chirp_from_foreign(fn);
}

/*

: add5 (n -- n + 5) 5 + ;
3 add5 ~display .

*/
