/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <plugin/data_engine/dictionary.h>
#include <plugin/data_engine/cursor.h>

#include <string>

using namespace std;
using namespace drizzled;

static const string schema_name("data_dictionary");
static const string schema_name_prefix("./data_dictionary/");

Dictionary::Dictionary(const std::string &name_arg) :
  drizzled::plugin::StorageEngine(name_arg,
                                  HTON_ALTER_NOT_SUPPORTED |
                                  HTON_SKIP_STORE_LOCK |
                                  HTON_TEMPORARY_NOT_SUPPORTED)
{
}


Cursor *Dictionary::create(TableShare &table, memory::Root *mem_root)
{
  return new (mem_root) DictionaryCursor(*this, table);
}

Tool *Dictionary::getTool(const char *path)
{
  string tab_name(path);

  if (strcmp(path, "./data_dictionary/character_sets") == 0)
    return &character_sets;
  else if (strcmp(path, "./data_dictionary/collation_character_set_applicability") == 0)
    return &collation_character_set_applicability;
  else if (strcmp(path, "./data_dictionary/collations") == 0)
    return &collations;
  else if (strcmp(path, "./data_dictionary/columns") == 0)
    return &columns;
  else if (strcmp(path, "./data_dictionary/global_status") == 0)
    return &global_status;
  else if (strcmp(path, "./data_dictionary/global_variables") == 0)
    return &global_variables;
  else if (strcmp(path, "./data_dictionary/key_column_usage") == 0)
    return &key_column_usage;
  else if (strcmp(path, "./data_dictionary/modules") == 0)
    return &modules;
  else if (strcmp(path, "./data_dictionary/plugins") == 0)
    return &plugins;
  else if (strcmp(path, "./data_dictionary/processlist") == 0)
    return &processlist;
  else if (strcmp(path, "./data_dictionary/statistics") == 0)
    return &statistics;

  fprintf(stderr, "\n %s\n", path);
  assert(path == NULL);

  return NULL;
}


int Dictionary::doGetTableDefinition(Session &,
                                     const char *path,
                                     const char *,
                                     const char *,
                                     const bool,
                                     message::Table *table_proto)
{
  string tab_name(path);

  if (tab_name.compare(0, schema_name_prefix.length(), schema_name_prefix) != 0)
  {
    return ENOENT;
  }
  else if (strcmp(path, "./data_dictionary/character_sets") == 0)
  {
    if (table_proto)
    {
      character_sets.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/collations") == 0)
  {
    if (table_proto)
    {
      collations.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/collation_character_set_applicability") == 0)
  {
    if (table_proto)
    {
      collation_character_set_applicability.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/columns") == 0)
  {
    if (table_proto)
    {
      columns.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/global_status") == 0)
  {
    if (table_proto)
    {
      global_status.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/global_variables") == 0)
  {
    if (table_proto)
    {
      global_variables.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/key_column_usage") == 0)
  {
    if (table_proto)
    {
      key_column_usage.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/modules") == 0)
  {
    if (table_proto)
    {
      modules.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/processlist") == 0)
  {
    if (table_proto)
    {
      processlist.define(*table_proto);
    }
  }
  else if (strcmp(path, "./data_dictionary/plugins") == 0)
  {
    if (table_proto)
      plugins.define(*table_proto);
  }
  else if (strcmp(path, "./data_dictionary/statistics") == 0)
  {
    if (table_proto)
    {
      statistics.define(*table_proto);
    }
  }
  else
  {
    return ENOENT;
  }

  return EEXIST;
}


void Dictionary::doGetTableNames(drizzled::CachedDirectory&, 
                                        string &db, 
                                        set<string> &set_of_names)
{
  if (db.compare("data_dictionary"))
    return;

  set_of_names.insert("CHARACTER_SETS");
  set_of_names.insert("COLLATIONS");
  set_of_names.insert("COLLATION_CHARACTER_SET_APPLICABILITY");
  set_of_names.insert("COLUMNS");
  set_of_names.insert("GLOBAL_STATUS");
  set_of_names.insert("GLOBAL_STATUS");
  set_of_names.insert("GLOBAL_VARIABLES");
  set_of_names.insert("GLOBAL_VARIABLES");
  set_of_names.insert("KEY_COLUMN_USAGE");
  set_of_names.insert("MODULES");
  set_of_names.insert("PLUGINS");
  set_of_names.insert("PROCESSLIST");
  set_of_names.insert("REFERENTIAL_CONSTRAINTS");
  set_of_names.insert("SCHEMATA");
  set_of_names.insert("SESSION_STATUS");
  set_of_names.insert("SESSION_VARIABLES");
  set_of_names.insert("STATISTICS");
  set_of_names.insert("TABLES");
  set_of_names.insert("TABLE_CONSTRAINTS");
}


static plugin::StorageEngine *dictionary_plugin= NULL;

static int init(plugin::Registry &registry)
{
  dictionary_plugin= new(std::nothrow) Dictionary(engine_name);
  if (! dictionary_plugin)
  {
    return 1;
  }

  registry.add(dictionary_plugin);
  
  return 0;
}

static int finalize(plugin::Registry &registry)
{
  registry.remove(dictionary_plugin);
  delete dictionary_plugin;

  return 0;
}

DRIZZLE_DECLARE_PLUGIN
{
  DRIZZLE_VERSION_ID,
  "DICTIONARY",
  "1.0",
  "Brian Aker",
  "Dictionary provides the information for data_dictionary.",
  PLUGIN_LICENSE_GPL,
  init,     /* Plugin Init */
  finalize,     /* Plugin Deinit */
  NULL,               /* status variables */
  NULL,               /* system variables */
  NULL                /* config options   */
}
DRIZZLE_DECLARE_PLUGIN_END;
