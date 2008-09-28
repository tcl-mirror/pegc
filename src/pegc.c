#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#if defined(__cplusplus)
extern "C" {
#define ARG_UNUSED(X)
#else
#define ARG_UNUSED(X) X
#endif

#if 1
#define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("MARKER: %s:%d:\n",__FILE__,__LINE__);
#endif

#define DUMPPOS(P) MARKER; printf("pos = [%s]\n", pegc_eof(P) ? "<EOF>" : pegc_latin1(*pegc_pos(P)) );
#include "pegc.h"
#include "hashtable.h"

/**
   Always returns false and does nothing.
*/
bool PegcRule_mf_failure( PegcRule const * self, pegc_parser * st );

const pegc_cursor pegc_cursor_init = { 0, 0, 0 };
#define PEGC_INIT_RULE(F,D) {	\
     F /* rule */,\
     D /* data */,\
     0 /* proxy */,\
     /* client */ { 0/* flags */,0 /* data */},\
     /* _internal */ { 0 /* key */}\
}

const PegcRule PegcRule_init = PEGC_INIT_RULE(PegcRule_mf_failure,0);
const PegcRule PegcRule_invalid = PEGC_INIT_RULE(0,0);

/**
   Internal holder for "match listener" data. Match listeners get
   notified any time pegc_set_match() is set.
*/
struct pegc_match_listener_data
{
    pegc_match_listener func;
    void * data;
    struct pegc_match_listener_data * next;
};
typedef struct pegc_match_listener_data pegc_match_listener_data;

const static pegc_match_listener_data
pegc_match_listener_data_init = {0,0,0};

struct pegc_parser
{
    char const * name; /* for debugging + error reporting purposes */
    pegc_cursor cursor;
    /**
       Set via pegc_set_match().
    */
    pegc_cursor match;
    pegc_match_listener_data * listeners;
    /**
       These hashtables are used to pass private data between routines
       and for garbage collection.
    */
    struct {
	hashtable * hashes; // GC for the following hashtables...
	hashtable * actions;
	hashtable * rulelists;
	hashtable * rules;
	hashtable * generic; // GC for (void*)
    } hashes;
#if 0
    struct {
	char * begin;
	char const * pos;
	unsigned int length;
    } keys;
#endif
    struct
    {
	void * data;
	void (*dtor)(void*);
    } client;
};

static const pegc_parser
pegc_parser_init = { 0, /* name */
		    {0,0,0}, /* cursor */
		    {0,0,0}, /* match */
		     0, /* listeners */
		    { /* hashes */
		    0, /* hashes */
		    0, /* actions */
		    0, /* rulelists */
		    0, /* rules */
		    0 /* generic */
		    },
#if 0
		     {/* keys */
		     0, /* begin */
		     0, /* pos */
		     0 /* length */
		     }
#endif
		    { /* client data */
		    0, /* data */
		    0 /* dtor */
		    }
};

void pegc_add_match_listener( pegc_parser * st,
			      pegc_match_listener f,
			      void * cdata )
{
    if( ! st || !f ) return;
    pegc_match_listener_data * d = (pegc_match_listener_data*) malloc(sizeof(pegc_match_listener_data));
    if( ! d ) return;
    d->func = f;
    d->data = cdata;
    pegc_match_listener_data * x = st->listeners;
    if( x )
    {
	while( x->next ) x = x->next;
	x->next = d;
    }
    else
    {
	st->listeners = d;
    }
}


static void * pegc_next_hash_key(pegc_parser * st)
{
    /**
       Note to self: We use a newly-malloc'ed value as a lookup key so
       we can avoid the question of "is this ID already in use". This
       solves several problems involved with using a numeric key and
       allows us to easily guaranty a unique key without having to
       check for dupes, overflows, and such.
    */
    if( ! st ) return NULL;
#if 0
    /* we can't do this b/c realloc() can move our pointers, which are our keys. */
    const unsigned int blockSize = 128;
    if( ! st->keys.begin )
    {
	st->keys.length = (blockSize * sizeof(char));
	st->keys.begin = (char *)malloc( st->keys.length + 1 );
	if( ! st->keys.begin )
	{
	    fprintf(stderr,"%s:%d:pegc_next_hash_key() could not allocate %ud bytes for keys!\n",
		    __FILE__,__LINE__,blockSize);
	    return 0;
	}
	memset( st->keys.begin, 0, st->keys.length + 1 );
	st->keys.pos = st->keys.begin;
    }
    if( st->keys.pos == (st->keys.begin + st->keys.length) )
    {
	const unsigned int sz = blockSize + st->keys.length;
	char * re = (char *)realloc( st->keys.begin, sz );
	if( ! re )
	{
	    fprintf(stderr,"%s:%d:pegc_next_hash_key() could not reallocate %ud bytes for keys!\n",
		    __FILE__,__LINE__,sz);
	    return 0;
	}
	memset( st->keys.pos, 0, sz - st->keys.length );
	st->keys.length += sz;
    }
#else
    /*
      FIXME: pre-allocate a memory block for action keys, and
      expand as necessary.  We can actually just pre-allocate a
      string and return successive addresses in that string. We
      "could" use the clob class to get auto-expansion on that
      array, but we may(?) run a risk of invalidating addresses on
      a realloc.

      This might require adding a pegc_parser argument so we can clean
      up the blocks properly.

      If we wanna be risky, we could just alloc 1k chars or so,
      and hope that app uses more than that. It's highly unlikely
      that even a very complex grammar needs more 100-200
      actions(?). My grammars to date have needed fewer than 20.
      
      Remember that we need to change the hashtables to not free
      their keys if we do this.
    */
    return malloc(1);
#endif
}

