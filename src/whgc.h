#ifndef WANDERINGHORSE_NET_WHGC_H_INCLUDED
#define WANDERINGHORSE_NET_WHGC_H_INCLUDED

#include <stdarg.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#  ifndef WHGC_HAVE_STDBOOL
#    define WHGC_HAVE_STDBOOL 0
#  endif

#  if defined(WHGC_HAVE_STDBOOL) && !(WHGC_HAVE_STDBOOL)
#    if !defined(bool)
#      define bool char
#    endif
#    if !defined(true)
#      define true 1
#    endif
#    if !defined(false)
#      define false 0
#    endif
#  else /* aha! stdbool.h! C99. */
#    include <stdbool.h>
#  endif /* WHGC_HAVE_STDBOOL */
#endif /* __cplusplus */

    /*!
      @page page_whgc whgc

      @section sec_about_whgc About

       Author: Stephan Beal (http://wanderinghorse.net/home/stephan)

       License: the core library is Public Domain, but some of the
       borrowed utility code is released under a BSD license (see
       hashtable*.{c,h} for details).

       Home page: http://fossil.wanderinghorse.net/repos/pegc/

       whgc is a small garbage collection library for C. It was
       originally developed to provide predictable cleanup for C
       structs which contained pointers to dynamically allocated
       objects. The solution to that particular application turned out
       to be pretty generic, so it was refactored into whgc.
    */

    /**
       whgc_context is an opaque handle for use with the whgc_xxx()
       routines. They are created using whgc_create_context() and
       destroyed using whgc_destroy_context().
    */
    struct whgc_context;
    typedef struct whgc_context whgc_context;

    /**
       Creates a gc context. The clientContext point may internally be
       used as a lookup key or some such but is otherwise unused by
       this API.
    */
    whgc_context * whgc_create_context( void const * clientContext );

    /**
       Typedef for deallocation functions symantically compatible with
       free().
    */
    typedef void (*whgc_dtor_f)(void*);

    /**
       Registers an arbitrary key and value with the garbage
       collector, such that whgc_destroy_context(st) will clean up the
       resources using the given destructor functions.

       The key parameter is used as a literal hash key (that is, the
       pointer's value is its hash value).

       If keyDtor is not 0 then during cleanup keyDtor(key) is
       called. Likewise, if valDtor is not 0 then valDtor(value) is
       called at cleanup time.

       It is perfectly legal for the key and value to be the same
       object, but only if at least one of the destructor functions is
       0 or a no-op function (otherwise a double-free will happen).

       It is legal for both keyDtor and valDtor to be 0, in which case
       this API simply holds a reference to the data but will not destroy
       it.

       Returns true if the item is now registered, or false on error
       (!st, !key, key was already mapped, or a memory allocation error).

       It is illegal to register the same key more than once with the
       same context. Doing so will cause false to be returned.

       Note that the destruction order of items cleaned up using this
       mechanism is undefined.
    */
    bool whgc_register( whgc_context * ,
			void * key, whgc_dtor_f keyDtor,
			void * value, whgc_dtor_f valDtor );

    bool whgc_add( whgc_context *, void * key, whgc_dtor_f keyDtor );

    /**
       Frees all resources associated with the given context.
       All entries which have been added via whgc_add() are passed to
       the dtor function which was assigned to them via whgc_add().

       Note that the destruction order is undefined.
    */
    void whgc_destroy_context( whgc_context * );

    /**
       Searches the given context for the given key. Returns 0 if the
       key is not found.
    */
    void * whgc_search( whgc_context const * cx, void const * key );

#ifdef __cplusplus
} // extern "C"
#endif


#endif /* WANDERINGHORSE_NET_WHGC_H_INCLUDED */