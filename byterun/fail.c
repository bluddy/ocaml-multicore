/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*            Xavier Leroy, projet Cristal, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* Raising exceptions from C. */

#include <stdio.h>
#include <stdlib.h>
#include "caml/alloc.h"
#include "caml/fail.h"
#include "caml/io.h"
#include "caml/gc.h"
#include "caml/memory.h"
#include "caml/misc.h"
#include "caml/mlvalues.h"
#include "caml/printexc.h"
#include "caml/signals.h"
#include "caml/fiber.h"

CAMLexport void caml_raise(cdst cds, value v)
{
  CAML_DOMAIN_STATE->exn_bucket = v;
  if (CAML_DOMAIN_STATE->external_raise == NULL) caml_fatal_uncaught_exception(v);
  while (CAML_LOCAL_ROOTS != CAML_DOMAIN_STATE->external_raise->local_roots) {
    Assert(CAML_LOCAL_ROOTS != NULL);
    struct caml__mutex_unwind* m = CAML_LOCAL_ROOTS->mutexes;
    while (m) {
      /* unlocked in reverse order of locking */
      caml_plat_unlock(m->mutex);
      m = m->next;
    }
    CAML_LOCAL_ROOTS = CAML_LOCAL_ROOTS->next;
  }
  siglongjmp(CAML_DOMAIN_STATE->external_raise->jmp->buf, 1);
}

CAMLexport void caml_raise_constant(cdst cds, value tag)
{
  caml_raise(cds, tag);
}

CAMLexport void caml_raise_with_arg(cdst cds, value tag, value arg)
{
  CAMLparam2 (tag, arg);
  CAMLlocal1 (bucket);

  bucket = caml_alloc_small (cds, 2, 0);
  Init_field(bucket, 0, tag);
  Init_field(bucket, 1, arg);
  caml_raise(cds, bucket);
  CAMLnoreturn;
}

CAMLexport void caml_raise_with_args(cdst cds, value tag, int nargs, value args[])
{
  CAMLparam1 (tag);
  CAMLxparamN (args, nargs);
  value bucket;
  int i;

  Assert(1 + nargs <= Max_young_wosize);
  bucket = caml_alloc_small (cds, 1 + nargs, 0);
  Init_field(bucket, 0, tag);
  for (i = 0; i < nargs; i++) Init_field(bucket, 1 + i, args[i]);
  caml_raise(cds, bucket);
  CAMLnoreturn;
}

CAMLexport void caml_raise_with_string(cdst cds, value tag, char const *msg)
{
  CAMLparam1(tag);
  value v_msg = caml_copy_string(cds, msg);
  caml_raise_with_arg(cds, tag, v_msg);
  CAMLnoreturn;
}

/* PR#5115: Failure and Invalid_argument can be triggered by
   input_value while reading the initial value of [caml_global_data]. */

static value get_exception(cdst cds, int exn, const char* exn_name)
{
  if (caml_global_data == 0 || !Is_block(caml_read_root(cds, caml_global_data))) {
    fprintf(stderr, "Fatal error %s during initialisation\n", exn_name);
    exit(2);
  }
  if (caml_domain_self() == 0 || !caml_domain_self()->vm_inited) {
    fprintf(stderr, "Fatal error %s during domain creation\n", exn_name);
    exit(2);
  }
  return Field(caml_read_root(cds, caml_global_data), exn);
}

#define GET_EXCEPTION(exn) get_exception(cds, exn, #exn)

CAMLexport void caml_failwith (char const *msg)
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_with_string(cds, GET_EXCEPTION(FAILURE_EXN), msg);
}

CAMLexport void caml_invalid_argument (char const *msg)
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_with_string(cds, GET_EXCEPTION(INVALID_EXN), msg);
}

CAMLexport void caml_array_bound_error()
{
  caml_invalid_argument("index out of bounds");
}

CAMLexport void caml_raise_out_of_memory()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(OUT_OF_MEMORY_EXN));
}

CAMLexport void caml_raise_stack_overflow()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(STACK_OVERFLOW_EXN));
}

CAMLexport void caml_raise_sys_error(value msg)
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_with_arg(cds, GET_EXCEPTION(SYS_ERROR_EXN), msg);
}

CAMLexport void caml_raise_end_of_file()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(END_OF_FILE_EXN));
}

CAMLexport void caml_raise_zero_divide()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(ZERO_DIVIDE_EXN));
}

CAMLexport void caml_raise_not_found()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(NOT_FOUND_EXN));
}

CAMLexport void caml_raise_sys_blocked_io()
{
  cdst cds = CAML_TL_DOMAIN_STATE;
  caml_raise_constant(cds, GET_EXCEPTION(SYS_BLOCKED_IO));
}

int caml_is_special_exception(cdst cds, value exn) {
  return exn == GET_EXCEPTION(MATCH_FAILURE_EXN)
    || exn == GET_EXCEPTION(ASSERT_FAILURE_EXN)
    || exn == GET_EXCEPTION(UNDEFINED_RECURSIVE_MODULE_EXN);
}