/**
   A hash routine for use with the hashtable API. Simply
   casts k to the numeric value of its pointer address.
*/
static hashval_t pegc_hash_void_ptr( void const * k )
{
    typedef long kludge_t; // must be the same size as the platform's (void*)
    return (hashval_t) (kludge_t) k;
}

/**
   A comparison function for use with the hashtable API. Matches
   only if k1 and k2 are the same address.
*/
static int pegc_hash_cmp_void_ptr( void const * k1, void const * k2 )
{
    return (k1 == k2);
}

/**
   A destructor for use with the hashtable API. Calls
   hashtable_destroy((hashtable*)k).
*/
static void pegc_free_hashtable( void * k )
{
    //MARKER; printf("Freeing hashtable %p\n",k);
    hashtable_destroy( (hashtable*)k );
}


/**
   Registers h with the garbage collector, such that pegc_destroy_parser(st)
   will destroy h.
*/
static void pegc_register_hashtable( pegc_parser * st, hashtable * h )
{
    if( ! st->hashes.hashes )
    {
	st->hashes.hashes = hashtable_create(10, pegc_hash_void_ptr, pegc_hash_cmp_void_ptr );
	hashtable_set_dtors( st->hashes.hashes, pegc_free_hashtable, 0 );
    }
    hashtable_insert(st->hashes.hashes, h, h);
}


/**
   if( P->hashes.H is not null then do nothing, otherwise initialize
   the P->hashes.H hashtable with the given key destructor (DK) and
   value destructor (DK) and register the hashtable with the garbage
   collector.
*/
#define HASH_INIT(P,H,DK,DV) \
    if( ! P->hashes.H ) { \
	P->hashes.H = hashtable_create(10, pegc_hash_void_ptr, pegc_hash_cmp_void_ptr ); \
	hashtable_set_dtors( P->hashes.H, DK, DV ); \
	pegc_register_hashtable(P,P->hashes.H); \
    }

struct pegc_action_info
{
    pegc_action action;
    void * data;
};
typedef struct pegc_action_info pegc_action_info;
/**
   Associates 'a' with st and key. Does not change ownership of 'a'.

   Ownership of key is transfered to st.
*/
static void pegc_actions_insert( pegc_parser * st, void * key, pegc_action_info * a )
{
    HASH_INIT(st,actions,free,free);
    hashtable_insert(st->hashes.actions, key, a);
}

/**
   Returns the pegc_action associated with the given key, or 0 if none
   was found.  See pegc_actions_insert().
*/
static pegc_action_info * pegc_actions_search( pegc_parser * st, void const * key )
{
    HASH_INIT(st,actions,free,free);
    void * r = hashtable_search( st->hashes.actions, key );
    //MARKER; printf("h=%p, key=%p, val=%p\n", h, key, r );
    return r ? (pegc_action_info*)r : 0;
}

/**
   Associates 'a' with st and key and transfers ownership of it to st.

   Ownership of key is transfered to st.
*/
static void pegc_rulelists_insert( pegc_parser * st, void * key, PegcRule const ** a )
{
    HASH_INIT(st,rulelists,free,free);
    hashtable_insert( st->hashes.rulelists, key, a);
}

/**
   Returns the list associated with the given key, or 0 if none was
   found.  See pegc_rulelists_insert().
*/
static PegcRule const ** pegc_rulelists_search( pegc_parser * st, void const * key )
{
    HASH_INIT(st,rulelists,free,free);
    void * r = hashtable_search( st->hashes.rulelists, key );
    //MARKER; printf("h=%p, key=%p, val=%p\n", h, key, r );
    return r ? (PegcRule const **)r : 0;
}

/**
   Associates 'a' with st and key and transfers ownership of it to st.

   Ownership of key is transfered to st.
*/
static void pegc_rules_insert( pegc_parser * st, void * key, PegcRule * a )
{
    HASH_INIT(st,rules,free,free);
    hashtable_insert( st->hashes.rules, key, a);
}

/**
   Returns the PegcRule associated with the given key, or 0 if none
   was found.  See pegc_rules_insert().
*/
static PegcRule const * pegc_rules_search( pegc_parser * st, void const * key )
{
    HASH_INIT(st,rules,free,free);
    void * r = hashtable_search( st->hashes.rules, key );
    //MARKER; printf("h=%p, key=%p, val=%p\n", h, key, r );
    return r ? (PegcRule const *)r : 0;
}

/**
   Adds item to the general-purposes garbage pool, to be cleaned up
   when pegc_destroy_parser(st) is called.
*/
static void pegc_gc_add( pegc_parser * st, void * item )
{
    HASH_INIT(st,generic,free,0);
    hashtable_insert( st->hashes.generic, item, item );
}

#undef HASH_INIT


