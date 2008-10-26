#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pegc.h"
#include "whgc.h"
#include "whclob.h"
#if 1
#define MARKER printf("******** MARKER: %s:%d:%s():\n",__FILE__,__LINE__,__func__);
#else
#define MARKER printf("******** MARKER: %s:%d:\n",__FILE__,__LINE__);
#endif


static struct ThisApp
{
    char const * argv0;
    pegc_parser * P;
    whgc_context * gc;
    char ** argv;
} ThisApp;

bool pg_test_action( pegc_parser * st,
		     pegc_cursor const *match,
		     void * msg )
{
#if 1
    char * c = pegc_cursor_tostring(*match);
    MARKER; printf( "%s() got a match: %s [%s]\n",
		    __func__,
		    msg ? (char const *)msg : "",
		    c ? c : "<EMPTY>");
    free(c);
#endif
    return true;
}

bool run_test( pegc_parser * P,
	       PegcRule r,
	       char const * name,
	       char const * input,
	       char const *expect,
	       bool shouldFail )
{
    pegc_parser * p = P ? P : ThisApp.P;
    pegc_set_input( P, input, -1 );
    pegc_const_iterator orig = pegc_pos(p);
    bool rc = pegc_parse( p, &r );
    bool realRC = rc;
    if( shouldFail && !rc ) rc = true;
    if( pegc_has_error(p) )
    {
	char const * err = pegc_get_error(p,0,0);
	if( shouldFail )
	{
	    rc = true;
	    printf("Got expected failure for rule [%s]\nInput=[%s]\nExpected=[%s]\nParser says: [%s]\n",
		   name, input, expect, err );
	}
	else
	{
	    rc = false;
	    printf("test failed for rule [%s]\nInput=[%s]\nExpected=[%s]\nParser says: [%s]\n",
		   name, input, expect, err );
	}
    }
    while( rc )
    {
	if( pegc_pos(p) == orig )
	{
	    printf("Rule succeeded but did not consume.\n");
	    if( expect && *expect )
	    {
		printf("EXPECT string [%s] was not empty for a non-consuming rule.\n",expect);
		rc = false;
	    }
	    break;
	}
	if( ! expect ) break;
	char * m = pegc_get_match_string(p);
	int len = strlen(expect);
	if( (0 != strncmp(m,expect,len)) )
	{
	    rc = false;
	    printf("Expected result does not match real result:\nRule name=[%s]\nInput=[%s]\nMatch=[%s]\nExpected=[%s]\n",
		   name, input, m, expect );
	}
	else
	{
	    printf("Rule matched expectations: [%s]==[%s]\n",m,expect);
	}
	free(m);
	break;
    }
    printf("Rule %s: [%s]\n",
	   (rc && realRC) ?
	   "succeeded"
	   : ((shouldFail && !realRC)
		     ? "successfully failed"
	      : "FAILED" ),
	   name
	   );
    return rc;
}

int a_test()
{
#define TEST(R,IN,EXP,SHOULDFAIL) \
    MARKER;printf("Testing rule [%s]\n",# R); \
    if( ! run_test(P,R,# R,IN,EXP,SHOULDFAIL) ) return 1;
#define TEST1(R,IN,EXP) TEST(R,IN,EXP,false)
#define TEST0(R,IN) TEST(R,IN,0,true)

#define RULE PegcRule const
    pegc_parser * P = ThisApp.P; //pegc_create_parser(0,0);

    RULE end = PegcRule_invalid;
    RULE alpha = PegcRule_alpha; 
    RULE digit = PegcRule_digit; 
    RULE a_plus = pegc_r_plus_p(&alpha);
    RULE d_plus = pegc_r_plus_p(&PegcRule_digit); 
    RULE a_then_d = pegc_r_and_ev(P,alpha,digit,end);
    RULE space = pegc_r_star_p(&PegcRule_space);

    TEST1(alpha,"zyx","z");
    TEST1(a_plus,"zyx","zyx");
    TEST1(digit,"123","1");
    TEST0(digit,"a123");
    TEST1(a_then_d,"a123","a1");

    RULE a_star = pegc_r_star_p(&alpha);
    TEST1(a_star,"ghij345","ghij");

    RULE d_pad = pegc_r_pad_p(P,&alpha,
			      &d_plus,
			      &alpha,
			      true );
    TEST1(d_pad,"abc123def","123");
    RULE d_pad2 = pegc_r_pad_p(P,&alpha,
			       &d_plus,
			       &alpha,
			       false );
    TEST1(d_pad2,"abc123def","abc123def");
    RULE at_a =
	pegc_r_and_ev(P,
		      space,
		      pegc_r_at_p(&alpha));
    RULE not_a = pegc_r_notat_p(&at_a);
    TEST1(not_a," *789*","");
    TEST1(at_a,"  a*789*","  ");

    RULE until_a = pegc_r_until_p(&at_a);
    TEST1(until_a," - a*789*"," - ");

    return 0;
}

int main( int argc, char ** argv )
{
    ThisApp.argv = argv+1;
    ThisApp.gc = whgc_create_context( &ThisApp );
    int i = 0;
    ThisApp.argv0 = argv[0];
    if(0) for( i = 0; i < argc; ++i )
    {
	printf("argv[%d]=[%s]\n",i,argv[i]);
    }
    ThisApp.P = pegc_create_parser( 0, 0 );
    int rc = 0;
    if(!rc) rc = a_test();
    //if(!rc) rc = test_actions();
    if( 1 )
    {
	whgc_stats const st = whgc_get_stats( ThisApp.gc );
	MARKER;printf("Approx memory allocated by ThisApp.gc context: %u\n", st.alloced);
	printf("GC entry/add/take count: %u/%u/%u\n", st.entry_count, st.add_count, st.take_count);
	pegc_stats const pst = pegc_get_stats( ThisApp.P );
	printf("APPROXIMATE allocated parser memory: parser=%u gc=%u\n", pst.alloced, pst.gc_internals_alloced);
    }
    whgc_destroy_context( ThisApp.gc );
    pegc_destroy_parser( ThisApp.P );
    printf("Done rc=%d=[%s].\n",rc,
	   (0==rc)
	   ? "You win :)"
	   : "You lose :(");
    return rc;
}
