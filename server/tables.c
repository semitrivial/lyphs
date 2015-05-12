#include "lyph.h"
#include "srv.h"

command_entry *first_handler[TABLES_HASH];
command_entry *last_handler[TABLES_HASH];

void init_command_table(void)
{
  add_handler( "all_templates", handle_all_templates_request, CMD_READONLY );
  add_handler( "all_lyphnodes", handle_all_lyphnodes_request, CMD_READONLY );
  add_handler( "all_lyphs", handle_all_lyphs_request, CMD_READONLY );
  add_handler( "all_lyphviews", handle_all_lyphviews_request, CMD_READONLY );
  add_handler( "all_ont_terms", handle_all_ont_terms_request, CMD_READONLY );
  add_handler( "all_pubmeds", handle_all_pubmeds_request, CMD_READONLY );
  add_handler( "all_clinical_indices", handle_all_clinical_indices_request, CMD_READONLY );
  add_handler( "all_radiological_indices", handle_radiological_indices_request, CMD_READONLY );
  add_handler( "clinical_index", handle_clinical_index_request, CMD_READONLY );
  add_handler( "pubmed", handle_pubmed_request, CMD_READONLY );
  add_handler( "template_hierarchy", handle_template_hierarchy_request, CMD_READONLY );
  add_handler( "assign_template", handle_assign_template_request, CMD_READWRITE );
  add_handler( "lyphconstrain", handle_lyphconstrain_request, CMD_READWRITE );
  add_handler( "template_along_path", handle_template_along_path_request, CMD_READWRITE );
  add_handler( "constrain_along_path", handle_constrain_along_path_request, CMD_READWRITE );
  add_handler( "maketemplate", handle_maketemplate_request, CMD_READWRITE );
  add_handler( "makelayer", handle_makelayer_request, CMD_READWRITE );
  add_handler( "makelyph", handle_makelyph_request, CMD_READWRITE );
  add_handler( "makelyphnode", handle_makelyphnode_request, CMD_READWRITE );
  add_handler( "makeview", handle_makeview_request, CMD_READWRITE );
  add_handler( "make_clinical_index", handle_make_clinical_index_request, CMD_READWRITE );
  add_handler( "make_pubmed", handle_make_pubmed_request, CMD_READWRITE );
  add_handler( "nodes_to_view", handle_nodes_to_view_request, CMD_READWRITE );
  add_handler( "lyphs_to_view", handle_nodes_to_view_request, CMD_READWRITE ); // Not a type -- lyphs_from_view and nodes_to_view are actually handled by the same handler function
  add_handler( "lyphs_from_view", handle_lyphs_from_view_request, CMD_READWRITE );
  add_handler( "nodes_from_view", handle_nodes_from_view_request, CMD_READWRITE );
  add_handler( "layer_from_template", handle_layer_from_template_request, CMD_READWRITE );
  add_handler( "layer_to_template", handle_layer_to_template_request, CMD_READWRITE );
  add_handler( "material_to_layer", handle_material_to_layer_request, CMD_READWRITE );
  add_handler( "material_from_layer", handle_material_from_layer_request, CMD_READWRITE );
  add_handler( "change_coords", handle_change_coords_request, CMD_READWRITE );
  add_handler( "lyphpath", handle_lyphpath_request, CMD_READONLY );
  add_handler( "reset_db", handle_reset_db_request, CMD_READWRITE );
  add_handler( "uclsyntax", handle_ucl_syntax_request, CMD_READONLY );
  add_handler( "ucl_syntax", handle_ucl_syntax_request, CMD_READONLY );
  add_handler( "ucl-syntax", handle_ucl_syntax_request, CMD_READONLY );
  add_handler( "template", handle_template_request, CMD_READONLY );
  add_handler( "layer", handle_layer_request, CMD_READONLY );
  add_handler( "lyph", handle_lyph_request, CMD_READONLY );
  add_handler( "lyphnode", handle_lyphnode_request, CMD_READONLY );
  add_handler( "lyphview", handle_lyphview_request, CMD_READONLY );
  add_handler( "subtemplates", handle_subtemplates_request, CMD_READONLY );
  add_handler( "instances_of", handle_instances_of_request, CMD_READONLY );
  add_handler( "involves_template", handle_involves_template_request, CMD_READONLY );
  add_handler( "has_template", handle_has_template_request, CMD_READONLY );
  add_handler( "has_clinical_index", handle_has_clinical_index_request, CMD_READONLY );
  add_handler( "unused_indices", handle_unused_indices_request, CMD_READONLY );
  add_handler( "annotate", handle_annotate_request, CMD_READWRITE );
  add_handler( "remove_annotations", handle_remove_annotation_request, CMD_READWRITE );
  add_handler( "editlyph", handle_editlyph_request, CMD_READWRITE );
  add_handler( "editlyphnode", handle_editlyphnode_request, CMD_READWRITE );
  add_handler( "edit_template", handle_edit_template_request, CMD_READWRITE );
  add_handler( "editview", handle_editview_request, CMD_READWRITE );
  add_handler( "editlayer", handle_editlayer_request, CMD_READWRITE );
  add_handler( "edit_clinical_index", handle_edit_clinical_index_request, CMD_READWRITE );
  add_handler( "edit_pubmed", handle_edit_pubmed_request, CMD_READWRITE );
  add_handler( "delete_lyphs", handle_delete_lyphs_request, CMD_READWRITE );
  add_handler( "delete_nodes", handle_delete_nodes_request, CMD_READWRITE );
  add_handler( "delete_templates", handle_delete_templates_request, CMD_READWRITE );
  add_handler( "delete_views", handle_delete_views_request, CMD_READWRITE );
  add_handler( "delete_layers", handle_delete_layers_request, CMD_READWRITE );
  add_handler( "parse_csv", handle_parse_csv_request, CMD_READWRITE );
}

void add_handler( char *cmd, handle_function *fnc, int read_write_state )
{
  int hash = *cmd % TABLES_HASH;
  command_entry *entry;

  CREATE( entry, command_entry, 1 );

  entry->cmd = cmd;
  entry->f = fnc;
  entry->read_write_state = read_write_state;

  LINK( entry, first_handler[hash], last_handler[hash], next );
}

command_entry *lookup_command( char *cmd )
{
  int hash = *cmd % TABLES_HASH;
  command_entry *entry;

  for ( entry = first_handler[hash]; entry; entry = entry->next )
    if ( !strcmp( cmd, entry->cmd ) )
      return entry;

  return NULL;
}
