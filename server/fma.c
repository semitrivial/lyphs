#include "lyph.h"
#include "srv.h"

#define FMA_HASH 65536

fma *first_fma[FMA_HASH];
fma *last_fma[FMA_HASH];
nifling *first_nifling;
nifling *last_nifling;
trie *fmacheck;

void parse_fma_file_for_raw_terms( char *file );
void parse_fma_file_for_parts( char *file );
char **parse_csv( const char *line, int *cnt );

fma *fma_by_trie( trie *id )
{
  fma *f;
  char *str = trie_to_static( id );
  unsigned long n;

  if ( str_begins( str, "FMA_" ) )
    str += strlen( "FMA_" );

  n = strtoul( str, NULL, 10 );

  if ( n < 1 )
    return NULL;

  for ( f = first_fma[n%FMA_HASH]; f; f = f->next )
    if ( f->id == n )
      return f;

  return NULL;
}

fma *fma_by_ul( unsigned long id )
{
  fma *f;

  for ( f = first_fma[id%FMA_HASH]; f; f = f->next )
    if ( f->id == id )
      return f;

  return NULL;
}

fma *fma_by_str( const char *str )
{
  unsigned long id;

  if ( str_begins( str, "FMA_" ) )
    str += strlen( "FMA_" );
  else if ( str_begins( str, "fma:" ) )
    str += strlen( "fma:" );

  id = strtoul( str, NULL, 10 );

  if ( id < 1 )
    return NULL;

  return fma_by_ul( id );
}

void add_raw_fma_term( unsigned long id )
{
  fma *f;
  char buf[1024];
  int hash;

  sprintf( buf, "%ld", id );

  if ( trie_search( buf, fmacheck ) )
    return;

  trie_strdup( buf, fmacheck );

  hash = id % FMA_HASH;

  CREATE( f, fma, 1 );
  f->id = id;
  f->parents = (fma**)blank_void_array();
  f->children = (fma**)blank_void_array();
  f->niflings = (nifling**)blank_void_array();
  f->flags = 0;
  f->is_up = 0;

  LINK( f, first_fma[hash], last_fma[hash], next );
}

void parse_fma_for_terms_one_word( char *word )
{
  const char *prefix = "http://purl.org/obo/owlapi/fma#FMA_";
  unsigned long id;

  if ( !str_begins( word, prefix ) )
    return;

  id = strtoul( word + strlen( prefix ), NULL, 10 );

  if ( id < 1 )
    return;

  add_raw_fma_term( id );
}

void parse_fma_file( void )
{
  char *file = load_file( FMA_FILE );
  int hash;

  for ( hash = 0; hash < FMA_HASH; hash++ )
  {
    first_fma[hash] = NULL;
    last_fma[hash] = NULL;
  }

  log_string( "Parsing FMA file..." );

  fmacheck = blank_trie();

  if ( !file )
  {
    error_messagef( "Could not open %s for reading -- no fma partonomy loaded", FMA_FILE );
    return;
  }

  parse_fma_file_for_raw_terms( file );
  parse_fma_file_for_parts( file );

  free( file );
}

void parse_fma_file_for_raw_terms( char *file )
{
  char *ptr, *left;

  log_string( "Parsing FMA file for raw terms..." );

  for ( ptr = file, left = file; *ptr; ptr++ )
  {
    if ( *ptr == ' ' || *ptr == '\n' )
    {
      char tmp = *ptr;

      *ptr = '\0';
      parse_fma_for_terms_one_word( left );
      left = ptr + 1;
      *ptr = tmp;
    }
  }
}

void maybe_fma_append( fma ***arr, fma *what )
{
  fma **deref = *arr, **ptr, **buf;

  for ( ptr = deref; *ptr; ptr++ )
    if ( *ptr == what )
      return;

  CREATE( buf, fma *, ptr - deref + 2 );

  memcpy( buf, deref, (ptr - deref) * sizeof(fma*) );
  buf[ptr-deref] = what;
  buf[ptr-deref + 1] = NULL;

  free( deref );
  *arr = buf;
}

void add_fma_part( unsigned long id1, unsigned long id2 )
{
  fma *parent, *child;

  parent = fma_by_ul( id1 );

  if ( !parent )
    return;

  child = fma_by_ul( id2 );

  if ( !child )
    return;

  maybe_fma_append( &parent->children, child );
  maybe_fma_append( &child->parents, parent );
}

