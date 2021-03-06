/*
 *  meta.c
 *  Logic and API Commands related to various metadata structures, e.g.,
 *  correlations, clinical indices, etc.
 */
#include "lyph.h"
#include "srv.h"
#include "nt_parse.h"

clinical_index *first_clinical_index;
clinical_index *last_clinical_index;
pubmed *first_pubmed;
pubmed *last_pubmed;
correlation *first_correlation;
correlation *last_correlation;
located_measure *first_located_measure;
located_measure *last_located_measure;
trie *human_species_uppercase;
trie *human_species_lowercase;
bop *first_bop;
bop *last_bop;

clinical_index *clinical_index_by_trie_or_create( trie *ind_tr, pubmed *pubmed );
located_measure *make_located_measure( char *qualstr, lyph *e, int should_save );
char *correlation_jsons_by_located_measure( const located_measure *m );
char *next_clindex_index( void );
void remove_clindex_from_array( clinical_index *ci, clinical_index ***arr );
int remove_located_measure_from_bops( const located_measure *m );
int clindex_correlation_count( const clinical_index *ci );

char *lyph_to_json_id( lyph *e )
{
  return trie_to_json( e->id );
}

char *lyph_annot_obj_to_json( lyph_annot *a )
{
  return trie_to_json( a->obj );
}

void load_lyph_annotations(void)
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( LYPH_ANNOTS_FILE, "r" );

  if ( !fp )
  {
    log_string( "Could not open " LYPH_ANNOTS_FILE " for reading, so no lyph_annotations have been loaded" );
    return;
  }

  for ( ; ; )
  {
    lyph *e;
    pubmed *pubmed;
    trie *pred_tr, *obj_tr;
    char subj_enc[MAX_STRING_LEN], pred_enc[MAX_STRING_LEN], obj_enc[MAX_STRING_LEN], pubmed_enc[MAX_STRING_LEN], *subj, *pred, *obj, *pubmed_ch;
    int sscanf_result;

    QUICK_GETLINE( buf, bptr, c, fp );

    if ( !bptr )
      break;

    line++;

    if ( !str_begins( buf, "Annot " ) )
    {
      error_messagef( "Bad line in " LYPH_ANNOTS_FILE ": %d", line );
      EXIT();
    }

    sscanf_result = sscanf( &buf[strlen("Annot ")], "%s %s %s %s", subj_enc, pred_enc, obj_enc, pubmed_enc );

    if ( sscanf_result != 4 )
    {
      error_messagef( "In " LYPH_ANNOTS_FILE ":%d, the line (%s) has an unrecognized format (%d)", line, &buf[strlen("Annot ")], sscanf_result );
      EXIT();
    }

    subj = url_decode( subj_enc );
    pred = url_decode( pred_enc );
    obj = url_decode( obj_enc );
    pubmed_ch = url_decode( pubmed_enc );

    e = lyph_by_id( subj );

    if ( !e )
    {
      error_messagef( "In " LYPH_ANNOTS_FILE ":%d, lyph '%s' is unrecognized", line, subj );
      EXIT();
    }

    if ( !strcmp( pred, "none" ) )
      pred_tr = NULL;
    else
      pred_tr = trie_strdup( pred, metadata );

    obj_tr = trie_strdup( obj, metadata );

    if ( !strcmp( pubmed_ch, "none" ) )
      pubmed = NULL;
    else
    {
      pubmed = pubmed_by_id( pubmed_ch );

      if ( !pubmed )
      {
        error_messagef( "In " LYPH_ANNOTS_FILE ":%d, pubmed ID '%s' is unrecognized", line, pubmed_ch );
        EXIT();
      }
    }

    MULTIFREE( subj, pred, obj, pubmed_ch );

    annotate_lyph( e, pred_tr, obj_tr, pubmed );
  }

  fclose( fp );
}

int annotate_lyph( lyph *e, trie *pred, trie *obj, pubmed *pubmed )
{
  lyph_annot **aptr, *a, **buf;
  int size;

  for ( aptr = e->annots; *aptr; aptr++ )
    if ( (*aptr)->pred == pred && (*aptr)->obj == obj )
      return 0;

  CREATE( a, lyph_annot, 1 );
  a->pred = pred;
  a->obj = obj;
  a->pubmed = pubmed;

  size = VOIDLEN( e->annots );

  CREATE( buf, lyph_annot *, size + 2 );

  memcpy( buf, e->annots, (size + 1) * sizeof(lyph_annot *) );

  buf[size] = a;
  buf[size+1] = NULL;

  free( e->annots );
  e->annots = buf;

  return 1;
}

HANDLER( do_annotate )
{
  lyph **lyphs, **lptr;
  trie *pred, *obj;
  pubmed *pubmed;
  char *lyphstr, *annotstr, *predstr, *pubmedstr, *err;
  int fMatch = 0;

  TRY_TWO_PARAMS( lyphstr, "lyphs", "lyph", "You did not specify which lyphs to annotate" );

  TRY_PARAM( annotstr, "annot", "You did not specify (using the 'annot' parameter) what to annotate the lyphs by" );
  TRY_PARAM( pubmedstr, "pubmed", "You did not specify (using the 'pubmed' parameter) which pubmed ID" );

  if ( !*annotstr )
    HND_ERR( "The 'annot' field cannot be left blank" );

  if ( !*pubmedstr )
    HND_ERR( "The 'pubmed' field cannot be left blank" );

  predstr = get_param( params, "pred" );

  if ( predstr && !*predstr )
    HND_ERR( "The 'pred' field cannot be blanked" );

  lyphs = (lyph**) PARSE_LIST( lyphstr, lyph_by_id, "lyph", &err );

  if ( !lyphs )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs could not be found in the database" );
  }

  obj = trie_strdup( annotstr, metadata );

  if ( predstr )
    pred = trie_strdup( predstr, metadata );
  else
    pred = NULL;

  pubmed = pubmed_by_id_or_create( pubmedstr, NULL );

  for ( lptr = lyphs; *lptr; lptr++ )
    fMatch |= annotate_lyph( *lptr, pred, obj, pubmed );

  if ( fMatch )
    save_lyph_annotations();

  send_ok( req );
}

void save_lyph_annotations_one_lyph( FILE *fp, lyph *e )
{
  if ( *e->annots )
  {
    lyph_annot **a;

    for ( a = e->annots; *a; a++ )
    {
      char *subj = url_encode( trie_to_static( e->id ) );
      char *pred;
      char *obj = url_encode( trie_to_static( (*a)->obj ) );
      char *pubmed;

      if ( (*a)->pred )
        pred = url_encode( trie_to_static( (*a)->pred ) );
      else
        pred = url_encode( "none" );

      if ( (*a)->pubmed )
        pubmed = url_encode( (*a)->pubmed->id );
      else
        pubmed = url_encode( "none" );

      fprintf( fp, "Annot %s %s %s %s\n", subj, pred, obj, pubmed );

      MULTIFREE( subj, pred, obj, pubmed );
    }
  }
}

void save_lyph_annotations( void )
{
  FILE *fp;
  lyph *e;

  if ( configs.readonly )
    return;

  fp = fopen( LYPH_ANNOTS_FILE, "w" );

  if ( !fp )
  {
    error_message( "Could not open " LYPH_ANNOTS_FILE " for writing" );
    return;
  }

  for ( e = first_lyph; e; e = e->next )
    save_lyph_annotations_one_lyph( fp, e );

  fclose(fp);

  return;
}

pubmed *pubmed_by_id( const char *id )
{
  pubmed *p;

  for ( p = first_pubmed; p; p = p->next )
    if ( !strcmp( p->id, id ) || !strcmp( p->title, id ) )
      return p;

  return NULL;
}

pubmed *pubmed_by_id_or_create( const char *id, int *callersaves )
{
  pubmed *p = pubmed_by_id( id );

  if ( p )
    return p;

  CREATE( p, pubmed, 1 );
  p->id = strdup( id );
  p->title = strdup( id );
  LINK( p, first_pubmed, last_pubmed, next );

  if ( callersaves )
    *callersaves = 1;
  else
    save_pubmeds();

  return p;
}

void save_one_pubmed( FILE *fp, pubmed *p )
{
  char *id = url_encode( p->id );
  char *title = url_encode( p->title );

  fprintf( fp, "%s %s\n", id, title );

  free( id );
  free( title );
}

void save_pubmeds_deprecated( void )
{
  pubmed *p;
  FILE *fp;

  if ( configs.readonly )
    return;

  fp = fopen( PUBMED_FILE, "w" );

  if ( !fp )
  {
    error_messagef( "Could not open " PUBMED_FILE " for writing" );
    EXIT();
  }

  for ( p = first_pubmed; p; p = p->next )
    save_one_pubmed( fp, p );

  fclose( fp );
}

void load_pubmeds_deprecated( void )
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( PUBMED_FILE_DEPRECATED, "r" );

  if ( !fp )
  {
    log_string( "Could not open " PUBMED_FILE_DEPRECATED " for reading -- no pubmeds loaded" );
    return;
  }

  for ( ; ; )
  {
    pubmed *p;
    char id_enc[MAX_STRING_LEN], title_enc[MAX_STRING_LEN];
    int sscanf_results;

    QUICK_GETLINE( buf, bptr, c, fp );

    if ( !bptr )
    {
      fclose(fp);
      return;
    }

    line++;
    sscanf_results = sscanf( buf, "%s %s", id_enc, title_enc );

    if ( sscanf_results != 2 )
    {
      error_messagef( PUBMED_FILE ":%d:  Line has unrecognized format", line );
      EXIT();
    }

    CREATE( p, pubmed, 1 );
    p->id = url_decode( id_enc );
    p->title = url_decode( title_enc );

    LINK( p, first_pubmed, last_pubmed, next );
  }
}

