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

#ifndef DRIZZLED_FUNCTION_MATH_INT_VAL_H
#define DRIZZLED_FUNCTION_MATH_INT_VAL_H

#include <drizzled/function/func.h>
#include <drizzled/function/num1.h>

namespace drizzled
{

class Item_func_int_val :public Item_func_num1
{
public:
  Item_func_int_val(Item *a) :Item_func_num1(a) {}
  void fix_num_length_and_dec();
  void find_num_type();
};

} /* namespace drizzled */

#endif /* DRIZZLED_FUNCTION_MATH_INT_VAL_H */
