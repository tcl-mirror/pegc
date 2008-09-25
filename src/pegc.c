#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MARKER printf("MARKER: %s:%s:%d:\n",__FUNCTION__,__FILE__,__LINE__);

#include "pegc.h"
#include "hashtable.h"


struct pegc_parser
{
    char const * name; /* for debugging + error reporting purposes */
    long id;
    pegc_cursor cursor;
    pegc_cursor match;
    struct {
	hashtable * actions;
	hashtable * rulelists;
    } hashes;
    struct
    {
	unsigned int flags;
	void * data;
    } client;
};


typedef short pegc_action_key;
typedef pegc_action_key * pegc_action_key_p;
struct pegc_hashes
{
    hashtable * actions;
    hashtable * rulelists;
};

static void * pegc_next_hash_key()
{
    return malloc(1);
}

static hashval_t pegc_hash_void_ptr( void const * k )
{
    typedef long kludge_t; // must be the same size as the platform's (void*)
    return (hashval_t) (kludge_t) k;
}
static int pegc_hash_cmp_void_ptr( void const * k1, void const * k2 )
{
    return (k1 == k2);
}



#define HASH_INIT(P,H,DK,DV) \
    if( ! P->hashes.H ) { P->hashes.H = hashtable_create(10, pegc_hash_void_ptr, pegc_hash_cmp_void_ptr ); \
	hashtable_set_dtors( P->hashes.H, DK, DV ); }

static void pegc_actions_insert( pegc_parser * st, void * key, pegc_action a )
{
    HASH_INIT(st,actions,free,0);
    hashtable_insert(st->hashes.actions, key, a);
}

static pegc_action pegc_actions_search( pegc_parser * st, void const * key )
{
    HASH_INIT(st,rulelists,free,free);
    void * r = hashtable_search( st->hashes.actions, key );
    //MARKER; printf("h=%p, key=%p/%lxd, val=%p\n", h, key, *((long *)key), r );
    return r ? (pegc_action)r : 0;
}

static void pegc_rulelists_insert( pegc_parser * st, void * key, PegcRule const ** a )
{
    HASH_INIT(st,rulelists,free,free);
    hashtable_insert( st->hashes.rulelists, key, a);
}

static PegcRule const ** pegc_rulelists_search( pegc_parser * st, void const * key )
{
    HASH_INIT(st,rulelists,free,free);
    void * r = hashtable_search( st->hashes.rulelists, key );
    //MARKER; printf("h=%p, key=%p/%lxd, val=%p\n", h, key, *((long *)key), r );
    return r ? (PegcRule const **)r : 0;
}
#undef HASH_INIT

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

const pegc_cursor pegc_cursor_init = { 0, 0, 0 };
const pegc_parser
pegc_parser_init = { 0, /* name */
		    0, /* id */
		    {0,0,0}, /* cursor */
		    {0,0,0}, /* match */
		    { /* hashes */
		    0, /* actions */
		    0 /* rulelists */
		    },
		    { /* client data */
		    0, /* flags */
		    0 /* data */
		    }
};

bool pegc_r_failure( pegc_parser * st )
{
    return false;
}

bool pegc_r_success( pegc_parser * st )
{
    return true;
}

bool pegc_eof( pegc_parser * st )
{
    return !st
	    || !st->cursor.pos
	    || !*(st->cursor.pos)
	    || (st->cursor.pos >= st->cursor.end)
	    ;
}

pegc_cursor * pegc_iter( pegc_parser * st )
{
    return &st->cursor;
}

pegc_const_iterator pegc_begin( pegc_parser * st )
{
    return pegc_iter(st)->begin;
}

pegc_const_iterator pegc_end( pegc_parser * st )
{
    return pegc_iter(st)->end;
}


bool pegc_in_bounds( pegc_parser * st, pegc_const_iterator p )
{
    return p && *p && (p>=pegc_begin(st)) && (p<pegc_end(st));
}


pegc_const_iterator pegc_pos( pegc_parser * st )
{
    return pegc_iter(st)->pos;
}

bool pegc_set_pos( pegc_parser * st, pegc_const_iterator p )
{
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
	: pegc_set_pos( st, pegc_begin(st) + n );
}

bool pegc_bump( pegc_parser * st )
{
    return pegc_advance(st, 1);
}

long pegc_distance( pegc_parser * st, pegc_const_iterator e )
{
    return e - pegc_pos(st);
}

pegc_cursor pegc_get_match_cursor( pegc_parser * st )
{
    pegc_cursor cur = pegc_cursor_init;
    cur.pos = cur.begin = st->match.begin;
    cur.end = st->match.end;
    return cur;
}

