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
#include <drizzled/statement/drop_schema.h>
#include <drizzled/db.h>

#include <string>

using namespace std;

namespace drizzled
{

bool statement::DropSchema::execute()
{
  if (! session->endActiveTransaction())
  {
    return true;
  }
  if (check_db_name(&session->lex->name))
  {
    my_error(ER_WRONG_DB_NAME, MYF(0), session->lex->name.str);
    return false;
  }
  if (session->inTransaction())
  {
    my_message(ER_LOCK_OR_ACTIVE_TRANSACTION, 
        ER(ER_LOCK_OR_ACTIVE_TRANSACTION), 
        MYF(0));
    return true;
  }
  bool res= mysql_rm_db(session, session->lex->name.str, drop_if_exists);
  return res;
}

} /* namespace drizzled */

