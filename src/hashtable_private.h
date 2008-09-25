/* Copyright (C) 2002, 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */
/* Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/) */
#ifndef __HASHTABLE_PRIVATE_CWC22_H__
#define __HASHTABLE_PRIVATE_CWC22_H__
#include <stddef.h> // size_t
#ifdef __cplusplus
extern "C" {
#endif
/*****************************************************************************/
struct hashtable_entry
{
    void *k, *v;
    hashval_t h;
    struct hashtable_entry *next;
};
typedef struct hashtable_entry hashtable_entry;

struct hashtable {
    unsigned int tablelength;
    struct hashtable_entry **table;
    size_t entrycount;
    size_t loadlimit;
    size_t primeindex;
    hashval_t (*hashfn) (void const *k);
    int (*eqfn) (void const *k1, void const *k2);
    void (*freeKey)( void * );
    void (*freeVal)( void * );
};
typedef struct hashtable  hashtable;

/*****************************************************************************/
hashval_t
hash(hashtable *h, void const *k);

/*****************************************************************************/
/* indexFor */
static inline size_t
indexFor(size_t tablelength, size_t hashvalue) {
    return (hashvalue % tablelength);
};

/**
   Cleans up key using h->freeKey(key).
*/
void hashtable_free_key(hashtable const * h, void * key );
/**
   Cleans up val using h->freeVal(val).
*/
void hashtable_free_val(hashtable const * h, void * val );

/* Only works if tablelength == 2^N */
/*static inline size_t
indexFor(size_t tablelength, size_t hashvalue)
{
    return (hashvalue & (tablelength - 1u));
}
*/

/*****************************************************************************/
/*#define freekey(X) free(X)*/
/*define freekey(X) ; */


/*****************************************************************************/

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* __HASHTABLE_PRIVATE_CWC22_H__*/

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
