/* - mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
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

/*
 * This is just the header file, the actual code is under a BSD license.
 */

#ifndef DRIZZLED_UTIL_GMTIME_H
#define DRIZZLED_UTIL_GMTIME_H

#include <time.h>

namespace drizzled
{

namespace util
{

struct tm *gmtime(const type::Time::epoch_t &timer, struct tm *tmbuf);
struct tm *localtime(const type::Time::epoch_t &timer, struct tm *tmbuf);
time_t mktime(struct tm *tmbuf);

} /* namespace util */
} /* namespace drizzled */

#endif /* DRIZZLED_UTIL_GMTIME_H */

