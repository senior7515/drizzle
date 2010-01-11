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


#ifndef DRIZZLED_DB_H
#define DRIZZLED_DB_H

#include <list>
#include <drizzled/message/schema.pb.h>

class NormalisedDatabaseName;

bool mysql_create_db(Session *session, const NormalisedDatabaseName &database_name, drizzled::message::Schema *schema_message, std::list<drizzled::message::Schema::SchemaOption> *parsed_schema_options, bool is_if_not_exists);
bool mysql_alter_db(Session *session, const NormalisedDatabaseName &database_name, drizzled::message::Schema *schema_message);
bool mysql_rm_db(Session *session, const NormalisedDatabaseName &database_name, bool if_exists);
bool mysql_change_db(Session *session, const NormalisedDatabaseName &new_db_name, bool force_switch);

bool check_db_dir_existence(const char *db_name);
int get_database_metadata(const char *dbname, drizzled::message::Schema *db);

const CHARSET_INFO *get_default_db_collation(const char *db_name);

class NonNormalisedDatabaseName
{
private:
  std::string database_name;

  /* Copying a NonNormalisedDatabaseName is always wrong, it's
     immutable and should be passed by reference */
  NonNormalisedDatabaseName(const NonNormalisedDatabaseName&);

  NonNormalisedDatabaseName operator=(const NonNormalisedDatabaseName&);

public:
  explicit NonNormalisedDatabaseName(const std::string db) :
    database_name(db)
  {
  }

  const std::string &to_string(void) const
  {
    return database_name;
  }
};

class NormalisedDatabaseName
{
private:
  char* database_name;

  /* Copying a NormalisedDatabaseName is always wrong, it's
     immutable and should be passed by reference */
  NormalisedDatabaseName(const NormalisedDatabaseName&);
  NormalisedDatabaseName operator=(const NormalisedDatabaseName&);

public:
  explicit NormalisedDatabaseName(const NonNormalisedDatabaseName &dbname);

  ~NormalisedDatabaseName();

  const std::string to_string() const
  {
    std::string tmp(database_name);
    return tmp;
  }

  bool isValid() const;
};

class DatabasePathName
{
private:
  std::string database_path;

  /* Copying a DatabasePathName is always wrong, it's
     immutable and should be passed by reference. */
  DatabasePathName(const DatabasePathName&);
  DatabasePathName operator=(const DatabasePathName&);

public:
  explicit DatabasePathName(const NormalisedDatabaseName &database_name);

  const std::string to_string() const
  {
    return database_path;
  }

  bool exists() const;
};

#endif /* DRIZZLED_DB_H */
