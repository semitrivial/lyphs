#include "lyph.h"
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

char **html_codes;
int *html_code_lengths;

void log_string( char *txt )
{
    printf( "%s\n", txt );
}

void log_stringf( char *fmt, ... )
{
  char *buf;
  va_list args;

  va_start( args, fmt );
  buf = vstrdupf( fmt, args );
  va_end( args );

  log_string( buf );

  free( buf );
}

void log_linenum( int linenum )
{
    printf( "(Line %d)\n", linenum );
}

void error_message( char *err )
{
    log_string( err );
}

char *html_encode( char *str )
{
  char *buf;
  char *bptr, *sptr;
  char *rebuf;

  CREATE( buf, char, strlen(str)*6 + 1 );

  for ( bptr = buf, sptr = str; *sptr; sptr++ )
  {
    strcat( bptr, html_codes[(int)(*sptr)] );
    bptr += html_code_lengths[(int)(*sptr)];
  }

  CREATE( rebuf, char, strlen(buf)+1 );
  *rebuf = '\0';
  strcat( rebuf, buf );
  free(buf);

  return rebuf;
}

void init_html_codes( void )
{
  char i;

  CREATE( html_codes, char *, CHAR_MAX - CHAR_MIN + 1 );
  CREATE( html_code_lengths, int, CHAR_MAX - CHAR_MIN + 1 );

  html_codes = &html_codes[-CHAR_MIN];
  html_code_lengths = &html_code_lengths[-CHAR_MIN];

  for ( i = CHAR_MIN; ; i++ )
  {
    if ( i <= 0 )
    {
      html_codes[(int)i] = "";
      html_code_lengths[(int)i] = 0;
      continue;
    }
    switch(i)
    {
      default:
        CREATE( html_codes[(int)i], char, 2 );
        sprintf( html_codes[(int)i], "%c", i );
        html_code_lengths[(int)i] = 1;
        break;
      case '&':
        html_codes[(int)i] = "&amp;";
        html_code_lengths[(int)i] = strlen( "&amp;" );
        break;
      case '\"':
        html_codes[(int)i] = "&quot;";
        html_code_lengths[(int)i] = strlen( "&quot;" );
        break;
      case '\'':
        html_codes[(int)i] = "&#039;";
        html_code_lengths[(int)i] = strlen( "&#039;" );
        break;
      case '<':
        html_codes[(int)i] = "&lt;";
        html_code_lengths[(int)i] = strlen( "&lt;" );
        break;
      case '>':
        html_codes[(int)i] = "&gt;";
        html_code_lengths[(int)i] = strlen( "&gt;" );
        break;
    }
    if ( i == CHAR_MAX )
      break;
  }
}

char *lowercaserize( char *x )
{
  static char buf[MAX_STRING_LEN * 2];
  char *xptr = x;
  char *bptr = buf;

  for ( ; *xptr; xptr++ )
  {
    if ( *xptr >= 'A' && *xptr <= 'Z' )
      *bptr++ = *xptr + 'a' - 'A';
    else
      *bptr++ = *xptr;
  }

  *bptr = '\0';

  return buf;
}

char *get_url_shortform( char *iri )
{
  char *ptr;
  int end_index;

  if ( iri[0] != 'h' || iri[1] != 't' || iri[2] != 't' || iri[3] != 'p' )
    return NULL;

  end_index = strlen(iri)-1;

  ptr = &iri[end_index];

  do
  {
    if ( ptr <= iri )
    {
      ptr = &iri[end_index];

      do
      {
        if ( ptr <= iri )
          return NULL;
        if ( *ptr == '/' )
          return &ptr[1];
        ptr--;
      }
      while(1);
    }
    if ( *ptr == '#' )
      return &ptr[1];
    ptr--;
  }
  while(1);
}

/*
 * url_decode (and from_hex) courtesy of Fred Bulback (http://www.geekhideout.com/urlcode.shtml)
 */

/* Converts a hex character to its integer value */
char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

char *url_decode(char *str)
{
  char *pstr = str, *buf = malloc(strlen(str) + 1), *pbuf = buf;

  while (*pstr)
  {
    if (*pstr == '%')
    {
      if (pstr[1] && pstr[2])
      {
        *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
        pstr += 2;
      }
    }
    else
    if (*pstr == '+')
      *pbuf++ = ' ';
    else
      *pbuf++ = *pstr;

    pstr++;
  }

  *pbuf = '\0';
  return buf;
}

int is_number( const char *arg )
{
  int first = 1;

  if ( *arg == '\0' )
    return 0;

  for ( ; *arg != '\0'; arg++ )
  {
    if ( first && *arg == '-')
    {
      first = 0;
      continue;
    }

    if ( !isdigit(*arg) )
      return 0;

    first = 0;
  }

  return 1;
}

char *pretty_free( char *json )
{
  static char *pretty;
  char *err;

  if ( pretty )
    free( pretty );

  pretty = json_format( json, 2, &err );

  if ( !pretty )
  {
    pretty = json;
    log_string( "json_format failed during a call to pretty_free" );
    log_string( err );
  }
  else
    free( json );

  return pretty;
}

size_t voidlen( void **x )
{
  void **ptr;

  for ( ptr = x; *ptr; ptr++ )
    ;

  return ptr - x;
}

char *constraints_comma_list( lyph **constraints )
{
  lyph **ptr;
  static char *buf;
  char **ids, **iptr, *bptr;
  int len = 0;

  if ( buf )
    free( buf );

  CREATE( ids, char *, voidlen((void**)constraints) + 1 );
  iptr = ids;

  for ( ptr = constraints; *ptr; ptr++ )
  {
    *iptr = strdup( trie_to_static( (*ptr)->id ) );
    len += strlen( *iptr ) + strlen(",");
    iptr++;
  }

  *iptr = NULL;

  CREATE( buf, char, len+1 );
  bptr = buf;

  for ( iptr = ids; *iptr; iptr++ )
  {
    if ( iptr != ids )
      *bptr++ = ',';

    strcpy( bptr, *iptr );
    bptr += strlen(bptr);
  }

  *bptr = '\0';

  return buf;
}
