/*
 * Json-Formatter: A JSON prettier in C.
 * Turns arbitrary JSON strings into beautiful properly-whitespaced equivalents.
 *
 * By Sam Alexander
 *
 * On github:  https://github.org/semitrivial/jsonfmt
 */

#ifndef JSONFMT_INTERNAL_INCLUDE_GUARD
#define JSONFMT_INTERNAL_INCLUDE_GUARD

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

#define JSON_HASH 1048576

#define JSONFMT_ERR( txt )\
  do\
  {\
    if ( errptr )\
      *errptr = (txt);\
    return NULL;\
  }\
  while(0)

#define JSONFMT_LINK( link, first, last, next )\
do\
{\
  (link)->next = NULL;\
  if ( last )\
    (last)->next = (link);\
  else\
    (first) = (link);\
  (last) = (link);\
} while(0)

#define MAX_LEVEL 128
#define MAX_INDENT_SIZE 32

typedef struct JSON_STR json_str;

struct JSON_STR
{
  json_str *next;
  char *str;
};

void add_spaces( char **ptr, int count );
int next_nonwhitespace_is( const char *ptr, char c, const char **where );
int last_nonspace_was_newline( char *ptr, char *buf );
int is_json( const char *str );
unsigned int get_js_hash( char const *str );
char *json_c_adapter( int paircnt, ... );
char *json_enquote( const char *str );
char *prep_for_json_gc( char *str );
char *json_array_worker_( char * (*non_reentrant) (void *), char * (*reentrant) (void *, void *), void **array, void *data );

/*
 * JSONFMT_INTERNAL_INCLUDE_GUARD
 */
#endif

