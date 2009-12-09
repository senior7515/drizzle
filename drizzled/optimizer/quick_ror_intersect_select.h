/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008-2009 Sun Microsystems
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

#ifndef DRIZZLED_OPTIMIZER_QUICK_ROR_INTERSECT_SELECT_H
#define DRIZZLED_OPTIMIZER_QUICK_ROR_INTERSECT_SELECT_H

#include "drizzled/optimizer/range.h"

namespace drizzled
{

namespace optimizer
{

/**
  Rowid-Ordered Retrieval (ROR) index intersection quick select.
  This quick select produces intersection of row sequences returned
  by several QuickRangeSelects it "merges".

  All merged QuickRangeSelects must return rowids in rowid order.
  QUICK_ROR_INTERSECT_SELECT will return rows in rowid order, too.

  All merged quick selects retrieve {rowid, covered_fields} tuples (not full
  table records).
  QUICK_ROR_INTERSECT_SELECT retrieves full records if it is not being used
  by QUICK_ROR_INTERSECT_SELECT and all merged quick selects together don't
  cover needed all fields.

  If one of the merged quick selects is a Clustered PK range scan, it is
  used only to filter rowid sequence produced by other merged quick selects.
*/
class QUICK_ROR_INTERSECT_SELECT : public QuickSelectInterface
{
public:

  QUICK_ROR_INTERSECT_SELECT(Session *session, 
                             Table *table,
                             bool retrieve_full_rows,
                             MEM_ROOT *parent_alloc);

  ~QUICK_ROR_INTERSECT_SELECT();

  /**
   * Do post-constructor initialization.
   * SYNOPSIS
   * QUICK_ROR_INTERSECT_SELECT::init()
   *
   * RETURN
   * @retval 0      OK
   * @retval other  Error code
   */
  int init();

  /**
   * Initialize quick select for row retrieval.
   * SYNOPSIS
   * reset()
   * RETURN
   * @retval 0      OK
   * @retval other  Error code
   */
  int reset(void);

  /**
   * Retrieve next record.
   * SYNOPSIS
   * QUICK_ROR_INTERSECT_SELECT::get_next()
   *
   * NOTES
   * Invariant on enter/exit: all intersected selects have retrieved all index
   * records with rowid <= some_rowid_val and no intersected select has
   * retrieved any index records with rowid > some_rowid_val.
   * We start fresh and loop until we have retrieved the same rowid in each of
   * the key scans or we got an error.
   *
   * If a Clustered PK scan is present, it is used only to check if row
   * satisfies its condition (and never used for row retrieval).
   *
   * RETURN
   * @retval 0     - Ok
   * @retval other - Error code if any error occurred.
   */
  int get_next();

  bool reverse_sorted() const
  {
    return false;
  }

  bool unique_key_range() const
  {
    return false;
  }

  int get_type() const
  {
    return QS_TYPE_ROR_INTERSECT;
  }

  void add_keys_and_lengths(String *key_names, String *used_lengths);
  void add_info_string(String *str);
  bool is_keys_used(const MyBitmap *fields);

  /**
   * Initialize this quick select to be a part of a ROR-merged scan.
   * SYNOPSIS
   * QUICK_ROR_INTERSECT_SELECT::init_ror_merged_scan()
   * reuse_handler If true, use head->cursor, otherwise create separate
   * Cursor object.
   * RETURN
   * @retval 0     OK
   * @retval other error code
   */
  int init_ror_merged_scan(bool reuse_handler);

  /**
   * Add a merged quick select to this ROR-intersection quick select.
   *
   * SYNOPSIS
   * QUICK_ROR_INTERSECT_SELECT::push_quick_back()
   * quick Quick select to be added. The quick select must return
   * rows in rowid order.
   * NOTES
   * This call can only be made before init() is called.
   *
   * RETURN
   * @retval false OK
   * @retval true  Out of memory.
   */
  bool push_quick_back(QuickRangeSelect *quick_sel_range);

  /**
   * Range quick selects this intersection consists of, not including
   * cpk_quick.
   */
  List<QuickRangeSelect> quick_selects;

  /**
   * Merged quick select that uses Clustered PK, if there is one. This quick
   * select is not used for row retrieval, it is used for row retrieval.
   */
  QuickRangeSelect *cpk_quick;

  MEM_ROOT alloc; /**< Memory pool for this and merged quick selects data. */
  Session *session; /**< Pointer to the current session */
  bool need_to_fetch_row; /**< if true, do retrieve full table records. */
  /** in top-level quick select, true if merged scans where initialized */
  bool scans_inited;
};

} /* namespace optimizer */

} /* namespace drizzled */

#endif /* DRIZZLED_OPTIMIZER_QUICK_ROR_INTERSECT_SELECT_H */
