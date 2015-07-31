/*
 *  srv.c
 *  Bare-bones HTTP server.
 *  Also contains the first API commands that were written (these should
 *  probably be migrated to cmds.c or to a new .c file).
 */
#include "lyph.h"
#include "srv.h"

system_configs configs;

char *html;
char *js;
char *bulk;

extern int lyphnode_to_json_flags;

int main( int argc, const char* argv[] )
{
  FILE *fp;
  int i = 1, port=5052;
  const char *filename;

  default_config_values();

  if ( !parse_commandline_args( argc, argv, &filename, &port ) )
    return 0;

  fp = fopen( filename, "r" );

  to_logfile( "Lyph started up at %s", current_date() );

  if ( !fp )
  {
    fprintf( stderr, "Could not open file %s for reading\n\n", argv[i] );
    return 0;
  }

  init_labels(fp);
  fclose(fp);

  init_lyph_http_server(port);
  init_command_table();

  printf( "Ready.\n" );

  while(1)
  {
    /*
     * To do: make this commandline-configurable
     */
    usleep(100000);
    main_loop();
  }
}

void init_lyph_http_server( int port )
{
  int status, yes=1;
  struct addrinfo hints, *servinfo;
  char portstr[128];

  html = load_file( "lyphgui.html" );
  js = load_file( "lyphgui.js" );
  bulk = load_file( "lyphbulk.html" );

  if ( !html || !js )
  {
    error_messagef( "Could not load %s for reading, aborting", !html ? "lyphgui.html" : "lyphgui.js" );
    EXIT();
  }

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
    exit( EXIT_SUCCESS );
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
      count++;

      to_logfile( "Got request:\n%s", req->query );

      handle_request( req, req->query );
    }
    else
      break;
  }

  json_gc();
}

