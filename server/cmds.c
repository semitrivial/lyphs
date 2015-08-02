/*
 *  cmds.c
 *  Various miscelaneous API commands
 *  (Including the complicated delete/edit commands for core structures)
 */
#include "lyph.h"
#include "srv.h"

HANDLER( do_clone )
{
  char *templatestr, *lyphstr, *layerstr;
  int matches=0;
  
  if ( (templatestr = get_param(params,"template")) != NULL )
    matches++;
    
  if ( (lyphstr = get_param(params,"lyph")) != NULL )
    matches++;
    
  if ( (layerstr = get_param(params,"layer")) != NULL )
    matches++;
    
  if ( matches < 1 )
    HND_ERR( "What do you want to clone?  Specify a 'lyph', 'template', or 'layer'." );
  
  if ( matches > 1 )
    HND_ERR( "Please limit the clone command to one thing ('lyph', 'template', or 'layer') at a time" );
    
  if ( templatestr )
  {
    lyphplate *L = lyphplate_by_id( templatestr );
    
    if ( !L )
      HND_ERR( "The indicated template was not recognized" );
      
    L = clone_template( L );
    
    save_lyphplates();
    
    send_response( req, lyphplate_to_json( L ) );
    
    return;
  }
  
  if ( layerstr )
  {
    layer *lyr = layer_by_id( layerstr );
    
    if ( !lyr )
      HND_ERR( "The indicated layer was not recognized" );
    
    lyr = clone_layer( lyr );
    
    save_lyphplates( );
    
    send_response( req, layer_to_json( lyr ) );
    
    return;
  }
  
  if ( lyphstr )
  {
    lyph *e = lyph_by_id( lyphstr );
    
    if ( !e )
      HND_ERR( "The indicated lyph was not recognized" );
      
    e = clone_lyph( e );
    save_lyphs();
    send_response( req, lyph_to_json( e ) );
    return;
  }
}

HANDLER( do_editlyphnode )
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

  if ( !locstr )
    locstr = get_param( params, "loc" );

  if ( locstr && strcmp( locstr, "none" ) && strcmp( locstr, "null" ) )
  {
    loc = lyph_by_id( locstr );

    if ( !loc )
      HND_ERR( "The indicated lyph was not found in the database." );

    if ( !can_node_fit_in_lyph( n, loc ) )
      HND_ERR( "The indicated node cannot be placed in the indicated location because that would place one of its incident edges inside itself" );
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
  else if ( locstr && (!strcmp( locstr, "null" ) || !strcmp( locstr, "none" )) )
    n->location = NULL;

  if ( loctypestr )
    n->loctype = loctype;

  save_lyphs();

  send_response( req, lyphnode_to_json( n ) );
}

HANDLER( do_editlyph )
{
  char *lyphid, *tmpltid, *typestr, *namestr, *fmastr, *constraintstr, *pubmedstr, *projstr;
  char *fromstr, *tostr, *speciesstr, *locstr;
  char *err = NULL;
  lyph *e, *loc;
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
    if ( type < 1 || type > 3 )
      HND_ERR( "Valid edge types are 1, 2, and 3." );
  }

  namestr = get_param( params, "name" );
  fmastr = get_param( params, "fma" );

  if ( fmastr && *fmastr )
    fma = trie_strdup( fmastr, lyph_fmas );
  else
    fma = NULL;

  constraintstr = get_param( params, "constraint" );

  if ( constraintstr )
    HND_ERR( "Constraints are temporarily disabled due to change in layer structure" );
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

  locstr = get_param( params, "loc" );

  if ( !locstr )
    locstr = get_param( params, "location" );

  if ( locstr && strcmp( locstr, "none" ) && strcmp( locstr, "null" ) )
  {
    if ( from || to )
      HND_ERR( "You can't change the lyph's 'from' or 'to' with the same command that you change its 'location'" );

    loc = lyph_by_id( locstr );

    if ( !loc )
      HND_ERR( "The indicated location was not recognized" );

    if ( !can_node_fit_in_lyph( e->from, loc )
    ||   !can_node_fit_in_lyph( e->to, loc ) )
      HND_ERR( "The indicated node cannot be placed in the indicated lyph or one of its incident edges would be self-contained" );
  }
  else
    loc = NULL;

  if ( from && to )
    HND_ERR( "Can't edit the edge's 'from' and 'to' with the same command" );

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
    e->lyphplt = L;

  projstr = get_param( params, "projection_strength" );

  if ( projstr )
  {
    if ( e->projection_strength )
      free( e->projection_strength );

    e->projection_strength = strdup( projstr );
  }

  pubmedstr = get_param( params, "pubmed" );

  if ( pubmedstr )
  {
    if ( e->pubmed )
      free( e->pubmed );

    e->pubmed = strdup( pubmedstr );
  }

  if ( constraint )
  {
    free( e->constraints );
    CREATE( e->constraints, lyphplate *, 2 );
    e->constraints[0] = constraint;
    e->constraints[1] = NULL;
  }

  if ( fma )
    e->fma = fma;
  else if ( fmastr && !*fmastr )
    e->fma = NULL;

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

  if ( loc )
  {
    e->from->location = loc;
    e->from->loctype = LOCTYPE_INTERIOR;
    e->to->location = loc;
    e->to->loctype = LOCTYPE_INTERIOR;
  }
  else if ( locstr && (!strcmp( locstr, "null" ) || !strcmp( locstr, "none" )) )
  {
    e->from->location = NULL;
    e->to->location = NULL;
  }

  speciesstr = get_param( params, "species" );

  if ( speciesstr )
  {
    if ( *speciesstr >= 'a' && *speciesstr <= 'z' )
      *speciesstr += 'A' - 'a';

    e->species = trie_strdup( speciesstr, metadata );
  }

  e->modified = longtime();

  save_lyphs();

  send_response( req, lyph_to_json( e ) );
}

