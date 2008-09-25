#ifndef WANDERINGHORSE_NET_ACP_H_INCLUDED
#define WANDERINGHORSE_NET_ACP_H_INCLUDED
/************************************************************************
ACP (A C Parser [toolkit]) is an experimental toolkit for writing
parsers in C using something similar to functional composition,
conceptually similar to C++ parsing toolkits like Boost.Spirit
and parsepp (http://wanderinghorse.net/cgi-bin/parsepp.cgi).

Author: Stephan Beal (http://wanderinghorse.net/home/stephan)
License: Public Domain

ACP attempts to implement a model of parser which has become quite
popular in C++, but within the limitations of C (e.g. lack of type
safety in many places, and no safe casts).

The basic idea is that one defines a grammar as a list of Rule
objects. A grammar starts with a top rule, and that rule may then
delegate all parsing, as it sees fit, to other rules. The result of a
parse is either 'true' (the top-most rule matches) or false (the
top-most rule fails). It is roughly modelled off of recursive descent
parsers, and follows some of those conventions. For example, a parsing
rule which does not match (i.e. return true) must not consume
input. Most rules which do match, on the other hand, do consume (there
are several exceptions to that rule, though).

In C++ we would build the parser using templates (at least that's
how i'd do it). In C we don't have that option, so we build up little
objects which contain a Rule function and some data for that function.
Those rules can then be processed in an RD fashion.

My theory is that once the basic set of rules are in place, it will be
relatively easy to implement a self-hosted code generator which can
read a lex/yacc/lemon-like grammar and generate ACP-based parsers. That
is, an ACP-parsed grammar which in turn generates ACP parsers code.

************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __cplusplus
#if 0
/* i still can't fucking believe that C has no bool. */
#  if !defined(bool)
#    define bool char
#    define true 1
#    define false 0
#  endif
#else /* aha! stdboo.h! */
#include <stdbool.h>
#endif
#endif

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
       Copy this object to get an pegc_cursor with
       its pointers properly initialized to 0.
    */
    extern const pegc_cursor pegc_cursor_init;

    /**
       Initializes it to point at the range [begin,end) and sets
       it->pos set to begin. Returns false and does nothing
       if (end<begin).
    */
    bool pegc_init_cursor( pegc_cursor * it, pegc_const_iterator begin, pegc_const_iterator end );

    struct pegc_parser;
    typedef struct pegc_parser pegc_parser;

    typedef bool (*pegc_rule_f)( pegc_parser * state );

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
       \endcode
    */
    bool pegc_create_parser( pegc_parser ** st, char const * inp, long len );

    /**
       Clears the parser's internal state, freeing any resources
       created internally by the parsing process. It then calls
       free() to deallocate the parser.

       This routine returns false only if st is 0.

       Note that the library internally allocates some storage
       associated with the parser for certain opertaions (e.g.  see
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
    bool pegc_eof( pegc_parser * st );

    /**
       Returns st's cursor.
    */
    pegc_cursor * pegc_iter( pegc_parser * st );

    /**
       Returns st's starting position.
    */
    pegc_const_iterator pegc_begin( pegc_parser * st );

    /**
       Returns st's ending position. This uses the one-after-the-end
       idiom, so the pointed-to value is considered invalid and should
       never be dereferenced.
    */
    pegc_const_iterator pegc_end( pegc_parser * st );
    /**
       Returns true only if (p>=pegc_begin(st)) and (p<pegc_end(st)).
    */
    bool pegc_in_bounds( pegc_parser * st, pegc_const_iterator p );
    /**
       Returns the current position in the parser.
    */
    pegc_const_iterator pegc_pos( pegc_parser * st );

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
       Return (e-pegc_pos(st)). It does no bounds checking.
    */
    long pegc_distance( pegc_parser * st, pegc_const_iterator e );

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

       Note that all rules which do not consume are supposed to update
       the matched string, so this value may be updated arbitrarily
       often during a parse run. Its value only applies to the last
       rule which set the match point (via pegc_set_match()).
    */
    pegc_cursor pegc_get_match_cursor( pegc_parser * st );

    /**
       Returns a copy of the current match string, or 0 if there
       is no match or there is a length-zero match. The caller
       is responsible for deallocating it using free().
    */
    pegc_iterator pegc_get_match_string( pegc_parser * st );

    /**
       Returns true if ch matches the character at pegc_pos(st). It only
       compares, it does not consume input.
    */
    bool pegc_matches_char( pegc_parser * st, int ch );

    /**
       Case-insensitive form of pegc_matches_char.
    */
    bool pegc_matches_chari( pegc_parser * st, int ch );

    /**
       If the next strLen characters of st match str then true is
       returned.  If strLen is less than 0 then strlen(str) is used to
       determine the length. If caseSensitive is false then the
       strings must match exactly, otherwise the comparison is done
       using tolower() on each char of each string. It only compares,
       it does not consume input.
    */
    bool pegc_matches_string( pegc_parser * st, pegc_const_iterator str, long strLen, bool caseSensitive );

    /**
       Clears the parser's match string.
    */
    void pegc_clear_match( pegc_parser * st );

    /**
       Returns a pointer to a statically allocated length-one string of
       all latin1 characters in the range [0,255]. The returned string
       contains the value of the given character, such that pegc_latin1('c')
       will return a pointer to a string with the value "c". The caller
       must never modify nor deallocate the string, as it is statically allocated
       the first time this routine is called. It is non-const because
       we need to be able to pass it as a value to PegcRule.data.

       If ch is not in the range [0,255] then 0 is returned.

       This function is intended to ease the implementation of
       PegcRule chains for rules matching single characters.
    */
    pegc_const_iterator pegc_latin1(int ch);


    /**
       If r(st) returns true then pegc_set_match() will contain the
       matched string, otherwise no input is consumed.
    */
    bool pegc_try_rule( pegc_parser * st, pegc_rule_f r );
    /**
       Don't use this.

       Requires that r is an array of pegc_rule_fs which is at least n entries
       long. For each rule in r, rule(st) is called. If it returns true,
       that result is returned.

       If and_op is true then this routine only returns true if all rules
       match.  If and_op is false then this routine returns true at the
       first match (i.e. an "or" operation).

       If no rules match, false is returned and no input is consumed.
    */
    bool pegc_try_rulesn( pegc_parser * st, pegc_rule_f *r, unsigned int n, bool and_op );

    /**
       Requires that r be a NULL-termined array of pegc_rule_fs. This routine
       walks the array to the first NULL then calls
       pegc_try_rules(r,rSize,st,and_op).
    */
    bool pegc_try_rules( pegc_parser * st, pegc_rule_f *r, bool and_op );

    /**
       Always returns false and does nothing.
    */
    bool pegc_r_failure( pegc_parser * st );
    /**
       Always returns true and does nothing.
    */
    bool pegc_r_success( pegc_parser * st );

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
       member function. Most rules hold a (char const *) here and
       match against a string or the characters in the string.
       Non-string rules will have other uses for that data pointer.

       Some rules also need a proxy rule, on whos behalf they run
       (normally providing some other processing if the proxy rule
       matches, such as running an action).
    */
    struct PegcRule
    {
	/**
	   This object's rule function.
	*/
	PegcRule_mf rule;

	/**
	   Data used by the rule function. The exact format of the
	   data is dependent on the rule member. Type mismatches will
	   cause undefined behaviour!
	*/
	void const * data;

	/**
	   Some rules need a proxy rule. Since it is not always
	   convenient to use the data slot for this, here it is...
	*/
	struct PegcRule const * proxy;

	/**
	   The client object is reserved for client-side use.
	   This library makes no use of it.
	*/
	struct {
	    /**
	       For client-side use. This library makes no use of it.
	    */
	    unsigned int flags;
	    /**
	       For client-side use. This library makes no use of it.
	    */
	    void * data;
	} client;

	/**
	   This _internal object reserved for internal use by the
	   library. It's structure may change with any revision of
	   this code - don't rely on it.
	*/
	struct
	{
	     /*
	       A unique lookup key for some mappings (e.g. actions and
	       rule lists).
	     */
	    void * key;
	} _internal;
    };
    typedef struct PegcRule PegcRule;

    /**
       This object can (should) be used as an initializer to ensure a
       clean slate for the pointer members of PegcRule objects. Simply
       copy this over the object. Its default rule is a rule which
       never matches (always returns false) and does not consume.
    */
    extern const PegcRule PegcRule_init;

    /**
       Creates an PegcRule from the given arguments. All fields
       not covered by these arguments are set to 0.
    */
    PegcRule pegc_r( PegcRule_mf func, void const * data );

    /**
       Requires that self->data be a pegc_const_iterator. Matches if any
       character in that string matches the next char of st.
    */
    PegcRule pegc_r_oneof( char const * list, bool caseSensitive );

    /**
       Creates a 'star' rule for the given proxy
       rule. See PegcRule_mf_star().
    */
    PegcRule pegc_r_star( PegcRule const * proxy );

    /**
       Creates a 'plus' rule for the given proxy
       rule. See PegcRule_mf_plus().
    */
    PegcRule pegc_r_plus( PegcRule const * proxy );

    /**
       Creates a rule which will match the given string
       case-sensitively. The string must outlive the rule,
       as it is not copied.
    */
    PegcRule pegc_r_string( pegc_const_iterator input, bool caseSensitive );

    /**
       Creates a rule which matches the given character, which must
       be in the range [0,127].
    */
    PegcRule pegc_r_char( pegc_char_t ch, bool caseSensitive );

    /**
       Creates a rule which matches if proxy matches, but
       does not consume. proxy must not be 0 and must outlive
       the returned object.
    */
    PegcRule pegc_r_at( PegcRule const * proxy );

    /**
       Creates a rule which performs either an OR (if orOp is true) or
       an AND (if orOp is false) on the given list of
       rules. The list MUST be terminated with either NULL, or an entry
       where entry->rule is 0, or results are undefined (almost certainly
       an overflow).

       All rules in li must outlive the returned object.

       This allocates resources for the returned rule which belong to
       this API and are freed when st is destroyed.
    */
    PegcRule pegc_r_list( pegc_parser * st, bool orOp, PegcRule const ** li );

    /**
       Convenience form of pegc_r_list( st, true, ... ).
    */
    PegcRule pegc_r_or( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs );

    /**
       Convenience form of pegc_r_list( st, false, ... ).
    */
    PegcRule pegc_r_and( pegc_parser * st, PegcRule const * lhs, PegcRule const * rhs );

    /**
       Typedef for Action functions. Actions are run in response to
       rules. Actions are created using pegc_r_action().

       Actions can act on client-side data by setting st->client.data
       and accessing it from the action.
    */
    typedef void (*pegc_action)( pegc_parser * st );

    /*
      Creates a new Action. If rule matches then onMatch(pegc_parser*)
      is called. onMatch can fetch the matched string (which must have
      been set by rule) using pegc_get_match_string() or
      pegc_get_match_cursor().

      This allocates resources for the returned rule which belong to
      this API and are freed when st is destroyed.

     */
    PegcRule pegc_r_action( pegc_parser * st,
			  PegcRule const * rule,
			  pegc_action onMatch );

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
       An object implementing functionality identical to the isascii()
       found in some C libraries
    */
    extern const PegcRule PegcRule_ascii;

    /**
       An object implementing functionality identical to the
       C-standard isblank().
    */
    extern const PegcRule PegcRule_blank;

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


#ifdef __cplusplus
} // extern "C"
#endif

#endif // WANDERINGHORSE_NET_ACP_H_INCLUDED
