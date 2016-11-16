/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

#ifndef CAML_ALLOC_H
#define CAML_ALLOC_H


#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "misc.h"
#include "mlvalues.h"

#ifdef __cplusplus
extern "C" {
#endif

CAMLextern value caml_alloc (cdst, mlsize_t, tag_t);
CAMLextern value caml_alloc_small (cdst, mlsize_t, tag_t);
CAMLextern value caml_alloc_tuple (cdst, mlsize_t);
CAMLextern value caml_alloc_string (cdst, mlsize_t);  /* size in bytes */
CAMLextern value caml_copy_string (cdst, char const *);
CAMLextern value caml_copy_string_array (cdst, char const **);
CAMLextern value caml_copy_double (cdst, double);
CAMLextern value caml_copy_int32 (cdst, int32);       /* defined in [ints.c] */
CAMLextern value caml_copy_int64 (cdst, int64);       /* defined in [ints.c] */
CAMLextern value caml_copy_nativeint (cdst, intnat);  /* defined in [ints.c] */
CAMLextern value caml_alloc_array (cdst, value (*funct) (cdst, char const *),
                                   char const ** array);
CAMLextern value caml_alloc_sprintf(cdst, const char * format, ...);

typedef void (*final_fun)(cdst, value);
CAMLextern value caml_alloc_final (cdst, mlsize_t, /*size in words*/
                                   final_fun, /*finalization function*/
                                   mlsize_t, /*resources consumed*/
                                   mlsize_t  /*max resources*/);

CAMLextern int caml_convert_flag_list (cdst, value, const int *);

#ifdef __cplusplus
}
#endif

#endif /* CAML_ALLOC_H */