bool pegc_parse( pegc_parser * st, PegcRule const * r )
{
    if( ! st || !r ) return false;
    return r->rule( r, st );
}

pegc_const_iterator pegc_latin1(int ch)
{
    static char latin1[256 * 2];
    static bool inited = false;
    if( ! inited )
    {
	inited = true;
	int i = 0;
	int ndx = 0;
	for( ; i <= 255; ++i, ndx += 2 )
	{
	    latin1[ndx] = (char)i;
	    latin1[ndx+1] = '\0';
	}
    }
    char const * r = ( (ch < 0) || (ch>255) )
	? 0
	: (latin1 + (2*ch));
    return r;
}



void pegc_set_client_data( pegc_parser * st, void * data, void (*dtor)(void*) )
{
    if( st )
    {
	st->client.data = data;
	st->client.dtor = dtor;
    }
}

void * pegc_get_client_data( pegc_parser * st )
{
    return st ? st->client.data : NULL;
}

bool pegc_eof( pegc_parser const * st )
{
    return !st
	    || !st->cursor.pos
	    || !*(st->cursor.pos)
	    || (st->cursor.pos >= st->cursor.end)
	    ;
}

pegc_cursor * pegc_iter( pegc_parser * st )
{
    return st ? &st->cursor : 0;
}

pegc_const_iterator pegc_begin( pegc_parser const * st )
{
    return st ? st->cursor.begin : 0;
}

pegc_const_iterator pegc_end( pegc_parser const * st )
{
    return st ? st->cursor.end : 0;
}


bool pegc_in_bounds( pegc_parser const * st, pegc_const_iterator p )
{
    return st && p && *p && (p>=pegc_begin(st)) && (p<pegc_end(st));
}


pegc_const_iterator pegc_pos( pegc_parser const * st )
{
    return st ? st->cursor.pos : 0;
}

bool pegc_set_pos( pegc_parser * st, pegc_const_iterator p )
{
    if( ! st || !p ) return false;
    if( pegc_in_bounds(st,p) || (p == pegc_end(st)) )
    {
	pegc_iter(st)->pos = p;
    }
    //MARKER; printf("pos=%p, p=%p, char=%d\n", pegc_iter(st)->pos, p, (p&&*p) ? *p : '!' );
    return pegc_iter(st)->pos == p;
}


bool pegc_advance( pegc_parser * st, long n )
{
    return ( 0 == n )
	? 0
	: (st ? pegc_set_pos( st, pegc_begin(st) + n ) : false);
}

bool pegc_bump( pegc_parser * st )
{
    return st ? pegc_advance(st, 1) : false;
}

long pegc_distance( pegc_parser const * st, pegc_const_iterator e )
{
    return (st&&e) ? (e - pegc_pos(st)) : 0;
}

pegc_cursor pegc_get_match_cursor( pegc_parser const * st )
{
    pegc_cursor cur = pegc_cursor_init;
    if( st )
    {
	cur.pos = cur.begin = st->match.begin;
	cur.end = st->match.end;
    }
    return cur;
}

pegc_iterator pegc_get_match_string( pegc_parser const * st )
{
    pegc_cursor cur = pegc_get_match_cursor(st);
    if( !st
	|| !cur.begin
	|| !*cur.begin
	)
    {
	return 0;
    }
    long sz = cur.end - cur.begin;
    if( sz <= 0 ) return 0;
    pegc_iterator ret = (pegc_iterator)calloc( sz, sizeof(pegc_char_t) );
    if( ! ret ) return 0;
    pegc_const_iterator it = cur.begin;
    pegc_iterator at = ret;
    for( ; it && *it && (it != cur.end); ++it, ++at )
    {
	*at = *it;
    }
    return ret;
}

bool pegc_set_match( pegc_parser * st, pegc_const_iterator begin, pegc_const_iterator end, bool movePos )
{
    if( !st
	|| (! pegc_in_bounds( st, begin ))
	|| (pegc_end(st) < end) )
    {
	MARKER;fprintf(stderr,"WARNING: pegc_set_match() is out of bounds.\n");
	return false;
    }
    //MARKER;printf("pegc_setting_match() setting match of %d characters.\n",(end-begin));
    st->match.begin = begin;
    st->match.end = end;
    if( movePos )
    {
	pegc_set_pos( st, end );
    }

    pegc_match_listener_data * x = st->listeners;
    while( x )
    {
	x->func( st, x->data );
	x = x->next;
    }
    return true;
}

bool pegc_matches_char( pegc_parser const * st, int ch )
{
    return st
	? (pegc_eof(st) ? false : (*pegc_pos(st) == ch))
	: false;
}

bool pegc_matches_chari( pegc_parser const * st, int ch )
{
    if( pegc_eof(st) ) return false;
    pegc_const_iterator p = pegc_pos(st);
    if( !p && !*p ) return false;
    return (p && *p)
	&& (tolower(*p) == tolower(ch));
}

