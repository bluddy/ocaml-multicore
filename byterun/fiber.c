#include <string.h>
#include <unistd.h>
#include "caml/fiber.h"
#include "caml/gc_ctrl.h"
#include "caml/instruct.h"
#include "caml/fail.h"
#include "caml/alloc.h"
#include "caml/platform.h"
#include "caml/fix_code.h"
#include "caml/memory.h"
#ifdef NATIVE_CODE
#include "stack.h"
#include "frame_descriptors.h"
#endif

static __thread int stack_is_saved = 0;

static void dirty_stack(cdst cds, value stack)
{
  /* There is no write barrier (caml_modify) on writes to the stack,
     so a just-run fiber's stack may contain untracked shared->young
     pointers or pointers to unmarked objects. We add the stack to a
     ref table so that the GC can find these pointers. */
  if (!Is_minor(stack)) {
    Assert(Stack_dirty_domain(stack) == 0 ||
           Stack_dirty_domain(stack) == caml_domain_self());
    if (Stack_dirty_domain(stack) == 0) {
      Stack_dirty_domain(stack) = caml_domain_self();
      Ref_table_add(&CAML_DOMAIN_STATE->remembered_set->fiber_ref, stack, 0);
    }
  }
}

#ifdef NATIVE_CODE

static value save_stack (cdst cds)
{
  value old_stack = CAML_DOMAIN_STATE->current_stack;
  dirty_stack(cds, old_stack);
  return old_stack;
}

static void load_stack (value stack) {
  CAML_DOMAIN_STATE->stack_threshold = Stack_base(stack) + Stack_threshold / sizeof(value);
  CAML_DOMAIN_STATE->stack_high = Stack_high(stack);
  CAML_DOMAIN_STATE->current_stack = stack;
}

extern void caml_fiber_exn_handler (value) Noreturn;
extern void caml_fiber_val_handler (value) Noreturn;

value caml_alloc_stack (cdst cds, value hval, value hexn, value heff) {
  CAMLparam3(hval, hexn, heff);
  CAMLlocal1(stack);
  char* sp;
  struct caml_context *ctxt;

  stack = caml_alloc(caml_fiber_wsz, Stack_tag);
  Stack_dirty_domain(stack) = 0;
  Stack_handle_value(stack) = hval;
  Stack_handle_exception(stack) = hexn;
  Stack_handle_effect(stack) = heff;
  Stack_parent(stack) = Val_unit;

  sp = (char*)Stack_high(stack);
  /* Fiber exception handler that returns to parent */
  sp -= sizeof(value);
  *(value**)sp = (value*)caml_fiber_exn_handler;
  /* No previous exception frame */
  sp -= sizeof(value);
  *(uintnat*)sp = 0;
  /* Value handler that returns to parent */
  sp -= sizeof(value);
  *(value**)sp = (value*)caml_fiber_val_handler;

  /* Build a context */
  sp -= sizeof(struct caml_context);
  ctxt = (struct caml_context*)sp;
  ctxt->exception_ptr_offset = 2 * sizeof(value);
  ctxt->gc_regs = NULL;
  Stack_sp(stack) = -(3 + sizeof(struct caml_context) / sizeof(value));

  caml_gc_log ("Allocate stack=0x%lx of %lu words\n",
               stack, caml_fiber_wsz);

  CAMLreturn (stack);
}


