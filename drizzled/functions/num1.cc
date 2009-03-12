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

#include <drizzled/server_includes.h>

#include CSTDINT_H
#include <cassert>

#include <drizzled/functions/num1.h>

/**
  Set result type for a numeric function of one argument
  (can be also used by a numeric function of many arguments, if the result
  type depends only on the first argument)
*/

void Item_func_num1::find_num_type()
{
  switch (hybrid_type= args[0]->result_type()) {
  case INT_RESULT:
    unsigned_flag= args[0]->unsigned_flag;
    break;
  case STRING_RESULT:
  case REAL_RESULT:
    hybrid_type= REAL_RESULT;
    max_length= float_length(decimals);
    break;
  case DECIMAL_RESULT:
    break;
  default:
    assert(0);
  }
  return;
}

void Item_func_num1::fix_num_length_and_dec()
{
  decimals= args[0]->decimals;
  max_length= args[0]->max_length;
}


