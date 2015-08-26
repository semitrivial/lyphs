#include <sys/socket.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#if !defined(FNDELAY)
#define FNDELAY O_NDELAY
#endif

/*
 * If a browser connects, but doesn't do anything, how long until kicking them off
 * (See also the next comment below)
 */
#define HTTP_KICK_IDLE_AFTER_X_SECS 60

/*
 * How many times, per second, will your main project be checking for new connections?
 * (Rather than keep track of the exact time a client is idle, rather we keep track of
 *  the number of times we've checked for updates and found none.  When the client has
 *  been idle for HTTP_KICK_IDLE_AFTER_X_SECS * HTTP_PULSE_PER_SEC consecutive checks,
 *  they will be booted.  HTTP_PULSES_PER_SEC is not used anywhere else.  Thus, it is
 *  not terribly important that it be completely precise, a ballpark estimate is good
 *  enough.
 */
#define HTTP_PULSES_PER_SEC 10

#define HTTP_INITIAL_OUTBUF_SIZE 16384
#define HTTP_INITIAL_INBUF_SIZE 16384
#define HTTP_MAX_INBUF_SIZE 131072
#define HTTP_MAX_OUTBUF_SIZE 524288

#define HTTP_LISTEN_BACKLOG 32

#define HTTP_SOCKSTATE_READING_REQUEST 0
#define HTTP_SOCKSTATE_WRITING_RESPONSE 1
#define HTTP_SOCKSTATE_AWAITING_INSTRUCTIONS 2

#define ALONG_PATH_TEMPLATE 1
#define ALONG_PATH_CONSTRAIN 2
#define ALONG_PATH_COMPUTE 3

#define TABLES_HASH 256

/*
 * Macros
 */

#define HND_ERR_NORETURN( x )\
do\
{\
  char *jsonerr = JSON1( "Error": x );\
  send_response( req, jsonerr );\
}\
while(0)

#define HND_ERR( x )\
do\
{\
  HND_ERR_NORETURN( x );\
  return;\
}\
while(0)

#define HND_ERRF_NORETURN( ... )\
do\
{\
  char *__hnd_errf_errmsg = strdupf( __VA_ARGS__ );\
  HND_ERR_NORETURN( __hnd_errf_errmsg );\
  free( __hnd_errf_errmsg );\
}\
while(0)

#define HND_ERRF( ... )\
do\
{\
  HND_ERRF_NORETURN( __VA_ARGS__ );\
  return;\
}\
while(0)

#define HND_ERR_FREE( x )\
do\
{\
  HND_ERR_NORETURN( x );\
  free( x );\
  return;\
}\
while(0)

#define TRY_PARAM( variable, param, errmsg )\
do\
{\
  variable = get_param( params, param );\
  \
  if ( !variable )\
    HND_ERR( errmsg );\
}\
while(0)

#define TRY_TWO_PARAMS( variable, param1, param2, errmsg )\
do\
{\
  variable = get_param( params, param1 );\
  \
  if ( !variable )\
    TRY_PARAM( variable, param2, errmsg );\
}\
while(0)

#define GET_NUMBERED_ARGS( params, base, fnc, err, size )\
        get_numbered_args( (params), (base), (char * (*) (void*))(fnc), (err), (size) )

#define GET_NUMBERED_ARGS_R( params, base, fnc, data, err, size )\
        get_numbered_args_r( (params), (base), (char * (*) (void*,void*))(fnc), (void*)data, (err), (size) )

/*
 * Structures
 */
typedef struct HTTP_REQUEST http_request;
typedef struct HTTP_CONN http_conn;
typedef struct URL_PARAM url_param;
typedef struct COMMAND_ENTRY command_entry;

typedef void do_function ( char *request, http_request *req, url_param **params );

struct HTTP_REQUEST
{
  http_request *next;
  http_request *prev;
  http_conn *conn;
  char *query;
  int *dead;

  /*
   * JSONP support
   */
  char *callback;
};

struct HTTP_CONN
{
  http_conn *next;
  http_conn *prev;
  http_request *req;
  int sock;
  int state;
  int idle;
  char *buf;
  int bufsize;
  int buflen;
  char *outbuf;
  int outbufsize;
  int outbuflen;
  int len;
  char *writehead;
};

struct URL_PARAM
{
  char *key;
  char *val;
};

struct COMMAND_ENTRY
{
  command_entry *next;
  do_function *f;
  char *cmd;
  int read_write_state;
};

typedef enum
{
  CMD_READONLY, CMD_READWRITE
} read_write_states;

/*
 * Global variables
 */
http_request *first_http_req;
http_request *last_http_req;
http_conn *first_http_conn;
http_conn *last_http_conn;

fd_set http_inset;
fd_set http_outset;
fd_set http_excset;

int srvsock;

/*
 * Function prototypes
 */

/*
 * srv.c
 */
