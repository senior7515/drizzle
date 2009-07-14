/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
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

/*  Extra functions used by unireg library */

#ifndef DRIZZLED_UNIREG_H
#define DRIZZLED_UNIREG_H

#include <drizzled/structs.h>				/* All structs we need */

void unireg_end(void) __attribute__((noreturn));
void unireg_abort(int exit_code) __attribute__((noreturn));

int rea_create_table(Session *session, const char *path,
                     const char *db, const char *table_name,
                     drizzled::message::Table *table_proto,
                     HA_CREATE_INFO *create_info,
                     List<CreateField> &create_field,
                     uint32_t key_count,KEY *key_info,
                     bool is_like);


#endif /* DRIZZLED_UNIREG_H */