HANDLER( do_edit_template )
{
  lyphplate *L, **misc_mats;
  trie *ont;
  char *tmpltstr, *namestr, *typestr, *ontstr, *miscstr, *movelayerstr, *toposstr, *lengthstr;
  int type, newpos, oldpos;

  TRY_PARAM( tmpltstr, "template", "You did not specify which template to edit" );

  L = lyphplate_by_id( tmpltstr );

  if ( !L )
    HND_ERR( "The specified template was not found in the database" );

  namestr = get_param( params, "name" );
  typestr = get_param( params, "type" );
  miscstr = get_param( params, "misc_materials" );
  movelayerstr = get_param( params, "movelayer" );
  toposstr = get_param( params, "topos" );
  
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

  if ( movelayerstr && !toposstr )
    HND_ERR( "If you want to 'movelayer' you must also specify (with 'topos') which position to move it to" );

  if ( toposstr && !movelayerstr )
    HND_ERR( "If you want to move a layer to a new position ('topos'), you must specify which layer ('movelayer')" );

  if ( movelayerstr && L->type != LYPHPLATE_SHELL && L->type != LYPHPLATE_MIX )
    HND_ERR( "You cannot move layers in this template because its type is neither shell nor mix" );
            
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

  if ( movelayerstr )
  {
    layer *lyr = layer_by_id( movelayerstr ), **lyrptr;
    
    if ( !lyr )
      HND_ERR( "The layer ('movelayer') you asked us to move, could not be recognized" );
      
    for ( lyrptr = L->layers; *lyrptr; lyrptr++ )
      if ( *lyrptr == lyr )
        break;
    
    if ( !*lyrptr )
      HND_ERR( "The indicated layer ('movelayer') is not present in this template" );
      
    oldpos = lyrptr - L->layers;
    
    newpos = strtoul( toposstr, NULL, 10 ) - 1;
    
    if ( newpos < 0 )
      HND_ERR( "'topos' should be a positive integer" );
    
    if ( newpos > VOIDLEN( L->layers ) - 1 )
      HND_ERRF( "You asked to move a layer to position %d, but the template only has %d layers in total", newpos, VOIDLEN( L->layers ) );
  }
    
  if ( miscstr )
  {
    char *err;

    if ( !strcmp( miscstr, "none" ) )
      misc_mats = (lyphplate **)blank_void_array();
    else
    {
      misc_mats = (lyphplate **)PARSE_LIST( miscstr, lyphplate_by_id, "template", &err );

      if ( !misc_mats )
      {
        if ( err )
          HND_ERR_FREE( err );
        else
          HND_ERR( "One of the indicated templates was not recognized" );
      }

      if ( is_Xs_built_from_Y( misc_mats, L ) )
      {
        free( misc_mats );
        HND_ERR( "One of the indicated miscelaneous materials is already built up from the template in question" );
      }
    }

    free( L->misc_material );

    L->misc_material = misc_mats;
  }

  if ( movelayerstr && newpos != oldpos )
  {
    layer **oldpospt = &L->layers[oldpos], **newpospt = &L->layers[newpos], **buf, **bptr, **Lptr;
    
    CREATE( buf, layer *, VOIDLEN( L->layers ) + 1 );
    bptr = buf;
    
    for ( Lptr = L->layers; *Lptr; Lptr++ )
    {
      if ( oldpospt < newpospt )
      {
        if ( Lptr < oldpospt || Lptr > newpospt )
          *bptr++ = *Lptr;
        else if ( Lptr == newpospt )
          *bptr++ = *oldpospt;
        else
          *bptr++ = Lptr[1];
      }
      else      
      {
        if ( Lptr < newpospt || Lptr > oldpospt )
          *bptr++ = *Lptr;
        else if ( Lptr == newpospt )
          *bptr++ = *oldpospt;
        else
          *bptr++ = Lptr[-1];
      }
    }
    
    *bptr = NULL;
    free( L->layers );
    L->layers = buf;
  }
  
  if ( namestr )
  {
    L->name->data = NULL;
    L->name = trie_strdup( namestr, lyphplate_names );
    L->name->data = (trie **)L;
  }

  lengthstr = get_param( params, "length" );
  
  if ( lengthstr )
  {
    if ( L->length )
      free( L->length );
    L->length = strdup( lengthstr );
  }
  
  if ( type != -1 )
    L->type = type;

  if ( ont )
    L->ont_term = ont;

  L->modified = longtime();
  save_lyphplates();

  send_response( req, lyphplate_to_json( L ) );
}

