#include "lyph.h"
#include "srv.h"

HANDLER( handle_editedge_request )
{
  char *edgeid, *lyphid, *typestr, *namestr, *fmastr, *constraintstr, *fromstr, *tostr;
  char *err = NULL;
  lyphedge *e;
  lyphnode *from, *to;
  lyph *L, *constraint;
  trie *fma;
  int type;

  edgeid = get_url_param( params, "edge" );

  if ( !edgeid )
    HND_ERR( "You did not specify which edge to edit." );

  e = lyphedge_by_id( edgeid );

  if ( !e )
    HND_ERR( "The specified edge was not found in the database." );

  lyphid = get_url_param( params, "lyph" );

  if ( lyphid )
  {
    L = lyph_by_id( lyphid );

    if ( !L )
      HND_ERR( "The indicated lyph was not found in the database." );

    if ( !can_assign_lyph_to_edge( L, e, &err ) )
      HND_ERR( err ? err : "The indicated lyph could not be assigned to the indicated edge." );
  }
  else
    L = NULL;

  typestr = get_url_param( params, "type" );

  if ( typestr )
  {
    type = strtol( typestr, NULL, 10 );
    if ( type < 1 || type > 4 )
      HND_ERR( "Valid edge types are 1, 2, 3, and 4." );
  }

  namestr = get_url_param( params, "name" );

  fmastr = get_url_param( params, "fma" );

  if ( fmastr )
    fma = trie_strdup( fmastr, lyphedge_fmas );
  else
    fma = NULL;

  constraintstr = get_url_param( params, "constraint" );

  if ( constraintstr )
  {
    constraint = lyph_by_id( constraintstr );

    if ( !constraint )
      HND_ERR( "The indicated constraint was not found in the database." );
  }
  else
    constraint = NULL;

  fromstr = get_url_param( params, "from" );

  if ( fromstr )
  {
    from = lyphnode_by_id( fromstr );

    if ( !from )
      HND_ERR( "The indicated 'from' node was not found in the database." );
  }
  else
    from = NULL;

  tostr = get_url_param( params, "to" );

  if ( tostr )
  {
    to = lyphnode_by_id( tostr );

    if ( !to )
      HND_ERR( "The indicated 'to' node was not found in the database." );
  }
  else
    to = NULL;

  if ( L && constraint )
  {
    if ( !is_superlyph( constraint, L ) )
      HND_ERR( "The indicated lyph is not a sublyph of the indicated constraint" );
  }
  else if ( L )
  {
    lyph **c;

    for ( c = e->constraints; *c; c++ )
      if ( !is_superlyph( *c, L ) )
        HND_ERR( "The indicated lyph is ruled out by one of the edge's constraints" );
  }
  else if ( constraint )
  {
    if ( e->lyph && !is_superlyph( constraint, e->lyph ) )
      HND_ERR( "The edge's lyph is not a sublyph of the indicated constraint" );
  }

  if ( namestr )
  {
    trie *name = trie_strdup( namestr, lyphedge_names );

    if ( e->name )
      e->name->data = NULL;

    e->name = name;
    name->data = (trie **)e;
  }

  if ( typestr )
    e->type = type;

  if ( L )
    e->lyph = L;

  if ( constraint )
  {
    free( e->constraints );
    CREATE( e->constraints, lyph *, 2 );
    e->constraints[0] = constraint;
    e->constraints[1] = NULL;
  }

  if ( fma )
    e->fma = fma;

  if ( from )
  {
    exit_data **xits, **xptr_new, **xptr_old;

    CREATE( xits, exit_data *, VOIDLEN( e->from->exits ) + 1 );

    for ( xptr_new = xits, xptr_old = e->from->exits; *xptr_old; xptr_old++ )
    {
      if ( (*xptr_old)->via == e )
        free( *xptr_old );
      else
        *xptr_new++ = *xptr_old;
    }

    CREATE( *xptr_new, exit_data, 1 );
    (*xptr_new)->to = to ? to : e->to;
    (*xptr_new)->via = e;

    xptr_new[1] = NULL;

    free( e->from->exits );
    e->from->exits = xits;

    e->from = from;
  }

  if ( to )
  {
    if ( !from )
    {
      exit_data **xit;

      for ( xit = e->from->exits; *xit; xit++ )
        if ( (*xit)->via == e )
          (*xit)->to = to;
    }

    e->to = to;
  }

  save_lyphedges();

  send_200_response( req, lyphedge_to_json( e ) );
}

