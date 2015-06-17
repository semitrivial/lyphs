#include "lyph.h"

ucl_syntax *parse_ucl_syntax( char *ucl, char **err, char **maybe_err, ambig **ambig_head, ambig **ambig_tail )
{
  char *end = &ucl[strlen(ucl)-1], *ptr, *shorturl, *unshort;
  ucl_syntax *s;
  int lparens, token_ready;
  trie *t;

  if ( end < ucl )
  {
    *err = strdup( "Empty expression or sub-expression" );
    return NULL;
  }

  while ( *ucl == ' ' )
    ucl++;

  while ( *end == ' ' && end > ucl )
    end--;

  if ( ucl >= end )
  {
    *err = strdup( "Empty expression or sub-expression" );
    return NULL;
  }

  end[1] = '\0';

  if ( *ucl == '(' && *end == ')' )
  {
    *end = '\0';

    CREATE( s, ucl_syntax, 1 );
    s->type = UCL_SYNTAX_PAREN;
    s->sub1 = parse_ucl_syntax( &ucl[1], err, maybe_err, ambig_head, ambig_tail );

    if ( !s->sub1 )
    {
      free( s );
      return NULL;
    }

    CREATE( s->toString, char, strlen( s->sub1->toString ) + strlen( "()" ) + 1 );

    sprintf( s->toString, "(%s)", s->sub1->toString );

    return s;
  }

  if ( str_approx( ucl, "not" ) )
  {
    CREATE( s, ucl_syntax, 1 );
    s->type = UCL_SYNTAX_NOT;
    s->sub1 = parse_ucl_syntax( &ucl[strlen("not")], err, maybe_err, ambig_head, ambig_tail );

    if ( !s->sub1 )
    {
      free( s );
      return NULL;
    }

    CREATE( s->toString, char, strlen( s->sub1->toString ) + strlen( "not " ) + 1 );

    sprintf( s->toString, "not %s", s->sub1->toString );

    return s;
  }

  for ( ptr = ucl, lparens = token_ready = 0; *ptr; ptr++ )
  {
    if ( lparens && *ptr != '(' && *ptr != ')' )
      continue;

    switch( *ptr )
    {
      default:
        token_ready = 0;
        break;

      case '(':
        lparens++;
        token_ready = 0;
        break;

      case ')':
        if ( !lparens )
        {
          *err = strdup( "Mismatched right parenthesis" );
          return NULL;
        }

        lparens--;
        token_ready = 1;
        break;

      case ' ':
        token_ready = 1;
        break;

      case 's':
      case 'a':
      case 'o':
        if ( !token_ready )
          break;

        token_ready = 0;

        if ( ptr == ucl )
          break;

        if ( *ptr == 's' && str_approx( ptr, "some" ) )
        {
          if ( ptr == ucl )
          {
            *err = strdup( "Encountered 'some' with no relation" );
            return NULL;
          }

          CREATE( s, ucl_syntax, 1 );
          s->type = UCL_SYNTAX_SOME;
          s->sub1 = parse_ucl_syntax( &ptr[strlen("some")], err, maybe_err, ambig_head, ambig_tail );

          if ( !s->sub1 )
          {
            free( s );
            return NULL;
          }

          s->reln = read_some_relation( ucl, &ptr[-1] );

          CREATE( s->toString, char, strlen( s->reln ) + strlen( " some " ) + strlen( s->sub1->toString ) + 1 );
          sprintf( s->toString, "%s some %s", s->reln, s->sub1->toString );

          return s;
        }

        if ( ( *ptr == 'a' && str_approx( ptr, "and" ) )
        ||   ( *ptr == 'o' && str_approx( ptr, "or" ) ) )
        {
          if ( ptr == ucl )
            return NULL;

          CREATE( s, ucl_syntax, 1 );
          s->type = (*ptr == 'a') ? UCL_SYNTAX_AND : UCL_SYNTAX_OR;

          if ( *ptr == 'a' )
            s->sub2 = parse_ucl_syntax( &ptr[strlen("and")], err, maybe_err, ambig_head, ambig_tail );
          else
            s->sub2 = parse_ucl_syntax( &ptr[strlen("or")], err, maybe_err, ambig_head, ambig_tail );

          if ( !s->sub2 )
          {
            free( s );
            return NULL;
          }

          *ptr = '\0';
          s->sub1 = parse_ucl_syntax( ucl, err, maybe_err, ambig_head, ambig_tail );

          if ( !s->sub1 )
          {
            kill_ucl_syntax( s->sub2 );
            free( s );
            return NULL;
          }

          CREATE( s->toString, char, strlen( s->sub1->toString ) + strlen( " and " ) + strlen( s->sub2->toString ) + 1 );

          sprintf( s->toString, "%s %s %s", s->sub1->toString, (s->type == UCL_SYNTAX_AND) ? "and" : "or", s->sub2->toString );

          return s;
        }
        break;
    }
  }

  t = trie_search( ucl, label_to_iris_lowercase );

  if ( t && t->data && *t->data )
  {
    CREATE( s, ucl_syntax, 1 );

    if ( !is_ambiguous(t->data) )
    {
      unshort = trie_to_static( *t->data );
      shorturl = get_url_shortform( unshort );
      s->toString = strdup( shorturl ? shorturl : unshort );
    }
    else
    {
      ambig *a;

      s->toString = strdup( "[Ambiguous]" );

      for ( a = *ambig_head; a; a = a->next )
        if ( a->data == t->data && !strcmp( a->label, ucl ) )
          break;

      if ( !a )
      {
        CREATE( a, ambig, 1 );
        a->data = t->data;
        a->label = strdup( ucl );
        LINK2( a, *ambig_head, *ambig_tail, next, prev );
      }
    }
  }
  else
  {
    t = trie_search( ucl, iri_to_labels );

    if ( t )
    {
      CREATE( s, ucl_syntax, 1 );
      unshort  = trie_to_static( t );
      shorturl = get_url_shortform( unshort );
      s->toString = strdup( shorturl ? shorturl : unshort );
    }
    else
    {
      CREATE( s, ucl_syntax, 1 );
      s->toString = strdup( ucl );

      if ( !*maybe_err )
      {
        CREATE( *maybe_err, char, strlen(ucl) + strlen( "Unrecognized term: " ) );
        sprintf( *maybe_err, "Unrecognized term: %s", ucl );
      }
    }
  }

  s->type = UCL_SYNTAX_BASE;
  s->iri = t;

  return s;
}

