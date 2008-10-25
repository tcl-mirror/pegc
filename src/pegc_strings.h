#ifndef WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED
#define WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED
/**
   This file is part of the pegc toolkit. Author and license
   information can be found in pegc.h.
*/
#include "pegc.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
       Unescapes an input string using a simple set of rules. Those
       rules are...

       inp must be a 0-terminated string at least inlen bytes long.
       If (inlen<0) then pegc_strlen() is used instead. inp must begin
       and end with the given quote character. Any such characters within
       the quoted string must be escaped by escChar, otherwise they will
       be interpretted as the end of the string.

       Unusual argument combinations:
       
       - (quoteChar==0) is not legal.

       - (escChar==0) is legal but implies that it is not possible to
       have embedded quote characters in a string.

       - (escChar==quoteChar) does not yet work. (e.g. SQL-style
       double-single-quotes escaping.)


       Escaping Rules:

       Characters prefixed by the given esc character have that character
       removed and the two-char sequence is replaced by an unescaped
       sequence (see below).


       All characters in the matched string which are preceeded by the
       esc character are replaced in the unescaped string with the
       second character (i.e. the esc is stripped). As a special case,
       if esc is a backslash then some common C-style escape sequences
       are supported (e.g. \\t becomes a tab character) and unknown
       escape sequences are simply unescaped (whereas in C they would
       be considered illegal).


       (End of escaping-specific notes.)

    */
    char * pegc_unescape_quoted_string( pegc_const_iterator inp,
					long inlen,
					int quoteChar,
					int escChar
					);

    /**
       Creates a rule for parsing quoted strings.  The quoteChar and
       escChar define the quote and escape characters, respectively.
       The target argument requires a bit of explaining, but first
       here's an example of how it's used:

       @code
       char * target = 0;
       PegcRule const S = pegc_r_string_quoted_unescape(P,'"','\\',&target);
       if( pegc_parse( P, &S ) ) { ... target now has a value ... }
       @endcode

       The rule looks for a string bound by the given quote character.

       If the target parameter is null then no escaping is done except
       to look for escaped quote character. Otherwise the escaping rules
       for pegc_unescape_quoted_string() apply.

       If target is non-null then each time this rule matches
       (*target) is assigned to a dynamically allocated string - the
       unescaped/unquoted version of pegc_get_match_string(). The
       target's initial value, passed in from the caller, should be a
       pointer to an uninitialized string, as shown in the example
       above. An empty successful match will cause target to be set to
       0.

       All strings allocated by this rule are owned by the parser. They
       are freed in two cases:

       a) When this rule successfully matches, any previous match is
       free()d and replaced with a new string. A failed attempt to
       match match will not clear the previous successful match.

       b) When pegc_destroy_parser() is called, all underlying
       metadata is freed (which includes the previous match string).


       Other Notes:

       - This rule checks up until the next closing quote, spanning
       newlines and other grammatical constructs.

       - (quoteChar==0) is not legal.

       - (escChar==0) is legal but implies that it is not possible to
       have embedded quote characters in a string.

       - (escChar==quoteChar) does not yet work. (e.g. SQL-style
       double-single-quotes escaping.)

       - If rule construction fails ((!st), (quoteChar==0), or
       allocation error), an invalid rule is returned.

       TODO:

       - This function should accept a user-definable table of escape
       sequences.

       - Consider whether or not to allow unescaping of \\0.

       - Consider how best to handle escapes of \\##-style
       characters.
    */
    PegcRule pegc_r_string_quoted( pegc_parser * st,
				   pegc_char_t quoteChar,
				   pegc_char_t escChar,
				   pegc_char_t ** target );

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED */

