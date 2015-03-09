#include "lyph.h"

void add_lyphplate_to_wrappers( lyphplate *L, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_lyphplates_to_wrappers( lyphplate **L, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_ont_term_parents( trie *t, lyphplates_wrapper **head, lyphplates_wrapper **tail );
void add_supers_by_layers( trie *t, lyphplate *sub, lyphplates_wrapper **head, lyphplates_wrapper **tail );
int is_superlayer( layer *sup, layer *sub );
int should_add_super_by_layers( lyphplate *sup, lyphplate *sub );

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

char *lyphplate_to_shallow_json( lyphplate *L )
{
  return trie_to_json( L->id );
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
    lyphplates_unset_bits( GDSR_ISNT_DSUBLYPHPLATE | GDSR_IS_DSUBLYPHPLATE, lyphplate_ids );
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
