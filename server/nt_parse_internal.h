int add_char( char c, char *bptr, char *end, int line, char **err );
void set_iri( char **which, int *f, char *buf, char *bptr );
void do_nothing_function( char *subj, char *pred, char *obj );

#define READ_BLOCK_SIZE 65536

#define PARSE_NTRIPS_ERROR( txt )\
  do\
  {\
    if ( err )\
    {\
      static char errbuf[1024];\
      \
      sprintf( errbuf, "(Line %d) %s", \
        line, txt );\
      *err = errbuf;\
    }\
    \
    return 0;\
  }\
  while(0)

#ifndef QUICK_GETC
  #ifdef WIN32

    #define QUICK_GETC( ch, fp )\
    do\
    {\
      if ( feof(fp) )\
        ch = '\0';\
      else\
        ch = getc(fp);\
    }\
    while(0)

  #else
    /*
     * Quickly read char from file
     */
    #define QUICK_GETC( ch, fp )\
    do\
    {\
      if ( read_ptr == read_end )\
      {\
        fread_len = fread( read_buf, sizeof(char), READ_BLOCK_SIZE, fp );\
        if ( fread_len < READ_BLOCK_SIZE )\
          read_buf[fread_len] = '\0';\
        read_ptr = read_buf;\
      }\
      ch = *read_ptr++;\
    }\
    while(0)

  #endif

#endif