void handle_request( http_request *req, char *query )
{
  char *reqptr, *reqtype, *request;
  const char *parse_params_err;
  command_entry *entry;
  url_param *params[MAX_URL_PARAMS+1];

  if ( req_cmp( query, "gui" )
  ||   req_cmp( query, "lyphgui" ) )
  {
    send_gui( req );
    return;
  }

  if ( req_cmp( query, "js" )
  ||   req_cmp( query, "lyphjs" ) )
  {
    send_js( req );
    return;
  }

  for ( reqptr = (*query == '/') ? query + 1 : query; *reqptr; reqptr++ )
    if ( *reqptr == '/' )
      break;

  if ( !*reqptr )
  {
    send_400_response( req );
    return;
  }

  *reqptr = '\0';
  reqtype = (*query == '/') ? query + 1 : query;

  parse_params_err = parse_params( &reqptr[1], req, params );

  if ( parse_params_err )
  {
    HND_ERR_NORETURN( parse_params_err );

    free_url_params( params );
    return;
  }

  request = url_decode(&reqptr[1]);

  entry = lookup_command( reqtype );

  if ( entry )
  {
    if ( entry->read_write_state == CMD_READWRITE && configs.readonly )
      send_response( req, "{\"error\": \"This instance of the LYPH system is read-only\"}" );
    else
      (*(entry->f))( request, req, params );

    free( request );
    free_url_params( params );
    return;
  }

  free_url_params( params );
  *reqptr = '/';
  free( request );
  send_400_response( req );
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

  MULTIFREE( c->buf, c->outbuf );
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

void send_response( http_request *req, char *txt )
{
  send_response_with_type( req, "200 OK", txt, "application/json" );
}

void send_error_response( http_request *req, char *txt )
{
  send_response_with_type( req, "400 Bad Request", txt, "application/json" );
}

void send_response_with_type( http_request *req, char *code, char *txt, char *type )
{
  char *buf, *fmt;

  if ( !strcmp( type, "application/json" ) )
  {
    fmt = json_format( txt, 2, NULL );

    if ( fmt )
      txt = fmt;
  }

  /*
   * JSONP support
   */
  if ( req->callback )
  {
    char *jsonp = malloc( strlen(txt) + strlen(req->callback) + strlen( "(\n\n);" ) + 1 );

    sprintf( jsonp, "%s(\n%s\n);", req->callback, txt );
    txt = jsonp;
  }

  buf = strdupf(  "HTTP/1.1 %s\r\n"
                  "Date: %s\r\n"
                  "Content-Type: %s; charset=utf-8\r\n"
                  "%s"
                  "Content-Length: %zd\r\n"
                  "\r\n"
                  "%s",
                  code,
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
  send_response_with_type( req, "200 OK", html, "text/html" );
}

void send_js( http_request *req )
{
  send_response_with_type( req, "200 OK", js, "application/javascript" );
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
    return NULL;

  fseek(fp, 0L, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  CREATE( buf, char, size+1 );

  for ( bptr = buf; ; bptr++ )
  {
    QUICK_GETC(*bptr,fp);

    if ( !*bptr )
    {
      fclose(fp);
      return buf;
    }
  }
}

const char *parse_params( char *buf, http_request *req, url_param **params )
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
      char *equals;

      if ( *bptr == '\0' )
        fEnd = 1;
      else
        *bptr = '\0';

      if ( ++cnt >= MAX_URL_PARAMS )
      {
        *pptr = NULL;
        return "Too many URL parameters";
      }

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

HANDLER( do_lyphpath )
{
  along_path_abstractor( req, params, ALONG_PATH_COMPUTE );
}

HANDLER( do_template_along_path )
{
  along_path_abstractor( req, params, ALONG_PATH_TEMPLATE );
}

HANDLER( do_constrain_along_path )
{
  along_path_abstractor( req, params, ALONG_PATH_CONSTRAIN );
}

void along_path_abstractor( http_request *req, url_param **params, int along_path_type )
{
  lyphnode *x, *y;
  lyphplate *L;
  lyph ***paths, ***pathsptr, **p, **pptr, *xlyph, *ylyph;
  lyph_filter *f;
  lyphnode_wrapper *w, *from_head = NULL, *from_tail = NULL, *to_head = NULL, *to_tail = NULL;
  char *xid, *yid, *Lid, *fid, *xlyphid, *ylyphid, *numpathsstr;
  int numpaths;

  xid = get_param( params, "from" );
  yid = get_param( params, "to" );
  xlyphid = get_param( params, "fromlyph" );
  ylyphid = get_param( params, "tolyph" );

  if ( !xid && !xlyphid )
    HND_ERR( "You did not specify the 'from' node (or the 'fromlyph' lyph)" );

  if ( !yid && !ylyphid )
    HND_ERR( "You did not specify the 'to' node (or the 'tolyph' lyph)" );

  Lid = get_param( params, "template" );
  fid = get_param( params, "filter" );

  if ( !Lid && along_path_type != ALONG_PATH_COMPUTE )
    HND_ERR( "You did not specify the template to assign along the path" );

  if ( xid )
  {
    x = lyphnode_by_id( xid );

    if ( !x )
      HND_ERR( "The indicated 'from' node was not found in the database" );
  }
  else
  {
    xlyph = lyph_by_id( xlyphid );

    if ( !xlyph )
      HND_ERR( "The indicated 'fromlyph' lyph was not found in the database" );
  }

  if ( yid )
  {
    y = lyphnode_by_id( yid );

    if ( !y )
      HND_ERR( "The indicated 'to' node was not found in the database" );
  }
  else
  {
    ylyph = lyph_by_id( ylyphid );

    if ( !ylyph )
      HND_ERR( "The indicated 'tolyph' lyph was not found in the database" );
  }

  numpathsstr = get_param( params, "numpaths" );
  if ( numpathsstr )
  {
    numpaths = strtoul( numpathsstr, NULL, 10 );
    if ( numpaths < 1 )
      HND_ERR( "'numpaths' must be a positive integer" );

    if ( numpaths > MAX_NUMPATHS )
      HND_ERRF( "'numpaths' too large, maximum is %d", MAX_NUMPATHS );
  }
  else
    numpaths = 1;

  if ( along_path_type != ALONG_PATH_COMPUTE )
  {
    L = lyphplate_by_id( Lid );

    if ( !L )
      HND_ERR( "The indicated template was not found in the database" );
  }

  if ( fid )
  {
    lyphplate *filt = lyphplate_by_id( fid );
    char *na_param;

    if ( !filt )
      HND_ERR( "The indicated template (to act as filter) was not found in the database" );

    CREATE( f, lyph_filter, 1 );

    f->sup = filt;

    na_param = get_param( params, "include_templateless" );

    if ( na_param && !strcmp( na_param, "yes" ) )
      f->accept_na_edges = 1;
    else if ( na_param && !strcmp( na_param, "no" ) )
      f->accept_na_edges = 0;
    else if ( na_param )
      HND_ERR( "'include_templateless' must be 'yes' or 'no'" );
    else
      f->accept_na_edges = 0;
  }
  else
    f = NULL;

  if ( xid )
  {
    CREATE( w, lyphnode_wrapper, 1 );
    w->n = x;
    LINK( w, from_head, from_tail, next );
  }
  else
    calc_nodes_in_lyph( xlyph, &from_head, &from_tail );

  if ( yid )
  {
    CREATE( w, lyphnode_wrapper, 1 );
    w->n = y;
    LINK( w, to_head, to_tail, next );
  }
  else
    calc_nodes_in_lyph( ylyph, &to_head, &to_tail );

  paths = compute_lyphpaths( from_head, to_head, f, numpaths, 0, 1, 0 );

  free_lyphnode_wrappers( from_head );
  free_lyphnode_wrappers( to_head );

  if ( !*paths )
  {
    if ( f )
      free( f );

    free( paths );

    HND_ERR( "No path found" );
  }

  if ( along_path_type == ALONG_PATH_TEMPLATE )
  {
    for ( pathsptr = paths; *pathsptr; pathsptr++ )
    {
      p = *pathsptr;

      for ( pptr = p; *pptr; pptr++ )
      {
        char *err;

        if ( !can_assign_lyphplate_to_lyph( L, *pptr, &err ) )
        {
          if ( f )
            free( f );

          free( p );

          HND_ERR( err ? err : "One of the lyphs on the path could not be assigned the template, due to a constraint on it" );
        }
      }
    }

    for ( pathsptr = paths; *pathsptr; pathsptr++ )
    {
       p = *pathsptr;

      for ( pptr = p; *pptr; pptr++ )
      {
        (*pptr)->lyphplt = L;
        (*pptr)->modified = longtime();
      }
    }

    save_lyphs();
  }
  else if ( along_path_type == ALONG_PATH_CONSTRAIN )
  {
    for ( pathsptr = paths; *pathsptr; pathsptr++ )
    {
      p = *pathsptr;

      for ( pptr = p; *pptr; pptr++ )
      {
#ifdef PRE_LAYER_CHANGE
        if ( (*pptr)->lyphplate && !is_superlyphplate( L, (*pptr)->lyphplate ) )
#else
        if ( 0 )
#endif
        {
          if ( f )
            free( f );

          for ( pathsptr = paths; *pathsptr; pathsptr++ )
            free( *pathsptr );
          free( paths );

          HND_ERR( "One of the lyphs on the path already has a template inconsistent with that constraint" );
        }
      }
    }

    for ( pathsptr = paths; *pathsptr; pathsptr++ )
    {
      p = *pathsptr;

      for ( pptr = p; *pptr; pptr++ )
      {
        lyph *e = *pptr;
        lyphplate **dupe, **c;
        int len;

        for ( dupe = e->constraints; *dupe; dupe++ )
          if ( *dupe == L )
            break;

        if ( *dupe )
          continue;

        len = dupe - e->constraints;

        CREATE( c, lyphplate *, len + 2 );
        memcpy( c, e->constraints, len * sizeof(lyphplate *) );
        c[len] = L;
        c[len+1] = NULL;
        free( e->constraints );
        e->constraints = c;
        e->modified = longtime();
      }
    }
  }

  if ( f )
    free( f );

  if ( along_path_type == ALONG_PATH_COMPUTE )
  {
    if ( !numpathsstr )
      send_response( req, lyphpath_to_json( *paths ) );
    else
      send_response( req, JS_ARRAY( lyphpath_to_json, paths ) );
  }
  else
    send_ok( req );

  for ( pathsptr = paths; *pathsptr; pathsptr++ )
    free( *pathsptr );
  free( paths );
}

HANDLER( do_makelyphnode )
{
  lyphnode *n;
  lyph *loc;
  char *locstr, *loctypestr;
  int loctype;

  locstr = get_param( params, "location" );

  if ( locstr )
  {
    loc = lyph_by_id( locstr );

    if ( !loc )
      HND_ERR( "The indicated lyph was not found in the database." );

    TRY_PARAM( loctypestr, "loctype", "You did not specify a loctype ('interior' or 'border')" );

    if ( !strcmp( loctypestr, "interior" ) )
      loctype = LOCTYPE_INTERIOR;
    else if ( !strcmp( loctypestr, "border" ) )
      loctype = LOCTYPE_BORDER;
    else
      HND_ERR( "Valid loctypes are: 'interior', 'border'" );
  }
  else
    loc = NULL;

  n = make_lyphnode();

  if ( !n )
    HND_ERR( "Could not create new lyphnode (out of memory?)" );

  if ( loc )
  {
    n->location = loc;
    n->loctype = loctype;
  }

  lyphnode_to_json_flags = LTJ_EXITS;

  send_response( req, lyphnode_to_json( n ) );

  lyphnode_to_json_flags = 0;

  if ( !configs.readonly )
    save_lyphs();
}

HANDLER( do_makelyph )
{
  lyphnode *from, *to;
  lyph *e;
  lyphplate *L;
  char *fmastr, *fromstr, *tostr, *namestr, *tmpltstr, *typestr, *pubmedstr, *projstr;
  int type;

  typestr = get_param( params, "type" );

  if ( typestr )
  {
    type = strtol( typestr, NULL, 10 );

    if ( type < 1 || type > 3 )
      HND_ERR( "Type must be 1, 2, or 3" );
  }
  else
    type = 1;

  fmastr = get_param( params, "fma" );

  if ( fmastr && !*fmastr )
    fmastr = NULL;

  fromstr = get_param( params, "from" );
  tostr = get_param( params, "to" );

  if ( !fromstr || !tostr )
    HND_ERR( "An edge requires a 'from' and a 'to' field" );

  from = lyphnode_by_id_or_new( fromstr );

  if ( !from )
    HND_ERR( "The indicated 'from' node was not found" );

  to = lyphnode_by_id_or_new( tostr );

  if ( !to )
    HND_ERR( "The indicated 'to' node was not found" );

  namestr = get_param( params, "name" );

  tmpltstr = get_param( params, "template" );

  if ( tmpltstr )
  {
    L = lyphplate_by_id( tmpltstr );

    if ( !L )
      HND_ERR( "The indicated template was not found" );
  }
  else
    L = NULL;

  pubmedstr = get_param( params, "pubmed" );
  projstr = get_param( params, "projection_strength" );

  e = make_lyph( type, from, to, L, fmastr, namestr, pubmedstr, projstr, get_param( params, "species" ) );

  if ( !e )
    HND_ERR( "The lyph could not be created (out of memory?)" );

  send_response( req, lyph_to_json( e ) );
}

HANDLER( do_makeview )
{
  makeview_worker( request, req, params, MAKEVIEW_WORKER_MAKEVIEW );
}

HANDLER( do_nodes_to_view )
{
  makeview_worker( request, req, params, MAKEVIEW_WORKER_NODES_TO_VIEW );
}

HANDLER( do_change_coords )
{
  makeview_worker( request, req, params, MAKEVIEW_WORKER_CHANGE_COORDS );
}

HANDLER( do_editview )
{
  makeview_worker( request, req, params, MAKEVIEW_WORKER_EDITVIEW );
}

void makeview_worker( char *request, http_request *req, url_param **params, int type )
{
  lyphnode **nodes, **nptr;
  lyph **lyphs, **lptr;
  lv_rect **rptr;
  char *namestr, *speciesstr, *err = NULL;
  char **xs=NULL, **ys=NULL, **lxs=NULL, **lys=NULL, **widths=NULL, **heights=NULL;
  char **xsptr, **ysptr, **lxsptr, **lysptr, **wptr, **hptr;
  int nodect, lyphct, xct, yct, lxct, lyct, widthct, heightct;
  int cnt, fChange;
  lyphview *v;

  if ( type != MAKEVIEW_WORKER_MAKEVIEW )
  {
    char *viewstr;

    if ( type == MAKEVIEW_WORKER_NODES_TO_VIEW )
      TRY_PARAM( viewstr, "view", "You did not specify which view to add nodes to." );
    else if ( type == MAKEVIEW_WORKER_CHANGE_COORDS )
      TRY_PARAM( viewstr, "view", "You did not specify which view to change coordinates in." );
    else
      TRY_PARAM( viewstr, "view", "You did not specify which view to edit." );

    v = lyphview_by_id( viewstr );

    if ( !v )
      HND_ERR( "The indicated view was not found in the database." );
  }

  nodes = (lyphnode**)GET_NUMBERED_ARGS( params, "node", lyphnode_by_id, &err, &nodect );

  if ( !nodes )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated nodes could not be found in the database" );
  }

  if ( nodect > 0 )
  {
    xs = (char**)GET_NUMBERED_ARGS( params, "x", NULL, &err, &xct );

    if ( xct < nodect )
    {
      MULTIFREE( nodes, xs );
      HND_ERR( "You did not specify x-coordinates for all the nodes you listed" );
    }

    ys = (char**)GET_NUMBERED_ARGS( params, "y", NULL, NULL, &yct );

    if ( yct < nodect )
    {
      MULTIFREE( nodes, xs, ys );
      HND_ERR( "You did not specify y-coordinates for all the nodes you listed" );
    }
  }

  speciesstr = get_param( params, "species" );

  lyphs = (lyph**) GET_NUMBERED_ARGS_R( params, "lyph", lyph_by_template_or_id_or_null, speciesstr, &err, &lyphct );

  if ( !lyphs )
  {
    MULTIFREE( nodes, xs, ys );

    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the specified lyphs was not found in the database." );
  }

  if ( lyphct > 0 )
  {
    lxs = (char**)GET_NUMBERED_ARGS( params, "lx", NULL, NULL, &lxct );

    if ( lxct < lyphct )
    {
      MULTIFREE( nodes, xs, ys, lyphs, lxs );
      HND_ERR( "You did not specify x-coordinates ('lx') for all the lyphs you listed" );
    }

    lys = (char**)GET_NUMBERED_ARGS( params, "ly", NULL, NULL, &lyct );

    if ( lyct < lyphct )
    {
      MULTIFREE( nodes, xs, ys, lyphs, lxs, lys );
      HND_ERR( "You did not specify y-coordinates ('ly') for all the lyphs you listed" );
    }

    widths = (char**)GET_NUMBERED_ARGS( params, "width", NULL, NULL, &widthct );

    if ( widthct < lyphct )
    {
      MULTIFREE( nodes, xs, ys, lyphs, lxs, lys, widths );
      HND_ERR( "You did not specify widths for all the lyphs you listed" );
    }

    heights = (char**)GET_NUMBERED_ARGS( params, "height", NULL, NULL, &heightct );

    if ( heightct < lyphct )
    {
      MULTIFREE( nodes, xs, ys, lyphs, lxs, lys, widths, heights );
      HND_ERR( "You did not specify heights for all the lyphs you listed" );
    }
  }

  if ( type == MAKEVIEW_WORKER_MAKEVIEW )
  {
    namestr = get_param( params, "name" );

    v = create_new_view( nodes, xs, ys, lyphs, lxs, lys, widths, heights, namestr ? strdup(namestr) : NULL );

    if ( !v )
    {
      MULTIFREE( nodes, lyphs );
      HND_ERR_NORETURN( "Could not create the view (out of memory?)" );
    }
    else
      send_response( req, lyphview_to_json( v ) );

    if ( nodect )
      MULTIFREE( xs, ys );

    if ( lyphct )
      MULTIFREE( lxs, lys, widths, heights );

    return;
  }

  #define LYPHNODE_ALREADY_IN_VIEW 1
  #define LYPHNODE_QUEUED_FOR_ADDING 2
  #define LYPH_ALREADY_IN_VIEW 1
  #define LYPH_QUEUED_FOR_ADDING 2

  fChange = 0;

  if ( type == MAKEVIEW_WORKER_EDITVIEW )
  {
    if ( v->nodes )
      free( v->nodes );
    if ( v->coords )
      free( v->coords );

    v->nodes = (lyphnode**)blank_void_array();
    v->coords = (char**)blank_void_array();

    if ( v->rects )
    {
      for ( rptr = v->rects; *rptr; rptr++ )
      {
        MULTIFREE( (*rptr)->x, (*rptr)->y, (*rptr)->width, (*rptr)->height );
        free( *rptr );
      }
      free( v->rects );
    }
    v->rects = (lv_rect**)blank_void_array();
    fChange = 1;
  }

  for ( nptr = v->nodes; *nptr; nptr++ )
    SET_BIT( (*nptr)->flags, LYPHNODE_ALREADY_IN_VIEW );

  for ( rptr = v->rects; *rptr; rptr++ )
    if ( (*rptr)->L && (*rptr)->L != null_rect )
      SET_BIT( (*rptr)->L->flags, LYPH_ALREADY_IN_VIEW );

  for ( nptr = nodes, cnt=0; *nptr; nptr++ )
  {
    if ( IS_SET( (*nptr)->flags, LYPHNODE_ALREADY_IN_VIEW )
    ||   IS_SET( (*nptr)->flags, LYPHNODE_QUEUED_FOR_ADDING ) )
      continue;

    SET_BIT( (*nptr)->flags, LYPHNODE_QUEUED_FOR_ADDING );
    cnt++;
  }

  if ( cnt )
  {
    lyphnode **buf, **bptr;
    char **newc, **newcptr;
    int oldlen = VOIDLEN( v->nodes );

    fChange = 1;

    CREATE( buf, lyphnode *, oldlen + cnt + 1 );
    memcpy( buf, v->nodes, oldlen * sizeof(lyphnode *) );
    bptr = &buf[oldlen];

    CREATE( newc, char *, (oldlen + cnt) * 2 + 1 );
    memcpy( newc, v->coords, oldlen * 2 * sizeof(char *) );
    newcptr = &newc[oldlen*2];

    for ( nptr = nodes, xsptr = xs, ysptr = ys; *nptr; nptr++ )
    {
      if ( IS_SET( (*nptr)->flags, LYPHNODE_ALREADY_IN_VIEW ) )
      {
        xsptr++;
        ysptr++;
        continue;
      }

      SET_BIT( (*nptr)->flags, LYPHNODE_ALREADY_IN_VIEW );

      *bptr++ = *nptr;
      *newcptr++ = strdup( *xsptr++ );
      *newcptr++ = strdup( *ysptr++ );
    }

    *bptr = NULL;
    *newcptr = NULL;

    MULTIFREE( v->nodes, v->coords );
    v->nodes = buf;
    v->coords = newc;
  }

  if ( (type == MAKEVIEW_WORKER_CHANGE_COORDS || type == MAKEVIEW_WORKER_EDITVIEW) && nodect )
  {
    for ( nptr = nodes, xsptr = xs, ysptr = ys; *nptr; nptr++, xsptr++, ysptr++ )
    {
      if ( !IS_SET( (*nptr)->flags, LYPHNODE_QUEUED_FOR_ADDING ) )
      {
        /*
         * The node was already in the view, and did not to be added.
         * Find it and change its coordinates.
         */
        char **oldc;
        lyphnode **oldnptr;

        for ( oldnptr = v->nodes, oldc = v->coords; *oldnptr; oldnptr++, oldc += 2 )
        {
          if ( *oldnptr == *nptr )
          {
            oldc[0] = *xsptr;
            oldc[1] = *ysptr;
            break;
          }
        }
      }
    }
  }

  for ( nptr = v->nodes; *nptr; nptr++ )
  {
    REMOVE_BIT( (*nptr)->flags, LYPHNODE_ALREADY_IN_VIEW );
    REMOVE_BIT( (*nptr)->flags, LYPHNODE_QUEUED_FOR_ADDING );
  }

  free( nodes );
  if ( nodect > 0 )
    MULTIFREE( xs, ys );

  for ( lptr = lyphs, cnt=0; *lptr; lptr++ )
  {
    if ( *lptr == null_rect )
    {
      cnt++;
      continue;
    }

    if ( IS_SET( (*lptr)->flags, LYPH_ALREADY_IN_VIEW )
    ||   IS_SET( (*lptr)->flags, LYPH_QUEUED_FOR_ADDING ) )
      continue;

    SET_BIT( (*lptr)->flags, LYPH_QUEUED_FOR_ADDING );
    cnt++;
  }

  if ( cnt )
  {
    lv_rect **buf, **bptr;
    int oldlen = VOIDLEN( v->rects );

    fChange = 1;

    CREATE( buf, lv_rect *, oldlen + cnt + 1 );
    memcpy( buf, v->rects, oldlen * sizeof( lv_rect * ) );
    bptr = &buf[oldlen];

    for ( lptr = lyphs, lxsptr = lxs, lysptr = lys, wptr = widths, hptr = heights; *lptr; lptr++ )
    {
      lv_rect *rect;

      if ( *lptr != null_rect && IS_SET( (*lptr)->flags, LYPH_ALREADY_IN_VIEW ) )
      {
        lxsptr++;
        lysptr++;
        wptr++;
        hptr++;

        continue;
      }

      if ( *lptr != null_rect )
        SET_BIT( (*lptr)->flags, LYPH_ALREADY_IN_VIEW );

      CREATE( rect, lv_rect, 1 );

      if ( *lptr != null_rect )
        rect->L = *lptr;
      else
        rect->L = NULL;

      rect->x = strdup( *lxsptr++ );
      rect->y = strdup( *lysptr++ );
      rect->width = strdup( *wptr++ );
      rect->height = strdup( *hptr++ );

      *bptr++ = rect;
    }

    *bptr = NULL;

    free( v->rects );
    v->rects = buf;
  }

  if ( type == MAKEVIEW_WORKER_CHANGE_COORDS && lyphct )
  {
    for ( lptr = lyphs, lxsptr = lxs, lysptr = lys, wptr = widths, hptr = heights;
          *lptr; lptr++, lxsptr++, lysptr++, wptr++, hptr++ )
    {
      if ( !IS_SET( (*lptr)->flags, LYPH_QUEUED_FOR_ADDING ) )
      {
        /*
         * The lyph was already present, and didn't need adding.
         * Find it and change its coordinates.
         */
        lv_rect **oldrptr;

        for ( oldrptr = v->rects; *oldrptr; oldrptr++ )
        {
          if ( (*oldrptr)->L == *lptr )
          {
            lv_rect *needle = *oldrptr;
            needle->x = *lxsptr;
            needle->y = *lysptr;
            needle->width = *wptr;
            needle->height = *hptr;
            break;
          }
        }
      }
    }
  }

  for ( rptr = v->rects; *rptr; rptr++ )
  {
    if ( (*rptr)->L == null_rect || !(*rptr)->L )
      continue;

    REMOVE_BIT( (*rptr)->L->flags, LYPH_ALREADY_IN_VIEW );
    REMOVE_BIT( (*rptr)->L->flags, LYPH_QUEUED_FOR_ADDING );
  }

  free( lyphs );

  if ( lyphct > 0 )
    MULTIFREE( lxs, lys, widths, heights );

  if ( type == MAKEVIEW_WORKER_EDITVIEW )
  {
    namestr = get_param( params, "name" );

    if ( namestr )
    {
      fChange = 1;

      if ( v->name )
        free( v->name );

      v->name = strdup( namestr );
    }
  }

  if ( fChange || type == MAKEVIEW_WORKER_EDITVIEW )
  {
    v->modified = longtime();
    save_lyphviews();
  }

  send_response( req, lyphview_to_json( v ) );
}

HANDLER( do_makelayer )
{
  layer *lyr;
  lyphplate **materials;
  char *mtid, *thickstr, *namestr, *name, *err;
  int thickness;

  TRY_TWO_PARAMS( mtid, "material", "materials", "No material specified for layer" );

  thickstr = get_param( params, "thickness" );

  if ( thickstr )
    thickness = strtol( thickstr, NULL, 10 );
  else
    thickness = -1;

  if ( !strcmp( mtid, "none" ) )
    materials = (lyphplate **)blank_void_array();
  else
    materials = (lyphplate **)PARSE_LIST( mtid, lyphplate_by_id, "template", &err );

  if ( !materials )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated templates was unrecognized" );
  }

  namestr = get_param( params, "name" );

  if ( namestr )
    name = strdup( namestr );
  else
    name = NULL;

  lyr = layer_by_description( name, materials, thickness );

  if ( !lyr )
    HND_ERR( "Invalid layer" );

  send_response( req, layer_to_json( lyr ) );
}

HANDLER( do_lyphconstrain )
{
#ifdef PRE_LAYER_CHANGE
  lyph *e;
  lyphplate *L, **c;
  char *lyphid, *tmpltid;
  int cnt;

  TRY_PARAM( lyphid, "lyph", "You did not specify a lyph." );
  TRY_PARAM( tmpltid, "template", "You did not specify a template." );

  e = lyph_by_id( lyphid );

  if ( !e )
    HND_ERR( "The database does not contain a lyph with that ID." );

  L = lyphplate_by_id( tmpltid );

  if ( !L )
    HND_ERR( "The database does not contain a template with that ID." );

  for ( c = e->constraints; *c; c++ )
    if ( *c == L )
      HND_ERR( "The lyph in question already has the constraint in question." );

  if ( e->lyphplate && !is_superlyphplate( L, e->lyphplate ) )
    HND_ERR( "The lyph in question already has a template that violates this constraint." );

  cnt = VOIDLEN( e->constraints );
  CREATE( c, lyphplate *, cnt + 2 );

  memcpy( c, e->constraints, cnt * sizeof(lyphplate *) );

  c[cnt] = L;
  c[cnt+1] = NULL;

  free( e->constraints );
  e->constraints = c;
  e->modified = longtime();

  save_lyphs();

  send_ok( req );
#else
  send_response( req, JSON1( "Response": "Temporarily disabled" ) );
#endif
}

HANDLER( do_assign_template )
{
  lyph **lyphs, **e;
  lyphplate *L;
  char *lyphid, *tmpltid, *err;

  TRY_PARAM( tmpltid, "template", "You did not specify a template." );

  L = lyphplate_by_id( tmpltid );

  if ( !L )
    HND_ERR( "The database has no template with that ID." );

  TRY_PARAM( lyphid, "lyph", "You did not specify a lyph." );

  lyphs = (lyph **) PARSE_LIST( lyphid, lyph_by_id, "lyph", &err );

  if ( !lyphs )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "The database has no lyph with that ID." );
  }

  for ( e = lyphs; *e; e++ )
  {
    if ( !can_assign_lyphplate_to_lyph( L, *e, &err ) )
    {
      free( lyphs );

      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "That template cannot be assigned to that lyph." );

      return;
    }
  }

  for ( e = lyphs; *e; e++ )
  {
    (*e)->lyphplt = L;
    (*e)->modified = longtime();
  }

  free( lyphs );

  save_lyphs();

  send_ok( req );
}

