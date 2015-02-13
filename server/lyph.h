#include "mallocf.h"
#include "macro.h"
#include "jsonfmt.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define MAX_STRING_LEN 64000
#define READ_BLOCK_SIZE 1048576  // 1024 * 1024.  For QUICK_GETC.
#define MAX_IRI_LEN 2048
#define MAX_AUTOCOMPLETE_RESULTS_PRESORT 30
#define MAX_AUTOCOMPLETE_RESULTS_POSTSORT 10
#define MAX_URL_PARAMS 32
#define MAX_URL_PARAM_LEN 512
#define MAX_LYPHEDGE_LINE_LEN (MAX_IRI_LEN * 3)
#define MAX_INT_LEN (strlen("-2147483647"))

/*
 * Typedefs
 */
typedef char * (*json_array_printer) (void *what, void *how);
typedef struct TRIE trie;
typedef struct TRIE_WRAPPER trie_wrapper;
typedef struct UCL_SYNTAX ucl_syntax;
typedef struct AMBIG ambig;
typedef struct LYPH lyph;
typedef struct LAYER layer;
typedef struct LAYER_LOADING layer_loading;
typedef struct LOAD_LAYERS_DATA load_layers_data;
typedef struct LYPHNODE lyphnode;
typedef struct LYPHEDGE lyphedge;
typedef struct EXIT_DATA exit_data;
typedef struct LYPHSTEP lyphstep;
typedef struct LYPHVIEW lyphview;

/*
 * Structures
 */
struct TRIE
{
  trie *parent;
  char *label;
  trie **children;
  trie **data;
};

struct TRIE_WRAPPER
{
  trie_wrapper *next;
  trie_wrapper *prev;
  trie *t;
};

struct UCL_SYNTAX
{
  int type;
  ucl_syntax *sub1;
  ucl_syntax *sub2;
  char *reln;
  trie *iri;
  char *toString;
};

typedef enum
{
  UCL_BLANK, UCL_SYNTAX_BASE, UCL_SYNTAX_PAREN, UCL_SYNTAX_SOME, UCL_SYNTAX_AND, UCL_SYNTAX_OR, UCL_SYNTAX_NOT
} ucl_syntax_types;

struct AMBIG
{
  ambig *next;
  ambig *prev;
  trie **data;
  char *label;
};

struct LYPH
{
  trie *name;
  trie *id;
  int type;
  layer **layers;
};

typedef enum
{
  LYPH_BASIC, LYPH_SHELL, LYPH_MIX, LYPH_MISSING
} lyph_types;

struct LAYER
{
  lyph *material;
  char *color;
  int thickness;
  trie *id;
};

struct LYPHNODE
{
  trie *id;
  int flags;
  exit_data **exits;
};

typedef enum
{
  LYPHNODE_SEEN = 1, LYPHNODE_SELECTED = 2
} lyphnode_flags;

typedef enum
{
  LTJ_EXITS = 1, LTJ_SELECTIVE = 2, LTJ_FULL_EXIT_DATA = 4
} lyphnode_to_json_flag_types;

typedef enum
{
  ETJ_FULL_EXIT_DATA = 1
} exit_to_json_flag_types;

struct LYPHEDGE
{
  trie *id;
  trie *name;
  int type;
  lyphnode *from;
  lyphnode *to;
  lyph *lyph;
  trie *fma;
};

typedef enum
{
  LYPHEDGE_ARTERIAL, LYPHEDGE_MICROCIRC, LYPHEDGE_VENOUS, LYPHEDGE_CARDIAC
} lyphedge_types;

struct EXIT_DATA
{
  lyphnode *to;
  lyphedge *via;
};

struct LYPHSTEP
{
  lyphstep *next;
  lyphstep *prev;
  int depth;
  lyphstep *backtrace;
  lyphnode *location;
  lyphedge *edge;
};

struct LYPHVIEW
{
  int id;
  lyphnode **nodes;
  char **coords;
};

struct LOAD_LAYERS_DATA
{
  lyph *subj;
  layer_loading *first_layer_loading;
  layer_loading *last_layer_loading;
  int layer_count;
};

struct LAYER_LOADING
{
  layer_loading *next;
  layer_loading *prev;
  layer *lyr;
};

/*
 * Global variables
 */
extern trie *iri_to_labels;
extern trie *label_to_iris;
extern trie *label_to_iris_lowercase;

extern trie *lyph_names;
extern trie *lyph_ids;
extern trie *layer_ids;
extern trie *lyphnode_ids;
extern trie *lyphedge_ids;
extern trie *lyphedge_fmas;
extern trie *lyphedge_names;

extern trie *subclasses;
extern trie *superclasses;

/*
 * Function prototypes
 */

/*
 * labels.c
 */