void save_one_clinical_index( FILE *fp, clinical_index *c )
{
  pubmed **pubmeds;
  char *index = url_encode( trie_to_static( c->index ) );
  char *label = url_encode( trie_to_static( c->label ) );

  fprintf( fp, "%s %s ", index, label );

  if ( !*c->pubmeds )
    fprintf( fp, "none" );
  else
  {
    int fFirst = 0;

    for ( pubmeds = c->pubmeds; *pubmeds; pubmeds++ )
    {
      char *pubmed = url_encode( (*pubmeds)->id );

      if ( fFirst )
        fprintf( fp, "," );
      else
        fFirst = 1;

      fprintf( fp, "%s", pubmed );
      free( pubmed );
    }
  }

  fprintf( fp, "\n" );

  free( index );
  free( label );
}

void save_pubmeds( void )
{
  FILE *fp;
  pubmed *p;
  int fFirst = 0;

  if ( configs.readonly )
    return;

  fp = fopen( PUBMED_FILE, "w" );

  if ( !fp )
  {
    error_messagef( "Could not open %s for writing", PUBMED_FILE );
    EXIT();
  }

  fprintf( fp, "[" );

  for ( p = first_pubmed; p; p = p->next )
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", pubmed_to_json_full( p ) );
  }

  fprintf( fp, "]" );
  fclose( fp );
}

void fprintf_one_clinical_index( FILE *fp, clinical_index *ci, int *fFirst )
{
  clinical_index **pptr;

  if ( ci->flags == 1 )
    return;

  ci->flags = 1;

  for ( pptr = ci->parents; *pptr; pptr++ )
    fprintf_one_clinical_index( fp, *pptr, fFirst );

  if ( *fFirst )
    fprintf( fp, "," );
  else
    *fFirst = 1;

  fprintf( fp, "%s", clinical_index_to_json_full( ci ) );
}

void save_clinical_indices( void )
{
  FILE *fp;
  clinical_index *c;
  int fFirst = 0;

  if ( configs.readonly )
    return;

  fp = fopen( CLINICAL_INDEX_FILE, "w" );

  if ( !fp )
  {
    error_messagef( "Could not open " CLINICAL_INDEX_FILE_DEPRECATED " for writing" );
    EXIT();
  }

  fprintf( fp, "[" );

  for ( c = first_clinical_index; c; c = c->next )
    fprintf_one_clinical_index( fp, c, &fFirst );

  for ( c = first_clinical_index; c; c = c->next )
    c->flags = 0;

  fprintf( fp, "]" );
  fclose( fp );
}

void save_clinical_indices_deprecated( void )
{
  FILE *fp;
  clinical_index *c;

  if ( configs.readonly )
    return;

  fp = fopen( CLINICAL_INDEX_FILE_DEPRECATED, "w" );

  if ( !fp )
  {
    error_messagef( "Could not open " CLINICAL_INDEX_FILE_DEPRECATED " for writing" );
    EXIT();
  }

  for ( c = first_clinical_index; c; c = c->next )
    save_one_clinical_index( fp, c );

  fclose( fp );
}

void load_clinical_indices_deprecated( void )
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( CLINICAL_INDEX_FILE_DEPRECATED, "r" );

  if ( !fp )
  {
    error_messagef( "Couldn't open " CLINICAL_INDEX_FILE_DEPRECATED " for reading -- no clinical indices loaded" );
    return;
  }

  for ( ; ; )
  {
    clinical_index *ci;
    pubmed **pubmeds;
    trie *index_tr, *label_tr;
    char index_enc[MAX_STRING_LEN], *index;
    char label_enc[MAX_STRING_LEN], *label;
    char pubmeds_enc[MAX_STRING_LEN], *pubmedsstr;
    char *err;
    int sscanf_results;

    QUICK_GETLINE( buf, bptr, c, fp );

    if ( !bptr )
      break;

    line++;

    sscanf_results = sscanf( buf, "%s %s %s", index_enc, label_enc, pubmeds_enc );

    if ( sscanf_results != 3 )
    {
      error_messagef( CLINICAL_INDEX_FILE_DEPRECATED ":%d: Unrecognized format", line );
      EXIT();
    }

    index = url_decode( index_enc );
    label = url_decode( label_enc );
    index_tr = trie_strdup( index, metadata );
    label_tr = trie_strdup( label, metadata );

    pubmedsstr = url_decode( pubmeds_enc );

    if ( !strcmp( pubmedsstr, "none" ) )
      pubmeds = (pubmed**)blank_void_array();
    else
    {
      pubmeds = (pubmed **) PARSE_LIST( pubmedsstr, pubmed_by_id, "pubmed", &err );

      if ( !pubmeds )
      {
        error_messagef( CLINICAL_INDEX_FILE_DEPRECATED ":%d: %s", line, err ? err : "Could not parse pubmeds list" );
        EXIT();
      }
    }

    MULTIFREE( index, label, pubmedsstr );

    CREATE( ci, clinical_index, 1 );
    ci->index = index_tr;
    ci->label = label_tr;
    ci->pubmeds = pubmeds;
    ci->claimed = NULL;
    ci->parents = (clinical_index**)blank_void_array();
    ci->children = (clinical_index**)blank_void_array();
    ci->flags = 0;
    LINK( ci, first_clinical_index, last_clinical_index, next );
  }

  fclose( fp );
}

void load_correlations( void )
{
  char *js = load_file( CORRELATION_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading -- no correlations loaded", CORRELATION_FILE );
    return;
  }

  correlations_from_js( js );

  save_pubmeds();

  free( js );
}

void load_located_measures( void )
{
  char *js = load_file( LOCATED_MEASURE_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading -- no located measures loaded", LOCATED_MEASURE_FILE );
    return;
  }

  located_measures_from_js( js );

  free( js );
}

void load_bops( void )
{
  char *js = load_file( BOPS_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading --- no bops loaded", BOPS_FILE );
    return;
  }

  bops_from_js( js );

  free( js );
}

void load_pubmeds( void )
{
  char *js = load_file( PUBMED_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading, attempting to read %s instead", PUBMED_FILE, PUBMED_FILE_DEPRECATED );
    load_pubmeds_deprecated();
    return;
  }

  pubmeds_from_js( js );

  free( js );
}

void load_clinical_indices( void )
{
  char *js = load_file( CLINICAL_INDEX_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading, attempting to read %s instead", CLINICAL_INDEX_FILE, CLINICAL_INDEX_FILE_DEPRECATED );
    load_clinical_indices_deprecated();
    return;
  }

  clinical_indices_from_js( js );

  free( js );
}

HANDLER( do_make_pubmed )
{
  pubmed *p;
  char *id, *title;

  TRY_PARAM( id, "id", "You did not specify an 'id' for the pubmed entry" );
  TRY_PARAM( title, "title", "You did not specify a 'title' for the pubmed entry" );

  p = pubmed_by_id( id );

  if ( !p )
    p = pubmed_by_id( title );

  if ( p )
  {
    if ( strcmp( p->title, title ) )
      HND_ERR( "There is already a pubmed entry with that ID or title." );
    else
    {
      send_response( req, pubmed_to_json_full( p ) );
      return;
    }
  }

  CREATE( p, pubmed, 1 );
  p->id = strdup( id );
  p->title = strdup( title );
  LINK( p, first_pubmed, last_pubmed, next );

  save_pubmeds();

  send_response( req, pubmed_to_json_full( p ) );
}

char *pubmed_to_json_full( pubmed *p )
{
  return JSON
  (
    "id": p->id,
    "title": p->title
  );
}

char *pubmed_to_json_brief( pubmed *p )
{
  if ( p )
    return str_to_json( p->id );
  else
    return NULL;
}

HANDLER( do_make_clinical_index )
{
  clinical_index *ci, **parents, **pptr;
  pubmed **pubmeds;
  char *label, *pubmedsstr, *index, *parentstr;

  TRY_PARAM( label, "label", "You did not specify a 'label' for this clinical index" );

  pubmedsstr = get_param( params, "pubmeds" );

  if ( pubmedsstr )
  {
    char *err;
    int save = 0;

    pubmeds = (pubmed **)PARSE_LIST_R( pubmedsstr, pubmed_by_id_or_create, &save, "pubmed", &err );

    if ( save )
      save_pubmeds();

    if ( !pubmeds )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated pubmed IDs was not recognized" );
    }
  }
  else
    pubmeds = (pubmed**)blank_void_array();

  parentstr = get_param( params, "parents" );

  if ( parentstr && *parentstr )
  {
    char *err = NULL;

    parents = (clinical_index **) PARSE_LIST( parentstr, clinical_index_by_index, "clinical index", &err );

    if ( !parents )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated clinical indices was unrecognized" );
    }
  }
  else
    parents = (clinical_index**)blank_void_array();

  index = next_clindex_index();

  CREATE( ci, clinical_index, 1 );
  ci->index = trie_strdup( index, metadata );
  ci->label = trie_strdup( label, metadata );
  ci->pubmeds = pubmeds;
  ci->claimed = NULL;
  ci->children = (clinical_index**)blank_void_array();
  ci->parents = parents;
  ci->flags = 0;
  LINK( ci, first_clinical_index, last_clinical_index, next );

  for ( pptr = parents; *pptr; pptr++ )
    add_clinical_index_to_array( ci, &((*pptr)->children) );

  save_clinical_indices();

  send_response( req, clinical_index_to_json_full( ci ) );
}

