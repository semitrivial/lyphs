/*
 *  util.c
 *  Various utility functions.
 */
#include "lyph.h"
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <time.h>

char **html_codes;
int *html_code_lengths;

void log_string( char *txt )
{
    to_logfile( txt );
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

void to_logfile( const char *fmt, ... )
{
  FILE *fp;
  va_list args;
  char *buf;
  time_t curr_time;

  va_start( args, fmt );
  buf = vstrdupf( fmt, args );
  va_end( args );

  printf( "%s\n", buf );

  fp = fopen( LOG_FILE, "a" );

  if ( !fp )
    fprintf( stderr, "Warning: Could not open " LOG_FILE " for appending\n" );
  else
  {
    time(&curr_time);

    fprintf( fp, "%s%s\n", ctime(&curr_time), buf );

    #ifndef NDEBUG
    fflush( fp );
    #endif

    fclose( fp );
  }

  free( buf );
}

void log_linenum( int linenum )
{
    log_stringf( "(Line %d)\n", linenum );
}

void error_messagef( const char *fmt, ... )
{
  char *buf;
  va_list args;

  va_start( args, fmt );
  buf = vstrdupf( fmt, args );
  va_end( args );

  error_message( buf );

  free( buf );
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

char *lowercaserize( const char *x )
{
  static char buf[MAX_STRING_LEN * 2];
  const char *xptr = x;
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
 * url_decode/url_encode (and from_hex/to_hex) courtesy of Fred Bulback (http://www.geekhideout.com/urlcode.shtml)
 */

/* Converts a hex character to its integer value */
char from_hex(char ch) {
  return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

char to_hex(char code) {
  static char hex[] = "0123456789abcdef";
  return hex[code & 15];
}

/* Returns a url-encoded version of str */
/* IMPORTANT: be sure to free() the returned string after use */
char *url_encode(char *str) {
  char *pstr = str, *buf = malloc(strlen(str) * 3 + 1), *pbuf = buf;
  while (*pstr) {
    if (isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
      *pbuf++ = *pstr;
    else if (*pstr == ' ')
      *pbuf++ = '+';
    else
      *pbuf++ = '%', *pbuf++ = to_hex(*pstr >> 4), *pbuf++ = to_hex(*pstr & 15);
    pstr++;
  }
  *pbuf = '\0';
  return buf;
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

size_t voidlen( void **x )
{
  void **ptr;

  for ( ptr = x; *ptr; ptr++ )
    ;

  return ptr - x;
}

char *constraints_comma_list( lyphplate **constraints )
{
  lyphplate **ptr;
  static char *buf;
  char **ids, **iptr, *bptr;
  int len = 0;

  if ( buf )
    free( buf );

  CREATE( ids, char *, VOIDLEN(constraints) + 1 );
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

int copy_file( char *dest_ch, char *src_ch )
{
  FILE *dest, *src;
  char c;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  src = fopen( src_ch, "r" );

  if ( !src )
    return 0;

  dest = fopen( dest_ch, "w" );

  if ( !dest )
  {
    fclose( src );
    return 0;
  }

  for (;;)
  {
    QUICK_GETC(c, src);

    if ( !c )
      break;

    fprintf( dest, "%c", c );
  }

  fclose( dest );
  fclose( src );
  return 1;
}

void **parse_list_worker( char *list, void * (*non_reentrant) (char *), void * (*reentrant) (char *, void *), void *data, char *name, char **err )
{
  void **buf, **bptr;
  char *left, *ptr;
  int commas = 0, fEnd = 0;

  for ( ptr = list; *ptr; ptr++ )
    if ( *ptr == ',' )
      commas++;

  CREATE( buf, void *, commas + 2 );
  bptr = buf;

  for ( ptr = list, left = list; ; ptr++ )
  {
    switch( *ptr )
    {
      case '\0':
        fEnd = 1;
      case ',':
        *ptr = '\0';

        if ( non_reentrant )
          *bptr = (*non_reentrant) (left);
        else
          *bptr = (*reentrant) (left, data);

        if ( !*bptr )
        {
          free( buf );

          if ( err )
          {
            if ( name )
              *err = strdupf( "There was no %s with id '%s' in the database", name, left );
            else
              *err = strdupf( "There was no match for '%s'", left );
          }

          return NULL;
        }

        bptr++;

        if ( fEnd )
        {
          *bptr = NULL;
          return buf;
        }

        left = &ptr[1];
      default:
        break;
    }
  }
}

void **parse_list( char *list, void * (*fnc) (char *), char *name, char **err )
{
  return parse_list_worker( list, fnc, NULL, NULL, name, err );
}

void **parse_list_r( char *list, void * (*fnc) (char *, void *), void *data, char *name, char **err )
{
  return parse_list_worker( list, NULL, fnc, data, name, err );
}


void maybe_update_top_id( int *top, const char *idstr )
{
  int id = strtol( idstr, NULL, 10 );

  if ( id > *top )
    *top = id;
}

char *loctype_to_str( int loctype )
{
  switch( loctype )
  {
    case LOCTYPE_INTERIOR:
      return "interior";
    case LOCTYPE_BORDER:
      return "border";
    case -1:
      return "none";
    default:
      return "unknown";
  }
}

void multifree( void *first, ... )
{
  va_list args;
  void *ptr;

  va_start( args, first );

  for ( ptr = va_arg( args, void * ); ptr; ptr = va_arg( args, void * ) )
    free( ptr );

  va_end( args );

  free( first );
}

int req_cmp( char *req, char *match )
{
  if ( *req == '/' )
    req++;

  if ( !strcmp( req, match ) )
    return 1;
  else
  {
    int len = strlen( req );

    if ( len && req[len-1] == '/' )
    {
      req[len-1] = '\0';

      if ( !strcmp( req, match ) )
      {
        req[len-1] = '/';
        return 1;
      }
      req[len-1] = '/';
    }
  }

  return 0;
}

void **blank_void_array( void )
{
  void **buf;

  CREATE( buf, void *, 1 );
  buf[0] = NULL;

  return buf;
}

void **copy_void_array( void **arr )
{
  void **buf;
  int size = VOIDLEN( arr );

  CREATE( buf, void *, size + 1 );
  memcpy( buf, arr, (size+1)*sizeof(void*) );

  return buf;
}

int cmp_possibly_null( const char *x, const char *y )
{
  int is_x_null = !x || !*x;
  int is_y_null = !y || !*y;

  if ( is_x_null && is_y_null )
    return 1;

  if ( (is_x_null && !is_y_null) || (!is_x_null && is_y_null) )
    return 0;

  return !strcmp( x, y );
}

int str_has_substring( const char *hay, const char *needle )
{
  const char *space;

  if ( str_begins( hay, needle ) )
    return 1;

  for ( space = hay; *space; space++ )
    if ( *space == ' ' && str_begins( space+1, needle ) )
      return 1;

  return 0;
}

char *ll_to_json( long long n )
{
  char buf[1024];

  sprintf( buf, "%lld", n );

  return str_to_json( buf );
}

char *ul_to_json( unsigned long n )
{
  char buf[1024];

  sprintf( buf, "%lu", n );

  return str_to_json( buf );
}

long long longtime( void )
{
  double d = difftime( time(NULL), 0 );

  return (long long) d;
}

int str_begins( const char *full, const char *init )
{
  const char *fptr = full, *iptr = init;

  for (;;)
  {
    if ( !*fptr && !*iptr )
      return 1;

    if ( !*fptr )
      return 0;

    if ( !*iptr )
      return 1;

    if ( LOWER( *fptr ) != LOWER( *iptr ) )
      return 0;

    fptr++;
    iptr++;
  }
}

char *trim_spaces( char *x )
{
  char *end;

  while ( *x == ' ' )
    x++;

  if ( !*x )
    return x;

  for ( end = &x[strlen(x)-1]; end > x; end-- )
    if ( *end )
      break;

  end[1] = '\0';

  return x;
}