void caml_scan_stack(cdst cds, scanning_action f, value stack)
{
  char * sp;
  uintnat retaddr;
  value * regs;
  frame_descr * d;
  uintnat h;
  int n, ofs;
#ifdef Stack_grows_upwards
  short * p;  /* PR#4339: stack offsets are negative in this case */
#else
  unsigned short * p;
#endif
  value *root;
  struct caml_context* context;
  frame_descr** frame_descriptors;
  int frame_descriptors_mask;

  f(caml_frame_descriptor_table, &caml_frame_descriptor_table);
  f(Stack_handle_value(stack), &Stack_handle_value(stack));
  f(Stack_handle_exception(stack), &Stack_handle_exception(stack));
  f(Stack_handle_effect(stack), &Stack_handle_effect(stack));
  f(Stack_parent(stack), &Stack_parent(stack));

  if (Stack_sp(stack) == 0) return;
  sp = (char*)(Stack_high(stack) + Stack_sp(stack));
  Assert(caml_frame_descriptor_table != Val_unit);
  frame_descriptors = Data_abstract_val(caml_frame_descriptor_table);
  frame_descriptors_mask = Wosize_val(caml_frame_descriptor_table) - 1;

next_chunk:
  if (sp == (char*)Stack_high(stack)) return;
  context = (struct caml_context*)sp;
  regs = context->gc_regs;
  sp += sizeof(struct caml_context);

  if (sp == (char*)Stack_high(stack)) return;
  retaddr = *(uintnat*)sp;
  sp += sizeof(value);

  while(1) {
    /* Find the descriptor corresponding to the return address */
    h = Hash_retaddr(retaddr, frame_descriptors_mask);
    while(1) {
      d = frame_descriptors[h];
      if (d->retaddr == retaddr) break;
      h = (h+1) & frame_descriptors_mask;
    }
    if (d->frame_size != 0xFFFF) {
      /* Scan the roots in this frame */
      for (p = d->live_ofs, n = d->num_live; n > 0; n--, p++) {
        ofs = *p;
        if (ofs & 1) {
          root = regs + (ofs >> 1);
        } else {
          root = (value *)(sp + ofs);
        }
        f (*root, root);
      }
      /* Move to next frame */
#ifndef Stack_grows_upwards
      sp += (d->frame_size & 0xFFFC);
#else
      sp -= (d->frame_size & 0xFFFC);
#endif
      retaddr = Saved_return_address(sp);
      /* XXX KC: disabled already scanned optimization. */
    } else {
      /* This marks the top of an ML stack chunk. */
#ifndef Stack_grows_upwards
      sp += Next_chunk_offset;
#else
      sp -= Next_chunk_offset;
#endif
      goto next_chunk;
    }
  }
}

void caml_maybe_expand_stack (cdst cds, value* gc_regs)
{
  CAMLparamN(gc_regs, 5);
  uintnat stack_available;

  Assert(Tag_val(CAML_DOMAIN_STATE->current_stack) == Stack_tag);

  stack_available = Wosize_val(CAML_DOMAIN_STATE->current_stack)
                  /* Stack_sp() is a -ve value in words */
                  + Stack_sp (CAML_DOMAIN_STATE->current_stack)
                  - Stack_ctx_words;
  if (stack_available < 2 * Stack_threshold / sizeof(value))
    caml_realloc_stack (cds, 0, 0, 0);

  CAMLreturn0;
}

void caml_update_gc_regs_slot (value* gc_regs)
{
  struct caml_context *ctxt;
  ctxt = (struct caml_context*) (Stack_high(CAML_DOMAIN_STATE->current_stack)
                                 + Stack_sp(CAML_DOMAIN_STATE->current_stack));
  ctxt->gc_regs = gc_regs;
}

#else /* End NATIVE_CODE, begin BYTE_CODE */

caml_root caml_global_data;

static value save_stack (cdst cds)
{
  value old_stack = CAML_DOMAIN_STATE->current_stack;
  Stack_sp(old_stack) = CAML_DOMAIN_STATE->extern_sp - CAML_DOMAIN_STATE->stack_high;
  Assert(CAML_DOMAIN_STATE->stack_threshold == Stack_base(old_stack) + Stack_threshold / sizeof(value));
  Assert(CAML_DOMAIN_STATE->stack_high == Stack_high(old_stack));
  Assert(CAML_DOMAIN_STATE->extern_sp == CAML_DOMAIN_STATE->stack_high + Stack_sp(old_stack));
  dirty_stack(cds, old_stack);
  return old_stack;
}

static void load_stack(cdst cds, value newstack)
{
  Assert(Tag_val(newstack) == Stack_tag);
  CAML_DOMAIN_STATE->stack_threshold = Stack_base(newstack) + Stack_threshold / sizeof(value);
  CAML_DOMAIN_STATE->stack_high = Stack_high(newstack);
  CAML_DOMAIN_STATE->extern_sp = CAML_DOMAIN_STATE->stack_high + Stack_sp(newstack);
  CAML_DOMAIN_STATE->current_stack = newstack;
}

