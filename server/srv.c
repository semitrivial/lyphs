#include "lyph.h"
#include "srv.h"

char *html;
char *js;
char *lyphgui_html;
char *lyphgui_js;

extern int lyphnode_to_json_flags;

int main( int argc, const char* argv[] )
{
  FILE *fp;
  int i = 1, port=5052;

  if ( argc < 2 )
  {
    main_syntax_error:

    printf( "Syntax: lyph <file>\nWhere <file> is the location of an N-Triples file containing the rdf:labels (possibly generated by the Converter)\n\n" );
    printf( "Or:     lyph -p <port> <file>\nWhere <port> is the port number to listen on\n\n" );
    return 0;
  }

  if ( !strcmp( argv[1], "-p" ) || !strcmp( argv[1], "-port" ) )
  {
    if ( argc < 4 || !is_number( argv[2] ) )
      goto main_syntax_error;

    port = strtoul( argv[2], NULL, 10 );
    i = 3;
  }

  fp = fopen( argv[i], "r" );

  if ( !fp )
  {
    fprintf( stderr, "Could not open file %s for reading\n\n", argv[i] );
    return 0;
  }

  init_labels(fp);
  fclose(fp);

  init_lyph_http_server(port);

  printf( "Ready.\n" );

  while(1)
  {
    /*
     * To do: make this commandline-configurable
     */
    usleep(10000);
    main_loop();
  }
}

void init_lyph_http_server( int port )
{
  int status, yes=1;
  struct addrinfo hints, *servinfo;
  char portstr[128];

  html = load_file( "gui.html" );
  js = load_file( "gui.js" );

  lyphgui_html = load_file( "lyphgui.html" );
  lyphgui_js = load_file( "lyphgui.js" );

  memset( &hints, 0, sizeof(hints) );
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  sprintf( portstr, "%d", port );

  if ( (status=getaddrinfo(NULL,portstr,&hints,&servinfo)) != 0 )
  {
    fprintf( stderr, "Fatal: Couldn't open server port (getaddrinfo failed)\n" );
    abort();
  }

  srvsock = socket( servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol );

  if ( srvsock == -1 )
  {
    fprintf( stderr, "Fatal: Couldn't open server port (socket failed)\n" );
    abort();
  }

  if ( setsockopt( srvsock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int) ) == -1 )
  {
    fprintf( stderr, "Fatal: Couldn't open server port (setsockopt failed)\n" );
    abort();
  }

  if ( bind( srvsock, servinfo->ai_addr, servinfo->ai_addrlen ) == -1 )
  {
    fprintf( stderr, "Fatal: Couldn't open server port (bind failed)\n" );
    abort();
  }

  if ( listen( srvsock, HTTP_LISTEN_BACKLOG ) == -1 )
  {
    fprintf( stderr, "Fatal: Couldn't open server port (listen failed)\n" );
    abort();
  }

  freeaddrinfo( servinfo );

  return;
}

