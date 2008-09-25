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


int main( int argc, char ** argv )
{
    char const * src = "hi \t world";
    pegc_parser * st;
    pegc_create_parser( &st, src, -1 );
    const unsigned int rulecount = 50;
    PegcRule Rules[rulecount];
    memset( Rules, 0, rulecount * sizeof(PegcRule) );

    unsigned int atRule = 0;
#define NR Rules[atRule++]
#define ACPMF(F,V) NR = pegc_r( F, V )

    NR = pegc_r_oneof("abcxyz",false);
    PegcRule rH = pegc_r_char( 'h', true );
    PegcRule rI = pegc_r_char( 'i', true );
    PegcRule rHI = pegc_r_or( st, &rI, &rH );
    NR = pegc_r_plus(&rHI);
    //NR = rH; NR = rI;
    NR = pegc_r_star( &PegcRule_blank );
    PegcRule starAlpha = pegc_r_star(&PegcRule_alpha);
    NR = pegc_r_action( st, &starAlpha, my_pegc_action );
    NR = pegc_r_string("world",false); // will fail
    ACPMF(0,0); // end of list

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
	       (char const *)R->data );
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
    printf("Done. rc==%d\n",rc);
    return 0;
}