HANDLER( do_maketemplate )
{
  lyphplate *L, **misc_mats;
  static layer **lyrs;
  layer **lptr;
  char *name, *typestr, *miscstr, *lengthstr, *length;
  int type, lcnt;

  if ( !lyrs )
    CREATE( lyrs, layer *, MAX_URL_PARAMS + 2 );

  TRY_PARAM( name, "name", "No name specified for template" );
  TRY_PARAM( typestr, "type", "No type specified for template" );

  type = parse_lyphplate_type( typestr );

  if ( type == -1 )
    HND_ERR( "Invalid type specified for template" );

  lcnt = 1;
  lptr = lyrs;

  if ( type == LYPHPLATE_BASIC && get_param( params, "layer1" ) )
    HND_ERR( "Basic templates cannot be given layers" );
  
  for(;;)
  {
    char pmname[MAX_URL_PARAM_LEN+128];
    char *lyrid;
    layer *lyr;

    sprintf( pmname, "layer%d", lcnt );

    lyrid = get_param( params, pmname );

    if ( !lyrid )
      break;

    lyr = layer_by_id( lyrid );

    if ( !lyr )
    {
      char *errmsg = strdupf( "No layer with id '%s'", lyrid );
      HND_ERR_FREE( errmsg );
    }

    *lptr++ = lyr;
    lcnt++;
  }

  *lptr = NULL;

  miscstr = get_param( params, "misc_materials" );

  if ( miscstr )
  {
    char *err;
    
    if ( type == LYPHPLATE_BASIC )
      HND_ERR( "Basic templates can't be given misc. materials" );

    misc_mats = (lyphplate **)PARSE_LIST( miscstr, lyphplate_by_id, "template", &err );

    if ( !misc_mats )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated templates was unrecognized" );
    }
  }
  else
    misc_mats = NULL;

  lengthstr = get_param( params, "length" );
  
  if ( lengthstr )
    length = strdup( lengthstr );
  else
    length = NULL;
    
  L = lyphplate_by_layers( type, lyrs, misc_mats, name, length );

  if ( !L )
  {
    if ( misc_mats )
      free( misc_mats );

    HND_ERR( "Could not create the desired template" );
  }

  send_response( req, lyphplate_to_json( L ) );
}

