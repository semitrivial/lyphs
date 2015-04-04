#include "lyph.h"
#include "srv.h"
#include "nt_parse.h"

clinical_index *first_clinical_index;
clinical_index *last_clinical_index;
pubmed *first_pubmed;
pubmed *last_pubmed;

void load_annotations(void)
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( ANNOTS_FILE, "r" );

  if ( !fp )
  {
    log_string( "Could not open " ANNOTS_FILE " for reading, so no annotations have been loaded" );
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
      error_messagef( "Bad line in " ANNOTS_FILE ": %d", line );
      EXIT();
    }

    sscanf_result = sscanf( &buf[strlen("Annot ")], "%s %s %s %s", subj_enc, pred_enc, obj_enc, pubmed_enc );

    if ( sscanf_result != 4 )
    {
      error_messagef( "In " ANNOTS_FILE ":%d, the line (%s) has an unrecognized format (%d)", line, &buf[strlen("Annot ")], sscanf_result );
      EXIT();
    }

    subj = url_decode( subj_enc );
    pred = url_decode( pred_enc );
    obj = url_decode( obj_enc );
    pubmed_ch = url_decode( pubmed_enc );

    e = lyph_by_id( subj );

    if ( !e )
    {
      error_messagef( "In " ANNOTS_FILE ":%d, lyph '%s' is unrecognized", line, subj );
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
        error_messagef( "In " ANNOTS_FILE ":%d, pubmed ID '%s' is unrecognized", line, pubmed_ch );
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
  annot **aptr, *a, **buf;
  int size;

  for ( aptr = e->annots; *aptr; aptr++ )
    if ( (*aptr)->pred == pred && (*aptr)->obj == obj )
      return 0;

  CREATE( a, annot, 1 );
  a->pred = pred;
  a->obj = obj;
  a->pubmed = pubmed;

  size = VOIDLEN( e->annots );

  CREATE( buf, annot *, size + 2 );

  memcpy( buf, e->annots, (size + 1) * sizeof(annot *) );

  buf[size] = a;
  buf[size+1] = NULL;

  free( e->annots );
  e->annots = buf;

  return 1;
}

