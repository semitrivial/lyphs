#include "lyph.h"
#include "srv.h"

#define FMA_HASH 65536

#define ITERATE_FMAS( code )\
do\
{\
  for ( hash = 0; hash < FMA_HASH; hash++ )\
  for ( f = first_fma[hash]; f; f = f->next )\
  {\
    code ;\
  }\
}\
while(0)

fma *first_fma[FMA_HASH];
fma *last_fma[FMA_HASH];
nifling *first_nifling;
nifling *last_nifling;
trie *fmacheck;

fma *brain;
fma *seg_of_brain;

void parse_fma_file_for_raw_terms( char *file );
void parse_fma_file_for_parts( char *file );
char **parse_csv( const char *line, int *cnt );
void generate_inferred_dotfile( void );

char *label_by_fma( const fma *f )
{
  trie *t;
  char buf[2048];

  sprintf( buf, "FMA_%lu", f->id );

  t = trie_search( buf, iri_to_labels );

  if ( t && t->data && t->data[0] )
    return trie_to_json( t->data[0] );

  return NULL;
}

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
  f->superclasses = (fma**)blank_void_array();
  f->subclasses = (fma**)blank_void_array();
  f->inferred_parts = (fma**)blank_void_array();
  f->inferred_parents = (fma**)blank_void_array();
  f->flags = 0;
  f->is_up = 0;
  f->lyph = NULL;

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

