/* Copyright (C) 2002 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */
/* Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/) */
/* Code taken from: http://www.cl.cam.ac.uk/~cwc22/hashtable/ */
#ifndef __HASHTABLE_CWC22_SGB11_H__
#define __HASHTABLE_CWC22_SGB11_H__
#ifdef __cplusplus
extern "C" {
#endif
/**
   Changes by Stephan Beal:

   - from unsigned int to unsigned long for hash keys.

   - created typedef hashval_t.

   - Added hash_cstring_{djb2,sdbm}() algorithms,
   taken from: http://www.cse.yorku.ca/~oz/hash.html

   - hashtable_iterator() now returns 0 if the hashtable
   is empty, simplifying iteration error checking a bit.

   - The API is now const-safe, insofar as feasible. That is, funcs
   which can get away with (void const *) instead of (void*) now use
   const parameters.

   - Added ability to set a custom key/value dtors for each
   hashtable.

   - Slightly changed semantics of some routines to accommodate the
   dtor handling.
*/

typedef unsigned long long hashval_t;
/**
   hashval_t_err is ((hashval_t)-1). It is used to report
   hash value errors.
 */
extern const hashval_t hashval_t_err;
#include "hashtable_private.h"

/* Example of use:
 *
 *      hashtable  *h;
 *      struct some_key   *k;
 *      struct some_value *v;
 *
 *      static hashval_t         hash_from_key_fn( void *k );
 *      static int                  keys_equal_fn ( void *key1, void *key2 );
 *
 *      h = hashtable_create(16, hash_from_key_fn, keys_equal_fn);
 *      k = (struct some_key *)     malloc(sizeof(struct some_key));
 *      v = (struct some_value *)   malloc(sizeof(struct some_value));
 *
 *      (initialise k and v to suitable values)
 * 
 *      if (! hashtable_insert(h,k,v) )
 *      {     exit(-1);               }
 *
 *      if (NULL == (found = hashtable_search(h,k) ))
 *      {    printf("not found!");                  }
 *
 *      if (NULL == (found = hashtable_take(h,k) ))
 *      {    printf("Not found\n");                 }
 *
 */

/* Macros may be used to define type-safe(r) hashtable access functions, with
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
 * hashtable_insert() etc., with the same parameters, but their function
 * signatures have 'struct some_key *' rather than 'void *', and hence
 * can generate compile time errors if your program is supplying
 * incorrect data as a key (and similarly for value).
 *
 * Note that the hash and key equality functions passed to hashtable_create
 * still take 'void *' parameters instead of 'some key *'. This shouldn't be
 * a difficult issue as they're only defined and passed once, and the other
 * functions will ensure that only valid keys are supplied to them.
 *
 * The cost for this checking is increased code size and runtime overhead
 * - if performance is important, it may be worth switching back to the
 * unsafe methods once your program has been debugged with the safe methods.
 * This just requires switching to some simple alternative defines - eg:
 * #define insert_some hashtable_insert
 *
 */

/*****************************************************************************
 * hashtable_create
   
 * @name                    hashtable_create
 * @param   minsize         minimum initial size of hashtable
 * @param   hashfunction    function for hashing keys
 * @param   key_eq_fn       function for determining key equality
 * @return                  newly created hashtable or NULL on failure
 */
hashtable *
hashtable_create(hashval_t minsize,
                 hashval_t (*hashfunction) (void const *),
                 int (*key_eq_fn) (void const *,void const *));


/************************************************************************
Sets the destructor function for h's keys, which are cleaned up when
items are removed or the hashtable is destroyed. By default a
hashtable owns its keys and will call free() to release them. If you
use keys from managed memory and don't want them destroyed by the
hashtable, simply pass 0 as the dtor argument.
 */
void hashtable_set_key_dtor( hashtable * h, void (*dtor)( void * ) );

/************************************************************************
This is similar to hashtable_set_key_dtor(), but applies to hashtable
values instead of keys. By default a hashtable does not own its values
and will not delete them under any circumstances. To make the
hashtable take ownership, simply pass 'free' (or suitable function for
your type, e.g. many structs may need a custom destructor) to this
function.
 */
void hashtable_set_val_dtor( hashtable * h, void (*dtor)( void * ) );

/**
   Equivalent to hashtable_set_key_dtor(h,keyDtor) and
   hashtable_set_val_dtor(h,valDtor).
*/
void hashtable_set_dtors( hashtable * h, void (*keyDtor)( void * ), void (*valDtor)( void * ) );


/*****************************************************************************
 * hashtable_insert
   
 * @name        hashtable_insert
 * @param   h   the hashtable to insert into
 * @param   k   the key - hashtable claims ownership and will free on removal (but see below)
 * @param   v   the value - does not claim ownership. v must outlive this hashtable. (See below)
 * @return      non-zero for successful insertion
 *
 * This function will cause the table to expand if the insertion would take
 * the ratio of entries to table size over the maximum load factor.
 *
 * This function does not check for repeated insertions with a duplicate key.
 * The value returned when using a duplicate key is undefined -- when
 * the hashtable changes size, the order of retrieval of duplicate key
 * entries is reversed.
 * If in doubt, remove before insert.
 *
 * Key/value ownership: one may set the ownership policy by using
 * hashtable_set_key_dtor() and hashtable_set_val_dtor(). The destructors
 * are used whenever items are removed from the hashtable or the hashtable
 * is destroyed.
 */

int 
hashtable_insert(hashtable *h, void *k, void *v);
/**
   
*/
int 
hashtable_replace(hashtable *h, void *k, void *v);

#define DEFINE_HASHTABLE_INSERT(fnname, keytype, valuetype) \
int fnname (hashtable *h, keytype *k, valuetype *v) \
{ \
    return hashtable_insert(h,k,v); \
}

/*****************************************************************************
 * hashtable_search
   
 * @name        hashtable_search
 * @param   h   the hashtable to search
 * @param   k   the key to search for  - does not claim ownership
 * @return      the value associated with the key, or NULL if none found
 */

void *
hashtable_search(hashtable *h, void const * k);

#define DEFINE_HASHTABLE_SEARCH(fnname, keytype, valuetype) \
valuetype * fnname (hashtable *h, keytype const *k) \
{ \
    return (valuetype *) (hashtable_search(h,k)); \
}

/*****************************************************************************
 * hashtable_take() removes the given key from the hashtable and
 * returns the value to the caller.  If a match is found, the key dtor
 * set via hashtable_set_key_dtor() (if any) will be called and passed
 * k.
 *
 * The ownership of the returned pointer is application-specific and
 * defined by the destructor set via hashtable_set_val_dtor(). This routine
 * does not call the value dtor. If you want to remove an item and call the
 * dtors for its key and value, use hashtable_remove().
 *
 * @name        hashtable_take
 * @param   h   the hashtable to remove the item from
 * @param   k   the key to search for  - does not claim ownership
 * @return      the value associated with the key, or NULL if none found
 */

void *
hashtable_take(hashtable *h, void const *k);

/**
   Works like hashtable_take(h,k), but also calls the value dtor (set
   via hashtable_set_val_dtor()) if it finds a match, which may (or
   may not) deallocate the object. Note that hashtable_take() may also
   deallocate the key (depends on the destructor assigned to
   hashtable_set_key_dtor()), so the k parameter may not be valid
   after this function returns.

   Returns 1 if it finds a value, else 0.
*/
short hashtable_remove(hashtable *h, void const *k);

#define DEFINE_HASHTABLE_TAKE(fnname, keytype, valuetype) \
valuetype * fnname (hashtable *h, keytype const *k) \
{ \
    return (valuetype *) (hashtable_take(h,k)); \
}

#define DEFINE_HASHTABLE_REMOVE(fnname, keytype) \
short fnname (hashtable *h, keytype const *k) { return hashtable_remove(h,k);}


/*****************************************************************************
 * hashtable_count
   
 * @name        hashtable_count
 * @param   h   the hashtable
 * @return      the number of items stored in the hashtable
 *
 * This is a constant-time operation.
 */
size_t
hashtable_count(hashtable const * h);


/*****************************************************************************
 * hashtable_destroy() cleans up resources allocated by a hashtable. After
 * this call, h is invalid.
 * 
 * @name        hashtable_destroy
 * @param   h   the hashtable
 */

void
hashtable_destroy(hashtable *h);

/**
  A comparison function for use with hashtable_create().
  It performs strcmp(k1,k2) and returns true if they
  match. It also returns true if (k1==k2).
 */
int hashtable_cmp_cstring( void const * k1, void const * k2 );


/**
  A comparison function for use with hashtable_create().
  It essentially performs (*((long*)k1) == (*((long*)k2))).

  Trying to compare numbers of different-sized types (e.g. long and
  short) won't work (well, probably won't). Results are undefined.
 */
int hashtable_cmp_long( void const * k1, void const * k2 );

/*
  A C-string hashing function for use with hashtable_create().  Uses
  the so-called "djb2" algorithm.

 Returns hashval_t_err if (!str).
*/
hashval_t hash_cstring_djb2( void const * str );

/*
  A C-string hashing function for use with hashtable_create().  Uses
  the so-called "sdbm" algorithm.

  Returns hashval_t_err if (!str).
*/
hashval_t hash_cstring_sdbm( void const * str );

/**
   An int/long hashing function for use with hashtable_create().  It
   requires that n point to a long integer, and it simply returns the
   value of n, or hashval_t_err on error (n is NULL).
 */
hashval_t hash_long( void const * n );


#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* __HASHTABLE_CWC22_SGB11_H__ */

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