HANDLER( handle_editlyph_request )
{
  lyph *L;
  trie *ont;
  char *lyphstr, *namestr, *typestr, *ontstr;
  int type, fQualitativeChange = 0;

  lyphstr = get_url_param( params, "lyph" );

  if ( !lyphstr )
    HND_ERR( "You did not specify which lyph to edit" );

  L = lyph_by_id( lyphstr );

  if ( !L )
    HND_ERR( "The specified lyph was not found in the database" );

  namestr = get_url_param( params, "name" );

  typestr = get_url_param( params, "type" );

  if ( typestr )
  {
    if ( !strcmp( typestr, "basic" ) )
    {
      if ( L->type != LYPH_BASIC )
        HND_ERR( "Currently, editing a non-basic lyph into type basic is not yet implemented" );

      type = -1;
    }
    else if ( !strcmp( typestr, "mix" ) )
    {
      if ( L->type == LYPH_BASIC )
        HND_ERR( "Currently, editing a basic lyph into type mix is not yet implemented" );

      type = LYPH_MIX;
    }
    else if ( !strcmp( typestr, "shell" ) )
    {
      if ( L->type == LYPH_BASIC )
        HND_ERR( "Currently, editing a basic lyph into type shell is not yet implemented" );

      type = LYPH_SHELL;
    }
    else
      HND_ERR( "Valid lyph types are 'basic', 'mix', and 'shell'" );
  }
  else
    type = -1;

  ontstr = get_url_param( params, "ont" );

  if ( ontstr )
  {
    lyph *rival;

    ont = trie_search( ontstr, superclasses );

    if ( !ont )
      HND_ERR( "The indicated ontology term was not found in the database" );

    rival = lyph_by_ont_term( ont );

    if ( rival && rival != L )
      HND_ERR( "There is already a lyph with the indicated ontology term" );
  }
  else
    ont = NULL;

  if ( namestr )
  {
    L->name->data = NULL;
    L->name = trie_strdup( namestr, lyph_names );
    L->name->data = (trie **)L;
  }

  if ( type != -1 )
  {
    fQualitativeChange = 1;
    L->type = type;
  }

  if ( ont )
  {
    fQualitativeChange = 1;
    L->ont_term = ont;
  }

  if ( fQualitativeChange )
    recalculate_lyph_hierarchy();

  save_lyphs();

  send_200_response( req, lyph_to_json( L ) );
}

HANDLER( handle_editview_request )
{
  char *viewstr, *namestr;
  lyphview *v;

  viewstr = get_url_param( params, "view" );

  if ( !viewstr )
    HND_ERR( "You did not specify which lyphview to edit" );

  v = lyphview_by_id( viewstr );

  if ( !v )
    HND_ERR( "The indicated view was not found in the database" );

  namestr = get_url_param( params, "name" );

  if ( namestr )
  {
    if ( v->name )
      free( v->name );

    v->name = strdup( namestr );
  }

  save_lyphviews();

  send_200_response( req, lyphview_to_json( v ) );
}