HANDLER( do_template )
{
  lyphplate *L;
  char *commonsstr, *output;

  L = lyphplate_by_id( request );

  if ( !L )
    HND_ERR( "No template with that id" );

  commonsstr = get_param( params, "commons" );

  if ( commonsstr && !strcmp( commonsstr, "yes" ) )
  {
    lyphplate_to_json_details det;

    det.show_common_mats = 1;
    output = lyphplate_to_json_r( L, &det );
  }
  else
    output = lyphplate_to_json( L );

  send_response( req, output );
}

HANDLER( do_lyph )
{
  lyph **e;
  lyph_to_json_details details;
  char *err;

  e = (lyph**) PARSE_LIST(request, lyph_by_id_or_fmastr, "lyph", &err);

  if ( !e )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs was unrecognized" );
  }

  details.show_annots = has_param( params, "annots" );
  details.suppress_correlations = !has_param( params, "correlations" );
  details.count_correlations = has_param( params, "correlations" );
  details.show_children = 1;
  details.buf = NULL;

  if ( *e && !e[1] && !has_param( params, "array" ) )
    send_response( req, lyph_to_json_r( *e, &details ) );
  else
    send_response( req, JS_ARRAY_R( lyph_to_json_r, e, &details ) );
}

HANDLER( do_lyphnode )
{
  lyphnode *n = lyphnode_by_id( request );

  if ( !n )
    HND_ERR( "No lyphnode by that id" );

  lyphnode_to_json_flags = LTJ_EXITS;

  send_response( req, lyphnode_to_json( n ) );

  lyphnode_to_json_flags = 0;
}

