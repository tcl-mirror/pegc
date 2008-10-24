/* Copyright (C) 2004 Christopher Clark <firstname.lastname@cl.cam.ac.uk> */
/* Copyright (C) 2008 Stephan Beal (http://wanderinghorse.net/home/stephan/) */

#include "hashtable.h"
#include "hashtable_private.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static const hashtable
hashtable_init = { 0, /*tablelength*/
		   0, /* table */
		   0, /* entrycount */
		   0, /* loadlimit */
		   0, /* primeindex */
		   0, /* hashfn */
		   0, /* eqfn */
		   free, /* freeKey */
		   0 /* freeVal */
};
const hashval_t hashval_t_err = (hashval_t)-1;
/*
Credit for primes table: Aaron Krowne
 http://br.endernet.org/~akrowne/
 http://planetmath.org/encyclopedia/GoodHashTablePrimes.html
*/
static const hashval_t primes[] = {
53, 97, 193, 389,
769, 1543, 3079, 6151,
12289, 24593, 49157, 98317,
196613, 393241, 786433, 1572869,
3145739, 6291469, 12582917, 25165843,
50331653, 100663319, 201326611, 402653189,
805306457, 1610612741
};
const hashval_t prime_table_length = sizeof(primes)/sizeof(primes[0]);
const float max_load_factor = 0.65;

#if 1
size_t hashtable_index(size_t tablelength, size_t hashvalue)
{
    return (hashvalue % tablelength);
}
#endif

/**
   Custom implementation of ceil() to avoid a dependency on
   libmath on some systems (e.g. the tcc compiler).
*/
static hashval_t hashtable_ceil( double d )
{
    hashval_t x = (hashval_t)d;
    return (( d - (double)x )>0.0)
	? (x+1)
	: x;
}

/*****************************************************************************/
hashtable *
hashtable_create(hashval_t minsize,
		 //void (*dtor)(void*),
                 hashval_t (*hashf) (void const *),
                 int (*eqf) (void const *,void const *))
{
    hashtable *h;
    hashval_t pindex, size = primes[0];
    /* Check requested hashtable isn't too large */
    if (minsize > (1u << 30)) return NULL;
    /* Enforce size as prime */
    for (pindex=0; pindex < prime_table_length; pindex++) {
        if (primes[pindex] > minsize) { size = primes[pindex]; break; }
    }
    h = (hashtable *)malloc(sizeof(hashtable));
    if (NULL == h) return NULL; /*oom*/
    *h = hashtable_init;
    h->freeKey = free;
    h->freeVal = 0;
    h->table = (hashtable_entry **)malloc(sizeof(hashtable_entry*) * size);
    if (NULL == h->table) { free(h); return NULL; } /*oom*/
    memset(h->table, 0, size * sizeof(hashtable_entry *));
    h->tablelength  = size;
    h->primeindex   = pindex;
    h->entrycount   = 0;
    h->hashfn       = hashf;
    h->eqfn         = eqf;
    h->loadlimit    = hashtable_ceil(size * max_load_factor);
    return h;
}
/*****************************************************************************/
/**
   For internal use only: this func frees the memory associated
   with key, using h->freeKey (if set) or free() (by default).
   Results are undefined if key is not a key in h and if key
   is used after this func is called.
 */
void hashtable_free_key(hashtable const * h, void * key )
{
    if( h && h->freeKey ) (*(h->freeKey))( key );
}

void hashtable_free_val(hashtable const * h, void * val )
{
    if( h && h->freeVal ) (*(h->freeVal))( val );
}


/*****************************************************************************/
hashval_t
hash(hashtable *h, void const *k)
{
    if( !h || !k ) return hashval_t_err;
    /* Aim to protect against poor hash functions by adding logic here
     * - logic taken from java 1.4 hashtable source */
    hashval_t i = h->hashfn(k);
    i += ~(i << 9);
    i ^=  ((i >> 14) | (i << 18)); /* >>> */
    i +=  (i << 4);
    i ^=  ((i >> 10) | (i << 22)); /* >>> */
    return i;
}

/*****************************************************************************/
static int
hashtable_expand(hashtable *h)
{
    /* Double the size of the table to accomodate more entries */
    hashtable_entry **newtable;
    hashtable_entry *e;
    hashtable_entry **pE;
    hashval_t newsize, i, index;
    /* Check we're not hitting max capacity */
    if (h->primeindex == (prime_table_length - 1)) return 0;
    newsize = primes[++(h->primeindex)];

    newtable = (hashtable_entry **)malloc(sizeof(hashtable_entry*) * newsize);
    if (NULL != newtable)
    {
        memset(newtable, 0, newsize * sizeof(hashtable_entry *));
        /* This algorithm is not 'stable'. ie. it reverses the list
         * when it transfers entries between the tables */
        for (i = 0; i < h->tablelength; i++) {
            while (NULL != (e = h->table[i])) {
                h->table[i] = e->next;
                index = hashtable_index(newsize,e->h);
                e->next = newtable[index];
                newtable[index] = e;
            }
        }
        free(h->table);
        h->table = newtable;
    }
    /* Plan B: realloc instead */
    else 
    {
        newtable = (hashtable_entry **)
                   realloc(h->table, newsize * sizeof(hashtable_entry *));
        if (NULL == newtable) { (h->primeindex)--; return 0; }
        h->table = newtable;
        memset(newtable[h->tablelength], 0, newsize - h->tablelength);
        for (i = 0; i < h->tablelength; i++) {
            for (pE = &(newtable[i]), e = *pE; e != NULL; e = *pE) {
                index = hashtable_index(newsize,e->h);
                if (index == i)
                {
                    pE = &(e->next);
                }
                else
                {
                    *pE = e->next;
                    e->next = newtable[index];
                    newtable[index] = e;
                }
            }
        }
    }
    h->tablelength = newsize;
    h->loadlimit   = hashtable_ceil(newsize * max_load_factor);
    return -1;
}