HANDLER( handle_editlayer_request )
{
  layer *lyr;
  lyph *mat;
  char *lyrstr, *matstr, *thkstr;
  int thk, fQualitativeChange = 0;

  lyrstr = get_url_param( params, "layer" );

  if ( !lyrstr )
    HND_ERR( "You did not indicate which layer to edit" );

  lyr = layer_by_id( lyrstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not found in the database" );

  matstr = get_url_param( params, "material" );

  if ( matstr )
  {
    mat = lyph_by_id( matstr );

    if ( !mat )
      HND_ERR( "The indicated material was not found in the database" );
  }
  else
    mat = NULL;

  thkstr = get_url_param( params, "thickness" );

  if ( thkstr )
  {
    thk = strtol( thkstr, NULL, 10 );

    if ( thk < 1 )
      HND_ERR( "Thickness must be a positive integer" );
  }
  else
    thk = -1;

  if ( mat )
  {
    fQualitativeChange = 1;
    lyr->material = mat;
  }

  if ( thk != -1 )
  {
    fQualitativeChange = 1;
    lyr->thickness = thk;
  }

  if ( fQualitativeChange )
    recalculate_lyph_hierarchy();

  save_lyphs();

  send_200_response( req, layer_to_json( lyr ) );
}

void remove_exit_data( lyphnode *n, lyphedge *e )
{
  exit_data **xptr, **xnew, **xnewptr;

  CREATE( xnew, exit_data *, VOIDLEN( n->exits ) );

  for ( xptr = n->exits, xnewptr = xnew; *xptr; xptr++ )
  {
    if ( (*xptr)->via != e )
      *xnewptr++ = *xptr;
  }

 *xnewptr = NULL;

 free( n->exits );
 n->exits = xnew;
}

void delete_lyphedge( lyphedge *e )
{
  e->id->data = NULL;

  free( e->constraints );

  remove_exit_data( e->from, e );

  free( e );
}

HANDLER( handle_delete_edges_request )
{
  char *edgestr, *err;
  lyphedge **e, **eptr, dupe;

  edgestr = get_url_param( params, "edges" );

  if ( !edgestr )
  {
    edgestr = get_url_param( params, "edge" );

    if ( !edgestr )
      HND_ERR( "You did not specify which edge to delete." );
  }

  e = (lyphedge **) PARSE_LIST( edgestr, lyphedge_by_id, "edge", &err );

  if ( !e )
  {
    if ( err )
    {
      HND_ERR_NORETURN( err );
      free( err );
      return;
    }
    HND_ERR( "One of the indicated edges was not found in the database." );
  }

  for ( eptr = e; *eptr; eptr++ )
  {
    if ( (*eptr)->type == LYPHEDGE_DELETED )
      *eptr = &dupe;
    else
      (*eptr)->type = LYPHEDGE_DELETED;
  }

  for ( eptr = e; *eptr; eptr++ )
    if ( *eptr != &dupe )
      delete_lyphedge( *eptr );

  free( e );

  save_lyphedges();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

#define LYPHNODE_BEING_DELETED 1

void remove_edges_with_doomed_nodes( trie *t )
{
  if ( t->data )
  {
    lyphedge *e = (lyphedge *)t->data;

    if ( e->from->flags == LYPHNODE_BEING_DELETED
    ||   e->to->flags   == LYPHNODE_BEING_DELETED )
      delete_lyphedge( e );
  }

  TRIE_RECURSE( remove_edges_with_doomed_nodes( *child ) );
}

int remove_doomed_nodes_from_views( void )
{
  int i, fMatch = 0;
  extern int top_view;
  extern lyphview **views;
  extern lyphview obsolete_lyphview;

  for ( i = 0; i <= top_view; i++ )
  {
    lyphview *v;
    lyphnode **nptr, **buf, **bptr;
    char **cptr, **newc, **newcptr;
    int size;

    if ( views[i] == &obsolete_lyphview || !views[i] )
      continue;

    v = views[i];

    for ( nptr = v->nodes; *nptr; nptr++ )
      if ( (*nptr)->flags == LYPHNODE_BEING_DELETED )
        break;

    if ( !*nptr )
      continue;

    fMatch = 1;

    for ( nptr = v->nodes, size = 0; *nptr; nptr++ )
      if ( (*nptr)->flags != LYPHNODE_BEING_DELETED )
        size++;

    CREATE( buf, lyphnode *, size + 1 );
    bptr = buf;
    CREATE( newc, char *, size*2 + 1 );
    newcptr = newc;

    for ( nptr = v->nodes, cptr = v->coords; *nptr; nptr++ )
    {
      if ( (*nptr)->flags == LYPHNODE_BEING_DELETED )
      {
        free( *cptr++ );
        free( *cptr++ );
      }
      else
      {
        *bptr++ = *nptr;
        *newcptr++ = *cptr++;
        *newcptr++ = *cptr++;
      }
    }

    *bptr = NULL;
    *newcptr = NULL;

    free( v->nodes );
    v->nodes = buf;
    free( v->coords );
    v->coords = newc;
  }

  return fMatch;
}

void delete_lyphnode( lyphnode *n )
{
  free( n->exits );
  n->id->data = NULL;
  free( n );
}

HANDLER( handle_delete_nodes_request )
{
  lyphnode **n, **nptr, dupe;
  char *nodestr, *err;

  nodestr = get_url_param( params, "nodes" );

  if ( !nodestr )
  {
    nodestr = get_url_param( params, "node" );

    if ( !nodestr )
      HND_ERR( "You did not specify which nodes to delete" );
  }

  n = (lyphnode **) PARSE_LIST( nodestr, lyphnode_by_id, "node", &err );

  if ( !n )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated nodes was not found in the database." );
  }

  for ( nptr = n; *nptr; nptr++ )
  {
    if ( (*nptr)->flags == LYPHNODE_BEING_DELETED )
      *nptr = &dupe;
    else
      (*nptr)->flags = LYPHNODE_BEING_DELETED;
  }

  remove_edges_with_doomed_nodes( lyphedge_ids );

  if ( remove_doomed_nodes_from_views() )
    save_lyphviews();

  for ( nptr = n; *nptr; nptr++ )
    if ( *nptr != &dupe )
      delete_lyphnode( *nptr );

  save_lyphedges();

  free( n );

  send_200_response( req, JSON1( "Response": "OK" ) );
}

#undef LYPHNODE_BEING_DELETED

#define LYPH_BEING_DELETED 1
#define LAYER_BEING_DELETED_THICKNESS -2

int remove_doomed_lyphs_from_edges( trie *t )
{
  int fMatch = 0;

  if ( t->data )
  {
    lyphedge *e = (lyphedge *)t->data;
    lyph **c;

    if ( e->lyph && e->lyph->flags == LYPH_BEING_DELETED )
    {
      e->lyph = NULL;
      fMatch = 1;
    }

    for ( c = e->constraints; *c; c++ )
      if ( (*c)->flags == LYPH_BEING_DELETED )
        break;

    if ( *c )
    {
      lyph **newc, **newcptr;
      int size = 0;

      for ( c = e->constraints; *c; c++ )
        if ( (*c)->flags != LYPH_BEING_DELETED )
          size++;

      CREATE( newc, lyph *, size + 1 );

      for ( c = e->constraints, newcptr = newc; *c; c++ )
        if ( (*c)->flags != LYPH_BEING_DELETED )
          *newcptr++ = *c;

      *newcptr = NULL;
      free( e->constraints );
      e->constraints = newc;
      fMatch = 1;
    }
  }

  TRIE_RECURSE( fMatch |= remove_doomed_lyphs_from_edges( *child ) );

  return fMatch;
}

int spread_lyphdoom( trie *t )
{
  int fMatch = 0;

  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( L->flags != LYPH_BEING_DELETED
    && ( L->type == LYPH_MIX || L->type == LYPH_SHELL ) )
    {
      layer **lyr;

      for ( lyr = L->layers; *lyr; lyr++ )
        if ( (*lyr)->material->flags == LYPH_BEING_DELETED )
          break;

      if ( *lyr )
      {
        L->flags = LYPH_BEING_DELETED;
        fMatch = 1;
      }
    }
  }

  TRIE_RECURSE( fMatch |= spread_lyphdoom( *child ) );

  return fMatch;
}

