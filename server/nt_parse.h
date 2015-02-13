#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef void ADD_TRIPLE_FUNCTION ( char *subj, char *pred, char *obj );

int parse_ntriples( FILE *fp, char **err, int max_iri_len, ADD_TRIPLE_FUNCTION *fnc );
