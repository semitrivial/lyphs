#include <cstdio>
#include <cstdlib>
#include <iostream>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace rapidjson;
using namespace std;

extern "C"
{
  #include "lyph.h"
}

#define CST(fnc) (void * (*) (const char *)) (fnc)

void **array_from_value( Value &v, void * (*fnc) (const char *) )
{
  int size = v.Size();
  void **buf = new void * [size+1];

  for ( int i = 0; i < size; i++ )
    buf[i] = (*fnc)( v[i].GetString() );

  buf[size] = NULL;

  return buf;
}

void **array_from_doc( Value &d, const char *key, void * (*fnc) (const char *) )
{
  if ( d.HasMember( key ) && d[key].IsArray() )
    return array_from_value( d[key], fnc );
  else
    return blank_void_array();
}

trie *trie_from_doc( Document &d, const char *key, trie *base )
{
  if ( d.HasMember( key ) && d[key].IsString() )
    return trie_search( d[key].GetString(), base );
  else
    return NULL;
}

void int_from_doc( Document &d, const char *key, int *dest )
{
  if ( d.HasMember( key ) && d[key].IsInt() )
    *dest = d[key].GetInt();
  else
    *dest = -1;
}

void clinical_index_from_js( clinical_index *ci, Value &v )
{
  clinical_index **parents;

  ci->index = trie_strdup( v["index"].GetString(), metadata );
  ci->label = trie_strdup( v["label"].GetString(), metadata );
  ci->flags = 0;
  ci->pubmeds = (pubmed**)array_from_doc( v, "pubmeds", CST(pubmed_by_id_or_create) );

  if ( v.HasMember("claimed") && v["claimed"].IsString() )
    ci->claimed = strdup( v["claimed"].GetString() );

  if ( v.HasMember("parents") && v["parents"].IsArray() )
  {
    clinical_index **pptr;

    parents = (clinical_index**)array_from_value( v["parents"], CST(clinical_index_by_index) );

    if ( !parents )
    {
      error_messagef( "Clinical index [%s]: has at least one unrecognized parent.  Making parentless.", v["index"].GetString() );
      parents = (clinical_index**)blank_void_array();
    }
    else
      for ( pptr = parents; *pptr; pptr++ )
        add_clinical_index_to_array( ci, &((*pptr)->children) );
  }
  else
    parents = (clinical_index**)blank_void_array();

  ci->parents = parents;
  ci->children = (clinical_index**)blank_void_array();
}

extern "C" void clinical_indices_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse(js);

  size = d.Size();

  for ( int i = 0; i < size; i++ )
  {
    clinical_index *ci;

    CREATE( ci, clinical_index, 1 );
    ci->claimed = NULL;
    clinical_index_from_js( ci, d[i] );
    LINK( ci, first_clinical_index, last_clinical_index, next );
  }
}

void pubmed_from_js( pubmed *p, Value &v )
{
  p->id = strdup( v["id"].GetString() );
  p->title = strdup( v["title"].GetString() );
}

extern "C" void pubmeds_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse(js);

  size = d.Size();

  for ( int i = 0; i < size; i++ )
  {
    pubmed *p = pubmed_by_id( d[i]["id"].GetString() );

    if ( p )
      MULTIFREE( p->id, p->title );
    else
    {
      CREATE( p, pubmed, 1 );
      LINK( p, first_pubmed, last_pubmed, next );
    }

    pubmed_from_js( p, d[i] );
  }
}

