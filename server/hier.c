/*
 *  hier.c
 *  Logic related to various hierarchies within the lyph system
 *  (much of which is obsoleted template hierarchy stuff)
 */
#include "lyph.h"
#include "srv.h"

int can_node_fit_in_lyph( lyphnode *n, lyph *e )
{
  lyph *d;

  for ( d = first_lyph; d; d = d->next )
  {
    lyphnode_wrapper *head, *tail, *w, *w_next;
    int fMatch;

    if ( d->from == n || d->to == n )
    {
      if ( d == e )
        return 0;

      head = NULL;
      tail = NULL;
      fMatch = 0;

      calc_nodes_in_lyph( d, &head, &tail );

      for ( w = head; w; w = w_next )
      {
        w_next = w->next;

        if ( w->n == e->from || w->n == e->to )
          fMatch = 1;

        free( w );
      }

      if ( fMatch )
        return 0;
    }
  }

  return 1;
}

char *lyphplate_to_shallow_json( lyphplate *L )
{
  return trie_to_json( L->id );
}

#define LYPHNODE_DEFINITELY_IN_LYPH 1
#define LYPHNODE_DEFINITELY_OUT_LYPH 2
#define LYPHNODE_CURRENTLY_CALCULATING 4
#define LYPH_DEFINITELY_IN_LYPH 1
#define LYPH_DEFINITELY_OUT_LYPH 2
#define LYPH_CURRENTLY_CALCULATING 4

void maybe_node_is_in( lyphnode *n, lyph *L, lyphnode_wrapper **head, lyphnode_wrapper **tail )
{
  lyphnode_wrapper *w;

  if ( !n->location )
  {
    SET_BIT( n->flags, LYPHNODE_DEFINITELY_OUT_LYPH );
    return;
  }

  if ( n->location == L )
  {
    maybe_node_is_in_add:

    REMOVE_BIT( n->flags, LYPHNODE_CURRENTLY_CALCULATING );
    SET_BIT( n->flags, LYPHNODE_DEFINITELY_IN_LYPH );

    CREATE( w, lyphnode_wrapper, 1 );
    w->n = n;
    LINK( w, *head, *tail, next );
    return;
  }

  if ( IS_SET( n->location->flags, LYPH_DEFINITELY_IN_LYPH ) )
    goto maybe_node_is_in_add;

  if ( IS_SET( n->location->flags, LYPH_DEFINITELY_OUT_LYPH )
  ||   IS_SET( n->location->flags, LYPH_CURRENTLY_CALCULATING ) )
    return;

  SET_BIT( n->flags, LYPHNODE_CURRENTLY_CALCULATING );

  maybe_node_is_in( n->location->from, L, head, tail );

  if ( !IS_SET( n->location->from->flags, LYPHNODE_DEFINITELY_IN_LYPH ) )
  {
    maybe_node_is_in_dontadd:

    REMOVE_BIT( n->flags, LYPHNODE_CURRENTLY_CALCULATING );
    SET_BIT( n->flags, LYPHNODE_DEFINITELY_OUT_LYPH );
    return;
  }

  maybe_node_is_in( n->location->to, L, head, tail );

  if ( !IS_SET( n->location->to->flags, LYPHNODE_DEFINITELY_IN_LYPH ) )
    goto maybe_node_is_in_dontadd;

  goto maybe_node_is_in_add;
}

void calc_nodes_in_lyph_recurse( lyph *L, lyphnode_wrapper **head, lyphnode_wrapper **tail, trie *t )
{
  if ( t->data )
  {
    lyphnode *n = (lyphnode *)t->data;

    if (!IS_SET( n->flags, LYPHNODE_DEFINITELY_IN_LYPH )
    &&  !IS_SET( n->flags, LYPHNODE_DEFINITELY_OUT_LYPH )
    &&  !IS_SET( n->flags, LYPHNODE_CURRENTLY_CALCULATING ) )
      maybe_node_is_in( n, L, head, tail );
  }

  TRIE_RECURSE( calc_nodes_in_lyph_recurse( L, head, tail, *child ) );
}

void calc_nodes_in_lyph( lyph *L, lyphnode_wrapper **head, lyphnode_wrapper **tail )
{
  calc_nodes_in_lyph_recurse( L, head, tail, lyphnode_ids );

  lyphs_unset_bits( LYPH_DEFINITELY_IN_LYPH | LYPH_DEFINITELY_OUT_LYPH | LYPHNODE_CURRENTLY_CALCULATING );
  lyphnodes_unset_bits( LYPHNODE_DEFINITELY_IN_LYPH | LYPHNODE_DEFINITELY_OUT_LYPH | LYPHNODE_CURRENTLY_CALCULATING, lyphnode_ids );
}