HANDLER( do_editlayer )
{
  layer *lyr;
  lyphplate **mat;
  char *lyrstr, *matstr, *thkstr, *namestr, *mutablestr;
  int thk;

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

    if ( !strcmp( matstr, "none" ) )
      mat = (lyphplate **)blank_void_array();
    else
    {
      mat = (lyphplate **)PARSE_LIST( matstr, lyphplate_by_id, "template", &err );

      if ( !mat )
      {
        if ( err )
          HND_ERR_FREE( err );
        else
          HND_ERR( "One of the indicated templates was not recognized" );
      }

      if ( is_Xs_built_from_Y( mat, lyr ) )
      {
        free( mat );
        HND_ERR( "The layer in question is already part of the construction of one of the materials in question" );
      }
    }
  }
  else
    mat = NULL;

  mutablestr = get_param( params, "mutable" );

  if ( !mutablestr || strcmp( mutablestr, "yes" ) )
    lyr = clone_layer( lyr );

  namestr = get_param( params, "name" );
  
  if ( namestr )
  {
    if ( lyr->name )
      free( lyr->name );
    
    lyr->name = !strcmp( namestr, "none" ) ? NULL : strdup( namestr );
  }
    
  if ( mat )
    lyr->material = mat;

  if ( thk != -1 )
    lyr->thickness = thk;

  save_lyphplates();

  send_response( req, layer_to_json( lyr ) );
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

void delete_located_measures_involving_lyph( lyph *e )
{
  located_measure *m, *m_next;

  for ( m = first_located_measure; m; m = m_next )
  {
    m_next = m->next;

    if ( m->loc == e )
      delete_located_measure( m );
  }
}

void delete_correlations_involving_lyph( lyph *e )
{
  correlation *c, *c_next;
  variable **v;

  for ( c = first_correlation; c; c = c_next )
  {
    c_next = c->next;

    for ( v = c->vars; *v; v++ )
      if ( (*v)->type == VARIABLE_LOCATED && (*v)->loc == e )
        break;

    if ( *v )
      delete_correlation( c );
  }
}

