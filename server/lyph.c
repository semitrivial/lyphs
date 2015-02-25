#include "lyph.h"
#include "nt_parse.h"

char *lyph_type_as_char( lyph *L );
void save_one_lyphview( lyphview *v, FILE *fp );
int is_duplicate_view( lyphview *v, lyphnode **nodes, char **coords );
int new_lyphview_id(void);
trie *new_lyphedge_id(lyphedge *e);
lyphnode *lyphnode_by_id_or_new( char *id );
trie *parse_lyphedge_name_field( char *namebuf, lyphedge *e );
lyphedge *find_duplicate_lyphedge( int type, lyphnode *from, lyphnode *to, lyph *L, trie *fma, char *namestr );
lyphedge *find_duplicate_lyphedge_recurse( trie *t, int type, lyphnode *from, lyphnode *to, lyph *L, trie *fma, trie *name );
void maybe_update_top_id( int *top, char *idstr );
trie *new_lyphnode_id(lyphnode *n);
lyph *lyph_by_ont_term_recurse( trie *term, trie *t );
lyph **parse_lyphedge_constraints( char *str );
int edge_passes_filter( lyphedge *e, edge_filter *f );

int top_layer_id;
int top_lyph_id;
int top_lyphedge_id;
int top_lyphnode_id;
trie *blank_nodes;

lyphview **views;
lyphview obsolete_lyphview;
int top_view;

int lyphnode_to_json_flags;
int exit_to_json_flags;

lyphview *create_new_view( lyphnode **nodes, char **coords, char *name )
{
  lyphnode **inptr, **out;
  lyphview *v, **vbuf;
  char **cinptr, **coords_out, **coutptr;
  int ncnt;

  for ( inptr = nodes; *inptr; inptr++ )
    ;

  ncnt = inptr - nodes;

  CREATE( out, lyphnode *, ncnt + 1 );
  memcpy( out, nodes, (ncnt + 1) * sizeof(lyphnode *) );

  CREATE( coords_out, char *, (ncnt * 2) + 1 );

  for ( cinptr = coords, coutptr = coords_out; *cinptr; cinptr++ )
    *coutptr++ = strdup( *cinptr );

  *coutptr = NULL;

  CREATE( v, lyphview, 1 );
  v->nodes = out;
  v->coords = coords_out;
  v->id = new_lyphview_id();
  v->name = name;

  CREATE( vbuf, lyphview *, top_view + 3 );
  memcpy( vbuf, views, (top_view + 1) * sizeof(lyphview *) );
  vbuf[top_view + 1] = v;
  vbuf[top_view + 2] = NULL;
  top_view++;
  free( views );
  views = vbuf;

  save_lyphviews();

  return v;
}

int new_lyphview_id(void)
{
  return top_view+1;
}

lyphview *search_duplicate_view( lyphnode **nodes, char **coords, char *name )
{
  lyphview **v;

  for ( v = views; *v; v++ )
  {
    if ( *v == &obsolete_lyphview )
      continue;

    if ( name && ( !(*v)->name || strcmp( (*v)->name, name ) ) )
      continue;

    if ( is_duplicate_view( *v, nodes, coords ) )
      return *v;
  }

  return NULL;
}

int is_duplicate_view( lyphview *v, lyphnode **nodes, char **coords )
{
  lyphnode **vnodespt = v->nodes, **nodespt = nodes;
  char **vcoordspt = v->coords, **coordspt = coords;

  for ( ; *vnodespt; vnodespt++ )
  {
    if ( *nodespt++ != *vnodespt )
      return 0;

    if ( strcmp( *coordspt++, *vcoordspt++ ) )
      return 0;

    if ( strcmp( *coordspt++, *vcoordspt++ ) )
      return 0;
  }

  if ( *nodespt )
    return 0;

  return 1;
}

void strip_lyphs_from_graph( trie *t )
{
  if ( t->data )
  {
    lyphedge *e = (lyphedge *)t->data;

    e->lyph = NULL;

    if ( *e->constraints )
    {
      free( e->constraints );
      CREATE( e->constraints, lyph *, 1 );
      *e->constraints = NULL;
    }
  }

  TRIE_RECURSE( strip_lyphs_from_graph( *child ) );
}

void free_all_edges( void )
{
  /*
   * Memory leak here is deliberate: this function should only
   * be called on the devport, not on production.
   */
  lyphnode_ids = blank_trie();
  lyphedge_ids = blank_trie();
  lyphedge_names = blank_trie();
  lyphedge_fmas = blank_trie();

  top_lyphedge_id = 0;

  free_all_views();

  save_lyphedges();
}

void free_all_lyphs( void )
{
  /*
   * Memory leak here is deliberate: this function should only be
   * called on the devport, not on production.
   */
  lyph_ids = blank_trie();
  lyph_names = blank_trie();
  layer_ids = blank_trie();
  save_lyphs();

  top_lyph_id = 0;
  top_layer_id = 0;

  strip_lyphs_from_graph( lyphedge_ids );
  save_lyphedges();
}

void free_view( lyphview *v )
{
  if ( v->nodes )
    free( v->nodes );

  if ( v->coords )
    free( v->coords );

  free( v );
}

void free_all_views( void )
{
  int i;

  for ( i = 0; i < top_view; i++ )
  {
    if ( views[i] && views[i] != &obsolete_lyphview )
      free_view( views[i] );
  }

  free( views );
  top_view = 0;

  CREATE( views, lyphview *, 2 );
  views[0] = &obsolete_lyphview;
  views[1] = NULL;

  save_lyphviews();
}

lyphview *lyphview_by_id( char *idstr )
{
  int id = strtol( idstr, NULL, 10 );

  if ( id < 0 || id > top_view )
    return NULL;

  if ( views[id] == &obsolete_lyphview )
    return NULL;

  return views[id];
}

char *viewed_node_to_json( viewed_node *vn )
{
  return lyphnode_to_json_wrappee( vn->node, vn->x, vn->y );
}