char *clinical_index_to_json_full( const clinical_index *ci )
{
  return JSON
  (
    "index": trie_to_json( ci->index ),
    "label": trie_to_json( ci->label ),
    "pubmeds": JS_ARRAY( pubmed_to_json_brief, ci->pubmeds ),
    "claimed": ci->claimed ? ci->claimed : js_suppress,
    "parents": JS_ARRAY( clinical_index_to_json_brief, ci->parents ),
    "children": JS_ARRAY( clinical_index_to_json_brief, ci->children ),
    "correlation count": int_to_json( clindex_correlation_count( ci ) )
  );
}

char *clinical_index_to_json_brief( const clinical_index *ci )
{
  return trie_to_json( ci->index );
}

clinical_index *clinical_index_by_index_or_create( const char *ind )
{
  clinical_index *ci = clinical_index_by_index( ind );

  if ( ci ) 
    return ci;

  CREATE( ci, clinical_index, 1 );

  ci->index = trie_strdup( ind, metadata );
  ci->label = ci->index;
  ci->pubmeds = (pubmed**)blank_void_array();
  ci->claimed = NULL;
  ci->children = (clinical_index**)blank_void_array();
  ci->parents = (clinical_index**)blank_void_array();
  ci->flags = 0;

  LINK( ci, first_clinical_index, last_clinical_index, next );

  save_clinical_indices();

  return ci;
}

clinical_index *clinical_index_by_index( const char *ind )
{
  trie *ind_tr;

  ind_tr = trie_search( ind, metadata );

  if ( !ind_tr )
    return NULL;

  return clinical_index_by_trie( ind_tr );
}

clinical_index *clinical_index_by_trie_or_create( trie *ind_tr, pubmed *pbmd )
{
  clinical_index *ci = clinical_index_by_trie( ind_tr );

  if ( ci )
    return ci;

  CREATE( ci, clinical_index, 1 );
  ci->index = ind_tr;
  ci->label = ind_tr;
  ci->parents = (clinical_index**)blank_void_array();
  ci->children = (clinical_index**)blank_void_array();
  ci->flags = 0;
  CREATE( ci->pubmeds, pubmed *, 2 );
  ci->pubmeds[0] = pbmd;
  ci->pubmeds[1] = NULL;
  ci->claimed = NULL;
  LINK( ci, first_clinical_index, last_clinical_index, next );

  save_clinical_indices();

  return ci;
}

clinical_index *clinical_index_by_trie( trie *ind_tr )
{
  clinical_index *ci;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    if ( ci->index == ind_tr )
      return ci;

  return NULL;
}

int clindex_has_descendant( clinical_index *anc, clinical_index *des )
{
  clinical_index **cptr;

  for ( cptr = anc->children; *cptr; cptr++ )
    if ( *cptr == des )
       return 1;

  for ( cptr = anc->children; *cptr; cptr++ )
    if ( clindex_has_descendant( *cptr, des ) )
      return 1;

  return 0;
}

HANDLER( do_edit_clinical_index )
{
  clinical_index *ci, **parents, **pptr;
  pubmed **pubmeds;
  char *indexstr, *label, *pubmedsstr, *claimedstr, *parentstr;

  TRY_PARAM( indexstr, "index", "You did not specify which clinical index ('index') to edit" );

  ci = clinical_index_by_index( indexstr );

  if ( !ci )
    HND_ERR( "The indicated clinical index was not recognized" );

  label = get_param( params, "label" );

  pubmedsstr = get_param( params, "pubmeds" );
  claimedstr = get_param( params, "claimed" );
  parentstr = get_param( params, "parents" );

  if ( (!label || !*label) && !pubmedsstr && !claimedstr && !parentstr )
    HND_ERR( "You did not specify any changes to make" );

  if ( parentstr && *parentstr )
  {
    char *err;

    parents = (clinical_index**)PARSE_LIST( parentstr, clinical_index_by_index, "clinical index", &err );

    if ( !parents )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated parent clinical indices was not recognized" );
    }

    for ( pptr = parents; *pptr; pptr++ )
    {
      if ( clindex_has_descendant( ci, *pptr ) )
      {
        free( parents );
        HND_ERR( "The indicated parents would induce a loop in the clinical index hierarchy" );
      }
    }
  }
  else if ( parentstr && !*parentstr )
    parents = (clinical_index**)blank_void_array();
  else
    parents = NULL;

  if ( pubmedsstr )
  {
    char *err;
    int save = 0;

    pubmeds = (pubmed **) PARSE_LIST_R( pubmedsstr, pubmed_by_id_or_create, &save, "pubmed", &err );

    if ( save )
      save_pubmeds();

    if ( !pubmeds )
    {
      if ( parents )
        free( parents );

      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the indicated pubmeds was not recognized" );
    }

    ci->pubmeds = pubmeds;
  }

  if ( parents )
  {
    for ( pptr = ci->parents; *pptr; pptr++ )
      remove_clindex_from_array( ci, &((*pptr)->children) );

    for ( pptr = parents; *pptr; pptr++ )
      add_clinical_index_to_array( ci, &((*pptr)->children) );
  }

  if ( label && *label )
    ci->label = trie_strdup( label, metadata );

  if ( parentstr )
  {
    free( ci->parents );
    ci->parents = parents;
  }

  if ( claimedstr )
  {
    if ( ci->claimed )
      free( ci->claimed );

    ci->claimed = strdup( claimedstr );
  }

  save_clinical_indices();

  send_response( req, clinical_index_to_json_full( ci ) );
}

HANDLER( do_edit_pubmed )
{
  pubmed *pubmed;
  char *pubmedstr, *title;

  TRY_PARAM( pubmedstr, "pubmed", "You did not specify which pubmed to edit" );

  pubmed = pubmed_by_id( pubmedstr );

  if ( !pubmed )
    HND_ERR( "The indicated pubmed entry was not recognized." );

  TRY_PARAM( title, "title", "You did not specify a new 'title' for the pubmed" );

  free( pubmed->title );
  pubmed->title = strdup( title );

  save_pubmeds();

  send_response( req, pubmed_to_json_full( pubmed ) );
}

HANDLER( do_pubmed )
{
  pubmed *pubmed;
  char *pubmedstr;

  TRY_PARAM( pubmedstr, "pubmed", "You did not specify which pubmed to view" );

  pubmed = pubmed_by_id( pubmedstr );

  if ( !pubmed )
    HND_ERR( "The indicated pubmed was not recognized" );

  send_response( req, pubmed_to_json_full( pubmed ) );
}

HANDLER( do_clinical_index )
{
  clinical_index *ci;
  char *cistr;

  TRY_PARAM( cistr, "index", "You did not specify the 'index' of the clinical index you want to view" );

  ci = clinical_index_by_index( cistr );

  if ( !ci )
    HND_ERR( "The indicated clinical index was not recognized" );

  send_response( req, clinical_index_to_json_full( ci ) );
}

HANDLER( do_all_clinical_indices )
{
  clinical_index **buf, **bptr, *ci;
  int cnt = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  CREATE( buf, clinical_index *, cnt + 1 );

  for ( bptr = buf, ci = first_clinical_index; ci; ci = ci->next )
    *bptr++ = ci;

  *bptr = NULL;

  send_response( req, JSON1
  (
    "results": JS_ARRAY( clinical_index_to_json_full, buf )
  ) );

  free( buf );
}

HANDLER( do_all_pubmeds )
{
  pubmed **buf, **bptr, *p;
  char *retval;
  int cnt = 0;

  for ( p = first_pubmed; p; p = p->next )
    cnt++;

  CREATE( buf, pubmed *, cnt + 1 );

  for ( bptr = buf, p = first_pubmed; p; p = p->next )
    *bptr++ = p;

  *bptr = NULL;

  retval = JSON1
  (
    "results": JS_ARRAY( pubmed_to_json_full, buf )
  );

  free( buf );

  send_response( req, retval );
}

int has_some_clinical_index( lyph *e, clinical_index **cis )
{
  lyph_annot **a;

  for ( a = e->annots; *a; a++ )
  {
    clinical_index **cptr;

    if ( (*a)->pred )
      continue;

    for ( cptr = cis; *cptr; cptr++ )
      if ( (*cptr)->index == (*a)->obj )
        return 1;
  }

  return 0;
}

int has_all_clinical_indices( lyph *e, clinical_index **cis )
{
  clinical_index **cptr;

  for ( cptr = cis; *cptr; cptr++ )
  {
    lyph_annot **a;

    for ( a = e->annots; *a; a++ )
      if ( !(*a)->pred && (*a)->obj == (*cptr)->index )
        break;

    if ( !*a )
      return 0;
  }

  return 1;
}

void calc_lyphs_with_indices( lyph ***bptr, clinical_index **cis, int type )
{
  lyph *e;

  for ( e = first_lyph; e; e = e->next )
  {
    if ( ( type == CLINICAL_INDEX_SEARCH_UNION && has_some_clinical_index( e, cis ) )
    ||   ( type == CLINICAL_INDEX_SEARCH_IX && has_all_clinical_indices( e, cis ) ) )
    {
      (**bptr) = e;
      (*bptr)++;
    }
  }
}

