#include "lyph.h"
#include "srv.h"
#include "nt_parse.h"

clinical_index *first_clinical_index;
clinical_index *last_clinical_index;
pubmed *first_pubmed;
pubmed *last_pubmed;
bulk_annot *first_bulk_annot;
bulk_annot *last_bulk_annot;
int top_bulk_annot_id;

HANDLER( handle_all_bulk_annots_request )
{
  bulk_annot *b, **buf, **bptr;
  int cnt = 0;

  for ( b = first_bulk_annot; b; b = b->next )
    cnt++;

  CREATE( buf, bulk_annot *, cnt + 1 );
  bptr = buf;

  for ( b = first_bulk_annot; b; b = b->next )
    *bptr++ = b;

  *bptr = NULL;

  send_200_response( req, JS_ARRAY( bulk_annot_to_json, buf ) );

  free( buf );
}

trie *assign_bulk_annot_id( void )
{
  char buf[256];

  sprintf( buf, "BULK_ANNOT_%d", ++top_bulk_annot_id );

  return trie_strdup( buf, metadata );
}

char *bulk_annot_type_to_str( int type )
{
  switch( type )
  {
    case BULK_ANNOT_CLINICAL:
      return "clinical index";
    case BULK_ANNOT_RADIOLOGICAL:
      return "radiological index";
    case BULK_ANNOT_FUNCTIONAL:
      return "functional";
    default:
      return "unknown";
  }
}

char *bulk_annot_to_json( bulk_annot *a )
{
  return JSON
  (
    "id": trie_to_json( a->id ),
    "type": bulk_annot_type_to_str( a->type ),
    "lyphs": JS_ARRAY( lyph_to_json_id, a->lyphs ),
    "clinical index": a->ci ? clinical_index_to_json_full( a->ci ) : json_suppressed,
    "radiological index": a->radio_index ? trie_to_json( a->radio_index ) : json_suppressed,
    "pubmed": pubmed_to_json_brief( a->pbmd )
  );
}

char *lyph_to_json_id( lyph *e )
{
  return trie_to_json( e->id );
}

char *bulk_annot_to_json_save( bulk_annot *a )
{
  return JSON
  (
    "id": trie_to_json( a->id ),
    "type": int_to_json( a->type ),
    "lyphs": JS_ARRAY( lyph_to_json_id, a->lyphs ),
    "clinical index": a->ci ? clinical_index_to_json_brief( a->ci ) : json_suppressed,
    "radiological index": a->radio_index ? trie_to_json( a->radio_index ) : json_suppressed,
    "pubmed": pubmed_to_json_brief( a->pbmd )
  );
}

bulk_annot *bulk_annot_by_id( const char *id )
{
  bulk_annot *b;
  trie *idtr = trie_strdup( id, metadata );

  for ( b = first_bulk_annot; b; b = b->next )
    if ( b->id == idtr )
      return b;

  return NULL;
}

void save_bulk_annots( void )
{
  bulk_annot *b;
  FILE *fp = fopen( BULK_ANNOTS_FILE, "w" );
  int fFirst = 0;

  if ( !fp )
  {
    error_message( "Could not open " BULK_ANNOTS_FILE " to write" );
    EXIT();
  }

  fprintf( fp, "[" );

  for ( b = first_bulk_annot; b; b = b->next )
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", bulk_annot_to_json_save( b ) );
  }

  fprintf( fp, "]" );
  fclose( fp );
}

