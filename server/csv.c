#include "lyph.h"
#include "srv.h"

#define MAX_DIGITS_OF_API_ARG 4

void free_csv_line( char **parsed )
{
  for ( ; *parsed; parsed++ )
    free( *parsed );

  free( parsed );
}

int count_fields( const char *line )
{
  const char *ptr;
  int cnt, fQuote;

  for ( cnt = 1, fQuote = 0, ptr = line; *ptr; ptr++ )
  {
    if ( fQuote )
    {
      if ( *ptr == '\"' )
      {
        if ( ptr[1] == '\"' )
        {
          ptr++;
          continue;
        }
        fQuote = 0;
      }
      continue;
    }

    switch( *ptr )
    {
      case '\"':
        fQuote = 1;
        continue;
      case ',':
        cnt++;
        continue;
      default:
        continue;
    }
  }

  if ( fQuote )
    return -1;

  return cnt;
}

char **parse_csv( const char *line, int *cnt )
{
  char **buf, **bptr, *tmp, *tptr;
  const char *ptr;
  int fieldcnt, fQuote, len, fEnd;

  fieldcnt = count_fields( line );

  if ( fieldcnt == -1 )
    return NULL;

  *cnt = fieldcnt;

  buf = malloc( sizeof(char*) * (fieldcnt+1) );

  if ( !buf )
    return NULL;

  len = strlen(line);

  tmp = malloc( len+1 );

  if ( !tmp )
  {
    free( buf );
    return NULL;
  }

  bptr = buf;

  for ( ptr = line, fQuote = 0, *tmp = '\0', tptr = tmp, fEnd = 0; ; ptr++ )
  {
    if ( fQuote )
    {
      if ( !*ptr )
        break;

      if ( *ptr == '\"' )
      {
        if ( ptr[1] == '\"' )
        {
          *tptr++ = '\"';
          ptr++;
          continue;
        }
        fQuote = 0;
      }
      else
        *tptr++ = *ptr;

      continue;
    }

    switch( *ptr )
    {
      case '\"':
        fQuote = 1;
        continue;
      case '\0':
        fEnd = 1;
      case ',':
        *tptr = '\0';
        *bptr = strdup( tmp );

        if ( !*bptr )
        {
          for ( bptr--; bptr >= buf; bptr-- )
            free( *bptr );

          free( buf );
          free( tmp );

          return NULL;
        }

        bptr++;
        tptr = tmp;

        if ( fEnd )
          break;
        else
          continue;

      default:
        *tptr++ = *ptr;
        continue;
    }

    if ( fEnd )
      break;
  }

  free( tmp );
  *bptr = NULL;
  return buf;
}

int get_max_arg( char *tmplt )
{
  char *tptr, *end, tmp;
  int max = 0, num;

  for ( tptr = tmplt; *tptr; tptr++ )
  {
    if ( *tptr != '$' )
      continue;

    if ( tptr[1] == '$' )
    {
      tptr++;
      continue;
    }

    if ( !isdigit(tptr[1]) )
      continue;

    for ( end = &tptr[2]; isdigit(*end); end++ )
      ;

    if ( end > &tptr[MAX_DIGITS_OF_API_ARG + 1] )
      return -1;

    tmp = *end;
    *end = '\0';
    num = strtoul( &tptr[1], NULL, 10 );
    *end = tmp;

    if ( num < 1 )
      return -1;

    if ( num > max )
      max = num;

    tptr = &end[-1];
  }

  return max;
}

int calc_len_of_applying_template( char *tmplt, char **fields )
{
  char *tptr, *end, tmp;
  int len = strlen(tmplt), key;

  for ( tptr = tmplt; *tptr; tptr++ )
  {
    if ( *tptr != '$' )
      continue;

    if ( tptr[1] == '$' )
    {
      tptr++;
      len -= 1;
      continue;
    }

    if ( !isdigit( tmplt[1] ) )
      continue;

    for ( end = &tptr[2]; isdigit(*end); end++ )
      ;

    tmp = *end;
    *end = '\0';
    key = strtoul( &tptr[1], NULL, 10 );

    len += strlen( fields[key-1] ) - strlen( tptr );

    *end = tmp;
    tptr = &end[-1];
  }

  return len;
}

