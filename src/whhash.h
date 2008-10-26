/* Copyright (C) 2002 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */
/* Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/) */
/* Code originally taken from: http://www.cl.cam.ac.uk/~cwc22/hashtable/ */
#ifndef WANDERINGHORSE_NET_WHHASH_H_INCLUDED
#define WANDERINGHORSE_NET_WHHASH_H_INCLUDED
#include <stddef.h> /* size_t */
#ifdef __cplusplus
extern "C" {
#endif
/**
   @page whhash_page_main whhash: WanderingHorse.net hashtable API.

   The functions and types named whhash* are part of the
   WanderingHorse.net hashtable library. It is a hashtable
   implementation based on code by Christopher Clark,
   adopted, extended, and changed somewhat by yours truly.

   License: New BSD License

   Maintainer: Stephan Beal (http://wanderinghorse.net/home/stephan)

   The hashtables described here map (void*) to (void*)
   by using a client-supplied hash algorithm on the key
   pointers. The hashtable can optionally take over ownership
   of its keys or values, via whhash_set_key_dtor() and
   whhash_set_key_dtor().

   @section whhash_sec_example Example

   @code
   whhash_table  *h;
   struct some_key   *k;
   struct some_value *v;

   // Assume the following functions which can hash and compare the
   // some_key and some_val structs:
   static whhash_val_t         hash_some_key( void *k );
   static int                  cmp_key_val ( void *key1, void *key2 );

   h = whhash_create(16, hash_some_key, cmp_key_val);
   k = (struct some_key *)     malloc(sizeof(struct some_key));
   v = (struct some_value *)   malloc(sizeof(struct some_value));

   ...initialise k and v to suitable values...

   if (! whhash_insert(h,k,v) )
   {
         ...error... probably OOM or one of the args was null...;
   }

   if (NULL == (found = whhash_search(h,k) ))
   {    printf("not found!");                  }
   if (NULL == (found = whhash_take(h,k) ))
   {    printf("Not found\n");                 }

   whgc_destroy( h );
   @endcode

   @seciond whhash_sec_ownership Memory ownership

   By default a hashtable does no memory management of its keys or
   values.  However, whhash_set_key_dtor() and whhash_set_val_dtor()
   can be used to set a cleanup function. If a cleanup function is
   set, it is called (and passed the appropriate key or value) when an
   item is removed from the hashtable (e.g. when the hashtable is
   destroyed). It is possible to transfer ownership back to the caller
   by using whhash_take().

   Example:

   @code
   whhash_table  *h;
   h = whhash_create(16, whhash_hash_cstring_djb2, whhash_cmp_cstring );
   whhash_set_dtors(h, free, free);
   char * str = 0;
   char * key = 0;
   int i = 0;
   for( int i = 0; i < 10; ++i )
   {
       // Assume mnprintf() is a printf-like func which allocates new strings:
       key = mnprintf("key_%d",i);
       str = mnprintf("...%d...",i);
       whhash_insert(h, key, str ); // transfers ownership of key/str to h
   }
   whgc_destroy( h ); // Calls free() on each inserted copy of key and str.
   @endcode


   @section whhash_sec_macros Macros

   Macros may be used to define type-safe(r) whhash_table access functions, with
   methods specialized to take known key and value types as parameters.
 
   Example:

   Insert this at the start of your file:

@code
 DEFINE_WHHASH_INSERT(insert_some, struct some_key, struct some_value);
 DEFINE_WHHASH_SEARCH(search_some, struct some_key, struct some_value);
 DEFINE_WHHASH_TAKE(take_some, struct some_key);
 DEFINE_WHHASH_REMOVE(remove_some, struct some_key);
@endcode

    This defines the functions 'insert_some', 'search_some',
    'take_some', and 'remove_some'.  These operate just like
    whhash_insert() etc., with the same parameters, but their function
    signatures have 'struct some_key *' rather than 'void *', and
    hence can generate compile time errors if your program is
    supplying incorrect data as a key (and similarly for value).

    Note that the hash and key equality functions passed to
    whhash_create still take 'void *' parameters instead of 'some key
    *'. This shouldn't be a difficult issue as they're only defined
    and passed once, and the other functions will ensure that only
    valid keys are supplied to them.

    The cost for this checking is increased code size and runtime
    overhead - if performance is important, it may be worth switching
    back to the unsafe methods once your program has been debugged
    with the safe methods.  This just requires switching to some
    simple alternative defines - eg:
@code
#define insert_some whhash_insert
@endcode

   @section whhash_sec_links Other resources

   Some other resources:

   - http://en.wikipedia.org/wiki/Hash_table

   - http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx

   - The original implementation off of which whhash is based can be found
   at http://www.cl.cam.ac.uk/~cwc22/hashtable/

*/

/*! @typedef whhash_val_t

   The hash key value type by the whhash API.
 */
typedef unsigned long whhash_val_t;

/* @var whhash_val_t hashval_t_err

   hashval_t_err is ((whhash_val_t)-1). It is used to report
   hash value errors. Hash routines which want to report
   an error may do so by returning this value.
 */
extern const whhash_val_t hashval_t_err;
struct whhash_table;

/*! @typedef whhash_table
  whhash_table is an opaque handle to hashtable data. They are created
  using whhash_create() and destroyed using whhash_destroy().
*/
typedef struct whhash_table whhash_table;


/*
  whhash_create() allocates a new hashtable with the given minimum starting
  size (the actual size may, and probably will, be larger). The given hash function
  and comparison function are used for all hashing and comparisons, respectively.


   
 @name                    whhash_create
 @param   minsize         minimum initial size of whhash_table
 @param   hashfunction    function for hashing keys
 @param   key_eq_fn       function for determining key equality
 @return                  newly created whhash_table or NULL on failure
 */
whhash_table *
whhash_create(whhash_val_t minsize,
	      whhash_val_t (*hashfunction) (void const *),
	      int (*key_eq_fn) (void const *,void const *));


/************************************************************************
Sets the destructor function for h's keys, which are cleaned up when
items are removed or the whhash_table is destroyed. By default a
whhash_table owns its keys and will call free() to release them. If you
use keys from managed memory and don't want them destroyed by the
whhash_table, simply pass 0 as the dtor argument.
 */
void whhash_set_key_dtor( whhash_table * h, void (*dtor)( void * ) );

/************************************************************************
This is similar to whhash_set_key_dtor(), but applies to whhash_table
values instead of keys. By default a whhash_table does not own its values
and will not delete them under any circumstances. To make the
whhash_table take ownership, simply pass 'free' (or suitable function for
your type, e.g. many structs may need a custom destructor) to this
function.
 */
void whhash_set_val_dtor( whhash_table * h, void (*dtor)( void * ) );

/**
   Equivalent to whhash_set_key_dtor(h,keyDtor) and
   whhash_set_val_dtor(h,valDtor).
*/
void whhash_set_dtors( whhash_table * h, void (*keyDtor)( void * ), void (*valDtor)( void * ) );


/*****************************************************************************
 * whhash_insert
   
 @name        whhash_insert
 @param   h   the whhash_table to insert into
 @param   k   the key - whhash_table claims ownership and will free on removal (but see below)
 @param   v   the value - does not claim ownership. v must outlive this whhash_table. (See below)
 @return      non-zero for successful (re)insertion or update.

 This function will cause the table to expand if the insertion would take
 the ratio of entries to table size over the maximum load factor.

 None of the parameters may be 0. Theoretically a value of 0 is okay but
 in practice it means we cannot differentiate between "not found" and
 "found value of 0", so this function doesn't allow it.

 When a key is re-inserted (already mapped to something) then this
 function operates like whhash_replace() and non-zero is
 returned.

 Key/value ownership: one may set the ownership policy by using
 whhash_set_key_dtor() and whhash_set_val_dtor(). The destructors
 are used whenever items are removed from the whhash_table or the whhash_table
 is destroyed.
 */
int 
whhash_insert(whhash_table *h, void *k, void *v);

/*****************************************************************************
 whhash_replace

 Function to change the value associated with a key, where there
 already exists a value bound to the key in the whhash_table. If (v ==
 existingValue) then this routine has no side effects, otherwise this
 function calls whhash_free_val() for any existing value tied to
 k, so it may (or may not) deallocate an object.

 Source by Holger Schemel. Modified by Stephan Beal to use
 whhash_free_value().

 Returns 0 if no match is found, -1 if a match is made and replaced,
 and 1 if (v == existingValue).

 None of the parameters may be 0. Theoretically a value of 0 is okay but
 in practice it means we cannot differentiate between "not found" and
 "found value of 0", so this function doesn't allow it.

 @name    whhash_replace
 @param   h   the whhash_table
 @param   key
 @param   value

 */
int
whhash_replace(whhash_table *h, void *k, void *v);

#define DEFINE_WHHASH_INSERT(fnname, keytype, valuetype) \
int fnname (whhash_table *h, keytype *k, valuetype *v) \
{ \
    return whhash_insert(h,k,v); \
}

/*
  whhash_search() searches for the given key and returns the
  associated value (if found) or 0 (if not found). Ownership of the
  returned value is unchanged.

 @name        whhash_search
 @param   h   the whhash_table to search
 @param   k   the key to search for  - does not claim ownership
 @return      the value associated with the key, or NULL if none found
 */

void *
whhash_search(whhash_table *h, void const * k);

#define DEFINE_WHHASH_SEARCH(fnname, keytype, valuetype) \
valuetype * fnname (whhash_table *h, keytype const *k) \
{ \
    return (valuetype *) (whhash_search(h,k)); \
}

/*****************************************************************************
 whhash_take() removes the given key from the whhash_table and
 returns the value to the caller. The key/value destructors (if any)
 set via whhash_set_key_dtor() and whhash_set_val_dtor() WILL
 NOT be called! If you want to remove an item and call the dtors for
 its key and value, use whhash_remove().

 If (!h), (!k), or no match is found, then 0 is returned and ownership
 of k is not changed. If a match is found which is 0, 0 is
 still returned but ownership of k is returned to the caller. There
 is unfortunately no way for a caller to differentiate between these
 two cases.

 @name        whhash_take
 @param   h   the whhash_table to remove the item from
 @param   k   the key to search for
 @return      the value associated with the key, or NULL if none found
 */

void *
whhash_take(whhash_table *h, void *k);

/**
   Works like whhash_take(h,k), but also calls the value dtor (set
   via whhash_set_val_dtor()) if it finds a match, which may (or
   may not) deallocate the object. Note that whhash_take() may also
   deallocate the key (depends on the destructor assigned to
   whhash_set_key_dtor()), so the k parameter may not be valid
   after this function returns.

   Returns 1 if it finds a value, else 0.
*/
short whhash_remove(whhash_table *h, void *k);

#define DEFINE_WHHASH_TAKE(fnname, keytype, valuetype) \
valuetype * fnname (whhash_table *h, keytype const *k) \
{ \
    return (valuetype *) (whhash_take(h,k)); \
}

#define DEFINE_WHHASH_REMOVE(fnname, keytype) \
short fnname (whhash_table *h, keytype const *k) { return whhash_remove(h,k);}


/*
  whhash_count() returns the number of items in the hashtable.
 This is a constant-time operation.
   
 @name        whhash_count
 @param   h   the whhash_table
 @return      the number of items stored in the whhash_table
*/
size_t
whhash_count(whhash_table const * h);

/**
   Returns the approximate number of bytes allocated for
   whhash_table-internal data associated with the given whhash_table,
   or 0 if (!h). This only measures internal allocations - it has no
   way of knowing the sizes of inserted items which are referenced by
   the whhash_table.
*/
size_t
whhash_bytes_alloced(whhash_table const * h);


/*****************************************************************************
 whhash_destroy() cleans up resources allocated by a whhash_table and calls
 the configured destructors for each key and value. After
 this call, h is invalid.
 
 @name        whhash_destroy
 @param   h   the whhash_table
 */

void
whhash_destroy(whhash_table *h);

/**
  A comparison function for use with whhash_create(). If (k1==k2) it
  returns true, otherwise it performs strcmp(k1,k2) and returns true
  if they match.
 */
int whhash_cmp_cstring( void const * k1, void const * k2 );


/**
  A comparison function for use with whhash_create().
  It essentially performs (*((long*)k1) == (*((long*)k2))).

  Trying to compare numbers of different-sized types (e.g. long and
  short) won't work (well, probably won't). Results are undefined.
 */
int whhash_cmp_long( void const * k1, void const * k2 );

/**
   An int/long hashing function for use with whhash_create().  It
   requires that n point to a long integer, and it simply returns the
   value of n, or hashval_t_err on error (n is NULL).
 */
whhash_val_t whhash_hash_long( void const * n );

/*
  A C-string hashing function for use with whhash_create().  Uses the
  so-called "djb2" algorithm. Returns hashval_t_err if (!str).

  For notes on the hash algorithm see:

  http://www.cse.yorku.ca/~oz/hash.html

*/
whhash_val_t whhash_hash_cstring_djb2( void const * str );

/**
   Implements the "Modified Bernstein Hash", as described at:

   http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx

*/
whhash_val_t whhash_hash_cstring_djb2m( void const * str );


/**
   Implements the "Shift-Add-XOR" (SAX) hash, as described at:

   http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx
*/
whhash_val_t whhash_hash_cstring_djb2m( void const * str );


/**
   Implements the One-at-a-time hash, as described at:

   http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx
*/
whhash_val_t whhash_hash_cstring_oaat( void const * str );



/**
   The "rotating hash", as described at:

   http://eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx

   To quote that page:

   "Much of the time, the rotating hash is sufficient, and can be
   considered the minimal acceptable algorithm."
*/
whhash_val_t whhash_hash_cstring_rot( void const * str );

/*
  A C-string hashing function for use with whhash_create().  Uses the
  so-called "sdbm" algorithm. Returns hashval_t_err if (!str).

  For notes on the hash algorithm see:

  http://www.cse.yorku.ca/~oz/hash.html
*/
whhash_val_t whhash_hash_cstring_sdbm( void const * str );


struct whhash_iter;
/*! @typedef whhash_iter

  Opaque handle for whhash_table interators.
*/
typedef struct whhash_iter whhash_iter;


/* whhash_get_iter() creates a new iterator for the given hashtable
   and returns it. The caller must call free() on the object when he
   is done with it. If (!whhash_count(h)) then this function returns
   0.
*/
whhash_iter * whhash_get_iter(whhash_table *h);

/* Returns the key of the (key,value) pair at the current position,
   or 0 if !i. */
void * whhash_iter_key(whhash_iter *i);

/* Returns the value of the (key,value) pair at the current position,
   or 0 if !i. */
void * whhash_iter_value(whhash_iter *i);

/*
  Advance the iterator to the next element returns zero if advanced to
  end of table or if (!itr).
*/
int
whhash_iter_advance(whhash_iter *itr);

/*****************************************************************************/
/* remove - remove current element and advance the iterator to the
   next element the associated destructors to free (or not) the
   pointed-to key and value, so this call may (or may not) deallocate
   those objects.
*/
int
whhash_iter_remove(whhash_iter *itr);

/*
  Searches for the given key in itr's associated hashtable.
  If found, itr is modified to point to the found item and
  a true value is returned. If no item is found or if (!itr||!k)
  then false (0) is returned.
*/
int whhash_iter_search(whhash_iter *itr, void *k);

#define DEFINE_WHHASH_ITERATOR_SEARCH(fnname, keytype) \
int fnname (whhash_iter *i, keytype *k) \
{ \
    return (whhash_iter_search(i,k)); \
}



#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* WANDERINGHORSE_NET_WHHASH_H_INCLUDED */

/*
 Copyright (c) 2002, Christopher Clark
 Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/)
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 
 * Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 
 * Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 
 * Neither the name of the original author; nor the names of any contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.
  
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
