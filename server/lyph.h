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
#define MAX_URL_PARAMS 300
#define MAX_URL_PARAM_LEN 512
#define MAX_LYPH_LINE_LEN (MAX_IRI_LEN * 3)
#define MAX_INT_LEN (strlen("-2147483647"))
#define MAX_NUMPATHS 16

#define LOG_FILE "log.txt"
#define ANNOTS_FILE "annots.dat"
#define PUBMED_FILE "pubmed.dat"
#define CLINICAL_INDEX_FILE "clinical_indices.dat"

/*
 * Typedefs
 */
typedef struct STR_WRAPPER str_wrapper;
typedef struct TRIE trie;
typedef struct TRIE_WRAPPER trie_wrapper;
typedef struct UCL_SYNTAX ucl_syntax;
typedef struct AMBIG ambig;
typedef struct LYPHPLATE lyphplate;
typedef struct LAYER layer;
typedef struct LAYER_LOADING layer_loading;
typedef struct LOAD_LAYERS_DATA load_layers_data;
typedef struct LYPHNODE lyphnode;
typedef struct LYPHNODE_WRAPPER lyphnode_wrapper;
typedef struct LYPH lyph;
typedef struct LYPH_WRAPPER lyph_wrapper;
typedef struct LYPH_TO_JSON_DETAILS lyph_to_json_details;
typedef struct EXIT_DATA exit_data;
typedef struct LYPHSTEP lyphstep;
typedef struct LYPHVIEW lyphview;
typedef struct LV_RECT lv_rect;
typedef struct VIEWED_NODE viewed_node;
typedef struct LYPHPLATE_WRAPPER lyphplate_wrapper;
typedef struct LYPHPLATES_WRAPPER lyphplates_wrapper;
typedef struct LYPH_FILTER lyph_filter;
typedef struct ANNOT annot;
typedef struct CLINICAL_INDEX clinical_index;
typedef struct PUBMED pubmed;
typedef struct SYSTEM_CONFIGS system_configs;

/*
 * Structures
 */
struct STR_WRAPPER
{
  str_wrapper *next;
  str_wrapper *prev;
  char *str;
};

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

struct LYPHPLATE
{
  lyphplate **misc_material;
  lyphplate **supers;
  lyphplate **subs;
  layer **layers;
  trie *ont_term;
  trie *name;
  trie *id;
  int type;
  int flags;
};

typedef enum
{
  LYPHPLATE_BASIC, LYPHPLATE_SHELL, LYPHPLATE_MIX, LYPHPLATE_MISSING
} lyphplate_types;

struct LYPHPLATE_WRAPPER
{
  lyphplate_wrapper *next;
  lyphplate *L;
};

struct LYPHPLATES_WRAPPER
{
  lyphplates_wrapper *next;
  lyphplate **L;
};

struct LAYER
{
  lyphplate **material;
  int thickness;
  trie *id;
};

struct LYPHNODE
{
  trie *id;
  int flags;
  exit_data **exits;
  exit_data **incoming;
  lyph *location;
  int loctype;
};

typedef enum
{
  LYPHNODE_SEEN = 1, LYPHNODE_SELECTED = 2, LYPHNODE_GOAL = 4
} lyphnode_flags;

typedef enum
{
  LOCTYPE_INTERIOR = 0, LOCTYPE_BORDER = 1
} lyphnode_loctypes;

typedef enum
{
  LTJ_EXITS = 1, LTJ_SELECTIVE = 2, LTJ_FULL_EXIT_DATA = 4
} lyphnode_to_json_flag_types;

typedef enum
{
  ETJ_FULL_EXIT_DATA = 1
} exit_to_json_flag_types;

struct LYPHNODE_WRAPPER
{
  lyphnode_wrapper *next;
  lyphnode *n;
};

struct LYPH
{
  trie *id;
  trie *name;
  trie *species;
  int type;
  int flags;
  lyphnode *from;
  lyphnode *to;
  lyphplate *lyphplate;
  lyphplate **constraints;
  annot **annots;
  trie *fma;
};

typedef enum
{
  LYPH_ARTERIAL, LYPH_MICROCIRC, LYPH_VENOUS, LYPH_CARDIAC, LYPH_DELETED
} lyph_types;

struct LYPH_WRAPPER
{
  lyph *e;
  lyph_wrapper *next;
};