char *lyphview_to_json( lyphview *v )
{
  lyphnode **n;
  char *result, **coords;
  viewed_node **vn, **vnptr;

  CREATE( vn, viewed_node *, VOIDLEN( v->nodes ) + 1 );
  vnptr = vn;
  coords = v->coords;

  for ( n = v->nodes; *n; n++ )
  {
    CREATE( *vnptr, viewed_node, 1 );
    (*vnptr)->node = *n;
    (*vnptr)->x = coords[0];
    (*vnptr)->y = coords[1];
    coords += 2;
    vnptr++;
  }
  *vnptr = NULL;

  for ( n = v->nodes; *n; n++ )
    SET_BIT( (*n)->flags, LYPHNODE_SELECTED );

  lyphnode_to_json_flags = LTJ_EXITS | LTJ_SELECTIVE | LTJ_FULL_EXIT_DATA;

  result = JSON
  (
    "id": int_to_json( v->id ),
    "name": v->name,
    "nodes": JS_ARRAY( viewed_node_to_json, vn )
  );

  for ( vnptr = vn; *vnptr; vnptr++ )
    free( *vnptr );

  free( vn );

  lyphnode_to_json_flags = 0;

  for ( n = v->nodes; *n; n++ )
    REMOVE_BIT( (*n)->flags, LYPHNODE_SELECTED );

  return result;
}

void save_lyphviews( void )
{
  FILE *fp;
  lyphview **ptr;

  if ( !views )
    return;

  fp = fopen( "lyphviews.dat", "w" );

  if ( !fp )
  {
    log_string( "Error: Could not open lyphviews.dat for writing" );
    return;
  }

  for ( ptr = &views[1]; *ptr; ptr++ )
    ;

  fprintf( fp, "TopView %d\n", (int) (ptr - views) );

  for ( ptr = &views[1]; *ptr; ptr++ )
  {
    if ( *ptr != &obsolete_lyphview )
      save_one_lyphview( *ptr, fp );
  }

  fclose( fp );
}

void save_one_lyphview( lyphview *v, FILE *fp )
{
  lyphnode **n;
  char **c;

  fprintf( fp, "View %d\n", v->id );

  for ( n = v->nodes; *n; n++ )
    ;

  if ( v->name )
    fprintf( fp, "Name %s\n", v->name );

  fprintf( fp, "Nodes %d\n", (int) (n - v->nodes) );

  for ( n = v->nodes, c = v->coords; *n; n++ )
  {
    fprintf( fp, "N %s %s %s\n", trie_to_static( (*n)->id ), c[0], c[1] );
    c = &c[2];
  }
}

void init_default_lyphviews( void )
{
  top_view = 0;

  CREATE( views, lyphview *, 1 );

  views[0] = NULL;
}

void load_lyphviews( void )
{
  FILE *fp;
  lyphview *v;
  lyphnode **nodes;
  char **coords;
  char buf[MAX_STRING_LEN], *bptr, c;
  int cnt, line = 1, prev_view_index = -1, id = -1;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( "lyphviews.dat", "r" );

  if ( !fp )
  {
    log_string( "Could not open lyphviews.dat for reading--no lyph views loaded" );
    init_default_lyphviews();
    return;
  }

  QUICK_GETLINE( buf, bptr, c, fp );

  if ( !bptr )
  {
    log_string( "lyphviews.dat appears to be blank or start with a blank line--no lyph views loaded" );
    init_default_lyphviews();
    fclose(fp);
    return;
  }

  if ( !str_begins( buf, "TopView " ) )
  {
    log_string( "lyphviews.dat does not appear to have the expected format (no initial TopView line)--no lyph views loaded" );
    init_default_lyphviews();
    fclose(fp);
    return;
  }

  bptr = &buf[strlen("TopView ")];

  cnt = strtol( bptr, NULL, 10 );

  if ( cnt < 1 )
  {
    log_string( "lyphviews.dat: top_view is not a positive integer--no lyphviews loaded" );
    init_default_lyphviews();
    fclose(fp);
    return;
  }

  top_view = cnt;

  CREATE( views, lyphview *, cnt + 2 );

  for ( cnt = 0; cnt <= top_view; cnt++ )
    views[cnt] = &obsolete_lyphview;

  views[cnt] = NULL;

  for ( ; ; )
  {
    QUICK_GETLINE( buf, bptr, c, fp );
    line++;

    if ( !bptr )
      break;

    if ( str_begins( buf, "View " ) )
    {
      char *idstr = &buf[strlen("View ")];

      id = strtol( idstr, NULL, 10 );

      if ( id < 1 )
      {
        log_stringf( "lyphviews.dat: view id (%s) is not a positive integer -- aborting", idstr );
        log_linenum( line );
        EXIT();
      }

      if ( id > top_view )
      {
        log_stringf( "lyphviews.dat: view id (%s) is higher than top_view -- aborting", idstr );
        log_linenum( line );
        EXIT();
      }

      if ( prev_view_index != -1 )
      {
        if ( !views[prev_view_index]->nodes )
        {
          log_string( "lyphviews.dat: previous view did not finish loading -- aborting" );
          log_linenum( line );
          EXIT();
        }

        *nodes = NULL;
        *coords = NULL;
      }

      prev_view_index = id;

      CREATE( v, lyphview, 1 );
      v->id = id;
      v->nodes = NULL;
      v->coords = NULL;
      v->name = NULL;

      views[id] = v;
      continue;
    }

    if ( str_begins( buf, "Name " ) )
    {
      if ( v->name )
        free( v->name );

      v->name = strdup( &buf[strlen("Name ")] );
      continue;
    }

    if ( str_begins( buf, "Nodes " ) )
    {
      int ncnt;

      if ( prev_view_index == -1 )
      {
        log_string( "lyphviews.dat: Mismatched Nodes line -- aborting" );
        log_linenum( line );
        EXIT();
      }

      ncnt = strtol( &buf[strlen("Nodes ")], NULL, 10 );

      if ( ncnt < 0 )
      {
        log_string( "lyphviews.dat: Number of nodes is not a nonnegative integer -- aborting" );
        log_linenum( line );
        EXIT();
      }

      CREATE( views[id]->nodes, lyphnode *, ncnt + 1 );
      CREATE( views[id]->coords, char *, (ncnt * 2) + 1 );
      nodes = views[id]->nodes;
      coords = views[id]->coords;

      continue;
    }

    if ( str_begins( buf, "N " ) )
    {
      char *left, *ptr;
      int fNodeID = 0, fXCoord = 0, fYCoord = 0, fEnd = 0;

      if ( prev_view_index == -1 || !views[id]->nodes )
      {
        log_string( "lyphviews.dat: Mismatched N line -- aborting" );
        log_linenum( line );
        EXIT();
      }

      left = &buf[strlen("N ")];

      for ( ptr = left; !fEnd; ptr++ )
      {
        if ( !*ptr || *ptr == ' ' )
        {
          if ( *ptr )
            *ptr = '\0';
          else
            fEnd = 1;

          if ( !fNodeID )
          {
            trie *nodetr;
            lyphnode *node;

            fNodeID = 1;

            nodetr = trie_strdup( left, lyphnode_ids );

            if ( !nodetr->data )
            {
              CREATE( node, lyphnode, 1 );
              node->id = nodetr;
              nodetr->data = (trie **)node;
              node->flags = 0;
              CREATE( node->exits, exit_data *, 1 );
              node->exits[0] = NULL;
              maybe_update_top_id( &top_lyphnode_id, left );
            }
            else
              node = (lyphnode *)nodetr->data;

            *nodes++ = node;
          }
          else if ( !fXCoord )
          {
            *coords++ = strdup( left );
            fXCoord = 1;
          }
          else if ( !fYCoord )
          {
            *coords++ = strdup( left );
            fYCoord = 1;
          }
          else
          {
            log_string( "lyphviews.dat: too many entries on line -- aborting" );
            log_linenum( line );
            EXIT();
          }

          left = &ptr[1];
        }
      }

      continue;
    }

    log_string( "lyphviews.dat: Unrecognized line -- aborting" );
    log_linenum( line );
    EXIT();
  }

  if ( prev_view_index != -1 )
  {
    if ( !views[prev_view_index]->nodes )
    {
      log_string( "lyphviews.dat: previous view did not finish loading -- aborting" );
      log_linenum( line );
      EXIT();
    }

    *nodes = NULL;
    *coords = NULL;
  }

  fclose(fp);
}