void parse_fma_for_parts_one_line( char *line )
{
  char *space, *pound;
  unsigned long id1, id2;

  if ( !str_begins( line, "Part " ) )
    return;

  for ( pound = line; *pound; pound++ )
    if ( *pound == '#' )
      break;

  if ( !*pound )
    return;

  for ( space = pound; *space; space++ )
    if ( *space == ' ' )
      break;

  if ( !*space )
    return;

  *space = '\0';
  if ( str_begins( pound + 1, "FMA_" ) )
    id1 = strtoul( pound + strlen( "FMA_" ) + 1, NULL, 10 );
  else
    id1 = 0;
  *space = ' ';

  if ( id1 < 1 )
    return;

  for ( pound = space + 1; *pound; pound++ )
    if ( *pound == '#' )
      break;

  if ( !*pound )
    return;

  if ( str_begins( pound + 1, "FMA_" ) )
    id2 = strtoul( pound + strlen( "FMA_" ) + 1, NULL, 10 );
  else
    id2 = 0;

  if ( id2 < 1 )
    return;

  add_fma_part( id1, id2 );
}

void parse_fma_file_for_parts( char *file )
{
  char *ptr, *left;

  log_string( "Parsing FMA file for parts..." );

  for ( ptr = file, left = file; *ptr; ptr++ )
  {
    if ( *ptr == '\n' )
    {
      *ptr = '\0';
      parse_fma_for_parts_one_line( left );
      left = ptr + 1;
      *ptr = '\n';
    }
  }
}

lyph *lyph_by_fma( char *fma, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph*)t->data;

    if ( is_human_species( e ) && e->fma && !strcmp( trie_to_static( e->fma ), fma ) )
      return e;
  }

  TRIE_RECURSE
  (
    lyph *e = lyph_by_fma( fma, *child );

    if ( e )
      return e;
  );

  return NULL;
}

void fma_append_nifling( fma *f, nifling *n )
{
  nifling **buf;
  int cnt = VOIDLEN( f->niflings );

  CREATE( buf, nifling *, cnt + 2 );
  memcpy( buf, f->niflings, cnt * sizeof(nifling*) );
  buf[cnt] = n;
  buf[cnt+1] = NULL;

  free( f->niflings );
  f->niflings = buf; 
}

/*
 * Following function intentionally leaks memory because the
 * complexity of preventing such would not be worth the saved memory
 * (since function is only called once)
 */
void parse_nifling_line( char *line )
{
  fma *fma1, *fma2;
  nifling *n;
  char **cols;
  unsigned long id1, id2;
  int cnt;

  cols = parse_csv( line, &cnt );

  if ( !cols )
    return;

  if ( cnt < 7 )
    return;

  if ( !str_begins( cols[0], "fma:" ) )
    return;

  if ( !str_begins( cols[1], "fma:" ) )
    return;

  id1 = strtoul( cols[0] + strlen("fma:"), NULL, 10 );
  id2 = strtoul( cols[1] + strlen("fma:"), NULL, 10 );

  if ( id1 < 1 || id2 < 1 )
    return;

  fma1 = fma_by_ul( id1 );
  fma2 = fma_by_ul( id2 );

  if ( !fma1 || !fma2 || fma1 == fma2 )
    return;

  CREATE( n, nifling, 1 );
  n->fma1 = fma1;
  n->fma2 = fma2;

  if ( *(cols[5]) )
    n->pubmed = strdup( cols[5] );
  else
    n->pubmed = NULL;

  if ( *(cols[6]) )
    n->proj = strdup( cols[6] );
  else
    n->proj = NULL;

  if ( *(cols[4]) )
    n->species = trie_strdup( cols[4], metadata );
  else
    n->species = NULL;

  fma_append_nifling( fma1, n );
  fma_append_nifling( fma2, n );
}

void parse_nifling_file( void )
{
  char *file = load_file( NIFLING_FILE );
  char *left, *ptr;

  log_string( "Parsing nifling file..." );

  if ( !file )
  {
    error_messagef( "Could not read from %s -- no niflings loaded", NIFLING_FILE );
    return;
  }

  for ( ptr = file, left = file; *ptr; ptr++ )
  {
    if ( *ptr == '\n' )
    {
      *ptr = '\0';
      parse_nifling_line( left );
      *ptr = '\n';
      left = ptr + 1;
    }
  }

  free( file );
}

char *nifling_to_json( const nifling *n )
{
  return JSON
  (
    "fma1": ul_to_json( n->fma1->id ),
    "fma2": ul_to_json( n->fma2->id ),
    "pubmed": n->pubmed,
    "projection strength": n->proj,
    "species": n->species ? trie_to_json( n->species ) : NULL
  );
}

char *displayed_niflings_to_json( const displayed_niflings *dn )
{
  return JSON
  (
    "lyph1": ul_to_json( dn->f1->id ),
    "lyph2": ul_to_json( dn->f2->id ),
    "niflings": JS_ARRAY( nifling_to_json, dn->niflings )
  );
}

