#include "lyph.h"

void add_lyph_to_wrappers( lyph *L, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_lyphs_to_wrappers( lyph **L, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_ont_term_parents( trie *t, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_supers_by_layers( trie *t, lyph *sub, lyphs_wrapper **head, lyphs_wrapper **tail );
int is_superlayer( layer *sup, layer *sub );
void get_next_lyph_hierarchy_level( lyph_wrapper **head, lyph_wrapper **tail, int *cnt, trie *t );
int should_add_super_by_layers( lyph *sup, lyph *sub );

void compute_lyph_hierarchy( trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( !L->supers )
    {
      compute_lyph_hierarchy_one_lyph( L );
      TRIE_RECURSE( compute_lyph_hierarchy( *child ) );
    }
  }
  else
    TRIE_RECURSE( compute_lyph_hierarchy( *child ) );
}

#define PARENT_ADDED 1

/*
 * To do: Optimize this
 */
void compute_lyph_hierarchy_one_lyph( lyph *L )
{
  lyphs_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  lyph **parent, **supers, **sptr, avoid_dupes;
  int cnt = 0;

  if ( L->ont_term )
    add_ont_term_parents( L->ont_term, &head, &tail );

  if ( L->layers && *L->layers )
  {
    layer **lyr;

    for ( lyr = L->layers; *lyr; lyr++ )
    {
      if ( !(*lyr)->material->supers )
        compute_lyph_hierarchy_one_lyph( (*lyr)->material );
    }
    add_supers_by_layers( lyph_ids, L, &head, &tail );
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

  CREATE( supers, lyph *, cnt + 1 );
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

void add_supers_by_layers( trie *t, lyph *sub, lyphs_wrapper **head, lyphs_wrapper **tail )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( should_add_super_by_layers( L, sub ) )
    {
      add_lyph_to_wrappers( L, head, tail );

      if ( !L->supers )
        compute_lyph_hierarchy_one_lyph( L );

      if ( *L->supers )
        add_lyphs_to_wrappers( L->supers, head, tail );
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

int should_add_super( lyph *sup, lyph *sub )
{
  if ( sup == sub )
    return 0;

  if ( sup->ont_term && sub->ont_term && ont_term_is_super( sup->ont_term, sub->ont_term ) )
    return 1;

  return should_add_super_by_layers( sup, sub );
}

int should_add_super_by_layers( lyph *sup, lyph *sub )
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

void add_ont_term_parents( trie *t, lyphs_wrapper **head, lyphs_wrapper **tail )
{
  trie **parent;

  for ( parent = t->data; *parent; parent++ )
  {
    lyph *L = lyph_by_ont_term( *parent );

    if ( L )
    {
      add_lyph_to_wrappers( L, head, tail );

      if ( !L->supers )
        compute_lyph_hierarchy_one_lyph( L );

      if ( *L->supers )
        add_lyphs_to_wrappers( L->supers, head, tail );

      add_ont_term_parents( *parent, head, tail );
    }
  }
}

void add_lyph_to_wrappers( lyph *L, lyphs_wrapper **head, lyphs_wrapper **tail )
{
  lyphs_wrapper *w;

  CREATE( w, lyphs_wrapper, 1 );
  CREATE( w->L, lyph *, 2 );
  w->L[0] = L;
  w->L[1] = NULL;

  LINK( w, *head, *tail, next );
}

void add_lyphs_to_wrappers( lyph **L, lyphs_wrapper **head, lyphs_wrapper **tail )
{
  lyphs_wrapper *w;
  int len = VOIDLEN( L );

  CREATE( w, lyphs_wrapper, 1 );
  CREATE( w->L, lyph *, len + 1 );
  memcpy( w->L, L, sizeof(lyph*) * (len+1) );

  LINK( w, *head, *tail, next );
}

int is_superlayer( layer *sup, layer *sub )
{
  lyph *sup_lyph = sup->material, *sub_lyph = sub->material, **supers;

  if ( sup == sub )
    return 1;

  for ( supers = sub_lyph->supers; *supers; supers++ )
    if ( *supers == sup_lyph )
      return 1;

  return 0;
}

char *lyph_to_shallow_json( lyph *L )
{
  return trie_to_json( L->id );
}

/*
 * add_lyph_as_super is for adding a new lyph to the hierarchy when it is
 * first created (as opposed to when it is loaded).
 */
void add_lyph_as_super( lyph *sup, trie *t )
{
  if ( t->data )
  {
    lyph *sub = (lyph *)t->data;

    if ( should_add_super( sup, sub ) )
    {
      lyph **new_supers;
      int cnt = VOIDLEN(sub->supers);

      CREATE( new_supers, lyph *, cnt + 2 );
      memcpy( new_supers, sub->supers, cnt * sizeof(lyph *) );
      new_supers[cnt] = sup;
      new_supers[cnt+1] = NULL;
      free( sub->supers );
      sub->supers = new_supers;
    }
  }

  TRIE_RECURSE( add_lyph_as_super( sup, *child ) );
}

int is_superlyph( lyph *sup, lyph *sub )
{
  lyph **supers;

  if ( sup == sub )
    return 1;

  for ( supers = sub->supers; *supers; supers++ )
    if ( *supers == sup )
      return 1;

  return 0;
}

void get_sublyphs_recurse( lyph *L, trie *t, lyph ***bptr )
{
  if ( t->data )
  {
    lyph *sub = (lyph *)t->data;

    if ( is_superlyph( L, sub ) )
    {
      **bptr = sub;
      (*bptr)++;
    }
  }

  TRIE_RECURSE( get_sublyphs_recurse( L, *child, bptr ) );
}

void get_direct_sublyphs_worker( lyph *L, lyph *sub, lyph ***bptr )
{
  #define GDSR_IS_DSUBLYPH 1
  #define GDSR_ISNT_DSUBLYPH 2

  if ( sub->flags )
    return;

  lyph **super;
  int fMatch = 0;

  for ( super = sub->supers; *super; super++ )
  {
    if ( *super == L )
      fMatch = 1;
    else if ( IS_SET( (*super)->flags, GDSR_IS_DSUBLYPH ) )
      break;
    else if (!IS_SET( (*super)->flags, GDSR_ISNT_DSUBLYPH ) )
    {
      get_direct_sublyphs_worker( L, *super, bptr );

      if ( IS_SET( (*super)->flags, GDSR_IS_DSUBLYPH ) )
        break;
    }
  }

  if ( *super || !fMatch )
    SET_BIT( sub->flags, GDSR_ISNT_DSUBLYPH );
  else
  {
    SET_BIT( sub->flags, GDSR_IS_DSUBLYPH );
    **bptr = sub;
    (*bptr)++;
  }
}

void get_direct_sublyphs_recurse( lyph *L, trie *t, lyph ***bptr )
{
  if ( t->data )
    get_direct_sublyphs_worker( L, (lyph *)t->data, bptr );

  TRIE_RECURSE( get_direct_sublyphs_recurse( L, *child, bptr ) );
}

lyph **get_sublyphs( lyph *L, int direct )
{
  /*
   * To do: optimize this
   */
  lyph **buf, **bptr;

  CREATE( buf, lyph *, 1024 * 1024 );
  bptr = buf;

  if ( direct )
  {
    SET_BIT( L->flags, GDSR_ISNT_DSUBLYPH );
    get_direct_sublyphs_recurse( L, lyph_ids, &bptr );
    lyphs_unset_bits( GDSR_ISNT_DSUBLYPH | GDSR_IS_DSUBLYPH, lyph_ids );
  }
  else
    get_sublyphs_recurse( L, lyph_ids, &bptr );

  *bptr = NULL;
  return buf;
}

void populate_superless( trie *t, lyph ***sptr )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( !*L->supers )
    {
      **sptr = L;
      (*sptr)++;
    }
  }

  TRIE_RECURSE( populate_superless( *child, sptr ) );
}

char *hierarchy_member_to_json( lyph *L )
{
  lyph **subs = get_sublyphs( L, 1 );
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

char *lyph_hierarchy_to_json( void )
{
  lyph **superless, **sptr;
  char *retval;

  /*
   * To do: Improve this next line
   */
  CREATE( superless, lyph *, 1024 * 1024 );
  sptr = superless;

  populate_superless( lyph_ids, &sptr );

  retval = JSON1
  (
    "hierarchy": JS_ARRAY( hierarchy_member_to_json, superless )
  );

  free( superless );

  return retval;
}

void clear_lyph_hierarchy( trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data;

    if ( L->supers )
    {
      free( L->supers );
      L->supers = NULL;
    }
  }

  TRIE_RECURSE( clear_lyph_hierarchy( *child ) );
}

void recalculate_lyph_hierarchy( void )
{
  clear_lyph_hierarchy( lyph_ids );
  compute_lyph_hierarchy( lyph_ids );
}
