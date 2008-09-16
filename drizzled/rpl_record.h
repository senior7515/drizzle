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

#ifndef RPL_RECORD_H
#define RPL_RECORD_H

#include <drizzled/rpl_reporting.h>

#if !defined(DRIZZLE_CLIENT)
size_t pack_row(Table* table, MY_BITMAP const* cols,
                uchar *row_data, const uchar *data);
#endif

#if !defined(DRIZZLE_CLIENT) && defined(HAVE_REPLICATION)
int unpack_row(Relay_log_info const *rli,
               Table *table, uint const colcnt,
               uchar const *const row_data, MY_BITMAP const *cols,
               uchar const **const row_end, ulong *const master_reclength);

// Fill table's record[0] with default values.
int prepare_record(Table *const, const MY_BITMAP *cols, uint width, const bool);
#endif

#endif
