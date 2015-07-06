#ifndef LYPH_MACRO_DOT_H_INCLUDE_GUARD
#define LYPH_MACRO_DOT_H_INCLUDE_GUARD

/*
 * Memory allocation macros
 */
#define CREATE(result, type, number)\
do\
{\
    if (!((result) = (type *) calloc ((number), sizeof(type))))\
    {\
        fprintf(stderr, "Malloc failure at %s:%d\n", __FILE__, __LINE__ );\
        to_logfile( "Malloc failure at %s:%d\n", __FILE__, __LINE__ );\
        abort();\
    }\
} while(0)

#define MULTIFREE(...)\
do\
{\
  multifree(__VA_ARGS__, NULL);\
}\
while(0)

/*
 * Bit-twiddling macros
 */
#define SET_BIT( flags, bit )\
do\
  (flags) |= (bit);\
while(0)

#define IS_SET( flags, bit ) ( (flags) & (bit) )

#define REMOVE_BIT( flags, bit )\
do\
  (flags) &= ~(bit);\
while(0)

/*
 * Generic trie recursion
 */
#define TRIE_RECURSE( code )\
do\
{\
  if ( t->children )\
  {\
    trie **child;\
    for ( child = t->children; *child; child++ )\
    {\
      code ;\
    }\
  }\
}\
while(0)

/*
 * Link object into singly-linked list
 */
#define LINK( link, first, last, next )\
do\
{\
  (link)->next = NULL;\
  if ( last )\
    (last)->next = (link);\
  else\
    (first) = (link);\
  (last) = (link);\
} while(0)

/*
 * Link object into doubly-linked list
 */
#define LINK2(link, first, last, next, prev)\
do\
{\
   if ( !(first) )\
   {\
      (first) = (link);\
      (last) = (link);\
   }\
   else\
      (last)->next = (link);\
   (link)->next = NULL;\
   if (first == link)\
      (link)->prev = NULL;\
   else\
      (link)->prev = (last);\
   (last) = (link);\
} while(0)

/*
 * Unlink object from doubly-linked list
 */
#define UNLINK2(link, first, last, next, prev)\
do\
{\
        if ( !(link)->prev )\
        {\
         (first) = (link)->next;\
           if ((first))\
              (first)->prev = NULL;\
        }\
        else\
        {\
         (link)->prev->next = (link)->next;\
        }\
        if ( !(link)->next )\
        {\
         (last) = (link)->prev;\
           if ((last))\
              (last)->next = NULL;\
        }\
        else\
        {\
         (link)->next->prev = (link)->prev;\
        }\
} while(0)

/*
 * Insert object into doubly-linked list
 */
#define INSERT2(link, insert, first, next, prev)\
do\
{\
  (link)->prev = (insert)->prev;\
  if ( !(insert)->prev )\
    (first) = (link);\
  else\
    (insert)->prev->next = (link);\
  (insert)->prev = (link);\
  (link)->next = (insert);\
}\
while(0)\

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

#define QUICK_GETLINE( buffer, bufferptr, c, fp )\
do\
{\
  for( bufferptr = buffer; ;)\
  {\
    QUICK_GETC( c, fp );\
    if ( !c )\
    {\
      if ( buffer == bufferptr )\
        bufferptr = NULL;\
      else\
        *bufferptr = '\0';\
      \
      break;\
    }\
    \
    if ( c == '\n' )\
    {\
      *bufferptr = '\0';\
      break;\
    }\
    \
    if ( c != '\r' )\
      *bufferptr++ = c;\
  }\
}\
while(0)

/*
 * Timing macros
 */
#define TIMING_VARS struct timespec timespec1, timespec2

#define BEGIN_TIMING \
do\
{\
  clock_gettime(CLOCK_MONOTONIC, &timespec1 );\
}\
while(0)

#define END_TIMING \
do\
{\
  clock_gettime(CLOCK_MONOTONIC, &timespec2 );\
}\
while(0)

//#define TIMING_RESULT ( (timespec2.tv_sec - timespec1.tv_sec) * 1e+6 + (double) (timespec2.tv_nsec - timespec1.tv_nsec) * 1e-3 )
#define TIMING_RESULT ( (timespec2.tv_sec - timespec1.tv_sec) + (double) (timespec2.tv_nsec - timespec1.tv_nsec) * 1e-9 )

/*
 * Char-twiddling macros
 */
#define LOWER(x) ( ( (x) >= 'A' && (x) <= 'Z' )? x - 'A' + 'a' : x )

/*
 * Testing macro, see: https://github.com/semitrivial/Apitest/
 */
#define Apitest(...) do {} while(0)

/*
 * Misc. macros
 */
#define HANDLER(fnc) void fnc( char *request, http_request *req, url_param **params )

#define VOIDLEN(x) voidlen((void**)(x))

#define COPY_VOID_ARRAY(x) copy_void_array((void**)x)

#define EXIT() do exit(EXIT_SUCCESS); while(0)

#define PARSE_LIST( list, fnc, name, err ) parse_list( (list), (void * (*) (char*))(fnc), (name), (err) )

#define PARSE_LIST_R( list, fnc, data, name, err ) parse_list_r( (list), (void * (*) (char*,void*))(fnc), (void*)(data), (name), (err) );

#endif //LYPH_MACRO_DOT_H_INCLUDE_GUARD