int delete_lyph( lyph *e )
{
  int fAnnot;
  lyph *prev;

  if ( *e->annots )
    fAnnot = 1;
  else
    fAnnot = 0;

  e->id->data = NULL;

  if ( e == first_lyph )
  {
    if ( e == last_lyph )
      last_lyph = NULL;

    first_lyph = e->next;
  }
  else
  {
    for ( prev = first_lyph; prev; prev = prev->next )
      if ( prev->next == e )
        break;

    if ( prev )
    {
      prev->next = e->next;

      if ( last_lyph == e )
        last_lyph = prev;
    }
    else
    {
      error_messagef( "Fatal error in delete_lyph:  lyph is not first_lyph, and yet has no prev" );
      EXIT();
    }
  }

  lyphcnt--;

  free( e->constraints );
  free( e->annots );

  remove_exit_data( e->from, e );

  delete_correlations_involving_lyph( e );
  save_correlations();
  delete_located_measures_involving_lyph( e );
  save_located_measures();

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
      v->modified = longtime();

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
      if ( (*rptr)->L && (*rptr)->L->type == LYPH_DELETED )
        break;

    if ( *rptr )
    {
      lv_rect **buf, **bptr;

      fMatch = 1;

      for ( rptr = v->rects, size = 0; *rptr; rptr++ )
        if ( !(*rptr)->L || (*rptr)->L->type != LYPH_DELETED )
          size++;

      CREATE( buf, lv_rect *, size + 1 );
      bptr = buf;

      for ( rptr = v->rects; *rptr; rptr++ )
      {
        if ( (*rptr)->L && (*rptr)->L->type == LYPH_DELETED )
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

HANDLER( do_delete_lyphs )
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
    save_lyph_annotations();

  send_ok( req );
}

int remove_lyphs_with_doomed_nodes( void )
{
  lyph *e, *e_next;
  int fAnnot = 0;

  for ( e = first_lyph; e; e = e_next )
  {
    e_next = e->next;

    if ( e->from->flags == LYPHNODE_BEING_DELETED
    ||   e->to->flags   == LYPHNODE_BEING_DELETED )
      fAnnot = delete_lyph( e );
  }

  return fAnnot;
}

void delete_lyphnode( lyphnode *n )
{
  free( n->exits );
  n->id->data = NULL;
  free( n );
}

HANDLER( do_delete_nodes )
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

  remove_lyphs_with_doomed_nodes( );

  if ( remove_doomed_items_from_views() )
    save_lyphviews();

  for ( nptr = n; *nptr; nptr++ )
    if ( *nptr != &dupe )
      delete_lyphnode( *nptr );

  save_lyphs();

  free( n );

  send_ok( req );
}

#undef LYPHNODE_BEING_DELETED

#define LYPHPLATE_BEING_DELETED 1
#define LAYER_BEING_DELETED_THICKNESS -2

int remove_doomed_lyphplates_from_lyphs( void )
{
  lyph *e;
  int fMatch = 0;

  for ( e = first_lyph; e; e = e->next )
  {
    lyphplate **c;

    if ( e->lyphplt && e->lyphplt->flags == LYPHPLATE_BEING_DELETED )
    {
      e->lyphplt = NULL;
      e->modified = longtime();
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
      e->modified = longtime();
      fMatch = 1;
    }
  }

  return fMatch;
}

int spread_lyphplate_doom( void )
{
  lyphplate *L;
  int fMatch = 0;

  for ( L = first_lyphplate; L; L = L->next )
  {
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

void delete_doomed_lyphplates( void )
{
  lyphplate *L, *L_next;

  for ( L = first_lyphplate; L; L = L_next )
  {
    L_next = L->next;

    if ( L->flags == LYPHPLATE_BEING_DELETED )
    {
      if ( L->id )
        L->id->data = NULL;

      if ( L->layers )
        free( L->layers );
      if ( L->supers )
        free( L->supers );
      if ( L->subs )
        free( L->subs );

      UNLINK2( L, first_lyphplate, last_lyphplate, next, prev );

      free( L );
    }
  }
}

int doomed_lyphplates_already_in_use_by_lyph( char **where )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {    
    if ( e->lyphplt && e->lyphplt->flags == LYPHPLATE_BEING_DELETED )
    {
      *where = strdupf( "Lyph %s", trie_to_static( e->id ) );
      return 1;
    }
  }
  
  return 0;
}

int doomed_lyphplates_already_in_use_by_lyphplate( char **where )
{
  lyphplate *L;

  for ( L = first_lyphplate; L; L = L->next )
  {
    if ( L->flags != LYPHPLATE_BEING_DELETED && L->misc_material )
    {
      lyphplate **mats;
      
      for ( mats = L->misc_material; *mats; mats++ )
        if ( (*mats)->flags == LYPHPLATE_BEING_DELETED )
        {
          *where = strdupf( "Template %s", trie_to_static( L->id ) );
          return 1;
        }
    }
  }
  
  return 0;
}

int layer_used_by_non_doomed_lyphplate( layer *lyr )
{
  lyphplate *L;

  for ( L = first_lyphplate; L; L = L->next )
  {
    if ( L->flags != LYPHPLATE_BEING_DELETED && L->layers )
    {
      layer **lyrs;

      for ( lyrs = L->layers; *lyrs; lyrs++ )
        if ( *lyrs == lyr )
          return 1;
    }
  }

  return 0;
}

int doomed_lyphplates_already_in_use_by_layer( char **where, trie *t, layer_wrapper **head, layer_wrapper **tail )
{
  if ( t->data )
  {
    layer *lyr = (layer*)t->data;
    lyphplate **mats;
    
    for ( mats = lyr->material; *mats; mats++ )
      if ( (*mats)->flags == LYPHPLATE_BEING_DELETED )
      {
        if ( !layer_used_by_non_doomed_lyphplate( lyr ) )
        {
          layer_wrapper *w;

          CREATE( w, layer_wrapper, 1 );
          w->lyr = lyr;
          LINK( w, *head, *tail, next );

          continue;
        }

        *where = strdupf( "Layer %s", trie_to_static( lyr->id ) );
        return 1;
      }
  }
  
  TRIE_RECURSE
  (
    if ( doomed_lyphplates_already_in_use_by_layer( where, *child, head, tail ) )
      return 1;
  );
  
  return 0;
}

int doomed_lyphplates_already_in_use( char **where, layer_wrapper **head, layer_wrapper **tail )
{
  if ( doomed_lyphplates_already_in_use_by_lyph( where ) )
    return 1;
    
  if ( doomed_lyphplates_already_in_use_by_lyphplate( where ) )
    return 1;
    
  if ( doomed_lyphplates_already_in_use_by_layer( where, layer_ids, head, tail ) )
    return 1;
    
  return 0;
}

HANDLER( do_delete_templates )
{
  char *tmpltstr, *recursivestr, *err;
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

  recursivestr = get_param( params, "recursive" );

  if ( recursivestr && !strcmp( recursivestr, "yes" ) )
  {
    /*
     * Any lyphplate/layer which refers to a doomed lyphplate/layer should also be doomed
     */
    while ( spread_lyphplate_doom( ) )
      ;

    if ( remove_doomed_lyphplates_from_lyphs( ) )
      save_lyphs();

    delete_doomed_layers( layer_ids );
  }
  else
  {
    layer_wrapper *layers_head = NULL, *layers_tail = NULL, *w, *w_next;
    int already_used = doomed_lyphplates_already_in_use(&err, &layers_head, &layers_tail);

    for ( w = layers_head; w; w = w_next )
    {
      w_next = w->next;

      /*
       * Small intentional memory leak here because fixing it would be completely
       * un-worth (in terms of added complexity to maintain) the rare uses of this command
       */
      if ( !already_used )
        w->lyr->id->data = NULL;

      free( w );
    }

    if ( already_used )
    {
      lyphplates_unset_bits( LYPHPLATE_BEING_DELETED );
      HND_ERRF_NORETURN( "One of the indicated lyphplates is already in use (in %s)", err );
      free( err );
      return;
    }
  }

  delete_doomed_lyphplates( );

  save_lyphplates();

  send_ok( req );
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

HANDLER( do_delete_views )
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

  send_ok( req );
}

void spread_lyphplate_doom_from_layers( void )
{
  lyphplate *L;

  for ( L = first_lyphplate; L; L = L->next )
  {
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
}

HANDLER( do_delete_layers )
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

  spread_lyphplate_doom_from_layers( );

  while ( spread_lyphplate_doom( ) )
    ;

  if ( remove_doomed_lyphplates_from_lyphs( ) )
    save_lyphs();

  delete_doomed_layers( layer_ids );
  delete_doomed_lyphplates( );

  save_lyphplates();

  send_ok( req );
}

HANDLER( do_lyphs_from_view )
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
    if ( !((*rptr)->L) || !IS_SET( (*rptr)->L->flags, LYPH_TO_BE_REMOVED ) )
      size++;

  CREATE( buf, lv_rect *, size + 1 );
  bptr = buf;

  for ( rptr = v->rects, size = 0; *rptr; rptr++ )
  {
    if ( (*rptr)->L && IS_SET( (*rptr)->L->flags, LYPH_TO_BE_REMOVED ) )
      free( *rptr );
    else
      *bptr++ = *rptr;
  }

  *bptr = NULL;
  free( v->rects );
  v->rects = buf;
  v->modified = longtime();

  for ( lptr = lyphs; *lptr; lptr++ )
    REMOVE_BIT( (*lptr)->flags, LYPH_TO_BE_REMOVED );

  save_lyphviews();

  #undef LYPH_TO_BE_REMOVED

  send_response( req, lyphview_to_json( v ) );
}

int template_involves_any_of( lyphplate *L, lyphplate **parts )
{
  if ( IS_SET( L->flags, LYPHPLATE_DOES_INVOLVE ) )
    return 1;
  else
  if ( IS_SET( L->flags, LYPHPLATE_DOES_NOT_INVOLVE ) )
    return 0;
  else
  {
    lyphplate **misc_mats, **partptr;
    int answer;

    for ( partptr = parts; *partptr; partptr++ )
    {
      lyphplate *part = *partptr;

      if ( L == part )
      {
        SET_BIT( L->flags, LYPHPLATE_DOES_INVOLVE );
        return 1;
      }

      for ( misc_mats = L->misc_material; *misc_mats; misc_mats++ )
        if ( template_involves_any_of( *misc_mats, parts ) )
          break;

      if ( *misc_mats )
      {
        answer = 1;
        goto template_involves_outer_escape_tag;
      }
      else
      switch( L->type )
      {
        case LYPHPLATE_BASIC:
          /*
           * answer will be set to 0 after break
           */
          break;
        case LYPHPLATE_SHELL:
        case LYPHPLATE_MIX:
        {
          layer **lyr;
          lyphplate **materials;

          for ( lyr = L->layers; *lyr; lyr++ )
          for ( materials = (*lyr)->material; *materials; materials++ )
            if ( template_involves_any_of( *materials, parts ) )
              goto template_involves_escape_tag;

          template_involves_escape_tag:

          if ( *lyr )
          {
            answer = 1;
            goto template_involves_outer_escape_tag;
          }
        }
      }
    }
    answer = 0;

    template_involves_outer_escape_tag:

    if ( answer )
      SET_BIT( L->flags, LYPHPLATE_DOES_INVOLVE );
    else
      SET_BIT( L->flags, LYPHPLATE_DOES_NOT_INVOLVE );

    return answer;
  }
}

void calc_involves_template( lyphplate *L, lyph_wrapper **head, lyph_wrapper **tail, int *cnt )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {
    lyphplate **parts;

    CREATE( parts, lyphplate *, 2 );
    parts[0] = L;
    parts[1] = NULL;

    if ( e->lyphplt && template_involves_any_of( e->lyphplt, parts ) )
    {
      lyph_wrapper *w;

      CREATE( w, lyph_wrapper, 1 );
      w->e = e;
      LINK( w, *head, *tail, next );
      (*cnt)++;
    }

    free( parts );
  }
}

