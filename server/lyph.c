#include "lyph.h"
#include "nt_parse.h"

char *lyphplate_type_as_char( lyphplate *L );
void save_one_lyphview( lyphview *v, FILE *fp );
int is_duplicate_view( lyphview *v, lyphnode **nodes, char **coords );
int new_lyphview_id(void);
trie *new_lyph_id(lyph *e);
lyphnode *lyphnode_by_id_or_new( char *id );
trie *parse_lyph_name_field( char *namebuf, lyph *e );
lyph *find_duplicate_lyph( int type, lyphnode *from, lyphnode *to, lyphplate *L, trie *fma, char *namestr );
lyph *find_duplicate_lyph_recurse( trie *t, int type, lyphnode *from, lyphnode *to, lyphplate *L, trie *fma, trie *name );
void maybe_update_top_id( int *top, char *idstr );
trie *new_lyphnode_id(lyphnode *n);
lyphplate *lyphplate_by_ont_term_recurse( trie *term, trie *t );
lyphplate **parse_lyph_constraints( char *str );
int lyph_passes_filter( lyph *e, lyph_filter *f );

int top_layer_id;
int top_lyphplate_id;
int top_lyph_id;
int top_lyphnode_id;
trie *blank_nodes;

lyphview **views;
lyphview obsolete_lyphview;
int top_view;

int lyphnode_to_json_flags;
int exit_to_json_flags;

lyphview *create_new_view( lyphnode **nodes, char **xs, char **ys, lyph **lyphs, char **lxs, char **lys, char **widths, char **heights, char *name )
{
  lyphview *v, **vbuf;
  lv_rect **rects, **rptr, *rect;
  char **coords, **cptr;
  int ncnt = VOIDLEN( nodes ), rcnt = VOIDLEN( lyphs );

  CREATE( v, lyphview, 1 );
  v->nodes = nodes;
  v->id = new_lyphview_id();
  v->name = name;

  CREATE( coords, char *, (ncnt * 2) + 1 );

  for ( cptr = coords; *nodes; nodes++ )
  {
    *cptr++ = strdup( *xs++ );
    *cptr++ = strdup( *ys++ );
  }

  *cptr = NULL;

  v->coords = coords;

  CREATE( rects, lv_rect *, rcnt + 1 );

  for ( rptr = rects; *lyphs; lyphs++ )
  {
    CREATE( rect, lv_rect, 1 );

    rect->L = *lyphs;
    rect->x = strdup( *lxs++ );
    rect->y = strdup( *lys++ );
    rect->width = strdup( *widths++ );
    rect->height = strdup( *heights++ );

    *rptr++ = rect;
  }

  *rptr = NULL;

  v->rects = rects;

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

void strip_lyphplates_from_graph( trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    e->lyphplate = NULL;

    if ( *e->constraints )
    {
      free( e->constraints );
      CREATE( e->constraints, lyphplate *, 1 );
      e->constraints[0] = NULL;
    }
  }

  TRIE_RECURSE( strip_lyphplates_from_graph( *child ) );
}

void free_all_lyphs( void )
{
  /*
   * Memory leak here is deliberate: this function should only
   * be called on the devport, not on production.
   */
  lyphnode_ids = blank_trie();
  lyph_ids = blank_trie();
  lyph_names = blank_trie();
  lyph_fmas = blank_trie();

  top_lyph_id = 0;

  free_all_views();

  save_lyphs();
}