HANDLER( do_lyphview )
{
  lyphview *v = lyphview_by_id( request );

  if ( !v )
    HND_ERR( "No lyphview by that id" );

  send_response( req, lyphview_to_json( v ) );
}

HANDLER( do_layer )
{
  layer *lyr = layer_by_id( request );

  if ( !lyr )
    HND_ERR( "No layer by that id" );

  send_response( req, layer_to_json( lyr ) );
}

HANDLER( do_ucl_syntax )
{
  ucl_syntax *s;
  char *err = NULL, *maybe_err = NULL, *output;
  ambig *head = NULL, *tail = NULL;

  s = parse_ucl_syntax( request, &err, &maybe_err, &head, &tail );

  if ( !s )
  {
    if ( head )
      free_ambigs( head );
    if ( maybe_err )
      free( maybe_err );

    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "Malformed UCL Syntax" );
  }

  output = ucl_syntax_output( s, head, tail, maybe_err );

  send_response( req, output );

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

char *get_param( url_param **params, char *key )
{
  url_param **ptr;

  for ( ptr = params; *ptr; ptr++ )
    if ( !strcmp( (*ptr)->key, key ) )
      return (*ptr)->val;

  return NULL;
}

int has_param( url_param **params, char *key )
{
  for ( ; *params; params++ )
    if ( !strcmp( (*params)->key, key ) )
      return 1;

  return 0;
}