bool pegc_matches_string( pegc_parser const * st, pegc_const_iterator str, long strLen, bool caseSensitive )
{
    if( pegc_eof(st) ) return false;
    if( strLen < 0 ) strLen = strlen(str);
    //MARKER; printf("Trying to match %ld chars of a string.\n",strLen);
    if( ! pegc_in_bounds(st, pegc_pos(st)+strLen-1) ) return false;
    //MARKER; printf("Trying to match %ld chars of a string.\n",strLen);
    pegc_const_iterator orig = pegc_pos(st);
    pegc_const_iterator p = orig;
    pegc_const_iterator sp = str;
    long i = 0;
    for( ; p && *p && (i < strLen); ++i, ++p, ++sp )
    {
	if( pegc_eof( st ) ) return false;
	if( caseSensitive )
	{
	    if( tolower(*p) != tolower(*sp) ) break;
	}
	else
	{
	    if( *p != *sp ) break;
	}
    }
    //MARKER; printf("matched string? == %d, i=%ld, strLen=%ld, str=[%s]\n", (i == strLen), i, strLen, str);
    if( i != strLen ) return false;
    return true;
}

void pegc_clear_match( pegc_parser * st )
{
    if( st ) pegc_set_match(st, 0, 0, false);
}


/**
   Always returns true and does nothing.
*/
bool PegcRule_mf_success( PegcRule const * self, pegc_parser * st );

/**
   Requires that self->data be a pegc_const_iterator. Matches if any
   character in that string matches the next char of st.
*/
bool PegcRule_mf_oneof( PegcRule const * self, pegc_parser * st );

/**
   Identical to PegcRule_mf_oneof() except that it compares
   case-insensitively by using tolower() for each compared
   character.
*/
bool PegcRule_mf_oneofi( PegcRule const * self, pegc_parser * st );

/**
   Requires that self->proxy be set to an object this routine can
   use as a proxy rule.

   This rule acts like a the regular expression (Rule)*. Always
   matches but may or may not consume input.
*/
bool PegcRule_mf_star( PegcRule const * self, pegc_parser * st );

/**
   Requires that self->proxy be set to an object this routine can
   use as a proxy rule.

   Works like PegcRule_mf_star(), but matches 1 or more times.
   This routine is "greedy", matching as long as it can UNLESS
   self->data (the rule) does not consume input, in which case
   this routine stops at the first match to avoid an endless loop.
*/
bool PegcRule_mf_plus( PegcRule const * self, pegc_parser * st );

/**
   Requires that self->data be a pegc_const_iterator. Matches if
   that string case-sensitively matches the next
   strlen(thatString) bytes of st.
*/
bool PegcRule_mf_string( PegcRule const * self, pegc_parser * st );

/**
   Identical to PegcRule_mf_string() except that it matches
   case-insensitively.
*/
bool PegcRule_mf_stringi( PegcRule const * self, pegc_parser * st );

/**
   Requires that self->data be an pegc_const_iterator. Matches if
   the first char of that string matches st.
*/
bool PegcRule_mf_char( PegcRule const * self, pegc_parser * st );
/**
   Case-insensitive form of PegcRule_mf_chari.
*/
bool PegcRule_mf_chari( PegcRule const * self, pegc_parser * st );

PegcRule pegc_r( PegcRule_mf rule, void const * data )
{
    PegcRule r = PegcRule_init;
    r.rule = rule; // ? rule : PegcRule_mf_failure;
    r.data = data;
    return r;
}

PegcRule * pegc_alloc_r( pegc_parser * st, PegcRule_mf const func, void const * data )
{
    if( ! st ) return 0;
    PegcRule r1 = pegc_r(func,data);
    PegcRule * r = (PegcRule*) malloc(sizeof(PegcRule));
    if( ! r ) return 0;
    *r = r1;
    if( st )
    {
	/**
	   Bug in waiting? We're using r->_internal.key, and probably shouldn't.
	*/
	//pegc_rules_insert( st, (r->_internal.key = pegc_next_hash_key(st)), r );
	pegc_gc_add( st, r );
    }
    return r;
}
PegcRule * pegc_copy_r( pegc_parser * st, PegcRule const src )
{
    PegcRule * r = st ? pegc_alloc_r( st, 0, 0 ) : 0;
    if( r ) *r = src;
    return r;
}

bool PegcRule_mf_failure( PegcRule const * self, pegc_parser * st )
{
    return false;
}
const PegcRule PegcRule_failure = PEGC_INIT_RULE(PegcRule_mf_failure,0);
bool PegcRule_mf_success( PegcRule const * self, pegc_parser * st )
{
    return true;
}
const PegcRule PegcRule_success = PEGC_INIT_RULE(PegcRule_mf_success,0);


static bool PegcRule_mf_oneof_impl( PegcRule const * self, pegc_parser * st, bool caseSensitive )
{
    if( ! st ) return false;
    pegc_const_iterator p = pegc_pos(st);
    pegc_const_iterator str = (pegc_const_iterator)self->data;
    if( ! str ) return false;
    int len = strlen(str);
    int i = 0;
    for( ; (i < len) && !pegc_eof(st); ++i )
    {
	if( caseSensitive
	    ? (*p == str[i])
	    : (tolower(*p) == tolower(str[i])) )
	{
	    //MARKER;
	    pegc_set_match( st, p, p+1, true );
	    return true;
	}
    }
    return false;

}


bool PegcRule_mf_oneof( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_oneof_impl(self,st,true);
}

