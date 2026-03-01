#include "chirp.h"

#define MIN(x, y) ((x < y) ? x : y)

#ifdef __GNUC__
#define strncmp __builtin_strncmp
#define strlen __builtin_strlen
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
#define bit(in, at) (((uintptr_t)((in) != 0U)) << (at))
#define ptr_setbit(ptr, in, at)                                                \
  (void*)((((uintptr_t)(ptr)) & ~(bit(in, at))) | bit(in, at))

[[gnu::hot]]
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

[[gnu::hot]]
static void
instr_push(struct chirp_vm* in, void* operand)
{
  chirp_push(*in) = (chirp_number)operand;
}

[[gnu::hot]]
static void
instr_deref(struct chirp_vm* in, void* operand)
{
  chirp_push(*in) = *(chirp_number*)operand;
}

[[gnu::hot]]
static inline void
instr_subcall(struct chirp_vm* in, struct chirp_instr const* operand)
{
  *(in->retstack++) = in->ip;
  in->ip = operand;
}

[[gnu::hot]]
static inline void
instr_branch(chirp_vm_param in, struct chirp_instr const* operand)
{
  if (chirp_pop(*in) == 0)
    in->ip = operand;
}

[[gnu::hot]]
static inline void
instr_jump(chirp_vm_param vm, struct chirp_instr const* operand)
{
  vm->ip = operand;
}

static void
align(chirp_vm_param vm)
{
  chirp_here(*vm) +=
    (pref_align - ((uintptr_t)chirp_here(*vm) % pref_align)) % pref_align;
}

/* jenkins one-at-a-time hash */
[[gnu::hot]]
static inline chirp_value
hash(char const* ptr, unsigned const length)
{
  enum
  {
    poly = 31
  };
  chirp_value out = 0;
  for (unsigned i = 0; i < length; i++)
    out += out * poly + ptr[i];
  return out;
}

static void
here(chirp_vm_param vm)
{
  chirp_push(*vm) = (chirp_value)vm->sbreak;
}

static void
cell_deref(chirp_vm_param vm)
{
  chirp_value const* const ptr = chirp_popptr(*vm);
  chirp_push(*vm) = *ptr;
}

static void
byte_deref(chirp_vm_param vm)
{
  char const* const ptr = chirp_popptr(*vm);
  chirp_push(*vm) = (chirp_value)*ptr;
}

static void
cell_set(chirp_vm_param vm)
{
  chirp_value* restrict const ptr = chirp_popptr(*vm);
  chirp_value const value = chirp_pop(*vm);
  *ptr = value;
}

static void
byte_set(chirp_vm_param vm)
{
  char* restrict const ptr = chirp_popptr(*vm);
  char const value = chirp_pop(*vm);
  *ptr = value;
}

static void
skipspace(chirp_vm_param vm)
{
  unsigned i = 0;
  while (vm->parsing_idx + i < vm->input_buffer_length &&
         isspace(vm->input_buffer[vm->parsing_idx + i]))
    i++;
  vm->parsing_idx += i;
}

/* ( -- len ptr ) */
static void
parsews(chirp_vm_param vm)
{
  unsigned i = 0;
  while (vm->parsing_idx + i < vm->input_buffer_length &&
         !isspace(vm->input_buffer[vm->parsing_idx + i]))
    i++;

  chirp_push(*vm) = i;
  chirp_push(*vm) = (uintptr_t)(vm->input_buffer + vm->parsing_idx);

  vm->parsing_idx += i;
}

/* ( -- size ptr ) */
static void
parse(chirp_vm_param vm)
{
  char const delim = chirp_pop(*vm);
  unsigned i = 0;
  while (vm->parsing_idx + i < vm->input_buffer_length &&
         vm->input_buffer[vm->parsing_idx + i] != delim)
    i++;

  chirp_push(*vm) = i;
  chirp_push(*vm) = (uintptr_t)(vm->input_buffer + vm->parsing_idx);

  vm->parsing_idx += i;
  if (vm->parsing_idx < vm->input_buffer_length &&
      vm->input_buffer[vm->parsing_idx] == delim)
    vm->parsing_idx++;
}

static inline void
run(chirp_vm_param vm, struct chirp_instr const* restrict const starting_instr)
{
  chirp_rpush(*vm) = 0;
  vm->ip = starting_instr;
  while (vm->ip) {
    op_fn fn = vm->ip->fn;
    void* op = (char*)1 - 1 + vm->ip->operand;
    vm->ip++;
    fn(vm, op);
  }
}