void free_all_lyphplates( void )
{
  /*
   * Memory leak here is deliberate: this function should only be
   * called on the devport, not on production.
   */
  lyphplate_ids = blank_trie();
  lyphplate_names = blank_trie();
  layer_ids = blank_trie();
  save_lyphplates();

  top_lyphplate_id = 0;
  top_layer_id = 0;

  strip_lyphplates_from_graph( lyph_ids );
  save_lyphs();
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

int marked_as_house( lyph *e )
{
  return IS_SET( e->flags, 1 );
}

void mark_houses( lyph *e, lyph_wrapper **head, lyph_wrapper **tail )
{
  lyph *house = get_lyph_location( e );

  if ( house && !marked_as_house( house ) )
  {
    lyph_wrapper *w;

    SET_BIT( house->flags, 1 );
    CREATE( w, lyph_wrapper, 1 );
    w->e = house;
    LINK( w, *head, *tail, next );
    mark_houses( house, head, tail );
  }
}

void unmark_houses( lyph_wrapper **head, lyph_wrapper **tail )
{
  lyph_wrapper *w, *w_next;

  for ( w = *head; w; w = w_next )
  {
    w_next = w->next;
    REMOVE_BIT( w->e->flags, 1 );
    free( w );
  }

  *head = NULL;
  *tail = NULL;
}

lyph *get_lyph_location( lyph *e )
{
  lyph *to = e->to->location, *from = e->from->location;

  for ( ; ; )
  {
    lyph_wrapper *head = NULL, *tail = NULL;

    if ( !to || !from )
      return NULL;

    if ( to == from )
      return to;

    if ( to == e || from == e )
      return NULL;

    mark_houses( to, &head, &tail );

    if ( marked_as_house( from ) )
    {
      unmark_houses( &head, &tail );
      return from;
    }

    unmark_houses( &head, &tail );
    mark_houses( from, &head, &tail );

    if ( marked_as_house( to ) )
    {
      unmark_houses( &head, &tail );
      return to;
    }

    unmark_houses( &head, &tail );

    to = get_lyph_location( to );
    from = get_lyph_location( from );
  }
}

lyph *get_relative_lyph_loc( lyph *e, lyphview *v )
{
  lv_rect **rects;
  lyph *house;

  for ( rects = v->rects; *rects; rects++ )
    SET_BIT( (*rects)->L->flags, 2 );

  for ( house = get_lyph_location( e ); house; house = get_lyph_location(house) )
    if ( IS_SET( house->flags, 2 ) )
      break;

  for ( rects = v->rects; *rects; rects++ )
    REMOVE_BIT( (*rects)->L->flags, 2 );

  return house;
}

char *lyph_relative_loc_to_json( lyph *e, lyphview *v )
{
  lyph *loc = get_relative_lyph_loc( e, v );

  if ( loc )
    return trie_to_json( loc->id );
  else
    return NULL;
}

char *lyph_location_to_json( lyph *e )
{
  lyph *loc = get_lyph_location( e );

  if ( loc )
    return trie_to_json( loc->id );
  else
    return NULL;
}

char *lv_rect_to_json_r( lv_rect *rect, lyphview *v )
{
  return JSON
  (
    "id": trie_to_json( rect->L->id ),
    "x": rect->x,
    "y": rect->y,
    "width": rect->width,
    "height": rect->height,
    "location": lyph_relative_loc_to_json( rect->L, v )
  );
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
    "nodes": JS_ARRAY( viewed_node_to_json, vn ),
    "lyphs": JS_ARRAY_R( lv_rect_to_json_r, v->rects, v )
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

  if ( configs.readonly )
    return;

  if ( !views )
    return;

  fp = fopen( "lyphviews.dat", "w" );

  if ( !fp )
  {
    log_string( "Error: Could not open lyphviews.dat for writing" );
    return;
  }

  fprintf( fp, "TopView %zd\n", VOIDLEN( &views[1] ) );

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
  lv_rect **r;
  char **c;

  fprintf( fp, "View %d\n", v->id );

  if ( v->name )
    fprintf( fp, "Name %s\n", v->name );

  fprintf( fp, "Nodes %zd\n", VOIDLEN( v->nodes ) );

  for ( n = v->nodes, c = v->coords; *n; n++ )
  {
    fprintf( fp, "N %s %s %s\n", trie_to_static( (*n)->id ), c[0], c[1] );
    c = &c[2];
  }

  fprintf( fp, "Lyphs %zd\n", VOIDLEN( v->rects ) );

  for ( r = v->rects; *r; r++ )
    fprintf( fp, "L %s %s %s %s %s\n", trie_to_static( (*r)->L->id ), (*r)->x, (*r)->y, (*r)->width, (*r)->height );
}

void init_default_lyphviews( void )
{
  top_view = 0;

  CREATE( views, lyphview *, 2 );

  views[0] = NULL;
  views[1] = NULL;
}

void load_lyphviews( void )
{
  FILE *fp;
  lyphview *v;
  lyphnode **nodes;
  char **coords;
  lv_rect **rects;
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
       /*
        *
        if ( (!views[prev_view_index]->nodes && !views[prev_view_index]->rects) )
        {
          log_string( "lyphviews.dat: previous view did not finish loading -- aborting" );
          log_linenum( line );
          EXIT();
        }
        else
        */
        if ( !views[prev_view_index]->nodes )
        {
          CREATE( views[prev_view_index]->nodes, lyphnode *, 1 );
          views[prev_view_index]->nodes[0] = NULL;
        }
        if ( !views[prev_view_index]->rects )
        {
          CREATE( views[prev_view_index]->rects, lv_rect *, 1 );
          views[prev_view_index]->rects[0] = NULL;
        }

        *nodes = NULL;
        *coords = NULL;
        *rects = NULL;
      }

      prev_view_index = id;

      CREATE( v, lyphview, 1 );
      v->id = id;
      v->nodes = NULL;
      v->rects = NULL;
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

    if ( str_begins( buf, "Lyphs " ) )
    {
      int rcnt;

      if ( prev_view_index == -1 )
      {
        log_string( "lyphviews.dat: Mismatched Lyphs line -- aborting" );
        log_linenum( line );
        EXIT();
      }

      rcnt = strtol( &buf[strlen("Lyphs ")], NULL, 10 );

      if ( rcnt < 0 )
      {
        log_string( "lyphviews.dat: Number of lyphs is not a nonnegative integer -- aborting" );
        log_linenum( line );
        EXIT();
      }

      CREATE( views[id]->rects, lv_rect *, rcnt + 1 );
      rects = views[id]->rects;

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

    if ( str_begins( buf, "L " ) )
    {
      char *left, *ptr;
      int fLyphID = 0, fXCoord = 0, fYCoord = 0, fWidth = 0, fHeight = 0, fEnd = 0;

      if ( prev_view_index == -1 || !views[id]->rects )
      {
        log_string( "lyphviews.dat: Mismatched L line -- aborting" );
        log_linenum( line );
        EXIT();
      }

      left = &buf[strlen("L ")];

      for ( ptr = left; !fEnd; ptr++ )
      {
        if ( !*ptr || *ptr == ' ' )
        {
          if ( *ptr )
            *ptr = '\0';
          else
            fEnd = 1;

          if ( !fLyphID )
          {
            trie *lyphtr;
            lyph *e;

            fLyphID = 1;

            lyphtr = trie_strdup( left, lyph_ids );

            if ( !lyphtr->data )
            {
              CREATE( e, lyph, 1 );
              e->id = lyphtr;
              lyphtr->data = (trie **)e;
              e->flags = 0;

              CREATE( e->constraints, lyphplate *, 1 );
              e->constraints[0] = NULL;

              CREATE( e->annots, annot *, 1 );
              e->annots[0] = NULL;

              maybe_update_top_id( &top_lyph_id, left );
            }
            else
              e = (lyph *)lyphtr->data;

            CREATE( rects[0], lv_rect, 1 );
            rects[0]->L = e;
            rects++;
          }

          else if ( !fXCoord )
          {
            rects[-1]->x = strdup( left );
            fXCoord = 1;
          }
          else if ( !fYCoord )
          {
            rects[-1]->y = strdup( left );
            fYCoord = 1;
          }
          else if ( !fWidth )
          {
            rects[-1]->width = strdup( left );
            fWidth = 1;
          }
          else if ( !fHeight )
          {
            rects[-1]->height = strdup( left );
            fHeight = 1;
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
              node = blank_lyphnode();
              node->id = nodetr;
              nodetr->data = (trie **)node;
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

int load_lyphs( void )
{
  FILE *fp;
  int line = 1;
  char c, row[MAX_LYPH_LINE_LEN], *rptr = row, *end = &row[MAX_LYPH_LINE_LEN - 1], *err = NULL;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  log_string( "Loading lyphs..." );

  fp = fopen( "lyphs.dat", "r" );

  if ( !fp )
  {
    log_string( "Could not open lyphs.dat for reading" );
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
          return fclose(fp);
      }

      *rptr = '\0';

      if ( !load_lyphs_one_line( row, &err ) )
      {
        log_string( "Error while loading lyphs file." );
        log_linenum( line );
        log_string( err );

        return fclose(fp);
      }

      if ( !c )
        return fclose(fp);

      line++;
      rptr = row;
    }
    else
    {
      if ( rptr >= end )
      {
        log_string( "Error while loading lyphs file." );
        log_linenum( line );
        log_string( "Line exceeds maximum length" );

        return fclose(fp);
      }

      *rptr++ = c;
    }
  }

  return fclose(fp);
}

void save_lyphs( void )
{
  FILE *fp;

  if ( configs.readonly )
    return;

  fp = fopen( "lyphs.dat", "w" );

  if ( !fp )
  {
    log_string( "Could not open lyphs.dat for writing" );
    return;
  }

  save_lyphs_recurse( lyph_ids, fp );
  save_lyphnode_locs( lyphnode_ids, fp );

  fclose( fp );
}

void save_lyphs_recurse( trie *t, FILE *fp )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    fprintf( fp, "%s\t", trie_to_static( e->id ) );
    fprintf( fp, "%d\t%s\t", e->type, e->fma ? trie_to_static( e->fma ) : "(nofma)" );
    fprintf( fp, "%s\t", trie_to_static( e->from->id ) );
    fprintf( fp, "%s\t", trie_to_static( e->to->id ) );

    if ( e->lyphplate )
      fprintf( fp, "lyphplate:%s ", trie_to_static( e->lyphplate->id ) );

    if ( *e->constraints )
      fprintf( fp, "constraints:%s ", constraints_comma_list( e->constraints ) );

    fprintf( fp, "%s\n", e->name ? trie_to_static( e->name ) : "(noname)" );
  }

  TRIE_RECURSE( save_lyphs_recurse( *child, fp ) );
}

void save_lyphnode_locs( trie *t, FILE *fp )
{
  if ( t->data )
  {
    lyphnode *n = (lyphnode *)t->data;

    if ( n->location )
    {
      fprintf( fp, "Loc\t%s\t", trie_to_static( n->id ) );
      fprintf( fp, "%s\t%d\n", trie_to_static( n->location->id ), n->loctype );
    }
  }

  TRIE_RECURSE( save_lyphnode_locs( *child, fp ) );
}

int load_lyphnode_location( char *line, char **err )
{
  lyph *loc;
  lyphnode *n;
  char nodeidbuf[MAX_LYPH_LINE_LEN+1];
  char locbuf[MAX_LYPH_LINE_LEN+1];
  char loctypebuf[MAX_LYPH_LINE_LEN+1];
  int loctype;

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

  KEY( nodeidbuf, "Missing node ID on a location line" );
  KEY( locbuf, "Missing location on a location line" );
  KEY( loctypebuf, "Missing loctype on a location line" );

  loc = lyph_by_id( locbuf );

  if ( !loc )
  {
    *err = "Invalid lyph id on a location line";
    return 0;
  }

  if ( !strcmp( loctypebuf, "0" ) )
    loctype = 0;
  else if ( !strcmp( loctypebuf, "1" ) )
    loctype = 1;
  else
  {
    *err = "Invalid loctype on a location line";
    return 0;
  }

  n = lyphnode_by_id( nodeidbuf );

  if ( !n )
  {
    n = blank_lyphnode();
    n->id = trie_strdup( nodeidbuf, lyphnode_ids );
    n->id->data = (trie**)n;
  }

  n->location = loc;
  n->loctype = loctype;

  return 1;
}

int load_lyphs_one_line( char *line, char **err )
{
  char lyphidbuf[MAX_LYPH_LINE_LEN+1];
  char typebuf[MAX_LYPH_LINE_LEN+1];
  char fmabuf[MAX_LYPH_LINE_LEN+1];
  char frombuf[MAX_LYPH_LINE_LEN+1];
  char tobuf[MAX_LYPH_LINE_LEN+1];
  char namebuf[MAX_LYPH_LINE_LEN+1];
  lyph *e;
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

  if ( str_begins( line, "Loc\t" ) )
    return load_lyphnode_location( line + strlen("Loc\t"), err );

  KEY( lyphidbuf, "Missing edge ID" );
  KEY( typebuf, "Missing edge type" );
  KEY( fmabuf, "Missing FMA ID" );
  KEY( frombuf, "Missing initial node ID" );
  KEY( tobuf, "Missing terminal node ID" );
  KEY( namebuf, "Missing description" );

  etr = trie_strdup( lyphidbuf, lyph_ids );

  if ( !etr->data )
  {
    CREATE( e, lyph, 1 );
    e->id = etr;
    e->flags = 0;
    etr->data = (trie **)e;

    maybe_update_top_id( &top_lyph_id, lyphidbuf );

    CREATE( e->constraints, lyphplate *, 1 );
    *e->constraints = NULL;

    CREATE( e->annots, annot *, 1 );
    *e->annots = NULL;
  }
  else
    e = (lyph *)etr->data;

  e->type = strtol( typebuf, NULL, 10 );

  if ( e->type < 1 || e->type > 4 )
  {
    *err = "Invalid edge type";
    return 0;
  }

  e->fma = strcmp( fmabuf, "(nofma)" ) ? trie_strdup( fmabuf, lyph_fmas ) : NULL;

  fromtr = trie_strdup( frombuf, lyphnode_ids );

  if ( !fromtr->data )
  {
    from = blank_lyphnode();
    from->id = fromtr;
    fromtr->data = (trie **)from;

    maybe_update_top_id( &top_lyphnode_id, frombuf );
  }
  else
    from = (lyphnode *)fromtr->data;

  totr = trie_strdup( tobuf, lyphnode_ids );

  if ( !totr->data )
  {
    to = blank_lyphnode();
    to->id = totr;
    totr->data = (trie **)to;

    maybe_update_top_id( &top_lyphnode_id, tobuf );
  }
  else
    to = (lyphnode *)totr->data;

  e->lyphplate = NULL;
  e->name = parse_lyph_name_field( namebuf, e );

  e->from = from;
  e->to = to;

  add_exit( e );

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

trie *parse_lyph_name_field( char *namebuf, lyph *e )
{
  char *preamble;

  if ( parse_name_preamble( &namebuf, "lyphplate:", &preamble ) )
  {
    lyphplate *L = lyphplate_by_id( preamble );

    if ( !L )
    {
      log_stringf( "lyphs.dat referred to a nonexistent lyphplate: %s", preamble );
      return NULL;
    }

    e->lyphplate = L;
  }

  if ( parse_name_preamble( &namebuf, "constraints:", &preamble ) )
    e->constraints = parse_lyph_constraints( preamble );

  if ( !strcmp( namebuf, "(noname)" ) )
    return NULL;

  return trie_strdup( namebuf, lyph_names );
}

void add_exit( lyph *e )
{
  add_to_exits( e, e->to, &e->from->exits );
  add_to_exits( e, e->from, &e->to->incoming );
}

char *lyph_type_str( int type )
{
  switch( type )
  {
    case LYPH_ARTERIAL:
      return "arterial";
    case LYPH_MICROCIRC:
      return "microcirculation";
    case LYPH_VENOUS:
      return "venous";
    case LYPH_CARDIAC:
      return "cardiac-chamber";
    default:
      return "unknown";
  }
}

int parse_lyphplate_type_str( char *type )
{
  if ( !strcmp( type, "arterial" ) )
    return LYPH_ARTERIAL;
  if ( !strcmp( type, "microcirculation" ) )
    return LYPH_MICROCIRC;
  if ( !strcmp( type, "venous" ) )
    return LYPH_VENOUS;
  if ( !strcmp( type, "cardiac-chamber" ) )
    return LYPH_CARDIAC;

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

void got_lyphplate_triple( char *subj, char *pred, char *obj )
{
  char *s = subj, *p = pred, *o = obj;

  if ( (*subj == '"' || *subj == '<') && s++ )
    subj[strlen(subj)-1] = '\0';

  if ( (*pred == '"' || *pred == '<') && p++ )
    pred[strlen(pred)-1] = '\0';

  if ( (*obj == '"' || *obj == '<') && o++ )
    obj[strlen(obj)-1] = '\0';

  if ( !strcmp( p, "http://www.w3.org/2000/01/rdf-schema#label" ) )
    load_lyphplate_label( s, o );
  else if ( !strcmp( p, "http://open-physiology.org/lyph#lyph_type" ) )
    load_lyphplate_type( s, o );
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

  iri = trie_search( subj, lyphplate_ids );

  if ( !iri )
    return;

  ((lyphplate*)iri->data)->ont_term = ont_term;
}

void load_lyphplate_type( char *subj_full, char *type_str )
{
  char *subj = get_url_shortform( subj_full );
  int type = parse_lyphplate_type( type_str );
  trie *iri;

  if ( type != -1 )
    iri = trie_search( subj, lyphplate_ids );

  if ( type == -1 || !iri )
    return;

  ((lyphplate *)iri->data)->type = type;
}

void load_lyphplate_label( char *subj_full, char *label )
{
  char *subj = get_url_shortform( subj_full );
  trie *iri;

  if ( str_begins( subj, "TEMPLATE_" ) )
  {
    lyphplate *L;

    maybe_update_top_id( &top_lyphplate_id, subj + strlen("TEMPLATE_") );

    iri = trie_strdup( subj, lyphplate_ids );

    if ( !iri->data )
    {
      CREATE( L, lyphplate, 1 );
      L->id = iri;
      L->type = LYPHPLATE_MISSING;
      L->layers = NULL;
      L->supers = NULL;
      L->subs = NULL;
      L->ont_term = NULL;
      iri->data = (void *)L;
    }
    else
      L = (lyphplate *)iri->data;

    L->name = trie_strdup( label, lyphplate_names );
    L->name->data = (void *)L;
  }
}

void acknowledge_has_layers( char *subj_full, char *bnode_id )
{
  char *subj = get_url_shortform( subj_full );
  trie *iri = trie_search( subj, lyphplate_ids );
  trie *bnode;
  load_layers_data *lld;

  if ( !iri || !iri->data )
    return;

  bnode = trie_strdup( bnode_id, blank_nodes );

  CREATE( lld, load_layers_data, 1 );
  lld->subj = (lyphplate *)iri->data;
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

lyphplate *create_or_find_lyphplate( char *id )
{
  lyphplate *L = lyphplate_by_id( id );

  if ( L )
    return L;

  CREATE( L, lyphplate, 1 );

  L->id = trie_strdup( id, lyphplate_ids );
  L->id->data = (void *)L;
  L->type = LYPHPLATE_MISSING;
  L->layers = NULL;
  L->supers = NULL;
  L->subs = NULL;
  L->ont_term = NULL;
  L->name = NULL;

  return L;
}

void load_layer_material( char *subj_full, char *obj_full )
{
  char *subj = get_url_shortform( subj_full );
  char *obj = get_url_shortform( obj_full );
  layer *lyr = layer_by_id( subj );

  if ( !lyr )
    return;

  lyr->material = create_or_find_lyphplate( obj );
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

void load_lyphplates(void)
{
  FILE *fp;
  char *err = NULL;
  lyphplate *naked;

  fp = fopen( "lyphplates.dat", "r" );

  if ( !fp )
  {
    log_string( "Could not open lyphplates.dat for reading" );
    return;
  }

  blank_nodes = blank_trie();

  if ( !parse_ntriples( fp, &err, MAX_IRI_LEN, got_lyphplate_triple ) )
  {
    error_message( strdupf( "Failed to parse the lyphplates-file (lyphplates.dat):\n%s\n", err ? err : "(no error given)" ) );
    EXIT();
  }

  handle_loaded_layers( blank_nodes );

  if ( (naked=missing_layers( lyph_ids )) != NULL )
  {
    error_message( strdupf( "Error in lyphplates.dat: template %s has type %s but has no layers\n", trie_to_static( naked->id ), lyphplate_type_as_char( naked ) ) );
    EXIT();
  }

  fclose( fp );
}

lyphplate *missing_layers( trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( ( L->type == LYPHPLATE_MIX || L->type == LYPHPLATE_SHELL ) && ( !L->layers || !L->layers[0] ) )
      return L;
  }

  TRIE_RECURSE
  (
    lyphplate *L = missing_layers( *child );

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
    lyphplate *L = lld->subj;
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

void save_lyphplates(void)
{
  FILE *fp;
  trie *avoid_dupe_layers;

  if ( configs.readonly )
    return;

  fp = fopen( "lyphplates.dat", "w" );

  if ( !fp )
  {
    log_string( "Could not open lyphplates.dat for writing" );
    return;
  }

  avoid_dupe_layers = blank_trie();

  save_lyphplates_recurse( lyphplate_ids, fp, avoid_dupe_layers );

  fclose(fp);

  free_lyphplate_dupe_trie( avoid_dupe_layers );

  return;
}

void save_lyphplates_recurse( trie *t, FILE *fp, trie *avoid_dupes )
{
  /*
   * Save in N-Triples format for improved interoperability
   */
  static int bnodes;

  if ( t == lyphplate_ids )
    bnodes = 0;

  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;
    char *ch, *id;

    id = id_as_iri( t, NULL );
    ch = html_encode( trie_to_static( L->name ) );

    fprintf( fp, "%s <http://www.w3.org/2000/01/rdf-schema#label> \"%s\" .\n", id, ch );
    free( ch );

    fprintf( fp, "%s <http://open-physiology.org/lyph#lyph_type> \"%s\" .\n", id, lyphplate_type_as_char( L ) );

    if ( L->ont_term )
      fprintf( fp, "%s <http://open-physiology.org/lyph#ont_term> \"%s\" .\n", id, trie_to_static(L->ont_term) );

    if ( L->type == LYPHPLATE_SHELL || L->type == LYPHPLATE_MIX )
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

  TRIE_RECURSE( save_lyphplates_recurse( *child, fp, avoid_dupes ) );
}

void fprintf_layer( FILE *fp, layer *lyr, int bnodes, int cnt, trie *avoid_dupes )
{
  char *lid = id_as_iri( lyr->id, NULL );
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

  mat_iri = id_as_iri( lyr->material->id, NULL );
  fprintf( fp, "%s <http://open-physiology.org/lyph#has_material> %s .\n", lid, mat_iri );
  free( mat_iri );

  if ( lyr->thickness != -1 )
    fprintf( fp, "%s <http://open-physiology.org/lyph#has_thickness> \"%d\" .\n", lid, lyr->thickness );

  free( lid );
}

char *id_as_iri( trie *id, char *prefix )
{
  if ( prefix )
    return strdupf( "<http://open-physiology.org/lyphs/#%s%s>", prefix, trie_to_static(id) );
  else
    return strdupf( "<http://open-physiology.org/lyphs/#%s>", trie_to_static(id) );
}

lyphplate *lyphplate_by_layers( int type, layer **layers, char *name )
{
  lyphplate *L;

  if ( type == LYPHPLATE_MIX )
    sort_layers( layers );

  L = lyphplate_by_layers_recurse( type, layers, lyphplate_ids );

  if ( !L )
  {
    if ( !name )
      return NULL;

    CREATE( L, lyphplate, 1 );
    L->name = trie_strdup( name, lyphplate_names );
    L->id = assign_new_lyphplate_id( L );
    L->type = type;
    L->layers = copy_layers( layers );
    L->ont_term = NULL;
    L->supers = NULL;
    compute_lyphplate_hierarchy_one_lyphplate( L );
    add_lyphplate_as_super( L, lyphplate_ids );

    save_lyphplates();
  }

  return L;
}

lyphplate *lyphplate_by_layers_recurse( int type, layer **layers, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( L->type == type && same_layers( L->layers, layers ) )
      return L;
  }

  TRIE_RECURSE
  (
    lyphplate *L = lyphplate_by_layers_recurse( type, layers, *child );

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
  lyphplate *L = lyphplate_by_id( mtid );
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

    save_lyphplates();
  }

  return lyr;
}

trie *assign_new_lyphplate_id( lyphplate *L )
{
  char buf[128];
  trie *t;

  top_lyphplate_id++;

  sprintf( buf, "TEMPLATE_%d", top_lyphplate_id );

  t = trie_strdup( buf, lyphplate_ids );

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

int layer_matches( layer *candidate, const lyphplate *material, const float thickness )
{
  if ( candidate->material != material )
    return 0;

  if ( thickness != -1 && thickness != candidate->thickness )
    return 0;

  return 1;
}

layer *layer_by_description_recurse( const lyphplate *L, const float thickness, const trie *t )
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

lyphplate *lyphplate_by_name( char *name )
{
  trie *t = trie_search( name, lyphplate_names );

  if ( t && t->data )
    return (lyphplate *) t->data;

  return NULL;
}

lyphplate *lyphplate_by_id( char *id )
{
  trie *t = trie_search( id, lyphplate_ids );

  if ( t && t->data )
    return (lyphplate *) t->data;

  t = trie_search( id, superclasses );

  if ( t && t->data )
  {
    lyphplate *L;
    trie *label;

    if ( (L = lyphplate_by_ont_term( t )) != NULL )
      return L;

    label = trie_search( id, iri_to_labels );

    CREATE( L, lyphplate, 1 );

    L->type = LYPHPLATE_BASIC;
    L->id = assign_new_lyphplate_id( L );
    L->ont_term = t;
    L->name = trie_strdup( label ? trie_to_static( *label->data ) : id, lyphplate_names );
    L->layers = NULL;
    L->supers = NULL;

    compute_lyphplate_hierarchy_one_lyphplate( L );
    add_lyphplate_as_super( L, lyphplate_ids );

    L->id->data = (trie **)L;
    L->name->data = (trie **)L;

    save_lyphplates();

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

lyph *lyph_by_template( trie *t, lyphplate *L )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->lyphplate == L )
      return e;
  }

  TRIE_RECURSE
  (
    lyph *e = lyph_by_template( *child, L );

    if ( e )
      return e;
  );

  return NULL;
}

lyph *lyph_by_template_or_id( char *id )
{
  lyph *e = lyph_by_id( id );
  lyphplate *L;
  lyphnode *from, *to;
  char *namestr;

  if ( e )
    return e;

  L = lyphplate_by_id( id );

  if ( !L )
    return NULL;

  e = lyph_by_template( lyph_ids, L );

  if ( e )
    return e;

  from = make_lyphnode();
  to = make_lyphnode();

  if ( L->name )
    namestr = trie_to_static( L->name );
  else
    namestr = "none";

  e = make_lyph( 1, from, to, L, NULL, namestr );

  /*
   * To do: pass this work upward to avoid duplicated effort
   */
  save_lyphs();

  return e;
}

lyph *lyph_by_id( char *id )
{
  trie *t = trie_search( id, lyph_ids );

  if ( t )
    return (lyph *)t->data;

  return NULL;
}

char *lyphplate_to_json( lyphplate *L )
{
  return JSON
  (
    "id": trie_to_json( L->id ),
    "name": trie_to_json( L->name ),
    "type": lyphplate_type_as_char( L ),
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
      "location": n->location ? trie_to_json( n->location->id ) : NULL,
      "loctype": loctype_to_str( n->loctype ),
      "x": x,
      "y": y
    );

    if ( exits != n->exits )
      free( exits );

    exit_to_json_flags = 0;

    return retval;
  }
  else return JSON
  (
    "id": trie_to_json( n->id ),
    "x": x,
    "y": y,
    "location": n->location ? trie_to_json( n->location->id ) : NULL,
    "loctype": loctype_to_str( n->loctype )
  );
}

char *exit_to_json( exit_data *x )
{
  return JSON
  (
    "to": trie_to_json( x->to->id ),
    "via":
      IS_SET( exit_to_json_flags, ETJ_FULL_EXIT_DATA ) ?
      lyph_to_json( x->via ) :
      trie_to_json( x->via->id )
  );
}

char *annot_to_json( annot *a )
{
  return JSON
  (
    "pred": trie_to_json( a->pred ),
    "obj": trie_to_json( a->obj ),
    "pubmed": pubmed_to_json_brief( a->pubmed )
  );
}

char *lyph_to_json( lyph *e )
{
  int yes = 1;

  return lyph_to_json_r( e, &yes );
}

char *lyph_to_json_r( lyph *e, int *show_annots )
{
  char *retval, *annots;
  int old_LTJ_flags = lyphnode_to_json_flags;
  lyphnode_to_json_flags = 0;

  if ( show_annots && *show_annots )
    annots = JS_ARRAY( annot_to_json, e->annots );
  else
    annots = json_suppressed;

  retval = JSON
  (
    "id": trie_to_json( e->id ),
    "fma": trie_to_json( e->fma ),
    "name": trie_to_json( e->name ),
    "type": int_to_json( e->type ),
    "from": lyphnode_to_json( e->from ),
    "to": lyphnode_to_json( e->to ),
    "template": e->lyphplate ? lyphplate_to_json( e->lyphplate ) : NULL,
    "annots": annots,
    "constraints": JS_ARRAY( lyphplate_to_shallow_json, e->constraints )
  );

  lyphnode_to_json_flags = old_LTJ_flags;
  return retval;
}

char *lyphpath_to_json( lyph **path )
{
  return JSON
  (
    "length": int_to_json( VOIDLEN( path ) ),
    "edges": JS_ARRAY( lyph_to_json, path )
  );
}

char *lyphplate_type_as_char( lyphplate *L )
{
  switch( L->type )
  {
    case LYPHPLATE_BASIC:
      return "basic";
    case LYPHPLATE_SHELL:
      return "shell";
    case LYPHPLATE_MIX:
      return "mix";
    default:
      return "unknown";
  }
}

layer **copy_layers( layer **src )
{
  int len = VOIDLEN( src );
  layer **dest;

  CREATE( dest, layer *, len + 1 );

  memcpy( dest, src, sizeof( layer * ) * (len + 1) );

  return dest;
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
  qsort( layers, VOIDLEN( layers ), sizeof(layer *), cmp_layers );
}

void free_lyphplate_dupe_trie( trie *t )
{
  TRIE_RECURSE( free_lyphplate_dupe_trie( *child ) );

  if ( t->children )
    free( t->children );

  if ( t->label )
    free( t->label );

  free( t );
}

int parse_lyphplate_type( char *str )
{
  if ( !strcmp( str, "mix" ) )
    return LYPHPLATE_MIX;

  if ( !strcmp( str, "shell" ) )
    return LYPHPLATE_SHELL;

  if ( !strcmp( str, "basic" ) )
    return LYPHPLATE_BASIC;

  return -1;
}

lyph ***compute_lyphpaths( lyphnode_wrapper *from_head, lyphnode_wrapper *to_head, lyph_filter *filter, int numpaths )
{
  lyphstep *head = NULL, *tail = NULL, *step, *curr;
  lyphnode_wrapper *w;
  lyph ***paths, ***pathsptr, **path;
  int pathcnt = 0;

  CREATE( paths, lyph **, numpaths + 1 );
  pathsptr = paths;

  if ( from_head == to_head )
  {
    compute_lyphpath_trivial_path:

    CREATE( path, lyph *, 1 );
    path[0] = NULL;
    paths[0] = path;
    paths[1] = NULL;

    return paths;
  }

  for ( w = to_head; w; w = w->next )
    SET_BIT( w->n->flags, LYPHNODE_GOAL );

  for ( w = from_head; w; w = w->next )
  {
    if ( IS_SET( w->n->flags, LYPHNODE_GOAL ) )
    {
      for ( w = to_head; w; w = w->next )
        REMOVE_BIT( w->n->flags, LYPHNODE_GOAL );
      free_lyphsteps( head );

      goto compute_lyphpath_trivial_path;
    }

    CREATE( step, lyphstep, 1 );
    step->depth = 0;
    step->backtrace = NULL;
    step->location = w->n;
    step->lyph = NULL;
    SET_BIT( w->n->flags, LYPHNODE_SEEN );

    LINK2( step, head, tail, next, prev );
  }

  curr = head;

  for ( ; ; curr = curr->next )
  {
    exit_data **x;
    int reversed;

    if ( !curr )
    {
      for ( w = to_head; w; w = w->next )
        REMOVE_BIT( w->n->flags, LYPHNODE_GOAL );

      free_lyphsteps( head );
      *pathsptr = NULL;
      return paths;
    }

    if ( IS_SET( curr->location->flags, LYPHNODE_GOAL ) )
    {
      lyph **pptr;
      lyphstep *back;

      CREATE( path, lyph *, curr->depth + 1 );
      pptr = &path[curr->depth-1];
      path[curr->depth] = NULL;

      back = curr;
      do
      {
        *pptr-- = back->lyph;
        back = back->backtrace;
      }
      while( back->backtrace );

      *pathsptr++ = path;

      if ( ++pathcnt == numpaths )
        goto compute_lyphpaths_escape;

      continue;
    }

    /*
     * First traverse curr->location->exits, then traverse curr->location->incoming
     */
    reversed = 0;

    for ( x = curr->location->exits; *x || (!reversed++ && (x=curr->location->incoming)); x++ )
    {
      if ( IS_SET( (*x)->to->flags, LYPHNODE_SEEN ) )
        continue;

      if ( filter && !lyph_passes_filter( (*x)->via, filter ) )
      {
        SET_BIT( step->location->flags, LYPHNODE_SEEN );
        continue;
      }

      CREATE( step, lyphstep, 1 );
      step->depth = curr->depth + 1;
      step->backtrace = curr;
      step->location = (*x)->to;
      step->lyph = (*x)->via;
      LINK2( step, head, tail, next, prev );
      SET_BIT( step->location->flags, LYPHNODE_SEEN );
    }
  }

  compute_lyphpaths_escape:

  for ( w = to_head; w; w = w->next )
    REMOVE_BIT( w->n->flags, LYPHNODE_GOAL );

  free_lyphsteps( head );
  *pathsptr = NULL;

  return paths;
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
  lyphnode *n = blank_lyphnode();

  n->id = new_lyphnode_id(n);

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

lyph *make_lyph( int type, lyphnode *from, lyphnode *to, lyphplate *L, char *fmastr, char *namestr )
{
  trie *fma;
  lyph *e;

  fma = fmastr ? trie_strdup( fmastr, lyph_fmas ) : NULL;

  e = find_duplicate_lyph( type, from, to, L, fma, namestr );

  if ( e )
    return e;

  CREATE( e, lyph, 1 );

  e->id = new_lyph_id(e);
  e->name = namestr ? trie_strdup( namestr, lyph_names ) : NULL;
  e->type = type;
  e->from = from;
  e->to = to;
  e->lyphplate = L;
  e->fma = fma;
  e->flags = 0;

  CREATE( e->constraints, lyphplate *, 1 );
  e->constraints[0] = NULL;

  CREATE( e->annots, annot *, 1 );
  e->annots[0] = NULL;

  add_exit( e );

  save_lyphs();

  return e;
}

trie *new_lyph_id(lyph *e)
{
  trie *id;
  char idstr[MAX_INT_LEN+1];

  top_lyph_id++;

  sprintf( idstr, "%d", top_lyph_id );

  id = trie_strdup( idstr, lyph_ids );

  id->data = (trie **)e;

  return id;
}

lyph *find_duplicate_lyph( int type, lyphnode *from, lyphnode *to, lyphplate *L, trie *fma, char *namestr )
{
  trie *name;

  if ( namestr )
  {
    name = trie_search( namestr, lyph_names );

    if ( !name )
      return NULL;
  }
  else
    name = NULL;

  return find_duplicate_lyph_recurse( lyph_ids, type, from, to, L, fma, name );
}

lyph *find_duplicate_lyph_recurse( trie *t, int type, lyphnode *from, lyphnode *to, lyphplate *L, trie *fma, trie *name )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->name == name
    &&   e->type == type
    &&   e->from == from
    &&   e->to   == to
    &&   e->lyphplate == L
    &&   e->fma  == fma )
      return e;
  }

  TRIE_RECURSE
  (
    lyph *e = find_duplicate_lyph_recurse( *child, type, from, to, L, fma, name );

    if ( e )
      return e;
  );

  return NULL;
}

void lyphs_unset_bits( int bits, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;
    REMOVE_BIT( e->flags, bits );
  }

  TRIE_RECURSE( lyphs_unset_bits( bits, *child ) );
}

void lyphplates_unset_bits( int bits, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;
    REMOVE_BIT( L->flags, bits );
  }

  TRIE_RECURSE( lyphplates_unset_bits( bits, *child ) );
}

void lyphnodes_unset_bits( int bits, trie *t )
{
  if ( t->data )
  {
    lyphnode *n = (lyphnode *)t->data;
    REMOVE_BIT( n->flags, bits );
  }

  TRIE_RECURSE( lyphnodes_unset_bits( bits, *child ) );
}

/*
 * To do: optimize this
 */
lyphplate *lyphplate_by_ont_term( trie *term )
{
  return lyphplate_by_ont_term_recurse( term, lyphplate_ids );
}

lyphplate *lyphplate_by_ont_term_recurse( trie *term, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;
    if ( L->ont_term == term )
      return L;
  }

  TRIE_RECURSE
  (
    lyphplate *L = lyphplate_by_ont_term_recurse( term, *child );

    if ( L )
      return L;
  );

  return NULL;
}

