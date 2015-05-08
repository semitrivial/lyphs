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
  ci->pubmeds = (pubmed**)array_from_doc( v, "pubmeds", CST(pubmed_by_id) );
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
    clinical_index_from_js( ci, d[i] );
    LINK( ci, first_clinical_index, last_clinical_index, next );
  }
}

void lyphplate_from_document( Document &d, lyphplate **Lptr )
{
  lyphplate *L = *Lptr;

  L->supers = NULL;
  L->subs = NULL;
  L->flags = 0;

  L->misc_material = (lyphplate**)array_from_doc( d, "misc_material", CST(lyphplate_by_id) );

  L->layers = (layer**)array_from_doc( d, "layers", CST(layer_by_id) );

  L->ont_term = trie_from_doc( d, "ont_term", superclasses );

  L->name = trie_from_doc( d, "name", lyphplate_names );
  L->name->data = (trie**)L;

  L->id = trie_from_doc( d, "id", lyphplate_ids );
  L->id->data = (trie**)L;

  int_from_doc( d, "type", &L->type );
}

extern "C" lyphplate *lyphplate_from_json( char *json )
{
  Document d;
  d.Parse(json);

  lyphplate *L = (lyphplate*) malloc(sizeof(lyphplate));

  lyphplate_from_document( d, &L );

  return L;
}
