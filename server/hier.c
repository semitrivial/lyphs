#include "lyph.h"
#include "srv.h"

#ifdef PRE_LAYER_CHANGE

void add_lyphplate_to_wrappers( lyphplate *L, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_lyphplates_to_wrappers( lyphplate **L, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_ont_term_parents( trie *t, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_supers_by_layers( trie *t, lyphplate *sub, lyphplates_wrapper **head, lyphplates_wrapper **tail );
int is_superlayer( layer *sup, layer *sub );
int should_add_super_by_layers( lyphplate *sup, lyphplate *sub );

#endif

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

#ifdef PRE_LAYER_CHANGE

void compute_lyphplate_hierarchy( trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( !L->supers )
    {
      compute_lyphplate_hierarchy_one_lyphplate( L );
      TRIE_RECURSE( compute_lyphplate_hierarchy( *child ) );
    }
  }
  else
    TRIE_RECURSE( compute_lyphplate_hierarchy( *child ) );
}

#define PARENT_ADDED 1

/*
 * To do: Optimize this
 */
void compute_lyphplate_hierarchy_one_lyphplate( lyphplate *L )
{
  lyphplates_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyphplate **parent, **supers, **sptr, avoid_dupes;
  int cnt = 0;

  if ( L->ont_term )
    add_ont_term_parents( L->ont_term, &head, &tail );

  if ( L->layers && *L->layers )
  {
    layer **lyr;

    for ( lyr = L->layers; *lyr; lyr++ )
    {
      if ( !(*lyr)->material->supers )
        compute_lyphplate_hierarchy_one_lyphplate( (*lyr)->material );
    }
    add_supers_by_layers( lyphplate_ids, L, &head, &tail );
  }

  for ( w = head; w; w = w->next )
  for ( parent = w->L; *parent; parent++ )
  {
    if ( !IS_SET( (*parent)->flags, PARENT_ADDED ) )
    {
      SET_BIT( (*parent)->flags, PARENT_ADDED );
      cnt++;
    }
    else
      *parent = &avoid_dupes;
  }

  CREATE( supers, lyphplate *, cnt + 1 );
  sptr = supers;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;

    for ( parent = w->L; *parent; parent++ )
    {
      if ( *parent == &avoid_dupes )
        continue;

      *sptr++ = *parent;
      REMOVE_BIT( (*parent)->flags, PARENT_ADDED );
    }

    free( w->L );
    free( w );
  }

  *sptr = NULL;
  L->supers = supers;
}

void add_supers_by_layers( trie *t, lyphplate *sub, lyphplates_wrapper **head, lyphplates_wrapper **tail )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( should_add_super_by_layers( L, sub ) )
    {
      add_lyphplate_to_wrappers( L, head, tail );

      if ( !L->supers )
        compute_lyphplate_hierarchy_one_lyphplate( L );

      if ( *L->supers )
        add_lyphplates_to_wrappers( L->supers, head, tail );
    }
  }

  TRIE_RECURSE( add_supers_by_layers( *child, sub, head, tail ) );
}

int ont_term_is_super( trie *sup, trie *sub )
{
  trie **super;

  for ( super = sub->data; *super; super++ )
  {
    if ( *super == sup )
      return 1;

    if ( ont_term_is_super( sup, *super ) )
      return 1;
  }

  return 0;
}

int should_add_super( lyphplate *sup, lyphplate *sub )
{
  if ( sup == sub )
    return 0;

  if ( sup->ont_term && sub->ont_term && ont_term_is_super( sup->ont_term, sub->ont_term ) )
    return 1;

  return should_add_super_by_layers( sup, sub );
}

int should_add_super_by_layers( lyphplate *sup, lyphplate *sub )
{
  layer **Llyr, **slyr;

  if ( sup == sub || sup->type != sub->type )
    return 0;

  if ( !sup->layers || !*sup->layers )
    return 0;

  for ( Llyr = sup->layers, slyr = sub->layers; *Llyr; Llyr++ )
  {
    if ( !*slyr || !is_superlayer( *Llyr, *slyr ) )
      return 0;
    slyr++;
  }

  if ( *slyr )
    return 0;

  return 1;
}

void add_ont_term_parents( trie *t, lyphplates_wrapper **head, lyphplates_wrapper **tail )
{
  trie **parent;

  for ( parent = t->data; *parent; parent++ )
  {
    lyphplate *L = lyphplate_by_ont_term( *parent );

    if ( L )
    {
      add_lyphplate_to_wrappers( L, head, tail );

      if ( !L->supers )
        compute_lyphplate_hierarchy_one_lyphplate( L );

      if ( *L->supers )
        add_lyphplates_to_wrappers( L->supers, head, tail );
    }

    add_ont_term_parents( *parent, head, tail );
  }
}

void add_lyphplate_to_wrappers( lyphplate *L, lyphplates_wrapper **head, lyphplates_wrapper **tail )
{
  lyphplates_wrapper *w;

  CREATE( w, lyphplates_wrapper, 1 );
  CREATE( w->L, lyphplate *, 2 );
  w->L[0] = L;
  w->L[1] = NULL;

  LINK( w, *head, *tail, next );
}

void add_lyphplates_to_wrappers( lyphplate **L, lyphplates_wrapper **head, lyphplates_wrapper **tail )
{
  lyphplates_wrapper *w;
  int len = VOIDLEN( L );

  CREATE( w, lyphplates_wrapper, 1 );
  CREATE( w->L, lyphplate *, len + 1 );
  memcpy( w->L, L, sizeof(lyphplate*) * (len+1) );

  LINK( w, *head, *tail, next );
}