char *apply_api_template( char *tmplt, char **fields )
{
  char *tptr, *buf, *bptr, *end, tmp;
  int key;
  int len = calc_len_of_applying_template( tmplt, fields );

  CREATE( buf, char, len + 1 );
  bptr = buf;

  for ( tptr = tmplt; *tptr; tptr++ )
  {
    if ( *tptr != '$' )
    {
      *bptr++ = *tptr;
      continue;
    }

    if ( tptr[1] == '$' )
    {
      *bptr++ = '$';
      tptr++;
      continue;
    }

    if ( !isdigit(tptr[1]) )
    {
      *bptr++ = '$';
      continue;
    }

    for ( end = &tptr[2]; isdigit(*end); end++ )
      ;

    tmp = *end;
    *end = '\0';
    key = strtoul( &tptr[1], NULL, 10 );
    *end = tmp;

    strcpy( bptr, fields[key-1] );
    bptr += strlen( fields[key-1] );

    tptr = &end[-1];
  }

  *bptr = '\0';

  return buf;
}

http_request *tmp_http_req( char *cmd )
{
  http_request *req;
  http_conn *conn;

  CREATE( req, http_request, 1 );
  req->next = NULL;
  req->prev = NULL;
  req->query = strdup( cmd );
  req->callback = NULL;
  req->dead = NULL;

  CREATE( conn, http_conn, 1 );
  conn->next = NULL;
  conn->prev = NULL;
  conn->req = req;
  conn->sock = -1;
  conn->state = -1;
  conn->idle = 0;

  CREATE( conn->buf, char, HTTP_INITIAL_INBUF_SIZE + 1 );
  *conn->buf = '\0';
  conn->bufsize = HTTP_INITIAL_INBUF_SIZE;
  conn->buflen = 0;

  CREATE( conn->outbuf, char, HTTP_INITIAL_OUTBUF_SIZE + 1 );
  *conn->outbuf = '\0';
  conn->outbuflen = 0;
  conn->outbufsize = HTTP_INITIAL_OUTBUF_SIZE;
  conn->writehead = NULL;
  conn->len = 0;

  req->conn = conn;

  return req;
}

void free_tmp_req( http_request *req )
{
  free( req->query );
  free( req->conn->buf );
  free( req->conn->outbuf );
  free( req->conn );
  free( req );
}

char *run_one_api_cmd( char *cmd )
{
  http_request *req = tmp_http_req( cmd );

  handle_request( req, req->query );

  free_tmp_req( req );

  return NULL;
}

char *run_api_cmds( str_wrapper *head )
{
  str_wrapper *w;
  char *err;

  for ( w = head; w; w = w->next )
  {
    err = run_one_api_cmd( w->str );

    if ( err )
      return err;
  }

  return strdup( JSON1( "Response": "OK" ) );
}

HANDLER( do_parse_csv )
{
  str_wrapper *head = NULL, *tail = NULL, *w;
  char *filename, *with_dir, *csv, *csvptr, *tmplt, *line, **fields, *modline, *response;
  int fEnd = 0, max_arg, fieldcnt, linenum;
  static int depth = 0;

  if ( depth )
    HND_ERR( "Parse_csv is not allowed to call itself recursively" );

  depth++;

  if ( !(filename = get_param( params, "filename" )) )
  {
    depth--;
    HND_ERR( "You did not specify a CSV filename" );
  }

  if ( strstr( filename, "/" ) )
  {
    depth--;
    HND_ERR( "The '/' character is not allowed in a parse_csv filename" );
  }

  if ( strstr( filename, ".." ) )
  {
    depth--;
    HND_ERR( "The substring '..' is not allowed in a parse_csv filename" );
  }

  if ( !(tmplt = get_param( params, "cmd" )) )
  {
    depth--;
    HND_ERR( "You did not specify a 'template' to use on the lines of the CSV file" );
  }

  max_arg = get_max_arg( tmplt );

  with_dir = strdupf( "%s%s", PARSE_CSV_DIR, filename );

  csv = load_file( with_dir );
  free( with_dir );

  if ( !csv )
  {
    depth--;
    HND_ERR( "Could not read the indicated filename" );
  }

  for ( linenum = 0, line = csv, csvptr = csv; ; csvptr++ )
  {
    char tmp;

    switch( *csvptr )
    {
      default:
        continue;

      case '\0':
        fEnd = 1;
      case '\n':
        tmp = *csvptr;
        *csvptr = '\0';
        linenum++;

        if ( *line )
        {
          fields = parse_csv( line, &fieldcnt );

          if ( !fields )
          {
            HND_ERRF_NORETURN( "Line %d of file: could not parse the CSV", linenum );
            goto handle_parse_csv_request_cleanup;
          }

          if ( fieldcnt < max_arg )
          {
            HND_ERRF_NORETURN( "Template uses field %d, but line %d of file has only %d field%s", max_arg, linenum, fieldcnt, fieldcnt==1 ? "" : "s" );
            goto handle_parse_csv_request_cleanup;
          }

          modline = apply_api_template( tmplt, fields );

          if ( strlen( modline ) > MAX_API_TEMPLATE_LEN-1 )
          {
            HND_ERRF_NORETURN( "After applying the template to line %d of the file, the resulting API command is too long (length limit: %d)", linenum, MAX_API_TEMPLATE_LEN-1 );
            free( modline );
            goto handle_parse_csv_request_cleanup;
          }

          CREATE( w, str_wrapper, 1 );
          w->str = modline;
          LINK2( w, head, tail, next, prev );
        }

        if ( fEnd )
          goto handle_parse_csv_request_escape;
        else
        {
          *csvptr = tmp;

          while ( csvptr[1] == '\r' || csvptr[1] == '\n' )
            csvptr++;

          line = &csvptr[1];
        }
        break;
    }
  }

  handle_parse_csv_request_escape:

  if ( !head )
    HND_ERR( "No API commands were generated by the file (is the file blank?)" );
  else
  {
    response = run_api_cmds( head );
    send_response( req, response );
    free( response );
  }

  handle_parse_csv_request_cleanup:
  {
    str_wrapper *w_next;

    for ( w = head; w; w = w_next )
    {
      w_next = w->next;

      free( w->str );
      free( w );
    }

    depth--;
  }
}

