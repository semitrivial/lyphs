#include "lyph.h"
#include "srv.h"

HANDLER( handle_editlyphnode_request )
{
  lyphnode *n;
  lyph *loc;
  char *idstr, *locstr, *loctypestr;
  int loctype;

  TRY_PARAM( idstr, "node", "You did not specify which lyphnode to edit" );

  n = lyphnode_by_id( idstr );

  if ( !n )
    HND_ERR( "The indicated lyphnode was not found in the database" );

  locstr = get_param( params, "location" );

  if ( locstr )
  {
    loc = lyph_by_id( locstr );

    if ( !loc )
      HND_ERR( "The indicated lyph was not found in the database." );
  }
  else
    loc = NULL;

  loctypestr = get_param( params, "loctype" );

  if ( loctypestr )
  {
    if ( !loc && !n->location )
      HND_ERR( "You cannot edit the loctype of this lyphnode because it does not have a location." );

    if ( !strcmp( loctypestr, "interior" ) )
      loctype = LOCTYPE_INTERIOR;
    else if ( !strcmp( loctypestr, "border" ) )
      loctype = LOCTYPE_BORDER;
    else
      HND_ERR( "Valid loctypes are: 'interior', 'border'" );
  }
  else if ( loc && !n->location )
    HND_ERR( "Please specify a loctype ('interior' or 'border') for the node" );

  if ( loc )
    n->location = loc;

  if ( loctypestr )
    n->loctype = loctype;

  save_lyphs();

  send_200_response( req, lyphnode_to_json( n ) );
}

HANDLER( handle_editlyph_request )
{
  char *lyphid, *tmpltid, *typestr, *namestr, *fmastr, *constraintstr, *fromstr, *tostr;
  char *err = NULL;
  lyph *e;
  lyphnode *from, *to;
  lyphplate *L, *constraint;
  trie *fma;
  int type;

  TRY_PARAM( lyphid, "lyph", "You did not specify which lyph to edit." );

  e = lyph_by_id( lyphid );

  if ( !e )
    HND_ERR( "The specified lyph was not found in the database." );

  tmpltid = get_param( params, "template" );

  if ( tmpltid )
  {
    L = lyphplate_by_id( tmpltid );

    if ( !L )
      HND_ERR( "The indicated template was not found in the database." );

    if ( !can_assign_lyphplate_to_lyph( L, e, &err ) )
      HND_ERR( err ? err : "The indicated template could not be assigned to the indicated lyph." );
  }
  else
    L = NULL;

  typestr = get_param( params, "type" );

  if ( typestr )
  {
    type = strtol( typestr, NULL, 10 );
    if ( type < 1 || type > 4 )
      HND_ERR( "Valid edge types are 1, 2, 3, and 4." );
  }

  namestr = get_param( params, "name" );
  fmastr = get_param( params, "fma" );

  if ( fmastr )
    fma = trie_strdup( fmastr, lyph_fmas );
  else
    fma = NULL;

  constraintstr = get_param( params, "constraint" );

  if ( constraintstr )
  {
#ifdef PRE_LAYER_CHANGE
    constraint = lyphplate_by_id( constraintstr );

    if ( !constraint )
      HND_ERR( "The indicated constraint was not found in the database." );
#else
    HND_ERR( "Constraints are temporarily disabled due to change in layer structure" );
#endif
  }
  else
    constraint = NULL;

  fromstr = get_param( params, "from" );

  if ( fromstr )
  {
    from = lyphnode_by_id( fromstr );

    if ( !from )
      HND_ERR( "The indicated 'from' node was not found in the database." );
  }
  else
    from = NULL;

  tostr = get_param( params, "to" );

  if ( tostr )
  {
    to = lyphnode_by_id( tostr );

    if ( !to )
      HND_ERR( "The indicated 'to' node was not found in the database." );
  }
  else
    to = NULL;

#ifdef PRE_LAYER_CHANGE
  if ( L && constraint )
  {
    if ( !is_superlyphplate( constraint, L ) )
      HND_ERR( "The indicated template is not a subtemplate of the indicated constraint" );
  }
  else if ( L )
  {
    lyphplate **c;

    for ( c = e->constraints; *c; c++ )
      if ( !is_superlyphplate( *c, L ) )
        HND_ERR( "The indicated template is ruled out by one of the lyph's constraints" );
  }
  else if ( constraint )
  {
    if ( e->lyphplate && !is_superlyphplate( constraint, e->lyphplate ) )
      HND_ERR( "The lyph's template is not a subtemplate of the indicated constraint" );
  }
#endif

  if ( namestr )
  {
    trie *name = trie_strdup( namestr, lyph_names );

    if ( e->name )
      e->name->data = NULL;

    e->name = name;
    name->data = (trie **)e;
  }

  if ( typestr )
    e->type = type;

  if ( L )
    e->lyphplate = L;

  if ( constraint )
  {
    free( e->constraints );
    CREATE( e->constraints, lyphplate *, 2 );
    e->constraints[0] = constraint;
    e->constraints[1] = NULL;
  }

  if ( fma )
    e->fma = fma;

  if ( from && to )
    HND_ERR( "Can't edit the edge's 'from' and 'to' with the same command" );

  if ( from )
  {
    remove_from_exits( e, &e->from->exits );
    add_to_exits( e, e->to, &from->exits );
    change_source_of_exit( e, from, e->to->incoming );
    e->from = from;
  }

  if ( to )
  {
    remove_from_exits( e, &e->to->incoming );
    add_to_exits( e, e->from, &to->incoming );
    change_dest_of_exit( e, to, e->from->exits );
    e->to = to;
  }

  save_lyphs();

  send_200_response( req, lyph_to_json( e ) );
}