HANDLER( do_has_clinical_index )
{
  clinical_index **cis;
  lyph **buf, **bptr;
  lyph_to_json_details details;
  char *cistr, *typestr, *err;
  int type;

  typestr = get_param( params, "type" );

  if ( typestr )
  {
    if ( !strcmp( typestr, "union" ) )
      type = CLINICAL_INDEX_SEARCH_UNION;
    else if ( !strcmp( typestr, "intersection" ) )
      type = CLINICAL_INDEX_SEARCH_IX;
    else
      HND_ERR( "'type' must be either 'union' or 'intersection'" );
  }
  else
    type = CLINICAL_INDEX_SEARCH_UNION;

  TRY_TWO_PARAMS( cistr, "index", "indices", "You did not specify a list of 'indices'" );

  cis = (clinical_index **)PARSE_LIST( cistr, clinical_index_by_index, "clinical index", &err );

  if ( !cis )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated clinical indices was unrecognized" );
  }

  CREATE( buf, lyph *, lyphcnt + 1 );
  bptr = buf;

  calc_lyphs_with_indices( &bptr, cis, type );

  free( cis );

  *bptr = NULL;

  details.show_annots = 1;
  details.suppress_correlations = 1;
  details.count_correlations = 0;
  details.buf = buf;
  details.show_children = 0;

  send_response( req, JS_ARRAY_R( lyph_to_json_r, buf, &details ) );

  free( buf );
}

HANDLER( do_remove_annotation )
{
  lyph **lyphs, **eptr;
  trie *obj;
  char *lyphsstr, *annotstr, *err;
  int fMatch;

  TRY_TWO_PARAMS( lyphsstr, "lyphs", "lyph", "You did not specify which lyphs to remove the annotations from" );
  TRY_PARAM( annotstr, "annot", "You did not specify an annotation to remove" );

  lyphs = (lyph**)PARSE_LIST( lyphsstr, lyph_by_id, "lyph", &err );

  if ( !lyphs )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs could not be recognized" );
  }

  if ( !strcmp( annotstr, "all" ) )
  {
    for ( eptr = lyphs; *eptr; eptr++ )
    {
      lyph_annot **a;

      for ( a = (*eptr)->annots; *a; a++ )
        free( *a );

      free( (*eptr)->annots );
      (*eptr)->annots = (lyph_annot**)blank_void_array();
    }

    save_lyph_annotations();
    send_ok( req );
    return;
  }

  obj = trie_strdup( annotstr, metadata );
  fMatch = 0;

  for ( eptr = lyphs; *eptr; eptr++ )
  {
    lyph *e = *eptr;
    lyph_annot **a;
    int matches = 0;

    for ( a = e->annots; *a; a++ )
    {
      if ( (*a)->obj == obj )
        matches++;
    }

    if ( matches )
    {
      lyph_annot **buf, **bptr;

      fMatch = 1;
      CREATE( buf, lyph_annot *, VOIDLEN( e->annots ) - matches + 1 );
      bptr = buf;

      for ( a = e->annots; *a; a++ )
      {
        if ( (*a)->obj != obj )
          *bptr++ = *a;
        else
          free( *a );
      }

      *bptr = NULL;
      free( e->annots );
      e->annots = buf;
    }
  }

  if ( *lyphs && !lyphs[1] && !fMatch )
    HND_ERR( "The indicated lyph does not have the indicated annotation" );

  if ( fMatch )
    save_lyph_annotations();

  send_ok( req );
}

char *variable_to_json( variable *v )
{
  if ( v->type == VARIABLE_CLINDEX )
    return JSON
    (
      "type": "clinical index",
      "clindex": trie_to_json( v->ci->index ),
      "clindex label": trie_to_json( v->ci->label )
    );
  else
    return JSON
    (
      "type": "located measure",
      "quality": v->quality,
      "location": trie_to_json( v->loc->id ),
      "location name": v->loc->name ? trie_to_json( v->loc->name ) : NULL
    );
}

char *correlation_to_json( correlation *c )
{
  return JSON
  (
    "id": int_to_json( c->id ),
    "variables": JS_ARRAY( variable_to_json, c->vars ),
    "pubmed": pubmed_to_json_full( c->pbmd ),
    "comment": c->comment
  );
}

HANDLER( do_correlation )
{
  correlation *c = correlation_by_id( request );

  if ( !c )
  {
    if ( !*request )
      HND_ERR( "You did not indicate which correlation to look up" );
    else
      HND_ERR( "The indicated correlation was not recognized" );
  }

  send_response( req, correlation_to_json( c ) );

  return;
}

correlation *correlation_by_id( const char *id )
{
  correlation *c;
  int id_as_int;

  id_as_int = strtoul( id, NULL, 10 );

  if ( id_as_int == -1 )
    return NULL;

  for ( c = first_correlation; c; c = c->next )
    if ( c->id == id_as_int )
      return c;

  return NULL;
}

void bars_to_commas( char *str )
{
  char *ptr;

  for ( ptr = str; *ptr; ptr++ )
    if ( *ptr == '|' )
      *ptr = ',';
}

variable *parse_one_variable( const char *vstr, variable ***tobuf )
{
  variable *v;
  lyph *e;
  char *of = strstr( vstr, " of " );

  if ( of )
  {
    char *of2;

    /*
     * Use the last "of", to allow for quantities which themselves contain " of ",
     * e.g., "volume of blood of 5"
     */
    do
    {
      of2 = strstr( of + 1, " of " );

      if ( of2 )
        of = of2;
    }
    while( of2 );
  }
  else
  {
    clinical_index *ci;
    const char *space;

    for ( space = vstr; *space; space++ )
      if ( *space == ' ' )
        break;

    if ( *space )
      return NULL;

    ci = clinical_index_by_index( vstr );

    if ( !ci )
    {
      CREATE( v, variable, 1 );
      v->type = VARIABLE_ERROR;
      return v;
    }

    CREATE( v, variable, 1 );
    v->type = VARIABLE_CLINDEX;
    v->ci = ci;
    v->quality = NULL;
    v->loc = NULL;

    if ( tobuf )
    {
      **tobuf = v;
      (*tobuf)++;
    }

    return v;
  }

  if ( of == vstr )
    return NULL;

  if ( of[strlen(" of ")] == '(' )
  {
    /*
     * Special syntax requested by bdb:
     * volume of (1,2,3) = volume of 1, volume of 2, volume of 3
     */
    lyph **buf, **bptr;
    variable *retval = NULL;
    char *end = &of[strlen(of)-1], *listptr = &of[strlen(" of ") + 1];

    if ( !tobuf )
      return NULL;

    while ( *end == ' ' )
      end--;

    if ( *end != ')' )
      return NULL;

    *end = '\0';

    bars_to_commas( listptr );

    buf = (lyph**)PARSE_LIST( listptr, lyph_by_id, "lyph", NULL );
    *end = ')';

    if ( !buf || !*buf )
      return NULL;

    for ( bptr = buf; *bptr; bptr++ )
    {
      CREATE( v, variable, 1 );
      v->type = VARIABLE_LOCATED;
      CREATE( v->quality, char, of - vstr + 1 );
      memcpy( v->quality, vstr, of - vstr );
      v->quality[of-vstr] = '\0';
      v->loc = *bptr;
      v->ci = NULL;
      make_located_measure( v->quality, *bptr, 0 );
      **tobuf = v;
      (*tobuf)++;

      if ( !retval )
        retval = v;
    }

    return retval;
  }

  e = lyph_by_id( of + strlen(" of ") );

  if ( !e )
    return NULL;

  CREATE( v, variable, 1 );
  v->type = VARIABLE_LOCATED;

  CREATE( v->quality, char, of - vstr + 1 );
  memcpy( v->quality, vstr, of - vstr );
  v->quality[of-vstr] = '\0';

  v->loc = e;
  v->ci = NULL;

  make_located_measure( v->quality, e, 0 );

  if ( tobuf )
  {
    **tobuf = v;
    (*tobuf)++;
    return v;
  }
  else
    return v;
}

int preprocess_vars_for_bdb_syntax( char *str )
{
  char *ptr;
  int parens = 0;

  for ( ptr = str; *ptr; ptr++ )
  {
    if ( *ptr == '(' )
    {
      parens++;
      continue;
    }

    if ( *ptr == ')' )
    {
      parens--;

      if ( parens < 0 )
        return 0;
    }

    if ( *ptr == ',' && parens )
      *ptr = '|';
  }

  if ( parens )
    return 0;

  return 1;
}

