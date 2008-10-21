#ifndef WANDERINGHORSE_NET_PEGC_H_INCLUDED
#define WANDERINGHORSE_NET_PEGC_H_INCLUDED
/*! @mainpage pegc PEG generation library

pegc is a toolkit for writing PEG-style parsers in C using something
similar to functional composition, conceptually similar to C++ parsing
toolkits like Boost.Spirit, PEGTL, and parsepp
(http://wanderinghorse.net/cgi-bin/parsepp.cgi).

Author: Stephan Beal (http://wanderinghorse.net/home/stephan)

License: the core library is Public Domain, but some of the borrowed
utility code is released under a BSD license (see hashtable*.{c,h} for details).

Home page: http://fossil.wanderinghorse.net/repos/pegc/

@section sec_about About

pegc attempts to implement a model of parser which has become quite
popular in C++, but attempts to do so within the limitations of C (e.g. lack of type
safety in many places, and no safe casts). As far as i am aware,
pegc is the first C code of its type. The peg/leg project
(http://piumarta.com/software/peg/) is similar but solves the problem
from the exact opposite direction - it uses a custom PEG grammar as
input to a code generator, whereas pegc is not a generator (but
could be used to implement one).

The basic idea is that one defines a grammar as a list of Rule
objects. A grammar starts with a top rule, and that rule may then
delegate all parsing, as it sees fit, to other rules. The result of a
parse is either true (the top-most rule matches) or false (the
top-most rule fails). It is roughly modelled off of recursive descent
parsers, and follows some of those conventions. For example, a parsing
rule which does not match (i.e. return true) must not consume
input. Most rules which do match, on the other hand, do consume (there
are several exceptions to that rule, though).

In C++ we would build the parser using templates (at least that's
how i'd do it). In C we don't have that option, so we build up little
objects which contain a Rule function and some data for that function.
Those rules can then be processed in a PEG fashion.

My theory is that once the basic set of rules are in place, it will be
relatively easy to implement a self-hosted code generator which can
read a lex/yacc/lemon-like grammar and generate pegc-based parsers. That
is, a PEGC-parsed grammar which in turn generates PEGC parser code.

@section sec_apinotes API Notes and Conventions:

Parsers are created using pegc_create_parser() and destroyed using
pegc_destroy_parser(). A parser object is an opaque type used by
the library to keep track of the state of a parse, including the
input range and any resources dynamically allocated by the rules
generation process.

Rules are modelled using PegcRule objects, which are composed in
various ways to parse client-defined grammars of arbitrary
complexity. A rule conceptually hold a function pointer (the rule
implementation), possibly rule-specific static data (e.g. a list of
characters to match against), an optional client-provided data
pointer, and (in some cases) "hidden" dynamically allocated data (some
rules cannot be implemented using only static data). Rule objects are
created either by using the pegc_r_XXX() family of functions or
providing customized PegcRule objects. Rules which can be implemented
using only static data (and no parser-specific data) can often be
implemented as shared PegcRule objects (e.g. PegcRule_eof and
PegcRule_isspace).

Many pegc_r_XXX() functions can be called without having a parser
object, but some require a parser object so that they have a place to
"attach" dynamically allocated resources and avoid memory leaks. An
important consideration when building parsers which use such rules is
that one only needs to create each rule one time for any given
parser. Rules have, by convention, no non-const state, so it is safe
to use them in multiple parts of a given grammar.  For example, if
you need a certain list of rules in several places in your grammar, it
is wise to create that list only once and reference that copy
throughout the grammar, instead of calling pegc_r_list_a() (or
similar) each time an identical rule is needed.  Failing to follow
this guideline will result in significantly larger memory costs for
the parser. Note that most allocation happens during the construction
of the grammar, not during the actual parsing (where little or no
allocation happens unless the user copies tokens from the parse).

Some examples of pegc rules:

@code
//matches a single 'a', case-sensitively:
PegcRule a = pegc_r_char('a',true);

// matches ([aA]):
PegcRule aA = pegc_r_char('a',false);
// same meaning, but different approach:
PegcRule aA = pegc_r_oneof("aA",false);

// matches the literal string "foo", case-sensitively:
PegcRule foo = pegc_r_string("foo", true);

// matches ((foo)?):
PegcRule optFoo = pegc_r_opt(&foo);
@endcode

The API provides routines for creating rule lists, but care must be
taken to always terminate such lists with a NULL entry so that this
API can avoid overrunning the bounds of a rule list.

@section sec_threadsafety Thread safety:

It is never legal to use the same instance of a parser in multiple
threads at one time, as the parsing process continually updates the
parser state. No routines in this library rely on any shared data
in a manner which prohibits multiple threads from running different
parsers at the same time. In theory, Rules (which are normally
effectively const) are more or less thread-safe after they are initialized
(though there is room for race conditions during their initialization and
cleanup). That said, many rules have an association with a specific parser
instance, and those rules must be treated as non-thread-safe.

@section sec_aboutbooleans About boolean types:

By default this code defines its own macros for true/false and the bool
keyword. If PEGC_HAVE_STDBOOL is defined to a true value then <stdbool.h>
is used instead. When compiling under C++ (i.e. __cplusplus is defined),
stdbool.h is not necessary and we use the C++-defined bool/true/false
(and PEGC_HAVE_STDBOOL is ignored entirely).

@section sec_credits Credits

Bryan Ford (http://www.brynosaurus.com) is, AFAIK, the originator of the
PEG concept.

PEGTL (http://code.google.com/p/pegtl/), by Dr. Colin Hirsch, was my first
exposure to PEGs, and immediately piqued my interest in the topic. After
implementing two libraries similar to PEGTL, i felt compelled to try it
yet again, this time in plain old C.

Christopher Clark implemented the hashtable code used extensively by
pegc.

Some of the utility code (e.g. vappendf.{c,h}) is based on public domain
code written mostly by other people.
************************************************************************/