void init_labels(void);
void parse_labels_file(FILE *fp);
void add_labels_entry( char *iri_ch, char *label_ch );
void add_subclass_entry( char *child_ch, char *parent_ch );
trie **get_labels_by_iri( char *iri_ch );
trie **get_iris_by_label( char *label_ch );
trie **get_iris_by_label_case_insensitive( char *label_ch );
trie **get_autocomplete_labels( char *label_ch, int case_insens );

/*
 * srv.c
 */
void main_loop(void);

/*
 * trie.c
 */
trie *blank_trie(void);
trie *trie_strdup( char *buf, trie *base );
trie *trie_search( char *buf, trie *base );
char *trie_to_static( trie *t );
char *trie_to_json( trie *t );
void trie_search_autocomplete( char *label_ch, trie **buf, trie *base );
int cmp_trie_data (const void * a, const void * b);
void **datas_to_array( trie *t );
int count_nontrivial_members( trie *t );

/*
 * util.c
 */
void log_string( char *txt );
void log_linenum( int linenum );
char *html_encode( char *str );
void init_html_codes( void );
char *lowercaserize( char *x );
char *get_url_shortform( char *iri );
char *url_decode(char *str);
int is_number( const char *arg );
void error_message( char *err );
char *pretty_free( char *json );
char *strdupf( const char *fmt, ... );
char *jsonf( int paircnt, ... );;
char *jslist_r( json_array_printer *p, void **array, void *param );
void json_gc( void );
size_t voidlen( void **x );

/*
 * ucl.c
 */
ucl_syntax *parse_ucl_syntax( char *ucl, char **err, char **maybe_err, ambig **ambig_head, ambig **ambig_tail );
int str_approx( char *full, char *init );
int str_begins( char *full, char *init );
char *read_some_relation( char *left, char *right );
void kill_ucl_syntax( ucl_syntax *s );
int is_ambiguous( trie **data );
void free_ambigs( ambig *head );
char *ucl_syntax_output( ucl_syntax *s, ambig *head, ambig *tail, char *possible_error );

/*
 * lyph.c
 */
lyph *lyph_by_name( char *name );
lyph *lyph_by_id( char *id );
char *lyph_to_json( lyph *L );
char *layer_to_json( layer *lyr );
lyphview *lyphview_by_id( char *idstr );
char *lyphnode_to_json( lyphnode *n );
char *lyphedge_to_json( lyphedge *e );
char *lyphpath_to_json( lyphedge **path );
char *exit_to_json( exit_data *x );
layer *layer_by_id( char *id );
layer *layer_by_description( char *mtid, int thickness, char *color );
layer *layer_by_description_recurse( const lyph *L, const float thickness, const char *color, const trie *t );
lyphnode *lyphnode_by_id( char *id );
lyphnode *lyphnode_by_id_or_new( char *id );
lyphedge *lyphedge_by_id( char *id );
trie *assign_new_layer_id( layer *lyr );
lyph *lyph_by_layers( int type, layer **layers, char *name );
lyph *lyph_by_layers_recurse( int type, layer **layers, trie *t );
int same_layers( layer **x, layer **y );
layer **copy_layers( layer **src );
int layers_len( layer **layers );
void sort_layers( layer **layers );
trie *assign_new_lyph_id( lyph *L );
void free_lyphdupe_trie( trie *t );
void save_lyphs_recurse( trie *t, FILE *fp, trie *avoid_dupes );
char *id_as_iri( trie *id );
void fprintf_layer( FILE *fp, layer *lyr, int bnodes, int cnt, trie *avoid_dupes );
void load_lyphs( void );
int parse_lyph_type( char *str );
void load_lyph_label( char *subj_full, char *label );
void load_lyph_type( char *subj_full, char *type_str );
void acknowledge_has_layers( char *subj_full, char *bnode_id );
void load_layer_material( char *subj_full, char *obj_full );
void load_layer_color( char *subj_full, char *obj_full );
void load_layer_to_lld( char *bnode, char *obj_full );
void load_layer_thickness( char *subj_full, char *obj );
lyph *missing_layers( trie *t );
void handle_loaded_layers( trie *t );
int load_lyphedges( void );
int load_lyphedges_one_line( char *line, char **err );
void save_lyphedges_recurse( trie *t, FILE *fp );
void save_lyphedges( void );
int word_from_line( char **line, char *buf );
char *lyphedge_type_str( int type );
int parse_lyph_type_str( char *type );
void add_exit( lyphedge *e, lyphnode *n );
lyphedge **compute_lyphpath( lyphnode *from, lyphnode *to );
void free_lyphsteps( lyphstep *head );
void save_lyphviews( void );
void load_lyphviews( void );
char *lyphview_to_json( lyphview *v );
lyphview *search_duplicate_view( lyphnode **nodes, char **coords );
lyphview *create_new_view( lyphnode **nodes, char **coords );
lyphedge *make_lyphedge( int type, lyphnode *from, lyphnode *to, lyph *L, char *fmastr, char *namestr );
lyphnode *make_lyphnode( void );