HANDLER( do_makecorrelation )
{
  correlation *c, *edit;
  variable **vars, **vptr, **tmp;
  char *pubmedstr, *varsstr, *editstr, *commentstr, *err = NULL;
  int yes;

  TRY_PARAM( pubmedstr, "pubmed", "You did not indicate a 'pubmed'" );
  TRY_TWO_PARAMS( varsstr, "vars", "variables", "You did not indicate a list of 'variables'" );

  editstr = get_param( params, "id" );

  if ( editstr )
  {
    edit = correlation_by_id( editstr );

    if ( !edit )
      HND_ERR( "The indicated correlation was not recognized" );
  }
  else
    edit = NULL;

  if ( !preprocess_vars_for_bdb_syntax( varsstr ) )
    HND_ERR( "Improperly formed parentheses detected" );

  CREATE( vars, variable *, strlen( varsstr ) + 1 );
  vptr = vars;

  tmp = (variable**)PARSE_LIST_R( varsstr, parse_one_variable, &vptr, "variable", &err );
  free( tmp );

  if ( err )
  {
    free( vars );
    HND_ERR_FREE( err );
  }

  *vptr = NULL;

  for ( vptr = vars; *vptr; vptr++ )
  {
    if ( (*vptr)->type == VARIABLE_ERROR )
    {
      for ( vptr = vars; *vptr; vptr++ )
      {
        if ( (*vptr)->type == VARIABLE_LOCATED )
          free( (*vptr)->quality );

        free( *vptr );
      }

      free( vars );
      HND_ERR( "One of the indicated clinical indices was not recognized" );
    }
  }

  save_located_measures();

  if ( !vars )
  {
    if ( err )
      free( err );
    HND_ERR( "One of the indicated variables was malformed" );
  }

  if ( !*vars )
  {
    free( vars );
    HND_ERR( "You cannot create a correlation with a blank list of variables" );
  }

  yes = 1;
  CREATE( c, correlation, 1 );
  c->vars = vars;
  c->pbmd = pubmed_by_id_or_create( pubmedstr, &yes );

  commentstr = get_param( params, "comment" );

  if ( commentstr )
    c->comment = strdup( commentstr );
  else
    c->comment = NULL;

  if ( edit )
  {
    /*
     * Intentional memory leak here as we do not anticipate
     * the functionality in question to be used so often that
     * it will have an impact
     */
    INSERT2( c, edit, first_correlation, next, prev );
    UNLINK2( edit, first_correlation, last_correlation, next, prev );
    c->id = edit->id;
  }
  else
  {
    if ( last_correlation )
      c->id = last_correlation->id + 1;
    else
      c->id = 1;

    LINK2( c, first_correlation, last_correlation, next, prev );
  }

  save_correlations();

  send_response( req, correlation_to_json( c ) );
  return;
}

HANDLER( do_all_correlations )
{
  correlation **cbuf, **cbufptr, *c;
  int cnt = 0;

  for ( c = first_correlation; c; c = c->next )
    cnt++;

  CREATE( cbuf, correlation *, cnt + 1 );
  cbufptr = cbuf;

  for ( c = first_correlation; c; c = c->next )
    *cbufptr++ = c;

  *cbufptr = NULL;

  send_response( req, JS_ARRAY( correlation_to_json, cbuf ) );

  free( cbuf );
}

void save_correlations( void )
{
  FILE *fp = fopen( CORRELATION_FILE, "w" );
  correlation *c;
  int fFirst = 0;

  if ( !fp )
  {
    error_messagef( "Could not open %s for writing", CORRELATION_FILE );
    return;
  }

  fprintf( fp, "[" );

  for ( c = first_correlation; c; c = c->next )
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", correlation_to_json( c ) );
  }

  fprintf( fp, "]" );

  fclose(fp);
}

void populate_ontsearch( char *key, trie ***bptr, int *cnt, trie *t )
{
  if ( t->data )
  {
    char *label = trie_to_static( t ), *lptr;

    for ( lptr = label; *lptr; )
    {
      if ( str_begins( lptr, key ) )
      {
        **bptr = t;
        (*bptr)++;
        (*cnt)++;

        break;
      }

      for ( ; *lptr; lptr++ )
        if ( *lptr == ' ' )
          break;

      if ( !*lptr )
        break;
      else
        lptr++;
    }
  }

  TRIE_RECURSE( populate_ontsearch( key, bptr, cnt, *child ) );
}

char *ontsearch_term_to_json( trie *x )
{
  return JSON
  (
    "label": trie_to_json( x ),
    "iri": trie_to_json( x->data[0] )
  );
}

HANDLER( do_ontsearch )
{
  trie **buf, **bptr;
  char *keystr;
  int cnt = 0;

  TRY_PARAM( keystr, "key", "You did not indicate a 'key' to search for" );

  CREATE( buf, trie *, count_nontrivial_members(label_to_iris)*2 + 1);
  bptr = buf;

  populate_ontsearch( keystr, &bptr, &cnt, label_to_iris );
  *bptr = NULL;

  qsort( buf, cnt, sizeof(trie*), cmp_trie_data );
  buf[100] = NULL;

  send_response( req, JS_ARRAY( ontsearch_term_to_json, buf ) );

  free( buf );
}

char *located_measure_to_json( const located_measure *m )
{
  return JSON
  (
    "id": int_to_json( m->id ),
    "quality": m->quality,
    "lyph": trie_to_json( m->loc->id ),
    "lyph name": m->loc->name ? trie_to_json( m->loc->name ) : js_suppress,
    "correlations": correlation_jsons_by_located_measure( m )
  );
}

char *located_measure_to_json_brief( const located_measure *m )
{
  return JSON
  (
    "id": int_to_json( m->id ),
    "quality": m->quality,
    "lyph": trie_to_json( m->loc->id ),
    "lyph name": m->loc->name ? trie_to_json( m->loc->name ) : js_suppress
  );
}

located_measure *located_measure_by_int( int id )
{
  located_measure *m;

  for ( m = first_located_measure; m; m = m->next )
    if ( m->id == id )
      return m;

  return NULL;
}

located_measure *located_measure_by_id( const char *id )
{
  return located_measure_by_int( strtoul( id, NULL, 10 ) );
}

HANDLER( do_all_located_measures )
{
  located_measure **buf, **bptr, *m;
  int cnt = 0;

  for ( m = first_located_measure; m; m = m->next )
    cnt++;

  CREATE( buf, located_measure *, cnt + 1 );
  bptr = buf;

  for ( m = first_located_measure; m; m = m->next )
    *bptr++ = m;

  *bptr = NULL;

  send_response( req, JS_ARRAY( located_measure_to_json, buf ) );

  free( buf );
}

located_measure *located_measure_by_description( const char *qual, const lyph *e )
{
  located_measure *m;

  for ( m = first_located_measure; m; m = m->next )
    if ( !strcmp( m->quality, qual ) && m->loc == e )
      return m;

  return NULL;
}

located_measure *make_located_measure( char *qualstr, lyph *e, int should_save )
{
  located_measure *m;

  m = located_measure_by_description( qualstr, e );

  if ( m )
    return m;

  CREATE( m, located_measure, 1 );
  m->quality = strdup( qualstr );
  m->loc = e;

  if ( last_located_measure )
    m->id = last_located_measure->id + 1;
  else
    m->id = 1;

  LINK2( m, first_located_measure, last_located_measure, next, prev );

  if ( should_save )
    save_located_measures();

  return m;
}

HANDLER( do_located_measure )
{
  located_measure *m;
  int id;

  if ( !*request )
    HND_ERR( "You didn't indicate which located measure to look up" );

  id = strtoul( request, NULL, 10 );

  if ( id < 1 )
    HND_ERR( "The indicated located measure was not recognized" );

  m = located_measure_by_int( id );

  if ( !m )
    HND_ERR( "The indicated located measure was not recognized" );

  send_response( req, located_measure_to_json( m ) );
}

HANDLER( do_make_located_measure )
{
  located_measure *m;
  lyph *e;
  char *qualstr, *lyphstr;

  TRY_PARAM( qualstr, "quality", "You did not indicate a 'quality'" );
  TRY_PARAM( lyphstr, "lyph", "You did not indicate a 'lyph'" );

  e = lyph_by_id( lyphstr );

  if ( !e )
    HND_ERR( "The indicated lyph was not recognized" );

  m = make_located_measure( qualstr, e, 1 );

  send_response( req, located_measure_to_json( m ) );
}

void save_located_measures( void )
{
  FILE *fp = fopen( LOCATED_MEASURE_FILE, "w" );
  located_measure *m;
  int fFirst = 0;

  if ( !fp )
  {
    error_messagef( "Could not open %s for writing", LOCATED_MEASURE_FILE );
    return;
  }

  fprintf( fp, "[" );

  for ( m = first_located_measure; m; m = m->next )
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", located_measure_to_json( m ) );
  }

  fprintf( fp, "]" );

  fclose(fp);
}

HANDLER( do_delete_correlation )
{
  correlation *c;
  char *corrstr;

  TRY_TWO_PARAMS( corrstr, "corr", "correlation", "You did not specify which 'correlation' to delete" );

  c = correlation_by_id( corrstr );

  if ( !c )
    HND_ERR( "The indicated correlation was not recognized" );

  delete_correlation( c );
  save_correlations();

  send_ok( req );
}

void delete_correlation( correlation *c )
{
  variable **v;

  UNLINK2( c, first_correlation, last_correlation, next, prev );

  for ( v = c->vars; *v; v++ )
  {
    if ( (*v)->type == VARIABLE_LOCATED )
      free( (*v)->quality );

    free( *v );
  }

  free( c->vars );
  free( c );
}

HANDLER( do_delete_located_measure )
{
  located_measure *m;
  char *measstr;
  int id;

  TRY_PARAM( measstr, "measure", "You did not specify which measure to delete" );

  id = strtoul( measstr, NULL, 10 );

  m = located_measure_by_int( id );

  if ( !m )
    HND_ERR( "The indicated located measure was not recognized" );

  delete_located_measure( m );
  save_located_measures();

  send_ok( req );
}

void delete_located_measure( located_measure *m )
{
  if ( remove_located_measure_from_bops( m ) )
    save_bops();

  if ( m->quality )
    free( m->quality );

  UNLINK2( m, first_located_measure, last_located_measure, next, prev );

  free( m );
}