CAMLprim value caml_alloc_stack(cdst cds, value hval, value hexn, value heff)
{
  CAMLparam3(hval, hexn, heff);
  CAMLlocal1(stack);
  value* sp;
  value* high;

  stack = caml_alloc(cds, caml_fiber_wsz, Stack_tag);
  high = sp = Stack_high(stack);

  // ?
  sp -= 1;
  sp[0] = Val_long(1); /* trapsp ?? */

  Stack_sp(stack) = sp - high;
  Stack_dirty_domain(stack) = 0;
  Stack_handle_value(stack) = hval;
  Stack_handle_exception(stack) = hexn;
  Stack_handle_effect(stack) = heff;
  Stack_parent(stack) = Val_unit;

  CAMLreturn (stack);
}

/*
  Find the stack that performed an effect,
  skipping over several stacks that reperformed
  the effect if necessary.

  Reverses the parent pointers to point
  performer -> delegator instead of
  delegator -> performer.
*/
value caml_find_performer(cdst cds, value stack)
{
  value parent = CAML_DOMAIN_STATE->current_stack;
  do {
    value delegator = Stack_parent(stack);
    Stack_parent(stack) = parent;
    parent = stack;
    stack = delegator;
  } while (stack != Val_unit);
  return parent;
}

CAMLprim value caml_ensure_stack_capacity(cdst cds, value required_space)
{
  asize_t req = Long_val(required_space);
  if (CAML_DOMAIN_STATE->extern_sp - req < Stack_base(CAML_DOMAIN_STATE->current_stack))
    caml_realloc_stack(cds, req, 0, 0);
  return Val_unit;
}

void caml_change_max_stack_size (cdst cds, uintnat new_max_size)
{
  asize_t size = CAML_DOMAIN_STATE->stack_high - CAML_DOMAIN_STATE->extern_sp
                 + Stack_threshold / sizeof (value);

  if (new_max_size < size) new_max_size = size;
  if (new_max_size != caml_max_stack_size){
    caml_gc_log ("Changing stack limit to %luk bytes",
                     new_max_size * sizeof (value) / 1024);
  }
  caml_max_stack_size = new_max_size;
}

/*
  Root scanning.

  Used by the GC to find roots on the stacks of running or runnable fibers.
*/

void caml_scan_stack(cdst cds, scanning_action f, value stack)
{
  value *low, *high, *sp;
  Assert(Is_block(stack) && Tag_val(stack) == Stack_tag);

  if (Is_promoted_hd(Hd_val(stack)))
    Assert(!Is_young(stack)); //FIXME

  f(cds, Stack_handle_value(stack), &Stack_handle_value(stack));
  f(cds, Stack_handle_exception(stack), &Stack_handle_exception(stack));
  f(cds, Stack_handle_effect(stack), &Stack_handle_effect(stack));
  f(cds, Stack_parent(stack), &Stack_parent(stack));

  high = Stack_high(stack);
  low = high + Stack_sp(stack);
  for (sp = low; sp < high; sp++) {
    f(cds, *sp, sp);
  }
}

#endif /* not NATIVE_CODE */

void caml_scan_dirty_stack(cdst cds, scanning_action f, value stack)
{
  Assert(Tag_val(stack) == Stack_tag);
  if (Stack_dirty_domain(stack) == caml_domain_self()) {
    caml_scan_stack(cds, f, stack);
  }
}

void caml_clean_stack(value stack)
{
  Assert(Tag_val(stack) == Stack_tag);
  if (Stack_dirty_domain(stack) == caml_domain_self()) {
    Stack_dirty_domain(stack) = 0;
  }
}

void caml_clean_stack_domain(value stack, struct domain* domain)
{
  Assert(Tag_val(stack) == Stack_tag);
  if (Stack_dirty_domain(stack) == domain) {
    Stack_dirty_domain(stack) = 0;
  }
}

/*
  Stack management.

  Used by the interpreter to allocate stack space.
*/

int caml_on_current_stack(cdst cds, value* p)
{
  return Stack_base(CAML_DOMAIN_STATE->current_stack) <= p && p < CAML_DOMAIN_STATE->stack_high;
}