void delete_doomed_layers( trie *t )
{
  if ( t->data )
  {
    layer *lyr = (layer *)t->data;

    if ( lyr->thickness == LAYER_BEING_DELETED_THICKNESS
    ||   lyr->material->flags == LYPH_BEING_DELETED )
    {
      t->data = NULL;
      free( lyr );
    }
  }

  TRIE_RECURSE( delete_doomed_layers( *child ) );
}

void delete_doomed_lyphs( trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( L->flags == LYPH_BEING_DELETED )
    {
      t->data = NULL;

      if ( L->layers )
        free( L->layers );
      if ( L->supers )
        free( L->supers );
      if ( L->subs )
        free( L->subs );

      free( L );
    }
  }

  TRIE_RECURSE( delete_doomed_lyphs( *child ) );
}

HANDLER( handle_delete_lyphs_request )
{
  char *lyphstr, *err;
  lyph **L, **Lptr, dupe;

  lyphstr = get_url_param( params, "lyphs" );

  if ( !lyphstr )
  {
    lyphstr = get_url_param( params, "lyph" );

    if ( !lyphstr )
      HND_ERR( "You did not indicate which lyphs to delete." );
  }

  L = (lyph **) PARSE_LIST( lyphstr, lyph_by_id, "lyph", &err );

  if ( !L )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs could not be found in the database" );
  }

  for ( Lptr = L; *Lptr; Lptr++ )
  {
    if ( (*Lptr)->flags == LYPH_BEING_DELETED )
      *Lptr = &dupe;
    else
      (*Lptr)->flags = LYPH_BEING_DELETED;
  }

  free( L );

  /*
   * Any lyph/layer which refers to a doomed lyph/layer should also be doomed
   */
  while ( spread_lyphdoom( lyph_ids ) )
    ;

  if ( remove_doomed_lyphs_from_edges( lyphedge_ids ) )
    save_lyphedges();

  delete_doomed_layers( layer_ids );
  delete_doomed_lyphs( lyph_ids );

  save_lyphs();

  recalculate_lyph_hierarchy();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void delete_lyphview( lyphview *v )
{
  char **coords;
  int id = v->id;
  extern lyphview **views;
  extern lyphview obsolete_lyphview;

  for ( coords = v->coords; *coords; coords++ )
    free( *coords );

  free( v->coords );
  free( v->nodes );

  if ( v->name )
    free( v->name );

  views[id] = &obsolete_lyphview;

  free( v );
}