void populate_lyphs_by_species( trie *species, lyph ***ptr, int include_null_species )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->species == species || ( is_null_species(e) && include_null_species ) )
    {
      **ptr = e;
      (*ptr)++;
    }
  }
}

HANDLER( do_all_lyphs )
{
  lyph **lyphs, **ptr, *e;
  lyph_to_json_details details;
  trie *species;
  char *speciesstr, *briefstr;
  int include_null_species = 0;

  speciesstr = get_param( params, "species" );
  briefstr = get_param( params, "brief" );

  if ( !speciesstr )
  {
    include_null_species = 1;
    speciesstr = "Human";
  }
  else if ( !strcmp( speciesstr, "Human" ) )
    include_null_species = 1;

  CREATE( lyphs, lyph *, lyphcnt + 1 );
  ptr = lyphs;

  if ( !strcmp( speciesstr, "any" ) )
  {
    for ( e = first_lyph; e; e = e->next )
      *ptr++ = e;

    *ptr = NULL;
  }
  else
  {
    species = trie_strdup( speciesstr, metadata );
    populate_lyphs_by_species( species, &ptr, include_null_species );
    *ptr = NULL;
  }

  if ( briefstr )
    send_response( req, JS_ARRAY( lyph_to_json_brief, lyphs ) );
  else
  {
    details.show_annots = 1;
    details.suppress_correlations = 1;
    details.count_correlations = 0;
    details.buf = NULL;
    details.show_children = 0;

    send_response( req, JS_ARRAY_R( lyph_to_json_r, lyphs, &details ) );
  }

  free( lyphs );
}