bool PegcRule_mf_oneofi( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_oneof_impl(self,st,false);
}

PegcRule pegc_r_oneof( char const * list, bool caseSensitive )
{
    return pegc_r( caseSensitive
		  ? PegcRule_mf_oneof
		  : PegcRule_mf_oneofi,
		  list );
}


typedef struct PegcRule_mf_string_params PegcRule_mf_string_params;
static bool PegcRule_mf_string_impl( PegcRule const * self, pegc_parser * st, bool caseSensitive )
{
    if( !self || !st ) return false;
    pegc_const_iterator str = (pegc_const_iterator)self->data;
    if( ! str ) return false;
    int len = strlen(str);
    pegc_const_iterator p = pegc_pos(st);
    bool b = pegc_matches_string( st, str, len, caseSensitive );
    //MARKER; printf("matches? == %d\n", b);
    if( b )
    {
	pegc_set_match( st, p, p+len, true );
    }
    return b;
}

bool PegcRule_mf_string( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_string_impl( self, st, true );
}

bool PegcRule_mf_stringi( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_string_impl( self, st, false );
}

static bool PegcRule_mf_char_impl( PegcRule const * self, pegc_parser * st, bool caseSensitive )
{
    if( !self || !st || !self->data ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    char sd = *((char const *)(self->data));
    if( ! sd )
    {

    }
    //MARKER; printf("trying to match: [%c] =? [%c] data=[%p]\n", sd ? sd : '!', *orig, self->data );
    if( caseSensitive
	? pegc_matches_char(st,sd)
	: pegc_matches_chari(st,sd) )
    {
	pegc_set_match( st, orig, orig + 1, true );
	return true;
    }
    return false;

}
bool PegcRule_mf_char( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_char_impl(self,st,true);
}
bool PegcRule_mf_chari( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_char_impl(self,st,false);
}

bool PegcRule_mf_star( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st || !self->proxy ) return false;
    int matches = 0;
    
    pegc_const_iterator orig = pegc_pos(st);
    pegc_const_iterator p2 = orig;
    bool matched = false;
    do
    {
	if( (matched = self->proxy->rule( self->proxy, st )) )
	{
	    ++matches;
	    if( p2 == pegc_pos(st) )
	    { // avoid endless loop on non-consuming rules
		break;
	    }
	    p2 = pegc_pos(st);
	    continue;
	}
	break;
    } while( 1 );
    if( matches > 0 )
    {
	pegc_set_match( st, orig, p2, true );
    }
    return true;
}

bool PegcRule_mf_plus( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st || !self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    int matches = self->proxy->rule( self->proxy, st )
	? 1 : 0;
    pegc_const_iterator p2 = pegc_pos(st);
    while( (matches>0)
	   && (p2 != orig)
	   && self->proxy->rule( self->proxy, st )
	   )
    {
	++matches;
	if( p2 == pegc_pos(st) ) break; // didn't consume
	p2 = pegc_pos(st);
    }
    if( matches > 0 )
    {
	//MARKER; printf("plus got %d matches\n", matches );
	pegc_set_match( st, orig, p2, true );
	return true;
    }
    pegc_set_pos(st,orig);
    return false;
}

static bool PegcRule_mf_at( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st || !self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool rc = self->proxy->rule( self->proxy, st );
    pegc_set_pos(st,orig);
    return rc;
}

PegcRule pegc_r_at( PegcRule const * proxy )
{
    PegcRule r = pegc_r( PegcRule_mf_at, 0 );
    r.proxy = proxy;
    return r;
}

static bool PegcRule_mf_notat( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool ret = ! PegcRule_mf_at(self,st);
    pegc_set_pos(st,orig);
    return ret;
}

PegcRule pegc_r_notat( PegcRule const * proxy )
{
    PegcRule r = pegc_r( PegcRule_mf_notat, 0 );
    r.proxy = proxy;
    return r;
}


static bool PegcRule_mf_or( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = pegc_rulelists_search( st, self->_internal.key );
    for( ; li && *li && (*li)->rule; ++li )
    {
	if( (*li)->rule( *li, st ) ) return true;
    }
    pegc_set_pos(st,orig);
    return false;
}

static bool PegcRule_mf_and( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = pegc_rulelists_search( st, self->_internal.key );
    if(!li) return false;
    for( ; li && *li && (*li)->rule; ++li )
    {
	if( ! (*li)->rule( *li, st ) )
	{
	    pegc_set_pos(st,orig);
	    return false;
	}
    }
    return true;
}

PegcRule pegc_r_list_a( pegc_parser * st,  bool orOp, PegcRule const ** li )
{
    PegcRule r = pegc_r( orOp ? PegcRule_mf_or : PegcRule_mf_and, 0 );
    int count = 0;
    if(st && li)
    {
	PegcRule const ** counter = li;
	for( ; counter && *counter && (*counter)->rule; ++counter )
	{
	    ++count;
	}
    }
    if( ! count ) return r;
    void * key = pegc_next_hash_key(st);
    r._internal.key = key;
    PegcRule  const** list = (PegcRule  const**)(calloc( count+1, sizeof(PegcRule*) ));
    if( ! list )
    {
	fprintf(stderr,
		"%s:%d:pegc_r_list_a() serious error: calloc() of %d (PegcRule*) failed!\n",
		__FILE__,__LINE__,count);
	return r;
    }
    pegc_rulelists_insert( st, key, list );
    int i = 0;
    for( ; i < count; ++i )
    {
	list[i] = li[i];
    }
    list[i] = NULL;
    //MARKER;printf("Added %d items to rule list.\n",i);
    return r;
}