int is_superlayer( layer *sup, layer *sub )
{
  lyphplate *sup_lyphplate = sup->material, *sub_lyphplate = sub->material, **supers;

  if ( sup == sub )
    return 1;

  for ( supers = sub_lyphplate->supers; *supers; supers++ )
    if ( *supers == sup_lyphplate )
      return 1;

  return 0;
}

/*
 * add_lyphplate_as_super is for adding a new lyphplate to the hierarchy when it is
 * first created (as opposed to when it is loaded).
 */
void add_lyphplate_as_super( lyphplate *sup, trie *t )
{
  if ( t->data )
  {
    lyphplate *sub = (lyphplate *)t->data;

    if ( should_add_super( sup, sub ) )
    {
      lyphplate **new_supers;
      int cnt = VOIDLEN(sub->supers);

      CREATE( new_supers, lyphplate *, cnt + 2 );
      memcpy( new_supers, sub->supers, cnt * sizeof(lyphplate *) );
      new_supers[cnt] = sup;
      new_supers[cnt+1] = NULL;
      free( sub->supers );
      sub->supers = new_supers;
    }
  }

  TRIE_RECURSE( add_lyphplate_as_super( sup, *child ) );
}

int is_superlyphplate( lyphplate *sup, lyphplate *sub )
{
  lyphplate **supers;

  if ( sup == sub )
    return 1;

  for ( supers = sub->supers; *supers; supers++ )
    if ( *supers == sup )
      return 1;

  return 0;
}

void get_sublyphplates_recurse( lyphplate *L, trie *t, lyphplate ***bptr )
{
  if ( t->data )
  {
    lyphplate *sub = (lyphplate *)t->data;

    if ( is_superlyphplate( L, sub ) )
    {
      **bptr = sub;
      (*bptr)++;
    }
  }

  TRIE_RECURSE( get_sublyphplates_recurse( L, *child, bptr ) );
}

void get_direct_sublyphplates_worker( lyphplate *L, lyphplate *sub, lyphplate ***bptr )
{
  #define GDSR_IS_DSUBLYPHPLATE 1
  #define GDSR_ISNT_DSUBLYPHPLATE 2

  if ( sub->flags )
    return;

  lyphplate **super;
  int fMatch = 0;

  for ( super = sub->supers; *super; super++ )
  {
    if ( *super == L )
      fMatch = 1;
    else if ( IS_SET( (*super)->flags, GDSR_IS_DSUBLYPHPLATE ) )
      break;
    else if (!IS_SET( (*super)->flags, GDSR_ISNT_DSUBLYPHPLATE ) )
    {
      get_direct_sublyphplates_worker( L, *super, bptr );

      if ( IS_SET( (*super)->flags, GDSR_IS_DSUBLYPHPLATE ) )
        break;
    }
  }

  if ( *super || !fMatch )
    SET_BIT( sub->flags, GDSR_ISNT_DSUBLYPHPLATE );
  else
  {
    SET_BIT( sub->flags, GDSR_IS_DSUBLYPHPLATE );
    **bptr = sub;
    (*bptr)++;
  }
}

void get_direct_sublyphplates_recurse( lyphplate *L, trie *t, lyphplate ***bptr )
{
  if ( t->data )
    get_direct_sublyphplates_worker( L, (lyphplate *)t->data, bptr );

  TRIE_RECURSE( get_direct_sublyphplates_recurse( L, *child, bptr ) );
}

lyphplate **get_sublyphplates( lyphplate *L, int direct )
{
  /*
   * To do: optimize this
   */
  lyphplate **buf, **bptr;

  CREATE( buf, lyphplate *, 1024 * 1024 );
  bptr = buf;

  if ( direct )
  {
    SET_BIT( L->flags, GDSR_ISNT_DSUBLYPHPLATE );
    get_direct_sublyphplates_recurse( L, lyphplate_ids, &bptr );
    lyphplates_unset_bits( GDSR_ISNT_DSUBLYPHPLATE | GDSR_IS_DSUBLYPHPLATE );
  }
  else
    get_sublyphplates_recurse( L, lyphplate_ids, &bptr );

  *bptr = NULL;
  return buf;
}

void populate_superless( trie *t, lyphplate ***sptr )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( !*L->supers )
    {
      **sptr = L;
      (*sptr)++;
    }
  }

  TRIE_RECURSE( populate_superless( *child, sptr ) );
}

char *hierarchy_member_to_json( lyphplate *L )
{
  lyphplate **subs = get_sublyphplates( L, 1 );
  char *retval;

  if ( *subs )
    retval = JSON
    (
      "id": trie_to_json( L->id ),
      "name": trie_to_json( L->name ),
      "children": JS_ARRAY( hierarchy_member_to_json, subs )
    );
  else
    retval = JSON
    (
      "id": trie_to_json( L->id ),
      "name": trie_to_json( L->name )
    );

  free( subs );
  return retval;
}

char *lyphplate_hierarchy_to_json( void )
{
  lyphplate **superless, **sptr;
  char *retval;

  /*
   * To do: Improve this next line
   */
  CREATE( superless, lyphplate *, 1024 * 1024 );
  sptr = superless;

  populate_superless( lyphplate_ids, &sptr );

  retval = JSON
  (
    "name": "Lyph Template Hierarchy",
    "children": JS_ARRAY( hierarchy_member_to_json, superless )
  );

  free( superless );

  return retval;
}

void clear_lyphplate_hierarchy( trie *t )
{
  if ( t->data )
  {
    lyphplate *L = (lyphplate *)t->data;

    if ( L->supers )
    {
      free( L->supers );
      L->supers = NULL;
    }
  }

  TRIE_RECURSE( clear_lyphplate_hierarchy( *child ) );
}

void recalculate_lyphplate_hierarchy( void )
{
  clear_lyphplate_hierarchy( lyphplate_ids );
  compute_lyphplate_hierarchy( lyphplate_ids );
}
#endif

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
