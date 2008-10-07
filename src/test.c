#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pegc.h"

#if 1
#define MARKER printf("MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("MARKER: %s:%d:\n",__FILE__,__LINE__);
#endif

void my_pegc_action( pegc_parser const * st, void * unused )
{
    char * c = pegc_get_match_string(st);
    MARKER; printf( "my_pegc_action got a match: [%s]\n", c ? c : "<EMPTY>");
    free(c);
}

void my_match_listener( pegc_parser const * st, void * d )
{
    char * c = pegc_get_match_string(st);
    MARKER; printf( "my_match_listener got a match: [%s]\ndata=%p\n",
		    c ? c : "<EMPTY>",
		    d );
    free(c);
}

int test_one()
{
    char const * src = "hihi \t world";
    pegc_parser * st;
    pegc_create_parser( &st, src, -1 );
    //pegc_add_match_listener( st, my_match_listener, st );
    const unsigned int rulecount = 50;
    PegcRule Rules[rulecount];
    memset( Rules, 0, rulecount * sizeof(PegcRule) );

    unsigned int atRule = 0;
#define NR Rules[atRule++]
    NR = pegc_r_oneof("abcxyz",false);
    PegcRule rH = pegc_r_char( 'h', true );
    PegcRule rI = pegc_r_char( 'i', true );
    PegcRule rHI = pegc_r_or( st, &rI, &rH );
    PegcRule rHIPlus = pegc_r_plus(&rHI);
    NR = pegc_r_action( st, &rHIPlus, my_pegc_action, 0 );
    NR = pegc_r_star( &PegcRule_blank );
#if 0
    PegcRule starAlpha = pegc_r_star(&PegcRule_alpha);
#else
    PegcRule starAlpha =
	pegc_r_repeat(st,&PegcRule_alpha,3,10)
	//pegc_r_opt(&PegcRule_alpha)
	;
#endif
    NR = pegc_r_notat(&PegcRule_digit);
    NR = pegc_r_action( st, &starAlpha, my_pegc_action, 0 );
    //NR = pegc_r_string("world",false); // will fail
    NR = pegc_r(0,0); // end of list
#undef ACPMF
#undef NR

    PegcRule * R = Rules;
    pegc_const_iterator at = 0;
    MARKER; printf("Input string=[%s]\n", src);
    bool rc;
    int i = 0;
    for( ; R && R->rule && !pegc_eof(st); ++R, ++i )
    {
	printf("Trying PegcRule[#%d, rule=%p, data=[%p]]\n",
	       i,
	       (void const *)R->rule,
	       (void const *)R->data );
	rc = R->rule( R, st );
	at = pegc_pos(st);
	printf("\trc == %d, current pos=", rc );
        if( pegc_eof(st) ) printf("<EOF>\n");
	else printf("[%c]\n", (at && *at) ? *at : '!' );
	pegc_iterator m = rc ? pegc_get_match_string(st) : 0;
	if( m )
	{
	    printf("\tMatched string=[%s]\n", m );
	    free(m);
	}
    }
    pegc_destroy_parser(st);
    return 0;
}



int test_two()
{
    MARKER; printf("test two...\n");
    char const * src = "hiaF!";
    char const * x = src;
    for( ; *x; ++x )
    {
	printf("pegc_latin1(%d/%c) = %s\n",(int)*x, *x, pegc_latin1(*x));
    }
#define TRY_STRICT 1
#if TRY_STRICT
    src = "-3492 . xyz . asa";
#else
    src = "-3492.323asa";
#endif
    pegc_parser * P;
    pegc_create_parser( &P, src, -1 );
    //pegc_add_match_listener( P, my_match_listener, P );
    int rc = 1;
#if ! TRY_STRICT
    //PegcRule sign = pegc_r_oneof("+-",true);
    //PegcRule R = pegc_r_notat( &PegcRule_alpha );
    const PegcRule R = PegcRule_int_dec;
    //const PegcRule R = PegcRule_double;
#else
    const PegcRule R = pegc_r_int_dec_strict(P);
    const PegcRule R2 = pegc_r_int_dec_strict(P); // just checking the cache hit
#endif
#undef TRY_STRICT
    printf("Source string = [%s]\n", src );
    if( pegc_parse(P, &R) )
    {
	rc = 0;
	char * m  = pegc_get_match_string(P);
	printf("Got match on [%s]: [%s]\n",src, m?m:"<EMPTY>");
	free(m);
    }
    else
    {
	printf("number parse failed to match [%s]\n",src);
    }
    printf("pos = [%s]\n", pegc_eof(P) ? "<EOF>" : pegc_latin1(*pegc_pos(P)) );
    pegc_destroy_parser(P);
    return rc;
}

int test_three()
{
    if(1)
    {
	char const * in = "fbde";
	char out[50];
	memset(out,0,50);
	int rc = sscanf(in,"%1[bdf]",&out[0]);
	MARKER; printf("sscanf rc==%d, out=%s\n",rc,out);
	//return -1;
    }


    MARKER; printf("test three...\n");
    char const * src = "ZYXtokenCBA!end";
    pegc_parser * P;
    pegc_create_parser( &P, src, -1 );

    PegcRule colon = pegc_r_char(':',true);
#if 1
    PegcRule rangeU = pegc_r_char_spec( P, "[A-Z]" );
    PegcRule rangeL = pegc_r_char_spec( P, "[a-z]" );
#else
    PegcRule rangeU = pegc_r_char_range( 'A','Z' );
    PegcRule rangeL = pegc_r_char_range( 'a','z' );
#endif
    PegcRule delim = pegc_r_plus( &rangeU );
    PegcRule word = pegc_r_plus( &rangeL );
    PegcRule R = pegc_r_pad( P, &delim, &word, &delim, true );
    int rc = 0;
    if( pegc_parse(P, &R) )
    {
	rc = 0;
	char * m  = pegc_get_match_string(P);
	pegc_const_iterator pos = pegc_pos(P);
	printf("Got match on [%s]: [%s] current pos=[%s]\n",
	       src,
	       m?m:"<EMPTY>",
	       (pos && *pos) ? pegc_latin1(*pos) : "<NULL>"
	       );
	free(m);
    }
    else
    {
	rc = 1;
	printf("failed to match [%s]\n",src);
    }

    pegc_destroy_parser( P );
    return rc;
}

int main( int argc, char ** argv )
{
    int rc = 0;
    rc = test_one();
    if(!rc) rc = test_two();
    if(!rc) rc = test_three();
    printf("Done rc=%d.\n",rc);
    return rc;
}