/* ( len ptr -- hash ) */
static void
stackhash(chirp_vm_param vm)
{
  char const* ptr = chirp_popptr(*vm);
  unsigned const len = chirp_pop(*vm);
  chirp_push(*vm) = hash(ptr, len);
}

static void
cells(chirp_vm_param vm)
{
  chirp_push(*vm) = sizeof(chirp_value);
}

static void
dup(chirp_vm_param vm)
{
  chirp_value const val = chirp_pop(*vm);
  chirp_push(*vm) = val;
  chirp_push(*vm) = val;
}

static void
over(chirp_vm_param vm)
{
  chirp_value const top = chirp_pop(*vm);
  chirp_value const bottom = chirp_pop(*vm);
  chirp_push(*vm) = bottom;
  chirp_push(*vm) = top;
  chirp_push(*vm) = bottom;
}

static void
rot(chirp_vm_param vm)
{
  chirp_value const c = chirp_pop(*vm);
  chirp_value const b = chirp_pop(*vm);
  chirp_value const a = chirp_pop(*vm);
  chirp_push(*vm) = b;
  chirp_push(*vm) = c;
  chirp_push(*vm) = a;
}

static void
over2(chirp_vm_param vm)
{
  over(vm);
  over(vm);
}

static void
swap(chirp_vm_param vm)
{
  chirp_value const top = chirp_pop(*vm);
  chirp_value const bot = chirp_pop(*vm);
  chirp_push(*vm) = top;
  chirp_push(*vm) = bot;
}

static void
drop(chirp_vm_param vm)
{
  (void)chirp_pop(*vm);
}

static void
drop2(chirp_vm_param vm)
{
  (void)chirp_pop(*vm);
  (void)chirp_pop(*vm);
}

static void
atoi(chirp_vm_param vm)
{
  enum
  {
    base = 10,
  };

  char const* ptr = chirp_popptr(*vm);
  unsigned const len = chirp_pop(*vm);

  if (len == 0)
    return (void)(chirp_push(*vm) = 0);

  chirp_value out = 0;

  int const isneg = (*ptr == '-');
  for (unsigned i = isneg; i < len; i++)
    out *= base, out += (signed)(ptr[i] - '0');

  chirp_push(*vm) = out;
}

static void
makewordhdr(chirp_vm_param vm)
{
  skipspace(vm);
  chirp_push(*vm) = ' ';
  parse(vm);
  stackhash(vm);
  chirp_value const hv = chirp_pop(*vm);

  align(vm);
  struct word_header* alloc = (void*)chirp_here(*vm);
  chirp_push(*vm) = (uintptr_t)alloc;
  alloc->hash = hv;
  alloc->backptr = vm->dict_head;
  chirp_allot(*vm, sizeof *alloc);
}

static void
endwordhdr(chirp_vm_param vm)
{
  struct word_header* hdr = chirp_popptr(*vm);
  vm->dict_head = hdr;
  struct chirp_instr* instr = (void*)chirp_here(*vm);
  instr->fn = instr_ret;
  instr->operand = 0;
  chirp_allot(*vm, sizeof *instr);
}

/* immediate word, ends parsing */
static void
semicolon(chirp_vm_param vm)
{
  endwordhdr(vm);
  vm->compiling = 0;
}

static void
create(chirp_vm_param vm)
{
  makewordhdr(vm);
  struct chirp_instr* instr = (void*)chirp_here(*vm);
  instr->fn = instr_push;
  chirp_allot(*vm, sizeof *instr);
  endwordhdr(vm);
  instr->operand = (chirp_operand)chirp_here(*vm);
}

void
does(chirp_vm_param vm)
{
  if (vm->compiling) {
    struct chirp_instr* jank = (void*)chirp_here(*vm);
    chirp_allot(*vm, sizeof *jank);
    jank->fn = (op_fn)instr_native;
    jank->operand = (chirp_value)does;
    dup(vm);
    endwordhdr(vm);
  } else {
    vm->dict_head->instructions[1].fn = (op_fn)instr_jump;
    vm->dict_head->instructions[1].operand = (chirp_value)(vm->ip + 1);
    vm->ip = chirp_rpop(*vm);
  }
}