HANDLER( handle_bulk_annot_request )
{
  bulk_annot *b;
  lyph **lyphs;
  pubmed *pbmd;
  clinical_index *ci;
  trie *radio;
  char *typestr, *lyphsstr, *cistr, *radiostr, *pubmedstr, *err;
  int type, savepubs = 0;

  TRY_PARAM( typestr, "type", "You did not indicate a 'type' ('C', 'R', or 'F')" );
  TRY_PARAM( lyphsstr, "lyphs", "You did not indicate which 'lyphs' the annotation covers" );
  TRY_PARAM( pubmedstr, "pubmed", "You did not indicate a 'pubmed' associated with the annotation" );

  if ( *typestr == 'C' || *typestr == 'c' )
    type = BULK_ANNOT_CLINICAL;
  else if ( *typestr == 'R' || *typestr == 'r' )
    type = BULK_ANNOT_RADIOLOGICAL;
  else if ( *typestr == 'F' || *typestr == 'f' )
    type = BULK_ANNOT_FUNCTIONAL;
  else
    HND_ERR( "'type' must be 'C' (clinical index), 'R' (radiological index), or 'F' (functional)" );

  if ( type == BULK_ANNOT_CLINICAL )
  {
    TRY_PARAM( cistr, "clindex", "You did not specify a 'clindex' (clinical index)" );

    ci = clinical_index_by_index( cistr );

    if ( !ci )
      HND_ERR( "The indicated clinical index was not recognized" );
  }
  else
    ci = NULL;

  if ( type == BULK_ANNOT_RADIOLOGICAL )
  {
    TRY_PARAM( radiostr, "radio", "You did not specify a 'radio' (radiological index)" );
    radio = trie_strdup( radiostr, metadata );
  }
  else
    radio = NULL;

  lyphs = (lyph**)PARSE_LIST( lyphsstr, lyph_by_id, "lyph", &err );

  if ( !lyphs )
  {
    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated lyphs was unrecognized" );
  }

  pbmd = pubmed_by_id_or_create( pubmedstr, &savepubs );

  if ( !pbmd )
  {
    free( lyphs );
    HND_ERR( "The indicated pubmed was unrecognized and could not be created" );
  }

  if ( savepubs )
    save_pubmeds();

  CREATE( b, bulk_annot, 1 );
  b->type = type;
  b->lyphs = lyphs;
  b->ci = ci;
  b->pbmd = pbmd;
  b->radio_index = radio;
  b->id = assign_bulk_annot_id();
printf( "Debug3: %s\n", trie_to_static( b->id ) );
  LINK( b, first_bulk_annot, last_bulk_annot, next );

  save_bulk_annots();

  send_200_response( req, bulk_annot_to_json( b ) );
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

void populate_lyph_annot_list_by_pred( trie *pred, lyph_annot_wrapper **head, lyph_annot_wrapper **tail, int *cnt, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph*)t->data;
    lyph_annot **a;

    for ( a = e->annots; *a; a++ )
    {
      if ( (*a)->pred == pred )
      {
        lyph_annot_wrapper *w;

        CREATE( w, lyph_annot_wrapper, 1 );
        w->a = *a;
        LINK( w, *head, *tail, next );
        (*cnt)++;
      }
    }
  }

  TRIE_RECURSE( populate_lyph_annot_list_by_pred( pred, head, tail, cnt, *child ) );
}

HANDLER( handle_radiological_indices_request )
{
  lyph_annot **buf, **bptr;
  lyph_annot_wrapper *head = NULL, *tail = NULL, *w, *w_next;
  trie *pred = trie_strdup( RADIOLOGICAL_INDEX_PRED, metadata );
  int cnt = 0;

  populate_lyph_annot_list_by_pred( pred, &head, &tail, &cnt, lyph_ids );

  CREATE( buf, lyph_annot *, cnt + 1 );
  bptr = buf;

  for ( w = head; w; w = w_next )
  {
    w_next = w->next;

    *bptr++ = w->a;
    free( w );
  }
  *bptr = NULL;

  send_200_response( req, JS_ARRAY( lyph_annot_obj_to_json, buf ) );

  free( buf );
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
  TRY_PARAM( pubmedstr, "pubmed", "You did not specify (using the 'pubmed' parameter) which pubmed ID" );

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

  pubmed = pubmed_by_id_or_create( pubmedstr, NULL );

  for ( lptr = lyphs; *lptr; lptr++ )
    fMatch |= annotate_lyph( *lptr, pred, obj, pubmed );

  if ( fMatch )
    save_lyph_annotations();

  send_200_response( req, JSON1( "Response": "OK" ) );
}

