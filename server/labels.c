/*
 *  labels.c
 *  Lyph system initiation function; functions for parsing N-Triples;
 *  and some miscelaneous ontology-term logic.
 */
#include "lyph.h"
#include "nt_parse.h"

void init_labels(FILE *fp)
{
  iri_to_labels = blank_trie();
  label_to_iris = blank_trie();
  label_to_iris_lowercase = blank_trie();
  superclasses = blank_trie();

  parse_ontology_file( fp );

  lyphplate_names = blank_trie();
  lyphplate_ids = blank_trie();
  layer_ids = blank_trie();
  lyphnode_ids = blank_trie();
  lyph_ids = blank_trie();
  lyph_names = blank_trie();
  lyph_fmas = blank_trie();
  metadata = blank_trie();

  init_html_codes();

  load_lyphplates();
  load_lyphs();
  load_lyphviews();

  load_pubmeds();
  load_clinical_indices();
  load_lyph_annotations();

  load_located_measures();
  load_correlations();
  load_bops();

  human_species_lowercase = trie_strdup( "human", metadata );
  human_species_uppercase = trie_strdup( "Human", metadata );

  parse_fma_file();
  parse_nifling_file();
  /*
   * flatten_fmas();
   */
  compute_inferred_parts( NULL, 0 );

  return;
}

void got_triple( char *subj, char *pred, char *obj )
{
  if ( !strcmp( pred, "<http://www.w3.org/2000/01/rdf-schema#label>" )
  ||   !strcmp( pred, "<rdfs:label>" ) )
  {
    if ( *obj == '"' && *subj == '<' )
    {
      obj[strlen(obj)-1] = '\0';
      subj[strlen(subj)-1] = '\0';

      add_labels_entry( &subj[1], &obj[1] );
    }
    goto got_triple_cleanup;
  }

  if ( !strcmp( pred, "<http://www.w3.org/2000/01/rdf-schema#subClassOf>" )
  ||   !strcmp( pred, "<rdfs:subClassOf>" ) )
  {
    obj[strlen(obj)-1] = '\0';
    subj[strlen(subj)-1] = '\0';

    add_subclass_entry( &subj[1], &obj[1] );
  }

  got_triple_cleanup:
  free( subj );
  free( pred );
  free( obj );
}

void parse_ontology_file(FILE *fp)
{
  char *err = NULL;

  if ( !parse_ntriples( fp, &err, MAX_IRI_LEN, got_triple ) )
  {
    char *buf = malloc(strlen(err) + 1024);

    sprintf( buf, "Failed to parse the triples-file:\n%s\n", err );

    error_message( buf );
    abort();
  }
}

void add_to_data( trie ***dest, trie *datum )
{
  trie **data;
  int cnt;

  if ( !*dest )
  {
    CREATE( *dest, trie *, 2 );
    (*dest)[0] = datum;
    (*dest)[1] = NULL;
  }
  else
  {
    cnt = VOIDLEN( *dest );
    CREATE( data, trie *, cnt + 2 );
    memcpy( data, *dest, cnt * sizeof(trie*) );
    data[cnt] = datum;
    data[cnt+1] = NULL;
    free( *dest );
    *dest = data;
  }
}

void add_subclass_entry( char *child_ch, char *parent_ch )
{
  char *child_shortform = get_url_shortform( child_ch );
  char *parent_shortform = get_url_shortform( parent_ch );
  trie *child, *parent;

  if ( !child_shortform )
    child_shortform = child_ch;
  if ( !parent_shortform )
    parent_shortform = parent_ch;

  child = trie_strdup( child_shortform, superclasses );
  parent = trie_strdup( parent_shortform, superclasses );

  add_to_data( &child->data, parent );
}

void add_labels_entry( char *iri_ch, char *label_ch )
{
  char *iri_shortform_ch, *label_lowercase_ch;
  trie *iri = trie_strdup(iri_ch, iri_to_labels);
  trie *label = trie_strdup(label_ch, label_to_iris);
  trie *label_lowercase;
  trie *in_superclasses;

  add_to_data( &iri->data, label );
  add_to_data( &label->data, iri );

  if ( (iri_shortform_ch = get_url_shortform(iri_ch)) != NULL )
  {
    trie *iri_shortform = trie_strdup( iri_shortform_ch, iri_to_labels );
    add_to_data( &iri_shortform->data, label );

    in_superclasses = trie_strdup( iri_shortform_ch, superclasses );

    if ( !in_superclasses->data )
      in_superclasses->data = (trie**)blank_void_array();
  }

  label_lowercase_ch = lowercaserize(label_ch);
  label_lowercase = trie_strdup( label_lowercase_ch, label_to_iris_lowercase );
  add_to_data( &label_lowercase->data, iri );

  return;
}

trie **get_labels_by_iri( char *iri_ch )
{
  trie *iri = trie_search( iri_ch, iri_to_labels );

  if ( !iri )
    return NULL;

  return iri->data;
}

trie **get_iris_by_label( char *label_ch )
{
  trie *label = trie_search( label_ch, label_to_iris );

  if ( !label )
    return NULL;

  return label->data;
}

trie **get_iris_by_label_case_insensitive( char *label_ch )
{
  char *lowercase = lowercaserize( label_ch );
  trie *label = trie_search( lowercase, label_to_iris_lowercase );

  if ( !label )
    return NULL;

  return label->data;
}

trie **get_autocomplete_labels( char *label_ch, int case_insens )
{
  static trie **buf;

  if ( case_insens )
    label_ch = lowercaserize( label_ch );

  if ( !buf )
    CREATE( buf, trie *, MAX_AUTOCOMPLETE_RESULTS_PRESORT + 1 );

  trie_search_autocomplete( label_ch, buf, case_insens ? label_to_iris_lowercase : label_to_iris, 0, 0 );

  return buf;
}

void all_ont_terms_as_json_populator( trie ***bptr, trie *t )
{
  if ( t->data )
  {
    **bptr = t;
    (*bptr)++;
  }

  TRIE_RECURSE( all_ont_terms_as_json_populator( bptr, *child ) );
}

char *ont_term_to_json( trie *t )
{
  trie *label = *t->data;

  return JSON
  (
    "term": trie_to_json( t ),
    "label": trie_to_json( label )
  );
}

char *all_ont_terms_as_json( void )
{
  trie **buf, **bptr;
  char *retval;
  int cnt = count_nontrivial_members( iri_to_labels );

  CREATE( buf, trie *, cnt + 1 );
  bptr = buf;

  all_ont_terms_as_json_populator( &bptr, iri_to_labels );
  *bptr = NULL;

  retval = JS_ARRAY( ont_term_to_json, buf );
  free( buf );

  return retval;
}