HANDLER( handle_edit_template_request )
{
  lyphplate *L;
  trie *ont;
  char *tmpltstr, *namestr, *typestr, *ontstr;
  int type, fQualitativeChange = 0;

  TRY_PARAM( tmpltstr, "template", "You did not specify which template to edit" );

  L = lyphplate_by_id( tmpltstr );

  if ( !L )
    HND_ERR( "The specified template was not found in the database" );

  namestr = get_param( params, "name" );
  typestr = get_param( params, "type" );

  if ( typestr )
  {
    if ( !strcmp( typestr, "basic" ) )
    {
      if ( L->type != LYPHPLATE_BASIC )
        HND_ERR( "Currently, editing a non-basic template into type basic is not yet implemented" );

      type = -1;
    }
    else if ( !strcmp( typestr, "mix" ) )
    {
      if ( L->type == LYPHPLATE_BASIC )
        HND_ERR( "Currently, editing a basic template into type mix is not yet implemented" );

      type = LYPHPLATE_MIX;
    }
    else if ( !strcmp( typestr, "shell" ) )
    {
      if ( L->type == LYPHPLATE_BASIC )
        HND_ERR( "Currently, editing a basic template into type shell is not yet implemented" );

      type = LYPHPLATE_SHELL;
    }
    else
      HND_ERR( "Valid template types are 'basic', 'mix', and 'shell'" );
  }
  else
    type = -1;

  ontstr = get_param( params, "ont" );

  if ( ontstr )
  {
    lyphplate *rival;

    ont = trie_search( ontstr, superclasses );

    if ( !ont )
      HND_ERR( "The indicated ontology term was not found in the database" );

    rival = lyphplate_by_ont_term( ont );

    if ( rival && rival != L )
      HND_ERR( "There is already a template with the indicated ontology term" );
  }
  else
    ont = NULL;

  if ( namestr )
  {
    L->name->data = NULL;
    L->name = trie_strdup( namestr, lyphplate_names );
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

#ifdef PRE_LAYER_CHANGE
  if ( fQualitativeChange )
    recalculate_lyphplate_hierarchy();
#else
  /*
   * Suppress warning about fQualitativeChange being set but not used
   */
  if ( fQualitativeChange && 0 )
    printf( "..." );
#endif

  save_lyphplates();

  send_200_response( req, lyphplate_to_json( L ) );
}

HANDLER( handle_editview_request )
{
  char *viewstr, *namestr;
  lyphview *v;

  TRY_PARAM( viewstr, "view", "You did not specify which lyphview to edit" );

  v = lyphview_by_id( viewstr );

  if ( !v )
    HND_ERR( "The indicated view was not found in the database" );

  namestr = get_param( params, "name" );

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
  lyphplate **mat;
  char *lyrstr, *matstr, *thkstr;
  int thk, fQualitativeChange = 0;

  TRY_PARAM( lyrstr, "layer", "You did not indicate which layer to edit" );

  lyr = layer_by_id( lyrstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not found in the database" );

  matstr = get_param( params, "material" );

  thkstr = get_param( params, "thickness" );

  if ( thkstr )
  {
    thk = strtol( thkstr, NULL, 10 );

    if ( thk < 1 )
      HND_ERR( "Thickness must be a positive integer" );
  }
  else
    thk = -1;

  if ( matstr )
  {
    char *err;

    mat = (lyphplate **)PARSE_LIST( matstr, lyphplate_by_id, "template", &err );

    if ( !mat )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated templates was not recognized" );
    }
  }
  else
    mat = NULL;

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

#ifdef PRE_LAYER_CHANGE
  if ( fQualitativeChange )
    recalculate_lyphplate_hierarchy();
#else
  /*
   * Suppress compiler warning about variable being set and never used
   */
  if ( fQualitativeChange && 0 )
    printf( "..." );
#endif

  save_lyphplates();

  send_200_response( req, layer_to_json( lyr ) );
}

void remove_exit_data( lyphnode *n, lyph *e )
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

int delete_lyph( lyph *e )
{
  int fAnnot;

  if ( *e->annots )
    fAnnot = 1;
  else
    fAnnot = 0;

  e->id->data = NULL;

  free( e->constraints );
  free( e->annots );

  remove_exit_data( e->from, e );

  free( e );

  return fAnnot;
}

void remove_deleted_lyph_locations( trie *t )
{
  if ( t->data )
  {
    lyphnode *n = (lyphnode *)t->data;

    if ( n->location && n->location->type == LYPH_DELETED )
    {
      n->location = NULL;
      n->loctype = -1;
    }
  }

  TRIE_RECURSE( remove_deleted_lyph_locations( *child ) );
}

#define LYPHNODE_BEING_DELETED 1

int remove_doomed_items_from_views( void )
{
  int i, fMatch = 0;
  extern int top_view;
  extern lyphview **views;
  extern lyphview obsolete_lyphview;

  for ( i = 0; i <= top_view; i++ )
  {
    lyphview *v;
    lyphnode **nptr;
    lv_rect **rptr;
    char **cptr, **newc, **newcptr;
    int size;

    if ( views[i] == &obsolete_lyphview || !views[i] )
      continue;

    v = views[i];

    for ( nptr = v->nodes; *nptr; nptr++ )
      if ( (*nptr)->flags == LYPHNODE_BEING_DELETED )
        break;

    if ( *nptr )
    {
      lyphnode **buf, **bptr;

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

      MULTIFREE( v->nodes, v->coords );
      v->nodes = buf;
      v->coords = newc;
    }

    for ( rptr = v->rects; *rptr; rptr++ )
      if ( (*rptr)->L->type == LYPH_DELETED )
        break;

    if ( *rptr )
    {
      lv_rect **buf, **bptr;

      fMatch = 1;

      for ( rptr = v->rects, size = 0; *rptr; rptr++ )
        if ( (*rptr)->L->type != LYPH_DELETED )
          size++;

      CREATE( buf, lv_rect *, size + 1 );
      bptr = buf;

      for ( rptr = v->rects; *rptr; rptr++ )
      {
        if ( (*rptr)->L->type == LYPH_DELETED )
          free( *rptr );
        else
          *bptr++ = *rptr;
      }

      *rptr = NULL;
      free( v->rects );
      v->rects = buf;
    }
  }

  return fMatch;
}

HANDLER( handle_delete_lyphs_request )
{
  char *lyphstr, *err;
  lyph **e, **eptr, dupe;
  int fAnnot;

  TRY_TWO_PARAMS( lyphstr, "lyphs", "lyph", "You did not specify which lyphs to delete." );

  e = (lyph **) PARSE_LIST( lyphstr, lyph_by_id, "lyph", &err );

  if ( !e )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs was not found in the database." );
  }

  for ( eptr = e; *eptr; eptr++ )
  {
    if ( (*eptr)->type == LYPH_DELETED )
      *eptr = &dupe;
    else
      (*eptr)->type = LYPH_DELETED;
  }

  if ( remove_doomed_items_from_views() )
    save_lyphviews();

  remove_deleted_lyph_locations(lyphnode_ids);

  for ( eptr = e, fAnnot = 0; *eptr; eptr++ )
    if ( *eptr != &dupe )
      fAnnot |= delete_lyph( *eptr );

  free( e );

  save_lyphs();

  if ( fAnnot )
    save_annotations();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

int remove_lyphs_with_doomed_nodes( trie *t )
{
  int fAnnot = 0;

  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->from->flags == LYPHNODE_BEING_DELETED
    ||   e->to->flags   == LYPHNODE_BEING_DELETED )
      fAnnot = delete_lyph( e );
  }

  TRIE_RECURSE( fAnnot |= remove_lyphs_with_doomed_nodes( *child ) );

  return fAnnot;
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

  TRY_TWO_PARAMS( nodestr, "nodes", "node", "You did not specify which nodes to delete" );

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

  remove_lyphs_with_doomed_nodes( lyph_ids );

  if ( remove_doomed_items_from_views() )
    save_lyphviews();

  for ( nptr = n; *nptr; nptr++ )
    if ( *nptr != &dupe )
      delete_lyphnode( *nptr );

  save_lyphs();

  free( n );

  send_200_response( req, JSON1( "Response": "OK" ) );
}