/*****************************************************************************/
size_t
hashtable_count(hashtable const * h)
{
    return h->entrycount;
}

/*****************************************************************************/
int
hashtable_insert(hashtable *h, void *k, void *v)
{
    /* historic: This method allows duplicate keys - but they shouldn't be used */
  /* Stephan Beal, 13 Feb 2008: now simply replaces the value of existing entries. */
    hashval_t index;
    hashtable_entry *e;
    if (++(h->entrycount) > h->loadlimit)
    {
        /* Ignore the return value. If expand fails, we should
         * still try cramming just this value into the existing table
         * -- we may not have memory for a larger table, but one more
         * element may be ok. Next time we insert, we'll try expanding again.*/
        hashtable_expand(h);
    }
    e = (hashtable_entry *)malloc(sizeof(hashtable_entry));
    if (NULL == e) { --(h->entrycount); return 0; } /*oom*/
    void * dupev = hashtable_search(h, k);
    if( dupev )
    {
      e->v = v;
    }
    else
    {
      e->h = hash(h,k);
      index = hashtable_index(h->tablelength,e->h);
      e->k = k;
      e->v = v;
      e->next = h->table[index];
      h->table[index] = e;
    }
    return 1;
}

/*****************************************************************************/
void * /* returns value associated with key */
hashtable_search(hashtable *h, void const *k)
{
    hashtable_entry *e;
    hashval_t hashvalue, index;
    hashvalue = hash(h,k);
    index = hashtable_index(h->tablelength,hashvalue);
    e = h->table[index];
    while (NULL != e)
    {
        /* Check hash value to short circuit heavier comparison */
        if ((hashvalue == e->h) && (h->eqfn(k, e->k))) return e->v;
        e = e->next;
    }
    return NULL;
}

/*****************************************************************************/
void * /* returns value associated with key */
hashtable_take(hashtable *h, void const *k)
{
    /* TODO: consider compacting the table when the load factor drops enough,
     *       or provide a 'compact' method. */

    hashtable_entry *e;
    hashtable_entry **pE;
    void *v;
    hashval_t hashvalue, index;

    hashvalue = hash(h,k);
    index = hashtable_index(h->tablelength,hash(h,k));
    pE = &(h->table[index]);
    e = *pE;
    while (NULL != e)
    {
        /* Check hash value to short circuit heavier comparison */
        if ( (e->k==k)
             || ((hashvalue == e->h) && (h->eqfn(k, e->k)))
             )
        {
            *pE = e->next;
            h->entrycount--;
            v = e->v;
            hashtable_free_key( h, e->k );
            free(e);
            return v;
        }
        pE = &(e->next);
        e = e->next;
    }
    return NULL;
}

short hashtable_remove(hashtable *h, void const *k)
{
    void * v = hashtable_take(h,k);
    if( v )
    {
	hashtable_free_val(h,v);
    }
    return NULL != v;
}

void hashtable_set_val_dtor( hashtable * h, void (*dtor)( void * ) )
{
    if( h ) h->freeVal = dtor;
}
void hashtable_set_key_dtor( hashtable * h, void (*dtor)( void * ) )
{
    if( h ) h->freeKey = dtor;
}
void hashtable_set_dtors( hashtable * h, void (*keyDtor)( void * ), void (*valDtor)( void * ) )
{
    hashtable_set_key_dtor( h, keyDtor );
    hashtable_set_val_dtor( h, valDtor );
}

/*****************************************************************************/
/* destroy */
void
hashtable_destroy(hashtable *h)
{
    if( ! h ) return;
    hashval_t i;
    hashtable_entry *e, *f;
    hashtable_entry **table = h->table;
    for (i = 0; i < h->tablelength; i++)
    {
        e = table[i];
        while (NULL != e)
        {
            f = e;
            e = e->next;
            hashtable_free_key( h, f->k );
            hashtable_free_val( h, f->v );
            free(f);
        }
    }
    free(h->table);
    free(h);
}

int hashtable_cmp_cstring( void const * k1, void const * k2 )
{
    return (k1==k2) || (0 == strcmp( (char const *) k1, (char const *) k2 ));
}

int hashtable_cmp_long( void const * k1, void const * k2 )
{
    return *((long const*)k1) == *((long const*)k2);
}


hashval_t
hash_cstring_djb2( void const * vstr)
{ /* "djb2" algo code taken from: http://www.cse.yorku.ca/~oz/hash.html */
    if( ! vstr ) return hashval_t_err;
    hashval_t hash = 5381;
    int c = 0;
    char const * str = (char const *)vstr;
    if( ! str ) return hashval_t_err;
    while( (c = *str++) )
    {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

hashval_t
hash_cstring_sdbm(void const *vstr)
{ /* "sdbm" algo code taken from: http://www.cse.yorku.ca/~oz/hash.html */
    if( ! vstr ) return hashval_t_err;
    hashval_t hash = 0;
    int c = 0;
    char const * str = (char const *)vstr;
    if( ! str ) return hashval_t_err;
    while( (c = *str++) )
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

hashval_t
hash_long( void const * n )
{
    return n
        ? *((hashval_t const *)n)
        : hashval_t_err;
}

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