void free_all_located_measures( void )
{
  /*
   * Intentional memory leak here because the complexity of avoiding
   * it would not be worth the anticipated rarity of this function
   * being called
   */
  first_located_measure = NULL;
  last_located_measure = NULL;
  save_located_measures();
}

void free_all_correlations( void )
{
  /*
   * Intentional memory leak here because the complexity of avoiding
   * it would not be worth the anticipated rarity of this function
   * being called
   */
  first_correlation = NULL;
  last_correlation = NULL;
  save_correlations();
}

int count_correlations( void )
{
  correlation *c;
  int cnt = 0;

  for ( c = first_correlation; c; c = c->next )
    cnt++;

  return cnt;
}

correlation **correlations_by_located_measure( const located_measure *m )
{
  correlation *c, **buf, **bptr;
  variable **vs, *v;

  CREATE( buf, correlation *, count_correlations() + 1 );
  bptr = buf;

  for ( c = first_correlation; c; c = c->next )
  {
    for ( vs = c->vars; *vs; vs++ )
    {
      v = *vs;

      if ( v->type == VARIABLE_LOCATED
      &&   v->loc == m->loc
      &&  !strcmp( v->quality, m->quality ) )
      {
        *bptr++ = c;
        break;
      }
    }
  }

  *bptr = NULL;

  return buf;  
}

char *correlation_jsons_by_located_measure( const located_measure *m )
{
  correlation **corrs = correlations_by_located_measure( m );
  char *retval = JS_ARRAY( correlation_to_json, corrs );

  free( corrs );
  return retval;
}

correlation **correlations_by_lyph( const lyph *e )
{
  correlation *c, **buf, **bptr;
  variable **vs, *v;
  int cnt = 0;

  for ( c = first_correlation; c; c = c->next )
    cnt++;

  CREATE( buf, correlation *, cnt + 1 );
  bptr = buf;

  for ( c = first_correlation; c; c = c->next )
  {
    for ( vs = c->vars; *vs; vs++ )
    {
      v = *vs;

      if ( v->type == VARIABLE_LOCATED && v->loc == e )
      {
        *bptr++ = c;
        break;
      }
    }
  }

  *bptr = NULL;
  return buf;
}

char *correlation_jsons_by_lyph( const lyph *e )
{
  correlation **corrs = correlations_by_lyph( e );
  char *retval = JS_ARRAY( correlation_to_json, corrs );

  free( corrs );

  return retval;
}

int correlation_count( lyph *e, lyph **children )
{
  lyph **chptr;
  correlation *c;
  variable **vs, *v;
  int cnt = 0;

  for ( chptr = children; *chptr; chptr++ )
    (*chptr)->flags = 1;

  e->flags = 1;

  for ( c = first_correlation; c; c = c->next )
  {
    if ( c->flags == 1 )
      continue;

    for ( vs = c->vars; *vs; vs++ )
    {
      v = *vs;

      if ( v->type == VARIABLE_LOCATED && v->loc->flags == 1 )
      {
        c->flags = 1;
        cnt++;
        break;
      }
    }
  }

  for ( chptr = children; *chptr; chptr++ )
    (*chptr)->flags = 0;

  e->flags = 0;

  for ( c = first_correlation; c; c = c->next )
    c->flags = 0;

  return cnt;
}

lyph *get_random_lyph( void )
{
  lyph *e;
  int cnt = lyphcnt;

  for ( e = first_lyph; e; e = e->next )
  {
    if ( !(rand() % cnt ) )
      return e;
    else
      cnt--;
  }

  return NULL;
}

const char *get_random_quality( void )
{
  switch( rand() % 5 )
  {
    default:
    case 0: return "volume";
    case 1: return "radius";
    case 2: return "surface area";
    case 3: return "length";
    case 4: return "weight";
  }
}

variable *generate_random_clindex_variable( variable **vars, variable **end )
{
  clinical_index *ci;
  variable **vptr, *v;
  int cnt = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  for ( ci = first_clinical_index; ci; ci = ci->next )
  {
    if ( !(rand() % cnt) )
      break;
    else
      cnt--;
  }

  for ( vptr = vars; *vptr; vptr++ )
    if ( (*vptr)->type == VARIABLE_CLINDEX && (*vptr)->ci == ci )
      break;

  if ( *vptr )
    return generate_random_clindex_variable( vars, end );

  CREATE( v, variable, 1 );
  v->type = VARIABLE_CLINDEX;
  v->ci = ci;
  v->loc = NULL;
  v->quality = NULL;

  return v;
}

variable *generate_random_located_variable( variable **vars, variable **end )
{
  lyph *e;
  variable **vptr, *v;
  const char *quality;

  for ( ; ; )
  {
    e = get_random_lyph();
    quality = get_random_quality();

    for ( vptr = vars; vptr < end; vptr++ )
    {
      if ( (*vptr)->type == VARIABLE_LOCATED
      &&   (*vptr)->loc == e
      &&   !strcmp( (*vptr)->quality, quality ) )
        break;
    }

    if ( vptr == end )
      break;
  }

  CREATE( v, variable, 1 );
  v->type = VARIABLE_LOCATED;
  v->loc = e;
  v->quality = strdup( quality );
  v->ci = NULL;

  return v;
}

variable *generate_random_correlation_variable( variable **vars, variable **end )
{
  if ( rand() % 5 )
    return generate_random_located_variable( vars, end );
  else
    return generate_random_clindex_variable( vars, end );
}

void generate_random_correlation( void )
{
  correlation *c;
  variable **vars, **vptr;
  int vcnt = 0;

  CREATE( vars, variable *, 6 );
  vptr = vars;

  for ( ; ; )
  {
    if ( vcnt == 5 || ( vcnt > 1 && !(rand() % 2) ) )
      break;

    *vptr = generate_random_correlation_variable( vars, vptr );
    vptr++;
    vcnt++;
  }

  *vptr = NULL;

  CREATE( c, correlation, 1 );
  c->pbmd = pubmed_by_id_or_create( "autogen", NULL );
  c->vars = vars;
  c->comment = NULL;

  if ( last_correlation )
    c->id = last_correlation->id + 1;
  else
    c->id = 1;

  LINK( c, first_correlation, last_correlation, next );
}

HANDLER( do_gen_random_correlations )
{
  char *cntstr;
  int cnt, i;

  TRY_PARAM( cntstr, "count", "You did not specify (using 'count') how many correlations to generate" );

  cnt = strtoul( cntstr, NULL, 10 );

  if ( cnt < 1 )
    HND_ERR( "'count' must be a positive integer" );

  for ( i = 0; i < cnt; i++ )
    generate_random_correlation();

  save_correlations();

  send_ok( req );
}

int is_null_species( lyph *e )
{
  if ( !e->species )
    return 1;

  if ( !e->species->parent )
    return 1;

  return 0;
}

int is_human_species( lyph *e )
{
  return
  (
    !e->species
    || e->species == human_species_uppercase
    || e->species == human_species_lowercase
  );
}

char *next_clindex_index( void )
{
  static char buf[2048];
  clinical_index *ci;
  int max = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
  {
    char *idstr = trie_to_static( ci->index );
    int id;

    if ( !str_begins( idstr, "CLINDEX_" ) )
      continue;

    id = strtoul( idstr + strlen( "CLINDEX_" ), NULL, 10 );

    if ( id > max )
      max = id;
  }

  sprintf( buf, "CLINDEX_%d", max + 1 );
  return buf;
}

HANDLER( do_stats )
{
  lyph *e;
  fma *f;
  pubmed *p;
  correlation *c;
  variable **v;
  clinical_index *ci;
  int distinct_fmas = 0, hash;
  int correlations_with_some_clindex = 0;
  int correlations_with_no_clindex = 0;
  int lyphs_in_correlations = 0;
  int clindices_in_correlations = 0;
  int pubmeds_in_correlations = 0;
  int distinct_fmas_annoting_lyphs = 0;
  fma *fma_by_str( const char *str );

  for ( e = first_lyph; e; e = e->next )
    e->flags = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    ci->flags = 0;

  for ( p = first_pubmed; p; p = p->next )
    p->flags = 0;

  for ( c = first_correlation; c; c = c->next )
  {
    int fClindex = 0;

    for ( v = c->vars; *v; v++ )
    {
      if ( (*v)->type == VARIABLE_CLINDEX )
      {
        fClindex = 1;
        (*v)->ci->flags = 1;
      }
      else if ( (*v)->type == VARIABLE_LOCATED && (*v)->loc )
        (*v)->loc->flags = 1;
    }

    if ( fClindex )
      correlations_with_some_clindex++;
    else
      correlations_with_no_clindex++;

    if ( c->pbmd )
      c->pbmd->flags = 1;
  }

  for ( ci = first_clinical_index; ci; ci = ci->next )
  {
    if ( ci->flags )
    {
      ci->flags = 0;
      clindices_in_correlations++;
    }
  }

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->flags )
    {
      e->flags = 0;
      lyphs_in_correlations++;
    }
  }

  for ( p = first_pubmed; p; p = p->next )
  {
    if ( p->flags )
    {
      p->flags = 0;
      pubmeds_in_correlations++;
    }
  }

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->fma )
    {
      f = fma_by_trie( e->fma );

      if ( f && !f->flags )
      {
        f->flags = 1;
        distinct_fmas++;
      }
    }
  }

  ITERATE_FMAS( f->flags = 0 );

  for ( e = first_lyph; e; e = e->next )
  {
    if ( e->fma )
    {
      char *id = trie_to_static( e->fma );

      f = fma_by_str( id );
      if ( f && !f->flags )
      {
        f->flags++;
        distinct_fmas_annoting_lyphs++;
      }
    }
  }

  ITERATE_FMAS( f->flags = 0 );

  send_response( req, JSON
  (
    "lyphcnt": int_to_json( lyphcnt ),
    "distinct fmas": int_to_json( distinct_fmas ),
    "correlations with some clindex": int_to_json( correlations_with_some_clindex ),
    "correlations with no clindex": int_to_json( correlations_with_no_clindex ),
    "pubmeds in correlations": int_to_json( pubmeds_in_correlations ),
    "lyphs in correlations": int_to_json( lyphs_in_correlations ),
    "clindices in correlations": int_to_json( clindices_in_correlations ),
    "distinct fmas annotating lyphs": int_to_json( distinct_fmas_annoting_lyphs )
  ));
}