lyphplate **parse_lyph_constraints( char *str )
{
  lyphplate_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyphplate *L, **buf, **bptr;
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
        L = lyphplate_by_id( left );

        if ( !L )
          log_stringf( "Unrecognized lyphplate (%s) while parsing lyphplate constraints", left );
        else
        {
          CREATE( w, lyphplate_wrapper, 1 );
          w->L = L;
          LINK( w, head, tail, next );
          cnt++;
        }

        if ( fEnd )
          goto parse_lyph_constraints_escape;

        left = &ptr[1];
        break;

      default:
        continue;
    }
  }

  parse_lyph_constraints_escape:

  CREATE( buf, lyphplate *, cnt + 1 );
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

int can_assign_lyphplate_to_lyph( lyphplate *L, lyph *e, char **err )
{
  lyphplate **c;

  for ( c = e->constraints; *c; c++ )
  {
    if ( !is_superlyphplate( *c, L ) )
    {
      *err = strdupf( "That lyph is constrained to have template a subtemplate of %s", trie_to_static( (*c)->id ) );
      return 0;
    }
  }

  return 1;
}

int lyph_passes_filter( lyph *e, lyph_filter *f )
{
  if ( e->lyphplate )
    return is_superlyphplate( f->sup, e->lyphplate );

  if ( !*e->constraints )
    return f->accept_na_edges;
  else
  {
    lyphplate **c;

    for ( c = e->constraints; *c; c++ )
      if ( !is_superlyphplate( f->sup, *c ) )
        return 0;

    return 1;
  }
}