void main_loop( void )
{
  http_request *req;
  int count=0;

  while ( count < 10 )
  {
    req = http_recv();

    if ( req )
    {
      char *reqptr, *reqtype, *request, repl[MAX_STRING_LEN], *rptr;
      const char *parse_params_err;
      trie **data;
      int len=0, fFirst=0, fShortIRI=0, fCaseInsens=0;
      url_param *params[MAX_URL_PARAMS+1];

      count++;

      if ( !strcmp( req->query, "gui" )
      ||   !strcmp( req->query, "/gui" )
      ||   !strcmp( req->query, "gui/" )
      ||   !strcmp( req->query, "/gui/" ) )
      {
        send_gui( req );
        continue;
      }

      if ( !strcmp( req->query, "lyphgui" )
      ||   !strcmp( req->query, "/lyphgui" )
      ||   !strcmp( req->query, "lyphgui/" )
      ||   !strcmp( req->query, "/lyphgui/" ) )
      {
        send_lyphgui( req );
        continue;
      }

      if ( !strcmp( req->query, "js/" )
      ||   !strcmp( req->query, "/js/" ) )
      {
        send_js( req );
        continue;
      }

      if ( !strcmp( req->query, "lyphjs/" )
      ||   !strcmp( req->query, "/lyphjs/" ) )
      {
        send_lyphjs( req );
        continue;
      }

      for ( reqptr = (*req->query == '/') ? req->query + 1 : req->query; *reqptr; reqptr++ )
        if ( *reqptr == '/' )
          break;

      if ( !*reqptr )
      {
        send_400_response( req );
        continue;
      }

      *reqptr = '\0';
      reqtype = (*req->query == '/') ? req->query + 1 : req->query;

      parse_params_err = parse_params( &reqptr[1], &fShortIRI, &fCaseInsens, req, params );

      if ( parse_params_err )
      {
        char errmsg[1024];

        sprintf( errmsg, "{\"Error\": \"%s\"}", parse_params_err );

        send_200_response( req, errmsg );
        free_url_params( params );
        continue;
      }

      request = url_decode(&reqptr[1]);

      if ( !strcmp( reqtype, "all_lyphs" ) )
      {
        handle_all_lyphs_request( req );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "all_lyphnodes" ) )
      {
        handle_all_lyphnodes_request( req );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "all_lyphedges" ) )
      {
        handle_all_lyphedges_request( req );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "all_lyphviews" ) )
      {
        handle_all_lyphviews_request( req );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "lyph_hierarchy" ) )
      {
        handle_lyph_hierarchy_request( req );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "assignlyph" ) )
      {
        handle_assignlyph_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "edgeconstrain" ) )
      {
        handle_edgeconstrain_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "makelyph" ) )
      {
        handle_makelyph_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "makelayer" ) )
      {
        handle_makelayer_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "makelyphedge" ) )
      {
        handle_makelyphedge_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "makelyphnode" ) )
      {
        handle_makelyphnode_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "makeview" ) )
      {
        handle_makeview_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      if ( !strcmp( reqtype, "lyphpath" ) )
      {
        handle_lyphpath_request( req, params );
        free( request );
        free_url_params( params );
        continue;
      }

      free_url_params( params );

      if ( !strcmp( reqtype, "uclsyntax" )
      ||   !strcmp( reqtype, "ucl_syntax" )
      ||   !strcmp( reqtype, "ucl-syntax" ) )
      {
        handle_ucl_syntax_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "lyph" ) )
      {
        handle_lyph_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "layer" ) )
      {
        handle_layer_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "lyphedge" ) )
      {
        handle_lyphedge_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "lyphnode" ) )
      {
        handle_lyphnode_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "lyphview" ) )
      {
        handle_lyphview_request( request, req );
        free( request );
        continue;
      }

      if ( !strcmp( reqtype, "iri" ) )
        data = get_labels_by_iri( request );
      else if ( !strcmp( reqtype, "label" ) && !fCaseInsens )
        data = get_iris_by_label( request );
      else if ( !strcmp( reqtype, "label" )
           ||   !strcmp( reqtype, "label-case-insensitive" ) )
        data = get_iris_by_label_case_insensitive( request );
      else if ( !strcmp( reqtype, "label-shortiri" ) )
      {
        data = get_iris_by_label( request );
        fShortIRI = 1;
      }
      else if ( !strcmp( reqtype, "label-shortiri-case-insensitive" ) || !strcmp( reqtype, "label-case-insensitive-shortiri" ) )
      {
        data = get_iris_by_label_case_insensitive( request );
        fShortIRI = 1;
      }
      else if ( !strcmp( reqtype, "autocomp" ) || !strcmp( reqtype, "autocomplete" ) )
        data = get_autocomplete_labels( request, 0 );
      else if ( !strcmp( reqtype, "autocomp-case-insensitive" ) || !strcmp( reqtype, "autocomplete-case-insensitive" ) )
        data = get_autocomplete_labels( request, 1 );
      else
      {
        *reqptr = '/';
        free( request );
        send_400_response( req );
        continue;
      }

      if ( !data )
      {
        *reqptr = '/';
        free( request );
        send_200_response( req, "{\"Results\": []}" );
        continue;
      }

      sprintf( repl, "{\"Results\": [" );
      rptr = &repl[strlen( "{\"Results\": [")];

      for ( ; *data; data++ )
      {
        char *datum, *encoded;
        int datumlen;

        if ( fShortIRI )
        {
          char *longform = trie_to_static( *data );
          datum = get_url_shortform( longform );
          if ( !datum )
            datum = longform;
        }
        else
          datum = trie_to_static( *data );

        encoded = html_encode( datum );

        datumlen = strlen( encoded ) + strlen("\"\"") + (fFirst ? strlen(",") : 0);

        if ( len + datumlen >= MAX_STRING_LEN - 10 )
        {
          sprintf( rptr, ",\"...\"" );
          free( encoded );
          break;
        }
        len += datumlen;
        sprintf( rptr, "%s\"%s\"", fFirst ? "," : "", encoded );
        rptr = &rptr[datumlen];
        free( encoded );
        fFirst = 1;
      }
      sprintf( rptr, "]}" );

      send_200_response( req, repl );
      *reqptr = '/';
      free( request );
    }
    else
      break;
  }

  json_gc();
}

