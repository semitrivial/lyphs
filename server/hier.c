#include "lyph.h"

void add_lyph_to_wrappers( lyph *L, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_lyphs_to_wrappers( lyph **L, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_ont_term_parents( trie *t, lyphs_wrapper **head, lyphs_wrapper **tail );
void add_supers_by_layers( trie *t, lyph *sub, lyphs_wrapper **head, lyphs_wrapper **tail );
int is_superlayer( layer *sup, layer *sub );
void get_next_lyph_hierarchy_level( lyph_wrapper **head, lyph_wrapper **tail, int *cnt, trie *t );
char *hierarchy_member_to_json( lyph *L );
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

int should_add_super( lyph *sup, lyph *sub )
{
  if ( sup == sub )
    return 0;

  if ( sup->ont_term && sub->ont_term )
  {
    trie **parent;

    for ( parent = sub->ont_term->data; *parent; parent++ )
      if ( *parent == sup->ont_term )
        return 1;
  }

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

/*
 * Possible to-do: optimize this function
 */
char *lyph_hierarchy_to_json( void )
{
  #define FOUND_IN_HIERARCHY 1
  str_wrapper *first_level = NULL, *last_level = NULL, *lev, *lev_next;
  char **levels, **lptr, *retval;
  int levcnt = 0;

  for ( ; ; )
  {
    lyph_wrapper *first_member = NULL, *last_member = NULL, *w, *w_next;
    lyph **members, **mptr;
    int cnt = 0;

    get_next_lyph_hierarchy_level( &first_member, &last_member, &cnt, lyph_ids );

    if ( !cnt )
      break;

    CREATE( members, lyph *, cnt + 1 );
    mptr = members;

    for ( w = first_member; w; w = w_next )
    {
      w_next = w->next;

      *mptr++ = w->L;
      SET_BIT( w->L->flags, FOUND_IN_HIERARCHY );
      free( w );
    }

    *mptr = NULL;

    CREATE( lev, str_wrapper, 1 );
    lev->str = JS_ARRAY( hierarchy_member_to_json, members );

    LINK2( lev, first_level, last_level, next, prev );
    levcnt++;

    free( members );
  }

  CREATE( levels, char *, levcnt + 1 );
  lptr = levels;

  for ( lev = first_level; lev; lev = lev_next )
  {
    lev_next = lev->next;

    *lptr++ = lev->str;
    free(lev);
  }

  *lptr = NULL;

  retval = JS_ARRAY( str_to_json, levels );
  free( levels );

  lyphs_unset_bit( FOUND_IN_HIERARCHY, lyph_ids );

  return JSON1( "levels": retval );
}

char *hierarchy_member_to_json( lyph *L )
{
  return JSON
  (
    "id": trie_to_json( L->id ),
    "superclasses": JS_ARRAY( lyph_to_shallow_json, L->supers )
  );
}

char *lyph_to_shallow_json( lyph *L )
{
  return trie_to_json( L->id );
}

void get_next_lyph_hierarchy_level( lyph_wrapper **head, lyph_wrapper **tail, int *cnt, trie *t )
{
  if ( t->data )
  {
    lyph *L = (lyph *)t->data, **sup;
    lyph_wrapper *w;

    if ( IS_SET( L->flags, FOUND_IN_HIERARCHY ) )
      goto get_next_lyph_hierarchy_level_escape;

    for ( sup = L->supers; *sup; sup++ )
      if ( !IS_SET( (*sup)->flags, FOUND_IN_HIERARCHY ) )
        goto get_next_lyph_hierarchy_level_escape;

    CREATE( w, lyph_wrapper, 1 );
    w->L = L;
    LINK( w, *head, *tail, next );
    (*cnt)++;
  }

  get_next_lyph_hierarchy_level_escape:
  TRIE_RECURSE( get_next_lyph_hierarchy_level( head, tail, cnt, *child ) );
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

lyph **get_sublyphs( lyph *L )
{
  /*
   * To do: optimize this
   */
  lyph **buf, **bptr;

  CREATE( buf, lyph *, 1024 * 1024 );
  bptr = buf;

  get_sublyphs_recurse( L, lyph_ids, &bptr );

  *bptr = NULL;
  return buf;
}
