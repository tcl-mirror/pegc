#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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

#define DUMPPOS(P) MARKER; printf("pos = [%s]\n", pegc_eof(P) ? "<EOF>" : pegc_latin1(*pegc_pos(P)) );
#include "pegc.h"
#include "pegc_strings.h"
#include "hashtable.h"
#include "hashtable_utility.h"
#include "vappendf.h"
/**
   We only use the clob API for dynamic allocation of error strings.
   If i could settle on having pre-alloced, fixed-length error
   string storage i could:

   a) get rid of this dep.

   b) report error strings on alloc error (b/c we don't need to
   allocate the error string).

   Alternately, we could use the clob API and pre-alloc our error
   buffer there. That would allow us to take advantage of clob's
   printf-like API without relying on snprintf() and co.
*/
#include "clob.h"

/**
   Always returns false and does nothing.
*/
static bool PegcRule_mf_failure( PegcRule const * self, pegc_parser * st );

const pegc_cursor pegc_cursor_init = { 0, 0, 0 };
#define PEGC_INIT_RULE(F,D) {	\
     F /* rule */,\
     D /* data */,\
     0 /* proxy */,\
     /* client */ { 0/* flags */,0 /* data */}\
}
const PegcRule PegcRule_init = PEGC_INIT_RULE(PegcRule_mf_failure,0);
const PegcRule PegcRule_invalid = PEGC_INIT_RULE(0,0);

/**
   Allocates and initializes (count+1) rules, with the +1
   rule intended for use as an end-of-list marker. Each rule
   in the returned array is initalized to an empty rule.
*/
static PegcRule ** pegc_alloc_rules( unsigned int count )
{
    PegcRule ** li = (PegcRule **)calloc(count+1,sizeof(PegcRule));
    if( li )
    {
#if 1
	PegcRule * x = li[0];
	while( x )
	{
	    *x = PegcRule_invalid;
	    ++x;
	}
#else
	memset( li, 0, (count + 1) * sizeof(PegcRule) );
#endif
    }
    return li;
}

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

/**
   The main parser state type.
*/
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
    struct hashes {
	hashtable * hashes; /* GC for the following hashtables... */
	hashtable * rulelists; /* data for rule lists */
	hashtable * funcptr; /* for mapping funcs to shared/reusable objects. */
	hashtable * gc; /* holds pegc_gc_entry objects. */
    } hashes;
    struct errinfo {
	char * message;
	unsigned int line;
	unsigned int col;
	int clientID;
    } errinfo;
};

