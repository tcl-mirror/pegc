#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pegc.h"
#include "pegc_strings.h"
#include "whgc.h"
#if 1
#define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("MARKER: %s:%d:\n",__FILE__,__LINE__);
#endif

static struct PGApp
{
    char const * argv0;
    pegc_parser * P;
    whgc_context * gc;
} PGApp;

/*
Grammar taken from: http://piumarta.com/software/peg/peg.1.html


   Grammar         <- Spacing Definition+ EndOfFile
   Definition      <- Identifier LEFTARROW Expression
   Expression      <- Sequence ( SLASH Sequence )*
   Sequence        <- Prefix*
   Prefix          <- AND Action
                    / ( AND | NOT )? Suffix
   Suffix          <- Primary ( QUERY / STAR / PLUS )?
   Primary         <- Identifier !LEFTARROW
                    / OPEN Expression CLOSE
                    / Literal
                    / Class
                    / DOT
                    / Action
                    / BEGIN
                    / END
   Identifier      <- < IdentStart IdentCont* > Spacing
   IdentStart      <- [a-zA-Z_]
   IdentCont       <- IdentStart / [0-9]
   Literal         <- ['] < ( !['] Char  )* > ['] Spacing
                    / ["] < ( !["] Char  )* > ["] Spacing
   Class           <- '[' < ( !']' Range )* > ']' Spacing
   Range           <- Char '-' Char / Char
   Char            <- '\\' [abefnrtv'"\[\]\\]
                    / '\\' [0-3][0-7][0-7]
                    / '\\' [0-7][0-7]?
                    / '\\' '-'
                    / !'\\' .
   LEFTARROW       <- '<-' Spacing
   SLASH           <- '/' Spacing
   AND             <- '&' Spacing
   NOT             <- '!' Spacing
   QUERY           <- '?' Spacing
   STAR            <- '*' Spacing
   PLUS            <- '+' Spacing
   OPEN            <- '(' Spacing
   CLOSE           <- ')' Spacing
   DOT             <- '.' Spacing
   Spacing         <- ( Space / Comment )*
   Comment         <- '#' ( !EndOfLine . )* EndOfLine
   Space           <- ' ' / '\t' / EndOfLine
   EndOfLine       <- '\r\n' / '\n' / '\r'
   EndOfFile       <- !.
   Action          <- '{' < [^}]* > '}' Spacing
   BEGIN           <- '<' Spacing
   END             <- '>' Spacing

*/

bool pg_test_action( pegc_parser * st,
		     pegc_cursor const *match,
		     void * msg )
{
    char * c = pegc_cursor_tostring(*match);
    MARKER; printf( "my_pegc_action got a match: %s [%s]\n",
		    msg ? (char const *)msg : "",
		    c ? c : "<EMPTY>");
    free(c);
    return true;
}

static bool PG_mf_spacing( PegcRule const * self, pegc_parser * p )
{
    if( !p || pegc_has_error(p) ) return false;
    if( pegc_eof(p) ) return true;
    PegcRule const space = pegc_r_plus_p( &PegcRule_blank );
    return space.rule( &space, p );
}
static const PegcRule PG_spacing = PEGC_INIT_RULE2(PG_mf_spacing,0);
static const PegcRule PG_end = PEGC_INIT_RULE;
static const PegcRule PG_alpha_uscor =
    PEGC_INIT_RULE2(PegcRule_mf_oneofi,"abcdebfhijklmnopqrstuvwxyz_");

PegcRule pg_r_spacearound( pegc_parser * p, PegcRule const R )
{
    return pegc_r_pad_v(p, PegcRule_success, R, PG_spacing, true );
}

PegcRule pg_r_identifier( pegc_parser * p )
{
    /*
   Identifier      <- < IdentStart IdentCont* > Spacing
   IdentStart      <- [a-zA-Z_]
   IdentCont       <- IdentStart / [0-9]
    */
    PegcRule const idstart =
	//pegc_r_oneof("abcdebfhijklmnopqrstuvwxyz_",false)
	//pegc_r_char_spec(p,"[a-zA-Z_]")
	PG_alpha_uscor
	;
    PegcRule const idcont = pegc_r_star_v(p,pegc_r_or_ev(p,idstart,PegcRule_digit,PG_end));
    PegcRule const id = pegc_r_and_ev(p, idstart, idcont, PG_end);
    PegcRule const pad = pg_r_spacearound( p, id );
    //PegcRule const id = pegc_r_and_ev(p, idstart, idcont, PG_spacing, PG_end);
    PegcRule const act = pegc_r_action_i_v( p, pad, pg_test_action, "pg_r_identifier()");
    return act;
}


int a_test()
{
    char const * src = "_abcd _1212 t930_9";
    pegc_parser * P = pegc_create_parser( src, -1 );
    PegcRule const R = pg_r_identifier(P);
    while( pegc_parse( P, &R ) )
    {
	char * m = pegc_get_match_string(P);
	MARKER;printf("matched: [%s]\n", m ? m : "<EMPTY>");
	free(m);
    }
    pegc_destroy_parser(P);
    return 0;
}

int main( int argc, char ** argv )
{
    MARKER; printf("This is an unfinished app! Don't use it!\n");
    PGApp.gc = whgc_create_context( &PGApp );
    int i = 0;
    PGApp.argv0 = argv[0];
    for( i = 0; i < argc; ++i )
    {
	MARKER;printf("argv[%d]=[%s]\n",i,argv[i]);
    }
    PGApp.P = pegc_create_parser( 0, 0 );
    int rc = 0;
    if(!rc) rc = a_test();
    //if(!rc) rc = test_actions();
    printf("Done rc=%d=[%s].\n",rc,
	   (0==rc)
	   ? "You win :)"
	   : "You lose :(");
    whgc_destroy_context( PGApp.gc );
    return rc;
}