#define LYPHPLATE_ACCOUNTED_FOR 1
void populate_all_lyphplates_L( lyphplate ***bptr, lyphplate *L )
{
  if ( IS_SET( L->flags, LYPHPLATE_ACCOUNTED_FOR ) )
    return;

  if ( L->type == LYPHPLATE_MIX || L->type == LYPHPLATE_SHELL )
  {
    layer **lyr;

    for ( lyr = L->layers; *lyr; lyr++ )
      populate_all_lyphplates_L( bptr, (*lyr)->material );
  }

  **bptr = L;
  (*bptr)++;

  SET_BIT( L->flags, LYPHPLATE_ACCOUNTED_FOR );
}

void populate_all_lyphplates( lyphplate ***bptr, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    populate_all_lyphplates_L( bptr, L );
  }

  TRIE_RECURSE( populate_all_lyphplates( bptr, *child ) );
}

lyphplate **get_all_lyphplates( void )
{
  lyphplate **buf, **bptr;
  int cnt = count_nontrivial_members( lyphplate_ids );

  CREATE( buf, lyphplate *, cnt + 1 );
  bptr = buf;

  populate_all_lyphplates( &bptr, lyphplate_ids );

  *bptr = NULL;

  lyphplates_unset_bits( LYPHPLATE_ACCOUNTED_FOR, lyphplate_ids );

  return buf;
}