#undef LYPHNODE_BEING_DELETED

#define LYPHPLATE_BEING_DELETED 1
#define LAYER_BEING_DELETED_THICKNESS -2

int remove_doomed_lyphplates_from_lyphs( trie *t )
{
  int fMatch = 0;

  if ( t->data )
  {
    lyph *e = (lyph *)t->data;
    lyphplate **c;

    if ( e->lyphplate && e->lyphplate->flags == LYPHPLATE_BEING_DELETED )
    {
      e->lyphplate = NULL;
      fMatch = 1;
    }

    for ( c = e->constraints; *c; c++ )
      if ( (*c)->flags == LYPHPLATE_BEING_DELETED )
        break;

    if ( *c )
    {
      lyphplate **newc, **newcptr;
      int size = 0;

      for ( c = e->constraints; *c; c++ )
        if ( (*c)->flags != LYPHPLATE_BEING_DELETED )
          size++;

      CREATE( newc, lyphplate *, size + 1 );

      for ( c = e->constraints, newcptr = newc; *c; c++ )
        if ( (*c)->flags != LYPHPLATE_BEING_DELETED )
          *newcptr++ = *c;

      *newcptr = NULL;
      free( e->constraints );
      e->constraints = newc;
      fMatch = 1;
    }
  }

  TRIE_RECURSE( fMatch |= remove_doomed_lyphplates_from_lyphs( *child ) );

  return fMatch;
}