int parse_commandline_args( int argc, const char *argv[], const char **filename, int *port );
void init_lyph_http_server( int port );
void http_update_connections( void );
void http_kill_socket( http_conn *c );
void free_http_request( http_request *r );
void http_answer_the_phone( int srvsock );
int resize_buffer( http_conn *c, char **buf );
void http_listen_to_request( http_conn *c );
void http_flush_response( http_conn *c );
void http_parse_input( http_conn *c );
http_request *http_recv( void );
void http_write( http_request *req, char *txt );
void http_send( http_request *req, char *txt, int len );
void handle_request( http_request *req, char *query );
void send_400_response( http_request *req );
void send_response( http_request *req, char *txt );
void send_response_with_type( http_request *req, char *code, char *txt, char *type );
char *nocache_headers(void);
char *current_date(void);
void send_gui( http_request *req );
void send_js( http_request *req );
char *load_file( char *filename );
const char *parse_params( char *buf, http_request *req, url_param **params );
void free_url_params( url_param **buf );
char *get_param( url_param **params, char *key );
int has_param( url_param **params, char *key );
void along_path_abstractor( http_request *req, url_param **params, int along_path_type );
void makeview_worker( char *request, http_request *req, url_param **params, int makeview );
void default_config_values( void );
void send_ok( http_request *req );

/*
 * tables.c
 */
void init_command_table( void );
void add_handler( char *cmd, do_function *fnc, int read_write_state );
command_entry *lookup_command( char *cmd );

/*
 * cmds.c
 */
void **get_numbered_args( url_param **params, char *base, char * (*fnc) (void *), char **err, int *size );
void **get_numbered_args_r( url_param **params, char *base, char * (*fnc) (void *, void *), void *data, char **err, int *size );
void save_annotations( void );

/*
 * Handlers (functions for handling HTTP requests)
 */
HANDLER( do_ucl_syntax );
HANDLER( do_makelayer );
HANDLER( do_maketemplate );
HANDLER( do_makeview );
HANDLER( do_nodes_to_view );
HANDLER( do_nodes_from_view );
HANDLER( do_lyphs_from_view );
HANDLER( do_material_to_layer );
HANDLER( do_material_from_layer );
HANDLER( do_change_coords );
HANDLER( do_makelyphnode );
HANDLER( do_makelyph );
HANDLER( do_template );
HANDLER( do_layer );
HANDLER( do_lyph );
HANDLER( do_lyphnode );
HANDLER( do_lyphview );
HANDLER( do_fma );
HANDLER( do_all_templates );
HANDLER( do_all_lyphs );
HANDLER( do_all_lyphnodes );
HANDLER( do_all_lyphviews );
HANDLER( do_template_hierarchy );
HANDLER( do_assign_template );
HANDLER( do_lyphconstrain );
HANDLER( do_lyphpath );
HANDLER( do_template_along_path );
HANDLER( do_constrain_along_path );
HANDLER( do_reset_db );
HANDLER( do_all_ont_terms );
HANDLER( do_subtemplates );
HANDLER( do_instances_of );
HANDLER( do_involves_template );
HANDLER( do_templates_involving );
HANDLER( do_has_template );
HANDLER( do_has_clinical_index );
HANDLER( do_unused_indices );
HANDLER( do_editlyph );
HANDLER( do_editlyphnode );
HANDLER( do_edit_template );
HANDLER( do_editview );
HANDLER( do_editlayer );
HANDLER( do_delete_lyphs );
HANDLER( do_delete_nodes );
HANDLER( do_delete_templates );
HANDLER( do_delete_views );
HANDLER( do_delete_layers );
HANDLER( do_annotate );
HANDLER( do_make_clinical_index );
HANDLER( do_make_pubmed );
HANDLER( do_edit_clinical_index );
HANDLER( do_edit_pubmed );
HANDLER( do_clinical_index );
HANDLER( do_pubmed );
HANDLER( do_all_pubmeds );
HANDLER( do_all_clinical_indices );
HANDLER( do_remove_annotation );
HANDLER( do_radiological_indices );
HANDLER( do_layer_from_template );
HANDLER( do_layer_to_template );
HANDLER( do_parse_csv );
HANDLER( do_lyphs_located_in_term );
HANDLER( do_is_built_from_template );
HANDLER( do_clone );
HANDLER( do_connections );
HANDLER( do_correlation );
HANDLER( do_all_correlations );
HANDLER( do_makecorrelation );
HANDLER( do_lyphs_by_prefix );
HANDLER( do_ontsearch );
HANDLER( do_all_located_measures );
HANDLER( do_make_located_measure );
HANDLER( do_located_measure );
HANDLER( do_delete_correlation );
HANDLER( do_delete_located_measure );
HANDLER( do_get_csv );
HANDLER( do_nifs );
HANDLER( do_stats );
HANDLER( do_bop );
HANDLER( do_all_bops );
HANDLER( do_fmamap );

HANDLER( do_niflyph );
HANDLER( do_nifconnection );
HANDLER( do_renif );
HANDLER( do_gen_random_correlations );
HANDLER( do_dotfile );
HANDLER( do_create_fmalyphs );
HANDLER( do_import_lateralized_brain );
HANDLER( do_makebop );