void populate_with_basic_lyphplates_subclass_of( trie **supers, lyphplate ***bptr, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate*)t->data;
    trie **super;

    for ( super = supers; *super; super++ )
      if ( L->ont_term == *super )
        break;

    if ( *super )
    {
      **bptr = L;
      (*bptr)++;
    }
    else
    if ( L->ont_term && L->ont_term->data )
    {
      trie **tptr;

      for ( tptr = L->ont_term->data; *tptr; tptr++ )
      for ( super = supers; *super; super++ )
      {
        if ( *tptr == *super )
        {
          **bptr = L;
          (*bptr)++;
          goto populate_with_basic_lyphplates_subclass_of_escape;
        }
      }
    }
  }

  populate_with_basic_lyphplates_subclass_of_escape:

  TRIE_RECURSE( populate_with_basic_lyphplates_subclass_of( supers, bptr, *child ) );
}

void populate_with_lyphplates_involving_any_of( lyphplate **basics, lyphplate ***bptr, trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( template_involves_any_of( L, basics ) )
    {
      **bptr = L;
      (*bptr)++;
    }
  }

  TRIE_RECURSE( populate_with_lyphplates_involving_any_of( basics, bptr, *child ) );
}

HANDLER( do_templates_involving )
{
  lyphplate **buf;
  char *ontstr;

  TRY_PARAM( ontstr, "ont", "You did not specify an ontology term ('ont')" );

  if ( strlen( ontstr ) < 3 )
    HND_ERR( "The 'ont' term you entered was too short (min length: 3)" );

  if ( !*ontstr )
    HND_ERR( "The 'templates_involving' command cannot be called with a blank 'ont' term" );

  buf = lyphplates_by_term( ontstr );

  if ( !buf )
    HND_ERR( "No lyphplates matched the term in question" );

  send_response( req, JS_ARRAY( lyphplate_to_json, buf ) );

  free( buf );
}

lyphplate **lyphplates_by_term( const char *ontstr )
{
  lyphplate *L, **basics, **bscptr, **buf, **bptr;
  trie **onts;
  char *lower;
  int cnt;

  lower = lowercaserize( ontstr );

  cnt = count_nontrivial_members( label_to_iris_lowercase );

  CREATE( onts, trie *, cnt + 1 );

  trie_search_autocomplete( lower, onts, label_to_iris_lowercase, 1, 1 );

  L = lyphplate_by_id( ontstr );

  if ( !*onts && !L )
  {
    free( onts );
    return NULL;
  }

  cnt = count_nontrivial_members( lyphplate_ids );
  CREATE( basics, lyphplate *, cnt + 1 );
  bscptr = basics;

  populate_with_basic_lyphplates_subclass_of( onts, &bscptr, lyphplate_ids );

  if ( L )
  {
    if ( *onts )
    {
      lyphplate **dupe;
      for ( dupe = basics; dupe < bscptr; dupe++ )
        if ( *dupe == L )
          break;

      if ( dupe == bscptr )
        *bscptr++ = L;
    }
    else
      *bscptr++ = L;
  }

  *bscptr = NULL;

  CREATE( buf, lyphplate *, cnt + 1 );
  bptr = buf;

  populate_with_lyphplates_involving_any_of( basics, &bptr, lyphplate_ids );
  lyphplates_unset_bits( LYPHPLATE_DOES_INVOLVE | LYPHPLATE_DOES_NOT_INVOLVE );
  *bptr = NULL;

  MULTIFREE( basics, onts );

  return buf;
}

lyphplate **common_materials_of_layers( lyphplate *L )
{
  lyphplate **buf, **bptr, **mptr;

  if ( L->type != LYPHPLATE_SHELL && L->type != LYPHPLATE_MIX )
    return (lyphplate**)blank_void_array();

  if ( !L->layers || !*L->layers )
    return (lyphplate**)blank_void_array();

  if ( !L->layers[1] )
    return (lyphplate**)COPY_VOID_ARRAY( L->layers[0]->material );

  CREATE( buf, lyphplate *, VOIDLEN(L->layers[0]->material) + 1 );
  bptr = buf;

  for ( mptr = L->layers[0]->material; *mptr; mptr++ )
  {
    layer **lyr;

    for ( lyr = &L->layers[1]; *lyr; lyr++ )
    {
      lyphplate **mptr2;

      for ( mptr2 = (*lyr)->material; *mptr2; mptr2++ )
        if ( *mptr2 == *mptr )
          break;

      if ( !*mptr2 )
        break;
    }

    if ( !*lyr )
      *bptr++ = *mptr;
  }

  *bptr = NULL;

  return buf;
}

#define LYPHPLATE_NOT_BUILT_FROM_Y 1

