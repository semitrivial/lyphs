#include "nt_parse_internal.h"
#include "nt_parse.h"

int parse_ntriples( FILE *fp, char **err, int max_iri_len, ADD_TRIPLE_FUNCTION *fnc )
{
  char c, *end, *bptr;
  char *subj, *pred, *obj;
  int line = 1, fQuote = 0, fBrace = 0, fUnderscore = 0, fSubj = 0, fPred = 0, fObj = 0, fresh_line = 1, whitespaceable = 1, fAnything = 0;
  static char *buf;
  static int bufsize;

  #ifndef WIN32
    // Variables for QUICK_GETC
    char read_buf[READ_BLOCK_SIZE], *read_end = &read_buf[READ_BLOCK_SIZE], *read_ptr = read_end;
    int fread_len;
  #endif

  if ( bufsize < max_iri_len + 1 )
  {
    if ( buf )
      free( buf );

    buf = malloc( max_iri_len + 1 );
    bufsize = max_iri_len + 1;
  }
  bptr = buf;
  end = &buf[max_iri_len];

  if ( !fnc )
    fnc = do_nothing_function;

  for ( ; ; )
  {
    QUICK_GETC( c, fp );
    main_parse_ntriples_loop:

    if ( c == '\\' )
    {
      char next;

      QUICK_GETC( next, fp );
      if ( !next )
        PARSE_NTRIPS_ERROR( "File ended with a backslash" );

      if ( !add_char( '\\', bptr++, end, line, err )
      ||   !add_char( next, bptr++, end, line, err ) )
        return 0;

      fAnything = 1;

      continue;
    }

    if ( !c )
      break;

    if ( fQuote )
    {
      if ( c == '\n' )
        PARSE_NTRIPS_ERROR( "Line ends with unterminated quote" );

      if ( !add_char( c, bptr++, end, line, err ) )
        return 0;

      if ( c == '"' )
        fQuote = 0;

      continue;
    }

    if ( fBrace )
    {
      if ( c == '\n' )
        PARSE_NTRIPS_ERROR( "Line ends with unmatched opening-brace, <" );

      if ( !add_char( c, bptr++, end, line, err ) )
        return 0;

      if ( c == '>' )
        fBrace = 0;

      continue;
    }

    if ( fUnderscore )
    {
      if ( c == '\n' )
        PARSE_NTRIPS_ERROR( "Line ends with blank node (and no .)" );

      if ( c != ' ' )
      {
        if ( !add_char( c, bptr++, end, line, err ) )
          return 0;

        continue;
      }

      fUnderscore = 0;
      goto main_parse_ntriples_loop;
    }

    if ( c == '^' )
    {
      /*
       * Ignore ^^<IRI>
       */
      QUICK_GETC( c, fp );

      if ( c != '^' )
        PARSE_NTRIPS_ERROR( "Line has stray caret (^)" );

      do
      {
        QUICK_GETC( c, fp );
        if ( !c )
          PARSE_NTRIPS_ERROR( "Line has stray caret (^)?" );
      }
      while ( c != ' ' );

      goto main_parse_ntriples_loop;
    }

    if ( whitespaceable )
    {
      if ( c == ' ' || c == '\t' )
        continue;

      if ( c == '#' && fresh_line )
      {
        do
          QUICK_GETC( c, fp );
        while ( c && c != '\n' );

        line++;
        continue;
      }

      fresh_line = 0;
      whitespaceable = 0;
    }

    if ( c == ' ' || c == '\t' || ( c == '.' && fPred ) )
    {
      whitespaceable = 1;

      if ( !fSubj )
        set_iri( &subj, &fSubj, buf, bptr );
      else if ( !fPred )
        set_iri( &pred, &fPred, buf, bptr );
      else if ( !fObj )
      {
        int fPeriod = (c=='.');

        set_iri( &obj, &fObj, buf, bptr );

        for( ; ; )
        {
          QUICK_GETC( c, fp );

          if ( !c || c == '\n' )
          {
            if ( !fPeriod )
              PARSE_NTRIPS_ERROR( "This line seems to be missing the ending period (.)" );

            break;
          }

          switch(c)
          {
            case ' ':
            case '\t':
              continue;

            case '.':
              if ( !fPeriod )
              {
                fPeriod = 1;
                continue;
              }
            default:
              PARSE_NTRIPS_ERROR( "Unexpected character after subject, predicate, and object" );
          }
        }

        line++;

        (fnc)( subj, pred, obj );

        fSubj = fPred = fObj = 0;
        fresh_line = 1;
        fAnything = 0;
      }

      bptr = buf;
      continue;
    }

    if ( c == '"' )
    {
      if ( !add_char( c, bptr++, end, line, err ) )
        return 0;

      fQuote = 1;
      fAnything = 1;
      continue;
    }

    if ( c == '<' )
    {
      if ( !add_char( c, bptr++, end, line, err ) )
        return 0;

      fBrace = 1;
      fAnything = 1;
      continue;
    }

    if ( c == '_' )
    {
      if ( !add_char( c, bptr++, end, line, err ) )
        return 0;

      fUnderscore = 1;
      fAnything = 1;
      continue;
    }

    if ( c == '\n' )
    {
      if ( !fAnything )
      {
        line++;
        whitespaceable = 1;
        fresh_line = 1;
        continue;
      }
      PARSE_NTRIPS_ERROR( "Line seems to end prematurely" );
    }

    PARSE_NTRIPS_ERROR( "Line appears to contain an unexpected character not enclosed in quotes or in <>" );
  }

  return 1;
}

int add_char( char c, char *bptr, char *end, int line, char **err )
{
  if ( bptr >= end )
    PARSE_NTRIPS_ERROR( "An IRI exceeded the maximum IRI length" );

  *bptr = c;

  return 1;
}

void set_iri( char **which, int *f, char *buf, char *bptr )
{
  *bptr = '\0';
  *which = strdup( buf );
  *f = 1;
}

void do_nothing_function( char *subj, char *pred, char *obj )
{
  free( subj );
  free( pred );
  free( obj );

  return;
}

