#include "chirp.h"

#ifdef __GNUC__
#define strncmp __builtin_strncmp
#define strlen __builtin_strlen
#define memcpy __builtin_memcpy
#define isspace __builtin_isspace
#define isdigit __builtin_isdigit
#else
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

static int
isspace(char const in)
{
  return 0;
}

static int
isdigit(char const in)
{
  return 0;
}
#endif

#define forword(word, iter, c)                                                 \
  for (unsigned i = 0, (c) = ((unsigned)(word)[i]); i < wordcap && (word)[i];  \
       (c) = (unsigned)(word)[i++])

enum : unsigned int
{
  native_marker = -1U,
  native_immediate_marker = -2U,
  pref_align = _Alignof(chirp_value),
  wordcap = 16,
};

typedef chirp_value chirp_operand;
typedef void (*op_fn)(struct chirp_vm* vm, void* operand);

struct chirp_instr
{
  op_fn fn;
  chirp_operand operand;
};

struct word_header
{
  chirp_value hash;
  void* backptr;
  /* FIXME: don't actually need this. overload the hash value later. */
  /* if -1U, then this is a struct dict_entry_native
   * if -2U, then this is also a native fn, but is an immediate */
  unsigned num_instructions;
};

struct word_native
{
  struct word_header header;
  chirp_foreign_fn fn;
};

struct word_proc
{
  struct word_header header;
  struct chirp_instr instructions[];
};

typedef char wordbuf[wordcap];
typedef wordbuf(*wordbufptr);

static void
instr_ret(struct chirp_vm* in, void* operand)
{
  (void)operand;
  in->ip = chirp_rpop(*in);
}

static void
instr_push(struct chirp_vm* in, void* operand)
{
  chirp_push(*in) = (chirp_number)operand;
}

static inline void
instr_subcall(struct chirp_vm* in, struct chirp_instr const* operand)
{
  *(in->retstack++) = in->ip;
  in->ip = operand;
}

static inline void
alloc8(struct chirp_vm* restrict vm, chirp_value const size)
{
  vm->heap += size;
}

static void*
align(struct chirp_vm* restrict vm)
{
  chirp_here(vm) += pref_align - ((uintptr_t)chirp_here(vm) % pref_align);
  return chirp_here(vm);
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
  for (; i < wordcap && i < len && !isspace(in[i]); i++)
    (*out)[i] = in[i];

  if (i != wordcap)
    (*out)[i] = 0;

  while (i < len && isspace(in[i]))
    i++;

  return i;
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

/* jenkins one-at-a-time hash */
static chirp_value
hash(wordbufptr restrict ptr)
{
  enum
  {
    poly = 31
  };
  chirp_value out = 0;
  for (unsigned i = 0, (c) = ((unsigned)(*ptr)[i]); i < wordcap && (*ptr)[i];
       (c) = (unsigned)(*ptr)[i++])
    out += out * poly + c;
  if (out == (chirp_value)-1 || out == (chirp_value)-2)
    out -= 2;
  return out;
}

struct word_header*
find_word(struct chirp_vm* restrict vm, chirp_value const hashname)
{
  struct word_header* cur = vm->dict_head;
  while (cur && cur->hash != hashname)
    cur = cur->backptr;

  return cur;
}

int
chirp_run(register struct chirp_vm* restrict vm, char const* code)
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
      struct word_proc* restrict const word = (void*)chirp_here(vm);
      word->header.hash = hash(&buf);
      word->header.num_instructions = 0;
      word->header.backptr = vm->dict_head;
      vm->dict_head = word;
      alloc8(vm, sizeof(*word));
      goto compile;
    compileret:;
      struct chirp_instr* end = (void*)chirp_here(vm);
      word->header.num_instructions = end - word->instructions;
      continue;
    }

    goto interpret;
  interpretret:;

  } while (1);

  return 1;

compile:
  while (1) {
    idx += next_word(code + idx, len - idx, &buf);
    if (strncmp(buf, ";", wordcap) == 0)
      break;

    struct word_header* word = find_word(vm, hash(&buf));
    struct chirp_instr* instr = (void*)chirp_here(vm);
    alloc8(vm, sizeof *instr);

    if (word) {
      if (word->num_instructions == native_immediate_marker) {
        /* FIXME: runnet */
      } else if (word->num_instructions == native_marker) {
        instr->fn = ((struct word_native const*)word)->fn;
        instr->operand = 0;
      } else {
        instr->fn = (op_fn)instr_subcall;
        instr->operand =
          (chirp_operand)((struct word_proc const*)word)->instructions;
      }
    } else {
      chirp_number const num = atoi(&buf);
      instr->fn = (op_fn)instr_push;
      instr->operand = (chirp_operand)num;
    }
  }

  struct chirp_instr* retinstr = (void*)chirp_here(vm);
  alloc8(vm, sizeof *retinstr);
  retinstr->fn = (op_fn)instr_ret;
  retinstr->operand = 0;

  goto compileret;

interpret:;
  struct word_header const* word = find_word(vm, hash(&buf));
  if (word) {
    /* goofy casting workaround to fall within standard constraints */
    if (word->num_instructions == native_marker) {
      struct word_native const* native = (void const*)word;
      native->fn(vm, 0);
    } else {
      struct word_proc const* proc = (void const*)word;
      chirp_rpush(*vm) = 0;
      vm->ip = proc->instructions;
      while (vm->ip) {
        op_fn fn = vm->ip->fn;
        void* op = (char*)1 - 1 + vm->ip->operand;
        vm->ip++;
        fn(vm, op);
      }
    }
  } else {
    chirp_push(*vm) = atoi(&buf);
  }

  goto interpretret;
}

void
chirp_add_foreign(struct chirp_vm* restrict vm,
                  char const* name,
                  chirp_foreign_fn fn,
                  int immediate)
{
  wordbuf buf = { 0 };
  cstrtowordbuf(name, &buf);
  struct word_native* ptr = align(vm);
  ptr->header.hash = hash(&buf);
  ptr->header.num_instructions =
    immediate ? native_immediate_marker : native_marker;
  ptr->header.backptr = vm->dict_head;
  ptr->fn = fn;
  alloc8(vm, sizeof *ptr);
  vm->dict_head = ptr;
}

/*

: add5 (n -- n + 5) 5 + ;
3 add5 ~display .

*/