HANDLER( do_all_templates )
{
  lyphplate **tmps = get_all_lyphplates();
  char *commonsstr, *output;

  commonsstr = get_param( params, "commons" );

  if ( commonsstr && !strcmp( commonsstr, "yes" ) )
  {
    lyphplate_to_json_details det;

    det.show_common_mats = 1;
    output = JS_ARRAY_R( lyphplate_to_json_r, tmps, &det );
  }
  else
    output = JS_ARRAY( lyphplate_to_json, tmps );

  send_response( req, output );

  free( tmps );
}

HANDLER( do_template_hierarchy )
{
#ifdef PRE_LAYER_CHANGE
  send_response( req, lyphplate_hierarchy_to_json() );
#else
  send_response( req, JSON1( "Response": "Temporarily disabled due to layer structure change" ) );
#endif
}

HANDLER( do_all_ont_terms )
{
  send_response( req, all_ont_terms_as_json() );
}

HANDLER( do_all_lyphviews )
{
  lyphview **v, **vptr, **viewsptr;
  extern lyphview obsolete_lyphview;
  extern lyphview **views;

  CREATE( v, lyphview *, VOIDLEN( &views[1] ) + 1 );
  vptr = v;

  for ( viewsptr = &views[1]; *viewsptr; viewsptr++ )
    if ( *viewsptr != &obsolete_lyphview )
      *vptr++ = *viewsptr;

  *vptr = NULL;

  send_response( req, JS_ARRAY( lyphview_to_json, v ) );

  free( v );
}