void init_brain( void )
{
  brain = fma_by_ul( 50801 );
  seg_of_brain = fma_by_ul( 55676 );

  if ( !brain )
  {
    error_message( "There was no brain FMA term detected.  Shutting down." );
    exit(1);
  }
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

  init_brain();
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

void add_fma_sub( unsigned long id1, unsigned long id2 )
{
  fma *super, *sub;

  super = fma_by_ul( id1 );

  if ( !super )
    return;

  sub = fma_by_ul( id2 );

  if ( !sub )
    return;

  maybe_fma_append( &sub->superclasses, super );
  maybe_fma_append( &super->subclasses, sub );
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

  if ( !str_begins( line, "Part " ) && !str_begins( line, "Sub " ) )
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

  if ( str_begins( line, "Part " ) )
    add_fma_part( id1, id2 );
  else
    add_fma_sub( id1, id2 );
}

void parse_fma_file_for_parts( char *file )
{
  char *ptr, *left;

  log_string( "Parsing FMA file for parts and subclasses..." );

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

lyph *lyph_by_fma( fma *f )
{
  lyph *e;
  char buf[2048];

  sprintf( buf, "FMA_%lu", f->id );

  if ( !trie_search( buf, lyph_fmas ) )
    return NULL;

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->fma )
    {
      char *fmastr = trie_to_static( e->fma );

      if ( str_begins( fmastr, "FMA_" ) )
        fmastr += strlen( "FMA_" );
      else if ( str_begins( fmastr, "fma:" ) )
        fmastr += strlen( "fma:" );

      if ( strtoul( fmastr, NULL, 10 ) == f->id )
        return e;
    }
  }

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

char *fma_lyph_to_json( fma *f )
{
  lyph *e = lyph_by_fma( f );

  return e ? trie_to_json( e->id ) : NULL;
}

char *nifling_to_json( const nifling *n )
{
  return JSON
  (
    "fma1": ul_to_json( n->fma1->id ),
    "fma2": ul_to_json( n->fma2->id ),
    "fma1_lyph": fma_lyph_to_json( n->fma1 ),
    "fma2_lyph": fma_lyph_to_json( n->fma2 ),
    "pubmed": n->pubmed,
    "projection strength": n->proj,
    "species": n->species ? trie_to_json( n->species ) : NULL
  );
}

char *displayed_niflings_to_json( const displayed_niflings *dn )
{
  return JSON
  (
    "fma1": ul_to_json( dn->f1->id ),
    "fma2": ul_to_json( dn->f2->id ),
    "fma1_lyph": fma_lyph_to_json( dn->f1 ),
    "fma2_lyph": fma_lyph_to_json( dn->f2 ),
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
      fma *y_rep;

      if ( n->fma1 == f )
        y_rep = n->fma2;
      else
        y_rep = f;

      if ( (y_rep->flags & 1) || !(y_rep->flags & 2) )  // Find connections in y's tree but not in x's
        continue;

      if ( !finds )
      {
        CREATE( dn, displayed_niflings, 1 );
        dn->f1 = x;
        dn->f2 = y;
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

fma **get_fmas_from_fmasstr_and_lyphsstr( char *fmasstr, char *lyphsstr )
{
  fma **by_fmas, **by_lyphs, **by_lyphs_ptr, **buf, **fs;
  lyph **lyphs, **lyphsptr;
  int by_fmas_len, by_lyphs_len;

  if ( fmasstr )
  {
    by_fmas = (fma**)PARSE_LIST( fmasstr, fma_by_str, "fma", NULL );

    if ( !by_fmas )
      return NULL;
  }
  else
    by_fmas = (fma**)blank_void_array();

  if ( lyphsstr )
  {
    lyphs = (lyph**)PARSE_LIST( lyphsstr, lyph_by_id, "lyph", NULL );

    if ( !lyphs )
    {
      free( by_fmas );
      return NULL;
    }
  }
  else
    lyphs = (lyph**)blank_void_array();

  for ( fs = by_fmas; *fs; fs++ )
    (*fs)->flags = 1;

  CREATE( by_lyphs, fma *, VOIDLEN( lyphs ) + 1 );

  for ( lyphsptr = lyphs, by_lyphs_ptr = by_lyphs; *lyphsptr; lyphsptr++ )
  {
    fma *f;

    if ( !(*lyphsptr)->fma )
      continue;

    f = fma_by_trie( (*lyphsptr)->fma );

    if ( !f || f->flags == 1 )
      continue;

    *by_lyphs_ptr++ = f;
    f->flags = 1;
  }

  *by_lyphs_ptr = NULL;
  free( lyphs );

  by_fmas_len = VOIDLEN( by_fmas );
  by_lyphs_len = VOIDLEN( by_lyphs );

  CREATE( buf, fma *, by_fmas_len + by_lyphs_len + 1 );

  memcpy( buf, by_fmas, by_fmas_len * sizeof(fma*) );
  memcpy( buf + by_fmas_len, by_lyphs, by_lyphs_len * sizeof(fma*) );
  buf[by_fmas_len + by_lyphs_len] = NULL;
  free( by_fmas );
  free( by_lyphs );

  for ( fs = buf; *fs; fs++ )
    (*fs)->flags = 0;

  return buf;
}

HANDLER( do_nifs )
{
  fma **buf, **bptr1, **bptr2;
  displayed_niflings **dns, **dnsptr;
  char *fmasstr, *lyphsstr;
  int cnt;

  fmasstr = get_param( params, "fmas" );
  lyphsstr = get_param( params, "lyphs" );

  if ( !fmasstr && !lyphsstr )
    HND_ERR( "You did not specify either a comma-separated list of 'fmas' nor a comma-separated list of 'lyphs'" );

  buf = get_fmas_from_fmasstr_and_lyphsstr( fmasstr, lyphsstr );

  if ( !buf )
    HND_ERR( "One of the indicated fmas or lyphs was not recognized" );

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

int is_bdbpart_brain( fma *f )
{
  fma **fs;

  if ( f == brain )
    return 1;

  if ( f->flags & 2 )
    return 0;

  f->flags |= 2;

  for ( fs = f->parents; *fs; fs++ )
  {
    if ( is_bdbpart_brain( *fs ) )
    {
      f->flags &= ~2;
      return 1;
    }
  }

  for ( fs = f->superclasses; *fs; fs++ )
  {
    if ( is_bdbpart_brain( *fs ) )
    {
      f->flags &= ~2;
      return 1;
    }
  }

  return 0;
}

void dotfile_handle( fma *f, FILE *fp )
{
  fma **fs;

  f->flags |= 1;

  if ( f != brain )
  {
    for ( fs = f->parents; *fs; fs++ )
    {
      if ( (*fs)->flags & 1 )
        continue;

      dotfile_handle( *fs, fp );
    }
  }

  for ( fs = f->children; *fs; fs++ )
  {
    fprintf( fp, "    \"%lu\" -> \"%lu\";\n", f->id, (*fs)->id );

    if ( !( (*fs)->flags & 1 ) )
      dotfile_handle( *fs, fp );
  }
}

fma *common_ancestor( fma **arr )
{
  fma *anc, **ptr, *retval;

  for ( anc = arr[0]->parents[0]; anc; anc = anc->parents[0] )
    SET_BIT( anc->flags, 2 );

  for ( ptr = arr + 1; *ptr; ptr++ )
  {
    for ( anc = ptr[0]->parents[0]; anc; anc = anc->parents[0] )
      if ( IS_SET( anc->flags, 2 ) )
        break;

    retval = anc;

    if ( !anc )
      break;

    for ( anc = ptr[0]->parents[0]; anc != retval; anc = anc->parents[0] )
      REMOVE_BIT( anc->flags, 2 );
  }

  for ( anc = arr[0]->parents[0]; anc; anc = anc->parents[0] )
    REMOVE_BIT( anc->flags, 2 );

  return retval;  
}

void flatten_fma( fma *f )
{
  fma **p, **buf, *anc;

  if ( IS_SET( f->flags, 1 ) )
    return;

  SET_BIT( f->flags, 1 );

  if ( !f->parents[0] || !f->parents[1] )
    return;

  for ( p = f->parents; *p; p++ )
    flatten_fma( *p );

  anc = common_ancestor( f->parents );

  if ( !anc )
  {
    free( f->parents );
    f->parents = (fma**)blank_void_array();
    return;
  }

  CREATE( buf, fma *, 2 );
  buf[0] = anc;
  buf[1] = NULL;
  free( f->parents );
  f->parents = buf;

  return;
}

void flatten_fmas( void )
{
  fma *f;
  int hash;

  log_string( "Flattening FMA from DAG into tree..." );

  ITERATE_FMAS( flatten_fma( f ) );

  ITERATE_FMAS( f->flags = 0 );
}

void create_fma_lyph( fma *f, int brain_only )
{
  fma *parent;
  lyph *e;
  lyphnode *from, *to;

  if ( IS_SET( f->flags, 1 ) )
    return;

  SET_BIT( f->flags, 1 );

  if ( !f->parents[0] )
    parent = NULL;
  else if ( brain_only && !is_bdbpart_brain( f->parents[0] ) )
    parent = NULL;
  else
    parent = f->parents[0];

  from = make_lyphnode();
  to = make_lyphnode();

  if ( parent )
  {
    create_fma_lyph( parent, brain_only );
    from->location = parent->lyph;
    from->loctype = LOCTYPE_INTERIOR;
    to->location = parent->lyph;
    to->loctype = LOCTYPE_INTERIOR;
  }

  e = lyph_by_fma( f );

  if ( !e )
  {
    char *fmastr = strdupf( "FMA_%lu", f->id );
    char *namestr;
    trie *nametr;

    nametr = trie_search( fmastr, iri_to_labels );

    if ( nametr && nametr->data && nametr->data[0] )
      namestr = strdupf( "%s", trie_to_static( nametr->data[0] ) );
    else
      namestr = strdup( fmastr );

    e = make_lyph_nosave( 1, from, to, NULL, fmastr, namestr, NULL, NULL, "Human" );

    free( namestr );
    free( fmastr );
  }

  e->from = from;
  e->to = to;
  f->lyph = e;
}

void create_fma_lyphs( int brain_only )
{
  fma *f;
  int hash;

  ITERATE_FMAS
  (
    if ( brain_only && !is_bdbpart_brain( f ) )
      continue;

    create_fma_lyph( f, brain_only );
  );

  ITERATE_FMAS( f->flags = 0 );
}

HANDLER( do_create_fmalyphs )
{
  char *brainstr;

  brainstr = get_param( params, "brain" );

  if ( brainstr && !strcmp( brainstr, "yes" ) )
    create_fma_lyphs( 1 );
  else
    create_fma_lyphs( 0 );

  save_lyphs();

  send_ok( req );
}

void mark_brain_part( fma *f, fma ***bptr )
{
  if ( f->flags == 1 )
    return;

  **bptr = f;
  (*bptr)++;
  f->flags = 1;
}

void close_brain_markings_upward( fma **buf, fma ***bptr )
{
  fma **ptr;

  for ( ptr = buf; *ptr; ptr++ )
  {
    fma **parents;

    for ( parents = (*ptr)->parents; *parents; parents++ )
    {
      if ( (*parents)->flags == 1 )
        continue;

      mark_brain_part( *parents, bptr );
    }

    for ( parents = (*ptr)->superclasses; *parents; parents++ )
    {
      if ( (*parents)->flags == 1 )
        continue;

      mark_brain_part( *parents, bptr );
    }
  }
}

void close_brain_markings_downward( fma **buf, fma ***bptr )
{
  fma **ptr;

  for ( ptr = buf; *ptr; ptr++ )
  {
    fma **children;

    for ( children = (*ptr)->children; *children; children++ )
    {
      if ( (*children)->flags == 1 )
        continue;

      mark_brain_part( *children, bptr );
    }

    for ( children = (*ptr)->subclasses; *children; children++ )
    {
      if ( (*children)->flags == 1 )
        continue;

      mark_brain_part( *children, bptr );
    }
  }
}

int count_fmas( void )
{
  fma *f;
  int hash, cnt = 0;

  ITERATE_FMAS( cnt++ );

  return cnt;
}

void mark_brain_stuff( void )
{
  fma **buf, **bptr;
  int cnt = count_fmas();

  CREATE( buf, fma *, cnt + 1 );
  bptr = buf;

  mark_brain_part( brain, &bptr );
  mark_brain_part( seg_of_brain, &bptr );

  close_brain_markings_downward( buf, &bptr );

  close_brain_markings_upward( buf, &bptr );

  free( buf );
}

void unmark_brain_stuff( void )
{
  fma *f;
  int hash;

  ITERATE_FMAS( f->flags = 0 );
}

void fprintf_dotfile_vertex( fma *f, FILE *fp )
{
  char *label, *color;

  label = label_by_fma( f );

  if ( f == brain || f == seg_of_brain )
    color = "red";
  else if ( strstr( label, "left" ) || strstr( label, "Left" ) )
    color = "greenyellow";
  else if ( strstr( label, "right" ) || strstr( label, "Right" ) )
    color = "gold";
  else
    color = NULL;

  fprintf( fp, "  %s [label=\"%lu\"", label, f->id );

  if ( color )
    fprintf( fp, ", style=filled, fillcolor=%s", color );

  if ( f->inferred_parents[0] && f->inferred_parents[1] )
    fprintf( fp, ", shape=box" );
  else if ( !f->inferred_parents[0] && f->inferred_parts[0] )
    fprintf( fp, ", shape=diamond" );

  fprintf( fp, "];\n" );
}

/*
 * The following was a presumably one-time use plugin requested by BdB.
 * Currently disabled in tables.c
 */
HANDLER( do_dotfile )
{
  FILE *fp, *csv;
  fma *f;
  int hash;

  if ( get_param( params, "inferred" ) )
  {
    generate_inferred_dotfile();
    send_ok( req );
  }

  fp = fopen( "dotfile.txt", "w" );
  csv = fopen( "dotcsv.csv", "w" );

  if ( !fp )
    HND_ERR( "!fp" );

  mark_brain_stuff();

  fprintf( csv, "FMA ID,RDFS:label\n" );

  ITERATE_FMAS
  (
    if ( f->flags != 1 )
      continue;

    fprintf( csv, "%lu,%s\n", f->id, label_by_fma(f) );
  );

  fclose( csv );

  fprintf( fp, "digraph\n{\n" );

  ITERATE_FMAS
  (
    if ( f->flags != 1 )
      continue;

    fprintf_dotfile_vertex( f, fp );
  );

  fprintf( fp, "  subgraph cluster_0\n  {\n" );

  ITERATE_FMAS
  (
    fma **ptr;
    char *label;

    if ( f->flags != 1 )
      continue;

    label = label_by_fma( f );

    for ( ptr = f->children; *ptr; ptr++ )
    {
      if ( (*ptr)->flags != 1 )
        continue;

      fprintf( fp, "    %s -> ", label );
      fprintf( fp, "%s [color=pink];\n", label_by_fma( *ptr ) );
    }

    for ( ptr = f->subclasses; *ptr; ptr++ )
    {
      if ( (*ptr)->flags != 1 )
        continue;

      fprintf( fp, "    %s -> ", label );
      fprintf( fp, "%s [color=purple];\n", label_by_fma( *ptr ) );
    }
  );

  fprintf( fp, "  }\n" );

  fprintf( fp, "}\n" );

  fclose( fp );

  unmark_brain_stuff();

  send_ok( req );
}

int is_oriented( fma *f, char **side )
{
  static char *left = "left", *right = "right";
  char *label = label_by_fma( f );

  if ( strstr( label, "left" ) || strstr( label, "Left" ) )
  {
    if ( side )
      *side = left;

    return 1;
  }

  if ( strstr( label, "right" ) || strstr( label, "Right" ) )
  {
    if ( side )
      *side = right;

    return 1;
  }

  return 0;
}

void infer_inferred_parts( fma *abstract, fma *parent, char *side )
{
  fma **subs;
  char *subside;
  int fMatch = 0;

  for ( subs = abstract->children; *subs; subs++ )
  {
    if ( !is_oriented( *subs, &subside ) )
      continue;

    if ( subside == side )
    {
      maybe_fma_append( &parent->inferred_parts, *subs );
      maybe_fma_append( &(*subs)->inferred_parents, parent );
    }

    fMatch = 1;
  }

  if ( !fMatch )
  {
    fma **parts;

    for ( parts = abstract->children; *parts; parts++ )
    {
      if ( *parts == abstract )
        continue;

      infer_inferred_parts( *parts, parent, side );
    }
  }
}

void compute_inferred_parts( void )
{
  fma *f, **supers, **parts;
  char *side;
  int hash;

  log_string( "Computing inferred parts of brain..." );

  mark_brain_stuff();

  ITERATE_FMAS
  (
    if ( f->flags != 1 )
      continue;

    if ( !is_oriented( f, &side ) )
      continue;

    for ( supers = f->superclasses; *supers; supers++ )
    for ( parts = (*supers)->children; *parts; parts++ )
      infer_inferred_parts( *parts, f, side );

    for ( parts = f->children; *parts; parts++ )
    {
      maybe_fma_append( &f->inferred_parts, *parts );
      maybe_fma_append( &(*parts)->inferred_parents, f );
    }
  );

  unmark_brain_stuff();

  return;
}

void generate_inferred_dotfile( void )
{
  FILE *fp = fopen( "inf_dotfile.txt", "w" );
  fma *f;
  int hash;

  if ( !fp )
    return;

  fprintf( fp, "digraph\n{\n" );

  mark_brain_stuff();

  ITERATE_FMAS
  (
    if ( !*f->inferred_parents
    &&   !*f->inferred_parts
    && ( f->flags != 1 || !is_oriented( f, NULL ) ) )
      continue;

    fprintf_dotfile_vertex( f, fp );
  );

  fprintf( fp, "  subgraph cluster_0\n  {\n" );

  ITERATE_FMAS
  (
    fma **infs;
    char *label;

    if ( !*f->inferred_parts )
      continue;

    label = label_by_fma( f );

    for ( infs = f->inferred_parts; *infs; infs++ )
      fprintf( fp, "    %s -> %s;\n", label, label_by_fma( *infs ) );
  );

  unmark_brain_stuff();

  fprintf( fp, "  }\n}\n" );

  fclose( fp );
}