HANDLER( handle_delete_views_request )
{
  char *viewstr, *err;
  lyphview **v, **vptr;
  int *i, *iptr;
  extern lyphview **views;
  extern lyphview obsolete_lyphview;

  viewstr = get_url_param( params, "views" );

  if ( !viewstr )
  {
    viewstr = get_url_param( params, "view" );

    if ( !viewstr )
      HND_ERR( "You did not indicate which views to delete." );
  }

  v = (lyphview **) PARSE_LIST( viewstr, lyphview_by_id, "view", &err );

  if ( !v )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated views was not found in the database." );
  }

  CREATE( i, int, VOIDLEN( v ) + 1 );

  for ( vptr = v, iptr = i; *vptr; vptr++ )
    *iptr++ = (*vptr)->id;

  *iptr = -1;
  free( v );

  for ( iptr = i; *iptr != -1; iptr++ )
  {
    if ( views[*iptr] != &obsolete_lyphview )
      delete_lyphview( views[*iptr] );
  }

  free( i );

  save_lyphviews();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void spread_lyphdoom_from_layers( trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( L->flags != LYPH_BEING_DELETED
    && ( L->type == LYPH_MIX || L->type == LYPH_SHELL ) )
    {
      layer **lyr;

      for ( lyr = L->layers; *lyr; lyr++ )
        if ( (*lyr)->thickness == LAYER_BEING_DELETED_THICKNESS )
          break;

      if ( *lyr )
        L->flags = LYPH_BEING_DELETED;
    }
  }

  TRIE_RECURSE( spread_lyphdoom_from_layers( *child ) );
}