PegcRule pegc_r_list_v( pegc_parser * st, bool orOp, va_list ap )
{
    if( !st ) return PegcRule_failure;
    const unsigned int max = 50; // FIXME
    PegcRule const * li[max];
    memset( li, 0, sizeof(PegcRule*) * max );
    unsigned int pos = 0;
    while( st && (pos<max) )
    {
	PegcRule const * r = va_arg(ap,PegcRule const *);
	if( ! r ) break;
	li[pos++] = r;
    }
    if( pos == max )
    {
	fprintf(stderr,
		"WARNING: %s:%d:pegc_r_list_v(): overflow: too many list items. Truncating at %d items.\n",
		__FILE__,__LINE__,max);
    }
    return pegc_r_list_a( st, orOp, li );
}


PegcRule pegc_r_list_e( pegc_parser * st, bool orOp, ... )
{
    va_list vargs;
    va_start( vargs, orOp );
    PegcRule ret = pegc_r_list_v( st, orOp, vargs );
    va_end(vargs);
    return ret;
}

PegcRule pegc_r_or( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs )
{
    if( !st || !lhs || ! rhs )
    {
	return PegcRule_failure;
    }
    return pegc_r_list_e( st, true, lhs, rhs, 0 );
}

PegcRule pegc_r_or_e( pegc_parser * st, ... )
{
    va_list vargs;
    va_start( vargs, st );
    PegcRule ret = pegc_r_list_v( st, true, vargs );
    va_end(vargs);
    return ret;
}

PegcRule pegc_r_and( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs )
{
    if( !st || ! lhs || ! rhs )
    {
	return PegcRule_failure;
    }
    return pegc_r_list_e( st, false, lhs, rhs, 0 );
}

PegcRule pegc_r_and_e( pegc_parser * st, ... )
{
    va_list vargs;
    va_start( vargs, st );
    PegcRule ret = pegc_r_list_v( st, false, vargs );
    va_end(vargs);
    return ret;
}