int spread_lyphplate_doom( trie *t )
{
  int fMatch = 0;

  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( L->flags != LYPHPLATE_BEING_DELETED
    && ( L->type == LYPHPLATE_MIX || L->type == LYPHPLATE_SHELL ) )
    {
      layer **lyr;
      lyphplate **materials;

      for ( lyr = L->layers; *lyr; lyr++ )
      for ( materials = (*lyr)->material; *materials; materials++ )
        if ( (*materials)->flags == LYPHPLATE_BEING_DELETED )
          break;

      if ( *lyr )
      {
        L->flags = LYPHPLATE_BEING_DELETED;
        fMatch = 1;
      }
    }
  }

  TRIE_RECURSE( fMatch |= spread_lyphplate_doom( *child ) );

  return fMatch;
}

int layer_has_doomed_material( layer *lyr )
{
  lyphplate **materials;

  for ( materials = lyr->material; *materials; materials++ )
    if ( (*materials)->flags == LYPHPLATE_BEING_DELETED )
      return 1;

  return 0;
}

void delete_doomed_layers( trie *t )
{
  if ( t->data )
  {
    layer *lyr = (layer *)t->data;

    if ( lyr->thickness == LAYER_BEING_DELETED_THICKNESS
    ||   layer_has_doomed_material( lyr ) )
    {
      t->data = NULL;
      free( lyr );
    }
  }

  TRIE_RECURSE( delete_doomed_layers( *child ) );
}