HANDLER( handle_delete_layers_request )
{
  char *layerstr, *err;
  layer **lyr, **lyrptr, dupe;

  layerstr = get_url_param( params, "layers" );

  if ( !layerstr )
  {
    layerstr = get_url_param( params, "layer" );

    if ( !layerstr )
      HND_ERR( "You did not specify which layers to delete." );
  }

  lyr = (layer **) PARSE_LIST( layerstr, layer_by_id, "layer", &err );

  if ( !lyr )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated layers could not be found in the database." );
  }

  for ( lyrptr = lyr; *lyrptr; lyrptr++ )
    if ( (*lyrptr)->thickness == LAYER_BEING_DELETED_THICKNESS )
      *lyrptr = &dupe;

  free( lyr );

  spread_lyphdoom_from_layers( lyph_ids );

  while ( spread_lyphdoom( lyph_ids ) )
    ;

  delete_doomed_layers( layer_ids );
  delete_doomed_lyphs( lyph_ids );

  save_lyphs();

  if ( remove_doomed_lyphs_from_edges( lyphedge_ids ) )
    save_lyphedges();

  recalculate_lyph_hierarchy();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

HANDLER( handle_nodes_from_view_request )
{
  char *viewstr, *nodestr, *err = NULL, **newc, **newcptr, **cptr;
  lyphview *v;
  lyphnode **n, **nptr, **newn, **newnptr;
  int size, fMatch;

  viewstr = get_url_param( params, "view" );

  if ( !viewstr )
    HND_ERR( "You did not specify which view to remove nodes from" );

  v = lyphview_by_id( viewstr );

  if ( !v )
    HND_ERR( "The indicated lyphview was not found in the database" );

  nodestr = get_url_param( params, "nodes" );

  if ( !nodestr )
  {
    nodestr = get_url_param( params, "node" );

    if ( !nodestr )
      HND_ERR( "You did not specify which nodes to remove from the view" );
  }

  n = (lyphnode **) PARSE_LIST( nodestr, lyphnode_by_id, "node", &err );

  if ( !n )
  {
    if ( err )
      HND_ERR_FREE( err );
    HND_ERR( "One of the indicated nodes could not be found in the database" );
  }

  #define LYPHNODE_TO_BE_REMOVED 1

  for ( nptr = n; *nptr; nptr++ )
    SET_BIT( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED );

  for ( nptr = v->nodes, size = 0, fMatch = 0; *nptr; nptr++ )
  {
    if ( IS_SET( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED ) )
      fMatch = 1;
    else
      size++;
  }

  if ( !fMatch )
  {
    send_200_response( req, lyphview_to_json( v ) );
    goto nodes_from_view_cleanup;
  }

  if ( !size )
  {
    HND_ERR_NORETURN( "Could not remove indicated nodes: the view would become empty!" );
    goto nodes_from_view_cleanup;
  }

  CREATE( newn, lyphnode *, size + 1 );
  newnptr = newn;
  CREATE( newc, char *, (size*2) + 1 );
  newcptr = newc;

  for ( nptr = v->nodes, cptr = v->coords; *nptr; nptr++ )
  {
    if ( IS_SET( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED ) )
    {
      free( *cptr++ );
      free( *cptr++ );
    }
    else
    {
      *newnptr++ = *nptr;
      *newcptr++ = *cptr++;
      *newcptr++ = *cptr++;
    }
  }

  *newcptr = NULL;
  *newnptr = NULL;

  free( v->nodes );
  v->nodes = newn;
  free( v->coords );
  v->coords = newc;

  send_200_response( req, lyphview_to_json( v ) );

  nodes_from_view_cleanup:

  for ( nptr = n; *nptr; nptr++ )
    REMOVE_BIT( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED );

  free( n );

  #undef LYPHNODE_TO_BE_REMOVED
}
