/*
 *  tables.c
 *  Maps names of API commands (plain strings) to the API commands
 *  themselves (function pointers).
 */
#include "lyph.h"
#include "srv.h"

command_entry *first_handler[TABLES_HASH];
command_entry *last_handler[TABLES_HASH];

void init_command_table(void)
{
  add_handler( "all_correlations", do_all_correlations, CMD_READONLY );
  add_handler( "all_templates", do_all_templates, CMD_READONLY );
  add_handler( "all_located_measures", do_all_located_measures, CMD_READONLY );
  add_handler( "all_lyphnodes", do_all_lyphnodes, CMD_READONLY );
  add_handler( "all_lyphs", do_all_lyphs, CMD_READONLY );
  add_handler( "all_lyphviews", do_all_lyphviews, CMD_READONLY );
  add_handler( "all_ont_terms", do_all_ont_terms, CMD_READONLY );
  add_handler( "all_pubmeds", do_all_pubmeds, CMD_READONLY );
  add_handler( "all_clinical_indices", do_all_clinical_indices, CMD_READONLY );
  add_handler( "all_bops", do_all_bops, CMD_READONLY );
  add_handler( "bop", do_bop, CMD_READONLY );
  add_handler( "makebop", do_makebop, CMD_READWRITE );
  add_handler( "clinical_index", do_clinical_index, CMD_READONLY );
  add_handler( "clone", do_clone, CMD_READWRITE );
  add_handler( "correlation", do_correlation, CMD_READONLY );
  add_handler( "delete_correlation", do_delete_correlation, CMD_READWRITE );
  add_handler( "delete_located_measure", do_delete_located_measure, CMD_READWRITE );
  add_handler( "ontsearch", do_ontsearch, CMD_READONLY );
  add_handler( "pubmed", do_pubmed, CMD_READONLY );
  add_handler( "template_hierarchy", do_template_hierarchy, CMD_READONLY );
  add_handler( "assign_template", do_assign_template, CMD_READWRITE );
  add_handler( "get_csv", do_get_csv, CMD_READONLY );
  add_handler( "lyphconstrain", do_lyphconstrain, CMD_READWRITE );
  add_handler( "template_along_path", do_template_along_path, CMD_READWRITE );
  add_handler( "constrain_along_path", do_constrain_along_path, CMD_READWRITE );
  add_handler( "makecorrelation", do_makecorrelation, CMD_READWRITE );
  add_handler( "maketemplate", do_maketemplate, CMD_READWRITE );
  add_handler( "makelayer", do_makelayer, CMD_READWRITE );
  add_handler( "make_located_measure", do_make_located_measure, CMD_READWRITE );
  add_handler( "makelyph", do_makelyph, CMD_READWRITE );
  add_handler( "makelyphnode", do_makelyphnode, CMD_READWRITE );
  add_handler( "makeview", do_makeview, CMD_READWRITE );
  add_handler( "make_clinical_index", do_make_clinical_index, CMD_READWRITE );
  add_handler( "make_pubmed", do_make_pubmed, CMD_READWRITE );
  add_handler( "nodes_to_view", do_nodes_to_view, CMD_READWRITE );
  add_handler( "located_measure", do_located_measure, CMD_READONLY );
  add_handler( "lyphs_by_prefix", do_lyphs_by_prefix, CMD_READONLY );
  add_handler( "lyphs_to_view", do_nodes_to_view, CMD_READWRITE ); // Not a type -- lyphs_from_view and nodes_to_view are actually handled by the same handler function
  add_handler( "lyphs_from_view", do_lyphs_from_view, CMD_READWRITE );
  add_handler( "nodes_from_view", do_nodes_from_view, CMD_READWRITE );
  add_handler( "layer_from_template", do_layer_from_template, CMD_READWRITE );
  add_handler( "layer_to_template", do_layer_to_template, CMD_READWRITE );
  add_handler( "material_to_layer", do_material_to_layer, CMD_READWRITE );
  add_handler( "material_from_layer", do_material_from_layer, CMD_READWRITE );
  add_handler( "change_coords", do_change_coords, CMD_READWRITE );
  add_handler( "lyphpath", do_lyphpath, CMD_READONLY );
  add_handler( "connections", do_connections, CMD_READONLY );
  add_handler( "reset_db", do_reset_db, CMD_READWRITE );
  add_handler( "stats", do_stats, CMD_READONLY );
  add_handler( "template", do_template, CMD_READONLY );
  add_handler( "layer", do_layer, CMD_READONLY );
  add_handler( "lyph", do_lyph, CMD_READONLY );
  add_handler( "lyphnode", do_lyphnode, CMD_READONLY );
  add_handler( "lyphview", do_lyphview, CMD_READONLY );
  add_handler( "fma", do_fma, CMD_READONLY );
  add_handler( "subtemplates", do_subtemplates, CMD_READONLY );
  add_handler( "instances_of", do_instances_of, CMD_READONLY );
  add_handler( "involves_template", do_involves_template, CMD_READONLY );
  add_handler( "is_built_from_template", do_is_built_from_template, CMD_READONLY );
  add_handler( "has_template", do_has_template, CMD_READONLY );
  add_handler( "has_clinical_index", do_has_clinical_index, CMD_READONLY );
  add_handler( "templates_involving", do_templates_involving, CMD_READONLY );
  add_handler( "lyphs_located_in_term", do_lyphs_located_in_term, CMD_READONLY );
  add_handler( "unused_indices", do_unused_indices, CMD_READONLY );
  add_handler( "annotate", do_annotate, CMD_READWRITE );
  add_handler( "remove_annotations", do_remove_annotation, CMD_READWRITE );
  add_handler( "editlyph", do_editlyph, CMD_READWRITE );
  add_handler( "editlyphnode", do_editlyphnode, CMD_READWRITE );
  add_handler( "edit_template", do_edit_template, CMD_READWRITE );
  add_handler( "editview", do_editview, CMD_READWRITE );
  add_handler( "editlayer", do_editlayer, CMD_READWRITE );
  add_handler( "edit_clinical_index", do_edit_clinical_index, CMD_READWRITE );
  add_handler( "edit_pubmed", do_edit_pubmed, CMD_READWRITE );
  add_handler( "delete_lyphs", do_delete_lyphs, CMD_READWRITE );
  add_handler( "delete_nodes", do_delete_nodes, CMD_READWRITE );
  add_handler( "delete_templates", do_delete_templates, CMD_READWRITE );
  add_handler( "delete_views", do_delete_views, CMD_READWRITE );
  add_handler( "delete_layers", do_delete_layers, CMD_READWRITE );
  add_handler( "parse_csv", do_parse_csv, CMD_READWRITE );
  add_handler( "nifs", do_nifs, CMD_READONLY );

  //add_handler( "niflyph", do_niflyph, CMD_READWRITE );
  //add_handler( "nifconnection", do_nifconnection, CMD_READWRITE );
  add_handler( "renif", do_renif, CMD_READWRITE );
  //add_handler( "gen_random_correlations", do_gen_random_correlations, CMD_READWRITE );
  add_handler( "dotfile", do_dotfile, CMD_READONLY );
  //add_handler( "create_fmalyphs", do_create_fmalyphs, CMD_READWRITE );
  add_handler( "import_lateralized_brain", do_import_lateralized_brain, CMD_READWRITE );
}

void add_handler( char *cmd, do_function *fnc, int read_write_state )
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