void http_update_connections( void )
{
  static struct timeval zero_time;
  http_conn *c, *c_next;
  int top_desc;

  FD_ZERO( &http_inset );
  FD_ZERO( &http_outset );
  FD_ZERO( &http_excset );
  FD_SET( srvsock, &http_inset );
  top_desc = srvsock;

  for ( c = first_http_conn; c; c = c->next )
  {
    if ( c->sock > top_desc )
      top_desc = c->sock;

    if ( c->state == HTTP_SOCKSTATE_READING_REQUEST )
      FD_SET( c->sock, &http_inset );
    else
      FD_SET( c->sock, &http_outset );

    FD_SET( c->sock, &http_excset );
  }

  /*
   * Poll sockets
   */
  if ( select( top_desc+1, &http_inset, &http_outset, &http_excset, &zero_time ) < 0 )
  {
    fprintf( stderr, "Fatal: select failed to poll the sockets\n" );
    abort();
  }

  if ( !FD_ISSET( srvsock, &http_excset ) && FD_ISSET( srvsock, &http_inset ) )
    http_answer_the_phone( srvsock );

  for ( c = first_http_conn; c; c = c_next )
  {
    c_next = c->next;

    c->idle++;

    if ( FD_ISSET( c->sock, &http_excset )
    ||   c->idle > HTTP_KICK_IDLE_AFTER_X_SECS * HTTP_PULSES_PER_SEC )
    {
      FD_CLR( c->sock, &http_inset );
      FD_CLR( c->sock, &http_outset );

      http_kill_socket( c );
      continue;
    }

    if ( c->state == HTTP_SOCKSTATE_AWAITING_INSTRUCTIONS )
      continue;

    if ( c->state == HTTP_SOCKSTATE_READING_REQUEST
    &&   FD_ISSET( c->sock, &http_inset ) )
    {
      c->idle = 0;
      http_listen_to_request( c );
    }
    else
    if ( c->state == HTTP_SOCKSTATE_WRITING_RESPONSE
    &&   c->outbuflen > 0
    &&   FD_ISSET( c->sock, &http_outset ) )
    {
      c->idle = 0;
      http_flush_response( c );
    }
  }
}

void http_kill_socket( http_conn *c )
{
  UNLINK2( c, first_http_conn, last_http_conn, next, prev );

  free( c->buf );
  free( c->outbuf );
  free_http_request( c->req );

  close( c->sock );

  free( c );
}

void free_http_request( http_request *r )
{
  if ( !r )
    return;

  if ( r->dead )
    *r->dead = 1;

  UNLINK2( r, first_http_req, last_http_req, next, prev );

  if ( r->query )
    free( r->query );

  if ( r->callback )
    free( r->callback );

  free( r );
}

void http_answer_the_phone( int srvsock )
{
  struct sockaddr_storage their_addr;
  socklen_t addr_size = sizeof(their_addr);
  int caller;
  http_conn *c;
  http_request *req;

  if ( ( caller = accept( srvsock, (struct sockaddr *) &their_addr, &addr_size ) ) < 0 )
    return;

  if ( ( fcntl( caller, F_SETFL, FNDELAY ) ) == -1 )
  {
    close( caller );
    return;
  }

  CREATE( c, http_conn, 1 );
  c->next = NULL;
  c->sock = caller;
  c->idle = 0;
  c->state = HTTP_SOCKSTATE_READING_REQUEST;
  c->writehead = NULL;

  CREATE( c->buf, char, HTTP_INITIAL_INBUF_SIZE + 1 );
  c->bufsize = HTTP_INITIAL_INBUF_SIZE;
  c->buflen = 0;
  *c->buf = '\0';

  CREATE( c->outbuf, char, HTTP_INITIAL_OUTBUF_SIZE + 1 );
  c->outbufsize = HTTP_INITIAL_OUTBUF_SIZE + 1;
  c->outbuflen = 0;
  *c->outbuf = '\0';

  CREATE( req, http_request, 1 );
  req->next = NULL;
  req->conn = c;
  req->query = NULL;
  req->callback = NULL;
  c->req = req;

  LINK2( req, first_http_req, last_http_req, next, prev );
  LINK2( c, first_http_conn, last_http_conn, next, prev );
}

int resize_buffer( http_conn *c, char **buf )
{
  int max, *size;
  char *tmp;

  if ( *buf == c->buf )
  {
    max = HTTP_MAX_INBUF_SIZE;
    size = &c->bufsize;
  }
  else
  {
    max = HTTP_MAX_OUTBUF_SIZE;
    size = &c->outbufsize;
  }

  *size *= 2;
  if ( *size >= max )
  {
    http_kill_socket(c);
    return 0;
  }

  CREATE( tmp, char, (*size)+1 );

  sprintf( tmp, "%s", *buf );
  free( *buf );
  *buf = tmp;
  return 1;
}

