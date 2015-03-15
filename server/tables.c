#include "lyph.h"
#include "srv.h"

command_entry *first_handler[TABLES_HASH];
command_entry *last_handler[TABLES_HASH];

void init_command_table(void)
{
  add_handler( "all_templates", handle_all_templates_request );
  add_handler( "all_lyphnodes", handle_all_lyphnodes_request );
  add_handler( "all_lyphs", handle_all_lyphs_request );
  add_handler( "all_lyphviews", handle_all_lyphviews_request );
  add_handler( "all_ont_terms", handle_all_ont_terms_request );
  add_handler( "template_hierarchy", handle_template_hierarchy_request );
  add_handler( "assign_template", handle_assign_template_request );
  add_handler( "lyphconstrain", handle_lyphconstrain_request );
  add_handler( "template_along_path", handle_template_along_path_request );
  add_handler( "constrain_along_path", handle_constrain_along_path_request );
  add_handler( "maketemplate", handle_maketemplate_request );
  add_handler( "makelayer", handle_makelayer_request );
  add_handler( "makelyph", handle_makelyph_request );
  add_handler( "makelyphnode", handle_makelyphnode_request );
  add_handler( "makeview", handle_makeview_request );
  add_handler( "nodes_to_view", handle_nodes_to_view_request );
  add_handler( "lyphs_to_view", handle_nodes_to_view_request ); // Not a type -- lyphs_from_view and nodes_to_view are actually handled by the same handler function
  add_handler( "lyphs_from_view", handle_lyphs_from_view_request );
  add_handler( "nodes_from_view", handle_nodes_from_view_request );
  add_handler( "lyphpath", handle_lyphpath_request );
  add_handler( "reset_db", handle_reset_db_request );
  add_handler( "uclsyntax", handle_ucl_syntax_request );
  add_handler( "ucl_syntax", handle_ucl_syntax_request );
  add_handler( "ucl-syntax", handle_ucl_syntax_request );
  add_handler( "template", handle_template_request );
  add_handler( "layer", handle_layer_request );
  add_handler( "lyph", handle_lyph_request );
  add_handler( "lyphnode", handle_lyphnode_request );
  add_handler( "lyphview", handle_lyphview_request );
  add_handler( "subtemplates", handle_subtemplates_request );
  add_handler( "instances_of", handle_instances_of_request );
  add_handler( "involves_template", handle_involves_template_request );
  add_handler( "editlyph", handle_editlyph_request );
  add_handler( "editlyphnode", handle_editlyphnode_request );
  add_handler( "edit_template", handle_edit_template_request );
  add_handler( "editview", handle_editview_request );
  add_handler( "editlayer", handle_editlayer_request );
  add_handler( "delete_lyphs", handle_delete_lyphs_request );
  add_handler( "delete_nodes", handle_delete_nodes_request );
  add_handler( "delete_templates", handle_delete_templates_request );
  add_handler( "delete_views", handle_delete_views_request );
  add_handler( "delete_layers", handle_delete_layers_request );
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
