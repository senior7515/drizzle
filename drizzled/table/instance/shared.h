/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2011 Brian Aker
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


#ifndef DRIZZLED_TABLE_INSTANCE_SHARED_H
#define DRIZZLED_TABLE_INSTANCE_SHARED_H

#include <drizzled/table/instance/base.h>

namespace drizzled
{

class TableShare;
class Session;

namespace identifier { class Table; }

namespace table
{

namespace instance
{

void release(TableShare *share);
void release(TableShare::shared_ptr &share);
void release(const identifier::Table &identifier);


class Shared : public drizzled::TableShare
{
  friend void release(TableShare *share);
  friend void release(TableShare::shared_ptr &share);

private:
  boost::mutex mutex;                /* For locking the share  */

public:
  typedef boost::shared_ptr<Shared> shared_ptr;
  typedef std::vector <shared_ptr> vector;

  Shared(const identifier::Table::Type type_arg,
         const identifier::Table &identifier,
         char *path_arg= NULL, uint32_t path_length_arg= 0); // Shares for cache

  Shared(const identifier::Table &identifier); // Used by placeholder

  ~Shared();


  void lock()
  {
    mutex.lock();
  }

  void unlock()
  {
    mutex.unlock();
  }

  static shared_ptr make_shared(Session *session, 
                                const identifier::Table &identifier,
                                int &in_error);

  static shared_ptr foundTableShare(shared_ptr share);
};

} /* namespace instance */
} /* namespace table */
} /* namespace drizzled */

#endif /* DRIZZLED_TABLE_INSTANCE_SHARED_H */