void delete_doomed_lyphplates( trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( L->flags == LYPHPLATE_BEING_DELETED )
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

  TRIE_RECURSE( delete_doomed_lyphplates( *child ) );
}

HANDLER( handle_delete_templates_request )
{
  char *tmpltstr, *err;
  lyphplate **L, **Lptr, dupe;

  TRY_TWO_PARAMS( tmpltstr, "templates", "template", "You did not indicate which templates to delete." );

  L = (lyphplate **) PARSE_LIST( tmpltstr, lyphplate_by_id, "template", &err );

  if ( !L )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated templates could not be found in the database" );
  }

  for ( Lptr = L; *Lptr; Lptr++ )
  {
    if ( (*Lptr)->flags == LYPHPLATE_BEING_DELETED )
      *Lptr = &dupe;
    else
      (*Lptr)->flags = LYPHPLATE_BEING_DELETED;
  }

  free( L );

  /*
   * Any lyphplate/layer which refers to a doomed lyphplate/layer should also be doomed
   */
  while ( spread_lyphplate_doom( lyphplate_ids ) )
    ;

  if ( remove_doomed_lyphplates_from_lyphs( lyph_ids ) )
    save_lyphs();

  delete_doomed_layers( layer_ids );
  delete_doomed_lyphplates( lyphplate_ids );

  save_lyphplates();

#ifdef PRE_LAYER_CHANGE
  recalculate_lyphplate_hierarchy();
#endif

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void delete_lyphview( lyphview *v )
{
  lv_rect **rptr;
  char **coords;
  int id = v->id;
  extern lyphview **views;
  extern lyphview obsolete_lyphview;

  for ( coords = v->coords; *coords; coords++ )
    free( *coords );

  for ( rptr = v->rects; *rptr; rptr++ )
    free( *rptr );

  MULTIFREE( v->coords, v->nodes, v->rects );

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

  TRY_TWO_PARAMS( viewstr, "views", "view", "You did not indicate which views to delete." );

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

void spread_lyphplate_doom_from_layers( trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( L->flags != LYPHPLATE_BEING_DELETED
    && ( L->type == LYPHPLATE_MIX || L->type == LYPHPLATE_SHELL ) )
    {
      layer **lyr;

      for ( lyr = L->layers; *lyr; lyr++ )
        if ( (*lyr)->thickness == LAYER_BEING_DELETED_THICKNESS )
          break;

      if ( *lyr )
        L->flags = LYPHPLATE_BEING_DELETED;
    }
  }

  TRIE_RECURSE( spread_lyphplate_doom_from_layers( *child ) );
}

