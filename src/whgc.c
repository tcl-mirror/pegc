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
#define MARKER printf("**** MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("**** MARKER: %s:%d:\n",__FILE__,__LINE__);
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
    struct whgc_gc_entry * left;
    struct whgc_gc_entry * right;
};
typedef struct whgc_gc_entry whgc_gc_entry;
#define WHGC_GC_ENTRY_INIT {0,0,0,0,0,0}
static const whgc_gc_entry whgc_gc_entry_init = WHGC_GC_ENTRY_INIT;

#define WHGC_STATS_INIT {\
	0, /* entry_count */			\
	0, /* add_count */		\
	0 /* take_count */ \
    }
static const whgc_stats whgc_stats_init = WHGC_STATS_INIT;

struct whgc_listener
{
    struct whgc_listener * next;
    whgc_listener_f func;
};
typedef struct whgc_listener whgc_listener;
#define WHGC_LISTENER_INIT {0,0}
static const whgc_listener whgc_listener_init = WHGC_LISTENER_INIT;
/**
   The main handle type used by the whgc API.
*/
struct whgc_context
{
    void const * client;
    hashtable * ht;
    /**
       Holds the right-most (most recently added) entry. A cleanup,
       the list is walked leftwards to free the entries in reverse
       order.
    */
    whgc_gc_entry * current;
    whgc_listener * listeners;
    whgc_stats stats;
};

#define WHGC_CONTEXT_INIT {\
	0,/*client*/			   \
	    0,/*ht*/			   \
	    0,/*current*/		   \
	    0,/*listeners*/		   \
	WHGC_STATS_INIT}
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

/**
   A logging version of free().
 */
static void whgc_free( void * k )
{
    //MARKER; printf("Freeing GENERIC (void*) @%p\n",k);
    free(k);
}

/**
   Calls all registered listeners with the given
   parameters.
*/
static void whgc_fire_event( whgc_context const *cx,
			     enum whgc_event_types ev,
			     void const * key,
			     void const * val )
{
    //MARKER;printf("Firing event %d for cx @%p\n",ev,cx);
    whgc_listener * l = cx ? cx->listeners : 0;
    if( l )
    {
	whgc_event E;
	E.cx = cx;
	E.type = ev;
	E.key = key;
	E.value = val;
	while( l )
	{
	    if( l->func )
	    {
		//MARKER;printf("Firing @%p(cx=@%p,event=%d,key=@%p,val=@%p)\n",l->func,cx,ev,key,val);
		//l->func( cx, ev, key, val );
		l->func( E );
	    }
	    l = l->next;
	}
    }
}

/**
   Destructor for use with the hashtable API. Frees
   whgc_gc_entry objects by calling the assigned
   dtors and then calling free(e).

   The caller is expected to relink e's neighbors himself if needed
   before calling this function.

   The cx parameter is only used for firing a
   whgc_event_destructing_item event.
*/
static void whgc_free_gc_entry( whgc_context const * cx,
				whgc_gc_entry * e )
{
    if( ! e ) return;
    whgc_fire_event( cx, whgc_event_destructing_item, e->key, e->value );
    //MARKER;printf("Freeing GC item e[@%p]: key=[%p(@%p)] val[%p(@%p)]]\n",e,e->keyDtor,e->key,e->valueDtor,e->value);
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
	hashtable_set_dtors( cx->ht, whgc_free_noop, whgc_free_noop );
	/**
	   We use no-op dtors so we can log the destruction process, but cx->ht
	   does not own anything. Because we need predictable destruction order,
	   we manage a list of entries and destroy them in reverse order.
	 */
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

bool whgc_add_listener( whgc_context *cx, whgc_listener_f f )
{
    if( ! cx || !f ) return false;
    //MARKER;printf("Adding listener @%p() to cx @%p\n",f,cx);
    whgc_listener * l = (whgc_listener *)malloc(sizeof(whgc_listener));
    if( ! l ) return 0;
    *l = whgc_listener_init;
    l->func = f;
    whgc_add( cx, l, whgc_free_noop );
    whgc_listener * L = cx->listeners;
    if( L )
    {
	while( L->next ) L = L->next;
	L->next = l;
    }
    else
    {
	cx->listeners = l;
    }
    return true;
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
    ++(cx->stats.add_count);
    cx->stats.entry_count = hashtable_count(cx->ht);
    if( cx->current )
    {
	e->left = cx->current;
	cx->current->right = e;
    }
    cx->current = e;
    whgc_fire_event( cx, whgc_event_registered, e->key, e->value );
    return true;
}

bool whgc_add( whgc_context * cx, void * key, whgc_dtor_f keyDtor )
{
    return whgc_register( cx, key, keyDtor, key, 0 );
}

void * whgc_unregister( whgc_context * cx, void * key )
{
    if( ! cx || !cx->ht || !key ) return 0;
    whgc_gc_entry * e = (whgc_gc_entry*)hashtable_take( cx->ht, key );
    void * ret = e ? e->value : 0;
    if( e )
    {
	cx->stats.entry_count = hashtable_count(cx->ht);
	++(cx->stats.take_count);
	if( e->left ) e->left->right = e->right;
	if( e->right ) e->right->left = e->left;
	if( cx->current == e ) cx->current = (e->right ? e->right : e->left);
	/**
	   ^^^ this is pedantic. In theory cx->current must always be
	   the right-most entry, so we could do: cx->current=e->left;
	 */
	whgc_fire_event( cx, whgc_event_unregistered, e->key, e->value );
	free(e);
    }
    return ret;
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
    whgc_fire_event( cx, whgc_event_destructing_context, 0, 0 );
    cx->client = 0;
    //MARKER;printf("Cleaning up %u GC entries...\n",cx->stats.entry_count);
    if( cx->ht )
    {
	//MARKER;printf("Cleaning up %u GC entries...\n",hashtable_count(cx->ht));
	whgc_free_hashtable( cx->ht );
	cx->ht = 0;
    }
#if 1
    /**
       Destroy registered entries in reverse order of
       their registration.
    */
    whgc_gc_entry * e = cx->current;
    while( e )
    {
	whgc_fire_event( cx, whgc_event_unregistered, e->key, e->value ); /* a bit of a kludge, really. */
	//MARKER;printf("Want to clean up @%p\n",e);
	whgc_gc_entry * left = e->left;
	whgc_free_gc_entry(cx,e);
	e = left;
	--(cx->stats.entry_count);
    }
    cx->current = 0;
#endif
    whgc_listener * L = cx->listeners;
    cx->listeners = 0;
    while( L )
    {
	whgc_listener * l = L->next;
	free(L);
	L = l;
    }
    whgc_free(cx);
}

whgc_stats whgc_get_stats( whgc_context const * cx )
{
    return cx ? cx->stats : whgc_stats_init;
}

#if defined(__cplusplus)
} /* extern "C" */
#endif