HANDLER( handle_annotate_request )
{
  lyph **lyphs, **lptr;
  trie *pred, *obj;
  pubmed *pubmed;
  char *lyphstr, *annotstr, *predstr, *pubmedstr, *err;
  int fMatch = 0;

  TRY_TWO_PARAMS( lyphstr, "lyphs", "lyph", "You did not specify which lyphs to annotate" );

  TRY_PARAM( annotstr, "annot", "You did not specify (using the 'annot' parameter) what to annotate the lyphs by" );

  pubmedstr = get_param( params, "pubmed" );

  if ( pubmedstr )
  {
    pubmed = pubmed_by_id( pubmedstr );

    if ( !pubmed )
      HND_ERR( "The indicated pubmed ID was not found" );
  }
  else
    pubmed = NULL;

  predstr = get_param( params, "pred" );

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

  for ( lptr = lyphs; *lptr; lptr++ )
    fMatch |= annotate_lyph( *lptr, pred, obj, pubmed );

  if ( fMatch )
    save_annotations();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void save_annotations_recurse( FILE *fp, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *) t->data;

    if ( *e->annots )
    {
      annot **a;

      for ( a = e->annots; *a; a++ )
      {
        char *subj = url_encode( trie_to_static( t ) );
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

  TRIE_RECURSE( save_annotations_recurse( fp, *child ) );
}

void save_annotations( void )
{
  FILE *fp;

  fp = fopen( ANNOTS_FILE, "w" );

  if ( !fp )
  {
    error_message( "Could not open " ANNOTS_FILE " for writing" );
    return;
  }

  save_annotations_recurse( fp, lyph_ids );

  fclose(fp);

  return;
}

pubmed *pubmed_by_id( char *id )
{
  pubmed *p;

  for ( p = first_pubmed; p; p = p->next )
    if ( !strcmp( p->id, id ) )
      return p;

  return NULL;
}

void save_one_pubmed( FILE *fp, pubmed *p )
{
  char *id = url_encode( p->id );
  char *title = url_encode( p->title );

  fprintf( fp, "%s %s\n", id, title );

  free( id );
  free( title );
}

void save_pubmeds( void )
{
  pubmed *p;
  FILE *fp;

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

void load_pubmeds( void )
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( PUBMED_FILE, "r" );

  if ( !fp )
  {
    log_string( "Could not open " PUBMED_FILE " for reading -- no pubmeds loaded" );
    return;
  }

  for ( ; ; )
  {
    pubmed *p;
    char id_enc[MAX_STRING_LEN], title_enc[MAX_STRING_LEN];
    int sscanf_results;

    QUICK_GETLINE( buf, bptr, c, fp );

    if ( !bptr )
      return;

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

  fclose( fp );

  return;
}

void save_one_clinical_index( FILE *fp, clinical_index *c )
{
  char *index = url_encode( trie_to_static( c->index ) );
  char *label = url_encode( trie_to_static( c->label ) );

  fprintf( fp, "%s %s\n", index, label );

  free( index );
  free( label );
}

void save_clinical_indices( void )
{
  FILE *fp;
  clinical_index *c;

  fp = fopen( CLINICAL_INDEX_FILE, "w" );

  if ( !fp )
  {
    error_messagef( "Could not open " CLINICAL_INDEX_FILE " for writing" );
    EXIT();
  }

  for ( c = first_clinical_index; c; c = c->next )
    save_one_clinical_index( fp, c );

  fclose( fp );
}

void load_clinical_indices( void )
{
  FILE *fp;
  char buf[MAX_STRING_LEN], *bptr, c;
  int line = 0;

  /*
   * Variables for QUICK_GETC
   */
  char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
  int fread_len;

  fp = fopen( CLINICAL_INDEX_FILE, "r" );

  if ( !fp )
  {
    error_messagef( "Couldn't open " CLINICAL_INDEX_FILE " for reading" );
    EXIT();
  }

  for ( ; ; )
  {
    clinical_index *ci;
    trie *index_tr, *label_tr;
    char index_enc[MAX_STRING_LEN], label_enc[MAX_STRING_LEN], *index, *label;
    int sscanf_results;

    QUICK_GETLINE( buf, bptr, c, fp );

    if ( !bptr )
      break;

    line++;

    sscanf_results = sscanf( buf, "%s %s", index_enc, label_enc );

    if ( sscanf_results != 2 )
    {
      error_messagef( CLINICAL_INDEX_FILE ":%d: Unrecognized format", line );
      EXIT();
    }

    index = url_decode( index_enc );
    label = url_decode( label_enc );
    index_tr = trie_strdup( index, metadata );
    label_tr = trie_strdup( label, metadata );

    MULTIFREE( index, label, index_enc, label_enc );

    CREATE( ci, clinical_index, 1 );
    ci->index = index_tr;
    ci->label = label_tr;
    LINK( ci, first_clinical_index, last_clinical_index, next );
  }

  fclose( fp );
}

HANDLER( handle_make_pubmed_request )
{
  pubmed *p;
  char *id, *title;

  TRY_PARAM( id, "id", "You did not specify an 'id' for the pubmed entry" );
  TRY_PARAM( title, "title", "You did not specify a 'title' for the pubmed entry" );

  p = pubmed_by_id( id );

  if ( p )
  {
    if ( strcmp( p->title, title ) )
      HND_ERR( "There is already a pubmed entry with that ID." );
    else
    {
      send_200_response( req, pubmed_to_json_full( p ) );
      return;
    }
  }

  CREATE( p, pubmed, 1 );
  p->id = strdup( id );
  p->title = strdup( title );
  LINK( p, first_pubmed, last_pubmed, next );

  save_pubmeds();

  send_200_response( req, pubmed_to_json_full( p ) );
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

HANDLER( handle_make_clinical_index_request )
{
  clinical_index *ci;
  char *index, *label;

  TRY_PARAM( index, "index", "You did not specify an 'index' for this clinical index" );
  TRY_PARAM( label, "label", "You did not specify a 'label' for this clinical index" );

  ci = clinical_index_by_index( index );

  if ( ci )
  {
    if ( !strcmp( label, trie_to_static( ci->label ) ) )
    {
      send_200_response( req, clinical_index_to_json_full( ci ) );
      return;
    }
    else
      HND_ERR( "There is already a clinical index with that index" );
  }

  CREATE( ci, clinical_index, 1 );
  ci->index = trie_strdup( index, metadata );
  ci->label = trie_strdup( label, metadata );
  LINK( ci, first_clinical_index, last_clinical_index, next );

  save_clinical_indices();

  send_200_response( req, clinical_index_to_json_full( ci ) );
}

char *clinical_index_to_json_full( clinical_index *ci )
{
  return JSON
  (
    "index": trie_to_json( ci->index ),
    "label": trie_to_json( ci->label )
  );
}

char *clinical_index_to_json_brief( clinical_index *ci )
{
  return trie_to_json( ci->index );
}

clinical_index *clinical_index_by_index( char *ind )
{
  clinical_index *ci;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    if ( !strcmp( ind, trie_to_static( ci->index ) ) )
      return ci;

  return NULL;
}

HANDLER( handle_edit_clinical_index_request )
{
  clinical_index *ci;
  char *indexstr, *label;

  TRY_PARAM( indexstr, "index", "You did not specify which clinical index ('index') to edit" );

  ci = clinical_index_by_index( indexstr );

  if ( !ci )
    HND_ERR( "The indicated clinical index was not recognized" );

  TRY_PARAM( label, "label", "You did not specify a new label for the clinical index" );

  ci->label = trie_strdup( label, metadata );

  send_200_response( req, clinical_index_to_json_full( ci ) );
}

HANDLER( handle_edit_pubmed_request )
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

  send_200_response( req, pubmed_to_json_full( pubmed ) );
}

HANDLER( handle_pubmed_request )
{
  pubmed *pubmed;
  char *pubmedstr;

  TRY_PARAM( pubmedstr, "pubmed", "You did not specify which pubmed to view" );

  pubmed = pubmed_by_id( pubmedstr );

  if ( !pubmed )
    HND_ERR( "The indicated pubmed was not recognized" );

  send_200_response( req, pubmed_to_json_full( pubmed ) );
}

HANDLER( handle_clinical_index_request )
{
  clinical_index *ci;
  char *cistr;

  TRY_PARAM( cistr, "index", "You did not specify the 'index' of the clinical index you want to view" );

  ci = clinical_index_by_index( cistr );

  if ( !ci )
    HND_ERR( "The indicated clinical index was not recognized" );

  send_200_response( req, clinical_index_to_json_full( ci ) );
}

HANDLER( handle_all_clinical_indices_request )
{
  clinical_index **buf, **bptr, *ci;
  int cnt = 0;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    cnt++;

  CREATE( buf, clinical_index *, cnt + 1 );

  for ( bptr = buf, ci = first_clinical_index; ci; ci = ci->next )
    *bptr++ = ci;

  *bptr = NULL;

  send_200_response( req, JSON1
  (
    "results": JS_ARRAY( clinical_index_to_json_full, buf )
  ) );

  free( buf );
}

HANDLER( handle_all_pubmeds_request )
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

  send_200_response( req, retval );
}