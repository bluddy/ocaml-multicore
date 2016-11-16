#ifndef CAML_MAJOR_GC
#define CAML_MAJOR_GC

intnat caml_major_collection_slice (cdst, intnat);
void caml_finish_marking (cdst);
void caml_init_major_gc(cdst);
void caml_darken(cdst, value, value* ignored);
void caml_mark_root(value, value*);
void caml_empty_mark_stack(cdst);

#endif