static unsigned int pegc_parser_instanceCount = 0;
static const pegc_parser
pegc_parser_init = { 0, /* name */
		    {0,0,0}, /* cursor */
		    {0,0,0}, /* match */
		     0, /* listeners */
		    { /* hashes */
		    0, /* hashes */
		    0, /* rulelists */
		    0, /* funcptr */
		    0 /* gc */
		    },
		     {/* errinfo */
		     0, /* message */
		     0, /* line */
		     0, /* col */
		     0 /* clientID */
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

/**
   A hash routine for use with the hashtable API. Simply
   casts k to the numeric value of its pointer address.
*/
static hashval_t pegc_hash_void_ptr( void const * k )
{
    typedef long kludge_t; /* must apparently be the same size as the platform's (void*) */
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
    //MARKER; printf("Freeing HASHTABLE @%p\n",k);
    hashtable_destroy( (hashtable*)k );
}

static void pegc_free( void * k )
{
    //MARKER; printf("Freeing GENERIC (void*) @%p\n",k);
    free(k);
}

static void pegc_free_key( void * k )
{
    //MARKER; printf("Freeing KEY (void*) @%p\n",k);
    free(k);
}

static void pegc_free_value( void * k )
{
    //MARKER; printf("Freeing VALUE (void*) @%p\n",k);
    free(k);
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
    //MARKER;printf("Registering hashtable @%p\n",h);
    hashtable_insert(st->hashes.hashes, h, h);
}



/**
   if P->hashes.H is not null then do nothing, otherwise initialize
   the P->hashes.H hashtable with the given key destructor (DK) and
   value destructor (DV) and register the hashtable with the garbage
   collector.
*/
#define PEGC_HASH_INIT(P,H,DK,DV) \
    if( ! P->hashes.H ) { \
	P->hashes.H = hashtable_create(10, pegc_hash_void_ptr, pegc_hash_cmp_void_ptr ); \
	hashtable_set_dtors( P->hashes.H, DK, DV ); \
	pegc_register_hashtable(P,P->hashes.H); \
    }

/**
   Holder for generic gc data.
*/
struct pegc_gc_entry
{
    void * key;
    void * value;
    void (*keyDtor)(void*);
    void (*valueDtor)(void*);
};
typedef struct pegc_gc_entry pegc_gc_entry;

/**
   Destructor for use with the hashtable API. Frees
   pegc_gc_entry objects.
*/
static void pegc_hashtable_free_gc_entry( void * v )
{
    pegc_gc_entry * e = (pegc_gc_entry*)v;
    if( ! e ) return;
    //MARKER;printf("Freeing GC item @%p\n",e);
    if( e->keyDtor ) (*(e->keyDtor))(e->key);
    if( e->valueDtor ) (*(e->valueDtor))(e->value);
    pegc_free(e);
}

bool pegc_gc_register( pegc_parser * st,
		       void * key, void (*keyDtor)(void*),
		       void * value, void (*valDtor)(void*) )
{
    if( ! st || !key ) return false;
    PEGC_HASH_INIT(st,gc,0,pegc_hashtable_free_gc_entry);
    if( ! st->hashes.gc ) return false;
    if( 0 != hashtable_search(st->hashes.gc, key) )
    {
	return false;
    }
    pegc_gc_entry * e = (pegc_gc_entry*)malloc(sizeof(pegc_gc_entry));
    if( ! e ) return false;
    e->key = key;
    e->keyDtor = keyDtor;
    e->value = value;
    e->valueDtor = valDtor;
    //MARKER;printf("Registering GC item @%p: @%p = @%p\n",e,key,value);
    hashtable_insert( st->hashes.gc, key, e );
    return true;
}

void * pegc_gc_search( pegc_parser const * st, void const * key )
{
    if( ! st || !key || !st->hashes.gc ) return 0;
    pegc_gc_entry * e = (pegc_gc_entry*)hashtable_search( st->hashes.gc, key );
    return e ? e->value : 0;
}

/**
   Adds item to the general-purposes garbage pool, to be cleaned
   up using free() when pegc_destroy_parser(st) is called. The
   item is stored in a hashtable and is used as its own
   key. Calling free(item) must result in defined behaviour
   or... well, results are undefined, obviously.
*/
bool pegc_gc_add( pegc_parser * st, void * item, void (*dtor)(void*) )
{
    return pegc_gc_register( st, item, dtor, item, 0 );
}

/**
   Holds internal data for pegc actions.
*/
struct pegc_action_info
{
    pegc_action action;
    void * data;
};
typedef struct pegc_action_info pegc_action_info;

/**
   Associates 'a' with st and key and transfers ownership of it to st.

   Ownership of key is transfered to st.
*/
static void pegc_rulelists_insert( pegc_parser * st, void * key, PegcRule const ** a )
{
    if( ! st || !key || !a ) return;
    PEGC_HASH_INIT(st,rulelists,0,0);//pegc_free_key,pegc_free);
    hashtable_insert( st->hashes.rulelists, key, a);
}

/**
   Returns the list associated with the given key, or 0 if none was
   found.  See pegc_rulelists_insert().
*/
static PegcRule const ** pegc_rulelists_search( pegc_parser * st, void const * key )
{
    if( !st || !key || ! st->hashes.rulelists ) return NULL;
    void * r = hashtable_search( st->hashes.rulelists, key );
    //MARKER; printf("h=%p, key=%p, val=%p\n", h, key, r );
    return r ? (PegcRule const **)r : 0;
}

/**
   This hashtable maps arbitrary data to a combination of (st,key). It
   is used by functions which dynamically create rules and cache those
   results for use in subsequent calls. The key should be a function
   pointer (the function we're mapping the data to) and the value is
   arbitrary. Ownership of all arguments is unchanged. Normally one
   needs to call pegc_gc_add(st,val,...) (or similar) if st is to take
   over ownership of it.
*/
static void pegc_funcptr_insert( pegc_parser * st, void * key, void * val )
{
    PEGC_HASH_INIT(st,funcptr,0,0);
    if( 0 == hashtable_change( st->hashes.funcptr, key, val ) )
    {
	hashtable_insert( st->hashes.funcptr, key, val );
    }
}

/**
   Returns the data associated with the given key, or 0 if none
   was found. See pegc_funcptr_insert().
*/
static void * pegc_funcptr_search( pegc_parser const * st, void const * key )
{
    if( ! st->hashes.funcptr ) return 0;
    void * r = hashtable_search( st->hashes.funcptr, key );
    //MARKER; printf("h=%p, key=%p, val=%p\n", st->hashes.funcptr, key, r );
    return r;
}
#undef PEGC_HASH_INIT

bool pegc_init_cursor( pegc_cursor * it, pegc_const_iterator begin, pegc_const_iterator end )
{
    if( begin && (0 == end) ) end = (begin + strlen(begin));
    if( !it || (end < begin) ) return false;
    it->begin = it->pos = begin;
    it->end = end;
    return true;
}

bool pegc_set_input( pegc_parser * st, pegc_const_iterator begin, long length )
{
    return pegc_set_error( st, 0, 0 )
	&& pegc_init_cursor( &st->cursor, begin,
			     (length < 0)
			     ? 0
			     : (begin + length) );
}

bool pegc_create_parser( pegc_parser ** st, char const * inp, long len )
{
    if( ! st ) return false;
    *st = 0;
    pegc_parser * p = (pegc_parser*) malloc( sizeof(pegc_parser) );
    if( ! p ) return false;
    ++pegc_parser_instanceCount;
    *p = pegc_parser_init;
    pegc_set_input( p, inp, len );
    *st = p;
    return true;
}

bool pegc_destroy_parser( pegc_parser * st )
{
    if( ! st ) return false;
    pegc_set_error( st, 0, 0 );
    if( st->hashes.hashes )
    {
	hashtable_destroy( st->hashes.hashes );
	st->hashes.hashes = 0;
    }
    pegc_match_listener_data * x = st->listeners;
    while( x )
    {
	pegc_match_listener_data * X = x->next;
	pegc_free(x);
	x = X;
    }
    pegc_free( st );
    return true;
}


bool pegc_parse( pegc_parser * st, PegcRule const * r )
{
    if( ! st || !r || (0 == r->rule) ) return false;
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


char const * pegc_get_error( pegc_parser const * st,
			     unsigned int * line,
			     unsigned int * col )
{
    if( ! st || !st->errinfo.message ) return 0;
    if( line ) *line = st->errinfo.line;
    if( col ) *col = st->errinfo.col;
    return st->errinfo.message;
}

static bool pegc_set_error_v( pegc_parser * st, char const * fmt, va_list vargs )
{
    if( ! st ) return false;
    if( st->errinfo.message ) pegc_free(st->errinfo.message);
    st->errinfo.message = 0;
    st->errinfo.line = st->errinfo.col = 0;
    if( ! fmt ) return true;
    char const * at = fmt;
    for( ; at && *at; ++at ){};
    unsigned int len = at - fmt;
    if( ! len )
    {
	st->errinfo.message = 0;
	st->errinfo.line = 1;
	st->errinfo.col = 0;
	return true;
    }
    else
    {
	pegc_line_col( st, &(st->errinfo.line), &(st->errinfo.col) );
	st->errinfo.message = clob_vmprintf( fmt, vargs );
	if( ! st->errinfo.message ) return false;
    }
    return true;
}

bool pegc_set_error( pegc_parser * st, char const * fmt, ... )
{
    va_list vargs;
    va_start( vargs, fmt );
    bool ret = pegc_set_error_v(st, fmt, vargs );
    va_end(vargs);
    return ret;
}

void pegc_set_client_data( pegc_parser * st, void * data )
{
#if 1
    pegc_funcptr_insert( st, (void *)pegc_set_client_data, data );
#else
    pegc_gc_register( st, (void *)pegc_set_client_data, 0, data, 0 );
#endif
}

void * pegc_get_client_data( pegc_parser const * st )
{
#if 1
    return pegc_funcptr_search( st, (void const *)pegc_set_client_data );
#else
    return pegc_gc_search( st, (void const *)pegc_set_client_data );
#endif
}

bool pegc_eof( pegc_parser const * st )
{
    return !st
	    || !st->cursor.pos
	    || !*(st->cursor.pos)
	    || (st->cursor.pos >= st->cursor.end)
	    ;
}

bool pegc_has_error( pegc_parser const * st )
{
    return st && (st->errinfo.message);
}
bool pegc_isgood( pegc_parser const * st )
{
    return st && !pegc_eof(st) && ! pegc_has_error(st);
}
pegc_cursor const * pegc_iter( pegc_parser const * st )
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
	st->cursor.pos = p;
    }
    //MARKER; printf("pos=%p, p=%p, char=%c\n", pegc_iter(st)->pos, p, (p&&*p) ? *p : '!' );
    return st->cursor.pos == p;
}


bool pegc_advance( pegc_parser * st, long n )
{
    return ( 0 == n )
	? false
	: (st ? pegc_set_pos( st, pegc_pos(st) + n ) : false);
}

bool pegc_bump( pegc_parser * st )
{
    return st ? pegc_advance(st, 1) : false;
}

long pegc_distance( pegc_parser const * st, pegc_const_iterator e )
{
    return (st&&e) ? (e - pegc_pos(st)) : 0;
}

bool pegc_line_col( pegc_parser const * st,
		    unsigned int * line,
		    unsigned int * col )
{
    if( !st || ! line || !col ) return false;
    *line = 1;
    *col = 0;
    pegc_const_iterator pos = pegc_pos(st);
    pegc_const_iterator beg = pegc_begin(st);
    for( ; beg && *beg && (beg != pos ); ++beg )
    {
	if( *beg == '\n' )
	{
	     ++(*line);
	     *col = 0;
	}
	else
	{
	    ++(*col);
	}
    }
    return true;
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
    pegc_iterator ret = (pegc_iterator)calloc( sz + 1, sizeof(pegc_char_t) );
    if( ! ret ) return 0;
    pegc_const_iterator it = cur.begin;
    pegc_iterator at = ret;
    for( ; it && *it && (it != cur.end); ++it, ++at )
    {
	*at = *it;
    }
    *at = '\0';
    return ret;
}

bool pegc_set_match( pegc_parser * st, pegc_const_iterator begin, pegc_const_iterator end, bool movePos )
{
    if( !st
	|| (! pegc_in_bounds( st, begin ))
	|| (pegc_end(st) < end) )
    {
	/**
	   Is this worth setting an error for?
	*/
#if 0
	MARKER; fprintf(stderr,"WARNING: pegc_set_match() is out of bounds.\n");
#else
	pegc_set_error( st, "pegc_set_match(parser=[%p],begin=[%p],end=[%p],%d) is out of bounds",
			st, begin, end, movePos );
#endif
	return false;
    }
    //MARKER;printf("pegc_setting_match() setting match of %d characters.\n",(end-begin));
    st->match.pos = st->match.begin = begin;
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
	? (pegc_eof(st) ? (ch == '\0') : (*pegc_pos(st) == ch))
	: false;
}

bool pegc_matches_chari( pegc_parser const * st, int ch )
{
    if( !st || pegc_has_error(st) ) return false;
    pegc_const_iterator p = pegc_pos(st);
    if( !p && !*p ) return false;
    return (p && *p)
	&& (tolower(*p) == tolower(ch));
}

bool pegc_matches_string( pegc_parser const * st, pegc_const_iterator str, long strLen, bool caseSensitive )
{
    if( !st || pegc_has_error(st) ) return false;
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


PegcRule pegc_r( PegcRule_mf rule, void const * data )
{
    PegcRule r = PEGC_INIT_RULE(rule,data);
    return r;
}

static bool PegcRule_mf_char_range( PegcRule const * self, pegc_parser * st )
{
    //MARKER;printf("self=%p, self->data=%p\n",self,self->data);
    if( !self || !self->data || !pegc_isgood( st ) ) return false;
    //MARKER;
#if 0
    unsigned int const * range = (unsigned int const *) self->data;
    if( ! range ) return false;
    int min = range[0];
    int max = range[1];
#else
    unsigned int evil = (unsigned int)self->data;
    int min = ((evil >> 8) & 0x00ff);
    int max = (evil & 0x00ff);
    //MARKER;printf("min=%c, max=%c, evil=%x\n",min,max,evil);
#endif
    pegc_const_iterator orig = pegc_pos(st);
    if( (*orig >= min) && (*orig <= max) )
    {
	//MARKER;printf("matched: ch=%c, min=%c, max=%c, evil=%x\n",*orig?*orig:'!',min,max,evil);
	pegc_set_match(st,orig,orig+1,true);
	return true;
    }
    return false;
}

PegcRule pegc_r_char_range( pegc_char_t start, pegc_char_t end )
{
    if( start > end )
    {
	pegc_char_t x = end;
	start = end;
	end = x;
    }
    /**
       i'm not proud of this, but i want to avoid allocating
       for this rule, since i expect it to be used often.
     */
    assert( (sizeof(unsigned int) <= sizeof(void*)) && "pegc_r_char_range(): invalid use of (void*) to store int value: (void*) is too small!");
    unsigned int evil = ((start << 8) | (0x00ff & end));
    PegcRule r = pegc_r( PegcRule_mf_char_range, (void*)evil );
    //MARKER;printf("min=%c, max=%c, evil=%x, r.data=%p\n",start,end,evil,r.data);
    return r;
}

static bool PegcRule_mf_char_spec( PegcRule const * self, pegc_parser * st )
{
    if( ! pegc_isgood( st ) || ! self ) return false;
    char const * spec = (char const *) self->data;
    pegc_const_iterator orig = pegc_pos(st);
    //MARKER;printf("inChar=%c format=%s strlen==%d\n",*orig,spec,strlen(spec));
    char ch[] = {0,0};
    int rc = sscanf(pegc_pos(st), spec, ch);
    //MARKER;printf("inChar=%c sscanf rc=%d, ch=%s\n",*orig,rc,ch);
    if( 0 == rc ) return false;
    pegc_set_match( st, orig, orig + 1, true );
    return true;
}

PegcRule pegc_r_char_spec( pegc_parser * st, char const * spec )
{
    if( ! st || !spec || (*spec != '[') ) return PegcRule_invalid;
    const int fmtSize = strlen(spec) + 5;
    char * fmt = mnprintf( fmtSize, "%%1%s", spec );
    if( ! fmt ) return PegcRule_invalid;
    pegc_gc_add( st, fmt, pegc_free_value );
    return pegc_r(PegcRule_mf_char_spec, fmt);
}

static bool PegcRule_mf_error( PegcRule const * self, pegc_parser * st )
{
    if( ! self || ! st ) return false;
    char const * msg = self->data ? (char const *)self->data : "unspecified error";
    pegc_set_error( st, msg );
    return false;
}

PegcRule pegc_r_error_v( pegc_parser * st, char const * fmt, va_list vargs )
{
    if( ! st || !fmt ) return PegcRule_invalid;
    Clob * c = 0;
    clob_init( &c, 0, 128 );
    clob_vappendf( c, fmt, vargs );
    char * str = clob_take_buffer( c );
    if( ! str ) return PegcRule_invalid;
    pegc_gc_add( st, str, pegc_free_value );
    clob_finalize(c);
    PegcRule r = pegc_r( PegcRule_mf_error, str );
    return r;
}

PegcRule pegc_r_error_e( pegc_parser * st, char const * fmt, ... )
{
    va_list vargs;
    va_start( vargs, fmt );
    PegcRule ret = pegc_r_error_v( st, fmt, vargs );
    va_end(vargs);
    return ret;
}

PegcRule pegc_r_error( char const * errstr  )
{
    return pegc_r( PegcRule_mf_error, errstr );
}


void pegc_clear_match( pegc_parser * st )
{
    if( st ) pegc_set_match(st, 0, 0, false);
}


bool pegc_is_rule_valid( PegcRule const * r )
{
    return r && r->rule;
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


PegcRule * pegc_alloc_r( pegc_parser * st, PegcRule_mf const func, void const * data )
{
    PegcRule * r = (PegcRule*) malloc(sizeof(PegcRule));
    if( ! r ) return 0;
    *r = pegc_r(func,data);
    if( st )
    {
	pegc_gc_add( st, r, pegc_free_key );
    }
    return r;
}
PegcRule * pegc_copy_r( pegc_parser * st, PegcRule const src )
{
    PegcRule * r = pegc_alloc_r( st, 0, 0 );
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
    if( ! pegc_isgood(st) ) return false;
    pegc_const_iterator p = pegc_pos(st);
    if( !*p ) return false;
    pegc_const_iterator str = (pegc_const_iterator)self->data;
    if( ! str ) return false;
    int len = strlen(str);
    int i = 0;
    for( ; (i < len); ++i )
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
    if( ! pegc_isgood(st) || !self ) return false;
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
    if( ! pegc_isgood(st) || !self || !self->data ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    char sd = *((char const *)(self->data));
    if( ! sd )
    {
	return false;
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
    if( !st || pegc_has_error(st) || !self || !self->proxy ) return false;
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

/**
   Requires that self->proxy be set to an object this routine can
   use as a proxy rule.

   Works like PegcRule_mf_star(), but matches 1 or more times.
   This routine is "greedy", matching as long as it can UNLESS
   self->data (the rule) does not consume input, in which case
   this routine stops at the first match to avoid an endless loop.
*/
static bool PegcRule_mf_plus( PegcRule const * self, pegc_parser * st )
{
    if( !st || pegc_has_error(st) || !self || !self->proxy ) return false;
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
    if( !self || !self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool rc = self->proxy->rule( self->proxy, st );
    pegc_set_pos(st,orig);
    return rc;
}

PegcRule pegc_r_at( PegcRule const * proxy )
//PegcRule pegc_r_at( PegcRule const * proxy )
{
    if( !proxy ) return PegcRule_invalid;
    PegcRule r = pegc_r( PegcRule_mf_at, 0 );
    r.proxy = proxy;
    return r;
}

static bool PegcRule_mf_notat( PegcRule const * self, pegc_parser * st )
{
    if( !st || pegc_has_error(st) || !self ) return false;
#if 0
    pegc_const_iterator orig = pegc_pos(st);
    bool ret = ! PegcRule_mf_at(self,st);
    pegc_set_pos(st,orig);
    return ret;
#else
    return  ! PegcRule_mf_at(self,st);
#endif
}

PegcRule pegc_r_notat( PegcRule const * proxy )
{
    if( ! proxy ) return PegcRule_invalid;
    PegcRule r = pegc_r( PegcRule_mf_notat, 0 );
    r.proxy = proxy;
    return r;
}


static bool PegcRule_mf_or( PegcRule const * self, pegc_parser * st )
{
    if( !self ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = (PegcRule const **)self->data;
    for( ; li && *li && (*li)->rule; ++li )
    {
	//MARKER;
	if( (*li)->rule( *li, st ) )
	{
	    pegc_set_match( st, orig, pegc_pos(st), true );
	    return true;
	}
    }
    //MARKER;
    pegc_set_pos(st,orig);
    return false;
}

static bool PegcRule_mf_and( PegcRule const * self, pegc_parser * st )
{
    if( !self ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = (PegcRule const **)self->data;
    if(!li) return false;
    for( ; li && *li && (*li)->rule; ++li )
    {
	if( ! (*li)->rule( *li, st ) )
	{
	    pegc_set_pos(st,orig);
	    return false;
	}
    }
    pegc_set_match( st, orig, pegc_pos(st), true );
    return true;
}

PegcRule pegc_r_list_a( pegc_parser * st, bool orOp, PegcRule const ** li )
{
    if( ! st || !li ) return PegcRule_invalid;
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
    PegcRule const ** list = (PegcRule const **)pegc_alloc_rules( count );
    if( ! list )
    {
	if(0)
	{ /* this might need to alloc. */
	    MARKER;
	    fprintf(stderr,
		    "%s:%d:pegc_r_list_a() serious error: calloc() of %d (PegcRule*) failed!\n",
		    __FILE__,__LINE__,count);
	}
	return PegcRule_invalid;
    }
    pegc_gc_add( st, list, pegc_free_value );
    r.data = list;
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
    if( !st ) return PegcRule_invalid;
    const unsigned int blockSize = 5; /* number of rules to allocate at a time. */
    unsigned int count = 0;
    PegcRule const ** li = 0;
    unsigned int pos = 0;
    while( true )
    {
	PegcRule const * vr = va_arg(ap,PegcRule const *);
	if( ! vr ) break;
	if( pos == count )
	{ /* (re)allocate list */
	    count += blockSize;
	    //MARKER;printf("(re)allocating list for %u items.\n",count);
	    if( ! li )
	    {
		li = (PegcRule const **)pegc_alloc_rules( count );
		if( ! li ) break;
	    }
	    else
	    {
		void * re = realloc( li, sizeof(PegcRule*) * (count + 1)  );
		if( ! re ) break;
		li = (PegcRule const **)re;
	    }
	}
	li[pos++] = vr;
    }
    if( ! pos || !li )
    {
	if(li) pegc_free(li);
	return PegcRule_invalid;
    }
    li[pos] = 0;
    PegcRule r = pegc_r_list_a( st, orOp, li );
    pegc_free(li);
    return r;
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
    if( !st || pegc_has_error(st) || !self || !self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool rc = self->proxy->rule( self->proxy, st );
    //MARKER; printf("rule matched =? %d\n", rc);
    if( rc )
    {
	pegc_set_match( st, orig, pegc_pos(st), true );
	pegc_action_info * const act = (pegc_action_info * const)self->data;
	//MARKER; printf("action = %p\n", act);
	if( act )
	{
	    act->action( st, act->data );
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
    pegc_action_info * info = 0;
    if( ! st || !rule ) return PegcRule_invalid;
    if( onMatch )
    {
	info = (pegc_action_info*)malloc(sizeof(pegc_action_info));
	if( ! info )
	{
	    return PegcRule_invalid;
	}
	pegc_gc_add( st, info, pegc_free_value );
	info->action = onMatch;
	info->data = clientData;
    }
    PegcRule r = pegc_r( PegcRule_mf_action, info );
    r.proxy = rule;
    //MARKER; printf("action key = %p\n", l);
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
    if( !st || !pegc_isgood(st)  ) return false; \
    pegc_const_iterator pos = pegc_pos(st); \
    if( is ## F(*pos) ) { \
	pegc_set_match( st, pos, pos+1, true ); \
	return true; \
    } \
    return false; \
}\
const PegcRule PegcRule_ ## F = PEGC_INIT_RULE(PegcRule_mf_ ## F,0);
/* SunStudio compiler says: warning: syntax error:  empty declaration. No clue what he's talking about. */

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

static bool PegcRule_mf_blanks( PegcRule const * self, pegc_parser * st )
{
    if( !st || pegc_has_error(st) || !self ) return false;
    const PegcRule r = pegc_r_star( &PegcRule_blank );
    return r.rule( &r, st );
}
const PegcRule PegcRule_blanks = {PegcRule_mf_blanks,0};


static bool PegcRule_mf_opt( PegcRule const * self, pegc_parser * st )
{
    if( !st || pegc_has_error(st) || !self || !self->proxy ) return false;
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
    bool r = pegc_eof(st);
    //MARKER; printf("at EOF? == %d\n",r);
    return r;
}
const PegcRule PegcRule_eof = PEGC_INIT_RULE(PegcRule_mf_eof,0);
static bool PegcRule_mf_eol( PegcRule const * self, pegc_parser * st )
{
    if( ! pegc_isgood(st) || !self ) return false;
    const PegcRule crnl = pegc_r_string("\r\n",true);
    if( crnl.rule( &crnl, st ) ) return true;
    const PegcRule nl = pegc_r_oneof("\n\r",true);
    return nl.rule( &nl, st );
}

const PegcRule PegcRule_eol = PEGC_INIT_RULE(PegcRule_mf_eol,0);

static bool PegcRule_mf_bol( PegcRule const * self, pegc_parser * st )
{
    if( ! pegc_isgood(st) || !self ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    return ( pegc_begin(st) == orig )
	|| ('\n' == orig[-1]);
}
const PegcRule PegcRule_bol = PEGC_INIT_RULE(PegcRule_mf_bol,0);

static bool PegcRule_mf_digits( PegcRule const * self, pegc_parser * st )
{
    if( ! pegc_isgood(st) || !self ) return false;
    const PegcRule digs = pegc_r_plus( &PegcRule_digit );
    return digs.rule( &digs, st );
}
const PegcRule PegcRule_digits = {PegcRule_mf_digits,0};

static bool PegcRule_mf_int_dec( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    long myv = 0;
    int len = 0;
    int rc = sscanf(pegc_pos(st), "%ld%n",&myv,&len);
    //MARKER;printf("sscanf(%%ld) rc=%d len=%ld\n", rc, len );
    if( (1 != rc) || (0 == len) ) return false;
    //MARKER;printf("sscanf(%%ld) rc=%d len=%ld\n", rc, len );
    //if( ! pegc_advance(st,len) ) return false;
    //MARKER;printf("sscanf(%%ld) rc=%d len=%ld\n", rc, len );
    pegc_set_match( st, orig, orig + len, true );
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
    //pegc_set_pos(st,orig);
    return false;
}
static const PegcRule PegcRule_int_dec_strict = {PegcRule_mf_int_dec_strict,0};

PegcRule pegc_r_int_dec_strict( pegc_parser * st )
{
    if( ! st ) return PegcRule_invalid;
    PegcRule * r = 0;
    void * x = pegc_gc_search( st, (void const *)PegcRule_mf_int_dec_strict );
    if( x )
    {
	r = (PegcRule *)x;
	//MARKER;printf("Got cached rule for pegc_r_int_dec_strict()\n");
    }
    else
    {
	r = pegc_alloc_r( st, PegcRule_mf_int_dec_strict, 0 );
	pegc_gc_register( st, (void *)PegcRule_mf_int_dec_strict, 0, r, 0 );
	//MARKER;printf("Creating shared proxy rule for pegc_r_int_dec_strict()\n");
	/**
	   Reminder: we have to copy the rules here because we need
	   the sub-rules to be valid pointers after this routine
	   returns.
	*/
	PegcRule const * integer = &PegcRule_int_dec;
	/**
	   After we've matched digits we need to ensure that the next
	   character is [what we consider to be] legal.
	*/
#define CP(X) pegc_copy_r(st,X)
	PegcRule const * punct = CP(pegc_r_oneof("._",true));
	PegcRule const * illegaltail = CP(pegc_r_or_e( st, &PegcRule_alpha, punct, 0 ));
	PegcRule const * next = CP(pegc_r_notat( illegaltail ) );
	PegcRule const * end = CP(pegc_r_or_e( st, &PegcRule_eof, next, 0 ) );
	r->proxy = CP(pegc_r_and_e( st, integer, end, 0 ));
#undef CP
    }
    /**
       ^^^ instead of setting r.proxy we could just do
       pegc_funcptr_search() in PegcRule_mf_int_dec_strict. That might
       complicate debugging, thought, as our physical rule chain would
       be effectively broken, so we couldn't effectively traverse the
       rule chain in a debugger. Also, r.proxy is there, costs us
       nothing extra, and is const-time access (unlike the hashtable
       lookup).
    */
#if 0
    return r ? *r : PegcRule_invalid;
#else
    return r ? *((PegcRule const *)r) : PegcRule_invalid;
    /*
      ^^^ This cast is to get around a completely stupid warning from
      the SunStudio compiler:

    "pegc.c", line xxxx: warning: operands have incompatible types:
    struct PegcRule {pointer to function(pointer to const struct
    PegcRule {..}, pointer to struct pegc_parser {..}) returning char
    rule, pointer to const void data, pointer to const struct PegcRule
    {..} proxy, struct client {..} client} ":" const struct PegcRule
    {pointer to function(pointer to const struct PegcRule {..},
    pointer to struct pegc_parser {..}) returning char rule, pointer
    to const void data, pointer to const struct PegcRule {..} proxy,
    struct client {..} client}

	
    The problem appears to be that PegcRule_invalid is explicitely
    const, whereas *r is not. This confuses the compiler, though in my
    opinion that's a compiler bug.
    */
#endif

}

static bool PegcRule_mf_double( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    double myv = 0.0;
    int len = 0;
    int rc = sscanf(pegc_pos(st), "%lf%n",&myv,&len);
    if( (EOF == rc) || (0 == len) ) return false;
    return pegc_set_match( st, orig, orig + len, true );
}

const PegcRule PegcRule_double = {PegcRule_mf_double,0};


static bool PegcRule_mf_ascii_impl( PegcRule const * self, pegc_parser * st, int max )
{
    if(  st && pegc_isgood(st) )
    {
	pegc_const_iterator p = pegc_pos(st);
	int ch = *p;
	if( (ch >= 0) && (ch <=max) )
	{
	    pegc_set_match( st, p, p+1, true );
	    return true;
	}
    }	
    return false;
}
static bool PegcRule_mf_latin1( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_ascii_impl(self,st,255);
}
static bool PegcRule_mf_ascii( PegcRule const * self, pegc_parser * st )
{
    return PegcRule_mf_ascii_impl(self,st,127);
}
const PegcRule PegcRule_latin1 = {PegcRule_mf_latin1,0};
const PegcRule PegcRule_ascii = {PegcRule_mf_ascii,0};


struct pegc_range_info
{
    unsigned int min;
    unsigned int max;
};
typedef struct pegc_range_info pegc_range_info;

static bool PegcRule_mf_repeat( PegcRule const * self, pegc_parser * st )
{
    if( ! self || !st || !self->data || !self->proxy || pegc_has_error(st) ) return false;
    pegc_range_info const * info = self->data;
    if( ! info ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    unsigned int count = 0;
    while( self->proxy->rule( self->proxy, st ) )
    {
	if( (++count == info->max)
	    || pegc_eof(st)
	    || (orig == pegc_pos(st))
	    ) break;
    }
    if( !pegc_has_error(st) &&
       ((count >= info->min) && (count <= info->max) )
       )
    {
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
    if( ! st || !rule ) return PegcRule_invalid;
    if( (max < min) || (0==max) ) return PegcRule_invalid;
    if( (min == 1) && (max ==1) ) return *rule;
    if( (min == 0) && (max == 1) ) return pegc_r_opt( rule );
    /**
       TODO: consider stuffing min into st->data and max into st->proxy, to
       avoid an allocation here.
    */
    pegc_range_info * info = (pegc_range_info *)malloc(sizeof(pegc_range_info));
    pegc_gc_add( st, info, pegc_free_key );
    info->min = min;
    info->max = max;
    PegcRule r = pegc_r( PegcRule_mf_repeat, info );
    r.proxy = rule;
    return r;
}

struct pegc_pad_info
{
    PegcRule left;
    PegcRule right;
    bool discard;
};
typedef struct pegc_pad_info pegc_pad_info ;
static bool PegcRule_mf_pad( PegcRule const * self, pegc_parser * st )
{
    if( ! self || !st || pegc_has_error(st) || !self->rule || !self->data ) return false;
    PegcRule const * left = 0;
    PegcRule const * right = 0;
    pegc_pad_info const * info = (pegc_pad_info const *)self->data;
    pegc_const_iterator orig = pegc_pos(st);
    pegc_const_iterator tail = 0;
    if( info && info->left.rule )
    {
	info->left.rule( &(info->left), st );
	if( info->discard ) orig = pegc_pos(st);
    }
    bool ret = self->proxy->rule( self->proxy, st );
    tail = pegc_pos(st);
    if( ret && info && info->right.rule )
    {
	info->right.rule( &(info->right), st );
	if( ! info->discard ) tail = pegc_pos(st);
    }
    if( ret )
    {
	pegc_set_match( st, orig, tail, false );
    }
    else
    {
	pegc_set_pos( st, orig );
    }
    return ret;
}

PegcRule pegc_r_pad( pegc_parser * st,
		     PegcRule const * left,
		     PegcRule const * rule,
		     PegcRule const * right,
		     bool discardLeftRight )
{
    if( !st || !rule ) return PegcRule_invalid;
    if( ! left && !right ) return *rule;
    pegc_pad_info * d = (pegc_pad_info *) malloc(sizeof(pegc_pad_info));
    if( ! d ) return PegcRule_invalid;
    d->discard = discardLeftRight;
    PegcRule r = pegc_r( PegcRule_mf_pad, d );
    r.proxy = rule;
    pegc_gc_add( st, d, pegc_free_value );
    d->left = d->right = PegcRule_invalid;
    if( left )
    {
	d->left = pegc_r_star( left );
    }
    if( right )
    {
	d->right = pegc_r_star( right );
    }
    return r;
}


/**
   Internal data for pegc_r_string_quoted.
*/
struct pegc_string_quoted_data
{
    char * freeme;
    pegc_char_t quote;
    pegc_char_t esc;
    pegc_char_t ** dest;
};
typedef struct pegc_string_quoted_data pegc_string_quoted_data;
/**
   GC dtor for pegc_string_quoted_data objects.
 */
static void pegc_free_string_quoted_data(void *x)
{
    pegc_string_quoted_data * p = (pegc_string_quoted_data*)x;
    if( p )
    {
	//MARKER; printf("freeing old match string @%p / %p / %p: [%s]\n", p, p->dest, *p->dest, *p->dest);
	if( p->freeme ) pegc_free(p->freeme);
	pegc_free(p);
    }
}

/**
   Main implementation of the quoted string parser.
*/
static bool PegcRule_mf_string_quoted( PegcRule const * self, pegc_parser * st )
{
    if( ! self || !self->data || !pegc_isgood(st) ) return false;
    pegc_string_quoted_data * sd = (pegc_string_quoted_data*)pegc_gc_search( st, self->data );
    if( ! sd ) return false;
    Clob * cb = 0;
    clob_init( &cb, 0, 5 );
    pegc_const_iterator orig = pegc_pos(st);
    if( *pegc_pos(st) != sd->quote ) return false;
    pegc_bump(st);
    bool ok = true;
    while( pegc_isgood(st) )
    {
	pegc_char_t ch = *pegc_pos(st);
	if( sd->esc == ch )
	{
	    if( ! pegc_bump(st) )
	    {
		ok = false;
		break;
	    }
	    ch = *pegc_pos(st);
	    if(sd->esc == '\\') switch(ch)
	    {
	      case 't': ch = '\t'; break;
	      case 'n': ch = '\n'; break;
	      case 'r': ch = '\r'; break;
	      case 'v': ch = '\v'; break;
	      case 'b': ch = '\b'; break;
	      default:
		  break;
	    };
	}
	else if( sd->quote == ch )
	{
	    break;
	}
	clob_append_char_n( cb, ch, 1 );
	pegc_bump(st);
    }
    if( (!ok)
	|| (!pegc_isgood(st))
	|| (*pegc_pos(st) != sd->quote) )
    {
	clob_finalize(cb);
	pegc_set_pos( st, orig );
	return false;
    }
    pegc_bump(st);
    if( sd->freeme )
    {
	//MARKER; printf("freeing old match string @%p / %p / %p: [%s]\n", sd, sd->dest, *sd->dest, *sd->dest);
	pegc_free(*sd->dest);
	*sd->dest = 0;
	sd->freeme = 0;
    }
    sd->freeme = clob_take_buffer(cb);
    *sd->dest = sd->freeme;
    clob_finalize(cb);
    //MARKER; printf("setting match string @%p / %p / %p: [%s]\n", sd, sd->dest, *sd->dest, *sd->dest);
    pegc_set_match( st, orig, pegc_pos(st), false );
    return true;
}


PegcRule pegc_r_string_quoted( pegc_parser * st,
			       pegc_char_t quoteChar,
			       pegc_char_t escChar,
			       pegc_char_t ** target )
{
    if( ! st || !quoteChar ) return PegcRule_invalid;
    pegc_string_quoted_data * sd = (pegc_string_quoted_data*)malloc(sizeof(pegc_string_quoted_data));
    if( ! sd ) return PegcRule_invalid;
    sd->quote = quoteChar;
    sd->esc = escChar;
    sd->freeme = 0;
    sd->dest = target;
    *sd->dest = 0;
    pegc_gc_register( st, sd, pegc_free_string_quoted_data, sd, 0 );
    //MARKER; printf("Register dest %c-style string @%p / %p / %p\n", sd->quote,sd, sd->dest, *sd->dest);
    return pegc_r( PegcRule_mf_string_quoted, sd );
}



#undef MARKER
#undef PEGC_INIT_RULE
#undef DUMPPOS
#if defined(__cplusplus)
} /* extern "C" */
#endif
