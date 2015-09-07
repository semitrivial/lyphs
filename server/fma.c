/*
 *  fma.c
 *  Logic surrounding the importation, mapping, and usage of
 *  FMA terms.
 */
#include "lyph.h"
#include "srv.h"

#define INF_DOTFILE "inf_dotfile.txt"
#define DOTFILE "dotfile.txt"

typedef struct FMA_LYPH_PAIR
{
  fma *f;
  lyph *e;
} fma_lyph_pair;

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
void generate_inferred_dotfile( fma **seeds, int skip_lat, int raw_nodes, int tabdelim );
char *fma_to_json( const fma *f );
char *fma_to_json_brief( const fma *f );

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

char *label_by_fma_nojson( const fma *f )
{
  trie *t;
  char buf[2048];

  sprintf( buf, "FMA_%lu", f->id );

  t = trie_search( buf, iri_to_labels );

  if ( t && t->data && t->data[0] )
    return strdup( trie_to_static( t->data[0] ) );

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
  char *cpy = strdup(str), *spaceless;
  unsigned long id;

  spaceless = trim_spaces(cpy);

  if ( strlen(spaceless)>2 && *spaceless == '\"' && str[strlen(spaceless)-1] == '\"' )
  {
    spaceless[strlen(spaceless)-1] = '\0';
    spaceless++;
  }

  if ( str_begins( spaceless, "FMA_" ) )
    spaceless += strlen( "FMA_" );
  else if ( str_begins( spaceless, "fma:" ) )
    spaceless += strlen( "fma:" );

  id = strtoul( spaceless, NULL, 10 );
  free( cpy );

  if ( id < 1 )
    return NULL;

  return fma_by_ul( id );
}