int is_X_built_from_Y_core( lyphplate *x, void *y )
{
  lyphplate **mats;
  layer **lyrs;

  if ( x == y )
    return 1;

  if ( IS_SET( x->flags, LYPHPLATE_NOT_BUILT_FROM_Y ) )
    return 0;

  for ( mats = x->misc_material; *mats; mats++ )
    if ( *mats == y )
      return 1;

  if ( x->layers )
  {
    for ( lyrs = x->layers; *lyrs; lyrs++ )
    {
      if ( *lyrs == y )
        return 1;

      for ( mats = (*lyrs)->material; *mats; mats++ )
        if ( *mats == y )
          return 1;
    }
  }

  for ( mats = x->misc_material; *mats; mats++ )
    if ( is_X_built_from_Y_core( *mats, y ) )
      return 1;

  if ( x->layers )
  {
    for ( lyrs = x->layers; *lyrs; lyrs++ )
    for ( mats = (*lyrs)->material; *mats; mats++ )
      if ( is_X_built_from_Y_core( *mats, y ) )
        return 1;
  }

  SET_BIT( x->flags, LYPHPLATE_NOT_BUILT_FROM_Y );
  return 0;
}

int is_X_built_from_Y( lyphplate *x, void *y )
{
  int result = is_X_built_from_Y_core( x, y );

  lyphplates_unset_bits( LYPHPLATE_NOT_BUILT_FROM_Y );

  return result;
}

int is_Xs_built_from_Y( lyphplate **xs, void *y )
{
  lyphplate **x;

  for ( x = xs; *x; x++ )
    if ( is_X_built_from_Y_core( *x, y ) )
      break;

  lyphplates_unset_bits( LYPHPLATE_NOT_BUILT_FROM_Y );

  return *x ? 1 : 0;
}

HANDLER( do_is_built_from_template )
{
  lyphplate *part, **wholes;
  char *partstr, *tmpltstr, *lyrstr, *err;
  int result;
  
  TRY_PARAM( partstr, "part", "You did not indicate the 'part' whose usage you wanted to check for" );
  
  part = lyphplate_by_id( partstr );
  
  if ( !part )
    HND_ERR( "The indicated 'part' was not recognized" );
  
  lyrstr = get_param( params, "layer" );
  tmpltstr = get_param( params, "template" );
  
  if ( !tmpltstr )
    tmpltstr = get_param( params, "templates" );
    
  if ( !lyrstr && !tmpltstr )
    HND_ERR( "You did not specify either a 'layer', nor a 'template', nor a list of 'templates', which you want me to check for construction from the indicated part" );
    
  if ( lyrstr )
  {
    layer *lyr = layer_by_id( lyrstr );
    
    if ( !lyr )
      HND_ERR( "The indicated layer was not recognized" );

    send_response( req, JSON1( "response": is_Xs_built_from_Y( lyr->material, part ) ? "yes" : "no" ) );
    return;
  }
  
  wholes = (lyphplate**)PARSE_LIST( tmpltstr, lyphplate_by_id, "template", &err );
  
  if ( !wholes )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated templates was not recognized" );
  }
  
  result = is_Xs_built_from_Y( wholes, part );
  
  free( wholes );
  
  send_response( req, JSON1( "response": result ? "yes" : "no" ) );  
}

void calc_parents_tmp( void )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
    e->parent_tmp = get_relative_lyph_loc_buf( e, NULL );
}

HANDLER( do_between )
{
  lyph *root, **ends;
  char *rootstr, *endsstr;

  TRY_PARAM( rootstr, "root", "You did not specify a 'root'" );
  TRY_PARAM( endsstr, "ends", "You did not specify a list of 'ends'" );

  root = lyph_by_id( rootstr );

  if ( !root )
    HND_ERR( "The indicated root was not recognized" );

  ends = (lyph**) PARSE_LIST( endsstr, lyph_by_id, "lyph", NULL );

  if ( !ends )
    HND_ERR( "One of the indicated 'ends' was not recognized" );

  between_worker( root, ends, req, 0 );
}

void between_worker( lyph *root, lyph **ends, http_request *req, int verbose )
{
  lyph *e, **eptr, **retval, **rptr;

  calc_parents_tmp();

  CREATE( retval, lyph *, lyphcnt + 1);

  rptr = retval;

  for ( e = first_lyph; e; e = e->next )
    e->flags = 0;

  for ( eptr = ends; *eptr; eptr++ )
  {
    lyph *up;

    for ( up = *eptr; up; up = up->parent_tmp )
      if ( up == root )
        break;

    if ( up )
    {
      for ( up = *eptr; up; up = up->parent_tmp )
      {
        if ( !up->flags )
        {
          up->flags = 1;
          *rptr++ = up;
        }

        if ( up == root )
          break;
      }
    }
  }

  *rptr = NULL;
  free( ends );

  for ( e = first_lyph; e; e = e->next )
    e->flags = 0;

  if ( verbose )
    send_response( req, JS_ARRAY( lyph_to_json, retval ) );
  else
    send_response( req, JS_ARRAY( lyph_to_json_brief, retval ) );

  free( retval );
}