void http_listen_to_request( http_conn *c )
{
  int start = c->buflen, readsize;

  if ( start >= c->bufsize - 5
  &&  !resize_buffer( c, &c->buf ) )
    return;

  readsize = recv( c->sock, c->buf + start, c->bufsize - 5 - start, 0 );

  if ( readsize > 0 )
  {
    c->buflen += readsize;
    http_parse_input( c );
    return;
  }

  if ( readsize == 0 || errno != EWOULDBLOCK )
    http_kill_socket( c );
}

void http_flush_response( http_conn *c )
{
  int sent_amount;

  if ( !c->writehead )
    c->writehead = c->outbuf;

  sent_amount = send( c->sock, c->writehead, c->outbuflen, 0 );

  if ( sent_amount >= c->outbuflen )
  {
    http_kill_socket( c );
    return;
  }

  c->outbuflen -= sent_amount;
  c->writehead = &c->writehead[sent_amount];
}

void http_parse_input( http_conn *c )
{
  char *bptr, *end, query[MAX_STRING_LEN], *qptr;
  int spaces = 0, chars = 0;

  end = &c->buf[c->buflen];

  for ( bptr = c->buf; bptr < end; bptr++ )
  {
    switch( *bptr )
    {
      case ' ':
        spaces++;

        if ( spaces == 2 )
        {
          *qptr = '\0';
          c->req->query = strdup( query );
          c->state = HTTP_SOCKSTATE_AWAITING_INSTRUCTIONS;
          return;
        }
        else
        {
          *query = '\0';
          qptr = query;
        }

        break;

      case '\0':
      case '\n':
      case '\r':
        http_kill_socket( c );
        return;

      default:
        if ( spaces != 0 )
        {
          if ( ++chars >= MAX_STRING_LEN - 10 )
          {
            http_kill_socket( c );
            return;
          }
          *qptr++ = *bptr;
        }
        break;
    }
  }
}

http_request *http_recv( void )
{
  http_request *req, *best = NULL;
  int max_idle = -1;

  http_update_connections();

  for ( req = first_http_req; req; req = req->next )
  {
    if ( req->conn->state == HTTP_SOCKSTATE_AWAITING_INSTRUCTIONS
    &&   req->conn->idle > max_idle )
    {
      max_idle = req->conn->idle;
      best = req;
    }
  }

  if ( best )
  {
    best->conn->state = HTTP_SOCKSTATE_WRITING_RESPONSE;
    return best;
  }

  return NULL;
}

void http_write( http_request *req, char *txt )
{
  http_send( req, txt, strlen(txt) );
}

/*
 * Warning: can crash if txt is huge.  Up to caller
 * to ensure txt is not too huge.
 */
void http_send( http_request *req, char *txt, int len )
{
  http_conn *c = req->conn;

  if ( len >= c->outbufsize - 5 )
  {
    char *newbuf;

    CREATE( newbuf, char, len+10 );
    memcpy( newbuf, txt, len+1 );
    free( c->outbuf );
    c->outbuf = newbuf;
    c->outbuflen = len;
    c->outbufsize = len+10;
  }
  else
  {
    memcpy( c->outbuf, txt, len );
    c->outbuflen = len;
  }
}

void send_400_response( http_request *req )
{
  char buf[MAX_STRING_LEN];

  sprintf( buf, "HTTP/1.1 400 Bad Request\r\n"
                "Date: %s\r\n"
                "Content-Type: text/plain; charset=utf-8\r\n"
                "%s"
                "Content-Length: %zd\r\n"
                "\r\n"
                "Syntax Error",
                current_date(),
                nocache_headers(),
                strlen( "Syntax Error" ) );

  http_write( req, buf );
}

void send_200_response( http_request *req, char *txt )
{
  send_200_with_type( req, txt, "application/json" );
}

void send_200_with_type( http_request *req, char *txt, char *type )
{
  char *buf;

  /*
   * JSONP support
   */
  if ( req->callback )
  {
    char *jsonp = malloc( strlen(txt) + strlen(req->callback) + strlen( "(\n\n);" ) + 1 );

    sprintf( jsonp, "%s(\n%s\n);", req->callback, txt );
    txt = jsonp;
  }

  buf = strdupf(  "HTTP/1.1 200 OK\r\n"
                  "Date: %s\r\n"
                  "Content-Type: %s; charset=utf-8\r\n"
                  "%s"
                  "Content-Length: %zd\r\n"
                  "\r\n"
                  "%s",
                  current_date(),
                  type,
                  nocache_headers(),
                  strlen(txt),
                  txt );

  if ( req->callback )
    free( txt );

  http_write( req, buf );

  free( buf );
}