int load_lyphedges( void )
{
  FILE *fp;
  int line = 1;
  char c, row[MAX_LYPHEDGE_LINE_LEN], *rptr = row, *end = &row[MAX_LYPHEDGE_LINE_LEN - 1], *err = NULL;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  log_string( "Loading lyphedges..." );

  fp = fopen( "lyphedges.dat", "r" );

  if ( !fp )
  {
    log_string( "Could not open lyphedges.dat for reading" );
    return 0;
  }

  for ( ; ; )
  {
    QUICK_GETC( c, fp );

    if ( !c || c == '\n' || c == '\r' )
    {
      if ( rptr == row )
      {
        if ( c )
        {
          line++;
          continue;
        }
        else
          return 1;
      }

      *rptr = '\0';

      if ( !load_lyphedges_one_line( row, &err ) )
      {
        log_string( "Error while loading lyphedges file." );
        log_linenum( line );
        log_string( err );

        return 0;
      }

      if ( !c )
        return 1;

      line++;
      rptr = row;
    }
    else
    {
      if ( rptr >= end )
      {
        log_string( "Error while loading lyphedges file." );
        log_linenum( line );
        log_string( "Line exceeds maximum length" );

        return 0;
      }

      *rptr++ = c;
    }
  }

  return 1;
}

void save_lyphedges( void )
{
  FILE *fp = fopen( "lyphedges.dat", "w" );

  if ( !fp )
  {
    log_string( "Could not open lyphedges.dat for writing" );
    return;
  }

  save_lyphedges_recurse( lyphedge_ids, fp );

  fclose( fp );
}

void save_lyphedges_recurse( trie *t, FILE *fp )
{
  if ( t->data )
  {
    lyphedge *e = (lyphedge *)t->data;

    fprintf( fp, "%s\t", trie_to_static( e->id ) );
    fprintf( fp, "%d\t%s\t", e->type, e->fma ? trie_to_static( e->fma ) : "(nofma)" );
    fprintf( fp, "%s\t", trie_to_static( e->from->id ) );
    fprintf( fp, "%s\t", trie_to_static( e->to->id ) );

    if ( e->lyph )
      fprintf( fp, "lyph:%s ", trie_to_static( e->lyph->id ) );

    if ( *e->constraints )
      fprintf( fp, "constraints:%s ", constraints_comma_list( e->constraints ) );

    fprintf( fp, "%s\n", e->name ? trie_to_static( e->name ) : "(noname)" );
  }

  TRIE_RECURSE( save_lyphedges_recurse( *child, fp ) );
}

int load_lyphedges_one_line( char *line, char **err )
{
  char edgeidbuf[MAX_LYPHEDGE_LINE_LEN+1];
  char typebuf[MAX_LYPHEDGE_LINE_LEN+1];
  char fmabuf[MAX_LYPHEDGE_LINE_LEN+1];
  char frombuf[MAX_LYPHEDGE_LINE_LEN+1];
  char tobuf[MAX_LYPHEDGE_LINE_LEN+1];
  char namebuf[MAX_LYPHEDGE_LINE_LEN+1];
  lyphedge *e;
  lyphnode *from, *to;
  trie *etr, *fromtr, *totr;

  #ifdef KEY
  #undef KEY
  #endif
  #define KEY( dest, errmsg )\
    do\
    {\
      if ( !word_from_line( &line, (dest) ) )\
      {\
        *err = (errmsg);\
        return 0;\
      }\
    }\
    while(0)

  KEY( edgeidbuf, "Missing edge ID" );
  KEY( typebuf, "Missing edge type" );
  KEY( fmabuf, "Missing FMA ID" );
  KEY( frombuf, "Missing initial node ID" );
  KEY( tobuf, "Missing terminal node ID" );
  KEY( namebuf, "Missing description" );

  etr = trie_strdup( edgeidbuf, lyphedge_ids );

  if ( !etr->data )
  {
    CREATE( e, lyphedge, 1 );
    e->id = etr;
    etr->data = (trie **)e;

    maybe_update_top_id( &top_lyphedge_id, edgeidbuf );

    CREATE( e->constraints, lyph *, 1 );
    *e->constraints = NULL;
  }
  else
    e = (lyphedge *)etr->data;

  e->type = strtol( typebuf, NULL, 10 );

  if ( e->type < 1 || e->type > 4 )
  {
    *err = "Invalid edge type";
    return 0;
  }

  e->fma = strcmp( fmabuf, "(nofma)" ) ? trie_strdup( fmabuf, lyphedge_fmas ) : NULL;

  fromtr = trie_strdup( frombuf, lyphnode_ids );

  if ( !fromtr->data )
  {
    CREATE( from, lyphnode, 1 );

    from->id = fromtr;
    fromtr->data = (trie **)from;
    from->flags = 0;
    CREATE( from->exits, exit_data *, 1 );
    from->exits[0] = NULL;

    maybe_update_top_id( &top_lyphnode_id, frombuf );
  }
  else
    from = (lyphnode *)fromtr->data;

  totr = trie_strdup( tobuf, lyphnode_ids );

  if ( !totr->data )
  {
    CREATE( to, lyphnode, 1 );

    to->id = totr;
    totr->data = (trie **)to;
    to->flags = 0;
    CREATE( to->exits, exit_data *, 1 );
    to->exits[0] = NULL;

    maybe_update_top_id( &top_lyphnode_id, tobuf );
  }
  else
    to = (lyphnode *)totr->data;

  e->lyph = NULL;
  e->name = parse_lyphedge_name_field( namebuf, e );

  e->from = from;
  e->to = to;

  add_exit( e, from );

  return 1;
}