void free_lyphnode_wrappers( lyphnode_wrapper *head )
{
  lyphnode_wrapper *next;

  for ( ; head; head = next )
  {
    next = head->next;
    free(head);
  }
}

void remove_from_exits( lyph *e, exit_data ***victim )
{
  exit_data **x, **xptr, **oldptr;

  CREATE( x, exit_data *, VOIDLEN( *victim ) + 1 );
  xptr = x;

  for ( oldptr = *victim; *oldptr; oldptr++ )
  {
    if ( (*oldptr)->via == e )
      free( *oldptr );
    else
      *xptr++ = *oldptr;
  }

  *xptr = NULL;
  free( *victim );
  *victim = xptr;
}

void add_to_exits( lyph *e, lyphnode *to, exit_data ***victim )
{
  exit_data **x, *newx;
  int len;

  if ( !*victim )
  {
    CREATE( x, exit_data *, 2 );
    CREATE( x[0], exit_data, 1 );
    x[0]->to = to;
    x[0]->via = e;
    x[1] = NULL;
    *victim = x;
    return;
  }

  len = VOIDLEN( *victim );

  CREATE( x, exit_data *, len+2 );
  memcpy( x, *victim, len * sizeof(exit_data *) );

  CREATE( newx, exit_data, 1 );
  newx->to = to;
  newx->via = e;

  x[len] = newx;
  x[len+1] = NULL;

  free( *victim );
  *victim = x;
}

void change_source_of_exit( lyph *via, lyphnode *new_src, exit_data **exits )
{
  exit_data **x;

  for ( x = exits; *x; x++ )
    if ( (*x)->via == via )
      (*x)->to = new_src;
}

void change_dest_of_exit( lyph *via, lyphnode *new_dest, exit_data **exits )
{
  exit_data **x;

  for ( x = exits; *x; x++ )
    if ( (*x)->via == via )
      (*x)->to = new_dest;
}

lyphnode *blank_lyphnode( void )
{
  lyphnode *n;

  CREATE( n, lyphnode, 1 );
  CREATE( n->exits, exit_data *, 1 );
  CREATE( n->incoming, exit_data *, 1 );
  n->exits[0] = NULL;
  n->incoming[0] = NULL;
  n->loctype = -1;

  return n;
}
