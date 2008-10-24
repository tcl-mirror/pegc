#include <stdlib.h>
#include <string.h> /* memset() */
#include "whgc.h"

#include "hashtable.h"
#include "hashtable_utility.h"

#include <stdio.h> /* only for debuggering. */

#if defined(__cplusplus)
extern "C" {
#  include <cassert>
#  define ARG_UNUSED(X)
#else
#  include <assert.h>
#  define ARG_UNUSED(X) X
#endif /* __cplusplus */

#if 1
#define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("MARKER: %s:%d:\n",__FILE__,__LINE__);
#endif

typedef hashval_t (*whgc_hash_f)(void const *key);
typedef int (*whgc_hash_cmp_f)(void const * lhs,void const *rhs);

/**
   Holder for generic gc data.
*/
struct whgc_gc_entry
{
    void * key;
    void * value;
    whgc_dtor_f keyDtor;
    whgc_dtor_f valueDtor;
};
typedef struct whgc_gc_entry whgc_gc_entry;
#define WHGC_GC_ENTRY_INIT {0,0,0,0}
static const whgc_gc_entry whgc_gc_entry_init = WHGC_GC_ENTRY_INIT;

struct whgc_entries
{
    whgc_gc_entry * entry;
    struct whgc_entries * left;
};
typedef struct whgc_entries whgc_entries;
#define WHGC_ENTRIES_INIT {0,0}
static const whgc_entries whgc_entries_init = WHGC_ENTRIES_INIT;

/**
   The main handle type used by the whgc API.
*/
struct whgc_context
{
    void const * client;
    hashtable * ht;
    whgc_entries * entries;
};

#define WHGC_CONTEXT_INIT {0,0,0}
static const whgc_context whgc_context_init = WHGC_CONTEXT_INIT;

/**
   A hash routine for use with the hashtable API. Simply
   casts k to the numeric value of its pointer address.
*/
static hashval_t whgc_hash_void_ptr( void const * k )
{
    typedef long kludge_t; /* must apparently be the same size as the platform's (void*) */
    return (hashval_t) (kludge_t) k;
}

/**
   A comparison function for use with the hashtable API. Matches
   only if k1 and k2 are the same address.
*/
static int whgc_hash_cmp_void_ptr( void const * k1, void const * k2 )
{
    return (k1 == k2);
}

/**
   A destructor for use with the hashtable API. Calls
   hashtable_destroy((hashtable*)k).
*/
static void whgc_free_hashtable( void * k )
{
    //MARKER; printf("Freeing HASHTABLE @%p\n",k);
    hashtable_destroy( (hashtable*)k );
}

static void whgc_free( void * k )
{
    //MARKER; printf("Freeing GENERIC (void*) @%p\n",k);
    free(k);
}

static void whgc_free_key( void * k )
{
    //MARKER; printf("Freeing KEY (void*) @%p\n",k);
    free(k);
}

static void whgc_free_value( void * k )
{
    //MARKER; printf("Freeing VALUE (void*) @%p\n",k);
    free(k);
}

/**
   Destructor for use with the hashtable API. Frees
   whgc_gc_entry objects.
*/
static void whgc_free_gc_entry( whgc_gc_entry *e )
{
    if( ! e ) return;
    //MARKER;printf("Freeing GC item e[@%p]: key[@%p]/%p() = val[@%p]/%p()\n",e,e->key,e->keyDtor,e->value,e->valueDtor);
    if( e->valueDtor )
    {
	//MARKER;printf("dtor'ing GC value %p( @%p )\n",e->valueDtor, e->value);
	e->valueDtor(e->value);
    }
    if( e->keyDtor )
    {
	//MARKER;printf("dtor'ing GC key %p( @%p )\n",e->keyDtor, e->key);
	e->keyDtor(e->key);
    }
    whgc_free(e);
}

/**
   A no-op "destructor" to assist in tracking down destructions.
*/
static void whgc_free_noop( void * ARG_UNUSED(v) )
{
    //MARKER;printf("dtor no-op @%p\n",v);
}

static hashtable * whgc_hashtable( whgc_context * cx )
{
    if( ! cx ) return 0;
    if( cx->ht ) return cx->ht;
    if( (cx->ht = hashtable_create( 10, whgc_hash_void_ptr, whgc_hash_cmp_void_ptr ) ) )
    {
	hashtable_set_dtors( cx->ht,
			     //0, 0
			     whgc_free_noop,whgc_free_noop
			     //whgc_free_noop,whgc_free_value
			     //whgc_free_noop,whgc_hashtable_free_gc_entry
			     );
    }
    return cx->ht;
}

whgc_context * whgc_create_context( void const * clientContext )
{
    whgc_context * cx = (whgc_context *)malloc(sizeof(whgc_context));
    if( ! cx ) return 0;
    *cx = whgc_context_init;
    cx->client = clientContext;
    return cx;
}


bool whgc_register( whgc_context * cx,
		    void * key, whgc_dtor_f keyDtor,
		    void * value, whgc_dtor_f valDtor )
{
    if( !key || !whgc_hashtable(cx) || (0 != hashtable_search(cx->ht, key)) )
    {
	return false;
    }
    whgc_gc_entry * e = (whgc_gc_entry*)malloc(sizeof(whgc_gc_entry));
    if( ! e ) return false;
    *e = whgc_gc_entry_init;
    e->key = key;
    e->keyDtor = keyDtor;
    e->value = value;
    e->valueDtor = valDtor;
    //MARKER;printf("Registering GC item e[@%p]: key[@%p]/%p() = val[@%p]/%p()\n",e,e->key,e->keyDtor,e->value,e->valueDtor);
    hashtable_insert( cx->ht, key, e );
#if 1
    whgc_entries * E = (whgc_entries*)malloc(sizeof(whgc_entries));
    if( ! E ) return false;
    *E = whgc_entries_init;
    E->entry = e;
    if( cx->entries )
    {
	E->left = cx->entries;
    }
    cx->entries = E;
#endif
    return true;
}

bool whgc_add( whgc_context * cx, void * key, whgc_dtor_f keyDtor )
{
    return whgc_register( cx, key, keyDtor, key, 0 );
}

void * whgc_search( whgc_context const * cx, void const * key )
{
    if( ! cx || !key || !cx->ht ) return 0;
    whgc_gc_entry * e = (whgc_gc_entry*)hashtable_search( cx->ht, key );
    return e ? e->value : 0;
}


void whgc_destroy_context( whgc_context * cx )
{
    if( ! cx ) return;
    cx->client = 0;

#if 1
    /**
       Destroy registered entries in reverse order of
       their registration.
    */
    whgc_entries * e = cx->entries;
    while( e )
    {
	//MARKER;printf("Want to clean up @%p\n",e);
	whgc_entries * left = e->left;
	whgc_gc_entry * E = e->entry;
#if 1
	whgc_free_gc_entry(E);
#else
	if( E->keyDtor )
	{
	    E->keyDtor( E->key );
	}
	if( E->valueDtor )
	{
	    E->valueDtor( E->value );
	}
#endif
	free(e);
	e = left;
    }
    cx->entries = 0;
#endif

    if( cx->ht )
    {
	whgc_free_hashtable( cx->ht );
	cx->ht = 0;
    }
    whgc_free(cx);
}

#if defined(__cplusplus)
} /* extern "C" */
#endif