void add_clinical_index_to_array( clinical_index *ci, clinical_index ***arr )
{
  clinical_index **buf, **ptr;
  int len;

  for ( ptr = *arr; *ptr; ptr++ )
    if ( *ptr == ci )
      return;

  len = VOIDLEN( *arr );

  CREATE( buf, clinical_index *, len + 2 );
  memcpy( buf, *arr, len * sizeof( clinical_index * ) );
  buf[len] = ci;
  buf[len+1] = NULL;

  free( *arr );
  *arr = buf;
}

void remove_clindex_from_array( clinical_index *ci, clinical_index ***arr )
{
  clinical_index **ptr, **buf, **bptr;
  int matches = 0, len;

  for ( ptr = *arr; *ptr; ptr++ )
    if ( *ptr == ci )
      matches++;

  if ( !matches )
    return;

  len = VOIDLEN( *arr );

  CREATE( buf, clinical_index *, len + 1 - matches );
  bptr = buf;

  for ( ptr = *arr; *ptr; ptr++ )
    if ( (*ptr) != ci )
      *bptr++ = *ptr;

  *bptr = NULL;
  free( *arr );
  *arr = buf;
}

bop *bop_by_int( int id )
{
  bop *b;

  for ( b = first_bop; b; b = b->next )
    if ( b->id == id )
      return b;

  return NULL;
}

bop *bop_by_id( const char *idstr )
{
  return bop_by_int( strtoul( idstr, NULL, 10 ) );
}

char *added_edge_to_json( const added_edge *e )
{
  return JSON
  (
    "from": trie_to_json( e->from->id ),
    "to": trie_to_json( e->to->id )
  );
}

char *bop_to_json( const bop *b )
{
  return JSON
  (
    "id": int_to_json( b->id ),
    "excluded": JS_ARRAY( lyph_to_json_brief, b->excluded ),
    "added": JS_ARRAY( added_edge_to_json, b->added ),
    "located measures": JS_ARRAY( located_measure_to_json_brief, b->measures )
  );
}

char *bop_to_json_flat( const bop *b )
{
  lyph **ex;
  added_edge **added;
  located_measure **measures;
  char *buf, *bptr;
  int fFirst;

  CREATE( buf, char, strlen( bop_to_json(b) ) + 1 );
  bptr = buf;

  sprintf( bptr, "{\"id\":%d,\"excluded\":\"", b->id );
  bptr += strlen( bptr );

  for ( fFirst = 0, ex = b->excluded; *ex; ex++ )
  {
    if ( fFirst )
      *bptr++ = ',';
    else
      fFirst = 1;

    sprintf( bptr, "%s", trie_to_static( (*ex)->id ) );
    bptr += strlen( bptr );
  }

  sprintf( bptr, "\",\"added\":\"" );
  bptr += strlen( bptr );

  for ( fFirst = 0, added = b->added; *added; added++ )
  {
    if ( fFirst )
      *bptr++ = ',';
    else
      fFirst = 1;

    sprintf( bptr, "%s->", trie_to_static( (*added)->from->id ) );
    bptr += strlen(bptr);
    sprintf( bptr, "%s", trie_to_static( (*added)->to->id ) );
    bptr += strlen(bptr);
  }

  sprintf( bptr, "\",\"measures\":\"" );
  bptr += strlen(bptr);

  for ( fFirst = 0, measures = b->measures; *measures; measures++ )
  {
    if ( fFirst )
      *bptr++ = ',';
    else
      fFirst = 1;

    sprintf( bptr, "%d", (*measures)->id );
    bptr += strlen( bptr );
  }

  sprintf( bptr, "\"}" );

  return buf;
}

HANDLER( do_all_bops )
{
  bop **buf, **bptr, *b;
  int cnt = 0;

  for ( b = first_bop; b; b = b->next )
    cnt++;

  CREATE( buf, bop *, cnt + 1 );
  bptr = buf;

  for ( b = first_bop; b; b = b->next )
    *bptr++ = b;

  *bptr = NULL;

  send_response( req, JS_ARRAY( bop_to_json, buf ) );

  free( buf );
}

HANDLER( do_bop )
{
  bop *b;

  if ( !*request )
    HND_ERR( "Which bop do you wish to view?" );

  b = bop_by_id( request );

  if ( !b )
    HND_ERR( "The indicated bop was not recognized" );

  send_response( req, bop_to_json( b ) );
}

added_edge *added_edge_by_notation( const char *notation )
{
  added_edge *e;
  lyphnode *from, *to;
  const char *arrow;
  char *fromid;

  for ( arrow = notation; *arrow; arrow++ )
    if ( arrow[0] == '-' && arrow[1] == '>' )
      break;

  if ( !*arrow )
    return NULL;

  to = lyphnode_by_id( arrow + strlen("->") );

  if ( !to )
    return NULL;

  CREATE( fromid, char, arrow - notation + 1 );
  memcpy( fromid, notation, arrow - notation );
  fromid[arrow - notation] = '\0';

  from = lyphnode_by_id( fromid );
  free( fromid );

  if ( !from )
    return NULL;

  CREATE( e, added_edge, 1 );
  e->from = from;
  e->to = to;

  return e;
}

void save_bops( void )
{
  FILE *fp = fopen( BOPS_FILE, "w" );
  bop *b;
  int fFirst = 0;

  if ( !fp )
  {
    error_messagef( "Could not open %s for writing", BOPS_FILE );
    return;
  }

  fprintf( fp, "[" );

  for ( b = first_bop; b; b = b->next )
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", bop_to_json_flat( b ) );
  }

  fprintf( fp, "]" );

  fclose( fp );
}

HANDLER( do_makebop )
{
  bop *b;
  lyph **excluded;
  added_edge **added;
  located_measure **measures;
  char *excludedstr, *addedstr, *locstr, *err = NULL;

  excludedstr = get_param( params, "excluded" );
  addedstr = get_param( params, "added" );
  locstr = get_param( params, "locmeas" );

  if ( excludedstr )
  {
    excluded = (lyph**)PARSE_LIST( excludedstr, lyph_by_id, "lyph", &err );

    if ( !excluded )
    {
      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the excluded lyphs was not recognized" );
    }
  }
  else
    excluded = (lyph**)blank_void_array();

  if ( addedstr )
  {
    added = (added_edge**)PARSE_LIST( addedstr, added_edge_by_notation, "added edge", &err );

    if ( !added )
    {
      free( excluded );

      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the added edges could not be resolved" );
    }
  }
  else
    added = (added_edge**)blank_void_array();

  if ( locstr )
  {
    measures = (located_measure**)PARSE_LIST( locstr, located_measure_by_id, "located measure", &err );

    if ( !measures )
    {
      MULTIFREE( excluded, added );

      if ( err )
        HND_ERR_FREE( err );
      else
        HND_ERR( "One of the located measures could not be recognized" );
    }
  }
  else
    measures = (located_measure**)blank_void_array();

  CREATE( b, bop, 1 );

  if ( last_bop )
    b->id = last_bop->id + 1;
  else
    b->id = 1;

  b->excluded = excluded;
  b->added = added;
  b->measures = measures;

  LINK2( b, first_bop, last_bop, next, prev );

  save_bops();

  send_response( req, bop_to_json( b ) );
}

int remove_lyphnode_from_bops( const lyphnode *n )
{
  bop *b;
  added_edge **edges, **new_edges, **neptr;
  int fMatch = 0;

  for ( b = first_bop; b; b = b->next )
  {
    int cnt = 0;

    for ( edges = b->added; *edges; edges++ )
      if ( (*edges)->from == n || (*edges)->to == n )
        cnt++;

    if ( !cnt )
      continue;

    fMatch = 1;

    CREATE( new_edges, added_edge *, (edges - b->added) - cnt + 1 );
    neptr = new_edges;

    for ( edges = b->added; *edges; edges++ )
    {
      if ( (*edges)->from != n && (*edges)->to != n )
        *neptr++ = *edges;
      else
        free( *edges );
    }

    *neptr = NULL;
    free( b->added );
    b->added = new_edges;
  }

  return fMatch;
}

int remove_lyph_from_bops( const lyph *e )
{
  bop *b;
  lyph **rem, **new_rem, **nrptr;
  int fMatch = 0;

  for ( b = first_bop; b; b = b->next )
  {
    int cnt = 0;

    for ( rem = b->excluded; *rem; rem++ )
      if ( *rem == e )
        cnt++;

    if ( !cnt )
      continue;

    fMatch = 1;

    CREATE( new_rem, lyph *, (rem - b->excluded) - cnt + 1 );
    nrptr = new_rem;

    for ( rem = b->excluded; *rem; rem++ )
      if ( *rem != e )
        *nrptr++ = *rem;

    *nrptr = NULL;
    free( b->excluded );
    b->excluded = nrptr;
  }

  return fMatch;
}