int parse_name_preamble( char **name, char *needle, char **dest )
{
  char *ptr;

  if ( !str_begins( *name, needle ) )
    return 0;

  *name += strlen(needle);

  for ( ptr = *name; *ptr; ptr++ )
    if ( *ptr == ' ' )
      break;

  if ( !*ptr )
  {
    log_stringf( "Expected ' ' after %s", needle );
    return 0;
  }

  *ptr = '\0';
  *dest = *name;
  *name = &ptr[1];
  return 1;
}

trie *parse_lyphedge_name_field( char *namebuf, lyphedge *e )
{
  char *preamble;

  if ( parse_name_preamble( &namebuf, "lyph:", &preamble ) )
  {
    lyph *L = lyph_by_id( preamble );

    if ( !L )
      return NULL;

    e->lyph = L;
  }

  if ( parse_name_preamble( &namebuf, "constraints:", &preamble ) )
    e->constraints = parse_lyphedge_constraints( preamble );

  if ( !strcmp( namebuf, "(noname)" ) )
    return NULL;

  return trie_strdup( namebuf, lyphedge_names );
}

void add_exit( lyphedge *e, lyphnode *n )
{
  lyphnode *to;
  exit_data **x;

  if ( e->to == n )
  {
    if ( e->from == n )
      return;

    to = e->from;
  }
  else
    to = e->to;

  if ( n->exits )
  {
    int size;

    for ( x = n->exits; *x; x++ )
      if ( (*x)->to == to )
      {
        /*
         * Possible future to-do: keep track of ALL edges associated with an exit, not just one
         */
        return;
      }

    size = x - n->exits;

    CREATE( x, exit_data *, size + 2 );

    if ( size )
      memcpy( x, n->exits, size * sizeof(exit_data *) );

    CREATE( x[size], exit_data, 1 );
    x[size]->to = to;
    x[size]->via = e;
    x[size+1] = NULL;

    free( n->exits );
    n->exits = x;
  }
  else
  {
    CREATE( x, exit_data *, 2 );
    CREATE( x[0], exit_data, 1 );
    x[0]->to = to;
    x[0]->via = e;
    x[1] = NULL;

    n->exits = x;
  }
}

char *lyphedge_type_str( int type )
{
  switch( type )
  {
    case LYPHEDGE_ARTERIAL:
      return "arterial";
    case LYPHEDGE_MICROCIRC:
      return "microcirculation";
    case LYPHEDGE_VENOUS:
      return "venous";
    case LYPHEDGE_CARDIAC:
      return "cardiac-chamber";
    default:
      return "unknown";
  }
}

int parse_lyph_type_str( char *type )
{
  if ( !strcmp( type, "arterial" ) )
    return LYPHEDGE_ARTERIAL;
  if ( !strcmp( type, "microcirculation" ) )
    return LYPHEDGE_MICROCIRC;
  if ( !strcmp( type, "venous" ) )
    return LYPHEDGE_VENOUS;
  if ( !strcmp( type, "cardiac-chamber" ) )
    return LYPHEDGE_CARDIAC;

  return -1;
}

int word_from_line( char **line, char *buf )
{
  char *lptr = *line, *bptr = buf;

  for ( ; ; )
  {
    switch( *lptr )
    {
      case '\n':
      case '\r':
        return 0;

      case '\t':
      case '\0':
        if ( bptr == buf )
          return 0;

        *bptr = '\0';
        if ( *lptr )
          *line = &lptr[1];
        else
          *line = lptr;

        return 1;

      default:
        *bptr++ = *lptr++;
        continue;
    }
  }
}