pegc_iterator pegc_get_match_string( pegc_parser * st )
{
    pegc_cursor cur = pegc_get_match_cursor(st);
    if( !cur.begin
	|| !*cur.begin
	|| (cur.begin==cur.end)
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
    if( (! pegc_in_bounds( st, begin ))
	|| (pegc_end(st) < end) )
    {
	//MARKER;fprintf(stderr,"WARNING: pegc_set_match() is out of bounds.\n");
	return false;
    }
    //MARKER;printf("pegc_setting_match() setting match of %d characters.\n",(end-begin));
    st->match.begin = begin;
    st->match.end = end;
    if( movePos )
    {
	pegc_set_pos( st, end );
    }
    return true;
}

bool pegc_matches_char( pegc_parser * st, int ch )
{
    return pegc_eof(st) ? false : (*pegc_pos(st) == ch);
}

bool pegc_matches_chari( pegc_parser * st, int ch )
{
    if( pegc_eof(st) ) return false;
    pegc_const_iterator p = pegc_pos(st);
    if( !p && !*p ) return false;
    return (p && *p)
	&& (tolower(*p) == tolower(ch));
}

bool pegc_matches_string( pegc_parser * st, pegc_const_iterator str, long strLen, bool caseSensitive )
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
    pegc_set_match( st, orig, p, true );
    return true;
}

void pegc_clear_match( pegc_parser * st )
{
    pegc_set_match(st, 0, 0, false);
}

bool pegc_try_rule( pegc_parser * st, pegc_rule_f r )
{
    pegc_const_iterator orig = pegc_pos(st);
    if( r(st) )
    {
	pegc_set_match( st, orig, pegc_pos(st), true );
	return true;
    }
    pegc_set_pos(st,orig);
    return false;
}

bool pegc_try_rulesn( pegc_parser * st, pegc_rule_f *r, unsigned int n, bool and_op )
{
    pegc_rule_f R = 0;
    unsigned int i = 0;
    pegc_const_iterator orig = pegc_pos(st);
    bool ret = false;
    for(; i < n; ++i, r++ )
    {
	if( ! (R = *r) ) break;
	ret = pegc_try_rule( st, R );
	if( ret && !and_op ) break;
	else if( and_op && !ret ) break;
    }
    if( ! ret )
    {
	pegc_set_pos( st, orig );
    }
    else
    {
	pegc_set_match( st, orig, pegc_pos(st), false );
    }
    return ret;
}

bool pegc_try_rules( pegc_parser * st, pegc_rule_f *r, bool and_op )
{
    pegc_rule_f * R = r;
    unsigned int i = 0;
    for( ; R && *R; ++i, ++R ){}
    return pegc_try_rulesn( st, r, i, and_op );
}

/**
   Always returns false and does nothing.
*/
bool PegcRule_mf_failure( PegcRule const * self, pegc_parser * st );

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


const PegcRule PegcRule_init = {PegcRule_mf_failure, /* rule */
			      0, /* data */
			      0, /* proxy */
			      { /* client */
			      0, /* flags */
			      0 /* data */
			      },
			      {/* _internal */
			      0 /* key */
			      } 
};

PegcRule pegc_r( PegcRule_mf rule, void const * data )
{
    PegcRule r = PegcRule_init;
    r.rule = rule; // ? rule : PegcRule_mf_failure;
    r.data = data;
    return r;
}

bool PegcRule_mf_failure( PegcRule const * self, pegc_parser * st )
{
    return false;
}

bool PegcRule_mf_success( PegcRule const * self, pegc_parser * st )
{
    return true;
}

bool PegcRule_mf_int_decimal( PegcRule const * self, pegc_parser * st )
{
    MARKER;printf("This rule is untested!\n");
    int * v = (int *)self->data;
    if( ! v ) return false;
    int myv = 0;
    int len = 0;
    int rc = sscanf(pegc_pos(st), "%d%n",&myv,&len);
    if( (EOF == rc) || (0 == len) ) return false;
    if( ! pegc_advance(st,len) ) return false;
    *v = myv;
    return true;
}

static bool PegcRule_mf_oneof_impl( PegcRule const * self, pegc_parser * st, bool caseSensitive )
{
    // caseSensitive
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
		  0 );
}


