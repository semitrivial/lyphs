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
  send_200_response( req, jsonerr );\
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

typedef void handle_function ( char *request, http_request *req, url_param **params );

struct HTTP_REQUEST
{
  http_request *next;
  http_request *prev;
  http_conn *conn;
  char *query;
  char *callback;  //JSONP support
  int *dead;
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
  handle_function *f;
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
void send_200_response( http_request *req, char *txt );
void send_200_with_type( http_request *req, char *txt, char *type );
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

/*
 * tables.c
 */
void init_command_table( void );
void add_handler( char *cmd, handle_function *fnc, int read_write_state );
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
HANDLER( handle_ucl_syntax_request );
HANDLER( handle_makelayer_request );
HANDLER( handle_maketemplate_request );
HANDLER( handle_makeview_request );
HANDLER( handle_nodes_to_view_request );
HANDLER( handle_nodes_from_view_request );
HANDLER( handle_lyphs_from_view_request );
HANDLER( handle_material_to_layer_request );
HANDLER( handle_material_from_layer_request );
HANDLER( handle_change_coords_request );
HANDLER( handle_makelyphnode_request );
HANDLER( handle_makelyph_request );
HANDLER( handle_template_request );
HANDLER( handle_layer_request );
HANDLER( handle_lyph_request );
HANDLER( handle_lyphnode_request );
HANDLER( handle_lyphview_request );
HANDLER( handle_all_templates_request );
HANDLER( handle_all_lyphs_request );
HANDLER( handle_all_lyphnodes_request );
HANDLER( handle_all_lyphviews_request );
HANDLER( handle_all_bulk_annots_request );
HANDLER( handle_template_hierarchy_request );
HANDLER( handle_assign_template_request );
HANDLER( handle_lyphconstrain_request );
HANDLER( handle_lyphpath_request );
HANDLER( handle_template_along_path_request );
HANDLER( handle_constrain_along_path_request );
HANDLER( handle_reset_db_request );
HANDLER( handle_all_ont_terms_request );
HANDLER( handle_subtemplates_request );
HANDLER( handle_instances_of_request );
HANDLER( handle_involves_template_request );
HANDLER( handle_templates_involving_request );
HANDLER( handle_has_template_request );
HANDLER( handle_has_clinical_index_request );
HANDLER( handle_unused_indices_request );
HANDLER( handle_editlyph_request );
HANDLER( handle_editlyphnode_request );
HANDLER( handle_edit_template_request );
HANDLER( handle_editview_request );
HANDLER( handle_editlayer_request );
HANDLER( handle_delete_lyphs_request );
HANDLER( handle_delete_nodes_request );
HANDLER( handle_delete_templates_request );
HANDLER( handle_delete_views_request );
HANDLER( handle_delete_layers_request );
HANDLER( handle_annotate_request );
HANDLER( handle_bulk_annot_request );
HANDLER( handle_make_clinical_index_request );
HANDLER( handle_make_pubmed_request );
HANDLER( handle_edit_clinical_index_request );
HANDLER( handle_edit_pubmed_request );
HANDLER( handle_clinical_index_request );
HANDLER( handle_pubmed_request );
HANDLER( handle_all_pubmeds_request );
HANDLER( handle_all_clinical_indices_request );
HANDLER( handle_remove_annotation_request );
HANDLER( handle_radiological_indices_request );
HANDLER( handle_layer_from_template_request );
HANDLER( handle_layer_to_template_request );
HANDLER( handle_parse_csv_request );