static bool PegcRule_mf_action( PegcRule const * self, pegc_parser * st )
{
    if( !self || !st || !self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool rc = self->proxy->rule( self->proxy, st );
    //MARKER; printf("rule matched =? %d\n", rc);
    if( rc )
    {
	pegc_set_match( st, orig, pegc_pos(st), true );
	pegc_action_info * act = pegc_actions_search( st, self->_internal.key );
	//MARKER; printf("action = %p\n", act);
	if( act )
	{
	    (*act->action)( st, act->data );
	}
	else
	{
	    fprintf(stderr,"%s:%d:PegcRule_mf_action(): internal error: rule is missing its action information.\n");
	}
	return true;
    }
    pegc_set_pos( st, orig );
    return false;
}

PegcRule pegc_r_action( pegc_parser * st,
			PegcRule const * rule,
			pegc_action onMatch,
			void * clientData )
{
    //MARKER;
    PegcRule r = PegcRule_init;
    r.proxy = rule;
    if( ! st || !rule ) return r;
    r.rule = PegcRule_mf_action;
    if( onMatch )
    {
	pegc_action_info * info = (pegc_action_info*)malloc(sizeof(pegc_action_info));
	if( ! info ) return PegcRule_invalid;
	info->action = onMatch;
	info->data = clientData;
	r._internal.key = pegc_next_hash_key(st);
	//MARKER; printf("action key = %p\n", l);
	pegc_actions_insert( st, r._internal.key, info );
    }
    return r;
}


PegcRule pegc_r_star( PegcRule const * proxy )
{
    PegcRule r = pegc_r( PegcRule_mf_star, 0 );
    r.proxy = proxy;
    return r;
}

PegcRule pegc_r_plus( PegcRule const * proxy )
{
    PegcRule r = pegc_r( PegcRule_mf_plus, 0 );
    r.proxy = proxy;
    return r;
}

PegcRule pegc_r_string( pegc_const_iterator input, bool caseSensitive )
{
    return pegc_r( caseSensitive
		  ? PegcRule_mf_string
		  : PegcRule_mf_stringi,
		  input );
}

PegcRule pegc_r_char( pegc_char_t input, bool caseSensitive )
{
    return pegc_r( caseSensitive
		  ? PegcRule_mf_char
		  : PegcRule_mf_chari,
		   pegc_latin1(input));
}


/*
  Generate some PegcRule_XXX routines. F is the name of one of C's
  standard isXXX(int) funcs, without the 'is' prefix
*/
#define ACPRULE_ISA(F) \
static bool PegcRule_mf_ ## F( PegcRule const * self, pegc_parser * st ) \
{ \
    if( pegc_eof(st) ) return false; \
    pegc_const_iterator pos = pegc_pos(st); \
    if( is ## F(*pos) ) { \
	pegc_set_match( st, pos, pos+1, true ); \
	return true; \
    } \
    return false; \
}\
const PegcRule PegcRule_ ## F = {PegcRule_mf_ ## F,0,0}

ACPRULE_ISA(alnum);
ACPRULE_ISA(alpha);
ACPRULE_ISA(cntrl);
ACPRULE_ISA(digit);
ACPRULE_ISA(graph);
ACPRULE_ISA(lower);
ACPRULE_ISA(print);
ACPRULE_ISA(punct);
ACPRULE_ISA(space);
ACPRULE_ISA(upper);
ACPRULE_ISA(xdigit);


#undef ACPRULE_ISA
#define ACPRULE_ISA(F, string) const PegcRule PegcRule_ ## F = {PegcRule_mf_oneof, string}
ACPRULE_ISA(blank," \t");
#undef ACPRULE_ISA

static bool PegcRule_mf_opt( PegcRule const * self, pegc_parser * st )
{
    if( ! self->proxy ) return false;
    self->proxy->rule( self->proxy, st );
    return true;
}

PegcRule pegc_r_opt( PegcRule const * proxy )
{
    PegcRule r = pegc_r( PegcRule_mf_opt, 0 );
    r.proxy = proxy;
    return r;
}

static bool PegcRule_mf_eof( PegcRule const * self, pegc_parser * st )
{
    return pegc_eof(st);
}
const PegcRule PegcRule_eof = PEGC_INIT_RULE(PegcRule_mf_eof,0);
static bool PegcRule_mf_eol( PegcRule const * self, pegc_parser * st )
{
    if( ! self || ! st ) return false;
    const PegcRule crnl = pegc_r_string("\r\n",true);
    if( crnl.rule( &crnl, st ) ) return true;
    const PegcRule nl = pegc_r_char('\n',true);
    return nl.rule( &nl, st );
}

PegcRule pegc_r_eol()
{
    return pegc_r( PegcRule_mf_eol, 0 );
}

static bool PegcRule_mf_digits( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule digs = pegc_r_plus( &PegcRule_digit );
    if( digs.rule( &digs, st ) )
    {
	//MARKER;
	pegc_set_match( st, orig, pegc_pos(st), true );
	return true;
    }
    //MARKER;
    pegc_set_pos(st,orig);
    return false;
}
const PegcRule PegcRule_digits = {PegcRule_mf_digits,0};

static bool PegcRule_mf_int_dec( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    long myv = 0;
    int len = 0;
    int rc = sscanf(pegc_pos(st), "%ld%n",&myv,&len);
    if( (EOF == rc) || (0 == len) ) return false;
    if( ! pegc_advance(st,len) ) return false;
    pegc_set_match( st, orig, pegc_pos(st), true );
    return true;
}
const PegcRule PegcRule_int_dec = {PegcRule_mf_int_dec,0};

static bool PegcRule_mf_int_dec_strict( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    if( self->proxy && self->proxy->rule( self->proxy, st ) )
    {
	//DUMPPOS(st);
	pegc_set_match( st, orig, pegc_pos(st), true );
	return true;
    }
    //MARKER;
    pegc_set_pos(st,orig);
    return false;
}
static const PegcRule PegcRule_int_dec_strict = {PegcRule_mf_int_dec_strict,0};

PegcRule pegc_r_int_dec_strict( pegc_parser * st )
{
    if( ! st ) return PegcRule_invalid;
    /**
       Fixme: use a hashtable to find out if st already has an
       instance of this rule, and re-use it if it does.  The problem
       is where to put the key, unless we add a list of such keys to
       pegc_parser and search through them. i don't like that idea,
       though.

       Reminder we have to copy the rules here because we need the
       sub-rules to be valid pointers after this routine returns.
    */
    PegcRule r = PegcRule_int_dec_strict;
    PegcRule * sign = pegc_copy_r( st, pegc_r_oneof("+-",true) );
    PegcRule * prefix = pegc_copy_r( st, pegc_r_opt( sign ) );
    PegcRule * integer = pegc_copy_r( st, pegc_r_and_e( st, prefix, &PegcRule_digits, 0 ) );

    /**
       After we've matched digits we need to ensure that the next
       character is not illegal.
    */
    PegcRule * uscore = pegc_copy_r( st, pegc_r_oneof("._",true) );
    PegcRule * illegaltail = pegc_copy_r( st, pegc_r_or_e( st, &PegcRule_alpha, uscore, 0 ) );
    PegcRule * next = pegc_copy_r( st, pegc_r_notat( illegaltail ) );
    PegcRule * end = pegc_copy_r( st, pegc_r_or_e( st, &PegcRule_eof, next, 0 ) );
    r.proxy = pegc_copy_r( st, pegc_r_and( st, integer, end ) );
    return r;
}


static bool PegcRule_mf_ascii( PegcRule const * self, pegc_parser * st )
{
    if(  st && !pegc_eof(st) )
    {
	pegc_const_iterator p = pegc_pos(st);
	int ch = *p;
	if( (ch >= 0) && (ch <=127) )
	{
	    pegc_set_match( st, p, p+1, true );
	    return true;
	}
    }	
    return false;
}
const PegcRule PegcRule_ascii = {PegcRule_mf_ascii,0};


struct pegc_range_info
{
    unsigned int min;
    unsigned int max;
};
typedef struct pegc_range_info pegc_range_info;

static bool PegcRule_mf_repeat( PegcRule const * self, pegc_parser * st )
{
    if( ! self || !st || !self->data || !self->proxy ) return false;
    pegc_range_info const * info = self->data;
    if( ! info ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    unsigned int count = 0;
    while( self->proxy->rule( self->proxy, st ) )
    {
	MARKER; printf("Matched %u time(s)\n", count+1);
	if( (++count == info->max)
	    || pegc_eof(st)
	    || (orig == pegc_pos(st))
	    ) break;
    }
    if( 
       ((count >= info->min) && (count <= info->max) )
       )
    {
	MARKER; printf("pos=(%c .. %c) distance=%ld\n",*orig,*pegc_pos(st),pegc_distance(st,orig));
	pegc_set_match( st, orig, pegc_pos(st), true );
	return true;
    }
    pegc_set_pos( st, orig );
    return false;
}


PegcRule pegc_r_repeat( pegc_parser * st,
			PegcRule const * rule,
			unsigned int min,
			unsigned int max )
{
    if( ! st || !rule || pegc_eof(st) ) return PegcRule_invalid;
    if( (max < min) || (0==max) ) return PegcRule_invalid;
    if( (min == 1) && (max ==1) ) return *rule;
    if( (min == 0) && (max == 1) ) return pegc_r_opt( rule );
    pegc_range_info * info = (pegc_range_info *)malloc(sizeof(pegc_range_info));
    pegc_gc_add( st, info );
    info->min = min;
    info->max = max;
    PegcRule r = pegc_r( PegcRule_mf_repeat, info );
    r.proxy = rule;
    return r;
}

struct pegc_pad_info
{
    PegcRule const * left;
    PegcRule const * right;
    bool discard;
};
typedef struct pegc_pad_info pegc_pad_info ;
static bool PegcRule_mf_pad( PegcRule const * self, pegc_parser * st )
{
    if( ! self || !st || !self->rule ) return false;
    PegcRule const * left = 0;
    PegcRule const * right = 0;
    pegc_pad_info const * info = (pegc_pad_info const *)self->data;
    pegc_const_iterator orig = pegc_pos(st);
    pegc_const_iterator tail = 0;
    if( info && info->left )
    {
	info->left->rule( info->left, st );
	if( info->discard ) orig = pegc_pos(st);
    }
    bool ret = self->proxy->rule( self->proxy, st );
    tail = pegc_pos(st);
    if( ret && info && info->right )
    {
	info->right->rule( info->right, st );
	if( ! info->discard ) tail = pegc_pos(st);
    }
    if( ret )
    {
	pegc_set_match( st, orig, tail, true );
	return true;
    }
    pegc_set_pos( st, orig );
    return false;
}
PegcRule pegc_r_pad( pegc_parser * st,
		     PegcRule const * left,
		     PegcRule const * rule,
		     PegcRule const * right,
		     bool discardLeftRight )
{
    if( !st || !rule ) return PegcRule_invalid;
    if( ! left || !right ) return *rule;
    PegcRule r = pegc_r( PegcRule_mf_pad, 0 );
    r.proxy = rule;
    if( left || right )
    {
	pegc_pad_info * d = (pegc_pad_info *) malloc(sizeof(pegc_pad_info));
	if( ! d ) return PegcRule_invalid;
	d->discard = discardLeftRight;
	r.data = d;
	pegc_gc_add( st, d );
	if( left )
	{
	    d->left = pegc_copy_r( st, pegc_r_star( left ) );
	}
	if( right )
	{
	    d->right = pegc_copy_r( st, pegc_r_star( right ) );
	}
    }
    return r;
}


bool pegc_init_cursor( pegc_cursor * it, pegc_const_iterator begin, pegc_const_iterator end )
{
    if( end < begin ) return false;
    it->begin = it->pos = begin;
    it->end = end;
    return true;
}



bool pegc_create_parser( pegc_parser ** st, char const * inp, long len )
{
    if( ! st ) return false;
    *st = 0;
    if( !inp || !*inp ) return false;
    pegc_parser * p = (pegc_parser*) malloc( sizeof(pegc_parser) );
    if( ! p ) return false;
    *p = pegc_parser_init;

    if( len < 0 ) len = strlen(inp);
    pegc_init_cursor( &p->cursor, inp, inp + len );
    *st = p;
    return true;
    /**
       fixme: do a proper check by calculating the size of a (char const *) and making sure that
       ((thatval - len) > imp)
    */
}

bool pegc_destroy_parser( pegc_parser * st )
{
    if( ! st ) return false;
    if( st->client.data && st->client.dtor )
    {
	st->client.dtor( st->client.data );
	st->client.data = NULL;
    }

    if( st->hashes.hashes )
    {
	hashtable_destroy( st->hashes.hashes );
	st->hashes.hashes = 0;
    }

    pegc_match_listener_data * x = st->listeners;
    while( x )
    {
	pegc_match_listener_data * X = x->next;
	free(x);
	x = X;
    }
    free(st);
    return true;
}

#undef MARKER
#undef PEGC_INIT_RULE
#if defined(__cplusplus)
} // extern "C"
#endif