int str_approx( char *full, char *init )
{
  char *fptr = full, *iptr = init;

  for (;;)
  {
    if ( !*fptr && !*iptr )
      return 1;

    if ( !*fptr )
      return 0;

    if ( !*iptr )
      return ( !*fptr || (*fptr==' ') || (*fptr=='(') );

    if ( LOWER( *fptr ) != LOWER( *iptr ) )
      return 0;

    fptr++;
    iptr++;
  }
}

int str_begins( const char *full, const char *init )
{
  const char *fptr = full, *iptr = init;

  for (;;)
  {
    if ( !*fptr && !*iptr )
      return 1;

    if ( !*fptr )
      return 0;

    if ( !*iptr )
      return 1;

    if ( LOWER( *fptr ) != LOWER( *iptr ) )
      return 0;

    fptr++;
    iptr++;
  }
}

char *read_some_relation( char *left, char *right )
{
  if ( left == right )
    return "";

  while ( *left == ' ' )
  {
    left++;

    if ( left == right )
      return "";
  }

  while ( right > left && right[-1] == ' ' )
  {
    right--;

    if ( left == right )
      return "";
  }

  if ( *left == '(' && *right == ')' )
    return read_some_relation( &left[1], right-1 );

  *right = '\0';

  return left;
}

int is_ambiguous( trie **data )
{
  char buf[MAX_STRING_LEN];
  trie **ptr, **left;

  if ( !data[0] || !data[1] )
    return 0;

  for ( ptr = &data[1]; *ptr; ptr++ )
  {
    sprintf( buf, "%s", trie_to_static( *ptr ) );

    for ( left = data; left < ptr; left++ )
    {
      if ( strcmp( trie_to_static( *left ), buf ) )
        return 1;
    }
  }

  return 0;
}

void free_ambigs( ambig *head )
{
  ambig *a, *a_next;

  for ( a = head; a; a = a_next )
  {
    a_next = a->next;
    free( a->label );
    free( a );
  }
}

char *ucl_syntax_output( ucl_syntax *s, ambig *head, ambig *tail, char *possible_error )
{
  int len;
  ambig *a;
  trie **data;
  char *buf, *bptr;

  len = strlen( "{\n  \"Result\": \"\",\n  \"Ambiguities\":\n  [\n  ],\n}" );

  len += strlen( s->toString );

  if ( possible_error )
    len += strlen( possible_error ) + strlen( "  \"Possible_error\": \"\"\n" );

  for ( a = head; a; a = a->next )
  {
    len += strlen( "    {\n      \"label\": \"\",\n      \"options\":\n      [\n      ]\n    },\n" );
    len += strlen( a->label );

    for ( data = a->data; *data; data++ )
    {
      len += strlen( "        \"\",\n" );
      len += strlen( trie_to_static( *data ) );
    }
  }

  CREATE( buf, char, len+1 );

  sprintf( buf, "{\n  \"Result\": \"%s\",\n  \"Ambiguities\":\n  [\n", s->toString );

  for ( a = head, bptr = &buf[strlen(buf)]; a; a = a->next )
  {
    sprintf( bptr, "    {\n      \"label\": \"%s\",\n      \"options\":\n      [\n", a->label );
    bptr = &bptr[strlen(bptr)];

    for ( data = a->data; *data; data++ )
    {
      sprintf( bptr, "        \"%s\"%s\n", trie_to_static( *data ), data[1] ? "," : "" );
      bptr = &bptr[strlen(bptr)];
    }

    sprintf( bptr, "      ]\n    }%s\n", a != tail ? "," : "" );
    bptr = &bptr[strlen(bptr)];
  }

  if ( possible_error )
    sprintf( bptr, "  ],\n  \"Possible_error\": \"%s\"\n}", possible_error );
  else
    sprintf( bptr, "  ]\n}" );

  return buf;
}

/*
{
  "Result": "...",
  "Ambiguities":
  [
    {
      "label": "...",
      "options":
      [
        "...",
        "..."
      ]
    },
    {
      "label": "...",
      "options":
      [
        "...",
        "..."
      ]
    }
  ],
  Possible_error: "..."
}
*/