HANDLER( handle_delete_layers_request )
{
  char *layerstr, *err;
  layer **lyr, **lyrptr;

  TRY_TWO_PARAMS( layerstr, "layers", "layer", "You did not specify which layers to delete." );

  lyr = (layer **) PARSE_LIST( layerstr, layer_by_id, "layer", &err );

  if ( !lyr )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated layers could not be found in the database." );
  }

  for ( lyrptr = lyr; *lyrptr; lyrptr++ )
    (*lyrptr)->thickness = LAYER_BEING_DELETED_THICKNESS;

  free( lyr );

  spread_lyphplate_doom_from_layers( lyphplate_ids );

  while ( spread_lyphplate_doom( lyphplate_ids ) )
    ;

  if ( remove_doomed_lyphplates_from_lyphs( lyph_ids ) )
    save_lyphs();

  delete_doomed_layers( layer_ids );
  delete_doomed_lyphplates( lyphplate_ids );

  save_lyphplates();

#ifdef PRE_LAYER_CHANGE
  recalculate_lyphplate_hierarchy();
#endif

  send_200_response( req, JSON1( "Response": "OK" ) );
}

HANDLER( handle_lyphs_from_view_request )
{
  lyphview *v;
  lyph **lyphs, **lptr;
  lv_rect **buf, **bptr, **rptr;
  char *viewstr, *lyphstr, *err;
  int size;

  TRY_PARAM( viewstr, "view", "You did not specify which view to remove the lyphs from" );

  v = lyphview_by_id( viewstr );

  if ( !v )
    HND_ERR( "The indicated lyphview was not found in the database" );

  TRY_TWO_PARAMS( lyphstr, "lyphs", "lyph", "You did not specify which lyphs to remove from the view" );

  lyphs = (lyph **) PARSE_LIST( lyphstr, lyph_by_id, "lyph", &err );

  if ( !lyphs )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs could not be found in the database" );
  }

  #define LYPH_TO_BE_REMOVED 1

  for ( lptr = lyphs; *lptr; lptr++ )
    SET_BIT( (*lptr)->flags, LYPH_TO_BE_REMOVED );

  for ( rptr = v->rects, size = 0; *rptr; rptr++ )
    if ( !IS_SET( (*lptr)->flags, LYPH_TO_BE_REMOVED ) )
      size++;

  CREATE( buf, lv_rect *, size + 1 );
  bptr = buf;

  for ( rptr = v->rects, size = 0; *rptr; rptr++ )
  {
    if ( IS_SET( (*rptr)->L->flags, LYPH_TO_BE_REMOVED ) )
      free( *rptr );
    else
      *bptr++ = *rptr;
  }

  *bptr = NULL;
  free( v->rects );
  v->rects = buf;

  for ( lptr = lyphs; *lptr; lptr++ )
    REMOVE_BIT( (*lptr)->flags, LYPH_TO_BE_REMOVED );

  save_lyphviews();

  #undef LYPH_TO_BE_REMOVED

  send_200_response( req, lyphview_to_json( v ) );
}

int template_involves( lyphplate *L, lyphplate *part )
{
  #define LYPHPLATE_DOES_INVOLVE 1
  #define LYPHPLATE_DOES_NOT_INVOLVE 2

  if ( IS_SET( L->flags, LYPHPLATE_DOES_INVOLVE ) )
    return 1;
  else
  if ( IS_SET( L->flags, LYPHPLATE_DOES_NOT_INVOLVE ) )
    return 0;
  else
  {
    int answer;

    if ( L == part )
    {
      SET_BIT( L->flags, LYPHPLATE_DOES_INVOLVE );
      return 1;
    }

    switch( L->type )
    {
      case LYPHPLATE_BASIC:
        answer = 0;
        break;
      case LYPHPLATE_SHELL:
      case LYPHPLATE_MIX:
      {
        layer **lyr;
        lyphplate **materials;

        for ( lyr = L->layers; *lyr; lyr++ )
        for ( materials = (*lyr)->material; *materials; materials++ )
          if ( template_involves( *materials, part ) )
            break;

        if ( *lyr )
          answer = 1;
        else
          answer = 0;
        break;
      }
    }

    if ( answer )
      SET_BIT( L->flags, LYPHPLATE_DOES_INVOLVE );
    else
      SET_BIT( L->flags, LYPHPLATE_DOES_NOT_INVOLVE );

    return answer;
  }
}

