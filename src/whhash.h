/* Copyright (C) 2002 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */
/* Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/) */
/* Code taken from: http://www.cl.cam.ac.uk/~cwc22/whhash_table/ */
#ifndef __HASHTABLE_CWC22_SGB12_H__
#define __HASHTABLE_CWC22_SGB12_H__
#include <stddef.h> /* size_t */
#ifdef __cplusplus
extern "C" {
#endif
/**
   Changes by Stephan Beal:

   - from unsigned int to unsigned long for hash keys.

   - created typedef whhash_val_t.

   - Added whhash_hash_cstring_{djb2,sdbm}() algorithms,
   taken from: http://www.cse.yorku.ca/~oz/hash.html

   - whhash_iter() now returns 0 if the whhash_table
   is empty, simplifying iteration error checking a bit.

   - The API is now const-safe, insofar as feasible. That is, funcs
   which can get away with (void const *) instead of (void*) now use
   const parameters.

   - Added ability to set a custom key/value dtors for each
   whhash_table.

   - Slightly changed semantics of some routines to accommodate the
   dtor handling.
*/

typedef unsigned long long whhash_val_t;
/**
   hashval_t_err is ((whhash_val_t)-1). It is used to report
   hash value errors.
 */
extern const whhash_val_t hashval_t_err;
struct whhash_table;
typedef struct whhash_table whhash_table;

/* Example of use:
 *
 *      whhash_table  *h;
 *      struct some_key   *k;
 *      struct some_value *v;
 *
 *      static whhash_val_t         whhash_hash_from_key_fn( void *k );
 *      static int                  keys_equal_fn ( void *key1, void *key2 );
 *
 *      h = whhash_create(16, whhash_hash_from_key_fn, keys_equal_fn);
 *      k = (struct some_key *)     malloc(sizeof(struct some_key));
 *      v = (struct some_value *)   malloc(sizeof(struct some_value));
 *
 *      (initialise k and v to suitable values)
 * 
 *      if (! whhash_insert(h,k,v) )
 *      {     exit(-1);               }
 *
 *      if (NULL == (found = whhash_search(h,k) ))
 *      {    printf("not found!");                  }
 *
 *      if (NULL == (found = whhash_take(h,k) ))
 *      {    printf("Not found\n");                 }
 *
 */

/* Macros may be used to define type-safe(r) whhash_table access functions, with
 * methods specialized to take known key and value types as parameters.
 * 
 * Example:
 *
 * Insert this at the start of your file:
 *
 * DEFINE_HASHTABLE_INSERT(insert_some, struct some_key, struct some_value);
 * DEFINE_HASHTABLE_SEARCH(search_some, struct some_key, struct some_value);
 * DEFINE_HASHTABLE_TAKE(take_some, struct some_key);
 * DEFINE_HASHTABLE_REMOVE(remove_some, struct some_key);
 *
 * This defines the functions 'insert_some', 'search_some',
 * 'take_some', and 'remove_some'.  These operate just like
 * whhash_insert() etc., with the same parameters, but their function
 * signatures have 'struct some_key *' rather than 'void *', and hence
 * can generate compile time errors if your program is supplying
 * incorrect data as a key (and similarly for value).
 *
 * Note that the hash and key equality functions passed to whhash_create
 * still take 'void *' parameters instead of 'some key *'. This shouldn't be
 * a difficult issue as they're only defined and passed once, and the other
 * functions will ensure that only valid keys are supplied to them.
 *
 * The cost for this checking is increased code size and runtime overhead
 * - if performance is important, it may be worth switching back to the
 * unsafe methods once your program has been debugged with the safe methods.
 * This just requires switching to some simple alternative defines - eg:
 * #define insert_some whhash_insert
 *
 */

/*****************************************************************************
 * whhash_create
   
 * @name                    whhash_create
 * @param   minsize         minimum initial size of whhash_table
 * @param   hashfunction    function for hashing keys
 * @param   key_eq_fn       function for determining key equality
 * @return                  newly created whhash_table or NULL on failure
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
   
 * @name        whhash_insert
 * @param   h   the whhash_table to insert into
 * @param   k   the key - whhash_table claims ownership and will free on removal (but see below)
 * @param   v   the value - does not claim ownership. v must outlive this whhash_table. (See below)
 * @return      non-zero for successful (re)insertion or update.
 *
 * This function will cause the table to expand if the insertion would take
 * the ratio of entries to table size over the maximum load factor.
 *
 * None of the parameters may be 0. Theoretically a value of 0 is okay but
 * in practice it means we cannot differentiate between "not found" and
 * "found value of 0", so this function doesn't allow it.
 *
 * When a key is re-inserted (already mapped to something) then this
 * function operates like whhash_replace() and non-zero is
 * returned.
 *
 * Key/value ownership: one may set the ownership policy by using
 * whhash_set_key_dtor() and whhash_set_val_dtor(). The destructors
 * are used whenever items are removed from the whhash_table or the whhash_table
 * is destroyed.
 */
int 
whhash_insert(whhash_table *h, void *k, void *v);

/*****************************************************************************
 * whhash_replace
 *
 * Function to change the value associated with a key, where there
 * already exists a value bound to the key in the whhash_table. If (v ==
 * existingValue) then this routine has no side effects, otherwise this
 * function calls whhash_free_val() for any existing value tied to
 * k, so it may (or may not) deallocate an object.
 *
 * Source by Holger Schemel. Modified by Stephan Beal to use
 * whhash_free_value().
 *
 * Returns 0 if no match is found, -1 if a match is made and replaced,
 * and 1 if (v == existingValue).
 *
 * None of the parameters may be 0. Theoretically a value of 0 is okay but
 * in practice it means we cannot differentiate between "not found" and
 * "found value of 0", so this function doesn't allow it.
 *
 * @name    whhash_replace
 * @param   h   the whhash_table
 * @param   key
 * @param   value
 *
 */
int
whhash_replace(whhash_table *h, void *k, void *v);

#define DEFINE_HASHTABLE_INSERT(fnname, keytype, valuetype) \
int fnname (whhash_table *h, keytype *k, valuetype *v) \
{ \
    return whhash_insert(h,k,v); \
}

/*****************************************************************************
 * whhash_search
   
 * @name        whhash_search
 * @param   h   the whhash_table to search
 * @param   k   the key to search for  - does not claim ownership
 * @return      the value associated with the key, or NULL if none found
 */

void *
whhash_search(whhash_table *h, void const * k);

#define DEFINE_HASHTABLE_SEARCH(fnname, keytype, valuetype) \
valuetype * fnname (whhash_table *h, keytype const *k) \
{ \
    return (valuetype *) (whhash_search(h,k)); \
}

/*****************************************************************************
 * whhash_take() removes the given key from the whhash_table and
 * returns the value to the caller. The key/value destructors (if any)
 * set via whhash_set_key_dtor() and whhash_set_val_dtor() WILL
 * NOT be called! If you want to remove an item and call the dtors for
 * its key and value, use whhash_remove().
 *
 * If (!h), (!k), or no match is found, then 0 is returned and ownership
 * of k is not changed. If a match is found which is 0, 0 is
 * still returned but ownership of k is returned to the caller. There
 * is unfortunately no way for a caller to differentiate between these
 * two cases.
 *
 * @name        whhash_take
 * @param   h   the whhash_table to remove the item from
 * @param   k   the key to search for
 * @return      the value associated with the key, or NULL if none found
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

#define DEFINE_HASHTABLE_TAKE(fnname, keytype, valuetype) \
valuetype * fnname (whhash_table *h, keytype const *k) \
{ \
    return (valuetype *) (whhash_take(h,k)); \
}

#define DEFINE_HASHTABLE_REMOVE(fnname, keytype) \
short fnname (whhash_table *h, keytype const *k) { return whhash_remove(h,k);}


/*****************************************************************************
 * whhash_count
   
 * @name        whhash_count
 * @param   h   the whhash_table
 * @return      the number of items stored in the whhash_table
 *
 * This is a constant-time operation.
 */
size_t
whhash_count(whhash_table const * h);

/**
   Returns the approximate number of bytes allocated for
   whhash_table-internal data associated with the given whhash_table, or 0
   if (!h). It has no way of knowing the sizes of inserted items which
   are referenced by the whhash_table.
*/
size_t
whhash_bytes_alloced(whhash_table const * h);


/*****************************************************************************
 * whhash_destroy() cleans up resources allocated by a whhash_table and calls
 * the configured destructors for each key and value. After
 * this call, h is invalid.
 * 
 * @name        whhash_destroy
 * @param   h   the whhash_table
 */

void
whhash_destroy(whhash_table *h);

/**
  A comparison function for use with whhash_create().
  It performs strcmp(k1,k2) and returns true if they
  match. It also returns true if (k1==k2).
 */
int whhash_cmp_cstring( void const * k1, void const * k2 );


/**
  A comparison function for use with whhash_create().
  It essentially performs (*((long*)k1) == (*((long*)k2))).

  Trying to compare numbers of different-sized types (e.g. long and
  short) won't work (well, probably won't). Results are undefined.
 */
int whhash_cmp_long( void const * k1, void const * k2 );

/*
  A C-string hashing function for use with whhash_create().  Uses
  the so-called "djb2" algorithm.

 Returns hashval_t_err if (!str).
*/
whhash_val_t whhash_hash_cstring_djb2( void const * str );

/*
  A C-string hashing function for use with whhash_create().  Uses
  the so-called "sdbm" algorithm.

  Returns hashval_t_err if (!str).
*/
whhash_val_t whhash_hash_cstring_sdbm( void const * str );

/**
   An int/long hashing function for use with whhash_create().  It
   requires that n point to a long integer, and it simply returns the
   value of n, or hashval_t_err on error (n is NULL).
 */
whhash_val_t whhash_hash_long( void const * n );

struct whhash_entry;
typedef struct whhash_entry whhash_entry;
/*****************************************************************************/
/* This struct is only concrete here to allow the inlining of two of the
 * accessor functions. */
struct whhash_itr
{
    whhash_table *h;
    whhash_entry *e;
    whhash_entry *parent;
    unsigned int index;
};
typedef struct whhash_itr whhash_itr;


/*****************************************************************************/
/* whhash_iter. Creates a new iterator and returns it. The
 * caller must call free() on the object when he is done with it.
 * If (!whhash_count(h)) then this function returns 0.
 */

whhash_itr *
whhash_iter(whhash_table *h);

/*****************************************************************************/
/* whhash_iter_key
 * - return the value of the (key,value) pair at the current position */

extern void *
whhash_iter_key(whhash_itr *i);

/*****************************************************************************/
/* value - return the value of the (key,value) pair at the current position */

extern void *
whhash_iter_value(whhash_itr *i);

/*****************************************************************************/
/* advance - advance the iterator to the next element
 *           returns zero if advanced to end of table */

int
whhash_iter_advance(whhash_itr *itr);

/*****************************************************************************/
/* remove - remove current element and advance the iterator to the
   next element Calls whhash_free_key() and whhash_free_val() to
   free (or not) the pointed-to key and value, so this call may (or may not)
   deallocate those objects.
*/

int
whhash_iter_remove(whhash_itr *itr);

/*****************************************************************************/
/* search - overwrite the supplied iterator, to point to the entry
 *          matching the supplied key.
            h points to the whhash_table to be searched.
 *          returns zero if not found. */
int
whhash_iter_search(whhash_itr *itr,
                          whhash_table *h, void *k);

#define DEFINE_HASHTABLE_ITERATOR_SEARCH(fnname, keytype) \
int fnname (whhash_itr *i, whhash_table *h, keytype *k) \
{ \
    return (whhash_iter_search(i,h,k)); \
}



#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* __HASHTABLE_CWC22_SGB12_H__ */

/*
 * Copyright (c) 2002, Christopher Clark
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * * Neither the name of the original author; nor the names of any contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 * 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
