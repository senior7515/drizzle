/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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

/* 
  This is a "work in progress". The concept needs to be replicated throughout
  the code, but we will start with baby steps for the moment. To not incur
  cost until we are complete, for the moment it will do no allocation.

  This is mainly here so that it can be used in the SE interface for
  the time being.

  This will replace Table_ident.
  */

#ifndef DRIZZLED_IDENTIFIER_TABLE_H
#define DRIZZLED_IDENTIFIER_TABLE_H

#include <drizzled/enum.h>
#include "drizzled/definitions.h"
#include "drizzled/message/table.pb.h"

#include "drizzled/identifier.h"

#include <string.h>

#include <assert.h>

#include <ostream>
#include <set>
#include <algorithm>
#include <functional>

#include <boost/functional/hash.hpp>

namespace drizzled {

class Table;

class TableIdentifier : public SchemaIdentifier
{
public:
  typedef message::Table::TableType Type;

  class Key
  {
    std::vector<char> key_buffer;
    size_t hash_value;

  public:

    Key() :
      hash_value(0)
    {
    }

    const char *vector() const
    {
      return &key_buffer[0];
    }

    std::vector<char> &vectorPtr()
    {
      return key_buffer;
    }

    void set(size_t resize_arg, const std::string &a, const std::string &b);

    friend bool operator==(const Key &left, const Key &right)
    {
      if (left.hash_value == right.hash_value and left.key_buffer.size() == right.key_buffer.size())
      {
        if (memcmp(&left.key_buffer[0], &right.key_buffer[0], left.key_buffer.size()) == 0)
          return true;
      }

      return false;
    }

    friend bool operator<(const Key &left, const Key &right)
    {
      return left.key_buffer < right.key_buffer;
    }

    size_t size() const
    {
      return key_buffer.size();
    }

    size_t getHashValue() const
    {
      return hash_value;
    }
  };

private:

  Type type;
  std::string path;
  std::string table_name;
  std::string sql_path;
  Key key;
  size_t hash_value;

  void init();

  size_t getKeySize() const
  {
    return getSchemaName().size() + getTableName().size() + 2;
  }

public:
  TableIdentifier(const Table &table);
                   
  TableIdentifier( const SchemaIdentifier &schema,
                   const std::string &table_name_arg,
                   Type tmp_arg= message::Table::STANDARD) :
    SchemaIdentifier(schema),
    type(tmp_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  TableIdentifier( const std::string &db_arg,
                   const std::string &table_name_arg,
                   Type tmp_arg= message::Table::STANDARD) :
    SchemaIdentifier(db_arg),
    type(tmp_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  TableIdentifier( const std::string &schema_name_arg,
                   const std::string &table_name_arg,
                   const std::string &path_arg ) :
    SchemaIdentifier(schema_name_arg),
    type(message::Table::TEMPORARY),
    path(path_arg),
    table_name(table_name_arg)
  { 
    init();
  }

  using SchemaIdentifier::compare;

  bool isTmp() const
  {
    if (type == message::Table::TEMPORARY || type == message::Table::INTERNAL)
      return true;
    return false;
  }

  static bool isView(message::Table::TableType arg) // Not a SQL view, but a view for I_S
  {
    switch (arg)
    {
    default:
    case message::Table::STANDARD:
    case message::Table::TEMPORARY:
    case message::Table::INTERNAL:
      break;
    case message::Table::FUNCTION:
      return true;
    }

    return false;
  }

  bool isView() const // Not a SQL view, but a view for I_S
  {
    return isView(type);
  }

  Type getType() const
  {
    return type;
  }

  const std::string &getSQLPath();

  const std::string &getPath() const;

  void setPath(const std::string &new_path)
  {
    path= new_path;
  }

  const std::string &getTableName() const
  {
    return table_name;
  }

  void copyToTableMessage(message::Table &message) const;

  friend bool operator<(const TableIdentifier &left, const TableIdentifier &right)
  {
    if (left.getKey() < right.getKey())
    {
      return true;
    }

    return false;
  }

  friend std::ostream& operator<<(std::ostream& output, const TableIdentifier &identifier)
  {
    const char *type_str;

    output << "TableIdentifier:(";
    output <<  identifier.getSchemaName();
    output << ", ";
    output << identifier.getTableName();
    output << ", ";

    switch (identifier.type) {
    case message::Table::STANDARD:
      type_str= "standard";
      break;
    case message::Table::INTERNAL:
      type_str= "internal";
      break;
    case message::Table::TEMPORARY:
      type_str= "temporary";
      break;
    case message::Table::FUNCTION:
      type_str= "function";
      break;
    }

    output << type_str;
    output << ", ";
    output << identifier.path;
    output << ", ";
    output << identifier.getHashValue();
    output << ")";

    return output;  // for multiple << operators.
  }

  friend bool operator==(TableIdentifier &left, TableIdentifier &right)
  {
    if (left.getHashValue() == right.getHashValue())
    {
      if (left.getKey() == right.getKey())
        return true;
    }

    return false;
  }

  static uint32_t filename_to_tablename(const char *from, char *to, uint32_t to_length);
  static size_t build_table_filename(std::string &path, const std::string &db, const std::string &table_name, bool is_tmp);
  static size_t build_tmptable_filename(std::string &buffer);
  static size_t build_tmptable_filename(std::vector<char> &buffer);

public:

  size_t getHashValue() const
  {
    return hash_value;
  }

  const Key &getKey() const
  {
    return key;
  }
};

std::size_t hash_value(TableIdentifier const& b);
std::size_t hash_value(TableIdentifier::Key const& b);

typedef std::vector <TableIdentifier> TableIdentifiers;

} /* namespace drizzled */

#endif /* DRIZZLED_IDENTIFIER_TABLE_H */
