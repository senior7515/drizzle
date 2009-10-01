/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include "drizzled/server_includes.h"
#include "drizzled/plugin/registry.h"

#include "drizzled/plugin.h"
#include "drizzled/show.h"
#include "drizzled/handler.h"
#include "drizzled/errmsg.h"
#include "drizzled/authentication.h"
#include "drizzled/qcache.h"
#include "drizzled/scheduling.h"
#include "drizzled/logging.h"
#include "drizzled/replication_services.h"

#include <string>
#include <vector>
#include <map>

using namespace std;
using namespace drizzled;


plugin::Handle *plugin::Registry::find(const LEX_STRING *name)
{
  string find_str(name->str,name->length);
  transform(find_str.begin(), find_str.end(), find_str.begin(), ::tolower);

  map<string, plugin::Handle *>::iterator map_iter;
  map_iter= plugin_map.find(find_str);
  if (map_iter != plugin_map.end())
    return (*map_iter).second;
  return(0);
}

void plugin::Registry::add(plugin::Handle *plugin)
{
  string add_str(plugin->getName());
  transform(add_str.begin(), add_str.end(),
            add_str.begin(), ::tolower);

  plugin_map[add_str]= plugin;
}


vector<plugin::Handle *> plugin::Registry::get_list(bool active)
{
  plugin::Handle *plugin= NULL;

  vector <plugin::Handle *> plugins;
  plugins.reserve(plugin_map.size());

  map<string, plugin::Handle *>::iterator map_iter;
  for (map_iter= plugin_map.begin();
       map_iter != plugin_map.end();
       map_iter++)
  {
    plugin= (*map_iter).second;
    if (active)
      plugins.push_back(plugin);
    else if (plugin->isInited)
      plugins.push_back(plugin);
  }

  return plugins;
}

void plugin::Registry::add(StorageEngine *engine)
{
  add_storage_engine(engine);
}

void plugin::Registry::add(InfoSchemaTable *schema_table)
{
  add_infoschema_table(schema_table);
}

void plugin::Registry::add(Logging_handler *handler)
{
  add_logger(handler);
}

void plugin::Registry::add(Error_message_handler *handler)
{
  add_errmsg_handler(handler);
}

void plugin::Registry::add(Authentication *auth)
{
  add_authentication(auth);
}

void plugin::Registry::add(QueryCache *qcache)
{
  add_query_cache(qcache);
}

void plugin::Registry::add(plugin::SchedulerFactory *factory)
{
  add_scheduler_factory(factory);
}


void plugin::Registry::add(drizzled::plugin::CommandReplicator *replicator)
{
  add_replicator(replicator);
}

void plugin::Registry::add(drizzled::plugin::CommandApplier *applier)
{
  add_applier(applier);
}

void plugin::Registry::remove(StorageEngine *engine)
{
  remove_storage_engine(engine);
}

void plugin::Registry::remove(InfoSchemaTable *schema_table)
{
  remove_infoschema_table(schema_table);
}

void plugin::Registry::remove(Logging_handler *handler)
{
  remove_logger(handler);
}

void plugin::Registry::remove(Error_message_handler *handler)
{
  remove_errmsg_handler(handler);
}

void plugin::Registry::remove(Authentication *auth)
{
  remove_authentication(auth);
}

void plugin::Registry::remove(QueryCache *qcache)
{
  remove_query_cache(qcache);
}

void plugin::Registry::remove(plugin::SchedulerFactory *factory)
{
  remove_scheduler_factory(factory);
}


void plugin::Registry::remove(drizzled::plugin::CommandReplicator *replicator)
{
  remove_replicator(replicator);
}

void plugin::Registry::remove(drizzled::plugin::CommandApplier *applier)
{
  remove_applier(applier);
}