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

  imm_offset = 0,
  exec_offset = 1,
};

#define word_backptr(hdr)                                                      \
  (void*)((uintptr_t)((hdr).backptr) & ~((uintptr_t)(1U << 2U) - 1U))
#define word_getbit(hdr, in)                                                   \
  (((uintptr_t)((hdr).backptr)) & ((uintptr_t)1 << (in)))
#define word_immediate(hdr) (word_getbit(hdr, 0U) != 0)
#define ptr_setbit(ptr, in, at)                                                \
  (void*)((uintptr_t)(ptr) | (uintptr_t)((in) != 0) << (uintptr_t)(at))

typedef char wordbuf[wordcap];
typedef wordbuf(*restrict wordbufptr);

static void
instr_ret(struct chirp_vm* in, void* operand)
{
  (void)operand;
  in->ip = chirp_rpop(*in);
}

[[gnu::hot]]
static void
instr_native(struct chirp_vm* in, chirp_foreign_fn fn)
{
  fn(in);
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

static void*
align(struct chirp_vm* restrict vm)
{
  chirp_here(*vm) += pref_align - ((uintptr_t)chirp_here(*vm) % pref_align);
  return chirp_here(*vm);
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

static int
isnum(wordbufptr restrict ptr)
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
chirp_find_word(struct chirp_vm* restrict vm, chirp_value const hashname)
{
  struct word_header* cur = vm->dict_head;
  while (cur && cur->hash != hashname)
    cur = word_backptr(*cur);

  return cur;
}

static int
interpret(register struct chirp_vm* restrict vm, wordbufptr buf)
{
  struct word_header const* word = chirp_find_word(vm, hash(buf));
  if (word) {
    chirp_rpush(*vm) = 0;
    vm->ip = word->instructions;
    while (vm->ip) {
      op_fn fn = vm->ip->fn;
      void* op = (char*)1 - 1 + vm->ip->operand;
      vm->ip++;
      fn(vm, op);
    }
  } else {
    if (!isnum(buf))
      return 0;

    chirp_push(*vm) = atoi(buf);
  }

  return 1;
}

static signed
compile(register struct chirp_vm* restrict vm, wordbufptr buf)
{
  if (strncmp(*buf, ";", wordcap) == 0)
    return 0;

  struct word_header* word = chirp_find_word(vm, hash(buf));
  struct chirp_instr* instr = (void*)chirp_here(*vm);
  chirp_allot(*vm, sizeof *instr);

  if (word) {
    if (word_immediate(*word)) {
      /* FIXME: runnet */
      __builtin_abort();
    } else {
      instr->fn = (op_fn)instr_subcall;
      instr->operand =
        (chirp_operand)((struct word_header const*)word)->instructions;
    }
  } else {
    if (!isnum(buf))
      return -1;
    chirp_number const num = atoi(buf);
    instr->fn = (op_fn)instr_push;
    instr->operand = (chirp_operand)num;
  }
  return 1;
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

    if (strncmp(buf, "'", wordcap) == 0) {
      idx += next_word(code + idx, len - idx, &buf);
      chirp_value const val = hash(&buf);
      struct word_header const* word = chirp_find_word(vm, val);
      if (!word)
        return 0;
      chirp_push(*vm) = (chirp_value)word;
      continue;
    }

    if (strncmp(buf, ":", wordcap) == 0) {
      idx += next_word(code + idx, len - idx, &buf);
      struct word_header* restrict const word = (void*)chirp_here(*vm);
      word->hash = hash(&buf);
      word->backptr = vm->dict_head;
      vm->dict_head = word;
      chirp_allot(*vm, sizeof(*word));
      signed retval = 0;
      do
        idx += next_word(code + idx, len - idx, &buf);
      while ((retval = compile(vm, &buf)));
      if (retval == -1)
        return 0;
      struct chirp_instr* retinstr = (void*)chirp_here(*vm);
      chirp_allot(*vm, sizeof *retinstr);
      retinstr->fn = (op_fn)instr_ret;
      retinstr->operand = 0;
      continue;
    }

    if (!interpret(vm, &buf))
      return 0;
  } while (1);

  return 1;
}

void
chirp_add_foreign(struct chirp_vm* restrict vm,
                  char const* name,
                  chirp_foreign_fn fn,
                  int immediate)
{
  wordbuf buf = { 0 };
  cstrtowordbuf(name, &buf);
  struct word_header* ptr = align(vm);
  ptr->hash = hash(&buf);
  ptr->backptr = ptr_setbit(vm->dict_head, immediate, 0);
  ptr->backptr = vm->dict_head;
  ptr->instructions[0].fn = (op_fn)instr_native;
  ptr->instructions[0].operand = (uintptr_t)fn;
  ptr->instructions[1].fn = (op_fn)instr_ret;
  ptr->instructions[1].operand = (uintptr_t)0;
  chirp_allot(*vm, sizeof *ptr + 2 * sizeof(struct chirp_instr));
  vm->dict_head = ptr;
}

/*

: add5 (n -- n + 5) 5 + ;
3 add5 ~display .

*/
