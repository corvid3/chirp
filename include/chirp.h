#pragma once

#include <assert.h>
#include <stdint.h>

typedef uintptr_t chirp_value;
typedef intptr_t chirp_number;
#define chirp_bitsize (sizeof(chirp_value) * 8)

struct chirp_vm;
typedef void (*chirp_foreign_fn)(struct chirp_vm* vm);

/* returned allocations must be aligned to 8 bytes */
typedef void* (*chirp_allocate)(unsigned size);

enum : chirp_value
{
  chirp_extra_width = 3,
  chirp_extra_offset = chirp_bitsize - chirp_extra_width,
  chirp_extra_mask = (((chirp_value)1 << chirp_extra_width) - 1U)
                     << chirp_extra_offset,

  chirp_data_width = chirp_bitsize - chirp_extra_width,
  chirp_data_mask = ((chirp_value)1 << chirp_data_width) - 1U,
  chirp_data_offset = 0,

  chirp_pair_size = ((sizeof(chirp_value) * 8 - chirp_extra_width) / 2) - 1,

  chirp_reccomended_stack_size = 100,
  chirp_reccomended_heap_size = 128,

  chirp_max_exec_stack = 8,
};

enum chirp_type : chirp_value
{
#define deriv(n) (((chirp_value)(n) << chirp_extra_offset))
  chirp_num = deriv(0),
  chirp_fnptr = deriv(1),
  chirp_str = deriv(2),
  chirp_foreign = deriv(3),
  chirp_ref = deriv(4),
  chirp_chain = deriv(5),
  chirp_sentinel = deriv(6),
#undef deriv
};

#define chirp_extend(in, size)                                                 \
  (((in) & ((chirp_value)1 << ((size) - 1)))                                   \
     ? (in) | ~(((chirp_value)1 << (size)) - (chirp_value)1)                   \
     : (in))
#define chirp_to_ptr(in) ((uintptr_t)((in) & ~chirp_extra_mask))
#define chirp_to_num(in)                                                       \
  chirp_extend(((in) & ~chirp_extra_mask), chirp_bitsize - (chirp_extra_width))
#define chirp_type(in) ((in) & (chirp_value)chirp_extra_mask)
#define chirp_from_ref(in) (((in) & chirp_data_mask) | chirp_ref)
#define chirp_from_num(in) (((in) & chirp_data_mask) | chirp_num)
#define chirp_from_pair(in) (((in) & chirp_data_mask) | chirp_chain)
#define chirp_from_foreign(fn) ((chirp_value)(fn) | (chirp_value)chirp_fnptr)

/* word expands out to other words or a value */
struct chirp_word
{
  chirp_value forwarding_address;
  /* -1U is used to designate an unused cell */
  chirp_value hash;

  /* if value is -1U & hash != -1U, then the value has been forwarded
   * the location index is stored in hash */
  chirp_value value;
};

struct chirp_vm
{
  chirp_allocate allocator;

  struct
  {
    chirp_value* ptr;
    unsigned size;
    unsigned capacity;
  } stack;

  struct
  {
    chirp_value ptr[chirp_max_exec_stack];
    unsigned size;
  } wordstack;

  struct
  {
    struct chirp_word* ptr;
    unsigned size;
    unsigned capacity;

    unsigned forward_free;
  } heap;
};

void
chirp_init(struct chirp_vm* restrict vm,
           chirp_value* stack,
           unsigned stack_capacity,
           struct chirp_word* heap_ptr,
           unsigned heap_capacity,
           chirp_allocate allocator);

void
chirp_uninit(struct chirp_vm* restrict vm);
int
chirp_run(struct chirp_vm* restrict, char const* code);
void
chirp_add_foreign(struct chirp_vm* restrict vm,
                  char const* name,
                  chirp_foreign_fn fn);

chirp_value
chirp_pop(struct chirp_vm* restrict vm);
void
chirp_push(struct chirp_vm* restrict vm, chirp_value val);