#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#  ifndef PEGC_HAVE_STDBOOL
#    define PEGC_HAVE_STDBOOL 0
#  endif

#  if defined(PEGC_HAVE_STDBOOL) && !(PEGC_HAVE_STDBOOL)
/* i still can't fucking believe that C has no bool. */
#    if !defined(bool)
#      define bool char
#    endif
#    if !defined(true)
#      define true 1
#    endif
#    if !defined(false)
#      define false 0
#    endif
#  else /* aha! stdbool.h! C99. */
#    include <stdbool.h>
#  endif /* PEGC_HAVE_STDBOOL */
#endif /* __cplusplus */

    /**
       We use typedefs for input types so that we can hopefully
       refactor the library to handle non-(char const *) input without
       too much work.
    */
    typedef char pegc_char_t;
    typedef pegc_char_t const * pegc_const_iterator;
    typedef pegc_char_t * pegc_iterator;

    /**
       A type for holding a set of pointers in a string:

       begin: the start of the string

       pos: current cursor pos in the string.

       end: one address after the end of the string (i.e., where the
       null terminator should be, except that the end may be within a
       substring of a larger string).
    */
    struct pegc_cursor
    {
	pegc_const_iterator begin;
	pegc_const_iterator pos;
	pegc_const_iterator end;
    };
    typedef struct pegc_cursor pegc_cursor;
    /**
       Copy this object to get a pegc_cursor with
       its pointers properly initialized to 0.
    */
    extern const pegc_cursor pegc_cursor_init;

    /**
       Initializes it to point at the range [begin,end) and sets
       it->pos set to begin. Returns false and does nothing
       if (end<begin).

       When re-mapping a parser to a different input source than
       previously used, be sure to call pegc_set_error() to clear the
       error state, or most parse operations will fail.
    */
    bool pegc_init_cursor( pegc_cursor * it, pegc_const_iterator begin, pegc_const_iterator end );

    /**
       pegc_parser is the parser class used by the pegc API. It is an
       opaque type used by almost all functions in the API. This type
       holds information about the current state of a given parse,
       such as the input range, a pointer to the current position of
       the input, and memory dynamically allocated by various rules.
    */
    struct pegc_parser;
    typedef struct pegc_parser pegc_parser;

    /**
       Creates a new parser, assigns it to st, and sets it up to point
       at the given input. The data pointed to by must outlive st and
       must not change during st's useful lifetime. If len is less
       than 0 then strlen(inp) is used to determine the input's
       length.

       If this routine returns true, the returned object must be
       cleaned up by calling pegc_destroy_parser(st). pegc_destroy_parser()
       may be called if this routine fails, but only if (p==0) (otherwise
       pegc_destroy_parser() will try to free that pointer, which probably
       isn't valid).

       If (st==0) then this routine returns false and does nothing. If
       st is not null then st is always assigned a value, but the value
       will be 0 if this routine returns false.

       Example:

       \code
       pegc_parser * p = 0;
       if( ! pegc_create_parser( &p, "...", -1 ) ) { ... error... }
       ...
       pegc_destroy_parser(p);
       \endcode


       Notes of potential interest:

       Creating a parser is not that expensive. A parser itself has a
       relatively small amount of state. However, many rules require
       dynamically allocated memory for their state data. Such memory
       is owned by the associated parser, so the amount of memory used
       by a parser is directly related to the number of rules
       associated with it.  Not all rules require dynamic memory. As a
       general rule, the pegc_r_xxx() functions which do not take a
       pegc_parser as the first argument do not require dynamic
       memory, whereas those which do not have a pegc_parser argument
       do. In fact, the reason they have the pegc_parser argument is
       so that they know where to "attach" the allocated memory to
       (for cleanup purposes).
       
    */
    bool pegc_create_parser( pegc_parser ** st, char const * inp, long len );

    /**
       Clears the parser's internal state, freeing any resources
       created internally by the parsing process. It then calls
       free() to deallocate the parser.

       This routine returns false only if st is 0.

       Note that the library internally allocates some storage
       associated with the parser for certain operations (e.g.  see
       pegc_r_action() and pegc_r_list()). That memory is not freed
       until this function is called. Thus if parsers are not properly
       finalized, leak detection tools may report that this code
       leaks resources.
    */
    bool pegc_destroy_parser( pegc_parser * st );

    /**
       Returns true if st is 0, points to a null character, or is
       currently pointed out of its bounds (see pegc_in_bounds(),
       pegc_begin(), and pegc_end()).

       Rules should check this value before doing any comparisons.
    */
    bool pegc_eof( pegc_parser const * st );

    /**
       Returns true if st has an error message set.
       See pegc_set_error().
    */
    bool pegc_has_error( pegc_parser const * st );

    /**
       Returns true if st is not null and !pegc_eof(st) and
       !pegc_has_errors(st). Remember that parsing may legally move
       the parser to EOF, and that arbitrary rules may treat EOF as a
       matchable value, so this routine has to be used carefully to
       avoid conflicts with EOF. If a rule might legally run into EOF
       then use pegc_eof() and pegc_has_error() instead of this
       routine.
    */
    bool pegc_isgood( pegc_parser const * st );

    /**
       Calculates the line and column position of st by counting
       newline characters, writing them to the given line and col
       pointers (which must not be 0). The line number starts at one
       and column starts at zero (because this is how emacs does it).

       Returns false if any of the arguments are null, otherwise
       returns true.

       Note that this is a linear-time operation, as rules to not
       update this information themselves (it would complicate the
       implementation of nearly every single rule).

       FIXME: does not correctly handle platforms which use a single
       carriage return as the newline character. We can use
       PegcRule_eol to implement that behaviour.
    */
    bool pegc_line_col( pegc_parser const * st, unsigned int * line, unsigned int * col );

    /**
       Gets the current error string (which may be 0), line, and
       column. The string is owned by the parser and will be invalided
       the next time pegc_set_error() is called.

       Any of the integer pointers may be 0.

       It returns 0 if:

       - (!st)

       - No error has been set using pegc_set_error().

       The returned string is owned by the parser and will be invalidated
       by the next parsing operation which sets the error state.
    */
    char const * pegc_get_error( pegc_parser const * st,
				 unsigned int * line,
				 unsigned int * col );

    /**
       Copies the given null-terminated string as the current error
       message for the parser. Also sets the line/column position.
       The error can be fetched with pegc_get_error().

       The clientNumber parameter is a client-determined number which
       is not used by this library but is returned by pegc_get_error().

       If msg if NULL then the error state is cleared.

       Returns false if:

       - st is null

       - cannot allocate memory for the error string.

       Note that because it must allocate memory for the error string,
       it is not a wise idea to set this in response to alloc errors.
    */
    bool pegc_set_error( pegc_parser * st, char const * fmt, ... );

    /**
       Returns st's cursor. Note that any parsing operations may change its
       state.
    */
    pegc_cursor const * pegc_iter( pegc_parser const * st );

    /**
       Returns st's starting position.
    */
    pegc_const_iterator pegc_begin( pegc_parser const * st );

    /**
       Returns st's ending position. This uses the one-after-the-end
       idiom, so the pointed-to value is considered invalid and should
       never be dereferenced.
    */
    pegc_const_iterator pegc_end( pegc_parser const * st );
    /**
       Returns true only if (p>=pegc_begin(st)) and (p<pegc_end(st)).
    */
    bool pegc_in_bounds( pegc_parser const * st, pegc_const_iterator p );
    /**
       Returns the current position in the parser.
    */
    pegc_const_iterator pegc_pos( pegc_parser const * st );

    /**
       Sets the current position of the parser. If p
       is not in st's bounds then false is returned
       and the position is not changed.

       Note that pegc_set_pos() considers pegc_end() to be
       in bounds, whereas pegc_in_bounds() does not.
    */
    bool pegc_set_pos( pegc_parser * st, pegc_const_iterator p );

    /**
       Advances the cursor n places. If that would take it out of
       bounds, or if n is 0, then false is returned and there are no
       side-effects, otherwise true is returned.
    */
    bool pegc_advance( pegc_parser * st, long n );

    /**
       Equivalent to pegc_advance(st,1).
    */
    bool pegc_bump( pegc_parser * st );

    /**
       Return (e-pegc_pos(st)). It does no bounds checking.  If either
       st or e are 0, then 0 is returned (you reap what you sow!).
    */
    long pegc_distance( pegc_parser const * st, pegc_const_iterator e );

    /**
       Typedef for callback routines called when pegc_set_match() is
       called. See pegc_add_match_listener().
    */
    typedef void (*pegc_match_listener)( pegc_parser const * st, void * clientData );

    /**
       Registers a callback function which will be called every time
       pegc_set_match(st,...) is called. While that may sound very useful,
       it's not quite as useful as it initially sounds because it's called
       *every* time pegc_set_match() is called, and that can happen an arbitrary
       number of times during the matching process, and can reveal tokens which
       are currently parsing but will end up part of a non-match. It's best reserved
       for debug purposes.

       The clientData parameter is an arbitrary client pointer. This
       API does nothing with it except pass it along to the callback.
    */
    void pegc_add_match_listener( pegc_parser * st, pegc_match_listener f, void * clientData );

    /**
       Sets the current match string to the range [begin,end). If
       movePos is true then pegc_set_pos(st,end) is called. If begin or
       end are null, or (end<begin), or pegc_in_bounds() fails for
       begin or end, then false is returned and this function has no
       side effects.
    */
    bool pegc_set_match( pegc_parser * st, pegc_const_iterator begin, pegc_const_iterator end, bool movePos );

    /**
       Returns a cursor pointing to the current match in st.  The
       returned object will have a 'begin' value of 0 if there is no
       current match. The 'pos' value of the returned object is not
       relevant in this context, but is set to the value of 'begin'
       for the sake of consistency.

       Note that all rules which consume are supposed to update the
       matched string, so this value may be updated arbitrarily often
       during a parse run. Its value only applies to the last rule
       which set the match point (via pegc_set_match()).
    */
    pegc_cursor pegc_get_match_cursor( pegc_parser const * st );

    /**
       Returns a copy of the current match string, or 0 if there
       is no match or there is a length-zero match. The caller
       is responsible for deallocating it using free().
    */
    pegc_iterator pegc_get_match_string( pegc_parser const * st );

    /**
       Returns true if ch matches the character at pegc_pos(st). It only
       compares, it does not consume input.
    */
    bool pegc_matches_char( pegc_parser const * st, int ch );

    /**
       Case-insensitive form of pegc_matches_char.
    */
    bool pegc_matches_chari( pegc_parser const * st, int ch );

    /**
       If the next strLen characters of st match str then true is
       returned.  If strLen is less than 0 then strlen(str) is used to
       determine the length. If caseSensitive is false then the
       strings must match exactly, otherwise the comparison is done
       using tolower() on each char of each string. It only compares,
       it does not consume input.
    */
    bool pegc_matches_string( pegc_parser const * st, pegc_const_iterator str, long strLen, bool caseSensitive );

    /**
       Clears the parser's match string, such that
       pegc_get_match_string() will return null. In practice this is
       never necessary, but here it is in case you need it.
    */
    void pegc_clear_match( pegc_parser * st );

    /**
       Returns a pointer to a statically allocated length-one string
       of all latin1 characters in the range [0,255]. The returned
       string contains the value of the given character, such that
       pegc_latin1('c') will return a pointer to a null-terminated
       string with the value "c". The caller must never modify nor
       deallocate the string, as it is statically allocated the first
       time this routine is called.

       If ch is not in the range [0,255] then 0 is returned.

       This function is intended to ease the implementation of
       PegcRule chains for rules matching single characters.
    */
    pegc_const_iterator pegc_latin1(int ch);

    /**
       Associates client-side data with the given parser. This library
       places no significance on the data parameter - it is owned by
       the caller and can be fetched with pegc_get_client_data().

       Client-specific data can be used to hold, e.g., parser state
       information specific to the client's parser.

       If this routine is called multiple times for the same
       parser, the data is replaced on subsequent calls.

       If you want the parser to take ownership of the data, use
       pegc_gc_register() and pegc_gc_search() instead.
    */
    void pegc_set_client_data( pegc_parser * st, void * data );

    /**
       Returns the data associated with st via pegc_set_client_data(),
       or 0 if no data has been associated or st is null.
    */
    void * pegc_get_client_data( pegc_parser const * st );


    struct PegcRule;
    /**
       A typedef for "member functions" of PegcRule objects.

       Conventions:

       If the rule can match then true is returned and st is advanced
       to one place after the last consumed token.  It is legal to not
       consume even on a match, but this is best reserved for certain
       cases, and it should be well documented in the API docs.

       If the rule cannot match it must not consume input. That is, if
       it doesn't match then it must ensure that pegc_pos(st) returns
       the same value after this call as it does before this call. It
       should use pegc_set_pos() to force the position back to the
       pre-call starting point if needed.

       The self pointer is the "this" object - the object context in
       which this function is called. Implementations may (and
       probably do) require a certain type of data to be set in
       self->data (e.g. a string to match against). The exact type of
       the data must be documented in the API docs for the
       implementation.
    */
    typedef bool (*PegcRule_mf) ( struct PegcRule const * self, pegc_parser * state );

    /**
       PegcRule objects hold data used for implement parsing rules.

       Each object holds an PegcRule_mf "member function" and a void
       data pointer. The data pointer holds information used by the
       member function. Some rules hold a (char const *) here and
       match against a string or the characters in the string.
       Non-string rules may have other uses for the data pointer.

       Some rules also need a proxy rule, on whos behalf they run
       (normally providing some other processing if the proxy rule
       matches, such as running an action).
    */
    struct PegcRule
    {
	/**
	   This object's rule function. An object with a rule of 0
	   is said to be "invalid" (several API routines use this
	   term).
	*/
	PegcRule_mf rule;

	/**
	   Data used by the rule function. The exact format of the
	   data is dependent on the rule member. Type mismatches will
	   cause undefined behaviour!
	*/
	void const * data;

	/**
	   Some rules need a proxy rule. Since it is often
	   inconvenient to use the data slot for this, here it is...
	*/
	struct PegcRule const * proxy;

	/**
	   The client object is reserved for client-side use.
	   This library makes no use of it.
	*/
	struct client {
	    /**
	       For client-side use. This library makes no use of it.
	    */
	    unsigned int flags;
	    /**
	       For client-side use. This library makes no use of it.
	    */
	    void * data;
	} client;
    };
    typedef struct PegcRule PegcRule;

    /**
       Returns true if (r && r->rule). Note that it does not
       know about rule-specific validity checks.
    */
    bool pegc_is_rule_valid( PegcRule const * r );

    /**
       If either st or r are null then this function returns false,
       otherwise it returns r->rule(r,st). It is simply a front-end
       and does no management of st's state (e.g. does not set the
       match string - that is up to the rule to do).
    */
    bool pegc_parse( pegc_parser * st, PegcRule const * r );

    /**
       This object can (should) be used as an initializer to ensure a
       clean slate for the internal members of PegcRule objects. Simply
       copy this over the object. Its default rule is a rule which
       never matches (always returns false) and does not consume.
    */
    extern const PegcRule PegcRule_init;

    /**
       Registers an arbitrary key and value with the garbage
       collector, such that pegc_destroy_parser(st) will clean up the
       resources using the given destructor functions. This is often
       useful for rule factories which need to dynamically allocate
       resources.

       The key parameter is used as a literal hash key (that is, the
       pointer's value is its hash value).

       If keyDtor is not 0 then during cleanup keyDtor(key) is
       called. Likewise, if valDtor is not 0 then valDtor(value) is
       called at cleanup time.

       It is perfectly legal for the key and value to be the same
       object, but only if at least one of the destructor functions is
       0 or a no-op function (otherwise a double-free will happen).

       It is legal for both keyDtor and valDtor to be 0, in which case
       this routine can be used similarly to pegc_set_client_data()
       (and should be used in place of that routine if the parser
       should take ownership of the client data).

       Returns true if the item is now registered, or false on error
       (!st, !key, or a memory allocation error).

       Note that the destruction order of items cleaned up using this
       mechanism is undefined.

       It is illegal to register the same key more than once with the
       same parser. Doing so will cause false to be returned.
    */
    bool pegc_gc_register( pegc_parser * st,
			   void * key, void (*keyDtor)(void*),
			   void * value, void (*valDtor)(void*) );
    /**
       A convenience form of pegc_gc_register(), equivalent to
       pegc_gc_register(st,item,dtor,item,0).
    */
    bool pegc_gc_add( pegc_parser * st, void * item, void (*dtor)(void*) );

    /**
       Searches the garbage collection pool for data associated with
       the given key, returning it (or 0 if not found or either st or
       key are 0). Ownership of the returned object does not change -
       it is defined in the corresponding call to pegc_gc_register().

       A hashtable is used for the lookups, so they "should" be pretty
       fast. It is sometimes convenient to stick a gc'd value into a
       PegcRule's 'data' member, rather than to waste time on a
       lookup.
    */
    void * pegc_gc_search( pegc_parser const * st, void const * key );

    /**
       Creates a PegcRule from the given arguments. All fields
       not covered by these arguments are set to 0.
    */
    PegcRule pegc_r( PegcRule_mf func, void const * data );

    /**
       Identical to pegc_r() but allocates a new object on the heap.
       If st is not NULL then the new object is owned by st and will
       be destroyed when pegc_destroy_parser(st) is called, otherwise
       the caller owns it.

       Returns 0 if it cannot allocate a new object.
    */
    PegcRule * pegc_alloc_r( pegc_parser * st, PegcRule_mf const func, void const * data );

    /**
       Like pegc_alloc_r() (with the same ownership conventions),
       but copies all data from r.

       Note that this is a shallow copy. Data pointed to by r or
       sub(sub(sub))-rules of r are not copied.

       This routine is often useful when constructing compound rules.
       For many examples of when/why to use it, see pegc.c and
       search for this function name.
    */
    PegcRule * pegc_copy_r( pegc_parser * st, PegcRule const r );

    /**
       Returns a rule which matches if any character in the given string
       matches the next input char.
    */
    PegcRule pegc_r_oneof( char const * list, bool caseSensitive );

    /**
       Creates a 'star' rule for the given proxy rule.

       This rule acts like a the regular expression (Rule)*. Always
       matches but may or may not consume input. It is "greedy",
       matching as long as it can UNLESS the proxy rule does not
       consume input, in which case this routine stops at the first
       match to avoid an endless loop.
    */
    PegcRule pegc_r_star( PegcRule const * proxy );

    /**
       Creates a 'plus' rule for the given proxy rule.

       Works like pegc_r_star(), but matches 1 or more times.  This
       routine is "greedy", matching as long as it can UNLESS the
       proxy rule does not consume input, in which case this routine
       stops at the first match to avoid an endless loop.
    */
    PegcRule pegc_r_plus( PegcRule const * proxy );

    /**
       Always returns true but only consumes if proxy does.

       Equivalent expression: (RULE)?
    */
    PegcRule pegc_r_opt( PegcRule const * proxy );

    /**
       Creates a rule which will match the given string. The string
       must outlive the rule, as it is not copied.
    */
    PegcRule pegc_r_string( pegc_const_iterator input, bool caseSensitive );

    /**
       Creates a rule which matches the given character, which must
       be in the range [0,255].
    */
    PegcRule pegc_r_char( pegc_char_t ch, bool caseSensitive );

    /**
       Returns a rule which matches any character in the inclusive
       range [start,end].
    */
    PegcRule pegc_r_char_range( pegc_char_t start, pegc_char_t end );
    /**
       Matches a single char in a set defined by the spec parameter.
       spec must be in the format "[a-z]", where "abc" is a range
       specified such as "a-z", "A-Z", or "a-zA-Z". It uses sscanf()
       to do the parsing, so it supports any definition supported by
       the '%[' specifier.
       
       If st or spec are null, or the first character of spec
       is not a '[' then an invalid rule is returned.
    */
    PegcRule pegc_r_char_spec( pegc_parser * st, char const * spec );


    /**
       Creates a rule which matches if proxy matches, but does not
       consume. proxy must not be 0 and must outlive the returned
       object.
    */
    //PegcRule pegc_r_at( PegcRule const * proxy );
    PegcRule pegc_r_at( PegcRule const * proxy );

    /**
       The converse of pegc_r_at(), this returns true only if the
       input does not match the given proxy rule. This rule never
       consumes.
    */
    PegcRule pegc_r_notat( PegcRule const * proxy );

    /**
       Creates a rule which performs either an OR operation (if orOp
       is true) or an AND operation (if orOp is false) on the given
       list of rules. The list MUST be terminated with either NULL, or
       an entry where entry->rule is 0, or results are undefined
       (almost certainly an overflow).

       All rules in li must outlive the returned object.

       This allocates resources for the returned rule which belong to
       this API and are freed when st is destroyed.

       If st or li are null then an invalid rule is returned.

       The null-termination approach was chosen over the client
       explicitly providing the length of the list because when
       editing rule lists (which happens a lot during development) it
       is more problematic to verify and change that number than it is
       to add a trailing 0 to the list (which only has to be done
       once).

       Pneumonic: the 'a' suffix refers to the 'a'rray parameter.

       Of the various pegc_r_list_X() implementations, this one is
       most efficient (the others synthesize an array, which causes
       extra allocations, and call this routine).  ).
    */
    PegcRule pegc_r_list_a( pegc_parser * st, bool orOp, PegcRule const ** li );

    /**
       Works like pegc_r_list_a() but requires a NULL-terminated list of
       (PegcRule const *).

       Pneumonic: the 'e' suffix refers to the 'e'lipse parameters.
    */
    PegcRule pegc_r_list_e( pegc_parser * st, bool orOp, ... );

    /**
       Works like pegc_r_list_a() but requires a NULL-terminated list of
       (PegcRule const *). If the list cannot be constructed for some
       reason then an invalid rule is returned.

       Pneumonic: the 'v' suffix refers to the 'v'a_list parameters.
    */
    PegcRule pegc_r_list_v( pegc_parser * st, bool orOp, va_list ap );

    /**
       Convenience form of pegc_r_list_e( st, true, lhs, rhs, 0 ).
    */
    PegcRule pegc_r_or( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs );

    /**
       Like pegc_r_or(), but requires a null-terminated list of (PegcRule const *).
    */
    PegcRule pegc_r_or_e( pegc_parser * st, ... );

    /**
       Convenience form of pegc_r_list_e( st, false, lhs, rhs, 0 ).
    */
    PegcRule pegc_r_and( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs );

    /**
       Like pegc_r_and(), but requires a null-terminated list of (PegcRule const *).
    */
    PegcRule pegc_r_and_e( pegc_parser * st, ... );

    /**
       Typedef for Action functions. Actions are created using
       pegc_r_action() and are triggered when their proxy rule
       matches.

       Actions can act on client-side data in two ways:

       - By passing a data object as the 4th paramter to
       pegc_r_action(). This approach is useful if different
       subparsers need different types of state.

       - By calling pegc_set_client_data() and accessing it from the
       action. If all actions access the same shared state, this is
       the simplest approach.

       Under consideration: make st non-const so that actions can set
       the error state. This leaves much room for inappropriate
       updates of the state from actions, however. In my experience,
       actions should not, as a rule, change the parser state.
    */
    typedef void (*pegc_action)( pegc_parser const * st, void * clientData );

    /*
      Creates a new Action. If rule matches then
      onMatch(st,clientData) is called. onMatch can fetch the matched
      string using pegc_get_match_string() or pegc_get_match_cursor().

      This allocates resources for the returned rule which belong to
      this API and are freed when st is destroyed.

      The clientData argument may be 0 and is not used by this API,
      but is passed on to onMatch as-is. This can be used to
      accumulate parsed tokens in a client-side structure, convert
      tokens to (e.g.) integers, or whatever the client needs to do.
     */
    PegcRule pegc_r_action( pegc_parser * st,
			    PegcRule const * rule,
			    pegc_action onMatch,
			    void * clientData );

    /**
       Creates a rule which matches between min and max
       times.

       This routine may perform optimizations for specific
       combinations of min and max:

       (min==1, max==1): returns *rule

       (min==0, max ==1): returns pegc_r_opt(rule)

       For those specific cases, the st parameter may be 0, as they do
       not allocate any extra resources. For all other cases, st must
       be valid so that we can allocate the resources needed for the
       rule mapping.

       On error ((max<min), st or rule are null, or eof), an invalid
       rule is returned.
    */
    PegcRule pegc_r_repeat( pegc_parser * st,
			    PegcRule const * rule,
			    unsigned int min,
			    unsigned int max );

    /**
       Creates a rule which matches if the equivalent of:

       ((leftRule*) && mainRule && (rightRule*))

       This is normally used to match leading or trailing spaces.

       Either or both of leftRule and rightRule to be 0, but both st
       and mainRule must be valid.  As a special case, if both
       leftRule and leftRule are 0 then the returned rule is a bitwise
       copy of mainRule and no extra resources need to be allocated.

       There are two policies for how the matched string is set by
       this rule:

       - If discardLeftRight is false then leftRule's and rightRule's
       matches (if any) contribute to the matched string.

       - If discardLeftRight is true then leftRule's and rightRule's
       matches (if any) do not contribute to the matched string. That is,
       the matched string represents only mainRule's match, but the parser's
       current position will be set for rightRule's match (if any).

       If you do not want to discard left/right but do want the
       mainRule match isolated from left/right then use an action as
       mainRule and fetch the match from there.

       As an example of how discardLeftRight affects the match and iterator:

       \code
       PegcRule colon = pegc_r_char(':',true);
       PegcRule word = pegc_r_plus( &PegcRule_alpha );
       PegcRule R = pegc_r_pad( myParser, &colon, &word, &colon, true );
       \endcode

       When the string "::token::xyz" is parsed using the R rule, the
       position of the parser will be 'x', but the match string will be
       "token". It is possible to capture the left/right pad rule matches
       by wrapping them in an Action rule.
    */
    PegcRule pegc_r_pad( pegc_parser * st,
			 PegcRule const * leftRule,
			 PegcRule const * mainRule,
			 PegcRule const * rightRule,
			 bool discardLeftRight);

    /**
       An object implementing functionality identical to the
       C-standard isalnum().
    */
    extern const PegcRule PegcRule_alnum;

    /**
       An object implementing functionality identical to the
       C-standard isalpha().
    */
    extern const PegcRule PegcRule_alpha;

    /**
       An object which matches a single character in the range
       [0,127].
    */
    extern const PegcRule PegcRule_ascii;

    /**
       An object which matches a single character in the range
       [0,255].
    */
    extern const PegcRule PegcRule_latin1;

    /**
       An object implementing functionality identical to the
       C-standard isblank().
    */
    extern const PegcRule PegcRule_blank;

    /**
       A rule object for matching any number of blank characters
       (space or horizontal tab). Equivalent to: ([ \\t]*)
    */
    extern const PegcRule PegcRule_blanks;

    /**
       An object implementing functionality identical to the
       C-standard iscntrl().
    */
    extern const PegcRule PegcRule_cntrl;

    /**
       An object implementing functionality identical to the
       C-standard isdigit().
    */
    extern const PegcRule PegcRule_digit;

    /**
       An object implementing functionality identical to the
       C-standard isgraph().
    */
    extern const PegcRule PegcRule_graph;

    /**
       An object implementing functionality identical to the
       C-standard islower().
    */
    extern const PegcRule PegcRule_lower;

    /**
       An object implementing functionality identical to the
       C-standard isprint().
    */
    extern const PegcRule PegcRule_print;

    /**
       An object implementing functionality identical to the
       C-standard ispunct().
    */
    extern const PegcRule PegcRule_punct;

    /**
       An object implementing functionality identical to the
       C-standard isspace().
    */
    extern const PegcRule PegcRule_space;

    /**
       An object implementing functionality identical to the
       C-standard isupper().
    */
    extern const PegcRule PegcRule_upper;

    /**
       An object implementing functionality identical to the
       C-standard isxdigit().
    */
    extern const PegcRule PegcRule_xdigit;

    /**
       A rule matching one or more consecutive digits.
    */
    extern const PegcRule PegcRule_digits;

    /**
       Creates a rule which matches a decimal integer (optionally
       signed), but only if the integer part is not followed by an
       "illegal" character, namely:

       [._a-zA-Z]

       Any other trailing characters (including EOF) are considered
       legal.

       This rule requires a "relatively" large amount of dynamic
       resources (for several sub-rules), but it caches the rules on a
       per-parser basis. This subsequent calls with the same parser
       argument will always return a handle to the same object.
    */
    PegcRule pegc_r_int_dec_strict( pegc_parser * st );

    /**
       Similar to pegc_r_int_dec_strict(), but does not
       do "tail checking" and does not allocate any
       extra parser-specific resources. By "tail checking"
       we mean that it will consume until the end of an
       integer (optionally signed), but doesn't care if
       the character after the last digit can be part of
       a number. For example, when parsing "12345doh", it
       will parse up to the 'd' and then stop, and match
       "12345".

       Limitation: this type cannot parse numbers larger
       than can be represented in a long int.

       FIXME: use (long long) if C99 mode is enabled.
    */
    extern const PegcRule PegcRule_int_dec;

    /**
       Matches a double-precision floating point number (or optinally
       signed decimal integer), in all formats supported by sscanf().

       See PegcRule_int_dec for notes about the lack of
       "tail checking".

       Limitation: this type cannot parse numbers larger
       than can be represented in a double.
    */
    extern const PegcRule PegcRule_double;

    /**
       A rule which matches only at EOF and never consumes.
    */
    extern const PegcRule PegcRule_eof;

    /**
       A rule which always matches and never consumes.
    */
    extern const PegcRule PegcRule_success;

    /**
       A rule which never matches and never consumes.
    */
    extern const PegcRule PegcRule_failure;

    /**
       An "invalid" rule, with all data members set to 0.
    */
    extern const PegcRule PegcRule_invalid;

    /*
      A rule which matches: (\r\n) / (\n) / (\r)
    */
    extern const PegcRule PegcRule_eol;

    /**
       A rule which never consumes and only matches when one of the
       following is true:

       - at the beginning of a parse (before any other characters) are
       consumed).

       - The the previous character (before the current parse
       position) is a newline char.

       Note that an empty line (containing only a newline sequence)
       will match this rule (on the newline character), though it
       would seem to philosophically lie somewhere between BOL and
       EOL.
    */
    extern const PegcRule PegcRule_bol;

    /**
       Creates a rule which always returns false and sets the parser
       error message to msg. The msg string is not copied until the rule
       is triggered, so it must outlive the returned rule.
    */
    PegcRule pegc_r_error( pegc_parser * st, char const * msg );

    /**
       Creates a rule which always returns false, never consumes, and
       sets the parser error string to the printf-style formated
       string. In contrast to pegc_r_error(), the string is copied
       when the rule is created.
    */
    PegcRule pegc_r_error_v( pegc_parser * st, char const * fmt, va_list );

    /**
       Identical to pegc_r_error_v() except that it takes (...) instead of a va_list.
    */
    PegcRule pegc_r_error_e( pegc_parser * st, char const * fmt, ... );

#ifdef __cplusplus
} // extern "C"
#endif

#endif // WANDERINGHORSE_NET_PEGC_H_INCLUDED