HANDLER( do_subtemplates )
{
#ifdef PRE_LAYER_CHANGE
  lyphplate *L, **subs;
  char *tmpltstr, *directstr;
  int direct;

  TRY_PARAM( tmpltstr, "template", "You did not specify a template" );

  L = lyphplate_by_id( tmpltstr );

  if ( !L )
    HND_ERR( "The indicated template was not found in the database" );

  directstr = get_param( params, "direct" );

  if ( directstr )
  {
    if ( !strcmp( directstr, "yes" ) )
      direct = 1;
    else if ( !strcmp( directstr, "no" ) )
      direct = 0;
    else
      HND_ERR( "The 'direct' parameter can be 'yes' or 'no'" );
  }
  else
    direct = 0;

  subs = get_sublyphplates( L, direct );

  send_response( req, JS_ARRAY( lyphplate_to_json, subs ) );

  free( subs );
#else
  send_response( req, JSON1( "Response": "Temporarily disabled" ) );
#endif
}

HANDLER( do_all_lyphnodes )
{
  lyphnode **n = (lyphnode **)datas_to_array( lyphnode_ids );
  extern int lyphnode_to_json_flags;

  lyphnode_to_json_flags = LTJ_EXITS;

  send_response( req, JS_ARRAY( lyphnode_to_json, n ) );

  lyphnode_to_json_flags = 0;

  free ( n );
}

HANDLER( do_reset_db )
{
  int fMatch = 0;

  #ifdef NDEBUG
  HND_ERR( "The reset_db command is only available when the server is running in debug mode." );
  #endif

  if ( get_param( params, "views" ) )
  {
    fMatch = 1;
    free_all_views();
  }

  if ( get_param( params, "templates" ) )
  {
    fMatch = 1;
    free_all_lyphplates();
  }

  if ( get_param( params, "graph" ) )
  {
    fMatch = 1;
    free_all_lyphs();
  }

  if ( get_param( params, "located_measures" ) )
  {
    fMatch = 1;
    free_all_located_measures();
  }

  if ( get_param( params, "correlations" ) )
  {
    fMatch = 1;
    free_all_correlations();
  }

  if ( !fMatch )
    HND_ERR( "You did not specify anything to delete (options are 'views', 'templates', and 'graph')" );

  send_ok( req );
}

void default_config_values( void )
{
  configs.readonly = 0;
}

int parse_commandline_args( int argc, const char *argv[], const char **filename, int *port )
{
  argc--;
  argv++;

  if ( !(argc % 2) )
  {
    parse_commandline_args_help:
    printf( "Syntax: lyph [optional arguments] <file>\n" );
    printf( "\n" );
    printf( "Optional arguments are as follows:\n" );
    printf( "  -p <portnum>\n" );
    printf( "    Specify which port to listen (default: 5052)\n" );
    printf( "\n" );
    printf( "  -readonly <yes or no>\n" );
    printf( "    Specify whether to run in read-only mode (default: no)\n" );
    printf( "  -help\n" );
    printf( "    Displays this helpfile\n" );
    printf( "\n" );

    return 0;
  }

  for ( ; argc > 1; argc -= 2, argv += 2 )
  {
    const char *param = argv[0];

    while ( *param == '-' )
      param++;

    param = lowercaserize( param );

    if ( !strcmp( param, "p" ) )
    {
      int portnum = strtoul( argv[1], NULL, 10 );

      if ( portnum < 1 )
      {
        printf( "Portnum must be a positive integer\n" );
        return 0;
      }

      *port = portnum;
      printf( "LYPH has been set to listen on port %d\n", portnum );

      continue;
    }

    if ( !strcmp( param, "readonly" ) )
    {
      if ( !strcmp( argv[1], "yes" ) )
      {
        configs.readonly = 1;
        printf( "LYPH has been set to run in read-only mode\n" );
        continue;
      }
      if ( !strcmp( argv[1], "no" ) )
      {
        configs.readonly = 0;
        printf( "LYPH has been set to run in read-and-write mode\n" );
        continue;
      }
      printf( "Valid options for 'readonly' are 'yes' or 'no'\n" );
      return 0;
    }

    goto parse_commandline_args_help;
  }

  *filename = argv[0];

  return 1;
}

void send_ok( http_request *req )
{
  send_response( req, JSON1( "Response": "OK" ) );
}
