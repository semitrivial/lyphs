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

  constraintstr = get_url_param( params, "constraints" );

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
    ont = trie_search( ontstr, superclasses );

    if ( !ont )
      HND_ERR( "The indicated ontology term was not found in the database" );

    if ( lyph_by_ont_term( ont ) )
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

HANDLER( handle_delete_edge_request )
{
  send_200_response( req, "This command is under construction." );
}

HANDLER( handle_delete_node_request )
{
  send_200_response( req, "This command is under construction." );
}

HANDLER( handle_delete_lyph_request )
{
  send_200_response( req, "This command is under construction." );
}

HANDLER( handle_delete_view_request )
{
  send_200_response( req, "This command is under construction." );
}

HANDLER( handle_delete_layer_request )
{
  send_200_response( req, "This command is under construction." );
}