HANDLER( do_involves_template )
{
  lyphplate *L;
  lyph_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyph **buf, **bptr;
  char *tmpidstr;
  int cnt;

  TRY_PARAM( tmpidstr, "template", "Missing argument: 'template': Specify a template, X, in order to find all lyphs L such that L has a template Y that involves X in any way" );

  L = lyphplate_by_id( tmpidstr );

  if ( !L )
    HND_ERR( "The indicated template was not found in the database" );

  cnt = 0;

  calc_involves_template( L, &head, &tail, &cnt );

  lyphplates_unset_bits( LYPHPLATE_DOES_INVOLVE | LYPHPLATE_DOES_NOT_INVOLVE );

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;

    *bptr++ = w->e;
    free( w );
  }
  *bptr = NULL;

  send_response( req, JSON1
  (
    "lyphs": JS_ARRAY( lyph_to_json, buf )
  ) );

  free( buf );
}

HANDLER( do_instances_of )
{
  send_response( req, JSON1( "Response": "Temporarily disabled due to layer structure change" ) );
}

HANDLER( do_nodes_from_view )
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
    send_response( req, lyphview_to_json( v ) );
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

  send_response( req, lyphview_to_json( v ) );

  nodes_from_view_cleanup:

  for ( nptr = n; *nptr; nptr++ )
    REMOVE_BIT( (*nptr)->flags, LYPHNODE_TO_BE_REMOVED );

  free( n );

  #undef LYPHNODE_TO_BE_REMOVED
}

