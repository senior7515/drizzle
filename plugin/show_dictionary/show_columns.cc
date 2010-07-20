/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2010 Brian Aker
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
#include "plugin/show_dictionary/dictionary.h"
#include "drizzled/identifier.h"


using namespace std;
using namespace drizzled;

static const string VARCHAR("VARCHAR");
static const string DOUBLE("DOUBLE");
static const string BLOB("BLOB");
static const string ENUM("ENUM");
static const string INTEGER("INTEGER");
static const string BIGINT("BIGINT");
static const string DECIMAL("DECIMAL");
static const string DATE("DATE");
static const string TIMESTAMP("TIMESTAMP");
static const string DATETIME("DATETIME");

ShowColumns::ShowColumns() :
  plugin::TableFunction("DATA_DICTIONARY", "SHOW_COLUMNS")
{
  add_field("Field");
  add_field("Type");
  add_field("Null", plugin::TableFunction::BOOLEAN, 0 , false);
  add_field("Default");
  add_field("Default_is_NULL", plugin::TableFunction::BOOLEAN, 0, false);
  add_field("On_Update");
}

ShowColumns::Generator::Generator(Field **arg) :
  plugin::TableFunction::Generator(arg),
  is_tables_primed(false),
  is_columns_primed(false),
  column_iterator(0)
{
  statement::Select *select= static_cast<statement::Select *>(getSession().lex->statement);

  if (not select->getShowTable().empty() && not select->getShowSchema().empty())
  {
    table_name.append(select->getShowTable().c_str());
    TableIdentifier identifier(select->getShowSchema().c_str(), select->getShowTable().c_str());

    is_tables_primed= plugin::StorageEngine::getTableDefinition(getSession(),
                                                                identifier,
                                                                table_proto);
  }
}

bool ShowColumns::Generator::nextColumnCore()
{
  if (is_columns_primed)
  {
    column_iterator++;
  }
  else
  {
    if (not isTablesPrimed())
      return false;

    column_iterator= 0;
    is_columns_primed= true;
  }

  if (column_iterator >= getTableProto().field_size())
    return false;

  column= getTableProto().field(column_iterator);

  return true;
}


bool ShowColumns::Generator::nextColumn()
{
  while (not nextColumnCore())
  {
    return false;
  }

  return true;
}

bool ShowColumns::Generator::populate()
{

  if (not nextColumn())
    return false;

  fill();

  return true;
}

void ShowColumns::Generator::pushType(message::Table::Field::FieldType type)
{
  switch (type)
  {
  default:
  case message::Table::Field::VARCHAR:
    push(VARCHAR);
    break;
  case message::Table::Field::DOUBLE:
    push(DOUBLE);
    break;
  case message::Table::Field::BLOB:
    push(BLOB);
    break;
  case message::Table::Field::ENUM:
    push(ENUM);
    break;
  case message::Table::Field::INTEGER:
    push(INTEGER);
    break;
  case message::Table::Field::BIGINT:
    push(BIGINT);
    break;
  case message::Table::Field::DECIMAL:
    push(DECIMAL);
    break;
  case message::Table::Field::DATE:
    push(DATE);
    break;
  case message::Table::Field::TIMESTAMP:
    push(TIMESTAMP);
    break;
  case message::Table::Field::DATETIME:
    push(DATETIME);
    break;
  }
}


void ShowColumns::Generator::fill()
{
  /* Field */
  push(column.name());

  /* Type */
  pushType(column.type());

  /* Null */
  push(column.constraints().is_nullable());

  /* Default */
  push(column.options().default_value());

  /* Default_is_NULL */
  push(column.options().default_null());

  /* On_Update */
  push(column.options().update_value());
}