char *nocache_headers(void)
{
  return "Cache-Control: no-cache, no-store, must-revalidate\r\n"
         "Pragma: no-cache\r\n"
         "Expires: 0\r\n";
}

char *current_date(void)
{
  time_t rawtime;
  struct tm *timeinfo;
  static char buf[2048];
  char *bptr;

  time ( &rawtime );
  timeinfo = localtime( &rawtime );
  sprintf( buf, "%s", asctime ( timeinfo ) );

  for ( bptr = &buf[strlen(buf)-1]; *bptr == '\n' || *bptr == '\r'; bptr-- )
    ;

  bptr[1] = '\0';

  return buf;
}

void send_gui( http_request *req )
{
  send_200_with_type( req, html, "text/html" );
}

void send_js( http_request *req )
{
  send_200_with_type( req, js, "application/javascript" );
}

void send_lyphgui( http_request *req )
{
  send_200_with_type( req, lyphgui_html, "text/html" );
}

void send_lyphjs( http_request *req )
{
  send_200_with_type( req, lyphgui_js, "application/javascript" );
}

char *load_file( char *filename )
{
  FILE *fp;
  char *buf, *bptr;
  int size;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( filename, "r" );

  if ( !fp )
  {
    fprintf( stderr, "Fatal: Couldn't open %s for reading\n", filename );
    abort();
  }

  fseek(fp, 0L, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  CREATE( buf, char, size+1 );

  for ( bptr = buf; ; bptr++ )
  {
    QUICK_GETC(*bptr,fp);

    if ( !*bptr )
      return buf;
  }
}

const char *parse_params( char *buf, int *fShortIRI, int *fCaseInsens, http_request *req, url_param **params )
{
  char *bptr;
  char *param;
  int fEnd, cnt=0;
  url_param **pptr = params;

  for ( bptr = buf; *bptr; bptr++ )
    if ( *bptr == '?' )
      break;

  if ( !*bptr )
  {
    *params = NULL;
    return NULL;
  }

  *bptr++ = '\0';
  param = bptr;
  fEnd = 0;

  for (;;)
  {
    if ( *bptr == '&' || *bptr == '\0' )
    {
      if ( *bptr == '\0' )
        fEnd = 1;
      else
        *bptr = '\0';

      if ( ++cnt >= MAX_URL_PARAMS )
      {
        *pptr = NULL;
        return "Too many URL parameters";
      }

      if ( !strcmp( param, "case-insensitive" )
      ||   !strcmp( param, "case-ins" )
      ||   !strcmp( param, "insensitive" )
      ||   !strcmp( param, "ins" ) )
        *fCaseInsens = 1;
      else
      if ( !strcmp( param, "short-iri" )
      ||   !strcmp( param, "short" ) )
        *fShortIRI = 1;
      else
      {
        char *equals;

        for ( equals = param; *equals; equals++ )
          if ( *equals == '=' )
            break;

        if ( *equals )
        {
          *equals = '\0';

          if ( strlen( param ) >= MAX_URL_PARAM_LEN )
          {
            *pptr = NULL;
            return "Url parameter too long";
          }

          CREATE( *pptr, url_param, 1 );
          (*pptr)->key = url_decode( param );
          (*pptr)->val = url_decode( &equals[1] );

          if ( !strcmp( (*pptr)->key, "callback" ) )
          {
            if ( req->callback )
              free( req->callback );

            req->callback = strdup( (*pptr)->val );
          }

          pptr++;
        }
      }

      if ( fEnd )
      {
        *pptr = NULL;
        return NULL;
      }

      param = &bptr[1];
    }

    bptr++;
  }
}

void handle_makelyphnode_request( http_request *req, url_param **params )
{
  lyphnode *n;

  n = make_lyphnode();

  if ( !n )
    HND_ERR( "Could not create new lyphnode (out of memory?)" );

  lyphnode_to_json_flags = LTJ_EXITS;

  send_200_response( req, lyphnode_to_json( n ) );

  lyphnode_to_json_flags = 0;
}

void handle_makelyphedge_request( http_request *req, url_param **params )
{
  lyphnode *from, *to;
  lyphedge *e;
  lyph *L;
  char *fmastr, *fromstr, *tostr, *namestr, *lyphstr, *typestr;
  int type;

  typestr = get_url_param( params, "type" );

  if ( !typestr )
    HND_ERR( "You did not specify a type" );

  type = strtol( typestr, NULL, 10 );

  if ( type < 1 || type > 4 )
    HND_ERR( "Type must be 1, 2, 3, or 4" );

  fmastr = get_url_param( params, "fma" );

  fromstr = get_url_param( params, "from" );
  tostr = get_url_param( params, "to" );

  if ( !fromstr || !tostr )
    HND_ERR( "An edge requires a 'from' and a 'to' field" );

  from = lyphnode_by_id_or_new( fromstr );

  if ( !from )
    HND_ERR( "The indicated 'from' node was not found" );

  to = lyphnode_by_id_or_new( tostr );

  if ( !to )
    HND_ERR( "The indicated 'to' node was not found" );

  namestr = get_url_param( params, "name" );

  lyphstr = get_url_param( params, "lyph" );

  if ( lyphstr )
  {
    L = lyph_by_id( lyphstr );

    if ( !L )
      HND_ERR( "The indicated lyph was not found" );
  }
  else
    L = NULL;

  e = make_lyphedge( type, from, to, L, fmastr, namestr );

  if ( !e )
    HND_ERR( "The lyphedge could not be created (out of memory?)" );

  send_200_response( req, lyphedge_to_json( e ) );
}

void handle_makeview_request( http_request *req, url_param **params )
{
  lyphnode **nodes, **nptr;
  char **coords, **cptr, key[1024];
  url_param **p;
  int param_cnt, i;
  lyphview *v;

  for ( p = params; *p; p++ )
    ;

  param_cnt = p - params;

  if ( !param_cnt )
    HND_ERR( "You did not specify the nodes and their coordinates" );

  CREATE( nodes, lyphnode *, param_cnt + 1 );
  CREATE( coords, char *, param_cnt + 1 );
  nptr = nodes;
  cptr = coords;

  for ( i = 1; i <= param_cnt; i++ )
  {
    char *nodeid, *x, *y;
    lyphnode *node;

    sprintf( key, "node%d", i );
    nodeid = get_url_param( params, key );

    if ( !nodeid )
      break;

    sprintf( key, "x%d", i );
    x = get_url_param( params, key );

    if ( !x )
    {
      handle_makeview_request_unspecd_coords:

      free( nodes );
      free( coords );
      HND_ERR( "You did not specify x- and y-coordinates for all the indicated nodes" );
    }

    sprintf( key, "y%d", i );
    y = get_url_param( params, key );

    if ( !y )
      goto handle_makeview_request_unspecd_coords;

    node = lyphnode_by_id( nodeid );

    if ( !node )
    {
      char *errbuf = malloc( strlen(nodeid) + 1024 );
      sprintf( errbuf, "{\"Error\": \"Node '%s' was not found in the database\"}", nodeid );
      HND_ERR_NORETURN( errbuf );
      free( errbuf );
      free( nodes );
      free( coords );
      return;
    }

    *nptr++ = node;
    cptr[0] = x;
    cptr[1] = y;
    cptr = &cptr[2];
  }

  *cptr = NULL;
  *nptr = NULL;

  v = search_duplicate_view( nodes, coords );

  if ( v )
  {
    free( nodes );
    free( coords );

    send_200_response( req, lyphview_to_json( v ) );
    return;
  }

  v = create_new_view( nodes, coords );

  if ( !v )
    HND_ERR( "Could not create the view (out of memory?)" );
  else
    send_200_response( req, lyphview_to_json( v ) );

  free( coords );
  free( nodes );
}

void handle_makelayer_request( http_request *req, url_param **params )
{
  char *mtid, *thickstr;
  int thickness;
  layer *lyr;

  mtid = get_url_param( params, "material" );

  if ( !mtid )
    HND_ERR( "No material specified for layer" );

  thickstr = get_url_param( params, "thickness" );

  if ( thickstr )
    thickness = strtol( thickstr, NULL, 10 );
  else
    thickness = -1;

  lyr = layer_by_description( mtid, thickness );

  if ( !lyr )
    HND_ERR( "Invalid material id specified for layer" );

  send_200_response( req, layer_to_json( lyr ) );
}

void handle_edgeconstrain_request( http_request *req, url_param **params )
{
  lyphedge *e;
  lyph *L, **c;
  char *edgeid, *lyphid;
  int cnt;

  edgeid = get_url_param( params, "edge" );
  lyphid = get_url_param( params, "lyph" );

  if ( !edgeid )
    HND_ERR( "You did not specify an edge." );

  if ( !lyphid )
    HND_ERR( "You did not specify a lyph." );

  e = lyphedge_by_id( edgeid );

  if ( !e )
    HND_ERR( "The database does not contain an edge with that ID." );

  L = lyph_by_id( lyphid );

  if ( !L )
    HND_ERR( "The database does not contain a lyph with that ID." );

  for ( c = e->constraints; *c; c++ )
    if ( *c == L )
      HND_ERR( "The edge in question already has the constraint in question." );

  cnt = voidlen( (void**)e->constraints );
  CREATE( c, lyph *, cnt + 2 );

  memcpy( c, e->constraints, cnt * sizeof(lyph*) );

  c[cnt] = L;
  c[cnt+1] = NULL;

  free( e->constraints );
  e->constraints = c;

  save_lyphedges();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void handle_assignlyph_request( http_request *req, url_param **params )
{
  lyphedge *e;
  lyph *L;
  char *edgeid, *lyphid, *err;

  edgeid = get_url_param( params, "edge" );

  if ( !edgeid )
    HND_ERR( "You did not specify an edge." );

  e = lyphedge_by_id( edgeid );

  if ( !e )
    HND_ERR( "The database has no edge with that ID." );

  lyphid = get_url_param( params, "lyph" );

  if ( !lyphid )
    HND_ERR( "You did not specify a lyph." );

  L = lyph_by_id( lyphid );

  if ( !L )
    HND_ERR( "The database has no edge with that ID." );

  if ( !can_assign_lyph_to_edge( L, e, &err ) )
  {
    if ( err )
    {
      HND_ERR( err );
      free( err );
    }
    else
      HND_ERR( "That lyph cannot be assigned to that edge." );

    return;
  }

  e->lyph = L;

  save_lyphedges();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void handle_makelyph_request( http_request *req, url_param **params )
{
  char *name, *typestr;
  int type, lcnt;
  static layer **lyrs;
  layer **lptr;
  lyph *L;

  if ( !lyrs )
    CREATE( lyrs, layer *, MAX_URL_PARAMS + 2 );

  name = get_url_param( params, "name" );

  if ( !name )
    HND_ERR( "No name specified for lyph" );

  typestr = get_url_param( params, "type" );

  if ( !typestr )
    HND_ERR( "No type specified for lyph" );

  type = parse_lyph_type( typestr );

  if ( type == -1 )
    HND_ERR( "Invalid type specified for lyph" );

  if ( type == LYPH_BASIC )
    HND_ERR( "Only lyph types 'mix' and 'shell' are created by makelyph" );

  lcnt = 1;
  lptr = lyrs;

  for(;;)
  {
    char pmname[MAX_URL_PARAM_LEN+128];
    char *lyrid;
    layer *lyr;

    sprintf( pmname, "layer%d", lcnt );

    lyrid = get_url_param( params, pmname );

    if ( !lyrid )
      break;

    lyr = layer_by_id( lyrid );

    if ( !lyr )
    {
      char *errmsg = malloc( strlen(lyrid) + 256 );

      sprintf( errmsg, "No layer with id '%s'", lyrid );
      HND_ERR_NORETURN( errmsg );

      free( errmsg );
      return;
    }

    *lptr++ = lyr;
    lcnt++;
  }

  *lptr = NULL;

  L = lyph_by_layers( type, lyrs, name );

  if ( !L )
    HND_ERR( "Could not create the desired lyph" );

  send_200_response( req, lyph_to_json( L ) );
}

void handle_lyph_request( char *request, http_request *req )
{
  lyph *L;

  L = lyph_by_id( request );

  if ( !L )
    HND_ERR( "No lyph with that id" );

  send_200_response( req, lyph_to_json( L ) );
}

void handle_lyphedge_request( char *request, http_request *req )
{
  lyphedge *e = lyphedge_by_id( request );

  if ( !e )
    HND_ERR( "No lyphedge by that id" );

  send_200_response( req, lyphedge_to_json( e ) );
}

void handle_lyphnode_request( char *request, http_request *req )
{
  lyphnode *n = lyphnode_by_id( request );

  if ( !n )
    HND_ERR( "No lyphnode by that id" );

  lyphnode_to_json_flags = LTJ_EXITS;

  send_200_response( req, lyphnode_to_json( n ) );

  lyphnode_to_json_flags = 0;
}

void handle_lyphview_request( char *request, http_request *req )
{
  lyphview *v = lyphview_by_id( request );

  if ( !v )
    HND_ERR( "No lyphview by that id" );

  send_200_response( req, lyphview_to_json( v ) );
}

void handle_layer_request( char *request, http_request *req )
{
  layer *lyr = layer_by_id( request );

  if ( !lyr )
    HND_ERR( "No layer by that id" );

  send_200_response( req, layer_to_json( lyr ) );
}

void handle_lyphpath_request( http_request *req, url_param **params )
{
  char *fromstr, *tostr, *filterstr;
  lyphnode *from, *to;
  lyphedge **path;
  edge_filter *filter;

  fromstr = get_url_param( params, "from" );

  if ( !fromstr )
    HND_ERR( "No 'from' parameter detected in your request" );

  from = lyphnode_by_id( fromstr );

  if ( !from )
    HND_ERR( "The specified 'from' node was not found in the database" );

  tostr = get_url_param( params, "to" );

  if ( !tostr )
    HND_ERR( "No 'to' parameter detected in your request" );

  to = lyphnode_by_id( tostr );

  if ( !to )
    HND_ERR( "The specified 'to' node was not found in the database" );

  filterstr = get_url_param( params, "filter" );

  if ( filterstr )
  {
    lyph *L = lyph_by_id( filterstr );
    char *na_str;

    if ( !L )
      HND_ERR( "The indicated filter lyph was not found in the database" );

    CREATE( filter, edge_filter, 1 );
    filter->sup = L;

    na_str = get_url_param( params, "include_lyphless" );

    if ( na_str )
    {
      if ( !strcmp( na_str, "yes" ) )
        filter->accept_na_edges = 1;
      else if ( !strcmp( na_str, "no" ) )
        filter->accept_na_edges = 0;
      else
        HND_ERR( "include_lyphless should be either 'yes' or 'no'" );
    }
    else
      filter->accept_na_edges = 0;
  }
  else
    filter = NULL;

  path = compute_lyphpath( from, to, filter );

  if ( !path )
    HND_ERR( "No path found" );

  send_200_response( req, lyphpath_to_json( path ) );

  free( path );
}

void handle_ucl_syntax_request( char *request, http_request *req )
{
  ucl_syntax *s;
  char *err = NULL, *maybe_err = NULL, *output;
  ambig *head = NULL, *tail = NULL;

  s = parse_ucl_syntax( request, &err, &maybe_err, &head, &tail );

  if ( !s )
  {
    if ( err )
    {
      HND_ERR_NORETURN( err );
      free( err );
    }
    else
      HND_ERR_NORETURN( "Malformed UCL Syntax" );

    if ( head )
      free_ambigs( head );
    if ( maybe_err )
      free( maybe_err );

    return;
  }

  output = ucl_syntax_output( s, head, tail, maybe_err );

  send_200_response( req, output );

  free( output );

  kill_ucl_syntax( s );

  if ( head )
    free_ambigs( head );
  if ( maybe_err )
    free( maybe_err );

  return;
}

void free_url_params( url_param **buf )
{
  url_param **ptr;

  for ( ptr = buf; *ptr; ptr++ )
  {
    free( (*ptr)->key );
    free( (*ptr)->val );
    free( *ptr );
  }
}

char *get_url_param( url_param **params, char *key )
{
  url_param **ptr;

  for ( ptr = params; *ptr; ptr++ )
    if ( !strcmp( (*ptr)->key, key ) )
      return (*ptr)->val;

  return NULL;
}

void handle_all_lyphedges_request( http_request *req )
{
  lyphedge **edges = (lyphedge **)datas_to_array( lyphedge_ids );

  send_200_response( req, JS_ARRAY( lyphedge_to_json, edges ) );

  free( edges );
}

void handle_all_lyphs_request( http_request *req )
{
  lyph **lyphs = (lyph **)datas_to_array( lyph_ids );

  send_200_response( req, JS_ARRAY( lyph_to_json, lyphs ) );

  free( lyphs );
}

void handle_lyph_hierarchy_request( http_request *req )
{
  send_200_response( req, lyph_hierarchy_to_json() );
}

void handle_all_lyphviews_request( http_request *req )
{
  lyphview **v, **vptr, **viewsptr;
  extern lyphview obsolete_lyphview;
  extern lyphview **views;

  CREATE( v, lyphview *, voidlen( (void **)&views[1] ) );
  vptr = v;

  for ( viewsptr = &views[1]; *viewsptr; viewsptr++ )
    if ( *viewsptr != &obsolete_lyphview )
      *vptr++ = *viewsptr;

  *vptr = NULL;

  send_200_response( req, JS_ARRAY( lyphview_to_json, v ) );

  free( v );
}

void handle_all_lyphnodes_request( http_request *req )
{
  lyphnode **n = (lyphnode **)datas_to_array( lyphnode_ids );
  extern int lyphnode_to_json_flags;

  lyphnode_to_json_flags = LTJ_EXITS;

  send_200_response( req, JS_ARRAY( lyphnode_to_json, n ) );

  lyphnode_to_json_flags = 0;

  free ( n );
}