HANDLER( do_niflyph )
{
  lyph *e;
  char *fmastr, *ptr, buf[1024], *speciesstr;

  TRY_PARAM( fmastr, "fma", "You didn't specify an fma" );
  TRY_PARAM( speciesstr, "species", "You didn't specify a species" );

  for ( ptr = fmastr; *ptr; ptr++ )
    if ( *ptr == ':' )
      break;

  if ( !*ptr )
  {
    printf( "niflyph: no colon found: %s\n", fmastr );
    HND_ERR( "No colon found" );
  }

  sprintf( buf, "%s FMA_%s", speciesstr, &ptr[1] );

  e = lyph_by_name( buf );

  if ( e )
  {
    send_response( req, lyph_to_json( e ) );
    return;
  }

  e = make_lyph( 0, make_lyphnode(), make_lyphnode(), NULL, &ptr[1], buf, speciesstr );

  send_response( req, lyph_to_json( e ) );
}

HANDLER( do_nifconnection )
{
  lyph *fromlyph, *tolyph, *e;
  lyphnode *from, *to;
  char *fromstr, *tostr, *speciesstr, *ptr, frombuf[2048], tobuf[2048], buf[10000];

  TRY_PARAM( fromstr, "from", "No 'from' specified" );
  TRY_PARAM( tostr, "to", "No 'to' specified" );
  TRY_PARAM( speciesstr, "species", "No 'species' specified" );

  for ( ptr = fromstr; *ptr; ptr++ )
    if ( *ptr == ':' )
      break;

  if ( !*ptr )
  {
    printf( "nifconnection: no colon: [%s]\n", fromstr );
    HND_ERR( "An error occurred processing the 'from' parameter" );
  }

  sprintf( frombuf, "%s FMA_%s", speciesstr, &ptr[1] );

  for ( ptr = tostr; *ptr; ptr++ )
    if ( *ptr == ':' )
      break;

  if ( !*ptr )
  {
    printf( "nifconnection: no colon: [%s]\n", tostr );
    HND_ERR( "An error occurred processing the 'to' parameter" );
  }

  sprintf( tobuf, "%s FMA_%s", speciesstr, &ptr[1] );

  fromlyph = lyph_by_name( frombuf );
  tolyph = lyph_by_name( tobuf );

  if ( !fromlyph )
    HND_ERRF( "'from' lyph [%s] unrecognized", frombuf );

  if ( !tolyph )
    HND_ERR( "'to' lyph unrecognized" );

  from = make_lyphnode();
  to = make_lyphnode();

  from->location = fromlyph;
  from->loctype = LOCTYPE_INTERIOR;

  to->location = tolyph;
  to->loctype = LOCTYPE_INTERIOR;

  sprintf( buf, "Connection from [%s] ", trie_to_static(fromlyph->name) );
  sprintf( buf + strlen(buf), "to [%s]", trie_to_static(tolyph->name) );

  e = make_lyph( 0, from, to, NULL, NULL, buf, speciesstr );

  send_response( req, lyph_to_json( e ) );
}