void correlation_from_js( Value &v )
{
  correlation *c;
  variable **vbl, **vblptr;
  pubmed *pbmd;
  char *idstr, *pubmedstr, *comment;
  int id, vrcnt, no = 0;

  idstr = strdup( v["id"].GetString() );

  id = strtoul( idstr, NULL, 10 );

  if ( id < 1 )
  {
    error_messagef( "Error loading correlation: invalid id %s", idstr );
    free( idstr );
    return;
  }
  free( idstr );

  if ( v.HasMember("comment") && v["comment"].IsString() )
    comment = strdup( v["comment"].GetString() );
  else
    comment = NULL;

  pubmedstr = strdup( v["pubmed"]["id"].GetString() );

  pbmd = pubmed_by_id_or_create( pubmedstr, &no );

  if ( !pbmd )
  {
    error_messagef( "Error loading correlation %d: invalid pubmed %s", id, pubmedstr );
    free( pubmedstr );
    return;
  }
  free( pubmedstr );

  vrcnt = v["variables"].Size();

  CREATE( vbl, variable *, vrcnt + 1 );
  vblptr = vbl;

  for ( int i = 0; i < vrcnt; i++ )
  {
    Value &varjs = v["variables"][i];
    variable *newvar;
    clinical_index *ci;
    lyph *e;
    char *typestr, *qual;
    int type;

    typestr = strdup( varjs["type"].GetString() );

    if ( !strcmp( typestr, "clinical index" ) )
    {
      ci = clinical_index_by_index( varjs["clindex"].GetString() );

      if ( !ci )
      {
        error_messagef( "Error loading correlation %d: invalid clinical index %s", id, varjs["clindex"].GetString() );
        return;
      }

      e = NULL;
      qual = NULL;
      type = VARIABLE_CLINDEX;
    }
    else if ( !strcmp( typestr, "located measure" ) )
    {
      e = lyph_by_id( varjs["location"].GetString() );

      if ( !e )
      {
        error_messagef( "Error loading correlation %d: a variable has invalid lyph %s", id, varjs["location"].GetString() );
        return;
      }

      qual = strdup( varjs["quality"].GetString() );
      ci = NULL;
      type = VARIABLE_LOCATED;
    }
    else
    {
      error_messagef( "Error loading correlation %d: a variable has invalid type %s", id, typestr );
      free( typestr );
      return;
    }
    free( typestr );

    CREATE( newvar, variable, 1 );
    newvar->type = type;
    newvar->ci = ci;
    newvar->quality = qual;
    newvar->loc = e;

    *vblptr++ = newvar;
  }
  *vblptr = NULL;

  CREATE( c, correlation, 1 );
  c->vars = vbl;
  c->pbmd = pbmd;
  c->id = id;
  c->comment = comment;

  LINK2( c, first_correlation, last_correlation, next, prev );
}

void bop_from_js( Value &v )
{
  bop *b;
  lyph **excluded;
  added_edge **added;
  located_measure **measures;
  char *excludedstr, *addedstr, *measurestr, *err = NULL;
  int id;

  excludedstr = strdup( v["excluded"].GetString() );
  addedstr = strdup( v["added"].GetString() );
  measurestr = strdup( v["measures"].GetString() );
  id = v["id"].GetInt();

  excluded = (lyph**)PARSE_LIST( excludedstr, lyph_by_id, NULL, &err );

  if ( !excluded )
  {
    error_messagef( "Error loading bop %d: unrecognized excluded lyph(s)", id );
    excluded = (lyph**)blank_void_array();
    err = NULL;
  }

  added = (added_edge**)PARSE_LIST( addedstr, added_edge_by_notation, NULL, &err );

  if ( !added )
  {
    error_messagef( "Error loading bop %d: one of the added edges did not resolve", id );
    added = (added_edge**)blank_void_array();
    err = NULL;
  }

  measures = (located_measure**)PARSE_LIST( measurestr, located_measure_by_id, NULL, &err );

  if ( !measures )
  {
    error_messagef( "Error loading bop %d: one of the located measures did not resolve", id );
    measures = (located_measure**)blank_void_array();
    err = NULL;
  }

  free( measurestr );
  free( addedstr );
  free( excludedstr );

  CREATE( b, bop, 1 );
  b->id = id;
  b->measures = measures;
  b->added = added;
  b->excluded = excluded;

  LINK2( b, first_bop, last_bop, next, prev );
}

void located_measure_from_js( Value &v )
{
  located_measure *m;
  lyph *e;
  char *lyphstr, *idstr;
  int id;

  lyphstr = strdup( v["lyph"].GetString() );

  e = lyph_by_id( lyphstr );

  if ( !e )
  {
    error_messagef( "Error while loading located measures: lyph %s not found", lyphstr );
    free( lyphstr );

    return;
  }

  free( lyphstr );

  idstr = strdup( v["id"].GetString() );

  id = strtoul( idstr, NULL, 10 );

  if ( id < 1 )
  {
    error_messagef( "Error while loading located measures: located measure with invalid ID %s", idstr );
    free( idstr );
    return;
  }
  free( idstr );

  CREATE( m, located_measure, 1 );
  m->quality = strdup( v["quality"].GetString() );
  m->id = id;
  m->loc = e;

  LINK2( m, first_located_measure, last_located_measure, next, prev );
}

extern "C" void correlations_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse( js );

  size = d.Size();

  for ( int i = 0; i < size; i++ )
    correlation_from_js( d[i] );

  return;
}

extern "C" void located_measures_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse( js );

  size = d.Size();

  for ( int i = 0; i < size; i++ )
    located_measure_from_js( d[i] );
}

extern "C" void bops_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse( js );

  size = d.Size();

  for ( int i = 0; i < size; i++ )
    bop_from_js( d[i] );
}
