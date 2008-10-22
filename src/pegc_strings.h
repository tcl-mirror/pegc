#ifndef WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED
#define WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED

#include "pegc.h"

#ifdef __cplusplus
extern "C" {
#endif

    PegcRule pegc_r_string_quoted_s( pegc_parser * st,
				     pegc_char_t ** target );
    PegcRule pegc_r_string_quoted_d( pegc_parser * st,
				     pegc_char_t ** target );
    PegcRule pegc_r_string_quoted( pegc_parser * st,
				   pegc_char_t ** target );

#ifdef __cplusplus
} /* extern "C" */
#endif


#endif /* WANDERINGHORSE_NET_PEGC_STRINGS_H_INCLUDED */