struct LYPH_TO_JSON_DETAILS
{
  int show_annots;
  lyph **buf;
};

struct ANNOT
{
  trie *pred;
  trie *obj;
  pubmed *pubmed;
};

struct CLINICAL_INDEX
{
  clinical_index *next;
  trie *index;
  trie *label;
  pubmed **pubmeds;
};

#define CLINICAL_INDEX_SEARCH_UNION 1
#define CLINICAL_INDEX_SEARCH_IX 2

struct PUBMED
{
  pubmed *next;
  char *id;
  char *title;
};

struct EXIT_DATA
{
  lyphnode *to;
  lyph *via;
};

struct LYPHSTEP
{
  lyphstep *next;
  lyphstep *prev;
  int depth;
  lyphstep *backtrace;
  lyphnode *location;
  lyph *lyph;
};

struct LYPH_FILTER
{
  lyphplate *sup;
  int accept_na_edges;
};

struct LYPHVIEW
{
  int id;
  char *name;
  lyphnode **nodes;
  char **coords;
  lv_rect **rects;
};

struct LV_RECT
{
  lyph *L;
  char *x;
  char *y;
  char *width;
  char *height;
};

struct VIEWED_NODE
{
  lyphnode *node;
  char *x;
  char *y;
};

struct LOAD_LAYERS_DATA
{
  lyphplate *subj;
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

struct SYSTEM_CONFIGS
{
  int readonly;
};

/*
 * Global variables
 */
extern system_configs configs;

extern trie *iri_to_labels;
extern trie *label_to_iris;
extern trie *label_to_iris_lowercase;

extern trie *lyphplate_names;
extern trie *lyphplate_ids;
extern trie *layer_ids;
extern trie *lyphnode_ids;
extern trie *lyph_ids;
extern trie *lyph_fmas;
extern trie *lyph_names;

extern trie *superclasses;

extern trie *metadata;
clinical_index *first_clinical_index;
pubmed *first_pubmed;

/*
 * Function prototypes
 */

/*
 * labels.c
 */
void init_labels(FILE *fp);
void parse_ontology_file(FILE *fp);
void add_labels_entry( char *iri_ch, char *label_ch );
void add_subclass_entry( char *child_ch, char *parent_ch );
trie **get_labels_by_iri( char *iri_ch );
trie **get_iris_by_label( char *label_ch );
trie **get_iris_by_label_case_insensitive( char *label_ch );
trie **get_autocomplete_labels( char *label_ch, int case_insens );
char *all_ont_terms_as_json( void );

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
void log_stringf( char *fmt, ... );
void log_linenum( int linenum );
void to_logfile( char *fmt, ... );
char *html_encode( char *str );
void init_html_codes( void );
char *lowercaserize( const char *x );
char *get_url_shortform( char *iri );
char *url_decode(char *str);
char *url_encode(char *str);
int is_number( const char *arg );
void error_message( char *err );
void error_messagef( char *fmt, ... );
char *strdupf( const char *fmt, ... );
char *jsonf( int paircnt, ... );;
void json_gc( void );
size_t voidlen( void **x );
char *constraints_comma_list( lyphplate **constraints );
int copy_file( char *dest_ch, char *src_ch );
void **parse_list( char *list, char * (*fnc) (void *), char *name, char **err );
void maybe_update_top_id( int *top, char *idstr );
char *loctype_to_str( int loctype );
void multifree( void *first, ... );
int req_cmp( char *req, char *match );

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
lyphplate *lyphplate_by_name( char *name );
lyphplate *lyphplate_by_id( char *id );
char *lyphplate_to_json( lyphplate *L );
char *lyphplate_to_shallow_json( lyphplate *L );
char *layer_to_json( layer *lyr );
char *annot_to_json( annot *a );
lyphview *lyphview_by_id( char *idstr );
char *lyphnode_to_json_wrappee( lyphnode *n, char *x, char *y );
char *lyphnode_to_json( lyphnode *n );
char *lyph_to_json( lyph *e );
char *lyph_to_json_r( lyph *e, lyph_to_json_details *details );
char *lyphpath_to_json( lyph **path );
char *exit_to_json( exit_data *x );
layer *layer_by_id( char *id );
layer *layer_by_description( lyphplate **materials, int thickness );
lyphnode *lyphnode_by_id( char *id );
lyphnode *lyphnode_by_id_or_new( char *id );
lyph *lyph_by_id( char *id );
lyph *lyph_by_template_or_id( char *id, char *species );
trie *assign_new_layer_id( layer *lyr );
lyphplate *lyphplate_by_layers( int type, layer **layers, lyphplate **misc_material, char *name );
int same_layers( layer **x, layer **y );
layer **copy_layers( layer **src );
void sort_layers( layer **layers );
trie *assign_new_lyphplate_id( lyphplate *L );
void free_lyphplate_dupe_trie( trie *t );
void save_lyphplates_recurse( trie *t, FILE *fp, trie *avoid_dupes );
char *id_as_iri( trie *id, char *prefix );
void fprintf_layer( FILE *fp, layer *lyr, int bnodes, int cnt, trie *avoid_dupes );
void load_lyphplates( void );
int parse_lyphplate_type( char *str );
void load_lyphplate_label( char *subj_full, char *label );
void load_misc_materials( char *subj_full, char *misc_materials_str );
void load_lyphplate_type( char *subj_full, char *type_str );
void acknowledge_has_layers( char *subj_full, char *bnode_id );
void load_layer_material( char *subj_full, char *obj_full );
void load_layer_to_lld( char *bnode, char *obj_full );
void load_layer_thickness( char *subj_full, char *obj );
lyphplate *missing_layers( trie *t );
void handle_loaded_layers( trie *t );
int load_lyphs( void );
int load_lyphs_one_line( char *line, char **err );
void save_lyphs_recurse( trie *t, FILE *fp );
void save_lyphs( void );
void save_lyphnode_locs( trie *t, FILE *fp );
int word_from_line( char **line, char *buf );
char *lyph_type_str( int type );
int parse_lyphplate_type_str( char *type );
void add_exit( lyph *e );
void add_to_exits( lyph *e, lyphnode *to, exit_data ***victim );
void remove_from_exits( lyph *e, exit_data ***victim );
void change_source_of_exit( lyph *via, lyphnode *new_src, exit_data **exits );
void change_dest_of_exit( lyph *via, lyphnode *new_dest, exit_data **exits );
lyph ***compute_lyphpaths( lyphnode_wrapper *from_head, lyphnode_wrapper *to_head, lyph_filter *filter, int numpaths );
void free_lyphsteps( lyphstep *head );
void save_lyphviews( void );
void load_lyphviews( void );
char *lyphview_to_json( lyphview *v );
lyphview *create_new_view( lyphnode **nodes, char **xs, char **ys, lyph **lyphs, char **lxs, char **lys, char **widths, char **heights, char *name );
lyph *make_lyph( int type, lyphnode *from, lyphnode *to, lyphplate *L, char *fmastr, char *namestr, char *speciesstr );
lyphnode *make_lyphnode( void );
void compute_lyphplate_hierarchy( trie *t );
lyphplate *lyphplate_by_ont_term( trie *term );
void load_ont_term( char *subj_full, char *ont_term_str );
char *lyphplate_hierarchy_to_json( void );
void lyphs_unset_bits( int bits, trie *t );
void lyphplates_unset_bits( int bits, trie *t );
void lyphnodes_unset_bits( int bits, trie *t );
int can_assign_lyphplate_to_lyph( lyphplate *L, lyph *e, char **err );
void free_all_views( void );
void free_all_lyphs( void );
void free_all_lyphplates( void );
void save_lyphplates(void);
lyphplate **get_all_lyphplates( void );
void free_lyphnode_wrappers( lyphnode_wrapper *head );
lyph *get_lyph_location( lyph *e );
lyphnode *blank_lyphnode( void );

/*
 * hier.c
 */
void calc_nodes_in_lyph( lyph *L, lyphnode_wrapper **head, lyphnode_wrapper **tail );

/*
 * meta.c
 */
void load_annotations(void);
int annotate_lyph( lyph *e, trie *pred, trie *obj, pubmed *pubmed );
void save_annotations( void );
pubmed *pubmed_by_id( char *id );
clinical_index *clinical_index_by_index( char *ind );
void save_pubmeds( void );
void load_pubmeds( void );
void save_clinical_indices( void );
void load_clinical_indices( void );
char *pubmed_to_json_brief( pubmed *p );
char *pubmed_to_json_full( pubmed *p );
char *clinical_index_to_json_brief( clinical_index *ci );
char *clinical_index_to_json_full( clinical_index *ci );
