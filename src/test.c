#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pegc.h"

#define MARKER printf("MARKER: %s:%s:%d:\n",__FUNCTION__,__FILE__,__LINE__);

void my_pegc_action( pegc_parser * st )
{
    char * c = pegc_get_match_string(st);
    MARKER; printf( "We got a match: [%s]\n", c ? c : "<EMPTY>");
    free(c);
}


int test_one()
{
    char const * src = "hihi \t world";
    pegc_parser * st;
    pegc_create_parser( &st, src, -1 );
    const unsigned int rulecount = 50;
    PegcRule Rules[rulecount];
    memset( Rules, 0, rulecount * sizeof(PegcRule) );

    unsigned int atRule = 0;
#define NR Rules[atRule++]
    NR = pegc_r_oneof("abcxyz",false);
    PegcRule rH = pegc_r_char( 'h', true );
    PegcRule rI = pegc_r_char( 'i', true );
    PegcRule rHI = pegc_r_or( st, &rI, &rH );
    NR = pegc_r_plus(&rHI);
    NR = pegc_r_star( &PegcRule_blank );
    PegcRule starAlpha = pegc_r_star(&PegcRule_alpha);
    NR = pegc_r_notat(&PegcRule_digit);
    NR = pegc_r_action( st, &starAlpha, my_pegc_action );
    NR = pegc_r_string("world",false); // will fail
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
    char const * src = "hiaF!";
    char const * x = src;
    for( ; *x; ++x )
    {
	printf("pegc_latin1(%d/%c) = %s\n",(int)*x, *x, pegc_latin1(*x));
    }
    src = "-3492xyz . asa";
    pegc_parser * P;
    pegc_create_parser( &P, src, -1 );
    int rc = 1;
#if 1
    //PegcRule sign = pegc_r_oneof("+-",true);
    //PegcRule R = pegc_r_notat( &PegcRule_alpha );
    const PegcRule R = PegcRule_int_dec;
#else
    const PegcRule R = pegc_r_int_dec_strict(P);
#endif
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
	printf("int_dec failed to match [%s]\n",src);
    }
    printf("pos = [%s]\n", pegc_eof(P) ? "<EOF>" : pegc_latin1(*pegc_pos(P)) );
    pegc_destroy_parser(P);
    return rc;
}
int main( int argc, char ** argv )
{
    int rc = test_one();
    if(!rc) rc = test_two();
    printf("Done rc=%d.\n",rc);
    return rc;
}
