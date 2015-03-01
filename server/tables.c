#include "lyph.h"
#include "srv.h"

command_entry *first_handler[TABLES_HASH];
command_entry *last_handler[TABLES_HASH];

void init_command_table(void)
{
  add_handler( "all_lyphs", handle_all_lyphs_request );
  add_handler( "all_lyphnodes", handle_all_lyphnodes_request );
  add_handler( "all_lyphedges", handle_all_lyphedges_request );
  add_handler( "all_lyphviews", handle_all_lyphviews_request );
  add_handler( "all_ont_terms", handle_all_ont_terms_request );
  add_handler( "lyph_hierarchy", handle_lyph_hierarchy_request );
  add_handler( "assignlyph", handle_assignlyph_request );
  add_handler( "edgeconstrain", handle_edgeconstrain_request );
  add_handler( "lyph_along_path", handle_lyph_along_path_request );
  add_handler( "constrain_along_path", handle_constrain_along_path_request );
  add_handler( "makelyph", handle_makelyph_request );
  add_handler( "makelayer", handle_makelayer_request );
  add_handler( "makelyphedge", handle_makelyphedge_request );
  add_handler( "makelyphnode", handle_makelyphnode_request );
  add_handler( "makeview", handle_makeview_request );
  add_handler( "nodes_to_view", handle_nodes_to_view_request );
  add_handler( "lyphpath", handle_lyphpath_request );
  add_handler( "reset_db", handle_reset_db_request );
  add_handler( "uclsyntax", handle_ucl_syntax_request );
  add_handler( "ucl_syntax", handle_ucl_syntax_request );
  add_handler( "ucl-syntax", handle_ucl_syntax_request );
  add_handler( "lyph", handle_lyph_request );
  add_handler( "layer", handle_layer_request );
  add_handler( "lyphedge", handle_lyphedge_request );
  add_handler( "lyphnode", handle_lyphnode_request );
  add_handler( "lyphview", handle_lyphview_request );
  add_handler( "sublyphs", handle_sublyphs_request );
  add_handler( "editedge", handle_editedge_request );
  add_handler( "editlyph", handle_editlyph_request );
  add_handler( "editview", handle_editview_request );
  add_handler( "editlayer", handle_editlayer_request );
  add_handler( "delete_edge", handle_delete_edge_request );
  add_handler( "delete_node", handle_delete_node_request );
  add_handler( "delete_lyph", handle_delete_lyph_request );
  add_handler( "delete_view", handle_delete_view_request );
  add_handler( "delete_layer", handle_delete_layer_request );
}

void add_handler( char *cmd, handle_function *fnc )
{
  int hash = *cmd % TABLES_HASH;
  command_entry *entry;

  CREATE( entry, command_entry, 1 );

  entry->cmd = cmd;
  entry->f = fnc;

  LINK( entry, first_handler[hash], last_handler[hash], next );
}

handle_function *lookup_command( char *cmd )
{
  int hash = *cmd % TABLES_HASH;
  command_entry *entry;

  for ( entry = first_handler[hash]; entry; entry = entry->next )
    if ( !strcmp( cmd, entry->cmd ) )
      return entry->f;

  return NULL;
}