void calc_involves_template( lyphplate *L, lyph_wrapper **head, lyph_wrapper **tail, int *cnt, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->lyphplate && template_involves( e->lyphplate, L ) )
    {
      lyph_wrapper *w;

      CREATE( w, lyph_wrapper, 1 );
      w->e = e;
      LINK( w, *head, *tail, next );
      (*cnt)++;
    }
  }

  TRIE_RECURSE( calc_involves_template( L, head, tail, cnt, *child ) );
}

HANDLER( handle_involves_template_request )
{
  lyphplate *L;
  lyph_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyph **buf, **bptr;
  char *tmpidstr;
  int cnt;

  TRY_PARAM( tmpidstr, "template", "Missing argument: 'template': Specify a template, X, in order to find all lyphs L such that L has a template Y that involves X in any way" );

  L = lyphplate_by_id( tmpidstr );

  if ( !L )
    HND_ERR( "The indicated lyphplate was not found in the database" );

  cnt = 0;

  calc_involves_template( L, &head, &tail, &cnt, lyph_ids );

  lyphs_unset_bits( LYPHPLATE_DOES_INVOLVE | LYPHPLATE_DOES_NOT_INVOLVE, lyph_ids );

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;

    *bptr++ = w->e;
    free( w );
  }
  *bptr = NULL;

  send_200_response( req, JSON1
  (
    "lyphs": JS_ARRAY( lyph_to_json, buf )
  ) );

  free( buf );
}

void find_instances_of( lyphplate *L, lyph_wrapper **head, lyph_wrapper **tail, int *cnt, trie *t )
{
#ifdef PRE_LAYER_CHANGE
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->lyphplate && is_superlyphplate( L, e->lyphplate ) )
    {
      lyph_wrapper *w;

      CREATE( w, lyph_wrapper, 1 );
      w->e = e;

      LINK( w, *head, *tail, next );
      (*cnt)++;
    }
  }

  TRIE_RECURSE( find_instances_of( L, head, tail, cnt, *child ) );
#endif
}

HANDLER( handle_instances_of_request )
{
#ifdef PRE_LAYER_CHANGE
  lyphplate *L;
  lyph_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyph **buf, **bptr;
  char *tmpidstr;
  int cnt;

  TRY_PARAM( tmpidstr, "template", "Missing argument: 'template': specify a template, X, and instances_of will search for all lyphs that have template Y such that Y is a subtemplate of X." );

  L = lyphplate_by_id( tmpidstr );

  if ( !L )
    HND_ERR( "There indicated template was not found in the database" );

  cnt = 0;

  find_instances_of( L, &head, &tail, &cnt, lyph_ids );

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;

    *bptr++ = w->e;
    free( w );
  }
  *bptr = NULL;

  send_200_response( req, JSON1
  (
    "instances": JS_ARRAY( lyph_to_json, buf )
  ) );

  free( buf );
#else
  send_200_response( req, JSON1( "Response": "Temporarily disabled due to layer structure change" ) );
#endif
}

