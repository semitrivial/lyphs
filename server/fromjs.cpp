#include <cstdio>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <iostream>

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
  if ( d.HasMember( key ) )
    return array_from_value( d[key], fnc );
  else
    return blank_void_array();
}

trie *trie_from_doc( Document &d, const char *key, trie *base )
{
  if ( d.HasMember( key ) )
    return trie_search( d[key].GetString(), base );
  else
    return NULL;
}

void int_from_doc( Document &d, const char *key, int *dest )
{
  if ( d.HasMember( key ) )
    *dest = d[key].GetInt();
}

void clinical_index_from_js( clinical_index *ci, Value &v )
{
  ci->index = trie_strdup( v["index"].GetString(), metadata );
  ci->label = trie_strdup( v["label"].GetString(), metadata );
  ci->pubmeds = (pubmed**)array_from_doc( v, "pubmeds", CST(pubmed_by_id_or_create) );

  if ( v.HasMember("claimed") )
    ci->claimed = strdup( v["claimed"].GetString() );
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

void bulk_annot_from_js( bulk_annot *b, Value &v )
{
  static int idpos = strlen("BULK_ANNOT_");
  const char *id = v["id"].GetString();

  b->id = trie_strdup( id, metadata );
  b->type = stoi( v["type"].GetString() );
  b->lyphs = (lyph**)array_from_doc( v, "lyph", CST( lyph_by_id ) );

  maybe_update_top_id( &top_bulk_annot_id, &id[idpos] );

  if ( v.HasMember("clinical index") )
    b->ci = clinical_index_by_index( v["clinical index"].GetString() );
  else
    b->ci = NULL;

  if ( v.HasMember("radiological index") )
    b->radio_index = trie_strdup( v["radiological index"].GetString(), metadata );

  b->pbmd = pubmed_by_id( v["pubmed"].GetString() );
}

extern "C" void bulk_annots_from_js( const char *js )
{
  Document d;
  int size;

  d.Parse(js);
  size = d.Size();

  for ( int i = 0; i < size; i++ )
  {
    bulk_annot *b = bulk_annot_by_id( d[i]["id"].GetString() );

    if ( !b )
      CREATE( b, bulk_annot, 1 );

    bulk_annot_from_js( b, d[i] );
    LINK( b, first_bulk_annot, last_bulk_annot, next );
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