void caml_realloc_stack(cdst cds, asize_t required_space, value* saved_vals, int nsaved)
{
  CAMLparamN(saved_vals, nsaved);
  CAMLlocal2(old_stack, new_stack);
  asize_t size;
  int stack_used;

  old_stack = save_stack(cds);

  stack_used = -Stack_sp(old_stack);
  size = Stack_high(old_stack) - Stack_base(old_stack);
  do {
    if (size >= caml_max_stack_size) caml_raise_stack_overflow(cds);
    size *= 2;
  } while (size < stack_used + required_space);
  caml_gc_log ("Growing stack to %"
                         ARCH_INTNAT_PRINTF_FORMAT "uk bytes",
                   (uintnat) size * sizeof(value) / 1024);

  new_stack = caml_alloc(cds, Stack_ctx_words + size, Stack_tag);
  memcpy(Stack_high(new_stack) - stack_used,
         Stack_high(old_stack) - stack_used,
         stack_used * sizeof(value));

  Stack_sp(new_stack) = Stack_sp(old_stack);
  Stack_handle_value(new_stack) = Stack_handle_value(old_stack);
  Stack_handle_exception(new_stack) = Stack_handle_exception(old_stack);
  Stack_handle_effect(new_stack) = Stack_handle_effect(old_stack);
  Stack_parent(new_stack) = Stack_parent(old_stack);

  Stack_dirty_domain(new_stack) = 0;
  if (Stack_dirty_domain(old_stack)) {
    Assert (Stack_dirty_domain(old_stack) == caml_domain_self());
    dirty_stack(cds, new_stack);
  }

  load_stack(cds, new_stack);

  /* Reset old stack */
  Stack_sp(old_stack) = 0;
  Stack_dirty_domain(old_stack) = 0;
  Stack_handle_value(old_stack) = Val_long(0);
  Stack_handle_exception(old_stack) = Val_long(0);
  Stack_handle_effect(old_stack) = Val_long(0);
  Stack_parent(old_stack) = Val_unit;

  CAMLreturn0;
}

value caml_alloc_main_stack (cdst cds, uintnat init_size)
{
  CAMLparam0();
  CAMLlocal1(stack);

  /* Create a stack for the main program.
     The GC is not initialised yet, so we use caml_alloc_shr
     which cannot trigger it */
  stack = caml_alloc_shr(cds, init_size, Stack_tag);
  Stack_sp(stack) = 0;
  Stack_dirty_domain(stack) = 0;
  Stack_handle_value(stack) = Val_long(0);
  Stack_handle_exception(stack) = Val_long(0);
  Stack_handle_effect(stack) = Val_long(0);
  Stack_parent(stack) = Val_unit;

  CAMLreturn(stack);
}

void caml_init_main_stack (cdst cds)
{
  value stack;

  stack = caml_alloc_main_stack (cds, Stack_size/sizeof(value));
  load_stack(cds, stack);
}

value caml_switch_stack(cdst cds, value stk)
{
  value s = save_stack(cds);
  load_stack(cds, stk);
  return s;
}

CAMLprim value caml_clone_continuation (cdst cds, value cont)
{
  CAMLparam1(cont);
  CAMLlocal3(new_cont, prev_target, source);
  value target;
  intnat bvar_stat;

  bvar_stat = caml_bvar_status(cds, cont);
  if (bvar_stat & BVAR_EMPTY)
    caml_invalid_argument ("continuation already taken");

  prev_target = Val_unit;
  source = Field (cont, 0);

  do {
    Assert (Is_block (source) && Tag_val(source) == Stack_tag);

    target = caml_alloc (cds, Wosize_val(source), Stack_tag);
    memcpy ((void*)target, (void*)source, Wosize_val(source) * sizeof(value));

    if (prev_target == Val_unit) {
      new_cont = caml_bvar_create (cds, target);
    } else {
      Stack_parent(prev_target) = target;
    }

    prev_target = target;
    source = Stack_parent(source);
  } while (source != Val_unit);

  CAMLreturn(new_cont);
}

void caml_save_stack_gc(cdst cds)
{
  Assert(!stack_is_saved);
  save_stack(cds);
  stack_is_saved = 1;
}

void caml_restore_stack_gc(cdst cds)
{
  if (stack_is_saved) {
    Assert(Tag_val(CAML_DOMAIN_STATE->current_stack) == Stack_tag);
    load_stack(cds, CAML_DOMAIN_STATE->current_stack);
  }
  stack_is_saved = 0;
}

void caml_restore_stack(cdst cds)
{
  Assert(Tag_val(CAML_DOMAIN_STATE->current_stack) == Stack_tag);
  load_stack(cds, CAML_DOMAIN_STATE->current_stack);
}

#ifdef DEBUG
uintnat stack_sp(value stk) {
  return Stack_sp(stk);
}

struct domain* stack_dirty_domain(value stk) {
  return Stack_dirty_domain(stk);
}

value stack_parent(value stk) {
  return Stack_parent(stk);
}
#endif
