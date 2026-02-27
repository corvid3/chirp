#pragma once

#include <assert.h>
#include <stdint.h>

typedef uintptr_t chirp_value;
typedef intptr_t chirp_number;
typedef unsigned char chirp_byte;
#define chirp_bitsize (sizeof(chirp_value) * 8)

struct chirp_vm;
typedef void (*chirp_foreign_fn)(struct chirp_vm* vm, void*);

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

  chirp_kilo = 1024,
  chirp_reccomended_stack_size = 64,
  chirp_reccomended_retstack_size = 24,
  chirp_reccomended_heap_size = chirp_kilo * 4,
};

#define chirp_quickstart(name)                                                 \
  static chirp_value name##_stack[chirp_reccomended_stack_size];               \
  static chirp_byte name##_heap[chirp_reccomended_heap_size];                  \
  static struct chirp_instr const*                                             \
    name##_retstack[chirp_reccomended_retstack_size];                          \
  struct chirp_vm name = chirp_init(name##_stack, name##_retstack, name##_heap);

#define chirp_extend(in, size)                                                 \
  (((in) & ((chirp_value)1 << ((size) - 1)))                                   \
     ? (in) | ~(((chirp_value)1 << (size)) - (chirp_value)1)                   \
     : (in))
#define chirp_to_ptr(in) ((uintptr_t)((in) & ~chirp_extra_mask))
#define chirp_to_num(in)                                                       \
  chirp_extend(((in) & ~chirp_extra_mask), chirp_bitsize - (chirp_extra_width))
#define chirp_type(in) ((in) & (chirp_value)chirp_extra_mask)
#define chirp_from_num(in) (((in) & chirp_data_mask) | chirp_num)
#define chirp_from_pair(in) (((in) & chirp_data_mask) | chirp_chain)
#define chirp_from_fnptr(fn) ((chirp_value)(fn) | (chirp_value)chirp_fnptr)
#define chirp_from_ptr(in) ((chirp_value)(in) | (chirp_value)chirp_ptr)

struct chirp_instr;
struct chirp_vm
{
  struct chirp_instr const* ip;
  void* dict_head;

  chirp_value* stack;
  struct chirp_instr const** retstack;
  chirp_byte* heap;
};

#define chirp_init(stack_, retstack_, heap_)                                   \
  (struct chirp_vm)                                                            \
  {                                                                            \
    .dict_head = 0, .ip = 0, .stack = (stack_), .retstack = (retstack_),       \
    .heap = (heap_),                                                           \
  }

int
chirp_run(struct chirp_vm* restrict, char const* code);
void
chirp_add_foreign(struct chirp_vm* restrict vm,
                  char const* name,
                  chirp_foreign_fn fn,
                  int immediate);

#define chirp_sptr_push(in) (*((in)++))
#define chirp_sptr_pop(in) (*(--(in)))

#define chirp_pop(vm) chirp_sptr_pop((vm).stack)
#define chirp_push(vm) chirp_sptr_push((vm).stack)
#define chirp_rpop(vm) chirp_sptr_pop((vm).retstack)
#define chirp_rpush(vm) chirp_sptr_push((vm).retstack)
#define chirp_here(vm) (vm)->heap