static void
colon(chirp_vm_param vm)
{
  vm->compiling = 1;
  makewordhdr(vm);
}

static void
immediate(chirp_vm_param vm)
{
  vm->dict_head->backptr = ptr_setbit(vm->dict_head->backptr, 1U, 0U);
}

static void
comma(chirp_vm_param vm)
{
  *(chirp_value*)chirp_here(*vm) = chirp_pop(*vm);
  chirp_allot(*vm, sizeof(chirp_value));
}

static void
source(chirp_vm_param vm)
{
  chirp_push(*vm) = vm->input_buffer_length;
  chirp_push(*vm) = (chirp_value)vm->input_buffer;
}

static void
sub(chirp_vm_param vm)
{
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);
  chirp_push(*vm) = lhs - rhs;
}

static void
add(chirp_vm_param vm)
{
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);
  chirp_push(*vm) = lhs + rhs;
}

static void
mul(chirp_vm_param vm)
{
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);
  chirp_push(*vm) = lhs * rhs;
}

static void
mod(chirp_vm_param vm)
{
  chirp_value const rhs = chirp_pop(*vm);
  chirp_value const lhs = chirp_pop(*vm);
  chirp_push(*vm) = lhs % rhs;
}

static void
branch(chirp_vm_param vm)
{
  struct chirp_instr* cur = (void*)chirp_here(*vm);
  cur->fn = (op_fn)instr_jump;
  cur->operand = 0;
  chirp_allot(*vm, sizeof *cur);
}

static void
branch0(chirp_vm_param vm)
{
  struct chirp_instr* cur = (void*)chirp_here(*vm);
  cur->fn = (op_fn)instr_branch;
  cur->operand = 0;
  chirp_allot(*vm, sizeof *cur);
}

/* ( instrPtr to -- ) */
static void
resLink(chirp_vm_param vm)
{
  struct chirp_instr* to = chirp_popptr(*vm);
  struct chirp_instr* cur = chirp_popptr(*vm);
  cur->operand = (chirp_value)to;
}

static void
quote(chirp_vm_param vm)
{
  skipspace(vm);
  chirp_push(*vm) = ' ';
  parse(vm);
  stackhash(vm);
  struct word_header const* word = chirp_find_word(vm, chirp_pop(*vm));
  if (vm->compiling) {
    struct chirp_instr* instr = (void*)chirp_here(*vm);
    instr->fn = (op_fn)instr_push;
    instr->operand = (chirp_operand)word->instructions;
    chirp_allot(*vm, sizeof *instr);
  } else {
    chirp_push(*vm) = (chirp_value)word->instructions;
  }
}

static void
postpone2(chirp_vm_param vm)
{
  chirp_value const xt = chirp_pop(*vm);
  struct chirp_instr* this = (void*)chirp_here(*vm);
  this->fn = (op_fn)instr_subcall;
  this->operand = (uintptr_t)xt;
  chirp_allot(*vm, sizeof *this);
}

static void
postpone(chirp_vm_param vm)
{
  skipspace(vm);
  chirp_push(*vm) = ' ';
  parse(vm);
  stackhash(vm);
  struct word_header const* word = chirp_find_word(vm, chirp_pop(*vm));

  struct chirp_instr* this = (void*)chirp_here(*vm);
  this->fn = (op_fn)instr_push;
  this->operand = (chirp_value)word->instructions;
  chirp_allot(*vm, sizeof *this);

  this = (void*)chirp_here(*vm);
  this->fn = (op_fn)instr_native;
  this->operand = (chirp_value)postpone2;
  chirp_allot(*vm, sizeof *this);
}

static void
rtod(chirp_vm_param vm)
{
  instr_ret(vm, 0);
  chirp_push(*vm) = (chirp_value)chirp_rpop(*vm);
}

static void
dtor(chirp_vm_param vm)
{
  instr_ret(vm, 0);
  chirp_rpush(*vm) = chirp_popptr(*vm);
}

static void
allot(chirp_vm_param vm)
{
  chirp_allot(*vm, chirp_pop(*vm));
}

