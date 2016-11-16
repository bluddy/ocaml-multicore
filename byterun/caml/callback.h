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

/* Callbacks from C to OCaml */

#ifndef CAML_CALLBACK_H
#define CAML_CALLBACK_H

#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "mlvalues.h"
#include "memory.h"

#ifdef __cplusplus
extern "C" {
#endif

void caml_init_callbacks (void);

CAMLextern value caml_callback (cdst, value closure, value arg);
CAMLextern value caml_callback2 (cdst, value closure, value arg1, value arg2);
CAMLextern value caml_callback3 (cdst, value closure, value arg1, value arg2,
                                 value arg3);
CAMLextern value caml_callbackN (cdst, value closure, int narg, value args[]);

CAMLextern value caml_callback_exn (cdst, value closure, value arg);
CAMLextern value caml_callback2_exn (cdst, value closure, value arg1, value arg2);
CAMLextern value caml_callback3_exn (cdst, value closure,
                                     value arg1, value arg2, value arg3);
CAMLextern value caml_callbackN_exn (cdst, value closure, int narg, value args[]);

#define Make_exception_result(v) ((v) | 2)
#define Is_exception_result(v) (((v) & 3) == 2)
#define Extract_exception(v) ((v) & ~3)

CAMLextern value caml_get_named_value (cdst, char const * name, int* found);

CAMLextern void caml_main (char ** argv);
CAMLextern void caml_startup (char ** argv);

CAMLextern __thread int caml_callback_depth;

#ifdef __cplusplus
}
#endif

#endif