void **get_numbered_args_( url_param **params, char *base, char * (*non_reentrant) (void *), char * (*reentrant) (void *, void *), void *data, char **err, int *size )
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

  if ( non_reentrant || reentrant )
  {
    for ( i = 1; vals[i]; i++ )
    {
      void *x;

      if ( non_reentrant )
        x = (*non_reentrant)(vals[i]);
      else
        x = (*reentrant)(vals[i],data);

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

void **get_numbered_args( url_param **params, char *base, char * (*fnc) (void *), char **err, int *size )
{
  return get_numbered_args_( params, base, fnc, NULL, NULL, err, size );
}

void **get_numbered_args_r( url_param **params, char *base, char * (*fnc) (void *, void *), void *data, char **err, int *size )
{
  return get_numbered_args_( params, base, NULL, fnc, data, err, size );
}

void find_lyphs_with_template( lyphplate *L, lyph ***bptr )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->lyphplt == L )
    {
      **bptr = e;
      (*bptr)++;
    }
  }
}

HANDLER( do_has_template )
{
  lyphplate *L;
  lyph **buf, **bptr;
  char *tmpltid;

  TRY_PARAM( tmpltid, "template", "You did not specify a template" );

  L = lyphplate_by_id( tmpltid );

  if ( !L )
    HND_ERR( "The indicated template was not recognized" );

  CREATE( buf, lyph *, lyphcnt + 1 );
  bptr = buf;

  find_lyphs_with_template( L, &bptr );
  *bptr = NULL;

  send_response( req, JS_ARRAY( lyph_to_json, buf ) );

  free( buf );
}