void got_lyph_triple( char *subj, char *pred, char *obj )
{
  char *s = subj, *p = pred, *o = obj;

  if ( (*subj == '"' || *subj == '<') && s++ )
    subj[strlen(subj)-1] = '\0';

  if ( (*pred == '"' || *pred == '<') && p++ )
    pred[strlen(pred)-1] = '\0';

  if ( (*obj == '"' || *obj == '<') && o++ )
    obj[strlen(obj)-1] = '\0';

  if ( !strcmp( p, "http://www.w3.org/2000/01/rdf-schema#label" ) )
    load_lyph_label( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#lyph_type" ) )
    load_lyph_type( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#ont_term" ) )
    load_ont_term( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#has_layers" ) )
    acknowledge_has_layers( s, o );
  else if ( str_begins( p, "http://www.w3.org/1999/02/22-rdf-syntax-ns#_" ) )
    load_layer_to_lld( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#has_material" ) )
    load_layer_material( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#has_thickness" ) )
    load_layer_thickness( s, o );

  free( subj );
  free( pred );
  free( obj );
}

void load_ont_term( char *subj_full, char *ont_term_str )
{
  char *subj = get_url_shortform( subj_full );
  trie *ont_term = trie_search( ont_term_str, superclasses );
  trie *iri;

  if ( !ont_term )
  {
    char *errmsg = strdupf( "Could not find ont_term: %s\n", ont_term_str );
    log_string( errmsg );
    free( errmsg );
    return;
  }

  iri = trie_search( subj, lyph_ids );

  if ( !iri )
    return;

  ((lyph*)iri->data)->ont_term = ont_term;
}

void load_lyph_type( char *subj_full, char *type_str )
{
  char *subj = get_url_shortform( subj_full );
  int type = parse_lyph_type( type_str );
  trie *iri;

  if ( type != -1 )
    iri = trie_search( subj, lyph_ids );

  if ( type == -1 || !iri )
    return;

  ((lyph *)iri->data)->type = type;
}

void load_lyph_label( char *subj_full, char *label )
{
  char *subj = get_url_shortform( subj_full );
  trie *iri;

  if ( str_begins( subj, "LYPH_" ) )
  {
    lyph *L;

    maybe_update_top_id( &top_lyph_id, subj + strlen("LYPH_") );

    iri = trie_strdup( subj, lyph_ids );

    if ( !iri->data )
    {
      CREATE( L, lyph, 1 );
      L->id = iri;
      L->type = LYPH_MISSING;
      L->layers = NULL;
      L->supers = NULL;
      L->subs = NULL;
      L->ont_term = NULL;
      iri->data = (void *)L;
    }
    else
      L = (lyph *)iri->data;

    L->name = trie_strdup( label, lyph_names );
    L->name->data = (void *)L;
  }
}

void acknowledge_has_layers( char *subj_full, char *bnode_id )
{
  char *subj = get_url_shortform( subj_full );
  trie *iri = trie_search( subj, lyph_ids );
  trie *bnode;
  load_layers_data *lld;

  if ( !iri || !iri->data )
    return;

  bnode = trie_strdup( bnode_id, blank_nodes );

  CREATE( lld, load_layers_data, 1 );
  lld->subj = (lyph *)iri->data;
  lld->first_layer_loading = NULL;
  lld->last_layer_loading = NULL;
  lld->layer_count = 0;

  bnode->data = (trie **)lld;
}

void load_layer_to_lld( char *bnode, char *obj_full )
{
  load_layers_data *lld;
  trie *lyr_trie;
  layer *lyr;
  char *obj;
  layer_loading *loading;
  trie *t = trie_search( bnode, blank_nodes );

  if ( !t || !t->data )
    return;

  obj = get_url_shortform( obj_full );

  lyr_trie = trie_search( obj, layer_ids );

  if ( lyr_trie )
    lyr = (layer *)lyr_trie->data;
  else
  {
    CREATE( lyr, layer, 1 );
    lyr->id = trie_strdup( obj, layer_ids );
    lyr->id->data = (trie **)lyr;
    lyr->thickness = -1;
    maybe_update_top_id( &top_layer_id, obj + strlen( "LAYER_" ) );
  }

  CREATE( loading, layer_loading, 1 );
  loading->lyr = lyr;

  lld = (load_layers_data *)t->data;

  LINK( loading, lld->first_layer_loading, lld->last_layer_loading, next );
  lld->layer_count++;
}

void load_layer_material( char *subj_full, char *obj_full )
{
  char *subj = get_url_shortform( subj_full );
  char *obj = get_url_shortform( obj_full );
  layer *lyr = layer_by_id( subj );
  lyph *mat;

  if ( !lyr )
    return;

  mat = lyph_by_id( obj );

  if ( !mat )
    return;

  lyr->material = mat;
}

void load_layer_thickness( char *subj_full, char *obj )
{
  char *subj = get_url_shortform( subj_full );
  trie *t = trie_search( subj, layer_ids );
  layer *lyr;

  if ( !t || !t->data )
    return;

  lyr = (layer *)t->data;

  lyr->thickness = strtol( obj, NULL, 10 );
}

void load_lyphs(void)
{
  FILE *fp;
  char *err = NULL;
  lyph *naked;

  fp = fopen( "lyphs.dat", "r" );

  if ( !fp )
  {
    log_string( "Could not open lyphs.dat for reading" );
    return;
  }

  blank_nodes = blank_trie();

  if ( !parse_ntriples( fp, &err, MAX_IRI_LEN, got_lyph_triple ) )
  {
    char *buf = malloc(strlen(err) + 1024);

    sprintf( buf, "Failed to parse the lyphs-file (lyphs.dat):\n%s\n", err ? err : "(no error given)" );

    error_message( buf );
    EXIT();
  }

  handle_loaded_layers( blank_nodes );

  if ( (naked=missing_layers( lyph_ids )) != NULL )
  {
    char buf[1024 + MAX_IRI_LEN];

    sprintf( buf, "Error in lyphs.dat: lyph %s has type %s but has no layers\n", trie_to_static( naked->id ), lyph_type_as_char( naked ) );
    error_message( buf );
    EXIT();
  }
}

lyph *missing_layers( trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( ( L->type == LYPH_MIX || L->type == LYPH_SHELL ) && ( !L->layers || !L->layers[0] ) )
      return L;
  }

  TRIE_RECURSE
  (
    lyph *L = missing_layers( *child );

    if ( L )
      return L;
  );

  return NULL;
}

void handle_loaded_layers( trie *t )
{
  if ( t->data )
  {
    load_layers_data *lld = (load_layers_data *)t->data;
    layer_loading *load, *load_next;
    lyph *L = lld->subj;
    layer **lyrs, **lptr;

    CREATE( lyrs, layer *, lld->layer_count + 1 );
    lptr = lyrs;

    for ( load = lld->first_layer_loading; load; load = load_next )
    {
      load_next = load->next;
      *lptr++ = load->lyr;
      free( load );
    }

    *lptr = NULL;
    L->layers = lyrs;
  }

  TRIE_RECURSE( handle_loaded_layers( *child ) );

  if ( t->children )
    free( t->children );

  if ( t->label )
    free( t->label );

  free( t );
}

void save_lyphs(void)
{
  FILE *fp;
  trie *avoid_dupe_layers;

  fp = fopen( "lyphs.dat", "w" );

  if ( !fp )
  {
    log_string( "Could not open lyphs.dat for writing" );
    return;
  }

  avoid_dupe_layers = blank_trie();

  save_lyphs_recurse( lyph_ids, fp, avoid_dupe_layers );

  fclose(fp);

  free_lyphdupe_trie( avoid_dupe_layers );

  return;
}

void save_lyphs_recurse( trie *t, FILE *fp, trie *avoid_dupes )
{
  /*
   * Save in N-Triples format for improved interoperability
   */
  static int bnodes;

  if ( t == lyph_ids )
    bnodes = 0;

  if ( t->data )
  {
    lyph *L = (lyph *)t->data;
    char *ch, *id;

    id = id_as_iri( t );
    ch = html_encode( trie_to_static( L->name ) );

    fprintf( fp, "%s <http://www.w3.org/2000/01/rdf-schema#label> \"%s\" .\n", id, ch );
    free( ch );

    fprintf( fp, "%s <http://open-physiology.org/lyph#lyph_type> \"%s\" .\n", id, lyph_type_as_char( L ) );

    if ( L->ont_term )
      fprintf( fp, "%s <http://open-physiology.org/lyph#ont_term> \"%s\" .\n", id, trie_to_static(L->ont_term) );

    if ( L->type == LYPH_SHELL || L->type == LYPH_MIX )
    {
      layer **lyrs;
      int cnt = 1;

      fprintf( fp, "%s <http://open-physiology.org/lyph#has_layers> _:node%d .\n", id, ++bnodes );
      fprintf( fp, "_:node%d <http://www.w3.org/1999/02/22-rdf-syntax-ns#type> <http://www.w3.org/1999/02/22-rdf-syntax-ns#Seq> .\n", bnodes );

      for ( lyrs = L->layers; *lyrs; lyrs++ )
        fprintf_layer( fp, *lyrs, bnodes, cnt++, avoid_dupes );
    }

    free( id );
  }

  TRIE_RECURSE( save_lyphs_recurse( *child, fp, avoid_dupes ) );
}

void fprintf_layer( FILE *fp, layer *lyr, int bnodes, int cnt, trie *avoid_dupes )
{
  char *lid = id_as_iri( lyr->id );
  trie *dupe_search;
  char *mat_iri;

  fprintf( fp, "_:node%d <http://www.w3.org/1999/02/22-rdf-syntax-ns#_%d> %s .\n", bnodes, cnt, lid );

  dupe_search = trie_search( lid, avoid_dupes );

  if ( dupe_search && dupe_search->data )
  {
    free( lid );
    return;
  }

  trie_strdup( lid, avoid_dupes );

  mat_iri = id_as_iri( lyr->material->id );
  fprintf( fp, "%s <http://open-physiology.org/lyph#has_material> %s .\n", lid, mat_iri );
  free( mat_iri );

  if ( lyr->thickness != -1 )
    fprintf( fp, "%s <http://open-physiology.org/lyph#has_thickness> \"%d\" .\n", lid, lyr->thickness );

  free( lid );
}

char *id_as_iri( trie *id )
{
  char *iri = "<http://open-physiology.org/lyphs/#%s>";
  char *retval;
  char *tmp = trie_to_static( id );

  CREATE( retval, char, strlen(iri) + strlen(tmp) + 1 );

  sprintf( retval, iri, tmp );

  return retval;
}

lyph *lyph_by_layers( int type, layer **layers, char *name )
{
  lyph *L;

  if ( type == LYPH_MIX )
    sort_layers( layers );

  L = lyph_by_layers_recurse( type, layers, lyph_ids );

  if ( !L )
  {
    if ( !name )
      return NULL;

    CREATE( L, lyph, 1 );
    L->name = trie_strdup( name, lyph_names );
    L->id = assign_new_lyph_id( L );
    L->type = type;
    L->layers = copy_layers( layers );
    L->ont_term = NULL;
    L->supers = NULL;
    compute_lyph_hierarchy_one_lyph( L );
    add_lyph_as_super( L, lyph_ids );

    save_lyphs();
  }

  return L;
}

lyph *lyph_by_layers_recurse( int type, layer **layers, trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( L->type == type && same_layers( L->layers, layers ) )
      return L;
  }

  TRIE_RECURSE
  (
    lyph *L = lyph_by_layers_recurse( type, layers, *child );

    if ( L )
      return L;
  );

  return NULL;
}

int same_layers( layer **x, layer **y )
{
  for ( ; ; )
  {
    if ( *x != *y )
      return 0;

    if ( !*x )
      return 1;

    x++;
    y++;
  }
}

layer *layer_by_description( char *mtid, int thickness )
{
  lyph *L = lyph_by_id( mtid );
  layer *lyr;

  if ( !L )
    return NULL;

  if ( thickness < 0 && thickness != -1 )
    return NULL;

  lyr = layer_by_description_recurse( L, thickness, layer_ids );

  if ( !lyr )
  {
    CREATE( lyr, layer, 1 );
    lyr->material = L;
    lyr->id = assign_new_layer_id( lyr );
    lyr->thickness = thickness;

    save_lyphs();
  }

  return lyr;
}

trie *assign_new_lyph_id( lyph *L )
{
  top_lyph_id++;
  char buf[128];
  trie *t;

  sprintf( buf, "LYPH_%d", top_lyph_id );

  t = trie_strdup( buf, lyph_ids );

  t->data = (trie **)L;

  return t;
}

trie *assign_new_layer_id( layer *lyr )
{
  char buf[128];
  trie *t;

  top_layer_id++;
  sprintf( buf, "LAYER_%d", top_layer_id );

  t = trie_strdup( buf, layer_ids );

  t->data = (trie **)lyr;

  return t;
}

int layer_matches( layer *candidate, const lyph *material, const float thickness )
{
  if ( candidate->material != material )
    return 0;

  if ( thickness != -1 && thickness != candidate->thickness )
    return 0;

  return 1;
}

layer *layer_by_description_recurse( const lyph *L, const float thickness, const trie *t )
{
  if ( t->data && layer_matches( (layer *)t->data, L, thickness ) )
      return (layer *)t->data;

  TRIE_RECURSE
  (
    layer *lyr = layer_by_description_recurse( L, thickness, *child );
    if ( lyr )
      return lyr;
  );

  return NULL;
}

layer *layer_by_id( char *id )
{
  trie *t = trie_search( id, layer_ids );

  if ( t && t->data )
    return (layer *) t->data;

  return NULL;
}

lyph *lyph_by_name( char *name )
{
  trie *t = trie_search( name, lyph_names );

  if ( t && t->data )
    return (lyph *) t->data;

  return NULL;
}

lyph *lyph_by_id( char *id )
{
  trie *t = trie_search( id, lyph_ids );

  if ( t && t->data )
    return (lyph *) t->data;

  t = trie_search( id, superclasses );

  if ( t && t->data )
  {
    lyph *L;
    trie *label;

    if ( (L = lyph_by_ont_term( t )) != NULL )
      return L;

    label = trie_search( id, iri_to_labels );

    CREATE( L, lyph, 1 );

    L->type = LYPH_BASIC;
    L->id = assign_new_lyph_id( L );
    L->ont_term = t;
    L->name = trie_strdup( label ? trie_to_static( *label->data ) : id, lyph_names );
    L->layers = NULL;
    L->supers = NULL;

    compute_lyph_hierarchy_one_lyph( L );
    add_lyph_as_super( L, lyph_ids );

    L->id->data = (trie **)L;
    L->name->data = (trie **)L;

    save_lyphs();

    return L;
  }

  return NULL;
}

lyphnode *lyphnode_by_id( char *id )
{
  trie *t = trie_search( id, lyphnode_ids );

  if ( t )
    return (lyphnode *)t->data;

  return NULL;
}

lyphnode *lyphnode_by_id_or_new( char *id )
{
  if ( !strcmp( id, "new" ) )
    return make_lyphnode();
  else
    return lyphnode_by_id( id );
}

lyphedge *lyphedge_by_id( char *id )
{
  trie *t = trie_search( id, lyphedge_ids );

  if ( t )
    return (lyphedge *)t->data;

  return NULL;
}

lyphedge **lyphedges_by_ids( char *ids, char **err )
{
  lyphedge **buf, **bptr, *e;
  char *left, *ptr;
  int commas = count_commas( ids ), fEnd = 0;

  CREATE( buf, lyphedge *, commas + 2 );
  bptr = buf;

  left = ids;
  for ( ptr = ids; ; ptr++ )
  {
    switch ( *ptr )
    {
      case '\0':
        fEnd = 1;
      case ',':
        *ptr = '\0';
        e = lyphedge_by_id( left );

        if ( !e )
        {
          free( buf );
          *err = strdupf( "No lyphedge with id '%s'", left );
          return NULL;
        }

        *bptr++ = e;

        if ( fEnd )
        {
          *bptr = NULL;
          return buf;
        }

        left = &ptr[1];
        break;

      default:
        break;
    }
  }
}

char *lyph_to_json( lyph *L )
{
  return JSON
  (
    "id": trie_to_json( L->id ),
    "name": trie_to_json( L->name ),
    "type": lyph_type_as_char( L ),
    "ont_term": L->ont_term ? trie_to_json( L->ont_term ) : "null",
    "layers": JS_ARRAY( layer_to_json, L->layers )
  );
}

char *layer_to_json( layer *lyr )
{
  return JSON
  (
    "id": trie_to_json( lyr->id ),
    "mtlname": trie_to_json( lyr->material->name ),
    "mtlid": trie_to_json( lyr->material->id ),
    "thickness": lyr->thickness == -1 ? "unspecified" : int_to_json( lyr->thickness )
  );
}

char *lyphnode_to_json( lyphnode *n )
{
  return lyphnode_to_json_wrappee( n, NULL, NULL );
}

char *lyphnode_to_json_wrappee( lyphnode *n, char *x, char *y )
{
  char *retval;

  if ( IS_SET( lyphnode_to_json_flags, LTJ_EXITS ) )
  {
    exit_data **exits;

    if ( IS_SET( lyphnode_to_json_flags, LTJ_FULL_EXIT_DATA ) )
      SET_BIT( exit_to_json_flags, ETJ_FULL_EXIT_DATA );

    if ( IS_SET( lyphnode_to_json_flags, LTJ_SELECTIVE ) )
    {
      exit_data **eptr, **nptr;

      CREATE( exits, exit_data *, VOIDLEN( n->exits ) + 1 );
      for ( eptr = exits, nptr = n->exits; *nptr; nptr++ )
        if ( IS_SET( (*nptr)->to->flags, LYPHNODE_SELECTED ) )
          *eptr++ = *nptr;
      *eptr = NULL;
    }
    else
      exits = n->exits;

    retval = JSON
    (
      "id": trie_to_json( n->id ),
      "exits": JS_ARRAY( exit_to_json, exits ),
      "x": x,
      "y": y
    );

    if ( exits != n->exits )
      free( exits );

    exit_to_json_flags = 0;

    return retval;
  }
  else
    return JSON
    (
      "id": trie_to_json( n->id ),
      "x": x,
      "y": y
    );

}

char *exit_to_json( exit_data *x )
{
  return JSON
  (
    "to": trie_to_json( x->to->id ),
    "via":
      IS_SET( exit_to_json_flags, ETJ_FULL_EXIT_DATA ) ?
      lyphedge_to_json( x->via ) :
      trie_to_json( x->via->id )
  );
}

char *lyphedge_to_json( lyphedge *e )
{
  char *retval;
  int old_LTJ_flags = lyphnode_to_json_flags;
  lyphnode_to_json_flags = 0;

  retval = JSON
  (
    "id": trie_to_json( e->id ),
    "fma": trie_to_json( e->fma ),
    "name": trie_to_json( e->name ),
    "type": int_to_json( e->type ),
    "from": lyphnode_to_json( e->from ),
    "to": lyphnode_to_json( e->to ),
    "lyph": e->lyph ? lyph_to_json( e->lyph ) : NULL,
    "constraints": JS_ARRAY( lyph_to_shallow_json, e->constraints )
  );

  lyphnode_to_json_flags = old_LTJ_flags;
  return retval;
}

char *lyphpath_to_json( lyphedge **path )
{
  return JSON
  (
    "length": int_to_json( VOIDLEN( path ) ),
    "edges": JS_ARRAY( lyphedge_to_json, path )
  );
}

char *lyph_type_as_char( lyph *L )
{
  switch( L->type )
  {
    case LYPH_BASIC:
      return "basic";
    case LYPH_SHELL:
      return "shell";
    case LYPH_MIX:
      return "mix";
    default:
      return "unknown";
  }
}

layer **copy_layers( layer **src )
{
  int len = layers_len( src );
  layer **dest;

  CREATE( dest, layer *, len + 1 );

  memcpy( dest, src, sizeof( layer * ) * (len + 1) );

  return dest;
}

int layers_len( layer **layers )
{
  layer **ptr;

  for ( ptr = layers; *ptr; ptr++ )
    ;

  return ptr - layers;
}

/*
 * cmp_layers could be optimized; right now we'll
 * operate under the assumption that an AU doesn't have
 * hundreds+ of layers, so optimizing this is deprioritized
 */
int cmp_layers(const void * a, const void * b)
{
  const layer *x;
  const layer *y;
  char buf[MAX_STRING_LEN+1];

  if ( a == b )
    return 0;

  x = *((const layer **) a);
  y = *((const layer **) b);

  sprintf( buf, "%s", trie_to_static( x->id ) );

  return strcmp( buf, trie_to_static( y->id ) );
}

void sort_layers( layer **layers )
{
  qsort( layers, layers_len( layers ), sizeof(layer *), cmp_layers );

  return;
}

void free_lyphdupe_trie( trie *t )
{
  TRIE_RECURSE( free_lyphdupe_trie( *child ) );

  if ( t->children )
    free( t->children );

  if ( t->label )
    free( t->label );

  free( t );
}

int parse_lyph_type( char *str )
{
  if ( !strcmp( str, "mix" ) )
    return LYPH_MIX;

  if ( !strcmp( str, "shell" ) )
    return LYPH_SHELL;

  if ( !strcmp( str, "basic" ) )
    return LYPH_BASIC;

  return -1;
}

lyphedge **compute_lyphpath( lyphnode *from, lyphnode *to, edge_filter *filter )
{
  lyphstep *head = NULL, *tail = NULL, *step, *curr;

  if ( from == to )
  {
    lyphedge **path;

    CREATE( path, lyphedge *, 1 );
    path[0] = NULL;

    return path;
  }

  CREATE( step, lyphstep, 1 );
  step->depth = 0;
  step->backtrace = NULL;
  step->location = from;
  step->edge = NULL;

  LINK2( step, head, tail, next, prev );
  curr = step;
  SET_BIT( from->flags, LYPHNODE_SEEN );

  for ( ; ; curr = curr->next )
  {
    exit_data **x;

    if ( !curr )
    {
      free_lyphsteps( head );
      return NULL;
    }

    if ( curr->location == to )
    {
      lyphedge **path, **pptr;

      CREATE( path, lyphedge *, curr->depth + 1 );
      pptr = &path[curr->depth-1];
      path[curr->depth] = NULL;

      do
      {
        *pptr-- = curr->edge;
        curr = curr->backtrace;
      }
      while( curr->backtrace );

      free_lyphsteps( head );
      return path;
    }

    for ( x = curr->location->exits; *x; x++ )
    {
      if ( IS_SET( (*x)->to->flags, LYPHNODE_SEEN ) )
        continue;

      if ( filter && !edge_passes_filter( (*x)->via, filter ) )
        continue;

      CREATE( step, lyphstep, 1 );
      step->depth = curr->depth + 1;
      step->backtrace = curr;
      step->location = (*x)->to;
      step->edge = (*x)->via;
      LINK2( step, head, tail, next, prev );
      SET_BIT( step->location->flags, LYPHNODE_SEEN );
    }
  }

  return NULL;
}

void free_lyphsteps( lyphstep *head )
{
  lyphstep *step, *step_next;

  for ( step = head; step; step = step_next )
  {
    step_next = step->next;

    REMOVE_BIT( step->location->flags, LYPHNODE_SEEN );
    free( step );
  }
}

lyphnode *make_lyphnode( void )
{
  lyphnode *n;

  CREATE( n, lyphnode, 1 );

  n->id = new_lyphnode_id(n);
  n->flags = 0;

  CREATE( n->exits, exit_data *, 1 );
  n->exits[0] = NULL;

  return n;
}

trie *new_lyphnode_id(lyphnode *n)
{
  trie *id;
  char idstr[MAX_INT_LEN+1];

  top_lyphnode_id++;

  sprintf( idstr, "%d", top_lyphnode_id );

  id = trie_strdup( idstr, lyphnode_ids );

  id->data = (trie **)n;

  return id;
}

lyphedge *make_lyphedge( int type, lyphnode *from, lyphnode *to, lyph *L, char *fmastr, char *namestr )
{
  trie *fma;
  lyphedge *e;

  fma = fmastr ? trie_strdup( fmastr, lyphedge_fmas ) : NULL;

  e = find_duplicate_lyphedge( type, from, to, L, fma, namestr );

  if ( e )
    return e;

  CREATE( e, lyphedge, 1 );

  e->id = new_lyphedge_id(e);
  e->name = namestr ? trie_strdup( namestr, lyphedge_names ) : NULL;
  e->type = type;
  e->from = from;
  e->to = to;
  e->lyph = L;
  e->fma = fma;

  CREATE( e->constraints, lyph *, 1 );
  *e->constraints = NULL;

  add_exit( e, from );

  save_lyphedges();

  return e;
}

trie *new_lyphedge_id(lyphedge *e)
{
  trie *id;
  char idstr[MAX_INT_LEN+1];

  top_lyphedge_id++;

  sprintf( idstr, "%d", top_lyphedge_id );

  id = trie_strdup( idstr, lyphedge_ids );

  id->data = (trie **)e;

  return id;
}

lyphedge *find_duplicate_lyphedge( int type, lyphnode *from, lyphnode *to, lyph *L, trie *fma, char *namestr )
{
  trie *name;

  if ( namestr )
  {
    name = trie_search( namestr, lyphedge_names );

    if ( !name )
      return NULL;
  }
  else
    name = NULL;

  return find_duplicate_lyphedge_recurse( lyphedge_ids, type, from, to, L, fma, name );
}

lyphedge *find_duplicate_lyphedge_recurse( trie *t, int type, lyphnode *from, lyphnode *to, lyph *L, trie *fma, trie *name )
{
  if ( t->data )
  {
    lyphedge *e = (lyphedge *)t->data;

    if ( e->name == name
    &&   e->type == type
    &&   e->from == from
    &&   e->to   == to
    &&   e->lyph == L
    &&   e->fma  == fma )
      return e;
  }

  TRIE_RECURSE
  (
    lyphedge *e = find_duplicate_lyphedge_recurse( *child, type, from, to, L, fma, name );

    if ( e )
      return e;
  );

  return NULL;
}

void maybe_update_top_id( int *top, char *idstr )
{
  int id = strtol( idstr, NULL, 10 );

  if ( id > *top )
    *top = id;
}

void lyphs_unset_bits( int bits, trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;
    REMOVE_BIT( L->flags, bits );
  }

  TRIE_RECURSE( lyphs_unset_bits( bits, *child ) );
}

/*
 * To do: optimize this
 */
lyph *lyph_by_ont_term( trie *term )
{
  return lyph_by_ont_term_recurse( term, lyph_ids );
}

lyph *lyph_by_ont_term_recurse( trie *term, trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;
    if ( L->ont_term == term )
      return L;
  }

  TRIE_RECURSE
  (
    lyph *L = lyph_by_ont_term_recurse( term, *child );

    if ( L )
      return L;
  );

  return NULL;
}

lyph **parse_lyphedge_constraints( char *str )
{
  lyph_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyph *L, **buf, **bptr;
  char *ptr, *left = str;
  int fEnd = 0, cnt = 0;

  for ( ptr = str; ; ptr++ )
  {
    switch ( *ptr )
    {
      case '\0':
        fEnd = 1;
      case ',':
        *ptr = '\0';
        L = lyph_by_id( left );

        if ( !L )
          log_stringf( "Unrecognized lyph (%s) while parsing lyph constraints", left );
        else
        {
          CREATE( w, lyph_wrapper, 1 );
          w->L = L;
          LINK( w, head, tail, next );
          cnt++;
        }

        if ( fEnd )
          goto parse_lyphedge_constraints_escape;

        left = &ptr[1];
        break;

      default:
        continue;
    }
  }

  parse_lyphedge_constraints_escape:

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;
    *bptr++ = w->L;
    free( w );
  }

  *bptr = NULL;

  return buf;
}

int can_assign_lyph_to_edge( lyph *L, lyphedge *e, char **err )
{
  lyph **c;

  for ( c = e->constraints; *c; c++ )
  {
    if ( !is_superlyph( *c, L ) )
    {
      *err = strdupf( "That edge is constrained to have lyph a sublyph of %s", trie_to_static( (*c)->id ) );
      return 0;
    }
  }

  return 1;
}

int edge_passes_filter( lyphedge *e, edge_filter *f )
{
  if ( e->lyph )
    return is_superlyph( f->sup, e->lyph );

  if ( !*e->constraints )
    return f->accept_na_edges;
  else
  {
    lyph **c;

    for ( c = e->constraints; *c; c++ )
      if ( !is_superlyph( f->sup, *c ) )
        return 0;

    return 1;
  }
}