void add_raw_fma_term( unsigned long id )
{
  fma *f;
  trie *t;
  static trie *marker = NULL;
  char buf[1024];
  int hash;

  sprintf( buf, "%ld", id );

  t = trie_search( buf, fmacheck );

  if ( t && t->data )
    return;

  t = trie_strdup( buf, fmacheck );
  t->data = &marker;

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

lyph *lyph_by_fma( const fma *f )
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

void create_fma_lyph( fma *f, int recursive, int brain_only )
{
  fma *parent;
  lyph *e;
  lyphnode *from, *to;

  if ( recursive )
  {
    if ( IS_SET( f->flags, 1 ) )
      return;

    SET_BIT( f->flags, 1 );

    if ( !f->parents[0] )
      parent = NULL;
    else if ( brain_only && !is_bdbpart_brain( f->parents[0] ) )
      parent = NULL;
    else
      parent = f->parents[0];
  }

  from = make_lyphnode();
  to = make_lyphnode();

  /*
   * Temporarily (?) disable partonomy here
   */
  if ( recursive && parent && 0 )
  {
    create_fma_lyph( parent, 1, brain_only );
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

    create_fma_lyph( f, 1, brain_only );
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

void mark_brain_stuff( fma **seeds )
{
  fma **buf, **bptr;
  int cnt = count_fmas();

  CREATE( buf, fma *, cnt + 1 );
  bptr = buf;

  if ( seeds )
    while ( *seeds )
      mark_brain_part( *seeds++, &bptr );
  else
  {
    mark_brain_part( brain, &bptr );
    mark_brain_part( seg_of_brain, &bptr );
  }

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
  else if ( !f->inferred_parents[0] && !f->inferred_parts[0] )
    fprintf( fp, ", shape=egg" );

  fprintf( fp, "];\n" );
}

/*
 * The following was a presumably one-time use plugin requested by BdB.
 * Currently disabled in tables.c
 */
HANDLER( do_dotfile )
{
  FILE *fp, *csv;
  fma *f, **seeds;
  char *seedstr, *file, *lateralized, *tabdelimitedstr, *raw_nodes_str;
  int hash, skip_lat, tabdelimited, raw_nodes;

  seedstr = get_param( params, "seeds" );
  lateralized = get_param( params, "lateralized" );
  tabdelimitedstr = get_param( params, "tabdelimited" );
  raw_nodes_str = get_param( params, "rawnodes" );

  tabdelimited = tabdelimitedstr && !strcmp( tabdelimitedstr, "1" );
  raw_nodes = raw_nodes_str && !strcmp( raw_nodes_str, "1" );

  if ( tabdelimited && !raw_nodes )
    HND_ERR( "The 'tabdelimited' option is only available in conjunction with the 'rawnodes' option" );

  if ( lateralized && !strcmp( lateralized, "0" ) )
    skip_lat = 1;
  else
    skip_lat = 0;

  if ( seedstr )
  {
    seeds = (fma**)PARSE_LIST( seedstr, fma_by_str, "fma", NULL );

    if ( !seeds )
      HND_ERR( "One of the indicated FMA terms was unrecognized" );
  }
  else
    seeds = NULL;

  if ( get_param( params, "inferred" ) )
  {
    generate_inferred_dotfile( seeds, skip_lat, raw_nodes, tabdelimited );

    if ( seeds )
      free( seeds );

    file = load_file( INF_DOTFILE );

    if ( !file )
      HND_ERR( "Something went wrong, and the dotfile was not generated" );

    send_response_with_type( req, "200", file, "text/plain" );
    free( file );
    return;
  }

  fp = fopen( DOTFILE, "w" );
  csv = fopen( "dotcsv.csv", "w" );

  if ( !fp )
    HND_ERR( "!fp" );

  mark_brain_stuff( seeds );

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

  if ( seeds )
    free( seeds );

  file = load_file( DOTFILE );
  send_response_with_type( req, "200", file, "text/plain" );
  free( file );
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

void infer_inferred_parts( fma *abstract, fma *parent, char *side, int skip_lat )
{
  fma **subs;
  char *subside;
  int fMatch = 0;

  for ( subs = abstract->children; *subs; subs++ )
  {
    if ( !skip_lat && !is_oriented( *subs, &subside ) )
      continue;

    if ( skip_lat || subside == side )
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

      infer_inferred_parts( *parts, parent, side, skip_lat );
    }
  }
}

void recompute_inferred_parts( fma **seeds, int skip_lat )
{
  fma *f;
  int hash;

  ITERATE_FMAS
  (
    free( f->inferred_parts );
    free( f->inferred_parents );
    f->inferred_parts = (fma**)blank_void_array();
    f->inferred_parents = (fma**)blank_void_array();
  );

  compute_inferred_parts( seeds, skip_lat );
}

void compute_inferred_parts( fma **seeds, int skip_lat )
{
  fma *f, **supers, **parts;
  char *side;
  int hash;

  log_string( "Computing inferred parts of brain..." );

  mark_brain_stuff( seeds );

  ITERATE_FMAS
  (
    if ( f->flags != 1 )
      continue;

    if ( !skip_lat && !is_oriented( f, &side ) )
      continue;

    for ( supers = f->superclasses; *supers; supers++ )
    for ( parts = (*supers)->children; *parts; parts++ )
      infer_inferred_parts( *parts, f, side, skip_lat );

    for ( parts = f->children; *parts; parts++ )
    {
      maybe_fma_append( &f->inferred_parts, *parts );
      maybe_fma_append( &(*parts)->inferred_parents, f );
    }
  );

  unmark_brain_stuff();

  return;
}

void generate_inferred_dotfile( fma **seeds, int skip_lat, int raw_nodes, int tabdelim )
{
  FILE *fp = fopen( INF_DOTFILE, "w" );
  fma *f;
  int hash;

  if ( !fp )
    return;

  if ( !raw_nodes )
    fprintf( fp, "digraph\n{\n" );

  recompute_inferred_parts( seeds, skip_lat );

  mark_brain_stuff( seeds );

  ITERATE_FMAS
  (
    if ( !*f->inferred_parents
    &&   !*f->inferred_parts )
    {
      if ( f->flags != 1 )
        continue;

      if ( !skip_lat && !is_oriented( f, NULL ) )
        continue;
    }

    if ( !tabdelim )
      fprintf_dotfile_vertex( f, fp );
    else
      fprintf( fp, "FMA_%ld\t%s\n", f->id, label_by_fma( f ) );
  );

  if ( !raw_nodes )
  {
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
  }

  unmark_brain_stuff();

  if ( !raw_nodes )
    fprintf( fp, "  }\n}\n" );

  fclose( fp );

  recompute_inferred_parts( NULL, 0 );
}

char *fma_lyph_pair_to_json( const fma_lyph_pair *p )
{
  return JSON
  (
    "fma": ul_to_json( p->f->id ),
    "fma label": label_by_fma( p->f ),
    "lyph": trie_to_json( p->e->id ),
    "lyph fma": p->e->fma ? trie_to_json( p->e->fma ) : NULL
  );
}

lyph *highlight_fma_lyph_conflict( const fma *f )
{
  lyph *e;
  char *label = label_by_fma_nojson( f ), fmastr[1024];

  if ( !label )
    label = "";

  sprintf( fmastr, "FMA_%lu", f->id );

  for ( e = first_lyph; e; e = e->next )
  {
    char *efmastr = (e->fma ? strdup(trie_to_static(e->fma)) : "");
    char *ename = (e->name ? trie_to_static( e->name ) : "");

    if ( ( strcmp( label, ename ) && !strcmp( efmastr, fmastr ) )
    ||   (!strcmp( label, ename ) &&  strcmp( efmastr, fmastr ) ) )
    {
      if ( e->fma )
        free( efmastr );

      free( label );
      return e;
    }

    free( efmastr );
  }

  free( label );
  return NULL;
}

/*
 * The following should presumably be a one-time use plugin BdB requested
 */
HANDLER( do_import_lateralized_brain )
{
  fma *f;
  fma_lyph_pair **buf, **bptr, *p;
  lyph *e;
  int hash, cnt = 0;

  mark_brain_stuff( NULL );

  ITERATE_FMAS( cnt++ );

  CREATE( buf, fma_lyph_pair *, cnt + 1 );
  bptr = buf;

  ITERATE_FMAS
  (
    if ( f->flags != 1 )
      continue;

    if ( !is_oriented( f, NULL ) && !*f->inferred_parents && !*f->inferred_parts )
      continue;

    e = highlight_fma_lyph_conflict( f );

    if ( e )
    {
      CREATE( p, fma_lyph_pair, 1 );
      p->f = f;
      p->e = e;
      *bptr++ = p;
    }

    if ( lyph_by_fma( f ) )
      continue;

    create_fma_lyph( f, 0, 1 );
  );

  *bptr = NULL;

  unmark_brain_stuff();

  save_lyphs();

  send_response( req, JSON1
  (
    "conflicts": JS_ARRAY( fma_lyph_pair_to_json, buf )
  ));

  for ( bptr = buf; *bptr; bptr++ )
    free( *bptr );

  free( buf );
}

HANDLER( do_fma )
{
  fma *f;

  f = fma_by_str( request );

  if ( !f )
    HND_ERR( "The indicated FMA term was unrecognized" );

  send_response( req, fma_to_json( f ) );
}

char *fma_to_json_brief( const fma *f )
{
  char *label = label_by_fma( f );

  return JSON
  (
    "id": ul_to_json( f->id ),
    "label": label
  );
}

char *fma_to_json( const fma *f )
{
  char *label = label_by_fma( f );

  return JSON
  (
    "id": ul_to_json( f->id ),
    "label": label,
    "parents": JS_ARRAY( fma_to_json_brief, f->parents ),
    "children": JS_ARRAY( fma_to_json_brief, f->children ),
    "superclasses": JS_ARRAY( fma_to_json_brief, f->superclasses ),
    "subclasses": JS_ARRAY( fma_to_json_brief, f->subclasses ),
    "inferred_parents": JS_ARRAY( fma_to_json_brief, f->inferred_parents ),
    "inferred_parts": JS_ARRAY( fma_to_json_brief, f->inferred_parts )
  );
}

lyph *nearest_fmad_parent( lyph *e, int *dist )
{
  lyph *parent;
  int d = 1;

  for ( parent = get_relative_lyph_loc_buf( e, NULL ); parent; parent = get_relative_lyph_loc_buf( parent, NULL ) )
  {
    if ( parent->fma )
    {
      *dist = d;
      return parent;
    }
    d++;
  }

  return NULL;
}

HANDLER( do_fmamap )
{
  FILE *fp = fopen( FMAMAP_FILE, "w" );
  lyph *e, *parent;
  char *txt, *suppress_str;
  int dist, suppress;

  if ( !fp )
    HND_ERR( "Could not open " FMAMAP_FILE " to write" );

  fprintf( fp, "LyphID\tLyphName\tFMA\tDistance_to_parent_with_FMA\n" );

  suppress_str = get_param( params, "suppress" );
  suppress = suppress_str && !strcmp( suppress_str, "1" );

  for ( e = first_lyph; e; e = e->next )
  {
    if ( !e->fma )
    {
      parent = nearest_fmad_parent( e, &dist );

      if ( !parent && suppress )
        continue;
    }

    fprintf( fp, "%s\t", trie_to_static(e->id) );

    if ( e->fma )
    {
      fprintf( fp, "%s\t0\n", trie_to_static( e->fma ) );
      continue;
    }

    if ( parent )
      fprintf( fp, "%s\t%d\t", trie_to_static( parent->fma ), dist );
    else
      fprintf( fp, "None\tNaN\t" );

    if ( !e->name )
      fprintf( fp, "None\n" );
    else
    {
      char *name = trie_to_static( e->name );

      if ( !*name )
        fprintf( fp, "(blank)\n" );
      else
        fprintf( fp, "%s\n", name );
    }
  }

  fclose( fp );

  txt = load_file( FMAMAP_FILE );

  if ( !txt )
    HND_ERR( "Could not read " FMAMAP_FILE );

  send_response_with_type( req, "200", txt, "text/tab-separated-values" );

  free( txt );
}

lyph *lyph_by_fma_scai( const fma *f, char direction, int *dist, lyph ***siblings )
{
  lyph *e;
  fma **fptr, **list;

  e = lyph_by_fma( f );

  if ( e )
    return e;

  if ( direction == 'u' && f->parents )
  {
    for ( fptr = f->parents; *fptr; fptr++ )
    {
      e = lyph_by_fma_scai( *fptr, direction, dist, siblings );

      if ( e )
      {
        (*dist)++;
        return e;
      }
    }
  }

  list = (direction == 'u')? f->superclasses : f->subclasses;

  if ( list )
  {
    for ( fptr = list; *fptr; fptr++ )
    {
      e = lyph_by_fma_scai( *fptr, direction, dist, siblings );

      if ( e )
      {
        if ( direction == 'd' && (*dist) == 0 )
        {
          lyph **buf, **bptr, *sibling;

          CREATE( buf, lyph *, lyphcnt + 1 );
          bptr = buf;
          *bptr++ = e;

          for ( fptr = fptr + 1; *fptr; fptr++ )
          {
            sibling = lyph_by_fma( *fptr );

            if ( sibling )
              *bptr++ = sibling;
          }
          *bptr = NULL;
          *siblings = buf;
        }

        (*dist)++;
        return e;
      }
    }
  }

  return NULL;
}

char *fma_onematch_to_scaijson( const lyph *e )
{
  return JSON
  (
    "lyphID": trie_to_json( e->id ),
    "lyphName": e->name ? trie_to_json(e->name) : NULL
  );
}

char *fma_to_scaijson( const fma *f, lyph ***bptr )
{
  lyph *e, **siblings = NULL;
  const char *direction = "down";
  char *retval;
  int distance = 0;

  e = lyph_by_fma_scai( f, direction[0], &distance, &siblings );

  if ( !e )
  {
    direction = "up";
    e = lyph_by_fma_scai( f, direction[0], &distance, &siblings );
  }

  if ( e )
  {
    if ( !siblings )
    {
      CREATE( siblings, lyph *, 2 );
      siblings[0] = e;
      siblings[1] = NULL;
    }

    if ( bptr )
    {
      lyph **sptr;

      for ( sptr = siblings; *sptr; sptr++ )
      {
        **bptr = *sptr;
        (*bptr)++;
      }
    }

    retval = JSON
    (
      "foundmatch": "yes",
      "distance": int_to_json( distance ),
      "direction": distance ? char_to_json( direction[0] ) : js_suppress,
      "lyphs": JS_ARRAY( fma_onematch_to_scaijson, siblings )
    );

    free( siblings );
    return retval;
  }
  else
    return JSON1( "foundmatch": "no" );
}

HANDLER( do_scaimap )
{
  fma **fmas, **fptr;
  char *fmastr, *err;

  TRY_PARAM( fmastr, "fmas", "You did not specify a list of fma terms" );

  fmastr = trim_spaces( fmastr );

  if ( *fmastr == '[' && fmastr[strlen(fmastr)-1] == ']' )
  {
    fmastr[strlen(fmastr)-1] = '\0';
    fmastr++;
  }

  fmas = (fma **)PARSE_LIST( fmastr, fma_by_str, "fma", &err );

  if ( !fmas )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated FMA IDs was not found in the FMA" );
  }

  if ( get_param( params, "pipe" ) )
  {
    lyph *root, **buf, **bptr;
    char *rootstr = get_param( params, "root" );

    if ( !rootstr )
    {
      free( fmas );
      HND_ERR( "You did not indicate a 'root' lyph ID" );
    }

    root = lyph_by_id( rootstr );

    if ( !root )
    {
      free( fmas );
      HND_ERR( "The indicated 'root' lyph was not recognized" );
    }

    CREATE( buf, lyph *, lyphcnt+1 );
    bptr = buf;

    for ( fptr = fmas; *fptr; fptr++ )
      fma_to_scaijson( *fptr, &bptr );

    *bptr = NULL;
    free( fmas );

    between_worker( root, buf, req, 1 );
  }
  else
  {
    send_response( req, JSON1
    (
      "results": JS_ARRAY_R( fma_to_scaijson, fmas, NULL )
    ));

    free( fmas );
  }
}