void mark_fma_tree_single( fma *f, int bit, fma **head, fma **tail, int offset, int is_up )
{
  f->flags |= bit;

  if ( is_up )
    f->is_up = 1;
  else
    f->is_up = 0;

  if ( !offset )
    LINK( f, *head, *tail, next_by_x );
  else
    LINK( f, *head, *tail, next_by_y );
}

void mark_fma_tree_upward( fma *f, int bit, fma **head, fma **tail, int offset )
{
  fma **parents;

  for ( parents = f->parents; *parents; parents++ )
  {
    if ( (*parents)->flags & bit )
      continue;

    mark_fma_tree_single( *parents, bit, head, tail, offset, 1 );
    mark_fma_tree_upward( *parents, bit, head, tail, offset );
  }
}

void mark_fma_tree_downward( fma *f, int bit, fma **head, fma **tail, int offset )
{
  fma **children;

  for ( children = f->children; *children; children++ )
  {
    if ( (*children)->flags & bit )
      continue;

    mark_fma_tree_single( *children, bit, head, tail, offset, 0 );
    mark_fma_tree_downward( *children, bit, head, tail, offset );
  }
}

void mark_fma_tree( fma *f, int bit, fma **head, fma **tail, int offset )
{
  mark_fma_tree_single( f, bit, head, tail, offset, 0 );
  mark_fma_tree_upward( f, bit, head, tail, offset );
  mark_fma_tree_downward( f, bit, head, tail, offset );
}

displayed_niflings *compute_niflings_by_fma( fma *x, fma *y )
{
  displayed_niflings *dn;
  fma *xhead = NULL, *xtail = NULL, *yhead = NULL, *ytail = NULL, *f, *f_next;
  int finds = 0;

  mark_fma_tree( x, 1, &xhead, &xtail, 0 );
  mark_fma_tree( y, 2, &yhead, &ytail, 1 );

  for ( f = xhead; f; f = f->next_by_x )  // Iterate over fma's in x's tree but not in y's
  {
    nifling **nptr;

    if ( f->flags & 2 )
      continue;

    for ( nptr = f->niflings; *nptr; nptr++ )
    {
      nifling *n = *nptr;
      fma *x_rep, *y_rep;

      if ( n->fma1 == f )
      {
        x_rep = f;
        y_rep = n->fma2;
      }
      else
      {
        y_rep = f;
        x_rep = n->fma2;
      }

      if ( (y_rep->flags & 1) || !(y_rep->flags & 2) )  // Find connections in y's tree but not in x's
        continue;

      if ( !finds )
      {
        CREATE( dn, displayed_niflings, 1 );
        dn->f1 = x_rep;
        dn->f2 = y_rep;
        CREATE( dn->niflings, nifling *, 2 );
        dn->niflings[0] = n;
        dn->niflings[1] = NULL;
      }
      else
      {
        nifling **newbuf;

        CREATE( newbuf, nifling *, finds + 2 );
        memcpy( newbuf, dn->niflings, finds * sizeof(nifling*) );
        newbuf[finds] = n;
        newbuf[finds+1] = NULL;
        free( dn->niflings );
        dn->niflings = newbuf;
      }

      finds++;
    }
  }

  for ( f = xhead; f; f = f_next )
  {
    f_next = f->next_by_x;
    f->next_by_x = NULL;
    f->flags = 0;
  }

  for ( f = yhead; f; f = f_next )
  {
    f_next = f->next_by_y;
    f->next_by_y = NULL;
    f->flags = 0;
  }

  if ( finds )
    return dn;
  else
    return NULL;
}

HANDLER( do_nifs )
{
  fma **buf, **bptr1, **bptr2;
  displayed_niflings **dns, **dnsptr;
  char *fmasstr, *err;
  int cnt;

  TRY_PARAM( fmasstr, "fmas", "You did not specify a comma-separated list of 'fmas'" );

  buf = (fma **)PARSE_LIST( fmasstr, fma_by_str, "fma", &err );

  if ( !buf )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated fmas was not recognized" );
  }

  cnt = VOIDLEN( buf );

  if ( cnt > 100 )
  {
    free( buf );
    HND_ERR( "The nifs command is restricted to 100 fmas at once" );
  }

  CREATE( dns, displayed_niflings *, (cnt * (cnt+1))/2 + 1 );
  dnsptr = dns;

  for ( bptr1 = buf; *bptr1; bptr1++ )
  for ( bptr2 = bptr1 + 1; *bptr2; bptr2++ )
  {
    displayed_niflings *computed;

    if ( (computed = compute_niflings_by_fma( *bptr1, *bptr2 )) != NULL )
      *dnsptr++ = computed;
  }

  *dnsptr = NULL;

  send_response( req, JS_ARRAY( displayed_niflings_to_json, dns ) );

  for ( dnsptr = dns; *dnsptr; dnsptr++ )
    free( *dnsptr );

  free( dns );
}