int remove_located_measure_from_bops( const located_measure *m )
{
  bop *b;
  int fMatch = 0;

  for ( b = first_bop; b; b = b->next )
  {
    located_measure **ptr, **remeasures, **reptr;
    int cnt = 0;

    for ( ptr = b->measures; *ptr; ptr++ )
      if ( *ptr == m )
        cnt++;

    if ( !cnt )
      continue;

    fMatch = 1;

    CREATE( remeasures, located_measure *, (ptr - b->measures) - cnt + 1 );
    reptr = remeasures;

    for ( ptr = b->measures; *ptr; ptr++ )
      if ( *ptr != m )
        *reptr++ = *ptr;

    *reptr = NULL;
    free( b->measures );
    b->measures = remeasures;
  }

  return fMatch;
}

int clindex_correlation_count( const clinical_index *ci )
{
  const correlation *c;
  variable **v;
  int cnt = 0;

  for ( c = first_correlation; c; c = c->next )
  {
    for ( v = c->vars; *v; v++ )
      if ( (*v)->type == VARIABLE_CLINDEX && (*v)->ci == ci )
        break;

    if ( *v )
      cnt++;
  }

  return cnt;
}

correlink **correlation_is_linked( correlation *x, correlation *y, int cnt )
{
  variable **v, **w;
  correlink **buf, **bptr;

  CREATE( buf, correlink *, (lyphcnt * cnt) + 1 );
  bptr = buf;

  for ( v = x->vars; *v; v++ )
    if ( (*v)->type == VARIABLE_LOCATED )
      (*v)->loc->flags = 1;

  for ( w = y->vars; *w; w++ )
  {
    if ( (*w)->type == VARIABLE_LOCATED && (*w)->loc->flags )
    {
      CREATE( *bptr, correlink, 1 );
      (*bptr)->c = y;
      (*bptr)->e = (*w)->loc;
      bptr++;
    }
  }

  for ( v = x->vars; *v; v++ )
    if ( (*v)->type == VARIABLE_LOCATED )
      (*v)->loc->flags = 0;

  if ( bptr == buf )
  {
    free( buf );
    return NULL;
  }

  *bptr = NULL;
  return buf;
}

char *correlink_to_json( const correlink *cl )
{
  return JSON
  (
    "withCorrelation": int_to_json( cl->c->id ),
    "viaLyph": trie_to_json( cl->e->id )
  );
}

int count_distinct_correlation_links( correlink **buf )
{
  correlink **ptr;
  int cnt = 0;

  for ( ptr = buf; *ptr; ptr++ )
  {
    if ( !(*ptr)->c->flags )
    {
      (*ptr)->c->flags = 1;
      cnt++;
    }
  }

  for ( ptr = buf; *ptr; ptr++ )
    (*ptr)->c->flags = 0;

  return cnt;
}

char *correlation_links_to_json( const correlation *c )
{
  return JSON
  (
    "id": int_to_json( c->id ),
    "linkcount": int_to_json( count_distinct_correlation_links( c->links ) ),
    "edgecount": int_to_json( VOIDLEN( c->links ) ),
    "links": JS_ARRAY( correlink_to_json, c->links )
  );
}

int generate_correlation_links_dotfile( correlation **cbuf )
{
  FILE *fp = fopen( CORRELATION_LINKS_DOTFILE, "w" );
  correlation **c;
  correlink **cl;

  if ( !fp )
    return 0;

  fprintf( fp, "digraph\n{\n" );

  for ( c = cbuf; *c; c++ )
    fprintf( fp, "  %d;\n", (*c)->id );

  fprintf( fp, "  subgraph cluster_0\n  {\n" );

  for ( c = cbuf; *c; c++ )
  for ( cl = (*c)->links; *cl; cl++ )
    fprintf( fp, "    %d -> %d[label=\"%s\"];\n", (*c)->id, (*cl)->c->id, trie_to_static((*cl)->e->id) );

  fprintf( fp, "  }\n}\n" );

  fclose( fp );

  return 1;
}

HANDLER( do_correlation_links )
{
  correlation *c, *lnk, **cbuf, **cbptr;
  correlink **buf, **bptr, **cl, **clptr;
  lyph *e;
  int cnt = 0;

  for ( e = first_lyph; e; e = e->next )
    e->flags = 0;

  for ( c = first_correlation; c; c = c->next )
  {
    c->links = NULL;
    cnt++;
  }

  for ( c = first_correlation; c; c = c->next )
  {
    CREATE( buf, correlink *, (cnt*lyphcnt) + 1 );
    bptr = buf;

    for ( lnk = first_correlation; lnk; lnk = lnk->next )
    {
      if ( lnk == c )
        continue;

      if ( (cl=correlation_is_linked( c, lnk, cnt )) != NULL )
      {
        for ( clptr = cl; *clptr; clptr++ )
          *bptr++ = *clptr;

        free( cl );
      }
    }

    *bptr = NULL;
    c->links = buf;
  }

  CREATE( cbuf, correlation *, cnt + 1 );
  cbptr = cbuf;

  for ( c = first_correlation; c; c = c->next )
    *cbptr++ = c;

  *cbptr = NULL;

  if ( !get_param( params, "dotfile" ) )
    send_response( req, JS_ARRAY( correlation_links_to_json, cbuf ) );
  else
  {
    char *dotfile;

    if ( !generate_correlation_links_dotfile( cbuf )
    ||   !(dotfile = load_file( CORRELATION_LINKS_DOTFILE )) )
    {
      free( cbuf );
      HND_ERR( "Could not open " CORRELATION_LINKS_DOTFILE );
    }

    send_response_with_type( req, "200", dotfile, "text/plain" );
    free( dotfile );
  }

  free( cbuf );

  for ( c = first_correlation; c; c = c->next )
  {
    for ( clptr = c->links; *clptr; clptr++ )
      free( *clptr );

    free( c->links );
    c->links = NULL;
  }
}

HANDLER( do_dump )
{
  lyphplate **lpbuf = (lyphplate**)datas_to_array( lyphplate_ids );
  layer **lyrbuf = (layer**)datas_to_array( layer_ids );
  lyphnode **lyphnodebuf = (lyphnode**)datas_to_array( lyphnode_ids );
  lyph **lyphbuf = (lyph**)datas_to_array( lyph_ids );
  lyphview **v, **vptr, **viewsptr;
  extern lyphview **views;
  clinical_index **cibuf, **cibufptr, *ci;
  pubmed **pubmedbuf, **pubmedbptr, *pbmd;
  correlation **cbuf, **cbufptr, *c;
  bop **bopbuf, **bopbptr, *bp;
  extern lyphview obsolete_lyphview;
  int cnt = 0;

  cnt = 0;
  for ( bp = first_bop; bp; bp = bp->next )
    cnt++;

  CREATE( bopbuf, bop *, cnt + 1 );
  bopbptr = bopbuf;

  for ( bp = first_bop; bp; bp = bp->next )
    *bopbptr++ = bp;

  *bopbptr = NULL;

  cnt = 0;
  for ( pbmd = first_pubmed; pbmd; pbmd = pbmd->next )
    cnt++;

  cnt = 0;
  for ( c = first_correlation; c; c = c->next )
    cnt++;

  CREATE( cbuf, correlation *, cnt + 1 );
  cbufptr = cbuf;

  for ( c = first_correlation; c; c = c->next )
    *cbufptr++ = c;

  *cbufptr = NULL;

  CREATE( pubmedbuf, pubmed *, cnt + 1 );
  pubmedbptr = pubmedbuf;
  for ( pbmd = first_pubmed; pbmd; pbmd = pbmd->next )
    *pubmedbptr++ = pbmd;
  *pubmedbptr = NULL;

  cnt = 0;
  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  CREATE( cibuf, clinical_index *, cnt + 1 );

  for ( cibufptr = cibuf, ci = first_clinical_index; ci; ci = ci->next )
    *cibufptr++ = ci;

  *cibufptr = NULL;

  CREATE( v, lyphview *, VOIDLEN( &views[1] ) + 1 );
  vptr = v;

  for ( viewsptr = &views[1]; *viewsptr; viewsptr++ )
    if ( *viewsptr != &obsolete_lyphview )
      *vptr++ = *viewsptr;

  *vptr = NULL;

  send_response( req, JSON
  (
    "lyphplates": JS_ARRAY( lyphplate_to_json, lpbuf ),
    "layers": JS_ARRAY( layer_to_json, lyrbuf ),
    "lyphnodes": JS_ARRAY( lyphnode_to_json, lyphnodebuf ),
    "lyphs": JS_ARRAY( lyph_to_json, lyphbuf ),
    "views": JS_ARRAY( lyphview_to_json, v ),
    "clinical indices": JS_ARRAY( clinical_index_to_json_full, cibuf ),
    "pubmeds": JS_ARRAY( pubmed_to_json_full, pubmedbuf ),
    "correlations": JS_ARRAY( correlation_to_json, cbuf ),
    "bops": JS_ARRAY( bop_to_json, bopbuf )
  ) );

  free( lpbuf );
  free( lyrbuf );
  free( lyphnodebuf );
  free( v );
  free( cibuf );
  free( cbuf );
  free( bopbuf );
}