void save_lyph_annotations_recurse( FILE *fp, trie *t )
{
  if ( t->data )
  {
    lyph *e = (lyph *) t->data;

    if ( *e->annots )
    {
      lyph_annot **a;

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

  TRIE_RECURSE( save_lyph_annotations_recurse( fp, *child ) );
}

void save_lyph_annotations( void )
{
  FILE *fp;

  if ( configs.readonly )
    return;

  fp = fopen( LYPH_ANNOTS_FILE, "w" );

  if ( !fp )
  {
    error_message( "Could not open " LYPH_ANNOTS_FILE " for writing" );
    return;
  }

  save_lyph_annotations_recurse( fp, lyph_ids );

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
  {
    if ( fFirst )
      fprintf( fp, "," );
    else
      fFirst = 1;

    fprintf( fp, "%s", clinical_index_to_json_full( c ) );
  }

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
    LINK( ci, first_clinical_index, last_clinical_index, next );
  }

  fclose( fp );
}

void load_bulk_annots( void )
{
  char *js = load_file( BULK_ANNOTS_FILE );

  if ( !js )
  {
    error_messagef( "Couldn't open %s for reading --- no bulk annots will be loaded", BULK_ANNOTS_FILE );
    return;
  }

  bulk_annots_from_js( js );

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

HANDLER( handle_make_pubmed_request )
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
  pubmed **pubmeds;
  char *index, *label, *pubmedsstr;

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

  CREATE( ci, clinical_index, 1 );
  ci->index = trie_strdup( index, metadata );
  ci->label = trie_strdup( label, metadata );
  ci->pubmeds = pubmeds;
  ci->claimed = NULL;
  LINK( ci, first_clinical_index, last_clinical_index, next );

  save_clinical_indices();

  send_200_response( req, clinical_index_to_json_full( ci ) );
}

char *clinical_index_to_json_full( clinical_index *ci )
{
  return JSON
  (
    "index": trie_to_json( ci->index ),
    "label": trie_to_json( ci->label ),
    "pubmeds": JS_ARRAY( pubmed_to_json_brief, ci->pubmeds ),
    "claimed": ci->claimed ? ci->claimed : json_suppressed
  );
}

char *clinical_index_to_json_brief( clinical_index *ci )
{
  return trie_to_json( ci->index );
}

clinical_index *clinical_index_by_index( const char *ind )
{
  clinical_index *ci;
  trie *ind_tr;

  ind_tr = trie_search( ind, metadata );

  if ( !ind_tr )
    return NULL;

  for ( ci = first_clinical_index; ci; ci = ci->next )
    if ( ci->index == ind_tr )
      return ci;

  return NULL;
}

HANDLER( handle_edit_clinical_index_request )
{
  clinical_index *ci;
  pubmed **pubmeds;
  char *indexstr, *label, *pubmedsstr, *claimedstr;

  TRY_PARAM( indexstr, "index", "You did not specify which clinical index ('index') to edit" );

  ci = clinical_index_by_index( indexstr );

  if ( !ci )
    HND_ERR( "The indicated clinical index was not recognized" );

  label = get_param( params, "label" );

  pubmedsstr = get_param( params, "pubmeds" );
  claimedstr = get_param( params, "claimed" );

  if ( !label && !pubmedsstr && !claimedstr )
    HND_ERR( "You did not specify any changes to make" );

  if ( pubmedsstr )
  {
    char *err;
    int save = 0;

    pubmeds = (pubmed **) PARSE_LIST_R( pubmedsstr, pubmed_by_id_or_create, &save, "pubmed", &err );

    if ( save )
      save_pubmeds();

    if ( err )
      HND_ERR_FREE( err );
    else
      HND_ERR( "One of the indicated pubmeds was not recognized" );

    ci->pubmeds = pubmeds;
  }

  if ( label )
    ci->label = trie_strdup( label, metadata );

  if ( claimedstr )
  {
    if ( ci->claimed )
      free( ci->claimed );

    ci->claimed = strdup( claimedstr );
  }

  save_clinical_indices();

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

int has_some_clinical_index( lyph *e, clinical_index **cis )
{
  lyph_annot **a;

  for ( a = e->annots; *a; a++ )
  {
    clinical_index **cptr;

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
      if ( (*a)->obj == (*cptr)->index )
        break;

    if ( !*a )
      return 0;
  }

  return 1;
}

void calc_lyphs_with_indices( trie *t, lyph ***bptr, clinical_index **cis, int type )
{
  if ( t->data )
  {
    lyph *e = (lyph *)t->data;

    if ( ( type == CLINICAL_INDEX_SEARCH_UNION && has_some_clinical_index( e, cis ) )
    ||   ( type == CLINICAL_INDEX_SEARCH_IX && has_all_clinical_indices( e, cis ) ) )
    {
      (**bptr) = e;
      (*bptr)++;
    }
  }

  TRIE_RECURSE( calc_lyphs_with_indices( *child, bptr, cis, type ) );
}

HANDLER( handle_has_clinical_index_request )
{
  clinical_index **cis;
  lyph **buf, **bptr;
  lyph_to_json_details details;
  char *cistr, *typestr, *err;
  int cnt, type;

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

  cnt = count_nontrivial_members( lyph_ids );

  CREATE( buf, lyph *, cnt + 1 );
  bptr = buf;

  calc_lyphs_with_indices( lyph_ids, &bptr, cis, type );

  free( cis );

  *bptr = NULL;

  details.show_annots = 1;
  details.buf = buf;

  send_200_response( req, JS_ARRAY_R( lyph_to_json_r, buf, &details ) );

  free( buf );
}

HANDLER( handle_remove_annotation_request )
{
  lyph **lyphs, **eptr;
  trie *obj;
  char *lyphsstr, *annotstr, *err;
  int fMatch;

  TRY_PARAM( lyphsstr, "lyphs", "You did not specify which lyphs to remove the annotations from" );
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
    send_200_response( req, JSON1( "Response": "OK" ) );
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
      CREATE( buf, lyph_annot *, VOIDLEN( e->annots ) - matches );
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

  send_200_response( req, JSON1( "Response": "OK" ) );
}