int index_is_used( clinical_index *ci )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {
    lyph_annot **a;

    for ( a = e->annots; *a; a++ )
      if ( (*a)->obj == ci->index )
        return 1;
  }

  return 0;
}

HANDLER( do_unused_indices )
{
  clinical_index *ci, **buf, **bptr;
  int cnt = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  CREATE( buf, clinical_index *, cnt + 1 );
  bptr = buf;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    if ( !index_is_used( ci ) )
      *bptr++ = ci;

  *bptr = NULL;

  send_response( req, JS_ARRAY( clinical_index_to_json_full, buf ) );

  free( buf );
}

HANDLER( do_layer_from_template )
{
  lyphplate *L;
  layer *lyr, **lyrptr, **buf, **bptr;
  char *tmpltstr, *lyrstr, *posstr;
  int pos, n, cnt;

  TRY_PARAM( tmpltstr, "template", "You did not specify which template you want to remove the layer from" );
  TRY_PARAM( lyrstr, "layer", "You did not specify which layer you want to remove from the template" );

  L = lyphplate_by_id( tmpltstr );

  if ( !L )
    HND_ERR( "The indicated template was not recognized" );

  lyr = layer_by_id( lyrstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not recognized" );

  posstr = get_param( params, "pos" );

  if ( posstr )
  {
    pos = strtoul( posstr, NULL, 10 );

    if ( pos < 1 )
      HND_ERR( "'pos' must be a positive integer" );
  }
  else
    pos = 1;

  for ( n = 0, lyrptr = L->layers; *lyrptr; lyrptr++ )
    if ( *lyrptr == lyr && ++n == pos )
      break;

  if ( !*lyrptr )
  {
    if ( pos > 1 )
      HND_ERR( "The indicated layer does not occur that many times in the indicated template" );
    else
      HND_ERR( "The indicated layer does not occur in the indicated template" );
  }

  cnt = VOIDLEN( L->layers );

  CREATE( buf, layer *, cnt );
  bptr = buf;

  for ( lyrptr = L->layers, n = 0; *lyrptr; lyrptr++ )
    if ( *lyrptr != lyr || ++n != pos )
      *bptr++ = *lyrptr;

  *bptr = NULL;

  free( L->layers );
  L->layers = buf;

  L->modified = longtime();
  save_lyphplates();

  send_response( req, lyphplate_to_json( L ) );
}

HANDLER( do_layer_to_template )
{
  lyphplate *L;
  layer *lyr, **lyrptr, **buf, **bptr;
  char *lyrstr, *tmpltstr, *posstr;
  int pos, n, size, fPos = 0;

  TRY_PARAM( tmpltstr, "template", "You did not indicate which template you want to add the layer to" );
  TRY_PARAM( lyrstr, "layer", "You did not indicate which layer you want to add to the template" );

  L = lyphplate_by_id( tmpltstr );

  if ( !L )
    HND_ERR( "The indicated template was not recognized" );

  lyr = layer_by_id( lyrstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not recognized" );

  if ( L->type != LYPHPLATE_SHELL && L->type != LYPHPLATE_MIX )
    HND_ERR( "Layers can only be added to a template if that template has type 'shell' or 'mix'" );

  if ( is_Xs_built_from_Y( lyr->material, L ) )
    HND_ERR( "The template in question already occurs in the construction of the layer in question" );

  posstr = get_param( params, "pos" );

  if ( !posstr && L->type == LYPHPLATE_SHELL )
    HND_ERR( "Since this template is of type 'shell', a position ('pos') must be specified for the new layer" );

  if ( posstr )
  {
    pos = strtoul( posstr, NULL, 10 );

    if ( pos < 1 )
      HND_ERR( "'pos' must be a positive integer" );
  }
  else
    pos = 1;

  size = VOIDLEN( L->layers );

  if ( pos > size + 1 )
    HND_ERRF( "You indicated a position of pos=%d but the template only has %d nodes", pos, size );

  CREATE( buf, layer *, size + 2 );
  bptr = buf;

  for ( n = 0, lyrptr = L->layers; *lyrptr; lyrptr++ )
  {
    if ( ++n == pos )
    {
      fPos = 1;
      *bptr++ = lyr;
    }

    *bptr++ = *lyrptr;
  }

  if ( !fPos )
    *bptr++ = lyr;

  *bptr = NULL;

  free( L->layers );
  L->layers = buf;

  L->modified = longtime();
  save_lyphplates();

  send_response( req, lyphplate_to_json( L ) );
}

HANDLER( do_material_to_layer )
{
  layer *lyr;
  lyphplate *material, **mptr, **buf;
  char *layerstr, *matstr, *mutablestr, *repeatstr;
  int size;

  TRY_PARAM( layerstr, "layer", "You did not specify which layer to add the material to" );
  TRY_PARAM( matstr, "material", "You did not specify which material to add to the layer" );

  lyr = layer_by_id( layerstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not recognized" );

  material = lyphplate_by_id( matstr );

  if ( !material )
    HND_ERR( "The indicated material was not recognized" );

  if ( is_X_built_from_Y( material, lyr ) )
    HND_ERR( "The layer in question already occurs in the construction of the material in question" );

  repeatstr = get_param( params, "repeat" );

  if ( !repeatstr || strcmp( repeatstr, "yes" ) )
  {
    for ( mptr = lyr->material; *mptr; mptr++ )
      if ( *mptr == material )
        HND_ERR( "The layer in question already has that material" );
  }

  mutablestr = get_param( params, "mutable" );

  if ( !mutablestr || strcmp( mutablestr, "yes" ) )
    lyr = clone_layer( lyr );

  size = VOIDLEN( lyr->material );

  CREATE( buf, lyphplate *, size + 2 );
  memcpy( buf, lyr->material, size * sizeof(lyphplate *) );
  buf[size] = material;
  buf[size+1] = NULL;

  free( lyr->material );
  lyr->material = buf;

  save_lyphplates();

  send_response( req, layer_to_json( lyr ) );
}

HANDLER( do_material_from_layer )
{
  layer *lyr;
  lyphplate *material, **mptr, **buf, **bptr;
  char *layerstr, *matstr, *mutablestr;
  int cnt;

  TRY_PARAM( layerstr, "layer", "You did not specify which layer to remove the material from" );
  TRY_PARAM( matstr, "material", "You did not specify which material to remove from the layer" );

  lyr = layer_by_id( layerstr );

  if ( !lyr )
    HND_ERR( "The indicated layer was not recognized" );

  material = lyphplate_by_id( matstr );

  if ( !material )
    HND_ERR( "The indicated material was not recognized" );

  for ( cnt = 0, mptr = lyr->material; *mptr; mptr++ )
  {
    if ( *mptr == material )
      break;

    cnt++;
  }

  if ( !*mptr )
    HND_ERR( "The layer in question does not have that material" );

  mutablestr = get_param( params, "mutable" );

  if ( !mutablestr || strcmp( mutablestr, "yes" ) )
    lyr = clone_layer( lyr );

  CREATE( buf, lyphplate *, cnt + 1 );
  bptr = buf;

  for ( mptr = lyr->material; *mptr; mptr++ )
    if ( *mptr != material )
      *bptr++ = *mptr;

  *bptr = NULL;

  free( lyr->material );
  lyr->material = buf;

  save_lyphplates();

  send_response( req, layer_to_json( lyr ) );
}
