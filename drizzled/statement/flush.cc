/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
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

#include "config.h"
#include <drizzled/show.h>
#include <drizzled/session.h>
#include <drizzled/lock.h>
#include <drizzled/statement/flush.h>
#include "drizzled/sql_table.h"

namespace drizzled
{

bool statement::Flush::execute()
{
  /*
   * reloadCache() will tell us if we are allowed to write to the
   * binlog or not.
   */
  if (! reloadCache())
  {
    /*
     * We WANT to write and we CAN write.
     * ! we write after unlocking the table.
     *
     * Presumably, RESET and binlog writing doesn't require synchronization
     */
    write_bin_log(session, session->query.c_str());
    session->my_ok();
  }

  return false;
}

bool statement::Flush::reloadCache()
{
  bool result= false;
  TableList *tables= (TableList *) session->lex->select_lex.table_list.first;

  if (flush_log)
  {
    if (plugin::StorageEngine::flushLogs(NULL))
    {
      result= true;
    }
  }
  /*
    Note that if REFRESH_READ_LOCK bit is set then REFRESH_TABLES is set too
    (see sql_yacc.yy)
  */
  if (flush_tables || flush_tables_with_read_lock)
  {
    if (session && flush_tables_with_read_lock)
    {
      if (session->lockGlobalReadLock())
      {
        return true; /* Killed */
      }
      result= session->close_cached_tables(tables, true, true);

      if (session->makeGlobalReadLockBlockCommit()) /* Killed */
      {
        /* Don't leave things in a half-locked state */
        session->unlockGlobalReadLock();
        return true;
      }
    }
    else
    {
      result= session->close_cached_tables(tables, true, false);
    }
  }

  if (session && flush_status)
  {
    session->refresh_status();
  }

 return result;
}

}