HANDLER( handle_nodes_from_view_request )
{
  char *viewstr, *nodestr, *err = NULL, **newc, **newcptr, **cptr;
  lyphview *v;
  lyphnode **n, **nptr, **newn, **newnptr;
  int size, fMatch;

  TRY_PARAM( viewstr, "view", "You did not specify which view to remove nodes from" );

  v = lyphview_by_id( viewstr );

  if ( !v )
    HND_ERR( "The indicated lyphview was not found in the database" );

  TRY_TWO_PARAMS( nodestr, "nodes", "node", "You did not specify which nodes to remove from the view" );

  n = (lyphnode **) PARSE_LIST( nodestr, lyphnode_by_id, "node", &err );

  if ( !n )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
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

  MULTIFREE( v->nodes, v->coords );
  v->nodes = newn;
  v->coords = newc;

  send_200_response( req, lyphview_to_json( v ) );

  nodes_from_view_cleanup:

  for ( nptr = n; *nptr; nptr++ )
    REMOVE_BIT( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED );

  free( n );

  #undef LYPHNODE_TO_BE_REMOVED
}

void **get_numbered_args( url_param **params, char *base, char * (*fnc) (void *), char **err, int *size )
{
  static void **buf, **vals;
  void **bptr, **retval;
  url_param **p;
  int baselen = strlen( base );
  int i, cnt;

  if ( !buf )
  {
    CREATE( buf, void *, MAX_URL_PARAMS + 1 );
    CREATE( vals, void *, MAX_URL_PARAMS + 1 );
  }
  else
    memset( vals, 0, (MAX_URL_PARAMS+1) * sizeof(void*) );

  for ( p = params; *p; p++ )
  {
    if ( str_begins( (*p)->key, base ) )
    {
      int n = strtoul( (*p)->key + baselen, NULL, 10 );

      if ( n < 1 || n >= MAX_URL_PARAMS )
        continue;

      vals[n] = (*p)->val;
    }
  }

  bptr = buf;

  if ( fnc )
  {
    for ( i = 1; vals[i]; i++ )
    {
      void *x = (*fnc)(vals[i]);

      if ( !x )
      {
        if ( err )
          *err = strdupf( "There was no %s with id '%s' in the database", base, vals[i] );

        return NULL;
      }

      *bptr++ = x;
    }
  }
  else
    for ( i = 1; vals[i]; i++ )
      *bptr++ = vals[i];

  *bptr = NULL;

  cnt = bptr - buf;

  CREATE( retval, void *, cnt + 1 );
  memcpy( retval, buf, (cnt + 1) * sizeof(void *) );

  if ( size )
    *size = cnt;

  return retval;
}

void find_lyphs_with_template( lyphplate *L, lyph ***bptr, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( e->lyphplate == L )
    {
      **bptr = e;
      (*bptr)++;
    }
  }

  TRIE_RECURSE( find_lyphs_with_template( L, bptr, *child ) );
}

HANDLER( handle_has_template_request )
{
  lyphplate *L;
  lyph **buf, **bptr;
  char *tmpltid;
  int cnt;

  TRY_PARAM( tmpltid, "template", "You did not specify a template" );

  L = lyphplate_by_id( tmpltid );

  if ( !L )
    HND_ERR( "The indicated template was not recognized" );

  cnt = count_nontrivial_members( lyph_ids );

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  find_lyphs_with_template( L, &bptr, lyph_ids );
  *bptr = NULL;

  send_200_response( req, JS_ARRAY( lyph_to_json, buf ) );

  free( buf );
}

int index_is_used( clinical_index *ci, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;
    annot **a;

    for ( a = e->annots; *a; a++ )
      if ( (*a)->obj == ci->index )
        return 1;
  }

  TRIE_RECURSE
  (
    if ( index_is_used( ci, *child ) )
      return 1;
  );

  return 0;
}

HANDLER( handle_unused_indices_request )
{
  clinical_index *ci, **buf, **bptr;
  int cnt = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  CREATE( buf, clinical_index *, cnt + 1 );
  bptr = buf;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    if ( !index_is_used( ci, lyph_ids ) )
      *bptr++ = ci;

  *bptr = NULL;

  send_200_response( req, JS_ARRAY( clinical_index_to_json_full, buf ) );

  free( buf );
}