typedef struct PegcRule_mf_string_params PegcRule_mf_string_params;
static bool PegcRule_mf_string_impl( PegcRule const * self, pegc_parser * st, bool caseSensitive )
{
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
    if( ! self->data ) return false;
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
    if( ! self->proxy ) return false;
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
    if( ! self->proxy ) return false;
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
    if( ! self->proxy ) return false;
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

static bool PegcRule_mf_or( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = pegc_rulelists_search( st, self->data );
    for( ; li && *li; ++li )
    {
	if( (*li)->rule( *li, st ) ) return true;
    }
    pegc_set_pos(st,orig);
    return false;
}

static bool PegcRule_mf_and( PegcRule const * self, pegc_parser * st )
{
    pegc_const_iterator orig = pegc_pos(st);
    PegcRule const ** li = pegc_rulelists_search( st, self->data );
    if(!li) return false;
    for( ; li && *li; ++li )
    {
	if( ! (*li)->rule( *li, st ) )
	{
	    pegc_set_pos(st,orig);
	    return false;
	}
    }
    return true;
}

PegcRule pegc_r_list( pegc_parser * st,  bool orOp, PegcRule const ** li )
{
    PegcRule r = pegc_r( orOp ? PegcRule_mf_or : PegcRule_mf_and, 0 );
    int count = 0;
    if(li)
    {
	PegcRule const ** counter = li;
	for( ; counter && *counter && (*counter)->rule; ++counter )
	{
	    ++count;
	}
    }
    if( ! count ) return r;
    void * key = pegc_next_hash_key();
    r.data = key;
    PegcRule  const** list = (PegcRule  const**)(calloc( count+1, sizeof(PegcRule*) ));
    if( ! list )
    {
	fprintf(stderr,
		"%s:%d:pegc_r_list() serious error: calloc() of %d (PegcRule*) failed!\n",
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

PegcRule pegc_r_or( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs )
{
    if( ! lhs || ! rhs )
    {
	return pegc_r( PegcRule_mf_failure, 0 );
    }
    PegcRule const *tmp[3] = {lhs,rhs,0};
    return pegc_r_list( st, true, tmp );
}
PegcRule pegc_r_and( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs )
{
    if( ! lhs || ! rhs )
    {
	return pegc_r( PegcRule_mf_failure, 0 );
    }
    PegcRule const *tmp[3] = {lhs,rhs,0};
    return pegc_r_list( st, false, tmp );
}

static bool PegcRule_mf_action( PegcRule const * self, pegc_parser * st )
{
    if( ! self->proxy ) return false;
    pegc_const_iterator orig = pegc_pos(st);
    bool rc = self->proxy->rule( self->proxy, st );
    //MARKER; printf("rule matched =? %d\n", rc);
    if( rc )
    {
	pegc_set_match( st, orig, pegc_pos(st), true );
	pegc_action act = pegc_actions_search( st, self->_internal.key );
	//MARKER; printf("action = %p\n", act);
	if( act )
	{
	    (*act)(st);
	}
	return true;
    }
    pegc_set_pos( st, orig );
    return false;
}

PegcRule pegc_r_action( pegc_parser * st, PegcRule const * rule, pegc_action onMatch )
{
    //MARKER;
    PegcRule r = PegcRule_init;
    r.proxy = rule;
    /**
       Note to self: We use a newly-malloc'ed value as a lookup key so we
       can avoid the question of "is this ID already in use". This solves
       several problems involved with using a numeric key and allows us to
       easily guaranty a unique key without having to check for dupes,
       overflows, and such.
    */
    r.rule = PegcRule_mf_action;
    if( onMatch )
    {
	/*
	  FIXME: pre-allocate a memory block for action keys, and
	  expand as necessary.  We can actually just pre-allocate a
	  string and return successive addresses in that string. We
	  "could" use the clob class to get auto-expansion on that
	  array, but we may(?) run a risk of invalidating addresses on
	  a realloc.

	  If we wanna be risky, we could just alloc 1k chars or so,
	  and hope that app uses more than that. It's highly unlikely
	  that even a very complex grammar needs more 100-200
	  actions(?). My grammars to date have needed fewer than 20.

	  Remember that we need to change the hashtables to not free
	  their keys if we do this.
	*/
	r._internal.key = pegc_next_hash_key();
	//MARKER; printf("action key = %p\n", l);
	pegc_actions_insert( st, r._internal.key, onMatch );
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
		  input ? pegc_latin1(input) : NULL);
}


/* Generate the PegcRule_isXXX routines.

  F is the name of one of C's standard isXXX(int) funcs,
  without the 'is' prefix
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
const PegcRule PegcRule_ ## F = {PegcRule_mf_ ## F,0}

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
#define HDESTROY(H) \
    if( st->hashes.H ) {hashtable_destroy( st->hashes.H ); st->hashes.H = 0; }
    HDESTROY(actions);
    HDESTROY(rulelists);
#undef HDESTROY
    free(st);
    return true;
}

#undef MARKER