void
chirp_init(chirp_vm_param vm,
           chirp_value* stack,
           struct chirp_instr const** retstack,
           void* sbreak)
{
  vm->ip = 0;
  vm->compiling = 0;
  vm->dict_head = 0;
  vm->stack = stack;
  vm->stack_start = stack;
  vm->retstack = retstack;
  vm->retstack_start = vm->retstack;
  vm->sbreak = sbreak;

  chirp_add_foreign(vm, "CELLS", cells, 0);
  chirp_add_foreign(vm, "HERE", here, 0);
  chirp_add_foreign(vm, "ALLOT", allot, 0);
  chirp_add_foreign(vm, "SOURCE", source, 0);
  chirp_add_foreign(vm, "CREATE", create, 0);
  chirp_add_foreign(vm, "DOES>", does, 1);
  chirp_add_foreign(vm, "@c", cell_deref, 0);
  chirp_add_foreign(vm, "@b", byte_deref, 0);
  chirp_add_foreign(vm, "!c", cell_set, 0);
  chirp_add_foreign(vm, "!b", byte_set, 0);
  chirp_add_foreign(vm, ",", comma, 0);
  chirp_add_foreign(vm, ":", colon, 0);
  chirp_add_foreign(vm, ";", semicolon, 1);
  chirp_add_foreign(vm, "'", quote, 1);
  chirp_add_foreign(vm, "+", add, 0);
  chirp_add_foreign(vm, "-", sub, 0);
  chirp_add_foreign(vm, "*", mul, 0);
  chirp_add_foreign(vm, "mod", mod, 0);
  chirp_add_foreign(vm, "r>", rtod, 0);
  chirp_add_foreign(vm, ">r", dtor, 0);
  chirp_add_foreign(vm, "align", align, 0);
  chirp_add_foreign(vm, "drop", drop, 0);
  chirp_add_foreign(vm, "drop2", drop2, 0);
  chirp_add_foreign(vm, "swap", swap, 0);
  chirp_add_foreign(vm, "rot", rot, 0);
  chirp_add_foreign(vm, "over", over, 0);
  chirp_add_foreign(vm, "dup", dup, 0);
  chirp_add_foreign(vm, "over2", over2, 0);
  chirp_add_foreign(vm, "postpone", postpone, 1);
  chirp_add_foreign(vm, "immediate", immediate, 0);
  chirp_add_foreign(vm, "SKIPSPACE", skipspace, 0);
  chirp_add_foreign(vm, "parse", parse, 0);
  chirp_add_foreign(vm, "atoi", atoi, 0);
  chirp_add_foreign(vm, "branch0", branch0, 0);
  chirp_add_foreign(vm, "branch", branch, 0);
  chirp_add_foreign(vm, "resLink", resLink, 0);
}

struct word_header const*
chirp_find_word(chirp_vm_param vm, chirp_value const hashname)
{
  struct word_header const* cur = vm->dict_head;
  while (cur && cur->hash != hashname)
    cur = word_backptr(*cur);

  return cur;
}

int
chirp_run(chirp_vm_param vm, char const* code)
{
  vm->input_buffer = code;
  vm->input_buffer_length = strlen(code);
  vm->parsing_idx = 0;

  do {
    skipspace(vm);

    if ((vm->input_buffer_length - vm->parsing_idx) == 0)
      break;
    chirp_push(*vm) = ' ';
    parse(vm);
    over2(vm);
    stackhash(vm);

    chirp_value const hv = chirp_pop(*vm);
    struct word_header const* restrict word = chirp_find_word(vm, hv);

    struct chirp_instr* restrict newinstr = (void*)chirp_here(*vm);

    if (word) {
      drop2(vm);
      if (((uintptr_t)word->backptr & 1U) || !vm->compiling) {
        run(vm, word->instructions);
      } else {
        newinstr->fn = (op_fn)instr_subcall;
        newinstr->operand = (chirp_operand)word->instructions;
        chirp_allot(*vm, sizeof *newinstr);
      }
    } else {
      atoi(vm);
      if (vm->compiling) {
        chirp_value const val = chirp_pop(*vm);
        newinstr->fn = (op_fn)instr_push;
        newinstr->operand = val;
        chirp_allot(*vm, sizeof *newinstr);
      }
    }

  } while (1);

  return 1;
}

[[gnu::cold]]
void
chirp_add_foreign(chirp_vm_param vm,
                  char const* restrict name,
                  chirp_foreign_fn fn,
                  int const immediate)
{

  align(vm);
  struct word_header* ptr = (void*)chirp_here(*vm);
  ptr->hash = hash(name, strlen(name));
  ptr->backptr = ptr_setbit(vm->dict_head, (immediate == 1), 0U);
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
