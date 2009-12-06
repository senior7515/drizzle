/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Definitions required for Error Message plugin
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

#ifndef DRIZZLED_PLUGIN_ERROR_MESSAGE_H
#define DRIZZLED_PLUGIN_ERROR_MESSAGE_H

#include "drizzled/plugin/plugin.h"

#include <stdarg.h>
#include <string>

namespace drizzled
{
namespace plugin
{

class ErrorMessage : public Plugin
{
  ErrorMessage();
  ErrorMessage(const ErrorMessage &);
  ErrorMessage& operator=(const ErrorMessage &);
public:
  explicit ErrorMessage(std::string name_arg)
   : Plugin(name_arg, "ErrorMessage")
  {}
  virtual ~ErrorMessage() {}

  virtual bool errmsg(Session *session, int priority,
                      const char *format, va_list ap)=0;

  static bool addPlugin(plugin::ErrorMessage *handler);
  static void removePlugin(plugin::ErrorMessage *handler);

  static bool vprintf(Session *session, int priority, char const *format,
                      va_list ap);
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_ERROR_MESSAGE_H */
