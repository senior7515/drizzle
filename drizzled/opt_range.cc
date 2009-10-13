/* Copyright (C) 2000-2006 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*
  TODO:
  Fix that MAYBE_KEY are stored in the tree so that we can detect use
  of full hash keys for queries like:

  select s.id, kws.keyword_id from sites as s,kws where s.id=kws.site_id and kws.keyword_id in (204,205);

*/

/*
  This file contains:

  RangeAnalysisModule
    A module that accepts a condition, index (or partitioning) description,
    and builds lists of intervals (in index/partitioning space), such that
    all possible records that match the condition are contained within the
    intervals.
    The entry point for the range analysis module is get_mm_tree() function.

    The lists are returned in form of complicated structure of interlinked
    SEL_TREE/SEL_IMERGE/SEL_ARG objects.
    See quick_range_seq_next, find_used_partitions for examples of how to walk
    this structure.
    All direct "users" of this module are located within this file, too.


  PartitionPruningModule
    A module that accepts a partitioned table, condition, and finds which
    partitions we will need to use in query execution. Search down for
    "PartitionPruningModule" for description.
    The module has single entry point - prune_partitions() function.


  Range/index_merge/groupby-minmax optimizer module
    A module that accepts a table, condition, and returns
     - a QUICK_*_SELECT object that can be used to retrieve rows that match
       the specified condition, or a "no records will match the condition"
       statement.

    The module entry points are
      test_quick_select()
      get_quick_select_for_ref()


  Record retrieval code for range/index_merge/groupby-min-max.
    Implementations of QUICK_*_SELECT classes.

  KeyTupleFormat
  ~~~~~~~~~~~~~~
  The code in this file (and elsewhere) makes operations on key value tuples.
  Those tuples are stored in the following format:

  The tuple is a sequence of key part values. The length of key part value
  depends only on its type (and not depends on the what value is stored)

    KeyTuple: keypart1-data, keypart2-data, ...

  The value of each keypart is stored in the following format:

    keypart_data: [isnull_byte] keypart-value-bytes

  If a keypart may have a NULL value (key_part->field->real_maybe_null() can
  be used to check this), then the first byte is a NULL indicator with the
  following valid values:
    1  - keypart has NULL value.
    0  - keypart has non-NULL value.

  <questionable-statement> If isnull_byte==1 (NULL value), then the following
  keypart->length bytes must be 0.
  </questionable-statement>

  keypart-value-bytes holds the value. Its format depends on the field type.
  The length of keypart-value-bytes may or may not depend on the value being
  stored. The default is that length is static and equal to
  KEY_PART_INFO::length.

  Key parts with (key_part_flag & HA_BLOB_PART) have length depending of the
  value:

     keypart-value-bytes: value_length value_bytes

  The value_length part itself occupies HA_KEY_BLOB_LENGTH=2 bytes.

  See key_copy() and key_restore() for code to move data between index tuple
  and table record

  CAUTION: the above description is only sergefp's understanding of the
           subject and may omit some details.
*/

#include <drizzled/server_includes.h>
#include <drizzled/sql_base.h>
#include <drizzled/sql_select.h>
#include <drizzled/error.h>
#include <drizzled/cost_vect.h>
#include <drizzled/item/cmpfunc.h>
#include <drizzled/field/num.h>
#include <drizzled/check_stack_overrun.h>

#include "drizzled/temporal.h" /* Needed in get_mm_leaf() for timestamp -> datetime comparisons */

#include <string>
#include <vector>
#include <algorithm>

using namespace std;

#define HA_END_SPACE_KEY 0

/*
  Convert double value to #rows. Currently this does floor(), and we
  might consider using round() instead.
*/
static inline ha_rows double2rows(double x)
{
    return static_cast<ha_rows>(x);
}

extern "C" int refpos_order_cmp(void* arg, const void *a,const void *b)
{
  handler *file= (handler*)arg;
  return file->cmp_ref((const unsigned char*)a, (const unsigned char*)b);
}

static int sel_cmp(Field *f,unsigned char *a,unsigned char *b,uint8_t a_flag,uint8_t b_flag);

static unsigned char is_null_string[2]= {1,0};

class RANGE_OPT_PARAM;
/*
  A construction block of the SEL_ARG-graph.

  The following description only covers graphs of SEL_ARG objects with
  sel_arg->type==KEY_RANGE:

  One SEL_ARG object represents an "elementary interval" in form

      min_value <=?  table.keypartX  <=? max_value

  The interval is a non-empty interval of any kind: with[out] minimum/maximum
  bound, [half]open/closed, single-point interval, etc.

  1. SEL_ARG GRAPH STRUCTURE

  SEL_ARG objects are linked together in a graph. The meaning of the graph
  is better demostrated by an example:

     tree->keys[i]
      |
      |             $              $
      |    part=1   $     part=2   $    part=3
      |             $              $
      |  +-------+  $   +-------+  $   +--------+
      |  | kp1<1 |--$-->| kp2=5 |--$-->| kp3=10 |
      |  +-------+  $   +-------+  $   +--------+
      |      |      $              $       |
      |      |      $              $   +--------+
      |      |      $              $   | kp3=12 |
      |      |      $              $   +--------+
      |  +-------+  $              $
      \->| kp1=2 |--$--------------$-+
         +-------+  $              $ |   +--------+
             |      $              $  ==>| kp3=11 |
         +-------+  $              $ |   +--------+
         | kp1=3 |--$--------------$-+       |
         +-------+  $              $     +--------+
             |      $              $     | kp3=14 |
            ...     $              $     +--------+

  The entire graph is partitioned into "interval lists".

  An interval list is a sequence of ordered disjoint intervals over the same
  key part. SEL_ARG are linked via "next" and "prev" pointers. Additionally,
  all intervals in the list form an RB-tree, linked via left/right/parent
  pointers. The RB-tree root SEL_ARG object will be further called "root of the
  interval list".

    In the example pic, there are 4 interval lists:
    "kp<1 OR kp1=2 OR kp1=3", "kp2=5", "kp3=10 OR kp3=12", "kp3=11 OR kp3=13".
    The vertical lines represent SEL_ARG::next/prev pointers.

  In an interval list, each member X may have SEL_ARG::next_key_part pointer
  pointing to the root of another interval list Y. The pointed interval list
  must cover a key part with greater number (i.e. Y->part > X->part).

    In the example pic, the next_key_part pointers are represented by
    horisontal lines.

  2. SEL_ARG GRAPH SEMANTICS

  It represents a condition in a special form (we don't have a name for it ATM)
  The SEL_ARG::next/prev is "OR", and next_key_part is "AND".

  For example, the picture represents the condition in form:
   (kp1 < 1 AND kp2=5 AND (kp3=10 OR kp3=12)) OR
   (kp1=2 AND (kp3=11 OR kp3=14)) OR
   (kp1=3 AND (kp3=11 OR kp3=14))


  3. SEL_ARG GRAPH USE

  Use get_mm_tree() to construct SEL_ARG graph from WHERE condition.
  Then walk the SEL_ARG graph and get a list of dijsoint ordered key
  intervals (i.e. intervals in form

   (constA1, .., const1_K) < (keypart1,.., keypartK) < (constB1, .., constB_K)

  Those intervals can be used to access the index. The uses are in:
   - check_quick_select() - Walk the SEL_ARG graph and find an estimate of
                            how many table records are contained within all
                            intervals.
   - get_quick_select()   - Walk the SEL_ARG, materialize the key intervals,
                            and create QUICK_RANGE_SELECT object that will
                            read records within these intervals.

  4. SPACE COMPLEXITY NOTES

    SEL_ARG graph is a representation of an ordered disjoint sequence of
    intervals over the ordered set of index tuple values.

    For multi-part keys, one can construct a WHERE expression such that its
    list of intervals will be of combinatorial size. Here is an example:

      (keypart1 IN (1,2, ..., n1)) AND
      (keypart2 IN (1,2, ..., n2)) AND
      (keypart3 IN (1,2, ..., n3))

    For this WHERE clause the list of intervals will have n1*n2*n3 intervals
    of form

      (keypart1, keypart2, keypart3) = (k1, k2, k3), where 1 <= k{i} <= n{i}

    SEL_ARG graph structure aims to reduce the amount of required space by
    "sharing" the elementary intervals when possible (the pic at the
    beginning of this comment has examples of such sharing). The sharing may
    prevent combinatorial blowup:

      There are WHERE clauses that have combinatorial-size interval lists but
      will be represented by a compact SEL_ARG graph.
      Example:
        (keypartN IN (1,2, ..., n1)) AND
        ...
        (keypart2 IN (1,2, ..., n2)) AND
        (keypart1 IN (1,2, ..., n3))

    but not in all cases:

    - There are WHERE clauses that do have a compact SEL_ARG-graph
      representation but get_mm_tree() and its callees will construct a
      graph of combinatorial size.
      Example:
        (keypart1 IN (1,2, ..., n1)) AND
        (keypart2 IN (1,2, ..., n2)) AND
        ...
        (keypartN IN (1,2, ..., n3))

    - There are WHERE clauses for which the minimal possible SEL_ARG graph
      representation will have combinatorial size.
      Example:
        By induction: Let's take any interval on some keypart in the middle:

           kp15=c0

        Then let's AND it with this interval 'structure' from preceding and
        following keyparts:

          (kp14=c1 AND kp16=c3) OR keypart14=c2) (*)

        We will obtain this SEL_ARG graph:

             kp14     $      kp15      $      kp16
                      $                $
         +---------+  $   +---------+  $   +---------+
         | kp14=c1 |--$-->| kp15=c0 |--$-->| kp16=c3 |
         +---------+  $   +---------+  $   +---------+
              |       $                $
         +---------+  $   +---------+  $
         | kp14=c2 |--$-->| kp15=c0 |  $
         +---------+  $   +---------+  $
                      $                $

       Note that we had to duplicate "kp15=c0" and there was no way to avoid
       that.
       The induction step: AND the obtained expression with another "wrapping"
       expression like (*).
       When the process ends because of the limit on max. number of keyparts
       we'll have:

         WHERE clause length  is O(3*#max_keyparts)
         SEL_ARG graph size   is O(2^(#max_keyparts/2))

       (it is also possible to construct a case where instead of 2 in 2^n we
        have a bigger constant, e.g. 4, and get a graph with 4^(31/2)= 2^31
        nodes)

    We avoid consuming too much memory by setting a limit on the number of
    SEL_ARG object we can construct during one range analysis invocation.
*/

class SEL_ARG :public Sql_alloc
{
public:
  uint8_t min_flag,max_flag,maybe_flag;
  uint8_t part;					// Which key part
  uint8_t maybe_null;
  /*
    Number of children of this element in the RB-tree, plus 1 for this
    element itself.
  */
  uint16_t elements;
  /*
    Valid only for elements which are RB-tree roots: Number of times this
    RB-tree is referred to (it is referred by SEL_ARG::next_key_part or by
    SEL_TREE::keys[i] or by a temporary SEL_ARG* variable)
  */
  ulong use_count;

  Field *field;
  unsigned char *min_value,*max_value;			// Pointer to range

  /*
    eq_tree() requires that left == right == 0 if the type is MAYBE_KEY.
   */
  SEL_ARG *left,*right;   /* R-B tree children */
  SEL_ARG *next,*prev;    /* Links for bi-directional interval list */
  SEL_ARG *parent;        /* R-B tree parent */
  SEL_ARG *next_key_part;
  enum leaf_color { BLACK,RED } color;
  enum Type { IMPOSSIBLE, MAYBE, MAYBE_KEY, KEY_RANGE } type;

  enum { MAX_SEL_ARGS = 16000 };

  SEL_ARG() {}
  SEL_ARG(SEL_ARG &);
  SEL_ARG(Field *,const unsigned char *, const unsigned char *);
  SEL_ARG(Field *field, uint8_t part, unsigned char *min_value, unsigned char *max_value,
	  uint8_t min_flag, uint8_t max_flag, uint8_t maybe_flag);
  SEL_ARG(enum Type type_arg)
    :min_flag(0),elements(1),use_count(1),left(0),right(0),next_key_part(0),
    color(BLACK), type(type_arg)
  {}
  inline bool is_same(SEL_ARG *arg)
  {
    if (type != arg->type || part != arg->part)
      return 0;
    if (type != KEY_RANGE)
      return 1;
    return cmp_min_to_min(arg) == 0 && cmp_max_to_max(arg) == 0;
  }
  inline void merge_flags(SEL_ARG *arg) { maybe_flag|=arg->maybe_flag; }
  inline void maybe_smaller() { maybe_flag=1; }
  /* Return true iff it's a single-point null interval */
  inline bool is_null_interval() { return maybe_null && max_value[0] == 1; }
  inline int cmp_min_to_min(SEL_ARG* arg)
  {
    return sel_cmp(field,min_value, arg->min_value, min_flag, arg->min_flag);
  }
  inline int cmp_min_to_max(SEL_ARG* arg)
  {
    return sel_cmp(field,min_value, arg->max_value, min_flag, arg->max_flag);
  }
  inline int cmp_max_to_max(SEL_ARG* arg)
  {
    return sel_cmp(field,max_value, arg->max_value, max_flag, arg->max_flag);
  }
  inline int cmp_max_to_min(SEL_ARG* arg)
  {
    return sel_cmp(field,max_value, arg->min_value, max_flag, arg->min_flag);
  }
  SEL_ARG *clone_and(SEL_ARG* arg)
  {						// Get overlapping range
    unsigned char *new_min,*new_max;
    uint8_t flag_min,flag_max;
    if (cmp_min_to_min(arg) >= 0)
    {
      new_min=min_value; flag_min=min_flag;
    }
    else
    {
      new_min=arg->min_value; flag_min=arg->min_flag;
    }
    if (cmp_max_to_max(arg) <= 0)
    {
      new_max=max_value; flag_max=max_flag;
    }
    else
    {
      new_max=arg->max_value; flag_max=arg->max_flag;
    }
    return new SEL_ARG(field, part, new_min, new_max, flag_min, flag_max,
		       test(maybe_flag && arg->maybe_flag));
  }
  SEL_ARG *clone_first(SEL_ARG *arg)
  {						// min <= X < arg->min
    return new SEL_ARG(field,part, min_value, arg->min_value,
		       min_flag, arg->min_flag & NEAR_MIN ? 0 : NEAR_MAX,
		       maybe_flag | arg->maybe_flag);
  }
  SEL_ARG *clone_last(SEL_ARG *arg)
  {						// min <= X <= key_max
    return new SEL_ARG(field, part, min_value, arg->max_value,
		       min_flag, arg->max_flag, maybe_flag | arg->maybe_flag);
  }
  SEL_ARG *clone(RANGE_OPT_PARAM *param, SEL_ARG *new_parent, SEL_ARG **next);

  bool copy_min(SEL_ARG* arg)
  {						// Get overlapping range
    if (cmp_min_to_min(arg) > 0)
    {
      min_value=arg->min_value; min_flag=arg->min_flag;
      if ((max_flag & (NO_MAX_RANGE | NO_MIN_RANGE)) ==
	  (NO_MAX_RANGE | NO_MIN_RANGE))
	return 1;				// Full range
    }
    maybe_flag|=arg->maybe_flag;
    return 0;
  }
  bool copy_max(SEL_ARG* arg)
  {						// Get overlapping range
    if (cmp_max_to_max(arg) <= 0)
    {
      max_value=arg->max_value; max_flag=arg->max_flag;
      if ((max_flag & (NO_MAX_RANGE | NO_MIN_RANGE)) ==
	  (NO_MAX_RANGE | NO_MIN_RANGE))
	return 1;				// Full range
    }
    maybe_flag|=arg->maybe_flag;
    return 0;
  }

  void copy_min_to_min(SEL_ARG *arg)
  {
    min_value=arg->min_value; min_flag=arg->min_flag;
  }
  void copy_min_to_max(SEL_ARG *arg)
  {
    max_value=arg->min_value;
    max_flag=arg->min_flag & NEAR_MIN ? 0 : NEAR_MAX;
  }
  void copy_max_to_min(SEL_ARG *arg)
  {
    min_value=arg->max_value;
    min_flag=arg->max_flag & NEAR_MAX ? 0 : NEAR_MIN;
  }
  /* returns a number of keypart values (0 or 1) appended to the key buffer */
  int store_min(uint32_t length, unsigned char **min_key,uint32_t min_key_flag)
  {
    /* "(kp1 > c1) AND (kp2 OP c2) AND ..." -> (kp1 > c1) */
    if ((!(min_flag & NO_MIN_RANGE) &&
	!(min_key_flag & (NO_MIN_RANGE | NEAR_MIN))))
    {
      if (maybe_null && *min_value)
      {
	**min_key=1;
	memset(*min_key+1, 0, length-1);
      }
      else
	memcpy(*min_key,min_value,length);
      (*min_key)+= length;
      return 1;
    }
    return 0;
  }
  /* returns a number of keypart values (0 or 1) appended to the key buffer */
  int store_max(uint32_t length, unsigned char **max_key, uint32_t max_key_flag)
  {
    if (!(max_flag & NO_MAX_RANGE) &&
	!(max_key_flag & (NO_MAX_RANGE | NEAR_MAX)))
    {
      if (maybe_null && *max_value)
      {
	**max_key=1;
	memset(*max_key+1, 0, length-1);
      }
      else
	memcpy(*max_key,max_value,length);
      (*max_key)+= length;
      return 1;
    }
    return 0;
  }

  /* returns a number of keypart values appended to the key buffer */
  int store_min_key(KEY_PART *key, unsigned char **range_key, uint32_t *range_key_flag)
  {
    SEL_ARG *key_tree= first();
    uint32_t res= key_tree->store_min(key[key_tree->part].store_length,
                                  range_key, *range_key_flag);
    *range_key_flag|= key_tree->min_flag;

    if (key_tree->next_key_part &&
	key_tree->next_key_part->part == key_tree->part+1 &&
	!(*range_key_flag & (NO_MIN_RANGE | NEAR_MIN)) &&
	key_tree->next_key_part->type == SEL_ARG::KEY_RANGE)
      res+= key_tree->next_key_part->store_min_key(key, range_key,
                                                   range_key_flag);
    return res;
  }

  /* returns a number of keypart values appended to the key buffer */
  int store_max_key(KEY_PART *key, unsigned char **range_key, uint32_t *range_key_flag)
  {
    SEL_ARG *key_tree= last();
    uint32_t res=key_tree->store_max(key[key_tree->part].store_length,
                                 range_key, *range_key_flag);
    (*range_key_flag)|= key_tree->max_flag;
    if (key_tree->next_key_part &&
	key_tree->next_key_part->part == key_tree->part+1 &&
	!(*range_key_flag & (NO_MAX_RANGE | NEAR_MAX)) &&
	key_tree->next_key_part->type == SEL_ARG::KEY_RANGE)
      res+= key_tree->next_key_part->store_max_key(key, range_key,
                                                   range_key_flag);
    return res;
  }

  SEL_ARG *insert(SEL_ARG *key);
  SEL_ARG *tree_delete(SEL_ARG *key);
  SEL_ARG *find_range(SEL_ARG *key);
  SEL_ARG *rb_insert(SEL_ARG *leaf);
  friend SEL_ARG *rb_delete_fixup(SEL_ARG *root,SEL_ARG *key, SEL_ARG *par);
#ifdef EXTRA_DEBUG
  friend int test_rb_tree(SEL_ARG *element,SEL_ARG *parent);
  void test_use_count(SEL_ARG *root);
#endif
  SEL_ARG *first();
  SEL_ARG *last();
  void make_root();
  inline bool simple_key()
  {
    return !next_key_part && elements == 1;
  }
  void increment_use_count(long count)
  {
    if (next_key_part)
    {
      next_key_part->use_count+=count;
      count*= (next_key_part->use_count-count);
      for (SEL_ARG *pos=next_key_part->first(); pos ; pos=pos->next)
	if (pos->next_key_part)
	  pos->increment_use_count(count);
    }
  }
  void free_tree()
  {
    for (SEL_ARG *pos=first(); pos ; pos=pos->next)
      if (pos->next_key_part)
      {
	pos->next_key_part->use_count--;
	pos->next_key_part->free_tree();
      }
  }

  inline SEL_ARG **parent_ptr()
  {
    return parent->left == this ? &parent->left : &parent->right;
  }


  /*
    Check if this SEL_ARG object represents a single-point interval

    SYNOPSIS
      is_singlepoint()

    DESCRIPTION
      Check if this SEL_ARG object (not tree) represents a single-point
      interval, i.e. if it represents a "keypart = const" or
      "keypart IS NULL".

    RETURN
      true   This SEL_ARG object represents a singlepoint interval
      false  Otherwise
  */

  bool is_singlepoint()
  {
    /*
      Check for NEAR_MIN ("strictly less") and NO_MIN_RANGE (-inf < field)
      flags, and the same for right edge.
    */
    if (min_flag || max_flag)
      return false;
    unsigned char *min_val= min_value;
    unsigned char *max_val= max_value;

    if (maybe_null)
    {
      /* First byte is a NULL value indicator */
      if (*min_val != *max_val)
        return false;

      if (*min_val)
        return true; /* This "x IS NULL" */
      min_val++;
      max_val++;
    }
    return !field->key_cmp(min_val, max_val);
  }
  SEL_ARG *clone_tree(RANGE_OPT_PARAM *param);
};

class SEL_IMERGE;


class SEL_TREE :public Sql_alloc
{
public:
  /*
    Starting an effort to document this field:
    (for some i, keys[i]->type == SEL_ARG::IMPOSSIBLE) =>
       (type == SEL_TREE::IMPOSSIBLE)
  */
  enum Type { IMPOSSIBLE, ALWAYS, MAYBE, KEY, KEY_SMALLER } type;
  SEL_TREE(enum Type type_arg) :type(type_arg) {}
  SEL_TREE() :type(KEY)
  {
    keys_map.reset();
    memset(keys, 0, sizeof(keys));
  }
  /*
    Note: there may exist SEL_TREE objects with sel_tree->type=KEY and
    keys[i]=0 for all i. (SergeyP: it is not clear whether there is any
    merit in range analyzer functions (e.g. get_mm_parts) returning a
    pointer to such SEL_TREE instead of NULL)
  */
  SEL_ARG *keys[MAX_KEY];
  key_map keys_map;        /* bitmask of non-NULL elements in keys */

  /*
    Possible ways to read rows using index_merge. The list is non-empty only
    if type==KEY. Currently can be non empty only if keys_map.none().
  */
  vector<SEL_IMERGE*> merges;

  /* The members below are filled/used only after get_mm_tree is done */
  key_map ror_scans_map;   /* bitmask of ROR scan-able elements in keys */
  uint32_t    n_ror_scans;     /* number of set bits in ror_scans_map */

  struct st_ror_scan_info **ror_scans;     /* list of ROR key scans */
  struct st_ror_scan_info **ror_scans_end; /* last ROR scan */
  /* Note that #records for each key scan is stored in table->quick_rows */
};

class RANGE_OPT_PARAM
{
public:
  Session	*session;   /* Current thread handle */
  Table *table; /* Table being analyzed */
  COND *cond;   /* Used inside get_mm_tree(). */
  table_map prev_tables;
  table_map read_tables;
  table_map current_table; /* Bit of the table being analyzed */

  /* Array of parts of all keys for which range analysis is performed */
  KEY_PART *key_parts;
  KEY_PART *key_parts_end;
  MEM_ROOT *mem_root; /* Memory that will be freed when range analysis completes */
  MEM_ROOT *old_root; /* Memory that will last until the query end */
  /*
    Number of indexes used in range analysis (In SEL_TREE::keys only first
    #keys elements are not empty)
  */
  uint32_t keys;

  /*
    If true, the index descriptions describe real indexes (and it is ok to
    call field->optimize_range(real_keynr[...], ...).
    Otherwise index description describes fake indexes.
  */
  bool using_real_indexes;

  bool remove_jump_scans;

  /*
    used_key_no -> table_key_no translation table. Only makes sense if
    using_real_indexes==true
  */
  uint32_t real_keynr[MAX_KEY];
  /* Number of SEL_ARG objects allocated by SEL_ARG::clone_tree operations */
  uint32_t alloced_sel_args;
  bool force_default_mrr;
};

class PARAM : public RANGE_OPT_PARAM
{
public:
  KEY_PART *key[MAX_KEY]; /* First key parts of keys used in the query */
  int64_t baseflag;
  uint32_t max_key_part;
  /* Number of ranges in the last checked tree->key */
  uint32_t range_count;
  unsigned char min_key[MAX_KEY_LENGTH+MAX_FIELD_WIDTH],
    max_key[MAX_KEY_LENGTH+MAX_FIELD_WIDTH];
  bool quick;				// Don't calulate possible keys

  uint32_t fields_bitmap_size;
  MyBitmap needed_fields;    /* bitmask of fields needed by the query */
  MyBitmap tmp_covered_fields;

  key_map *needed_reg;        /* ptr to SQL_SELECT::needed_reg */

  uint32_t *imerge_cost_buff;     /* buffer for index_merge cost estimates */
  uint32_t imerge_cost_buff_size; /* size of the buffer */

  /* true if last checked tree->key can be used for ROR-scan */
  bool is_ror_scan;
  /* Number of ranges in the last checked tree->key */
  uint32_t n_ranges;
};

class TABLE_READ_PLAN;
  class TRP_RANGE;
  class TRP_ROR_INTERSECT;
  class TRP_ROR_UNION;
  class TRP_ROR_INDEX_MERGE;
  class TRP_GROUP_MIN_MAX;

struct st_ror_scan_info;

static SEL_TREE * get_mm_parts(RANGE_OPT_PARAM *param,COND *cond_func,Field *field,
			       Item_func::Functype type,Item *value,
			       Item_result cmp_type);
static SEL_ARG *get_mm_leaf(RANGE_OPT_PARAM *param,COND *cond_func,Field *field,
			    KEY_PART *key_part,
			    Item_func::Functype type,Item *value);
static SEL_TREE *get_mm_tree(RANGE_OPT_PARAM *param,COND *cond);

static bool is_key_scan_ror(PARAM *param, uint32_t keynr, uint8_t nparts);
static ha_rows check_quick_select(PARAM *param, uint32_t idx, bool index_only,
                                  SEL_ARG *tree, bool update_tbl_stats,
                                  uint32_t *mrr_flags, uint32_t *bufsize,
                                  COST_VECT *cost);
                                  //bool update_tbl_stats);
/*static ha_rows check_quick_keys(PARAM *param,uint32_t index,SEL_ARG *key_tree,
                                unsigned char *min_key, uint32_t min_key_flag, int,
                                unsigned char *max_key, uint32_t max_key_flag, int);
*/

QUICK_RANGE_SELECT *get_quick_select(PARAM *param,uint32_t index,
                                     SEL_ARG *key_tree, uint32_t mrr_flags,
                                     uint32_t mrr_buf_size, MEM_ROOT *alloc);
static TRP_RANGE *get_key_scans_params(PARAM *param, SEL_TREE *tree,
                                       bool index_read_must_be_used,
                                       bool update_tbl_stats,
                                       double read_time);
static
TRP_ROR_INTERSECT *get_best_ror_intersect(const PARAM *param, SEL_TREE *tree,
                                          double read_time,
                                          bool *are_all_covering);
static
TRP_ROR_INTERSECT *get_best_covering_ror_intersect(PARAM *param,
                                                   SEL_TREE *tree,
                                                   double read_time);
static
TABLE_READ_PLAN *get_best_disjunct_quick(PARAM *param, SEL_IMERGE *imerge,
                                         double read_time);
static
TRP_GROUP_MIN_MAX *get_best_group_min_max(PARAM *param, SEL_TREE *tree);

static void print_sel_tree(PARAM *param, SEL_TREE *tree, key_map *tree_map,
                           const char *msg);
static void print_ror_scans_arr(Table *table, const char *msg,
                                struct st_ror_scan_info **start,
                                struct st_ror_scan_info **end);

static SEL_TREE *tree_and(RANGE_OPT_PARAM *param,SEL_TREE *tree1,SEL_TREE *tree2);
static SEL_TREE *tree_or(RANGE_OPT_PARAM *param,SEL_TREE *tree1,SEL_TREE *tree2);
static SEL_ARG *sel_add(SEL_ARG *key1,SEL_ARG *key2);
static SEL_ARG *key_or(RANGE_OPT_PARAM *param, SEL_ARG *key1, SEL_ARG *key2);
static SEL_ARG *key_and(RANGE_OPT_PARAM *param, SEL_ARG *key1, SEL_ARG *key2,
                        uint32_t clone_flag);
static bool get_range(SEL_ARG **e1,SEL_ARG **e2,SEL_ARG *root1);
bool get_quick_keys(PARAM *param,QUICK_RANGE_SELECT *quick,KEY_PART *key,
                    SEL_ARG *key_tree, unsigned char *min_key,uint32_t min_key_flag,
                    unsigned char *max_key,uint32_t max_key_flag);
static bool eq_tree(SEL_ARG* a,SEL_ARG *b);

static SEL_ARG null_element(SEL_ARG::IMPOSSIBLE);
static bool null_part_in_key(KEY_PART *key_part, const unsigned char *key,
                             uint32_t length);
bool sel_trees_can_be_ored(SEL_TREE *tree1, SEL_TREE *tree2, RANGE_OPT_PARAM* param);


/*
  SEL_IMERGE is a list of possible ways to do index merge, i.e. it is
  a condition in the following form:
   (t_1||t_2||...||t_N) && (next)

  where all t_i are SEL_TREEs, next is another SEL_IMERGE and no pair
  (t_i,t_j) contains SEL_ARGS for the same index.

  SEL_TREE contained in SEL_IMERGE always has merges=NULL.

  This class relies on memory manager to do the cleanup.
*/

class SEL_IMERGE : public Sql_alloc
{
  enum { PREALLOCED_TREES= 10};
public:
  SEL_TREE *trees_prealloced[PREALLOCED_TREES];
  SEL_TREE **trees;             /* trees used to do index_merge   */
  SEL_TREE **trees_next;        /* last of these trees            */
  SEL_TREE **trees_end;         /* end of allocated space         */

  SEL_ARG  ***best_keys;        /* best keys to read in SEL_TREEs */

  SEL_IMERGE() :
    trees(&trees_prealloced[0]),
    trees_next(trees),
    trees_end(trees + PREALLOCED_TREES)
  {}
  int or_sel_tree(RANGE_OPT_PARAM *param, SEL_TREE *tree);
  int or_sel_tree_with_checks(RANGE_OPT_PARAM *param, SEL_TREE *new_tree);
  int or_sel_imerge_with_checks(RANGE_OPT_PARAM *param, SEL_IMERGE* imerge);
};


/*
  Add SEL_TREE to this index_merge without any checks,

  NOTES
    This function implements the following:
      (x_1||...||x_N) || t = (x_1||...||x_N||t), where x_i, t are SEL_TREEs

  RETURN
     0 - OK
    -1 - Out of memory.
*/

int SEL_IMERGE::or_sel_tree(RANGE_OPT_PARAM *param, SEL_TREE *tree)
{
  if (trees_next == trees_end)
  {
    const int realloc_ratio= 2;		/* Double size for next round */
    uint32_t old_elements= (trees_end - trees);
    uint32_t old_size= sizeof(SEL_TREE**) * old_elements;
    uint32_t new_size= old_size * realloc_ratio;
    SEL_TREE **new_trees;
    if (!(new_trees= (SEL_TREE**)alloc_root(param->mem_root, new_size)))
      return -1;
    memcpy(new_trees, trees, old_size);
    trees=      new_trees;
    trees_next= trees + old_elements;
    trees_end=  trees + old_elements * realloc_ratio;
  }
  *(trees_next++)= tree;
  return 0;
}


/*
  Perform OR operation on this SEL_IMERGE and supplied SEL_TREE new_tree,
  combining new_tree with one of the trees in this SEL_IMERGE if they both
  have SEL_ARGs for the same key.

  SYNOPSIS
    or_sel_tree_with_checks()
      param    PARAM from SQL_SELECT::test_quick_select
      new_tree SEL_TREE with type KEY or KEY_SMALLER.

  NOTES
    This does the following:
    (t_1||...||t_k)||new_tree =
     either
       = (t_1||...||t_k||new_tree)
     or
       = (t_1||....||(t_j|| new_tree)||...||t_k),

     where t_i, y are SEL_TREEs.
    new_tree is combined with the first t_j it has a SEL_ARG on common
    key with. As a consequence of this, choice of keys to do index_merge
    read may depend on the order of conditions in WHERE part of the query.

  RETURN
    0  OK
    1  One of the trees was combined with new_tree to SEL_TREE::ALWAYS,
       and (*this) should be discarded.
   -1  An error occurred.
*/

int SEL_IMERGE::or_sel_tree_with_checks(RANGE_OPT_PARAM *param, SEL_TREE *new_tree)
{
  for (SEL_TREE** tree = trees;
       tree != trees_next;
       tree++)
  {
    if (sel_trees_can_be_ored(*tree, new_tree, param))
    {
      *tree = tree_or(param, *tree, new_tree);
      if (!*tree)
        return 1;
      if (((*tree)->type == SEL_TREE::MAYBE) ||
          ((*tree)->type == SEL_TREE::ALWAYS))
        return 1;
      /* SEL_TREE::IMPOSSIBLE is impossible here */
      return 0;
    }
  }

  /* New tree cannot be combined with any of existing trees. */
  return or_sel_tree(param, new_tree);
}


/*
  Perform OR operation on this index_merge and supplied index_merge list.

  RETURN
    0 - OK
    1 - One of conditions in result is always true and this SEL_IMERGE
        should be discarded.
   -1 - An error occurred
*/

int SEL_IMERGE::or_sel_imerge_with_checks(RANGE_OPT_PARAM *param, SEL_IMERGE* imerge)
{
  for (SEL_TREE** tree= imerge->trees;
       tree != imerge->trees_next;
       tree++)
  {
    if (or_sel_tree_with_checks(param, *tree))
      return 1;
  }
  return 0;
}


/*
  Perform AND operation on two index_merge lists and store result in im1.
*/

inline void imerge_list_and_list(vector<SEL_IMERGE*> &im1, vector<SEL_IMERGE*> &im2)
{
  im1.insert(im1.end(), im2.begin(), im2.end());
  im2.clear();
}


/*
  Perform OR operation on 2 index_merge lists, storing result in first list.

  NOTES
    The following conversion is implemented:
     (a_1 &&...&& a_N)||(b_1 &&...&& b_K) = AND_i,j(a_i || b_j) =>
      => (a_1||b_1).

    i.e. all conjuncts except the first one are currently dropped.
    This is done to avoid producing N*K ways to do index_merge.

    If (a_1||b_1) produce a condition that is always true, NULL is returned
    and index_merge is discarded (while it is actually possible to try
    harder).

    As a consequence of this, choice of keys to do index_merge read may depend
    on the order of conditions in WHERE part of the query.

  RETURN
    0     OK, result is stored in *im1
    other Error, both passed lists are unusable
*/

static int imerge_list_or_list(RANGE_OPT_PARAM *param,
                               vector<SEL_IMERGE*> &im1,
                               vector<SEL_IMERGE*> &im2)
{
  SEL_IMERGE *imerge= im1.front();
  im1.clear();
  im1.push_back(imerge);

  return imerge->or_sel_imerge_with_checks(param, im2.front());
}


/*
  Perform OR operation on index_merge list and key tree.

  RETURN
    false   OK, result is stored in im1.
    true    Error
*/

static bool imerge_list_or_tree(RANGE_OPT_PARAM *param,
                                vector<SEL_IMERGE*> &im1,
                                SEL_TREE *tree)
{
  vector<SEL_IMERGE*>::iterator imerge= im1.begin();

  while (imerge != im1.end())
  {
    if ((*imerge)->or_sel_tree_with_checks(param, tree))
      imerge= im1.erase( imerge );
    else
      ++imerge;
  }

  return im1.empty();
}


/***************************************************************************
** Basic functions for SQL_SELECT and QUICK_RANGE_SELECT
***************************************************************************/

	/* make a select from mysql info
	   Error is set as following:
	   0 = ok
	   1 = Got some error (out of memory?)
	   */

SQL_SELECT *make_select(Table *head, table_map const_tables,
                        table_map read_tables, COND *conds,
                        bool allow_null_cond,
                        int *error)
{
  SQL_SELECT *select;

  *error=0;

  if (!conds && !allow_null_cond)
    return 0;
  if (!(select= new SQL_SELECT))
  {
    *error= 1;			// out of memory
    return 0;
  }
  select->read_tables=read_tables;
  select->const_tables=const_tables;
  select->head=head;
  select->cond=conds;

  if (head->sort.io_cache)
  {
    select->file= *head->sort.io_cache;
    select->records=(ha_rows) (select->file.end_of_file/
			       head->file->ref_length);
    delete head->sort.io_cache;
    head->sort.io_cache=0;
  }
  return(select);
}


SQL_SELECT::SQL_SELECT() :quick(0),cond(0),free_cond(0)
{
  quick_keys.reset(); 
  needed_reg.reset();
  my_b_clear(&file);
}


void SQL_SELECT::cleanup()
{
  delete quick;
  quick= 0;
  if (free_cond)
  {
    free_cond=0;
    delete cond;
    cond= 0;
  }
  close_cached_file(&file);
}


SQL_SELECT::~SQL_SELECT()
{
  cleanup();
}


bool SQL_SELECT::check_quick(Session *session, bool force_quick_range,
                             ha_rows limit)
{
  key_map tmp;
  tmp.set();
  return test_quick_select(session, tmp, 0, limit,
                           force_quick_range, false) < 0;
}


bool SQL_SELECT::skip_record()
{
  return cond ? cond->val_int() == 0 : 0;
}


QUICK_SELECT_I::QUICK_SELECT_I()
  :max_used_key_length(0),
   used_key_parts(0)
{}

QUICK_RANGE_SELECT::QUICK_RANGE_SELECT(Session *session, Table *table, uint32_t key_nr,
                                       bool no_alloc, MEM_ROOT *parent_alloc,
                                       bool *create_error)
  :free_file(0),cur_range(NULL),last_range(0),dont_free(0)
{
  my_bitmap_map *bitmap;

  in_ror_merged_scan= 0;
  sorted= 0;
  index= key_nr;
  head=  table;
  key_part_info= head->key_info[index].key_part;
  my_init_dynamic_array(&ranges, sizeof(QUICK_RANGE*), 16, 16);

  /* 'session' is not accessible in QUICK_RANGE_SELECT::reset(). */
  mrr_buf_size= session->variables.read_rnd_buff_size;
  mrr_buf_desc= NULL;

  if (!no_alloc && !parent_alloc)
  {
    // Allocates everything through the internal memroot
    init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
    session->mem_root= &alloc;
  }
  else
    memset(&alloc, 0, sizeof(alloc));
  file= head->file;
  record= head->record[0];
  save_read_set= head->read_set;
  save_write_set= head->write_set;

  /* Allocate a bitmap for used columns. Using sql_alloc instead of malloc
     simply as a "fix" to the MySQL 6.0 code that also free()s it at the
     same time we destroy the mem_root.
   */

  bitmap= reinterpret_cast<my_bitmap_map*>(sql_alloc(head->s->column_bitmap_size));
  if (! bitmap)
  {
    column_bitmap.setBitmap(NULL);
    *create_error= 1;
  }
  else
  {
    column_bitmap.init(bitmap, head->s->fields);
  }
}


int QUICK_RANGE_SELECT::init()
{
  if (file->inited != handler::NONE)
    file->ha_index_or_rnd_end();
  return(file->ha_index_init(index, 1));
}


void QUICK_RANGE_SELECT::range_end()
{
  if (file->inited != handler::NONE)
    file->ha_index_or_rnd_end();
}


QUICK_RANGE_SELECT::~QUICK_RANGE_SELECT()
{
  if (!dont_free)
  {
    /* file is NULL for CPK scan on covering ROR-intersection */
    if (file)
    {
      range_end();
      if (head->key_read)
      {
        head->key_read= 0;
        file->extra(HA_EXTRA_NO_KEYREAD);
      }
      if (free_file)
      {
        file->ha_external_lock(current_session, F_UNLCK);
        file->close();
        delete file;
      }
    }
    delete_dynamic(&ranges); /* ranges are allocated in alloc */
    free_root(&alloc,MYF(0));
  }
  head->column_bitmaps_set(save_read_set, save_write_set);
  assert(mrr_buf_desc == NULL);
  if (mrr_buf_desc)
    free(mrr_buf_desc);
}


QUICK_INDEX_MERGE_SELECT::QUICK_INDEX_MERGE_SELECT(Session *session_param,
                                                   Table *table)
  :pk_quick_select(NULL), session(session_param)
{
  index= MAX_KEY;
  head= table;
  memset(&read_record, 0, sizeof(read_record));
  init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
  return;
}

int QUICK_INDEX_MERGE_SELECT::init()
{
  return 0;
}

int QUICK_INDEX_MERGE_SELECT::reset()
{
  return(read_keys_and_merge());
}

bool
QUICK_INDEX_MERGE_SELECT::push_quick_back(QUICK_RANGE_SELECT *quick_sel_range)
{
  /*
    Save quick_select that does scan on clustered primary key as it will be
    processed separately.
  */
  if (head->file->primary_key_is_clustered() &&
      quick_sel_range->index == head->s->primary_key)
    pk_quick_select= quick_sel_range;
  else
    return quick_selects.push_back(quick_sel_range);
  return 0;
}

QUICK_INDEX_MERGE_SELECT::~QUICK_INDEX_MERGE_SELECT()
{
  List_iterator_fast<QUICK_RANGE_SELECT> quick_it(quick_selects);
  QUICK_RANGE_SELECT* quick;
  quick_it.rewind();
  while ((quick= quick_it++))
    quick->file= NULL;
  quick_selects.delete_elements();
  delete pk_quick_select;
  free_root(&alloc,MYF(0));
  return;
}


QUICK_ROR_INTERSECT_SELECT::QUICK_ROR_INTERSECT_SELECT(Session *session_param,
                                                       Table *table,
                                                       bool retrieve_full_rows,
                                                       MEM_ROOT *parent_alloc)
  : cpk_quick(NULL), session(session_param), need_to_fetch_row(retrieve_full_rows),
    scans_inited(false)
{
  index= MAX_KEY;
  head= table;
  record= head->record[0];
  if (!parent_alloc)
    init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
  else
    memset(&alloc, 0, sizeof(MEM_ROOT));
  last_rowid= (unsigned char*) alloc_root(parent_alloc? parent_alloc : &alloc,
                                  head->file->ref_length);
}


/*
  Do post-constructor initialization.
  SYNOPSIS
    QUICK_ROR_INTERSECT_SELECT::init()

  RETURN
    0      OK
    other  Error code
*/

int QUICK_ROR_INTERSECT_SELECT::init()
{
 /* Check if last_rowid was successfully allocated in ctor */
  return(!last_rowid);
}


/*
  Initialize this quick select to be a ROR-merged scan.

  SYNOPSIS
    QUICK_RANGE_SELECT::init_ror_merged_scan()
      reuse_handler If true, use head->file, otherwise create a separate
                    handler object

  NOTES
    This function creates and prepares for subsequent use a separate handler
    object if it can't reuse head->file. The reason for this is that during
    ROR-merge several key scans are performed simultaneously, and a single
    handler is only capable of preserving context of a single key scan.

    In ROR-merge the quick select doing merge does full records retrieval,
    merged quick selects read only keys.

  RETURN
    0  ROR child scan initialized, ok to use.
    1  error
*/

int QUICK_RANGE_SELECT::init_ror_merged_scan(bool reuse_handler)
{
  handler *save_file= file, *org_file;
  Session *session;

  in_ror_merged_scan= 1;
  if (reuse_handler)
  {
    if (init() || reset())
    {
      return 0;
    }
    head->column_bitmaps_set(&column_bitmap, &column_bitmap);
    goto end;
  }

  /* Create a separate handler object for this quick select */
  if (free_file)
  {
    /* already have own 'handler' object. */
    return 0;
  }

  session= head->in_use;
  if (!(file= head->file->clone(session->mem_root)))
  {
    /*
      Manually set the error flag. Note: there seems to be quite a few
      places where a failure could cause the server to "hang" the client by
      sending no response to a query. ATM those are not real errors because
      the storage engine calls in question happen to never fail with the
      existing storage engines.
    */
    my_error(ER_OUT_OF_RESOURCES, MYF(0));
    /* Caller will free the memory */
    goto failure;
  }

  head->column_bitmaps_set(&column_bitmap, &column_bitmap);

  if (file->ha_external_lock(session, F_RDLCK))
    goto failure;

  if (init() || reset())
  {
    file->ha_external_lock(session, F_UNLCK);
    file->close();
    goto failure;
  }
  free_file= true;
  last_rowid= file->ref;

end:
  /*
    We are only going to read key fields and call position() on 'file'
    The following sets head->tmp_set to only use this key and then updates
    head->read_set and head->write_set to use this bitmap.
    The now bitmap is stored in 'column_bitmap' which is used in ::get_next()
  */
  org_file= head->file;
  head->file= file;
  /* We don't have to set 'head->keyread' here as the 'file' is unique */
  if (!head->no_keyread)
  {
    head->key_read= 1;
    head->mark_columns_used_by_index(index);
  }
  head->prepare_for_position();
  head->file= org_file;
  column_bitmap= *head->read_set;
  head->column_bitmaps_set(&column_bitmap, &column_bitmap);

  return 0;

failure:
  head->column_bitmaps_set(save_read_set, save_write_set);
  delete file;
  file= save_file;
  return 0;
}


void QUICK_RANGE_SELECT::save_last_pos()
{
  file->position(record);
}


/*
  Initialize this quick select to be a part of a ROR-merged scan.
  SYNOPSIS
    QUICK_ROR_INTERSECT_SELECT::init_ror_merged_scan()
      reuse_handler If true, use head->file, otherwise create separate
                    handler object.
  RETURN
    0     OK
    other error code
*/
int QUICK_ROR_INTERSECT_SELECT::init_ror_merged_scan(bool reuse_handler)
{
  List_iterator_fast<QUICK_RANGE_SELECT> quick_it(quick_selects);
  QUICK_RANGE_SELECT* quick;

  /* Initialize all merged "children" quick selects */
  assert(!need_to_fetch_row || reuse_handler);
  if (!need_to_fetch_row && reuse_handler)
  {
    quick= quick_it++;
    /*
      There is no use of this->file. Use it for the first of merged range
      selects.
    */
    if (quick->init_ror_merged_scan(true))
      return 0;
    quick->file->extra(HA_EXTRA_KEYREAD_PRESERVE_FIELDS);
  }
  while ((quick= quick_it++))
  {
    if (quick->init_ror_merged_scan(false))
      return 0;
    quick->file->extra(HA_EXTRA_KEYREAD_PRESERVE_FIELDS);
    /* All merged scans share the same record buffer in intersection. */
    quick->record= head->record[0];
  }

  if (need_to_fetch_row && head->file->ha_rnd_init(1))
  {
    return 0;
  }
  return 0;
}


/*
  Initialize quick select for row retrieval.
  SYNOPSIS
    reset()
  RETURN
    0      OK
    other  Error code
*/

int QUICK_ROR_INTERSECT_SELECT::reset()
{
  if (!scans_inited && init_ror_merged_scan(true))
    return 0;
  scans_inited= true;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  QUICK_RANGE_SELECT *quick;
  while ((quick= it++))
    quick->reset();
  return 0;
}


/*
  Add a merged quick select to this ROR-intersection quick select.

  SYNOPSIS
    QUICK_ROR_INTERSECT_SELECT::push_quick_back()
      quick Quick select to be added. The quick select must return
            rows in rowid order.
  NOTES
    This call can only be made before init() is called.

  RETURN
    false OK
    true  Out of memory.
*/

bool
QUICK_ROR_INTERSECT_SELECT::push_quick_back(QUICK_RANGE_SELECT *quick)
{
  return quick_selects.push_back(quick);
}

QUICK_ROR_INTERSECT_SELECT::~QUICK_ROR_INTERSECT_SELECT()
{
  quick_selects.delete_elements();
  delete cpk_quick;
  free_root(&alloc,MYF(0));
  if (need_to_fetch_row && head->file->inited != handler::NONE)
    head->file->ha_rnd_end();
  return;
}


QUICK_ROR_UNION_SELECT::QUICK_ROR_UNION_SELECT(Session *session_param,
                                               Table *table)
  : session(session_param), scans_inited(false)
{
  index= MAX_KEY;
  head= table;
  rowid_length= table->file->ref_length;
  record= head->record[0];
  init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
  session_param->mem_root= &alloc;
}

/*
 * Function object that is used as the comparison function
 * for the priority queue in the QUICK_ROR_UNION_SELECT
 * class.
 */
class compare_functor
{
  QUICK_ROR_UNION_SELECT *self;
  public:
  compare_functor(QUICK_ROR_UNION_SELECT *in_arg)
    : self(in_arg) { }
  inline bool operator()(const QUICK_SELECT_I *i, const QUICK_SELECT_I *j) const
  {
    int val= self->head->file->cmp_ref(i->last_rowid,
                                       j->last_rowid);
    return (val >= 0);
  }
};

/*
  Do post-constructor initialization.
  SYNOPSIS
    QUICK_ROR_UNION_SELECT::init()

  RETURN
    0      OK
    other  Error code
*/

int QUICK_ROR_UNION_SELECT::init()
{
  queue= 
    new priority_queue<QUICK_SELECT_I *, vector<QUICK_SELECT_I *>, compare_functor >(compare_functor(this));
  if (!(cur_rowid= (unsigned char*) alloc_root(&alloc, 2*head->file->ref_length)))
    return 0;
  prev_rowid= cur_rowid + head->file->ref_length;
  return 0;
}


/*
  Initialize quick select for row retrieval.
  SYNOPSIS
    reset()

  RETURN
    0      OK
    other  Error code
*/

int QUICK_ROR_UNION_SELECT::reset()
{
  QUICK_SELECT_I *quick;
  int error;
  have_prev_rowid= false;
  if (!scans_inited)
  {
    List_iterator_fast<QUICK_SELECT_I> it(quick_selects);
    while ((quick= it++))
    {
      if (quick->init_ror_merged_scan(false))
        return 0;
    }
    scans_inited= true;
  }
  while (!queue->empty())
    queue->pop();
  /*
    Initialize scans for merged quick selects and put all merged quick
    selects into the queue.
  */
  List_iterator_fast<QUICK_SELECT_I> it(quick_selects);
  while ((quick= it++))
  {
    if (quick->reset())
      return 0;
    if ((error= quick->get_next()))
    {
      if (error == HA_ERR_END_OF_FILE)
        continue;
      return(error);
    }
    quick->save_last_pos();
    queue->push(quick);
  }

  if (head->file->ha_rnd_init(1))
  {
    return 0;
  }

  return 0;
}


bool
QUICK_ROR_UNION_SELECT::push_quick_back(QUICK_SELECT_I *quick_sel_range)
{
  return quick_selects.push_back(quick_sel_range);
}

QUICK_ROR_UNION_SELECT::~QUICK_ROR_UNION_SELECT()
{
  while (!queue->empty())
    queue->pop();
  delete queue;
  quick_selects.delete_elements();
  if (head->file->inited != handler::NONE)
    head->file->ha_rnd_end();
  free_root(&alloc,MYF(0));
  return;
}


QUICK_RANGE::QUICK_RANGE()
  :min_key(0),max_key(0),min_length(0),max_length(0),
   flag(NO_MIN_RANGE | NO_MAX_RANGE),
  min_keypart_map(0), max_keypart_map(0)
{}

SEL_ARG::SEL_ARG(SEL_ARG &arg) :Sql_alloc()
{
  type=arg.type;
  min_flag=arg.min_flag;
  max_flag=arg.max_flag;
  maybe_flag=arg.maybe_flag;
  maybe_null=arg.maybe_null;
  part=arg.part;
  field=arg.field;
  min_value=arg.min_value;
  max_value=arg.max_value;
  next_key_part=arg.next_key_part;
  use_count=1; elements=1;
}


inline void SEL_ARG::make_root()
{
  left=right= &null_element;
  color=BLACK;
  next=prev=0;
  use_count=0; elements=1;
}

SEL_ARG::SEL_ARG(Field *f,const unsigned char *min_value_arg,
                 const unsigned char *max_value_arg)
  :min_flag(0), max_flag(0), maybe_flag(0), maybe_null(f->real_maybe_null()),
   elements(1), use_count(1), field(f), min_value((unsigned char*) min_value_arg),
   max_value((unsigned char*) max_value_arg), next(0),prev(0),
   next_key_part(0),color(BLACK),type(KEY_RANGE)
{
  left=right= &null_element;
}

SEL_ARG::SEL_ARG(Field *field_,uint8_t part_,
                 unsigned char *min_value_, unsigned char *max_value_,
		 uint8_t min_flag_,uint8_t max_flag_,uint8_t maybe_flag_)
  :min_flag(min_flag_),max_flag(max_flag_),maybe_flag(maybe_flag_),
   part(part_),maybe_null(field_->real_maybe_null()), elements(1),use_count(1),
   field(field_), min_value(min_value_), max_value(max_value_),
   next(0),prev(0),next_key_part(0),color(BLACK),type(KEY_RANGE)
{
  left=right= &null_element;
}

SEL_ARG *SEL_ARG::clone(RANGE_OPT_PARAM *param, SEL_ARG *new_parent,
                        SEL_ARG **next_arg)
{
  SEL_ARG *tmp;

  /* Bail out if we have already generated too many SEL_ARGs */
  if (++param->alloced_sel_args > MAX_SEL_ARGS)
    return 0;

  if (type != KEY_RANGE)
  {
    if (!(tmp= new (param->mem_root) SEL_ARG(type)))
      return 0;					// out of memory
    tmp->prev= *next_arg;			// Link into next/prev chain
    (*next_arg)->next=tmp;
    (*next_arg)= tmp;
  }
  else
  {
    if (!(tmp= new (param->mem_root) SEL_ARG(field,part, min_value,max_value,
                                             min_flag, max_flag, maybe_flag)))
      return 0;					// OOM
    tmp->parent=new_parent;
    tmp->next_key_part=next_key_part;
    if (left != &null_element)
      if (!(tmp->left=left->clone(param, tmp, next_arg)))
	return 0;				// OOM

    tmp->prev= *next_arg;			// Link into next/prev chain
    (*next_arg)->next=tmp;
    (*next_arg)= tmp;

    if (right != &null_element)
      if (!(tmp->right= right->clone(param, tmp, next_arg)))
	return 0;				// OOM
  }
  increment_use_count(1);
  tmp->color= color;
  tmp->elements= this->elements;
  return tmp;
}

SEL_ARG *SEL_ARG::first()
{
  SEL_ARG *next_arg=this;
  if (!next_arg->left)
    return 0;					// MAYBE_KEY
  while (next_arg->left != &null_element)
    next_arg=next_arg->left;
  return next_arg;
}

SEL_ARG *SEL_ARG::last()
{
  SEL_ARG *next_arg=this;
  if (!next_arg->right)
    return 0;					// MAYBE_KEY
  while (next_arg->right != &null_element)
    next_arg=next_arg->right;
  return next_arg;
}


/*
  Check if a compare is ok, when one takes ranges in account
  Returns -2 or 2 if the ranges where 'joined' like  < 2 and >= 2
*/

static int sel_cmp(Field *field, unsigned char *a, unsigned char *b, uint8_t a_flag,
                   uint8_t b_flag)
{
  int cmp;
  /* First check if there was a compare to a min or max element */
  if (a_flag & (NO_MIN_RANGE | NO_MAX_RANGE))
  {
    if ((a_flag & (NO_MIN_RANGE | NO_MAX_RANGE)) ==
	(b_flag & (NO_MIN_RANGE | NO_MAX_RANGE)))
      return 0;
    return (a_flag & NO_MIN_RANGE) ? -1 : 1;
  }
  if (b_flag & (NO_MIN_RANGE | NO_MAX_RANGE))
    return (b_flag & NO_MIN_RANGE) ? 1 : -1;

  if (field->real_maybe_null())			// If null is part of key
  {
    if (*a != *b)
    {
      return *a ? -1 : 1;
    }
    if (*a)
      goto end;					// NULL where equal
    a++; b++;					// Skip NULL marker
  }
  cmp=field->key_cmp(a , b);
  if (cmp) return cmp < 0 ? -1 : 1;		// The values differed

  // Check if the compared equal arguments was defined with open/closed range
 end:
  if (a_flag & (NEAR_MIN | NEAR_MAX))
  {
    if ((a_flag & (NEAR_MIN | NEAR_MAX)) == (b_flag & (NEAR_MIN | NEAR_MAX)))
      return 0;
    if (!(b_flag & (NEAR_MIN | NEAR_MAX)))
      return (a_flag & NEAR_MIN) ? 2 : -2;
    return (a_flag & NEAR_MIN) ? 1 : -1;
  }
  if (b_flag & (NEAR_MIN | NEAR_MAX))
    return (b_flag & NEAR_MIN) ? -2 : 2;
  return 0;					// The elements where equal
}


SEL_ARG *SEL_ARG::clone_tree(RANGE_OPT_PARAM *param)
{
  SEL_ARG tmp_link,*next_arg,*root;
  next_arg= &tmp_link;
  if (!(root= clone(param, (SEL_ARG *) 0, &next_arg)))
    return 0;
  next_arg->next=0;				// Fix last link
  tmp_link.next->prev=0;			// Fix first link
  if (root)					// If not OOM
    root->use_count= 0;
  return root;
}


/*
  Find the best index to retrieve first N records in given order

  SYNOPSIS
    get_index_for_order()
      table  Table to be accessed
      order  Required ordering
      limit  Number of records that will be retrieved

  DESCRIPTION
    Find the best index that allows to retrieve first #limit records in the
    given order cheaper then one would retrieve them using full table scan.

  IMPLEMENTATION
    Run through all table indexes and find the shortest index that allows
    records to be retrieved in given order. We look for the shortest index
    as we will have fewer index pages to read with it.

    This function is used only by UPDATE/DELETE, so we take into account how
    the UPDATE/DELETE code will work:
     * index can only be scanned in forward direction
     * HA_EXTRA_KEYREAD will not be used
    Perhaps these assumptions could be relaxed.

  RETURN
    Number of the index that produces the required ordering in the cheapest way
    MAX_KEY if no such index was found.
*/

uint32_t get_index_for_order(Table *table, order_st *order, ha_rows limit)
{
  uint32_t idx;
  uint32_t match_key= MAX_KEY, match_key_len= MAX_KEY_LENGTH + 1;
  order_st *ord;

  for (ord= order; ord; ord= ord->next)
    if (!ord->asc)
      return MAX_KEY;

  for (idx= 0; idx < table->s->keys; idx++)
  {
    if (!(table->keys_in_use_for_query.test(idx)))
      continue;
    KEY_PART_INFO *keyinfo= table->key_info[idx].key_part;
    uint32_t n_parts=  table->key_info[idx].key_parts;
    uint32_t partno= 0;

    /*
      The below check is sufficient considering we now have either BTREE
      indexes (records are returned in order for any index prefix) or HASH
      indexes (records are not returned in order for any index prefix).
    */
    if (!(table->file->index_flags(idx, 0, 1) & HA_READ_ORDER))
      continue;
    for (ord= order; ord && partno < n_parts; ord= ord->next, partno++)
    {
      Item *item= order->item[0];
      if (!(item->type() == Item::FIELD_ITEM &&
           ((Item_field*)item)->field->eq(keyinfo[partno].field)))
        break;
    }

    if (!ord && table->key_info[idx].key_length < match_key_len)
    {
      /*
        Ok, the ordering is compatible and this key is shorter then
        previous match (we want shorter keys as we'll have to read fewer
        index pages for the same number of records)
      */
      match_key= idx;
      match_key_len= table->key_info[idx].key_length;
    }
  }

  if (match_key != MAX_KEY)
  {
    /*
      Found an index that allows records to be retrieved in the requested
      order. Now we'll check if using the index is cheaper then doing a table
      scan.
    */
    double full_scan_time= table->file->scan_time();
    double index_scan_time= table->file->read_time(match_key, 1, limit);
    if (index_scan_time > full_scan_time)
      match_key= MAX_KEY;
  }
  return match_key;
}


/*
  Table rows retrieval plan. Range optimizer creates QUICK_SELECT_I-derived
  objects from table read plans.
*/
class TABLE_READ_PLAN
{
public:
  /*
    Plan read cost, with or without cost of full row retrieval, depending
    on plan creation parameters.
  */
  double read_cost;
  ha_rows records; /* estimate of #rows to be examined */

  /*
    If true, the scan returns rows in rowid order. This is used only for
    scans that can be both ROR and non-ROR.
  */
  bool is_ror;

  /*
    Create quick select for this plan.
    SYNOPSIS
     make_quick()
       param               Parameter from test_quick_select
       retrieve_full_rows  If true, created quick select will do full record
                           retrieval.
       parent_alloc        Memory pool to use, if any.

    NOTES
      retrieve_full_rows is ignored by some implementations.

    RETURN
      created quick select
      NULL on any error.
  */
  virtual QUICK_SELECT_I *make_quick(PARAM *param,
                                     bool retrieve_full_rows,
                                     MEM_ROOT *parent_alloc=NULL) = 0;

  /* Table read plans are allocated on MEM_ROOT and are never deleted */
  static void *operator new(size_t size, MEM_ROOT *mem_root)
  { return (void*) alloc_root(mem_root, (uint32_t) size); }
  static void operator delete(void *, size_t)
    { TRASH(ptr, size); }
  static void operator delete(void *, MEM_ROOT *)
    { /* Never called */ }
  virtual ~TABLE_READ_PLAN() {}               /* Remove gcc warning */

};

class TRP_ROR_INTERSECT;
class TRP_ROR_UNION;
class TRP_INDEX_MERGE;


/*
  Plan for a QUICK_RANGE_SELECT scan.
  TRP_RANGE::make_quick ignores retrieve_full_rows parameter because
  QUICK_RANGE_SELECT doesn't distinguish between 'index only' scans and full
  record retrieval scans.
*/

class TRP_RANGE : public TABLE_READ_PLAN
{
public:
  SEL_ARG *key; /* set of intervals to be used in "range" method retrieval */
  uint32_t     key_idx; /* key number in PARAM::key */
  uint32_t     mrr_flags;
  uint32_t     mrr_buf_size;

  TRP_RANGE(SEL_ARG *key_arg, uint32_t idx_arg, uint32_t mrr_flags_arg)
   : key(key_arg), key_idx(idx_arg), mrr_flags(mrr_flags_arg)
  {}
  virtual ~TRP_RANGE() {}                     /* Remove gcc warning */

  QUICK_SELECT_I *make_quick(PARAM *param, bool, MEM_ROOT *parent_alloc)
  {
    QUICK_RANGE_SELECT *quick;
    if ((quick= get_quick_select(param, key_idx, key, mrr_flags, mrr_buf_size,
                                 parent_alloc)))
    {
      quick->records= records;
      quick->read_time= read_cost;
    }
    return quick;
  }
};


/* Plan for QUICK_ROR_INTERSECT_SELECT scan. */

class TRP_ROR_INTERSECT : public TABLE_READ_PLAN
{
public:
  TRP_ROR_INTERSECT() {}                      /* Remove gcc warning */
  virtual ~TRP_ROR_INTERSECT() {}             /* Remove gcc warning */
  QUICK_SELECT_I *make_quick(PARAM *param, bool retrieve_full_rows,
                             MEM_ROOT *parent_alloc);

  /* Array of pointers to ROR range scans used in this intersection */
  struct st_ror_scan_info **first_scan;
  struct st_ror_scan_info **last_scan; /* End of the above array */
  struct st_ror_scan_info *cpk_scan;  /* Clustered PK scan, if there is one */
  bool is_covering; /* true if no row retrieval phase is necessary */
  double index_scan_costs; /* SUM(cost(index_scan)) */
};


/*
  Plan for QUICK_ROR_UNION_SELECT scan.
  QUICK_ROR_UNION_SELECT always retrieves full rows, so retrieve_full_rows
  is ignored by make_quick.
*/

class TRP_ROR_UNION : public TABLE_READ_PLAN
{
public:
  TRP_ROR_UNION() {}                          /* Remove gcc warning */
  virtual ~TRP_ROR_UNION() {}                 /* Remove gcc warning */
  QUICK_SELECT_I *make_quick(PARAM *param, bool retrieve_full_rows,
                             MEM_ROOT *parent_alloc);
  TABLE_READ_PLAN **first_ror; /* array of ptrs to plans for merged scans */
  TABLE_READ_PLAN **last_ror;  /* end of the above array */
};


/*
  Plan for QUICK_INDEX_MERGE_SELECT scan.
  QUICK_ROR_INTERSECT_SELECT always retrieves full rows, so retrieve_full_rows
  is ignored by make_quick.
*/

class TRP_INDEX_MERGE : public TABLE_READ_PLAN
{
public:
  TRP_INDEX_MERGE() {}                        /* Remove gcc warning */
  virtual ~TRP_INDEX_MERGE() {}               /* Remove gcc warning */
  QUICK_SELECT_I *make_quick(PARAM *param, bool retrieve_full_rows,
                             MEM_ROOT *parent_alloc);
  TRP_RANGE **range_scans; /* array of ptrs to plans of merged scans */
  TRP_RANGE **range_scans_end; /* end of the array */
};


/*
  Plan for a QUICK_GROUP_MIN_MAX_SELECT scan.
*/

class TRP_GROUP_MIN_MAX : public TABLE_READ_PLAN
{
private:
  bool have_min, have_max;
  KEY_PART_INFO *min_max_arg_part;
  uint32_t group_prefix_len;
  uint32_t used_key_parts;
  uint32_t group_key_parts;
  KEY *index_info;
  uint32_t index;
  uint32_t key_infix_len;
  unsigned char key_infix[MAX_KEY_LENGTH];
  SEL_TREE *range_tree; /* Represents all range predicates in the query. */
  SEL_ARG  *index_tree; /* The SEL_ARG sub-tree corresponding to index_info. */
  uint32_t param_idx; /* Index of used key in param->key. */
  /* Number of records selected by the ranges in index_tree. */
public:
  ha_rows quick_prefix_records;
public:
  TRP_GROUP_MIN_MAX(bool have_min_arg, bool have_max_arg,
                    KEY_PART_INFO *min_max_arg_part_arg,
                    uint32_t group_prefix_len_arg, uint32_t used_key_parts_arg,
                    uint32_t group_key_parts_arg, KEY *index_info_arg,
                    uint32_t index_arg, uint32_t key_infix_len_arg,
                    unsigned char *key_infix_arg,
                    SEL_TREE *tree_arg, SEL_ARG *index_tree_arg,
                    uint32_t param_idx_arg, ha_rows quick_prefix_records_arg)
  : have_min(have_min_arg), have_max(have_max_arg),
    min_max_arg_part(min_max_arg_part_arg),
    group_prefix_len(group_prefix_len_arg), used_key_parts(used_key_parts_arg),
    group_key_parts(group_key_parts_arg), index_info(index_info_arg),
    index(index_arg), key_infix_len(key_infix_len_arg), range_tree(tree_arg),
    index_tree(index_tree_arg), param_idx(param_idx_arg),
    quick_prefix_records(quick_prefix_records_arg)
    {
      if (key_infix_len)
        memcpy(this->key_infix, key_infix_arg, key_infix_len);
    }
  virtual ~TRP_GROUP_MIN_MAX() {}             /* Remove gcc warning */

  QUICK_SELECT_I *make_quick(PARAM *param, bool retrieve_full_rows,
                             MEM_ROOT *parent_alloc);
};


/*
  Fill param->needed_fields with bitmap of fields used in the query.
  SYNOPSIS
    fill_used_fields_bitmap()
      param Parameter from test_quick_select function.

  NOTES
    Clustered PK members are not put into the bitmap as they are implicitly
    present in all keys (and it is impossible to avoid reading them).
  RETURN
    0  Ok
    1  Out of memory.
*/

static int fill_used_fields_bitmap(PARAM *param)
{
  Table *table= param->table;
  my_bitmap_map *tmp;
  uint32_t pk;
  param->tmp_covered_fields.setBitmap(0);
  param->fields_bitmap_size= table->s->column_bitmap_size;
  if (!(tmp= (my_bitmap_map*) alloc_root(param->mem_root,
                                  param->fields_bitmap_size)) ||
      param->needed_fields.init(tmp, table->s->fields))
    return 1;

  param->needed_fields= *table->read_set;
  bitmap_union(&param->needed_fields, table->write_set);

  pk= param->table->s->primary_key;
  if (pk != MAX_KEY && param->table->file->primary_key_is_clustered())
  {
    /* The table uses clustered PK and it is not internally generated */
    KEY_PART_INFO *key_part= param->table->key_info[pk].key_part;
    KEY_PART_INFO *key_part_end= key_part +
                                 param->table->key_info[pk].key_parts;
    for (;key_part != key_part_end; ++key_part)
      param->needed_fields.clearBit(key_part->fieldnr-1);
  }
  return 0;
}


/*
  Test if a key can be used in different ranges

  SYNOPSIS
    SQL_SELECT::test_quick_select()
      session               Current thread
      keys_to_use       Keys to use for range retrieval
      prev_tables       Tables assumed to be already read when the scan is
                        performed (but not read at the moment of this call)
      limit             Query limit
      force_quick_range Prefer to use range (instead of full table scan) even
                        if it is more expensive.

  NOTES
    Updates the following in the select parameter:
      needed_reg - Bits for keys with may be used if all prev regs are read
      quick      - Parameter to use when reading records.

    In the table struct the following information is updated:
      quick_keys           - Which keys can be used
      quick_rows           - How many rows the key matches
      quick_condition_rows - E(# rows that will satisfy the table condition)

  IMPLEMENTATION
    quick_condition_rows value is obtained as follows:

      It is a minimum of E(#output rows) for all considered table access
      methods (range and index_merge accesses over various indexes).

    The obtained value is not a true E(#rows that satisfy table condition)
    but rather a pessimistic estimate. To obtain a true E(#...) one would
    need to combine estimates of various access methods, taking into account
    correlations between sets of rows they will return.

    For example, if values of tbl.key1 and tbl.key2 are independent (a right
    assumption if we have no information about their correlation) then the
    correct estimate will be:

      E(#rows("tbl.key1 < c1 AND tbl.key2 < c2")) =
      = E(#rows(tbl.key1 < c1)) / total_rows(tbl) * E(#rows(tbl.key2 < c2)

    which is smaller than

       MIN(E(#rows(tbl.key1 < c1), E(#rows(tbl.key2 < c2)))

    which is currently produced.

  TODO
   * Change the value returned in quick_condition_rows from a pessimistic
     estimate to true E(#rows that satisfy table condition).
     (we can re-use some of E(#rows) calcuation code from index_merge/intersection
      for this)

   * Check if this function really needs to modify keys_to_use, and change the
     code to pass it by reference if it doesn't.

   * In addition to force_quick_range other means can be (an usually are) used
     to make this function prefer range over full table scan. Figure out if
     force_quick_range is really needed.

  RETURN
   -1 if impossible select (i.e. certainly no rows will be selected)
    0 if can't use quick_select
    1 if found usable ranges and quick select has been successfully created.
*/

int SQL_SELECT::test_quick_select(Session *session, key_map keys_to_use,
				  table_map prev_tables,
				  ha_rows limit, bool force_quick_range,
                                  bool ordered_output)
{
  uint32_t idx;
  double scan_time;
  delete quick;
  quick=0;
  needed_reg.reset();
  quick_keys.reset();
  if (keys_to_use.none())
    return 0;
  records= head->file->stats.records;
  if (!records)
    records++;
  scan_time= (double) records / TIME_FOR_COMPARE + 1;
  read_time= (double) head->file->scan_time() + scan_time + 1.1;
  if (head->force_index)
    scan_time= read_time= DBL_MAX;
  if (limit < records)
    read_time= (double) records + scan_time + 1; // Force to use index
  else if (read_time <= 2.0 && !force_quick_range)
    return 0;				/* No need for quick select */

  keys_to_use&= head->keys_in_use_for_query;
  if (keys_to_use.any())
  {
    MEM_ROOT alloc;
    SEL_TREE *tree= NULL;
    KEY_PART *key_parts;
    KEY *key_info;
    PARAM param;

    if (check_stack_overrun(session, 2*STACK_MIN_SIZE, NULL))
      return 0;                           // Fatal error flag is set

    /* set up parameter that is passed to all functions */
    param.session= session;
    param.baseflag= head->file->ha_table_flags();
    param.prev_tables= prev_tables | const_tables;
    param.read_tables= read_tables;
    param.current_table= head->map;
    param.table=head;
    param.keys=0;
    param.mem_root= &alloc;
    param.old_root= session->mem_root;
    param.needed_reg= &needed_reg;
    param.imerge_cost_buff_size= 0;
    param.using_real_indexes= true;
    param.remove_jump_scans= true;
    param.force_default_mrr= ordered_output;

    session->no_errors=1;				// Don't warn about NULL
    init_sql_alloc(&alloc, session->variables.range_alloc_block_size, 0);
    if (!(param.key_parts= (KEY_PART*) alloc_root(&alloc,
                                                  sizeof(KEY_PART)*
                                                  head->s->key_parts)) ||
        fill_used_fields_bitmap(&param))
    {
      session->no_errors=0;
      free_root(&alloc,MYF(0));			// Return memory & allocator
      return 0;				// Can't use range
    }
    key_parts= param.key_parts;
    session->mem_root= &alloc;

    /*
      Make an array with description of all key parts of all table keys.
      This is used in get_mm_parts function.
    */
    key_info= head->key_info;
    for (idx=0 ; idx < head->s->keys ; idx++, key_info++)
    {
      KEY_PART_INFO *key_part_info;
      if (! keys_to_use.test(idx))
	continue;

      param.key[param.keys]=key_parts;
      key_part_info= key_info->key_part;
      for (uint32_t part=0;
           part < key_info->key_parts;
           part++, key_parts++, key_part_info++)
      {
        key_parts->key= param.keys;
        key_parts->part= part;
        key_parts->length= key_part_info->length;
        key_parts->store_length= key_part_info->store_length;
        key_parts->field= key_part_info->field;
        key_parts->null_bit= key_part_info->null_bit;
        /* Only HA_PART_KEY_SEG is used */
        key_parts->flag= (uint8_t) key_part_info->key_part_flag;
      }
      param.real_keynr[param.keys++]=idx;
    }
    param.key_parts_end=key_parts;
    param.alloced_sel_args= 0;

    /* Calculate cost of full index read for the shortest covering index */
    if (!head->covering_keys.none())
    {
      int key_for_use= head->find_shortest_key(&head->covering_keys);
      double key_read_time=
        param.table->file->index_only_read_time(key_for_use,
                                                rows2double(records)) +
        (double) records / TIME_FOR_COMPARE;
      if (key_read_time < read_time)
        read_time= key_read_time;
    }

    TABLE_READ_PLAN *best_trp= NULL;
    TRP_GROUP_MIN_MAX *group_trp;
    double best_read_time= read_time;

    if (cond)
    {
      if ((tree= get_mm_tree(&param,cond)))
      {
        if (tree->type == SEL_TREE::IMPOSSIBLE)
        {
          records=0L;                      /* Return -1 from this function. */
          read_time= (double) HA_POS_ERROR;
          goto free_mem;
        }
        /*
          If the tree can't be used for range scans, proceed anyway, as we
          can construct a group-min-max quick select
        */
        if (tree->type != SEL_TREE::KEY && tree->type != SEL_TREE::KEY_SMALLER)
          tree= NULL;
      }
    }

    /*
      Try to construct a QUICK_GROUP_MIN_MAX_SELECT.
      Notice that it can be constructed no matter if there is a range tree.
    */
    group_trp= get_best_group_min_max(&param, tree);
    if (group_trp)
    {
      param.table->quick_condition_rows= min(group_trp->records,
                                             head->file->stats.records);
      if (group_trp->read_cost < best_read_time)
      {
        best_trp= group_trp;
        best_read_time= best_trp->read_cost;
      }
    }

    if (tree)
    {
      /*
        It is possible to use a range-based quick select (but it might be
        slower than 'all' table scan).
      */
      if (tree->merges.empty() == true)
      {
        TRP_RANGE         *range_trp;
        TRP_ROR_INTERSECT *rori_trp;
        bool can_build_covering= false;

        /* Get best 'range' plan and prepare data for making other plans */
        if ((range_trp= get_key_scans_params(&param, tree, false, true,
                                             best_read_time)))
        {
          best_trp= range_trp;
          best_read_time= best_trp->read_cost;
        }

        /*
          Simultaneous key scans and row deletes on several handler
          objects are not allowed so don't use ROR-intersection for
          table deletes.
        */
        if ((session->lex->sql_command != SQLCOM_DELETE))
        {
          /*
            Get best non-covering ROR-intersection plan and prepare data for
            building covering ROR-intersection.
          */
          if ((rori_trp= get_best_ror_intersect(&param, tree, best_read_time,
                                                &can_build_covering)))
          {
            best_trp= rori_trp;
            best_read_time= best_trp->read_cost;
            /*
              Try constructing covering ROR-intersect only if it looks possible
              and worth doing.
            */
            if (!rori_trp->is_covering && can_build_covering &&
                (rori_trp= get_best_covering_ror_intersect(&param, tree,
                                                           best_read_time)))
              best_trp= rori_trp;
          }
        }
      }
      else
      {
        /* Try creating index_merge/ROR-union scan. */
        TABLE_READ_PLAN *best_conj_trp= NULL, *new_conj_trp;
        vector<SEL_IMERGE*>::iterator imerge= tree->merges.begin();
        while (imerge != tree->merges.end())
        {
          new_conj_trp= get_best_disjunct_quick(&param, *imerge, best_read_time);
          if (new_conj_trp)
            set_if_smaller(param.table->quick_condition_rows,
                           new_conj_trp->records);

          if (!best_conj_trp || (new_conj_trp && new_conj_trp->read_cost <
                                 best_conj_trp->read_cost))
            best_conj_trp= new_conj_trp;

          ++imerge;
        }
        if (best_conj_trp)
          best_trp= best_conj_trp;
      }
    }

    session->mem_root= param.old_root;

    /* If we got a read plan, create a quick select from it. */
    if (best_trp)
    {
      records= best_trp->records;
      if (!(quick= best_trp->make_quick(&param, true)) || quick->init())
      {
        delete quick;
        quick= NULL;
      }
    }

  free_mem:
    free_root(&alloc,MYF(0));			// Return memory & allocator
    session->mem_root= param.old_root;
    session->no_errors=0;
  }

  /*
    Assume that if the user is using 'limit' we will only need to scan
    limit rows if we are using a key
  */
  return(records ? test(quick) : -1);
}

/*
  Get best plan for a SEL_IMERGE disjunctive expression.
  SYNOPSIS
    get_best_disjunct_quick()
      param     Parameter from check_quick_select function
      imerge    Expression to use
      read_time Don't create scans with cost > read_time

  NOTES
    index_merge cost is calculated as follows:
    index_merge_cost =
      cost(index_reads) +         (see #1)
      cost(rowid_to_row_scan) +   (see #2)
      cost(unique_use)            (see #3)

    1. cost(index_reads) =SUM_i(cost(index_read_i))
       For non-CPK scans,
         cost(index_read_i) = {cost of ordinary 'index only' scan}
       For CPK scan,
         cost(index_read_i) = {cost of non-'index only' scan}

    2. cost(rowid_to_row_scan)
      If table PK is clustered then
        cost(rowid_to_row_scan) =
          {cost of ordinary clustered PK scan with n_ranges=n_rows}

      Otherwise, we use the following model to calculate costs:
      We need to retrieve n_rows rows from file that occupies n_blocks blocks.
      We assume that offsets of rows we need are independent variates with
      uniform distribution in [0..max_file_offset] range.

      We'll denote block as "busy" if it contains row(s) we need to retrieve
      and "empty" if doesn't contain rows we need.

      Probability that a block is empty is (1 - 1/n_blocks)^n_rows (this
      applies to any block in file). Let x_i be a variate taking value 1 if
      block #i is empty and 0 otherwise.

      Then E(x_i) = (1 - 1/n_blocks)^n_rows;

      E(n_empty_blocks) = E(sum(x_i)) = sum(E(x_i)) =
        = n_blocks * ((1 - 1/n_blocks)^n_rows) =
       ~= n_blocks * exp(-n_rows/n_blocks).

      E(n_busy_blocks) = n_blocks*(1 - (1 - 1/n_blocks)^n_rows) =
       ~= n_blocks * (1 - exp(-n_rows/n_blocks)).

      Average size of "hole" between neighbor non-empty blocks is
           E(hole_size) = n_blocks/E(n_busy_blocks).

      The total cost of reading all needed blocks in one "sweep" is:

      E(n_busy_blocks)*
       (DISK_SEEK_BASE_COST + DISK_SEEK_PROP_COST*n_blocks/E(n_busy_blocks)).

    3. Cost of Unique use is calculated in Unique::get_use_cost function.

  ROR-union cost is calculated in the same way index_merge, but instead of
  Unique a priority queue is used.

  RETURN
    Created read plan
    NULL - Out of memory or no read scan could be built.
*/

static
TABLE_READ_PLAN *get_best_disjunct_quick(PARAM *param, SEL_IMERGE *imerge,
                                         double read_time)
{
  SEL_TREE **ptree;
  TRP_INDEX_MERGE *imerge_trp= NULL;
  uint32_t n_child_scans= imerge->trees_next - imerge->trees;
  TRP_RANGE **range_scans;
  TRP_RANGE **cur_child;
  TRP_RANGE **cpk_scan= NULL;
  bool imerge_too_expensive= false;
  double imerge_cost= 0.0;
  ha_rows cpk_scan_records= 0;
  ha_rows non_cpk_scan_records= 0;
  bool pk_is_clustered= param->table->file->primary_key_is_clustered();
  bool all_scans_ror_able= true;
  bool all_scans_rors= true;
  uint32_t unique_calc_buff_size;
  TABLE_READ_PLAN **roru_read_plans;
  TABLE_READ_PLAN **cur_roru_plan;
  double roru_index_costs;
  ha_rows roru_total_records;
  double roru_intersect_part= 1.0;

  if (!(range_scans= (TRP_RANGE**)alloc_root(param->mem_root,
                                             sizeof(TRP_RANGE*)*
                                             n_child_scans)))
    return NULL;
  /*
    Collect best 'range' scan for each of disjuncts, and, while doing so,
    analyze possibility of ROR scans. Also calculate some values needed by
    other parts of the code.
  */
  for (ptree= imerge->trees, cur_child= range_scans;
       ptree != imerge->trees_next;
       ptree++, cur_child++)
  {
    print_sel_tree(param, *ptree, &(*ptree)->keys_map, "tree in SEL_IMERGE");
    if (!(*cur_child= get_key_scans_params(param, *ptree, true, false, read_time)))
    {
      /*
        One of index scans in this index_merge is more expensive than entire
        table read for another available option. The entire index_merge (and
        any possible ROR-union) will be more expensive then, too. We continue
        here only to update SQL_SELECT members.
      */
      imerge_too_expensive= true;
    }
    if (imerge_too_expensive)
      continue;

    imerge_cost += (*cur_child)->read_cost;
    all_scans_ror_able &= ((*ptree)->n_ror_scans > 0);
    all_scans_rors &= (*cur_child)->is_ror;
    if (pk_is_clustered &&
        param->real_keynr[(*cur_child)->key_idx] ==
        param->table->s->primary_key)
    {
      cpk_scan= cur_child;
      cpk_scan_records= (*cur_child)->records;
    }
    else
      non_cpk_scan_records += (*cur_child)->records;
  }

  if (imerge_too_expensive || (imerge_cost > read_time) ||
      ((non_cpk_scan_records+cpk_scan_records >= param->table->file->stats.records) && read_time != DBL_MAX))
  {
    /*
      Bail out if it is obvious that both index_merge and ROR-union will be
      more expensive
    */
    return NULL;
  }
  if (all_scans_rors)
  {
    roru_read_plans= (TABLE_READ_PLAN**)range_scans;
    goto skip_to_ror_scan;
  }
  if (cpk_scan)
  {
    /*
      Add one ROWID comparison for each row retrieved on non-CPK scan.  (it
      is done in QUICK_RANGE_SELECT::row_in_ranges)
     */
    imerge_cost += non_cpk_scan_records / TIME_FOR_COMPARE_ROWID;
  }

  /* Calculate cost(rowid_to_row_scan) */
  {
    COST_VECT sweep_cost;
    JOIN *join= param->session->lex->select_lex.join;
    bool is_interrupted= test(join && join->tables == 1);
    get_sweep_read_cost(param->table, non_cpk_scan_records, is_interrupted,
                        &sweep_cost);
    imerge_cost += sweep_cost.total_cost();
  }
  if (imerge_cost > read_time)
    goto build_ror_index_merge;

  /* Add Unique operations cost */
  unique_calc_buff_size=
    Unique::get_cost_calc_buff_size((ulong)non_cpk_scan_records,
                                    param->table->file->ref_length,
                                    param->session->variables.sortbuff_size);
  if (param->imerge_cost_buff_size < unique_calc_buff_size)
  {
    if (!(param->imerge_cost_buff= (uint*)alloc_root(param->mem_root,
                                                     unique_calc_buff_size)))
      return NULL;
    param->imerge_cost_buff_size= unique_calc_buff_size;
  }

  imerge_cost +=
    Unique::get_use_cost(param->imerge_cost_buff, (uint32_t)non_cpk_scan_records,
                         param->table->file->ref_length,
                         param->session->variables.sortbuff_size);
  if (imerge_cost < read_time)
  {
    if ((imerge_trp= new (param->mem_root)TRP_INDEX_MERGE))
    {
      imerge_trp->read_cost= imerge_cost;
      imerge_trp->records= non_cpk_scan_records + cpk_scan_records;
      imerge_trp->records= min(imerge_trp->records,
                               param->table->file->stats.records);
      imerge_trp->range_scans= range_scans;
      imerge_trp->range_scans_end= range_scans + n_child_scans;
      read_time= imerge_cost;
    }
  }

build_ror_index_merge:
  if (!all_scans_ror_able || param->session->lex->sql_command == SQLCOM_DELETE)
    return(imerge_trp);

  /* Ok, it is possible to build a ROR-union, try it. */
  bool dummy;
  if (!(roru_read_plans=
          (TABLE_READ_PLAN**)alloc_root(param->mem_root,
                                        sizeof(TABLE_READ_PLAN*)*
                                        n_child_scans)))
    return(imerge_trp);
skip_to_ror_scan:
  roru_index_costs= 0.0;
  roru_total_records= 0;
  cur_roru_plan= roru_read_plans;

  /* Find 'best' ROR scan for each of trees in disjunction */
  for (ptree= imerge->trees, cur_child= range_scans;
       ptree != imerge->trees_next;
       ptree++, cur_child++, cur_roru_plan++)
  {
    /*
      Assume the best ROR scan is the one that has cheapest full-row-retrieval
      scan cost.
      Also accumulate index_only scan costs as we'll need them to calculate
      overall index_intersection cost.
    */
    double cost;
    if ((*cur_child)->is_ror)
    {
      /* Ok, we have index_only cost, now get full rows scan cost */
      cost= param->table->file->
              read_time(param->real_keynr[(*cur_child)->key_idx], 1,
                        (*cur_child)->records) +
              rows2double((*cur_child)->records) / TIME_FOR_COMPARE;
    }
    else
      cost= read_time;

    TABLE_READ_PLAN *prev_plan= *cur_child;
    if (!(*cur_roru_plan= get_best_ror_intersect(param, *ptree, cost,
                                                 &dummy)))
    {
      if (prev_plan->is_ror)
        *cur_roru_plan= prev_plan;
      else
        return(imerge_trp);
      roru_index_costs += (*cur_roru_plan)->read_cost;
    }
    else
      roru_index_costs +=
        ((TRP_ROR_INTERSECT*)(*cur_roru_plan))->index_scan_costs;
    roru_total_records += (*cur_roru_plan)->records;
    roru_intersect_part *= (*cur_roru_plan)->records /
                           param->table->file->stats.records;
  }

  /*
    rows to retrieve=
      SUM(rows_in_scan_i) - table_rows * PROD(rows_in_scan_i / table_rows).
    This is valid because index_merge construction guarantees that conditions
    in disjunction do not share key parts.
  */
  roru_total_records -= (ha_rows)(roru_intersect_part*
                                  param->table->file->stats.records);
  /* ok, got a ROR read plan for each of the disjuncts
    Calculate cost:
    cost(index_union_scan(scan_1, ... scan_n)) =
      SUM_i(cost_of_index_only_scan(scan_i)) +
      queue_use_cost(rowid_len, n) +
      cost_of_row_retrieval
    See get_merge_buffers_cost function for queue_use_cost formula derivation.
  */
  double roru_total_cost;
  {
    COST_VECT sweep_cost;
    JOIN *join= param->session->lex->select_lex.join;
    bool is_interrupted= test(join && join->tables == 1);
    get_sweep_read_cost(param->table, roru_total_records, is_interrupted,
                        &sweep_cost);
    roru_total_cost= roru_index_costs +
                     rows2double(roru_total_records)*log((double)n_child_scans) /
                     (TIME_FOR_COMPARE_ROWID * M_LN2) +
                     sweep_cost.total_cost();
  }

  TRP_ROR_UNION* roru;
  if (roru_total_cost < read_time)
  {
    if ((roru= new (param->mem_root) TRP_ROR_UNION))
    {
      roru->first_ror= roru_read_plans;
      roru->last_ror= roru_read_plans + n_child_scans;
      roru->read_cost= roru_total_cost;
      roru->records= roru_total_records;
      return(roru);
    }
  }
  return(imerge_trp);
}


typedef struct st_ror_scan_info
{
  uint32_t      idx;      /* # of used key in param->keys */
  uint32_t      keynr;    /* # of used key in table */
  ha_rows   records;  /* estimate of # records this scan will return */

  /* Set of intervals over key fields that will be used for row retrieval. */
  SEL_ARG   *sel_arg;

  /* Fields used in the query and covered by this ROR scan. */
  MyBitmap covered_fields;
  uint32_t      used_fields_covered; /* # of set bits in covered_fields */
  int       key_rec_length; /* length of key record (including rowid) */

  /*
    Cost of reading all index records with values in sel_arg intervals set
    (assuming there is no need to access full table records)
  */
  double    index_read_cost;
  uint32_t      first_uncovered_field; /* first unused bit in covered_fields */
  uint32_t      key_components; /* # of parts in the key */
} ROR_SCAN_INFO;


/*
  Create ROR_SCAN_INFO* structure with a single ROR scan on index idx using
  sel_arg set of intervals.

  SYNOPSIS
    make_ror_scan()
      param    Parameter from test_quick_select function
      idx      Index of key in param->keys
      sel_arg  Set of intervals for a given key

  RETURN
    NULL - out of memory
    ROR scan structure containing a scan for {idx, sel_arg}
*/

static
ROR_SCAN_INFO *make_ror_scan(const PARAM *param, int idx, SEL_ARG *sel_arg)
{
  ROR_SCAN_INFO *ror_scan;
  my_bitmap_map *bitmap_buf;

  uint32_t keynr;

  if (!(ror_scan= (ROR_SCAN_INFO*)alloc_root(param->mem_root,
                                             sizeof(ROR_SCAN_INFO))))
    return NULL;

  ror_scan->idx= idx;
  ror_scan->keynr= keynr= param->real_keynr[idx];
  ror_scan->key_rec_length= (param->table->key_info[keynr].key_length +
                             param->table->file->ref_length);
  ror_scan->sel_arg= sel_arg;
  ror_scan->records= param->table->quick_rows[keynr];

  if (!(bitmap_buf= (my_bitmap_map*) alloc_root(param->mem_root,
                                                param->fields_bitmap_size)))
    return NULL;

  if (ror_scan->covered_fields.init(bitmap_buf,
                                    param->table->s->fields))
    return NULL;
  ror_scan->covered_fields.clearAll();

  KEY_PART_INFO *key_part= param->table->key_info[keynr].key_part;
  KEY_PART_INFO *key_part_end= key_part +
                               param->table->key_info[keynr].key_parts;
  for (;key_part != key_part_end; ++key_part)
  {
    if (param->needed_fields.isBitSet(key_part->fieldnr-1))
      ror_scan->covered_fields.setBit(key_part->fieldnr-1);
  }
  double rows= rows2double(param->table->quick_rows[ror_scan->keynr]);
  ror_scan->index_read_cost=
    param->table->file->index_only_read_time(ror_scan->keynr, rows);
  return(ror_scan);
}


/*
  Compare two ROR_SCAN_INFO** by  E(#records_matched) * key_record_length.
  SYNOPSIS
    cmp_ror_scan_info()
      a ptr to first compared value
      b ptr to second compared value

  RETURN
   -1 a < b
    0 a = b
    1 a > b
*/

static int cmp_ror_scan_info(ROR_SCAN_INFO** a, ROR_SCAN_INFO** b)
{
  double val1= rows2double((*a)->records) * (*a)->key_rec_length;
  double val2= rows2double((*b)->records) * (*b)->key_rec_length;
  return (val1 < val2)? -1: (val1 == val2)? 0 : 1;
}

/*
  Compare two ROR_SCAN_INFO** by
   (#covered fields in F desc,
    #components asc,
    number of first not covered component asc)

  SYNOPSIS
    cmp_ror_scan_info_covering()
      a ptr to first compared value
      b ptr to second compared value

  RETURN
   -1 a < b
    0 a = b
    1 a > b
*/

static int cmp_ror_scan_info_covering(ROR_SCAN_INFO** a, ROR_SCAN_INFO** b)
{
  if ((*a)->used_fields_covered > (*b)->used_fields_covered)
    return -1;
  if ((*a)->used_fields_covered < (*b)->used_fields_covered)
    return 1;
  if ((*a)->key_components < (*b)->key_components)
    return -1;
  if ((*a)->key_components > (*b)->key_components)
    return 1;
  if ((*a)->first_uncovered_field < (*b)->first_uncovered_field)
    return -1;
  if ((*a)->first_uncovered_field > (*b)->first_uncovered_field)
    return 1;
  return 0;
}


/* Auxiliary structure for incremental ROR-intersection creation */
typedef struct
{
  const PARAM *param;
  MyBitmap covered_fields; /* union of fields covered by all scans */
  /*
    Fraction of table records that satisfies conditions of all scans.
    This is the number of full records that will be retrieved if a
    non-index_only index intersection will be employed.
  */
  double out_rows;
  /* true if covered_fields is a superset of needed_fields */
  bool is_covering;

  ha_rows index_records; /* sum(#records to look in indexes) */
  double index_scan_costs; /* SUM(cost of 'index-only' scans) */
  double total_cost;
} ROR_INTERSECT_INFO;


/*
  Allocate a ROR_INTERSECT_INFO and initialize it to contain zero scans.

  SYNOPSIS
    ror_intersect_init()
      param         Parameter from test_quick_select

  RETURN
    allocated structure
    NULL on error
*/

static
ROR_INTERSECT_INFO* ror_intersect_init(const PARAM *param)
{
  ROR_INTERSECT_INFO *info;
  my_bitmap_map* buf;
  if (!(info= (ROR_INTERSECT_INFO*)alloc_root(param->mem_root,
                                              sizeof(ROR_INTERSECT_INFO))))
    return NULL;
  info->param= param;
  if (!(buf= (my_bitmap_map*) alloc_root(param->mem_root,
                                         param->fields_bitmap_size)))
    return NULL;
  if (info->covered_fields.init(buf, param->table->s->fields))
    return NULL;
  info->is_covering= false;
  info->index_scan_costs= 0.0;
  info->index_records= 0;
  info->out_rows= (double) param->table->file->stats.records;
  info->covered_fields.clearAll();
  return info;
}

static void ror_intersect_cpy(ROR_INTERSECT_INFO *dst,
                              const ROR_INTERSECT_INFO *src)
{
  dst->param= src->param;
  dst->covered_fields= src->covered_fields;
  dst->out_rows= src->out_rows;
  dst->is_covering= src->is_covering;
  dst->index_records= src->index_records;
  dst->index_scan_costs= src->index_scan_costs;
  dst->total_cost= src->total_cost;
}


/*
  Get selectivity of a ROR scan wrt ROR-intersection.

  SYNOPSIS
    ror_scan_selectivity()
      info  ROR-interection
      scan  ROR scan

  NOTES
    Suppose we have a condition on several keys
    cond=k_11=c_11 AND k_12=c_12 AND ...  // parts of first key
         k_21=c_21 AND k_22=c_22 AND ...  // parts of second key
          ...
         k_n1=c_n1 AND k_n3=c_n3 AND ...  (1) //parts of the key used by *scan

    where k_ij may be the same as any k_pq (i.e. keys may have common parts).

    A full row is retrieved if entire condition holds.

    The recursive procedure for finding P(cond) is as follows:

    First step:
    Pick 1st part of 1st key and break conjunction (1) into two parts:
      cond= (k_11=c_11 AND R)

    Here R may still contain condition(s) equivalent to k_11=c_11.
    Nevertheless, the following holds:

      P(k_11=c_11 AND R) = P(k_11=c_11) * P(R | k_11=c_11).

    Mark k_11 as fixed field (and satisfied condition) F, save P(F),
    save R to be cond and proceed to recursion step.

    Recursion step:
    We have a set of fixed fields/satisfied conditions) F, probability P(F),
    and remaining conjunction R
    Pick next key part on current key and its condition "k_ij=c_ij".
    We will add "k_ij=c_ij" into F and update P(F).
    Lets denote k_ij as t,  R = t AND R1, where R1 may still contain t. Then

     P((t AND R1)|F) = P(t|F) * P(R1|t|F) = P(t|F) * P(R1|(t AND F)) (2)

    (where '|' mean conditional probability, not "or")

    Consider the first multiplier in (2). One of the following holds:
    a) F contains condition on field used in t (i.e. t AND F = F).
      Then P(t|F) = 1

    b) F doesn't contain condition on field used in t. Then F and t are
     considered independent.

     P(t|F) = P(t|(fields_before_t_in_key AND other_fields)) =
          = P(t|fields_before_t_in_key).

     P(t|fields_before_t_in_key) = #records(fields_before_t_in_key) /
                                   #records(fields_before_t_in_key, t)

    The second multiplier is calculated by applying this step recursively.

  IMPLEMENTATION
    This function calculates the result of application of the "recursion step"
    described above for all fixed key members of a single key, accumulating set
    of covered fields, selectivity, etc.

    The calculation is conducted as follows:
    Lets denote #records(keypart1, ... keypartK) as n_k. We need to calculate

     n_{k1}      n_{k2}
    --------- * ---------  * .... (3)
     n_{k1-1}    n_{k2-1}

    where k1,k2,... are key parts which fields were not yet marked as fixed
    ( this is result of application of option b) of the recursion step for
      parts of a single key).
    Since it is reasonable to expect that most of the fields are not marked
    as fixed, we calculate (3) as

                                  n_{i1}      n_{i2}
    (3) = n_{max_key_part}  / (   --------- * ---------  * ....  )
                                  n_{i1-1}    n_{i2-1}

    where i1,i2, .. are key parts that were already marked as fixed.

    In order to minimize number of expensive records_in_range calls we group
    and reduce adjacent fractions.

  RETURN
    Selectivity of given ROR scan.
*/

static double ror_scan_selectivity(const ROR_INTERSECT_INFO *info,
                                   const ROR_SCAN_INFO *scan)
{
  double selectivity_mult= 1.0;
  KEY_PART_INFO *key_part= info->param->table->key_info[scan->keynr].key_part;
  unsigned char key_val[MAX_KEY_LENGTH+MAX_FIELD_WIDTH]; /* key values tuple */
  unsigned char *key_ptr= key_val;
  SEL_ARG *sel_arg, *tuple_arg= NULL;
  key_part_map keypart_map= 0;
  bool cur_covered;
  bool prev_covered= test(info->covered_fields.isBitSet(key_part->fieldnr-1));
  key_range min_range;
  key_range max_range;
  min_range.key= key_val;
  min_range.flag= HA_READ_KEY_EXACT;
  max_range.key= key_val;
  max_range.flag= HA_READ_AFTER_KEY;
  ha_rows prev_records= info->param->table->file->stats.records;

  for (sel_arg= scan->sel_arg; sel_arg;
       sel_arg= sel_arg->next_key_part)
  {
    cur_covered= 
      test(info->covered_fields.isBitSet(key_part[sel_arg->part].fieldnr-1));
    if (cur_covered != prev_covered)
    {
      /* create (part1val, ..., part{n-1}val) tuple. */
      ha_rows records;
      if (!tuple_arg)
      {
        tuple_arg= scan->sel_arg;
        /* Here we use the length of the first key part */
        tuple_arg->store_min(key_part->store_length, &key_ptr, 0);
        keypart_map= 1;
      }
      while (tuple_arg->next_key_part != sel_arg)
      {
        tuple_arg= tuple_arg->next_key_part;
        tuple_arg->store_min(key_part[tuple_arg->part].store_length,
                             &key_ptr, 0);
        keypart_map= (keypart_map << 1) | 1;
      }
      min_range.length= max_range.length= (size_t) (key_ptr - key_val);
      min_range.keypart_map= max_range.keypart_map= keypart_map;
      records= (info->param->table->file->
                records_in_range(scan->keynr, &min_range, &max_range));
      if (cur_covered)
      {
        /* uncovered -> covered */
        double tmp= rows2double(records)/rows2double(prev_records);
        selectivity_mult *= tmp;
        prev_records= HA_POS_ERROR;
      }
      else
      {
        /* covered -> uncovered */
        prev_records= records;
      }
    }
    prev_covered= cur_covered;
  }
  if (!prev_covered)
  {
    double tmp= rows2double(info->param->table->quick_rows[scan->keynr]) /
                rows2double(prev_records);
    selectivity_mult *= tmp;
  }
  return(selectivity_mult);
}


/*
  Check if adding a ROR scan to a ROR-intersection reduces its cost of
  ROR-intersection and if yes, update parameters of ROR-intersection,
  including its cost.

  SYNOPSIS
    ror_intersect_add()
      param        Parameter from test_quick_select
      info         ROR-intersection structure to add the scan to.
      ror_scan     ROR scan info to add.
      is_cpk_scan  If true, add the scan as CPK scan (this can be inferred
                   from other parameters and is passed separately only to
                   avoid duplicating the inference code)

  NOTES
    Adding a ROR scan to ROR-intersect "makes sense" iff the cost of ROR-
    intersection decreases. The cost of ROR-intersection is calculated as
    follows:

    cost= SUM_i(key_scan_cost_i) + cost_of_full_rows_retrieval

    When we add a scan the first increases and the second decreases.

    cost_of_full_rows_retrieval=
      (union of indexes used covers all needed fields) ?
        cost_of_sweep_read(E(rows_to_retrieve), rows_in_table) :
        0

    E(rows_to_retrieve) = #rows_in_table * ror_scan_selectivity(null, scan1) *
                           ror_scan_selectivity({scan1}, scan2) * ... *
                           ror_scan_selectivity({scan1,...}, scanN).
  RETURN
    true   ROR scan added to ROR-intersection, cost updated.
    false  It doesn't make sense to add this ROR scan to this ROR-intersection.
*/

static bool ror_intersect_add(ROR_INTERSECT_INFO *info,
                              ROR_SCAN_INFO* ror_scan, bool is_cpk_scan)
{
  double selectivity_mult= 1.0;

  selectivity_mult = ror_scan_selectivity(info, ror_scan);
  if (selectivity_mult == 1.0)
  {
    /* Don't add this scan if it doesn't improve selectivity. */
    return false;
  }

  info->out_rows *= selectivity_mult;

  if (is_cpk_scan)
  {
    /*
      CPK scan is used to filter out rows. We apply filtering for
      each record of every scan. Assuming 1/TIME_FOR_COMPARE_ROWID
      per check this gives us:
    */
    info->index_scan_costs += rows2double(info->index_records) /
                              TIME_FOR_COMPARE_ROWID;
  }
  else
  {
    info->index_records += info->param->table->quick_rows[ror_scan->keynr];
    info->index_scan_costs += ror_scan->index_read_cost;
    bitmap_union(&info->covered_fields, &ror_scan->covered_fields);
    if (!info->is_covering && bitmap_is_subset(&info->param->needed_fields,
                                               &info->covered_fields))
    {
      info->is_covering= true;
    }
  }

  info->total_cost= info->index_scan_costs;
  if (!info->is_covering)
  {
    COST_VECT sweep_cost;
    JOIN *join= info->param->session->lex->select_lex.join;
    bool is_interrupted= test(join && join->tables == 1);
    get_sweep_read_cost(info->param->table, double2rows(info->out_rows),
                        is_interrupted, &sweep_cost);
    info->total_cost += sweep_cost.total_cost();
  }
  return true;
}


/*
  Get best ROR-intersection plan using non-covering ROR-intersection search
  algorithm. The returned plan may be covering.

  SYNOPSIS
    get_best_ror_intersect()
      param            Parameter from test_quick_select function.
      tree             Transformed restriction condition to be used to look
                       for ROR scans.
      read_time        Do not return read plans with cost > read_time.
      are_all_covering [out] set to true if union of all scans covers all
                       fields needed by the query (and it is possible to build
                       a covering ROR-intersection)

  NOTES
    get_key_scans_params must be called before this function can be called.

    When this function is called by ROR-union construction algorithm it
    assumes it is building an uncovered ROR-intersection (and thus # of full
    records to be retrieved is wrong here). This is a hack.

  IMPLEMENTATION
    The approximate best non-covering plan search algorithm is as follows:

    find_min_ror_intersection_scan()
    {
      R= select all ROR scans;
      order R by (E(#records_matched) * key_record_length).

      S= first(R); -- set of scans that will be used for ROR-intersection
      R= R-first(S);
      min_cost= cost(S);
      min_scan= make_scan(S);
      while (R is not empty)
      {
        firstR= R - first(R);
        if (!selectivity(S + firstR < selectivity(S)))
          continue;

        S= S + first(R);
        if (cost(S) < min_cost)
        {
          min_cost= cost(S);
          min_scan= make_scan(S);
        }
      }
      return min_scan;
    }

    See ror_intersect_add function for ROR intersection costs.

    Special handling for Clustered PK scans
    Clustered PK contains all table fields, so using it as a regular scan in
    index intersection doesn't make sense: a range scan on CPK will be less
    expensive in this case.
    Clustered PK scan has special handling in ROR-intersection: it is not used
    to retrieve rows, instead its condition is used to filter row references
    we get from scans on other keys.

  RETURN
    ROR-intersection table read plan
    NULL if out of memory or no suitable plan found.
*/

static
TRP_ROR_INTERSECT *get_best_ror_intersect(const PARAM *param, SEL_TREE *tree,
                                          double read_time,
                                          bool *are_all_covering)
{
  uint32_t idx;
  double min_cost= DBL_MAX;

  if ((tree->n_ror_scans < 2) || !param->table->file->stats.records)
    return NULL;

  /*
    Step1: Collect ROR-able SEL_ARGs and create ROR_SCAN_INFO for each of
    them. Also find and save clustered PK scan if there is one.
  */
  ROR_SCAN_INFO **cur_ror_scan;
  ROR_SCAN_INFO *cpk_scan= NULL;
  uint32_t cpk_no;
  bool cpk_scan_used= false;

  if (!(tree->ror_scans= (ROR_SCAN_INFO**)alloc_root(param->mem_root,
                                                     sizeof(ROR_SCAN_INFO*)*
                                                     param->keys)))
    return NULL;
  cpk_no= ((param->table->file->primary_key_is_clustered()) ?
           param->table->s->primary_key : MAX_KEY);

  for (idx= 0, cur_ror_scan= tree->ror_scans; idx < param->keys; idx++)
  {
    ROR_SCAN_INFO *scan;
    if (! tree->ror_scans_map.test(idx))
      continue;
    if (!(scan= make_ror_scan(param, idx, tree->keys[idx])))
      return NULL;
    if (param->real_keynr[idx] == cpk_no)
    {
      cpk_scan= scan;
      tree->n_ror_scans--;
    }
    else
      *(cur_ror_scan++)= scan;
  }

  tree->ror_scans_end= cur_ror_scan;
  print_ror_scans_arr(param->table, "original",
                                          tree->ror_scans,
                                          tree->ror_scans_end);
  /*
    Ok, [ror_scans, ror_scans_end) is array of ptrs to initialized
    ROR_SCAN_INFO's.
    Step 2: Get best ROR-intersection using an approximate algorithm.
  */
  my_qsort(tree->ror_scans, tree->n_ror_scans, sizeof(ROR_SCAN_INFO*),
           (qsort_cmp)cmp_ror_scan_info);
  print_ror_scans_arr(param->table, "ordered",
                                          tree->ror_scans,
                                          tree->ror_scans_end);

  ROR_SCAN_INFO **intersect_scans; /* ROR scans used in index intersection */
  ROR_SCAN_INFO **intersect_scans_end;
  if (!(intersect_scans= (ROR_SCAN_INFO**)alloc_root(param->mem_root,
                                                     sizeof(ROR_SCAN_INFO*)*
                                                     tree->n_ror_scans)))
    return NULL;
  intersect_scans_end= intersect_scans;

  /* Create and incrementally update ROR intersection. */
  ROR_INTERSECT_INFO *intersect, *intersect_best;
  if (!(intersect= ror_intersect_init(param)) ||
      !(intersect_best= ror_intersect_init(param)))
    return NULL;

  /* [intersect_scans,intersect_scans_best) will hold the best intersection */
  ROR_SCAN_INFO **intersect_scans_best;
  cur_ror_scan= tree->ror_scans;
  intersect_scans_best= intersect_scans;
  while (cur_ror_scan != tree->ror_scans_end && !intersect->is_covering)
  {
    /* S= S + first(R);  R= R - first(R); */
    if (!ror_intersect_add(intersect, *cur_ror_scan, false))
    {
      cur_ror_scan++;
      continue;
    }

    *(intersect_scans_end++)= *(cur_ror_scan++);

    if (intersect->total_cost < min_cost)
    {
      /* Local minimum found, save it */
      ror_intersect_cpy(intersect_best, intersect);
      intersect_scans_best= intersect_scans_end;
      min_cost = intersect->total_cost;
    }
  }

  if (intersect_scans_best == intersect_scans)
  {
    return NULL;
  }

  print_ror_scans_arr(param->table,
                                          "best ROR-intersection",
                                          intersect_scans,
                                          intersect_scans_best);

  *are_all_covering= intersect->is_covering;
  uint32_t best_num= intersect_scans_best - intersect_scans;
  ror_intersect_cpy(intersect, intersect_best);

  /*
    Ok, found the best ROR-intersection of non-CPK key scans.
    Check if we should add a CPK scan. If the obtained ROR-intersection is
    covering, it doesn't make sense to add CPK scan.
  */
  if (cpk_scan && !intersect->is_covering)
  {
    if (ror_intersect_add(intersect, cpk_scan, true) &&
        (intersect->total_cost < min_cost))
    {
      cpk_scan_used= true;
      intersect_best= intersect; //just set pointer here
    }
  }

  /* Ok, return ROR-intersect plan if we have found one */
  TRP_ROR_INTERSECT *trp= NULL;
  if (min_cost < read_time && (cpk_scan_used || best_num > 1))
  {
    if (!(trp= new (param->mem_root) TRP_ROR_INTERSECT))
      return(trp);
    if (!(trp->first_scan=
           (ROR_SCAN_INFO**)alloc_root(param->mem_root,
                                       sizeof(ROR_SCAN_INFO*)*best_num)))
      return NULL;
    memcpy(trp->first_scan, intersect_scans, best_num*sizeof(ROR_SCAN_INFO*));
    trp->last_scan=  trp->first_scan + best_num;
    trp->is_covering= intersect_best->is_covering;
    trp->read_cost= intersect_best->total_cost;
    /* Prevent divisons by zero */
    ha_rows best_rows = double2rows(intersect_best->out_rows);
    if (!best_rows)
      best_rows= 1;
    set_if_smaller(param->table->quick_condition_rows, best_rows);
    trp->records= best_rows;
    trp->index_scan_costs= intersect_best->index_scan_costs;
    trp->cpk_scan= cpk_scan_used? cpk_scan: NULL;
  }
  return(trp);
}


/*
  Get best covering ROR-intersection.
  SYNOPSIS
    get_best_covering_ror_intersect()
      param     Parameter from test_quick_select function.
      tree      SEL_TREE with sets of intervals for different keys.
      read_time Don't return table read plans with cost > read_time.

  RETURN
    Best covering ROR-intersection plan
    NULL if no plan found.

  NOTES
    get_best_ror_intersect must be called for a tree before calling this
    function for it.
    This function invalidates tree->ror_scans member values.

  The following approximate algorithm is used:
    I=set of all covering indexes
    F=set of all fields to cover
    S={}

    do
    {
      Order I by (#covered fields in F desc,
                  #components asc,
                  number of first not covered component asc);
      F=F-covered by first(I);
      S=S+first(I);
      I=I-first(I);
    } while F is not empty.
*/

static
TRP_ROR_INTERSECT *get_best_covering_ror_intersect(PARAM *param,
                                                   SEL_TREE *tree,
                                                   double read_time)
{
  ROR_SCAN_INFO **ror_scan_mark;
  ROR_SCAN_INFO **ror_scans_end= tree->ror_scans_end;

  for (ROR_SCAN_INFO **scan= tree->ror_scans; scan != ror_scans_end; ++scan)
    (*scan)->key_components=
      param->table->key_info[(*scan)->keynr].key_parts;

  /*
    Run covering-ROR-search algorithm.
    Assume set I is [ror_scan .. ror_scans_end)
  */

  /*I=set of all covering indexes */
  ror_scan_mark= tree->ror_scans;

  MyBitmap *covered_fields= &param->tmp_covered_fields;
  if (! covered_fields->getBitmap())
  {
    my_bitmap_map *tmp_bitmap= (my_bitmap_map*)alloc_root(param->mem_root,
                                               param->fields_bitmap_size);
    covered_fields->setBitmap(tmp_bitmap);
  }
  if (! covered_fields->getBitmap() ||
      covered_fields->init(covered_fields->getBitmap(),
                           param->table->s->fields))
    return 0;
  covered_fields->clearAll();

  double total_cost= 0.0f;
  ha_rows records=0;
  bool all_covered;

  print_ror_scans_arr(param->table,
                                           "building covering ROR-I",
                                           ror_scan_mark, ror_scans_end);
  do
  {
    /*
      Update changed sorting info:
        #covered fields,
	number of first not covered component
      Calculate and save these values for each of remaining scans.
    */
    for (ROR_SCAN_INFO **scan= ror_scan_mark; scan != ror_scans_end; ++scan)
    {
      bitmap_subtract(&(*scan)->covered_fields, covered_fields);
      (*scan)->used_fields_covered=
        (*scan)->covered_fields.getBitsSet();
      (*scan)->first_uncovered_field=
        (*scan)->covered_fields.getFirst();
    }

    my_qsort(ror_scan_mark, ror_scans_end-ror_scan_mark, sizeof(ROR_SCAN_INFO*),
             (qsort_cmp)cmp_ror_scan_info_covering);

    print_ror_scans_arr(param->table,
                                             "remaining scans",
                                             ror_scan_mark, ror_scans_end);

    /* I=I-first(I) */
    total_cost += (*ror_scan_mark)->index_read_cost;
    records += (*ror_scan_mark)->records;
    if (total_cost > read_time)
      return NULL;
    /* F=F-covered by first(I) */
    bitmap_union(covered_fields, &(*ror_scan_mark)->covered_fields);
    all_covered= bitmap_is_subset(&param->needed_fields, covered_fields);
  } while ((++ror_scan_mark < ror_scans_end) && !all_covered);

  if (!all_covered || (ror_scan_mark - tree->ror_scans) == 1)
    return NULL;

  /*
    Ok, [tree->ror_scans .. ror_scan) holds covering index_intersection with
    cost total_cost.
  */
  print_ror_scans_arr(param->table,
                                           "creating covering ROR-intersect",
                                           tree->ror_scans, ror_scan_mark);

  /* Add priority queue use cost. */
  total_cost += rows2double(records)*
                log((double)(ror_scan_mark - tree->ror_scans)) /
                (TIME_FOR_COMPARE_ROWID * M_LN2);

  if (total_cost > read_time)
    return NULL;

  TRP_ROR_INTERSECT *trp;
  if (!(trp= new (param->mem_root) TRP_ROR_INTERSECT))
    return(trp);
  uint32_t best_num= (ror_scan_mark - tree->ror_scans);
  if (!(trp->first_scan= (ROR_SCAN_INFO**)alloc_root(param->mem_root,
                                                     sizeof(ROR_SCAN_INFO*)*
                                                     best_num)))
    return NULL;
  memcpy(trp->first_scan, tree->ror_scans, best_num*sizeof(ROR_SCAN_INFO*));
  trp->last_scan=  trp->first_scan + best_num;
  trp->is_covering= true;
  trp->read_cost= total_cost;
  trp->records= records;
  trp->cpk_scan= NULL;
  set_if_smaller(param->table->quick_condition_rows, records);

  return(trp);
}


/*
  Get best "range" table read plan for given SEL_TREE, also update some info

  SYNOPSIS
    get_key_scans_params()
      param                    Parameters from test_quick_select
      tree                     Make range select for this SEL_TREE
      index_read_must_be_used  true <=> assume 'index only' option will be set
                               (except for clustered PK indexes)
      update_tbl_stats         true <=> update table->quick_* with information
                               about range scans we've evaluated.
      read_time                Maximum cost. i.e. don't create read plans with
                               cost > read_time.

  DESCRIPTION
    Find the best "range" table read plan for given SEL_TREE.
    The side effects are
     - tree->ror_scans is updated to indicate which scans are ROR scans.
     - if update_tbl_stats=true then table->quick_* is updated with info
       about every possible range scan.

  RETURN
    Best range read plan
    NULL if no plan found or error occurred
*/

static TRP_RANGE *get_key_scans_params(PARAM *param, SEL_TREE *tree,
                                       bool index_read_must_be_used,
                                       bool update_tbl_stats,
                                       double read_time)
{
  uint32_t idx;
  SEL_ARG **key,**end, **key_to_read= NULL;
  ha_rows best_records= 0;
  uint32_t    best_mrr_flags= 0, best_buf_size= 0;
  TRP_RANGE* read_plan= NULL;
  /*
    Note that there may be trees that have type SEL_TREE::KEY but contain no
    key reads at all, e.g. tree for expression "key1 is not null" where key1
    is defined as "not null".
  */
  print_sel_tree(param, tree, &tree->keys_map, "tree scans");
  tree->ror_scans_map.reset();
  tree->n_ror_scans= 0;
  for (idx= 0,key=tree->keys, end=key+param->keys; key != end; key++,idx++)
  {
    if (*key)
    {
      ha_rows found_records;
      COST_VECT cost;
      double found_read_time;
      uint32_t mrr_flags, buf_size;
      uint32_t keynr= param->real_keynr[idx];
      if ((*key)->type == SEL_ARG::MAYBE_KEY ||
          (*key)->maybe_flag)
        param->needed_reg->set(keynr);

      bool read_index_only= index_read_must_be_used ||
                            param->table->covering_keys.test(keynr);

      found_records= check_quick_select(param, idx, read_index_only, *key,
                                        update_tbl_stats, &mrr_flags,
                                        &buf_size, &cost);
      found_read_time= cost.total_cost();
      if ((found_records != HA_POS_ERROR) && param->is_ror_scan)
      {
        tree->n_ror_scans++;
        tree->ror_scans_map.set(idx);
      }
      if (read_time > found_read_time && found_records != HA_POS_ERROR)
      {
        read_time=    found_read_time;
        best_records= found_records;
        key_to_read=  key;
        best_mrr_flags= mrr_flags;
        best_buf_size=  buf_size;
      }
    }
  }

  print_sel_tree(param, tree, &tree->ror_scans_map, "ROR scans");
  if (key_to_read)
  {
    idx= key_to_read - tree->keys;
    if ((read_plan= new (param->mem_root) TRP_RANGE(*key_to_read, idx,
                                                    best_mrr_flags)))
    {
      read_plan->records= best_records;
      read_plan->is_ror= tree->ror_scans_map.test(idx);
      read_plan->read_cost= read_time;
      read_plan->mrr_buf_size= best_buf_size;
    }
  }

  return(read_plan);
}


QUICK_SELECT_I *TRP_INDEX_MERGE::make_quick(PARAM *param, bool, MEM_ROOT *)
{
  QUICK_INDEX_MERGE_SELECT *quick_imerge;
  QUICK_RANGE_SELECT *quick;
  /* index_merge always retrieves full rows, ignore retrieve_full_rows */
  if (!(quick_imerge= new QUICK_INDEX_MERGE_SELECT(param->session, param->table)))
    return NULL;

  quick_imerge->records= records;
  quick_imerge->read_time= read_cost;
  for (TRP_RANGE **range_scan= range_scans; range_scan != range_scans_end;
       range_scan++)
  {
    if (!(quick= (QUICK_RANGE_SELECT*)
          ((*range_scan)->make_quick(param, false, &quick_imerge->alloc)))||
        quick_imerge->push_quick_back(quick))
    {
      delete quick;
      delete quick_imerge;
      return NULL;
    }
  }
  return quick_imerge;
}

QUICK_SELECT_I *TRP_ROR_INTERSECT::make_quick(PARAM *param,
                                              bool retrieve_full_rows,
                                              MEM_ROOT *parent_alloc)
{
  QUICK_ROR_INTERSECT_SELECT *quick_intrsect;
  QUICK_RANGE_SELECT *quick;
  MEM_ROOT *alloc;

  if ((quick_intrsect=
         new QUICK_ROR_INTERSECT_SELECT(param->session, param->table,
                                        (retrieve_full_rows? (!is_covering) :
                                         false),
                                        parent_alloc)))
  {
    print_ror_scans_arr(param->table,
                                             "creating ROR-intersect",
                                             first_scan, last_scan);
    alloc= parent_alloc? parent_alloc: &quick_intrsect->alloc;
    for (; first_scan != last_scan;++first_scan)
    {
      if (!(quick= get_quick_select(param, (*first_scan)->idx,
                                    (*first_scan)->sel_arg,
                                    HA_MRR_USE_DEFAULT_IMPL | HA_MRR_SORTED,
                                    0, alloc)) ||
          quick_intrsect->push_quick_back(quick))
      {
        delete quick_intrsect;
        return NULL;
      }
    }
    if (cpk_scan)
    {
      if (!(quick= get_quick_select(param, cpk_scan->idx,
                                    cpk_scan->sel_arg,
                                    HA_MRR_USE_DEFAULT_IMPL | HA_MRR_SORTED,
                                    0, alloc)))
      {
        delete quick_intrsect;
        return NULL;
      }
      quick->file= NULL;
      quick_intrsect->cpk_quick= quick;
    }
    quick_intrsect->records= records;
    quick_intrsect->read_time= read_cost;
  }
  return(quick_intrsect);
}


QUICK_SELECT_I *TRP_ROR_UNION::make_quick(PARAM *param, bool, MEM_ROOT *)
{
  QUICK_ROR_UNION_SELECT *quick_roru;
  TABLE_READ_PLAN **scan;
  QUICK_SELECT_I *quick;
  /*
    It is impossible to construct a ROR-union that will not retrieve full
    rows, ignore retrieve_full_rows parameter.
  */
  if ((quick_roru= new QUICK_ROR_UNION_SELECT(param->session, param->table)))
  {
    for (scan= first_ror; scan != last_ror; scan++)
    {
      if (!(quick= (*scan)->make_quick(param, false, &quick_roru->alloc)) ||
          quick_roru->push_quick_back(quick))
        return NULL;
    }
    quick_roru->records= records;
    quick_roru->read_time= read_cost;
  }
  return(quick_roru);
}


/*
  Build a SEL_TREE for <> or NOT BETWEEN predicate

  SYNOPSIS
    get_ne_mm_tree()
      param       PARAM from SQL_SELECT::test_quick_select
      cond_func   item for the predicate
      field       field in the predicate
      lt_value    constant that field should be smaller
      gt_value    constant that field should be greaterr
      cmp_type    compare type for the field

  RETURN
    #  Pointer to tree built tree
    0  on error
*/

static SEL_TREE *get_ne_mm_tree(RANGE_OPT_PARAM *param, Item_func *cond_func,
                                Field *field,
                                Item *lt_value, Item *gt_value,
                                Item_result cmp_type)
{
  SEL_TREE *tree;
  tree= get_mm_parts(param, cond_func, field, Item_func::LT_FUNC,
                     lt_value, cmp_type);
  if (tree)
  {
    tree= tree_or(param, tree, get_mm_parts(param, cond_func, field,
					    Item_func::GT_FUNC,
					    gt_value, cmp_type));
  }
  return tree;
}


/*
  Build a SEL_TREE for a simple predicate

  SYNOPSIS
    get_func_mm_tree()
      param       PARAM from SQL_SELECT::test_quick_select
      cond_func   item for the predicate
      field       field in the predicate
      value       constant in the predicate
      cmp_type    compare type for the field
      inv         true <> NOT cond_func is considered
                  (makes sense only when cond_func is BETWEEN or IN)

  RETURN
    Pointer to the tree built tree
*/

static SEL_TREE *get_func_mm_tree(RANGE_OPT_PARAM *param, Item_func *cond_func,
                                  Field *field, Item *value,
                                  Item_result cmp_type, bool inv)
{
  SEL_TREE *tree= 0;

  switch (cond_func->functype()) {

  case Item_func::NE_FUNC:
    tree= get_ne_mm_tree(param, cond_func, field, value, value, cmp_type);
    break;

  case Item_func::BETWEEN:
  {
    if (!value)
    {
      if (inv)
      {
        tree= get_ne_mm_tree(param, cond_func, field, cond_func->arguments()[1],
                             cond_func->arguments()[2], cmp_type);
      }
      else
      {
        tree= get_mm_parts(param, cond_func, field, Item_func::GE_FUNC,
		           cond_func->arguments()[1],cmp_type);
        if (tree)
        {
          tree= tree_and(param, tree, get_mm_parts(param, cond_func, field,
					           Item_func::LE_FUNC,
					           cond_func->arguments()[2],
                                                   cmp_type));
        }
      }
    }
    else
      tree= get_mm_parts(param, cond_func, field,
                         (inv ?
                          (value == (Item*)1 ? Item_func::GT_FUNC :
                                               Item_func::LT_FUNC):
                          (value == (Item*)1 ? Item_func::LE_FUNC :
                                               Item_func::GE_FUNC)),
                         cond_func->arguments()[0], cmp_type);
    break;
  }
  case Item_func::IN_FUNC:
  {
    Item_func_in *func=(Item_func_in*) cond_func;

    /*
      Array for IN() is constructed when all values have the same result
      type. Tree won't be built for values with different result types,
      so we check it here to avoid unnecessary work.
    */
    if (!func->arg_types_compatible)
      break;

    if (inv)
    {
      if (func->array && func->array->result_type() != ROW_RESULT)
      {
        /*
          We get here for conditions in form "t.key NOT IN (c1, c2, ...)",
          where c{i} are constants. Our goal is to produce a SEL_TREE that
          represents intervals:

          ($MIN<t.key<c1) OR (c1<t.key<c2) OR (c2<t.key<c3) OR ...    (*)

          where $MIN is either "-inf" or NULL.

          The most straightforward way to produce it is to convert NOT IN
          into "(t.key != c1) AND (t.key != c2) AND ... " and let the range
          analyzer to build SEL_TREE from that. The problem is that the
          range analyzer will use O(N^2) memory (which is probably a bug),
          and people do use big NOT IN lists (e.g. see BUG#15872, BUG#21282),
          will run out of memory.

          Another problem with big lists like (*) is that a big list is
          unlikely to produce a good "range" access, while considering that
          range access will require expensive CPU calculations (and for
          MyISAM even index accesses). In short, big NOT IN lists are rarely
          worth analyzing.

          Considering the above, we'll handle NOT IN as follows:
          * if the number of entries in the NOT IN list is less than
            NOT_IN_IGNORE_THRESHOLD, construct the SEL_TREE (*) manually.
          * Otherwise, don't produce a SEL_TREE.
        */
#define NOT_IN_IGNORE_THRESHOLD 1000
        MEM_ROOT *tmp_root= param->mem_root;
        param->session->mem_root= param->old_root;
        /*
          Create one Item_type constant object. We'll need it as
          get_mm_parts only accepts constant values wrapped in Item_Type
          objects.
          We create the Item on param->mem_root which points to
          per-statement mem_root (while session->mem_root is currently pointing
          to mem_root local to range optimizer).
        */
        Item *value_item= func->array->create_item();
        param->session->mem_root= tmp_root;

        if (func->array->count > NOT_IN_IGNORE_THRESHOLD || !value_item)
          break;

        /* Get a SEL_TREE for "(-inf|NULL) < X < c_0" interval.  */
        uint32_t i=0;
        do
        {
          func->array->value_to_item(i, value_item);
          tree= get_mm_parts(param, cond_func, field, Item_func::LT_FUNC,
                             value_item, cmp_type);
          if (!tree)
            break;
          i++;
        } while (i < func->array->count && tree->type == SEL_TREE::IMPOSSIBLE);

        if (!tree || tree->type == SEL_TREE::IMPOSSIBLE)
        {
          /* We get here in cases like "t.unsigned NOT IN (-1,-2,-3) */
          tree= NULL;
          break;
        }
        SEL_TREE *tree2;
        for (; i < func->array->count; i++)
        {
          if (func->array->compare_elems(i, i-1))
          {
            /* Get a SEL_TREE for "-inf < X < c_i" interval */
            func->array->value_to_item(i, value_item);
            tree2= get_mm_parts(param, cond_func, field, Item_func::LT_FUNC,
                                value_item, cmp_type);
            if (!tree2)
            {
              tree= NULL;
              break;
            }

            /* Change all intervals to be "c_{i-1} < X < c_i" */
            for (uint32_t idx= 0; idx < param->keys; idx++)
            {
              SEL_ARG *new_interval, *last_val;
              if (((new_interval= tree2->keys[idx])) &&
                  (tree->keys[idx]) &&
                  ((last_val= tree->keys[idx]->last())))
              {
                new_interval->min_value= last_val->max_value;
                new_interval->min_flag= NEAR_MIN;
              }
            }
            /*
              The following doesn't try to allocate memory so no need to
              check for NULL.
            */
            tree= tree_or(param, tree, tree2);
          }
        }

        if (tree && tree->type != SEL_TREE::IMPOSSIBLE)
        {
          /*
            Get the SEL_TREE for the last "c_last < X < +inf" interval
            (value_item cotains c_last already)
          */
          tree2= get_mm_parts(param, cond_func, field, Item_func::GT_FUNC,
                              value_item, cmp_type);
          tree= tree_or(param, tree, tree2);
        }
      }
      else
      {
        tree= get_ne_mm_tree(param, cond_func, field,
                             func->arguments()[1], func->arguments()[1],
                             cmp_type);
        if (tree)
        {
          Item **arg, **end;
          for (arg= func->arguments()+2, end= arg+func->argument_count()-2;
               arg < end ; arg++)
          {
            tree=  tree_and(param, tree, get_ne_mm_tree(param, cond_func, field,
                                                        *arg, *arg, cmp_type));
          }
        }
      }
    }
    else
    {
      tree= get_mm_parts(param, cond_func, field, Item_func::EQ_FUNC,
                         func->arguments()[1], cmp_type);
      if (tree)
      {
        Item **arg, **end;
        for (arg= func->arguments()+2, end= arg+func->argument_count()-2;
             arg < end ; arg++)
        {
          tree= tree_or(param, tree, get_mm_parts(param, cond_func, field,
                                                  Item_func::EQ_FUNC,
                                                  *arg, cmp_type));
        }
      }
    }
    break;
  }
  default:
  {
    /*
       Here the function for the following predicates are processed:
       <, <=, =, >=, >, LIKE, IS NULL, IS NOT NULL.
       If the predicate is of the form (value op field) it is handled
       as the equivalent predicate (field rev_op value), e.g.
       2 <= a is handled as a >= 2.
    */
    Item_func::Functype func_type=
      (value != cond_func->arguments()[0]) ? cond_func->functype() :
        ((Item_bool_func2*) cond_func)->rev_functype();
    tree= get_mm_parts(param, cond_func, field, func_type, value, cmp_type);
  }
  }

  return(tree);
}


/*
  Build conjunction of all SEL_TREEs for a simple predicate applying equalities

  SYNOPSIS
    get_full_func_mm_tree()
      param       PARAM from SQL_SELECT::test_quick_select
      cond_func   item for the predicate
      field_item  field in the predicate
      value       constant in the predicate
                  (for BETWEEN it contains the number of the field argument,
                   for IN it's always 0)
      inv         true <> NOT cond_func is considered
                  (makes sense only when cond_func is BETWEEN or IN)

  DESCRIPTION
    For a simple SARGable predicate of the form (f op c), where f is a field and
    c is a constant, the function builds a conjunction of all SEL_TREES that can
    be obtained by the substitution of f for all different fields equal to f.

  NOTES
    If the WHERE condition contains a predicate (fi op c),
    then not only SELL_TREE for this predicate is built, but
    the trees for the results of substitution of fi for
    each fj belonging to the same multiple equality as fi
    are built as well.
    E.g. for WHERE t1.a=t2.a AND t2.a > 10
    a SEL_TREE for t2.a > 10 will be built for quick select from t2
    and
    a SEL_TREE for t1.a > 10 will be built for quick select from t1.

    A BETWEEN predicate of the form (fi [NOT] BETWEEN c1 AND c2) is treated
    in a similar way: we build a conjuction of trees for the results
    of all substitutions of fi for equal fj.
    Yet a predicate of the form (c BETWEEN f1i AND f2i) is processed
    differently. It is considered as a conjuction of two SARGable
    predicates (f1i <= c) and (f2i <=c) and the function get_full_func_mm_tree
    is called for each of them separately producing trees for
       AND j (f1j <=c ) and AND j (f2j <= c)
    After this these two trees are united in one conjunctive tree.
    It's easy to see that the same tree is obtained for
       AND j,k (f1j <=c AND f2k<=c)
    which is equivalent to
       AND j,k (c BETWEEN f1j AND f2k).
    The validity of the processing of the predicate (c NOT BETWEEN f1i AND f2i)
    which equivalent to (f1i > c OR f2i < c) is not so obvious. Here the
    function get_full_func_mm_tree is called for (f1i > c) and (f2i < c)
    producing trees for AND j (f1j > c) and AND j (f2j < c). Then this two
    trees are united in one OR-tree. The expression
      (AND j (f1j > c) OR AND j (f2j < c)
    is equivalent to the expression
      AND j,k (f1j > c OR f2k < c)
    which is just a translation of
      AND j,k (c NOT BETWEEN f1j AND f2k)

    In the cases when one of the items f1, f2 is a constant c1 we do not create
    a tree for it at all. It works for BETWEEN predicates but does not
    work for NOT BETWEEN predicates as we have to evaluate the expression
    with it. If it is true then the other tree can be completely ignored.
    We do not do it now and no trees are built in these cases for
    NOT BETWEEN predicates.

    As to IN predicates only ones of the form (f IN (c1,...,cn)),
    where f1 is a field and c1,...,cn are constant, are considered as
    SARGable. We never try to narrow the index scan using predicates of
    the form (c IN (c1,...,f,...,cn)).

  RETURN
    Pointer to the tree representing the built conjunction of SEL_TREEs
*/

static SEL_TREE *get_full_func_mm_tree(RANGE_OPT_PARAM *param,
                                       Item_func *cond_func,
                                       Item_field *field_item, Item *value,
                                       bool inv)
{
  SEL_TREE *tree= 0;
  SEL_TREE *ftree= 0;
  table_map ref_tables= 0;
  table_map param_comp= ~(param->prev_tables | param->read_tables |
		          param->current_table);

  for (uint32_t i= 0; i < cond_func->arg_count; i++)
  {
    Item *arg= cond_func->arguments()[i]->real_item();
    if (arg != field_item)
      ref_tables|= arg->used_tables();
  }

  Field *field= field_item->field;
  field->setWriteSet();

  Item_result cmp_type= field->cmp_type();
  if (!((ref_tables | field->table->map) & param_comp))
    ftree= get_func_mm_tree(param, cond_func, field, value, cmp_type, inv);
  Item_equal *item_equal= field_item->item_equal;
  if (item_equal)
  {
    Item_equal_iterator it(*item_equal);
    Item_field *item;
    while ((item= it++))
    {
      Field *f= item->field;
      f->setWriteSet();

      if (field->eq(f))
        continue;
      if (!((ref_tables | f->table->map) & param_comp))
      {
        tree= get_func_mm_tree(param, cond_func, f, value, cmp_type, inv);
        ftree= !ftree ? tree : tree_and(param, ftree, tree);
      }
    }
  }
  return(ftree);
}

	/* make a select tree of all keys in condition */

static SEL_TREE *get_mm_tree(RANGE_OPT_PARAM *param,COND *cond)
{
  SEL_TREE *tree=0;
  SEL_TREE *ftree= 0;
  Item_field *field_item= 0;
  bool inv= false;
  Item *value= 0;

  if (cond->type() == Item::COND_ITEM)
  {
    List_iterator<Item> li(*((Item_cond*) cond)->argument_list());

    if (((Item_cond*) cond)->functype() == Item_func::COND_AND_FUNC)
    {
      tree=0;
      Item *item;
      while ((item=li++))
      {
	SEL_TREE *new_tree=get_mm_tree(param,item);
	if (param->session->is_fatal_error ||
            param->alloced_sel_args > SEL_ARG::MAX_SEL_ARGS)
	  return 0;	// out of memory
	tree=tree_and(param,tree,new_tree);
	if (tree && tree->type == SEL_TREE::IMPOSSIBLE)
	  break;
      }
    }
    else
    {						// COND OR
      tree=get_mm_tree(param,li++);
      if (tree)
      {
	Item *item;
	while ((item=li++))
	{
	  SEL_TREE *new_tree=get_mm_tree(param,item);
	  if (!new_tree)
	    return 0;	// out of memory
	  tree=tree_or(param,tree,new_tree);
	  if (!tree || tree->type == SEL_TREE::ALWAYS)
	    break;
	}
      }
    }
    return(tree);
  }
  /* Here when simple cond */
  if (cond->const_item())
  {
    /*
      During the cond->val_int() evaluation we can come across a subselect
      item which may allocate memory on the session->mem_root and assumes
      all the memory allocated has the same life span as the subselect
      item itself. So we have to restore the thread's mem_root here.
    */
    MEM_ROOT *tmp_root= param->mem_root;
    param->session->mem_root= param->old_root;
    tree= cond->val_int() ? new(tmp_root) SEL_TREE(SEL_TREE::ALWAYS) :
                            new(tmp_root) SEL_TREE(SEL_TREE::IMPOSSIBLE);
    param->session->mem_root= tmp_root;
    return(tree);
  }

  table_map ref_tables= 0;
  table_map param_comp= ~(param->prev_tables | param->read_tables |
		          param->current_table);
  if (cond->type() != Item::FUNC_ITEM)
  {						// Should be a field
    ref_tables= cond->used_tables();
    if ((ref_tables & param->current_table) ||
	(ref_tables & ~(param->prev_tables | param->read_tables)))
      return 0;
    return(new SEL_TREE(SEL_TREE::MAYBE));
  }

  Item_func *cond_func= (Item_func*) cond;
  if (cond_func->functype() == Item_func::BETWEEN ||
      cond_func->functype() == Item_func::IN_FUNC)
    inv= ((Item_func_opt_neg *) cond_func)->negated;
  else if (cond_func->select_optimize() == Item_func::OPTIMIZE_NONE)
    return 0;

  param->cond= cond;

  switch (cond_func->functype()) {
  case Item_func::BETWEEN:
    if (cond_func->arguments()[0]->real_item()->type() == Item::FIELD_ITEM)
    {
      field_item= (Item_field*) (cond_func->arguments()[0]->real_item());
      ftree= get_full_func_mm_tree(param, cond_func, field_item, NULL, inv);
    }

    /*
      Concerning the code below see the NOTES section in
      the comments for the function get_full_func_mm_tree()
    */
    for (uint32_t i= 1 ; i < cond_func->arg_count ; i++)
    {
      if (cond_func->arguments()[i]->real_item()->type() == Item::FIELD_ITEM)
      {
        field_item= (Item_field*) (cond_func->arguments()[i]->real_item());
        SEL_TREE *tmp= get_full_func_mm_tree(param, cond_func,
                                    field_item, (Item*)(intptr_t)i, inv);
        if (inv)
          tree= !tree ? tmp : tree_or(param, tree, tmp);
        else
          tree= tree_and(param, tree, tmp);
      }
      else if (inv)
      {
        tree= 0;
        break;
      }
    }

    ftree = tree_and(param, ftree, tree);
    break;
  case Item_func::IN_FUNC:
  {
    Item_func_in *func=(Item_func_in*) cond_func;
    if (func->key_item()->real_item()->type() != Item::FIELD_ITEM)
      return 0;
    field_item= (Item_field*) (func->key_item()->real_item());
    ftree= get_full_func_mm_tree(param, cond_func, field_item, NULL, inv);
    break;
  }
  case Item_func::MULT_EQUAL_FUNC:
  {
    Item_equal *item_equal= (Item_equal *) cond;
    if (!(value= item_equal->get_const()))
      return 0;
    Item_equal_iterator it(*item_equal);
    ref_tables= value->used_tables();
    while ((field_item= it++))
    {
      Field *field= field_item->field;
      field->setWriteSet();

      Item_result cmp_type= field->cmp_type();
      if (!((ref_tables | field->table->map) & param_comp))
      {
        tree= get_mm_parts(param, cond, field, Item_func::EQ_FUNC,
		           value,cmp_type);
        ftree= !ftree ? tree : tree_and(param, ftree, tree);
      }
    }

    return(ftree);
  }
  default:
    if (cond_func->arguments()[0]->real_item()->type() == Item::FIELD_ITEM)
    {
      field_item= (Item_field*) (cond_func->arguments()[0]->real_item());
      value= cond_func->arg_count > 1 ? cond_func->arguments()[1] : 0;
    }
    else if (cond_func->have_rev_func() &&
             cond_func->arguments()[1]->real_item()->type() ==
                                                            Item::FIELD_ITEM)
    {
      field_item= (Item_field*) (cond_func->arguments()[1]->real_item());
      value= cond_func->arguments()[0];
    }
    else
      return 0;
    ftree= get_full_func_mm_tree(param, cond_func, field_item, value, inv);
  }

  return(ftree);
}


static SEL_TREE *
get_mm_parts(RANGE_OPT_PARAM *param, COND *cond_func, Field *field,
	     Item_func::Functype type,
	     Item *value, Item_result)
{
  if (field->table != param->table)
    return 0;

  KEY_PART *key_part = param->key_parts;
  KEY_PART *end = param->key_parts_end;
  SEL_TREE *tree=0;
  if (value &&
      value->used_tables() & ~(param->prev_tables | param->read_tables))
    return 0;
  for (; key_part != end ; key_part++)
  {
    if (field->eq(key_part->field))
    {
      SEL_ARG *sel_arg=0;
      if (!tree && !(tree=new SEL_TREE()))
	return 0;				// OOM
      if (!value || !(value->used_tables() & ~param->read_tables))
      {
	sel_arg=get_mm_leaf(param,cond_func,
			    key_part->field,key_part,type,value);
	if (!sel_arg)
	  continue;
	if (sel_arg->type == SEL_ARG::IMPOSSIBLE)
	{
	  tree->type=SEL_TREE::IMPOSSIBLE;
	  return(tree);
	}
      }
      else
      {
	// This key may be used later
	if (!(sel_arg= new SEL_ARG(SEL_ARG::MAYBE_KEY)))
	  return 0;			// OOM
      }
      sel_arg->part=(unsigned char) key_part->part;
      tree->keys[key_part->key]=sel_add(tree->keys[key_part->key],sel_arg);
      tree->keys_map.set(key_part->key);
    }
  }

  return(tree);
}


static SEL_ARG *
get_mm_leaf(RANGE_OPT_PARAM *param, COND *conf_func, Field *field,
            KEY_PART *key_part, Item_func::Functype type,Item *value)
{
  uint32_t maybe_null=(uint32_t) field->real_maybe_null();
  bool optimize_range;
  SEL_ARG *tree= 0;
  MEM_ROOT *alloc= param->mem_root;
  unsigned char *str;
  int err= 0;

  /*
    We need to restore the runtime mem_root of the thread in this
    function because it evaluates the value of its argument, while
    the argument can be any, e.g. a subselect. The subselect
    items, in turn, assume that all the memory allocated during
    the evaluation has the same life span as the item itself.
    TODO: opt_range.cc should not reset session->mem_root at all.
  */
  param->session->mem_root= param->old_root;
  if (!value)					// IS NULL or IS NOT NULL
  {
    if (field->table->maybe_null)		// Can't use a key on this
      goto end;
    if (!maybe_null)				// Not null field
    {
      if (type == Item_func::ISNULL_FUNC)
        tree= &null_element;
      goto end;
    }
    if (!(tree= new (alloc) SEL_ARG(field,is_null_string,is_null_string)))
      goto end;                                 // out of memory
    if (type == Item_func::ISNOTNULL_FUNC)
    {
      tree->min_flag=NEAR_MIN;		    /* IS NOT NULL ->  X > NULL */
      tree->max_flag=NO_MAX_RANGE;
    }
    goto end;
  }

  /*
    1. Usually we can't use an index if the column collation
       differ from the operation collation.

    2. However, we can reuse a case insensitive index for
       the binary searches:

       WHERE latin1_swedish_ci_column = 'a' COLLATE lati1_bin;

       WHERE latin1_swedish_ci_colimn = BINARY 'a '

  */
  if (field->result_type() == STRING_RESULT &&
      value->result_type() == STRING_RESULT &&
      ((Field_str*)field)->charset() != conf_func->compare_collation() &&
      !(conf_func->compare_collation()->state & MY_CS_BINSORT))
    goto end;

  if (param->using_real_indexes)
    optimize_range= field->optimize_range(param->real_keynr[key_part->key],
                                          key_part->part);
  else
    optimize_range= true;

  if (type == Item_func::LIKE_FUNC)
  {
    bool like_error;
    char buff1[MAX_FIELD_WIDTH];
    unsigned char *min_str,*max_str;
    String tmp(buff1,sizeof(buff1),value->collation.collation),*res;
    size_t length, offset, min_length, max_length;
    uint32_t field_length= field->pack_length()+maybe_null;

    if (!optimize_range)
      goto end;
    if (!(res= value->val_str(&tmp)))
    {
      tree= &null_element;
      goto end;
    }

    /*
      TODO:
      Check if this was a function. This should have be optimized away
      in the sql_select.cc
    */
    if (res != &tmp)
    {
      tmp.copy(*res);				// Get own copy
      res= &tmp;
    }
    if (field->cmp_type() != STRING_RESULT)
      goto end;                                 // Can only optimize strings

    offset=maybe_null;
    length=key_part->store_length;

    if (length != key_part->length  + maybe_null)
    {
      /* key packed with length prefix */
      offset+= HA_KEY_BLOB_LENGTH;
      field_length= length - HA_KEY_BLOB_LENGTH;
    }
    else
    {
      if (unlikely(length < field_length))
      {
	/*
	  This can only happen in a table created with UNIREG where one key
	  overlaps many fields
	*/
	length= field_length;
      }
      else
	field_length= length;
    }
    length+=offset;
    if (!(min_str= (unsigned char*) alloc_root(alloc, length*2)))
      goto end;

    max_str=min_str+length;
    if (maybe_null)
      max_str[0]= min_str[0]=0;

    field_length-= maybe_null;
    int escape_code=
      make_escape_code(field->charset(),
                       ((Item_func_like*)(param->cond))->escape);
    like_error= my_like_range(field->charset(),
			      res->ptr(), res->length(),
                              escape_code,
			      wild_one, wild_many,
			      field_length,
			      (char*) min_str+offset, (char*) max_str+offset,
			      &min_length, &max_length);
    if (like_error)				// Can't optimize with LIKE
      goto end;

    if (offset != maybe_null)			// BLOB or VARCHAR
    {
      int2store(min_str+maybe_null,min_length);
      int2store(max_str+maybe_null,max_length);
    }
    tree= new (alloc) SEL_ARG(field, min_str, max_str);
    goto end;
  }

  if (!optimize_range &&
      type != Item_func::EQ_FUNC &&
      type != Item_func::EQUAL_FUNC)
    goto end;                                   // Can't optimize this

  /*
    We can't always use indexes when comparing a string index to a number
    cmp_type() is checked to allow compare of dates to numbers
  */
  if (field->result_type() == STRING_RESULT &&
      value->result_type() != STRING_RESULT &&
      field->cmp_type() != value->result_type())
    goto end;

  /*
   * Some notes from Jay...
   *
   * OK, so previously, and in MySQL, what the optimizer does here is
   * override the sql_mode variable to ignore out-of-range or bad date-
   * time values.  It does this because the optimizer is populating the
   * field variable with the incoming value from the comparison field, 
   * and the value may exceed the bounds of a proper column type.
   *
   * For instance, assume the following:
   *
   * CREATE TABLE t1 (ts TIMESTAMP); 
   * INSERT INTO t1 ('2009-03-04 00:00:00');
   * CREATE TABLE t2 (dt1 DATETIME, dt2 DATETIME); 
   * INSERT INT t2 ('2003-12-31 00:00:00','2999-12-31 00:00:00');
   *
   * If we issue this query:
   *
   * SELECT * FROM t1, t2 WHERE t1.ts BETWEEN t2.dt1 AND t2.dt2;
   *
   * We will come into bounds issues.  Field_timestamp::store() will be
   * called with a datetime value of "2999-12-31 00:00:00" and will throw
   * an error for out-of-bounds.  MySQL solves this via a hack with sql_mode
   * but Drizzle always throws errors on bad data storage in a Field class.
   *
   * Therefore, to get around the problem of the Field class being used for
   * "storage" here without actually storing anything...we must check to see 
   * if the value being stored in a Field_timestamp here is out of range.  If
   * it is, then we must convert to the highest Timestamp value (or lowest,
   * depending on whether the datetime is before or after the epoch.
   */
  if (field->type() == DRIZZLE_TYPE_TIMESTAMP)
  {
    /* 
     * The left-side of the range comparison is a timestamp field.  Therefore, 
     * we must check to see if the value in the right-hand side is outside the
     * range of the UNIX epoch, and cut to the epoch bounds if it is.
     */
    /* Datetime and date columns are Item::FIELD_ITEM ... and have a result type of STRING_RESULT */
    if (value->real_item()->type() == Item::FIELD_ITEM
        && value->result_type() == STRING_RESULT)
    {
      char buff[drizzled::DateTime::MAX_STRING_LENGTH];
      String tmp(buff, sizeof(buff), &my_charset_bin);
      String *res= value->val_str(&tmp);

      if (!res)
        goto end;
      else
      {
        /* 
         * Create a datetime from the string and compare to fixed timestamp
         * instances representing the epoch boundaries.
         */
        drizzled::DateTime value_datetime;

        if (! value_datetime.from_string(res->c_ptr(), (size_t) res->length()))
          goto end;

        drizzled::Timestamp max_timestamp;
        drizzled::Timestamp min_timestamp;

        (void) max_timestamp.from_time_t((time_t) INT32_MAX);
        (void) min_timestamp.from_time_t((time_t) 0);

        /* We rely on Temporal class operator overloads to do our comparisons. */
        if (value_datetime < min_timestamp)
        {
          /* 
           * Datetime in right-hand side column is before UNIX epoch, so adjust to
           * lower bound.
           */
          char new_value_buff[drizzled::DateTime::MAX_STRING_LENGTH];
          int new_value_length;
          String new_value_string(new_value_buff, sizeof(new_value_buff), &my_charset_bin);

          new_value_length= min_timestamp.to_string(new_value_string.c_ptr(),
				    drizzled::DateTime::MAX_STRING_LENGTH);
	  assert((new_value_length+1) < drizzled::DateTime::MAX_STRING_LENGTH);
          new_value_string.length(new_value_length);
          err= value->save_str_value_in_field(field, &new_value_string);
        }
        else if (value_datetime > max_timestamp)
        {
          /*
           * Datetime in right hand side column is after UNIX epoch, so adjust
           * to the higher bound of the epoch.
           */
          char new_value_buff[drizzled::DateTime::MAX_STRING_LENGTH];
          int new_value_length;
          String new_value_string(new_value_buff, sizeof(new_value_buff), &my_charset_bin);

          new_value_length= max_timestamp.to_string(new_value_string.c_ptr(),
					drizzled::DateTime::MAX_STRING_LENGTH);
	  assert((new_value_length+1) < drizzled::DateTime::MAX_STRING_LENGTH);
          new_value_string.length(new_value_length);
          err= value->save_str_value_in_field(field, &new_value_string);
        }
        else
          err= value->save_in_field(field, 1);
      }
    }
    else /* Not a datetime -> timestamp comparison */
      err= value->save_in_field(field, 1);
  }
  else /* Not a timestamp comparison */
    err= value->save_in_field(field, 1);

  if (err > 0)
  {
    if (field->cmp_type() != value->result_type())
    {
      if ((type == Item_func::EQ_FUNC || type == Item_func::EQUAL_FUNC) &&
          value->result_type() == item_cmp_type(field->result_type(),
                                                value->result_type()))
      {
        tree= new (alloc) SEL_ARG(field, 0, 0);
        tree->type= SEL_ARG::IMPOSSIBLE;
        goto end;
      }
      else
      {
        /*
          TODO: We should return trees of the type SEL_ARG::IMPOSSIBLE
          for the cases like int_field > 999999999999999999999999 as well.
        */
        tree= 0;
        if (err == 3 && field->type() == DRIZZLE_TYPE_DATE &&
            (type == Item_func::GT_FUNC || type == Item_func::GE_FUNC ||
             type == Item_func::LT_FUNC || type == Item_func::LE_FUNC) )
        {
          /*
            We were saving DATETIME into a DATE column, the conversion went ok
            but a non-zero time part was cut off.

            In MySQL's SQL dialect, DATE and DATETIME are compared as datetime
            values. Index over a DATE column uses DATE comparison. Changing
            from one comparison to the other is possible:

            datetime(date_col)< '2007-12-10 12:34:55' -> date_col<='2007-12-10'
            datetime(date_col)<='2007-12-10 12:34:55' -> date_col<='2007-12-10'

            datetime(date_col)> '2007-12-10 12:34:55' -> date_col>='2007-12-10'
            datetime(date_col)>='2007-12-10 12:34:55' -> date_col>='2007-12-10'

            but we'll need to convert '>' to '>=' and '<' to '<='. This will
            be done together with other types at the end of this function
            (grep for field_is_equal_to_item)
          */
        }
        else
          goto end;
      }
    }

    /*
      guaranteed at this point:  err > 0; field and const of same type
      If an integer got bounded (e.g. to within 0..255 / -128..127)
      for < or >, set flags as for <= or >= (no NEAR_MAX / NEAR_MIN)
    */
    else if (err == 1 && field->result_type() == INT_RESULT)
    {
      if (type == Item_func::LT_FUNC && (value->val_int() > 0))
        type = Item_func::LE_FUNC;
      else if (type == Item_func::GT_FUNC &&
               !((Field_num*)field)->unsigned_flag &&
               !((Item_int*)value)->unsigned_flag &&
               (value->val_int() < 0))
        type = Item_func::GE_FUNC;
    }
  }
  else if (err < 0)
  {
    /* This happens when we try to insert a NULL field in a not null column */
    tree= &null_element;                        // cmp with NULL is never true
    goto end;
  }
  str= (unsigned char*) alloc_root(alloc, key_part->store_length+1);
  if (!str)
    goto end;
  if (maybe_null)
    *str= (unsigned char) field->is_real_null();        // Set to 1 if null
  field->get_key_image(str+maybe_null, key_part->length);
  if (!(tree= new (alloc) SEL_ARG(field, str, str)))
    goto end;                                   // out of memory

  /*
    Check if we are comparing an UNSIGNED integer with a negative constant.
    In this case we know that:
    (a) (unsigned_int [< | <=] negative_constant) == false
    (b) (unsigned_int [> | >=] negative_constant) == true
    In case (a) the condition is false for all values, and in case (b) it
    is true for all values, so we can avoid unnecessary retrieval and condition
    testing, and we also get correct comparison of unsinged integers with
    negative integers (which otherwise fails because at query execution time
    negative integers are cast to unsigned if compared with unsigned).
   */
  if (field->result_type() == INT_RESULT &&
      value->result_type() == INT_RESULT &&
      ((Field_num*)field)->unsigned_flag && !((Item_int*)value)->unsigned_flag)
  {
    int64_t item_val= value->val_int();
    if (item_val < 0)
    {
      if (type == Item_func::LT_FUNC || type == Item_func::LE_FUNC)
      {
        tree->type= SEL_ARG::IMPOSSIBLE;
        goto end;
      }
      if (type == Item_func::GT_FUNC || type == Item_func::GE_FUNC)
      {
        tree= 0;
        goto end;
      }
    }
  }

  switch (type) {
  case Item_func::LT_FUNC:
    if (field_is_equal_to_item(field,value))
      tree->max_flag=NEAR_MAX;
    /* fall through */
  case Item_func::LE_FUNC:
    if (!maybe_null)
      tree->min_flag=NO_MIN_RANGE;		/* From start */
    else
    {						// > NULL
      tree->min_value=is_null_string;
      tree->min_flag=NEAR_MIN;
    }
    break;
  case Item_func::GT_FUNC:
    /* Don't use open ranges for partial key_segments */
    if (field_is_equal_to_item(field,value) &&
        !(key_part->flag & HA_PART_KEY_SEG))
      tree->min_flag=NEAR_MIN;
    /* fall through */
  case Item_func::GE_FUNC:
    tree->max_flag=NO_MAX_RANGE;
    break;
  default:
    break;
  }

end:
  param->session->mem_root= alloc;
  return(tree);
}


/******************************************************************************
** Tree manipulation functions
** If tree is 0 it means that the condition can't be tested. It refers
** to a non existent table or to a field in current table with isn't a key.
** The different tree flags:
** IMPOSSIBLE:	 Condition is never true
** ALWAYS:	 Condition is always true
** MAYBE:	 Condition may exists when tables are read
** MAYBE_KEY:	 Condition refers to a key that may be used in join loop
** KEY_RANGE:	 Condition uses a key
******************************************************************************/

/*
  Add a new key test to a key when scanning through all keys
  This will never be called for same key parts.
*/

static SEL_ARG *
sel_add(SEL_ARG *key1,SEL_ARG *key2)
{
  SEL_ARG *root,**key_link;

  if (!key1)
    return key2;
  if (!key2)
    return key1;

  key_link= &root;
  while (key1 && key2)
  {
    if (key1->part < key2->part)
    {
      *key_link= key1;
      key_link= &key1->next_key_part;
      key1=key1->next_key_part;
    }
    else
    {
      *key_link= key2;
      key_link= &key2->next_key_part;
      key2=key2->next_key_part;
    }
  }
  *key_link=key1 ? key1 : key2;
  return root;
}

#define CLONE_KEY1_MAYBE 1
#define CLONE_KEY2_MAYBE 2
#define swap_clone_flag(A) ((A & 1) << 1) | ((A & 2) >> 1)


static SEL_TREE *
tree_and(RANGE_OPT_PARAM *param,SEL_TREE *tree1,SEL_TREE *tree2)
{
  if (!tree1)
    return(tree2);
  if (!tree2)
    return(tree1);
  if (tree1->type == SEL_TREE::IMPOSSIBLE || tree2->type == SEL_TREE::ALWAYS)
    return(tree1);
  if (tree2->type == SEL_TREE::IMPOSSIBLE || tree1->type == SEL_TREE::ALWAYS)
    return(tree2);
  if (tree1->type == SEL_TREE::MAYBE)
  {
    if (tree2->type == SEL_TREE::KEY)
      tree2->type=SEL_TREE::KEY_SMALLER;
    return(tree2);
  }
  if (tree2->type == SEL_TREE::MAYBE)
  {
    tree1->type=SEL_TREE::KEY_SMALLER;
    return(tree1);
  }
  key_map  result_keys;
  result_keys.reset();

  /* Join the trees key per key */
  SEL_ARG **key1,**key2,**end;
  for (key1= tree1->keys,key2= tree2->keys,end=key1+param->keys ;
       key1 != end ; key1++,key2++)
  {
    uint32_t flag=0;
    if (*key1 || *key2)
    {
      if (*key1 && !(*key1)->simple_key())
	flag|=CLONE_KEY1_MAYBE;
      if (*key2 && !(*key2)->simple_key())
	flag|=CLONE_KEY2_MAYBE;
      *key1=key_and(param, *key1, *key2, flag);
      if (*key1 && (*key1)->type == SEL_ARG::IMPOSSIBLE)
      {
	tree1->type= SEL_TREE::IMPOSSIBLE;
        return(tree1);
      }
      result_keys.set(key1 - tree1->keys);
#ifdef EXTRA_DEBUG
        if (*key1 && param->alloced_sel_args < SEL_ARG::MAX_SEL_ARGS)
          (*key1)->test_use_count(*key1);
#endif
    }
  }
  tree1->keys_map= result_keys;
  /* dispose index_merge if there is a "range" option */
  if (result_keys.any())
  {
    tree1->merges.clear();
    return(tree1);
  }

  /* ok, both trees are index_merge trees */
  imerge_list_and_list(tree1->merges, tree2->merges);
  return(tree1);
}


/*
  Check if two SEL_TREES can be combined into one (i.e. a single key range
  read can be constructed for "cond_of_tree1 OR cond_of_tree2" ) without
  using index_merge.
*/

bool sel_trees_can_be_ored(SEL_TREE *tree1, SEL_TREE *tree2,
                           RANGE_OPT_PARAM* param)
{
  key_map common_keys= tree1->keys_map;
  common_keys&= tree2->keys_map;

  if (common_keys.none())
    return false;

  /* trees have a common key, check if they refer to same key part */
  SEL_ARG **key1,**key2;
  for (uint32_t key_no=0; key_no < param->keys; key_no++)
  {
    if (common_keys.test(key_no))
    {
      key1= tree1->keys + key_no;
      key2= tree2->keys + key_no;
      if ((*key1)->part == (*key2)->part)
      {
        return true;
      }
    }
  }
  return false;
}


/*
  Remove the trees that are not suitable for record retrieval.
  SYNOPSIS
    param  Range analysis parameter
    tree   Tree to be processed, tree->type is KEY or KEY_SMALLER

  DESCRIPTION
    This function walks through tree->keys[] and removes the SEL_ARG* trees
    that are not "maybe" trees (*) and cannot be used to construct quick range
    selects.
    (*) - have type MAYBE or MAYBE_KEY. Perhaps we should remove trees of
          these types here as well.

    A SEL_ARG* tree cannot be used to construct quick select if it has
    tree->part != 0. (e.g. it could represent "keypart2 < const").

    WHY THIS FUNCTION IS NEEDED

    Normally we allow construction of SEL_TREE objects that have SEL_ARG
    trees that do not allow quick range select construction. For example for
    " keypart1=1 AND keypart2=2 " the execution will proceed as follows:
    tree1= SEL_TREE { SEL_ARG{keypart1=1} }
    tree2= SEL_TREE { SEL_ARG{keypart2=2} } -- can't make quick range select
                                               from this
    call tree_and(tree1, tree2) -- this joins SEL_ARGs into a usable SEL_ARG
                                   tree.

    There is an exception though: when we construct index_merge SEL_TREE,
    any SEL_ARG* tree that cannot be used to construct quick range select can
    be removed, because current range analysis code doesn't provide any way
    that tree could be later combined with another tree.
    Consider an example: we should not construct
    st1 = SEL_TREE {
      merges = SEL_IMERGE {
                            SEL_TREE(t.key1part1 = 1),
                            SEL_TREE(t.key2part2 = 2)   -- (*)
                          }
                   };
    because
     - (*) cannot be used to construct quick range select,
     - There is no execution path that would cause (*) to be converted to
       a tree that could be used.

    The latter is easy to verify: first, notice that the only way to convert
    (*) into a usable tree is to call tree_and(something, (*)).

    Second look at what tree_and/tree_or function would do when passed a
    SEL_TREE that has the structure like st1 tree has, and conlcude that
    tree_and(something, (*)) will not be called.

  RETURN
    0  Ok, some suitable trees left
    1  No tree->keys[] left.
*/

static bool remove_nonrange_trees(RANGE_OPT_PARAM *param, SEL_TREE *tree)
{
  bool res= false;
  for (uint32_t i=0; i < param->keys; i++)
  {
    if (tree->keys[i])
    {
      if (tree->keys[i]->part)
      {
        tree->keys[i]= NULL;
        tree->keys_map.reset(i);
      }
      else
        res= true;
    }
  }
  return !res;
}


static SEL_TREE *
tree_or(RANGE_OPT_PARAM *param,SEL_TREE *tree1,SEL_TREE *tree2)
{
  if (!tree1 || !tree2)
    return 0;
  if (tree1->type == SEL_TREE::IMPOSSIBLE || tree2->type == SEL_TREE::ALWAYS)
    return(tree2);
  if (tree2->type == SEL_TREE::IMPOSSIBLE || tree1->type == SEL_TREE::ALWAYS)
    return(tree1);
  if (tree1->type == SEL_TREE::MAYBE)
    return(tree1);				// Can't use this
  if (tree2->type == SEL_TREE::MAYBE)
    return(tree2);

  SEL_TREE *result= 0;
  key_map  result_keys;
  result_keys.reset();
  if (sel_trees_can_be_ored(tree1, tree2, param))
  {
    /* Join the trees key per key */
    SEL_ARG **key1,**key2,**end;
    for (key1= tree1->keys,key2= tree2->keys,end= key1+param->keys ;
         key1 != end ; key1++,key2++)
    {
      *key1=key_or(param, *key1, *key2);
      if (*key1)
      {
        result=tree1;				// Added to tree1
        result_keys.set(key1 - tree1->keys);
#ifdef EXTRA_DEBUG
        if (param->alloced_sel_args < SEL_ARG::MAX_SEL_ARGS)
          (*key1)->test_use_count(*key1);
#endif
      }
    }
    if (result)
      result->keys_map= result_keys;
  }
  else
  {
    /* ok, two trees have KEY type but cannot be used without index merge */
    if ((tree1->merges.empty() == true) && (tree2->merges.empty() == true))
    {
      if (param->remove_jump_scans)
      {
        bool no_trees= remove_nonrange_trees(param, tree1);
        no_trees= no_trees || remove_nonrange_trees(param, tree2);
        if (no_trees)
          return(new SEL_TREE(SEL_TREE::ALWAYS));
      }

      /* both trees are "range" trees, produce new index merge structure. */
      result= new SEL_TREE();
      SEL_IMERGE *merge= new SEL_IMERGE();
      result->merges.push_back(merge);

      if( merge->or_sel_tree(param, tree1) || merge->or_sel_tree(param, tree2))
        result= NULL;
      else
        result->type= tree1->type;
    }
    else if ((tree1->merges.empty() == false) && (tree2->merges.empty() == false))
    {
      if (imerge_list_or_list(param, tree1->merges, tree2->merges))
        result= new SEL_TREE(SEL_TREE::ALWAYS);
      else
        result= tree1;
    }
    else
    {
      /* one tree is index merge tree and another is range tree */
      if (tree1->merges.empty() == true)
        std::swap(tree1, tree2);

      if (param->remove_jump_scans && remove_nonrange_trees(param, tree2))
         return(new SEL_TREE(SEL_TREE::ALWAYS));

      /* add tree2 to tree1->merges, checking if it collapses to ALWAYS */
      if (imerge_list_or_tree(param, tree1->merges, tree2))
        result= new SEL_TREE(SEL_TREE::ALWAYS);
      else
        result= tree1;
    }
  }
  return result;
}


/* And key trees where key1->part < key2 -> part */

static SEL_ARG *
and_all_keys(RANGE_OPT_PARAM *param, SEL_ARG *key1, SEL_ARG *key2,
             uint32_t clone_flag)
{
  SEL_ARG *next;
  ulong use_count=key1->use_count;

  if (key1->elements != 1)
  {
    key2->use_count+=key1->elements-1; //psergey: why we don't count that key1 has n-k-p?
    key2->increment_use_count((int) key1->elements-1);
  }
  if (key1->type == SEL_ARG::MAYBE_KEY)
  {
    key1->right= key1->left= &null_element;
    key1->next= key1->prev= 0;
  }
  for (next=key1->first(); next ; next=next->next)
  {
    if (next->next_key_part)
    {
      SEL_ARG *tmp= key_and(param, next->next_key_part, key2, clone_flag);
      if (tmp && tmp->type == SEL_ARG::IMPOSSIBLE)
      {
	key1=key1->tree_delete(next);
	continue;
      }
      next->next_key_part=tmp;
      if (use_count)
	next->increment_use_count(use_count);
      if (param->alloced_sel_args > SEL_ARG::MAX_SEL_ARGS)
        break;
    }
    else
      next->next_key_part=key2;
  }
  if (!key1)
    return &null_element;			// Impossible ranges
  key1->use_count++;
  return key1;
}


/*
  Produce a SEL_ARG graph that represents "key1 AND key2"

  SYNOPSIS
    key_and()
      param   Range analysis context (needed to track if we have allocated
              too many SEL_ARGs)
      key1    First argument, root of its RB-tree
      key2    Second argument, root of its RB-tree

  RETURN
    RB-tree root of the resulting SEL_ARG graph.
    NULL if the result of AND operation is an empty interval {0}.
*/

static SEL_ARG *
key_and(RANGE_OPT_PARAM *param, SEL_ARG *key1, SEL_ARG *key2, uint32_t clone_flag)
{
  if (!key1)
    return key2;
  if (!key2)
    return key1;
  if (key1->part != key2->part)
  {
    if (key1->part > key2->part)
    {
      std::swap(key1, key2);
      clone_flag=swap_clone_flag(clone_flag);
    }
    // key1->part < key2->part
    key1->use_count--;
    if (key1->use_count > 0)
      if (!(key1= key1->clone_tree(param)))
	return 0;				// OOM
    return and_all_keys(param, key1, key2, clone_flag);
  }

  if (((clone_flag & CLONE_KEY2_MAYBE) &&
       !(clone_flag & CLONE_KEY1_MAYBE) &&
       key2->type != SEL_ARG::MAYBE_KEY) ||
      key1->type == SEL_ARG::MAYBE_KEY)
  {						// Put simple key in key2
    std::swap(key1, key2);
    clone_flag=swap_clone_flag(clone_flag);
  }

  /* If one of the key is MAYBE_KEY then the found region may be smaller */
  if (key2->type == SEL_ARG::MAYBE_KEY)
  {
    if (key1->use_count > 1)
    {
      key1->use_count--;
      if (!(key1=key1->clone_tree(param)))
	return 0;				// OOM
      key1->use_count++;
    }
    if (key1->type == SEL_ARG::MAYBE_KEY)
    {						// Both are maybe key
      key1->next_key_part=key_and(param, key1->next_key_part,
                                  key2->next_key_part, clone_flag);
      if (key1->next_key_part &&
	  key1->next_key_part->type == SEL_ARG::IMPOSSIBLE)
	return key1;
    }
    else
    {
      key1->maybe_smaller();
      if (key2->next_key_part)
      {
	key1->use_count--;			// Incremented in and_all_keys
	return and_all_keys(param, key1, key2, clone_flag);
      }
      key2->use_count--;			// Key2 doesn't have a tree
    }
    return key1;
  }

  key1->use_count--;
  key2->use_count--;
  SEL_ARG *e1=key1->first(), *e2=key2->first(), *new_tree=0;

  while (e1 && e2)
  {
    int cmp=e1->cmp_min_to_min(e2);
    if (cmp < 0)
    {
      if (get_range(&e1,&e2,key1))
	continue;
    }
    else if (get_range(&e2,&e1,key2))
      continue;
    SEL_ARG *next=key_and(param, e1->next_key_part, e2->next_key_part,
                          clone_flag);
    e1->increment_use_count(1);
    e2->increment_use_count(1);
    if (!next || next->type != SEL_ARG::IMPOSSIBLE)
    {
      SEL_ARG *new_arg= e1->clone_and(e2);
      if (!new_arg)
	return &null_element;			// End of memory
      new_arg->next_key_part=next;
      if (!new_tree)
      {
	new_tree=new_arg;
      }
      else
	new_tree=new_tree->insert(new_arg);
    }
    if (e1->cmp_max_to_max(e2) < 0)
      e1=e1->next;				// e1 can't overlapp next e2
    else
      e2=e2->next;
  }
  key1->free_tree();
  key2->free_tree();
  if (!new_tree)
    return &null_element;			// Impossible range
  return new_tree;
}


static bool
get_range(SEL_ARG **e1,SEL_ARG **e2,SEL_ARG *root1)
{
  (*e1)=root1->find_range(*e2);			// first e1->min < e2->min
  if ((*e1)->cmp_max_to_min(*e2) < 0)
  {
    if (!((*e1)=(*e1)->next))
      return 1;
    if ((*e1)->cmp_min_to_max(*e2) > 0)
    {
      (*e2)=(*e2)->next;
      return 1;
    }
  }
  return 0;
}


static SEL_ARG *
key_or(RANGE_OPT_PARAM *param, SEL_ARG *key1,SEL_ARG *key2)
{
  if (!key1)
  {
    if (key2)
    {
      key2->use_count--;
      key2->free_tree();
    }
    return 0;
  }
  if (!key2)
  {
    key1->use_count--;
    key1->free_tree();
    return 0;
  }
  key1->use_count--;
  key2->use_count--;

  if (key1->part != key2->part)
  {
    key1->free_tree();
    key2->free_tree();
    return 0;					// Can't optimize this
  }

  // If one of the key is MAYBE_KEY then the found region may be bigger
  if (key1->type == SEL_ARG::MAYBE_KEY)
  {
    key2->free_tree();
    key1->use_count++;
    return key1;
  }
  if (key2->type == SEL_ARG::MAYBE_KEY)
  {
    key1->free_tree();
    key2->use_count++;
    return key2;
  }

  if (key1->use_count > 0)
  {
    if (key2->use_count == 0 || key1->elements > key2->elements)
    {
      std::swap(key1,key2);
    }
    if (key1->use_count > 0 || !(key1=key1->clone_tree(param)))
      return 0;					// OOM
  }

  // Add tree at key2 to tree at key1
  bool key2_shared=key2->use_count != 0;
  key1->maybe_flag|=key2->maybe_flag;

  for (key2=key2->first(); key2; )
  {
    SEL_ARG *tmp=key1->find_range(key2);	// Find key1.min <= key2.min
    int cmp;

    if (!tmp)
    {
      tmp=key1->first();			// tmp.min > key2.min
      cmp= -1;
    }
    else if ((cmp=tmp->cmp_max_to_min(key2)) < 0)
    {						// Found tmp.max < key2.min
      SEL_ARG *next=tmp->next;
      if (cmp == -2 && eq_tree(tmp->next_key_part,key2->next_key_part))
      {
	// Join near ranges like tmp.max < 0 and key2.min >= 0
	SEL_ARG *key2_next=key2->next;
	if (key2_shared)
	{
	  if (!(key2=new SEL_ARG(*key2)))
	    return 0;		// out of memory
	  key2->increment_use_count(key1->use_count+1);
	  key2->next=key2_next;			// New copy of key2
	}
	key2->copy_min(tmp);
	if (!(key1=key1->tree_delete(tmp)))
	{					// Only one key in tree
	  key1=key2;
	  key1->make_root();
	  key2=key2_next;
	  break;
	}
      }
      if (!(tmp=next))				// tmp.min > key2.min
	break;					// Copy rest of key2
    }
    if (cmp < 0)
    {						// tmp.min > key2.min
      int tmp_cmp;
      if ((tmp_cmp=tmp->cmp_min_to_max(key2)) > 0) // if tmp.min > key2.max
      {
	if (tmp_cmp == 2 && eq_tree(tmp->next_key_part,key2->next_key_part))
	{					// ranges are connected
	  tmp->copy_min_to_min(key2);
	  key1->merge_flags(key2);
	  if (tmp->min_flag & NO_MIN_RANGE &&
	      tmp->max_flag & NO_MAX_RANGE)
	  {
	    if (key1->maybe_flag)
	      return new SEL_ARG(SEL_ARG::MAYBE_KEY);
	    return 0;
	  }
	  key2->increment_use_count(-1);	// Free not used tree
	  key2=key2->next;
	  continue;
	}
	else
	{
	  SEL_ARG *next=key2->next;		// Keys are not overlapping
	  if (key2_shared)
	  {
	    SEL_ARG *cpy= new SEL_ARG(*key2);	// Must make copy
	    if (!cpy)
	      return 0;				// OOM
	    key1=key1->insert(cpy);
	    key2->increment_use_count(key1->use_count+1);
	  }
	  else
	    key1=key1->insert(key2);		// Will destroy key2_root
	  key2=next;
	  continue;
	}
      }
    }

    // tmp.max >= key2.min && tmp.min <= key.cmax(overlapping ranges)
    if (eq_tree(tmp->next_key_part,key2->next_key_part))
    {
      if (tmp->is_same(key2))
      {
	tmp->merge_flags(key2);			// Copy maybe flags
	key2->increment_use_count(-1);		// Free not used tree
      }
      else
      {
	SEL_ARG *last=tmp;
	while (last->next && last->next->cmp_min_to_max(key2) <= 0 &&
	       eq_tree(last->next->next_key_part,key2->next_key_part))
	{
	  SEL_ARG *save=last;
	  last=last->next;
	  key1=key1->tree_delete(save);
	}
        last->copy_min(tmp);
	if (last->copy_min(key2) || last->copy_max(key2))
	{					// Full range
	  key1->free_tree();
	  for (; key2 ; key2=key2->next)
	    key2->increment_use_count(-1);	// Free not used tree
	  if (key1->maybe_flag)
	    return new SEL_ARG(SEL_ARG::MAYBE_KEY);
	  return 0;
	}
      }
      key2=key2->next;
      continue;
    }

    if (cmp >= 0 && tmp->cmp_min_to_min(key2) < 0)
    {						// tmp.min <= x < key2.min
      SEL_ARG *new_arg=tmp->clone_first(key2);
      if (!new_arg)
	return 0;				// OOM
      if ((new_arg->next_key_part= key1->next_key_part))
	new_arg->increment_use_count(key1->use_count+1);
      tmp->copy_min_to_min(key2);
      key1=key1->insert(new_arg);
    }

    // tmp.min >= key2.min && tmp.min <= key2.max
    SEL_ARG key(*key2);				// Get copy we can modify
    for (;;)
    {
      if (tmp->cmp_min_to_min(&key) > 0)
      {						// key.min <= x < tmp.min
	SEL_ARG *new_arg=key.clone_first(tmp);
	if (!new_arg)
	  return 0;				// OOM
	if ((new_arg->next_key_part=key.next_key_part))
	  new_arg->increment_use_count(key1->use_count+1);
	key1=key1->insert(new_arg);
      }
      if ((cmp=tmp->cmp_max_to_max(&key)) <= 0)
      {						// tmp.min. <= x <= tmp.max
	tmp->maybe_flag|= key.maybe_flag;
	key.increment_use_count(key1->use_count+1);
	tmp->next_key_part= key_or(param, tmp->next_key_part, key.next_key_part);
	if (!cmp)				// Key2 is ready
	  break;
	key.copy_max_to_min(tmp);
	if (!(tmp=tmp->next))
	{
	  SEL_ARG *tmp2= new SEL_ARG(key);
	  if (!tmp2)
	    return 0;				// OOM
	  key1=key1->insert(tmp2);
	  key2=key2->next;
	  goto end;
	}
	if (tmp->cmp_min_to_max(&key) > 0)
	{
	  SEL_ARG *tmp2= new SEL_ARG(key);
	  if (!tmp2)
	    return 0;				// OOM
	  key1=key1->insert(tmp2);
	  break;
	}
      }
      else
      {
	SEL_ARG *new_arg=tmp->clone_last(&key); // tmp.min <= x <= key.max
	if (!new_arg)
	  return 0;				// OOM
	tmp->copy_max_to_min(&key);
	tmp->increment_use_count(key1->use_count+1);
	/* Increment key count as it may be used for next loop */
	key.increment_use_count(1);
	new_arg->next_key_part= key_or(param, tmp->next_key_part, key.next_key_part);
	key1=key1->insert(new_arg);
	break;
      }
    }
    key2=key2->next;
  }

end:
  while (key2)
  {
    SEL_ARG *next=key2->next;
    if (key2_shared)
    {
      SEL_ARG *tmp=new SEL_ARG(*key2);		// Must make copy
      if (!tmp)
	return 0;
      key2->increment_use_count(key1->use_count+1);
      key1=key1->insert(tmp);
    }
    else
      key1=key1->insert(key2);			// Will destroy key2_root
    key2=next;
  }
  key1->use_count++;
  return key1;
}


/* Compare if two trees are equal */

static bool eq_tree(SEL_ARG* a,SEL_ARG *b)
{
  if (a == b)
    return 1;
  if (!a || !b || !a->is_same(b))
    return 0;
  if (a->left != &null_element && b->left != &null_element)
  {
    if (!eq_tree(a->left,b->left))
      return 0;
  }
  else if (a->left != &null_element || b->left != &null_element)
    return 0;
  if (a->right != &null_element && b->right != &null_element)
  {
    if (!eq_tree(a->right,b->right))
      return 0;
  }
  else if (a->right != &null_element || b->right != &null_element)
    return 0;
  if (a->next_key_part != b->next_key_part)
  {						// Sub range
    if (!a->next_key_part != !b->next_key_part ||
	!eq_tree(a->next_key_part, b->next_key_part))
      return 0;
  }
  return 1;
}


SEL_ARG *
SEL_ARG::insert(SEL_ARG *key)
{
  SEL_ARG *element, **par= NULL, *last_element= NULL;

  for (element= this; element != &null_element ; )
  {
    last_element=element;
    if (key->cmp_min_to_min(element) > 0)
    {
      par= &element->right; element= element->right;
    }
    else
    {
      par = &element->left; element= element->left;
    }
  }
  *par=key;
  key->parent=last_element;
	/* Link in list */
  if (par == &last_element->left)
  {
    key->next=last_element;
    if ((key->prev=last_element->prev))
      key->prev->next=key;
    last_element->prev=key;
  }
  else
  {
    if ((key->next=last_element->next))
      key->next->prev=key;
    key->prev=last_element;
    last_element->next=key;
  }
  key->left=key->right= &null_element;
  SEL_ARG *root=rb_insert(key);			// rebalance tree
  root->use_count=this->use_count;		// copy root info
  root->elements= this->elements+1;
  root->maybe_flag=this->maybe_flag;
  return root;
}


/*
** Find best key with min <= given key
** Because the call context this should never return 0 to get_range
*/

SEL_ARG *
SEL_ARG::find_range(SEL_ARG *key)
{
  SEL_ARG *element=this,*found=0;

  for (;;)
  {
    if (element == &null_element)
      return found;
    int cmp=element->cmp_min_to_min(key);
    if (cmp == 0)
      return element;
    if (cmp < 0)
    {
      found=element;
      element=element->right;
    }
    else
      element=element->left;
  }
}


/*
  Remove a element from the tree

  SYNOPSIS
    tree_delete()
    key		Key that is to be deleted from tree (this)

  NOTE
    This also frees all sub trees that is used by the element

  RETURN
    root of new tree (with key deleted)
*/

SEL_ARG *
SEL_ARG::tree_delete(SEL_ARG *key)
{
  enum leaf_color remove_color;
  SEL_ARG *root,*nod,**par,*fix_par;

  root=this;
  this->parent= 0;

  /* Unlink from list */
  if (key->prev)
    key->prev->next=key->next;
  if (key->next)
    key->next->prev=key->prev;
  key->increment_use_count(-1);
  if (!key->parent)
    par= &root;
  else
    par=key->parent_ptr();

  if (key->left == &null_element)
  {
    *par=nod=key->right;
    fix_par=key->parent;
    if (nod != &null_element)
      nod->parent=fix_par;
    remove_color= key->color;
  }
  else if (key->right == &null_element)
  {
    *par= nod=key->left;
    nod->parent=fix_par=key->parent;
    remove_color= key->color;
  }
  else
  {
    SEL_ARG *tmp=key->next;			// next bigger key (exist!)
    nod= *tmp->parent_ptr()= tmp->right;	// unlink tmp from tree
    fix_par=tmp->parent;
    if (nod != &null_element)
      nod->parent=fix_par;
    remove_color= tmp->color;

    tmp->parent=key->parent;			// Move node in place of key
    (tmp->left=key->left)->parent=tmp;
    if ((tmp->right=key->right) != &null_element)
      tmp->right->parent=tmp;
    tmp->color=key->color;
    *par=tmp;
    if (fix_par == key)				// key->right == key->next
      fix_par=tmp;				// new parent of nod
  }

  if (root == &null_element)
    return 0;				// Maybe root later
  if (remove_color == BLACK)
    root=rb_delete_fixup(root,nod,fix_par);
#ifdef EXTRA_DEBUG
  test_rb_tree(root,root->parent);
#endif /* EXTRA_DEBUG */

  root->use_count=this->use_count;		// Fix root counters
  root->elements=this->elements-1;
  root->maybe_flag=this->maybe_flag;
  return(root);
}


	/* Functions to fix up the tree after insert and delete */

static void left_rotate(SEL_ARG **root,SEL_ARG *leaf)
{
  SEL_ARG *y=leaf->right;
  leaf->right=y->left;
  if (y->left != &null_element)
    y->left->parent=leaf;
  if (!(y->parent=leaf->parent))
    *root=y;
  else
    *leaf->parent_ptr()=y;
  y->left=leaf;
  leaf->parent=y;
}

static void right_rotate(SEL_ARG **root,SEL_ARG *leaf)
{
  SEL_ARG *y=leaf->left;
  leaf->left=y->right;
  if (y->right != &null_element)
    y->right->parent=leaf;
  if (!(y->parent=leaf->parent))
    *root=y;
  else
    *leaf->parent_ptr()=y;
  y->right=leaf;
  leaf->parent=y;
}


SEL_ARG *
SEL_ARG::rb_insert(SEL_ARG *leaf)
{
  SEL_ARG *y,*par,*par2,*root;
  root= this; root->parent= 0;

  leaf->color=RED;
  while (leaf != root && (par= leaf->parent)->color == RED)
  {					// This can't be root or 1 level under
    if (par == (par2= leaf->parent->parent)->left)
    {
      y= par2->right;
      if (y->color == RED)
      {
	par->color=BLACK;
	y->color=BLACK;
	leaf=par2;
	leaf->color=RED;		/* And the loop continues */
      }
      else
      {
	if (leaf == par->right)
	{
	  left_rotate(&root,leaf->parent);
	  par=leaf;			/* leaf is now parent to old leaf */
	}
	par->color=BLACK;
	par2->color=RED;
	right_rotate(&root,par2);
	break;
      }
    }
    else
    {
      y= par2->left;
      if (y->color == RED)
      {
	par->color=BLACK;
	y->color=BLACK;
	leaf=par2;
	leaf->color=RED;		/* And the loop continues */
      }
      else
      {
	if (leaf == par->left)
	{
	  right_rotate(&root,par);
	  par=leaf;
	}
	par->color=BLACK;
	par2->color=RED;
	left_rotate(&root,par2);
	break;
      }
    }
  }
  root->color=BLACK;
#ifdef EXTRA_DEBUG
  test_rb_tree(root,root->parent);
#endif /* EXTRA_DEBUG */

  return root;
}


SEL_ARG *rb_delete_fixup(SEL_ARG *root,SEL_ARG *key,SEL_ARG *par)
{
  SEL_ARG *x,*w;
  root->parent=0;

  x= key;
  while (x != root && x->color == SEL_ARG::BLACK)
  {
    if (x == par->left)
    {
      w=par->right;
      if (w->color == SEL_ARG::RED)
      {
	w->color=SEL_ARG::BLACK;
	par->color=SEL_ARG::RED;
	left_rotate(&root,par);
	w=par->right;
      }
      if (w->left->color == SEL_ARG::BLACK && w->right->color == SEL_ARG::BLACK)
      {
	w->color=SEL_ARG::RED;
	x=par;
      }
      else
      {
	if (w->right->color == SEL_ARG::BLACK)
	{
	  w->left->color=SEL_ARG::BLACK;
	  w->color=SEL_ARG::RED;
	  right_rotate(&root,w);
	  w=par->right;
	}
	w->color=par->color;
	par->color=SEL_ARG::BLACK;
	w->right->color=SEL_ARG::BLACK;
	left_rotate(&root,par);
	x=root;
	break;
      }
    }
    else
    {
      w=par->left;
      if (w->color == SEL_ARG::RED)
      {
	w->color=SEL_ARG::BLACK;
	par->color=SEL_ARG::RED;
	right_rotate(&root,par);
	w=par->left;
      }
      if (w->right->color == SEL_ARG::BLACK && w->left->color == SEL_ARG::BLACK)
      {
	w->color=SEL_ARG::RED;
	x=par;
      }
      else
      {
	if (w->left->color == SEL_ARG::BLACK)
	{
	  w->right->color=SEL_ARG::BLACK;
	  w->color=SEL_ARG::RED;
	  left_rotate(&root,w);
	  w=par->left;
	}
	w->color=par->color;
	par->color=SEL_ARG::BLACK;
	w->left->color=SEL_ARG::BLACK;
	right_rotate(&root,par);
	x=root;
	break;
      }
    }
    par=x->parent;
  }
  x->color=SEL_ARG::BLACK;
  return root;
}


	/* Test that the properties for a red-black tree hold */

#ifdef EXTRA_DEBUG
int test_rb_tree(SEL_ARG *element,SEL_ARG *parent)
{
  int count_l,count_r;

  if (element == &null_element)
    return 0;					// Found end of tree
  if (element->parent != parent)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, "Wrong tree: Parent doesn't point at parent");
    return -1;
  }
  if (element->color == SEL_ARG::RED &&
      (element->left->color == SEL_ARG::RED ||
       element->right->color == SEL_ARG::RED))
  {
    errmsg_printf(ERRMSG_LVL_ERROR, "Wrong tree: Found two red in a row");
    return -1;
  }
  if (element->left == element->right && element->left != &null_element)
  {						// Dummy test
    errmsg_printf(ERRMSG_LVL_ERROR, "Wrong tree: Found right == left");
    return -1;
  }
  count_l=test_rb_tree(element->left,element);
  count_r=test_rb_tree(element->right,element);
  if (count_l >= 0 && count_r >= 0)
  {
    if (count_l == count_r)
      return count_l+(element->color == SEL_ARG::BLACK);
    errmsg_printf(ERRMSG_LVL_ERROR, "Wrong tree: Incorrect black-count: %d - %d",
	    count_l,count_r);
  }
  return -1;					// Error, no more warnings
}


/*
  Count how many times SEL_ARG graph "root" refers to its part "key"

  SYNOPSIS
    count_key_part_usage()
      root  An RB-Root node in a SEL_ARG graph.
      key   Another RB-Root node in that SEL_ARG graph.

  DESCRIPTION
    The passed "root" node may refer to "key" node via root->next_key_part,
    root->next->n

    This function counts how many times the node "key" is referred (via
    SEL_ARG::next_key_part) by
     - intervals of RB-tree pointed by "root",
     - intervals of RB-trees that are pointed by SEL_ARG::next_key_part from
       intervals of RB-tree pointed by "root",
     - and so on.

    Here is an example (horizontal links represent next_key_part pointers,
    vertical links - next/prev prev pointers):

         +----+               $
         |root|-----------------+
         +----+               $ |
           |                  $ |
           |                  $ |
         +----+       +---+   $ |     +---+    Here the return value
         |    |- ... -|   |---$-+--+->|key|    will be 4.
         +----+       +---+   $ |  |  +---+
           |                  $ |  |
          ...                 $ |  |
           |                  $ |  |
         +----+   +---+       $ |  |
         |    |---|   |---------+  |
         +----+   +---+       $    |
           |        |         $    |
          ...     +---+       $    |
                  |   |------------+
                  +---+       $
  RETURN
    Number of links to "key" from nodes reachable from "root".
*/

static ulong count_key_part_usage(SEL_ARG *root, SEL_ARG *key)
{
  ulong count= 0;
  for (root=root->first(); root ; root=root->next)
  {
    if (root->next_key_part)
    {
      if (root->next_key_part == key)
	count++;
      if (root->next_key_part->part < key->part)
	count+=count_key_part_usage(root->next_key_part,key);
    }
  }
  return count;
}


/*
  Check if SEL_ARG::use_count value is correct

  SYNOPSIS
    SEL_ARG::test_use_count()
      root  The root node of the SEL_ARG graph (an RB-tree root node that
            has the least value of sel_arg->part in the entire graph, and
            thus is the "origin" of the graph)

  DESCRIPTION
    Check if SEL_ARG::use_count value is correct. See the definition of
    use_count for what is "correct".
*/

void SEL_ARG::test_use_count(SEL_ARG *root)
{
  uint32_t e_count=0;
  if (this == root && use_count != 1)
  {
    errmsg_printf(ERRMSG_LVL_INFO, "Use_count: Wrong count %lu for root",use_count);
    return;
  }
  if (this->type != SEL_ARG::KEY_RANGE)
    return;
  for (SEL_ARG *pos=first(); pos ; pos=pos->next)
  {
    e_count++;
    if (pos->next_key_part)
    {
      ulong count=count_key_part_usage(root,pos->next_key_part);
      if (count > pos->next_key_part->use_count)
      {
        errmsg_printf(ERRMSG_LVL_INFO, "Use_count: Wrong count for key at 0x%lx, %lu "
                              "should be %lu", (long unsigned int)pos,
                              pos->next_key_part->use_count, count);
	return;
      }
      pos->next_key_part->test_use_count(root);
    }
  }
  if (e_count != elements)
    errmsg_printf(ERRMSG_LVL_WARN, "Wrong use count: %u (should be %u) for tree at 0x%lx",
                      e_count, elements, (long unsigned int) this);
}

#endif

/****************************************************************************
  MRR Range Sequence Interface implementation that walks a SEL_ARG* tree.
 ****************************************************************************/

/* MRR range sequence, SEL_ARG* implementation: stack entry */
typedef struct st_range_seq_entry
{
  /*
    Pointers in min and max keys. They point to right-after-end of key
    images. The 0-th entry has these pointing to key tuple start.
  */
  unsigned char *min_key, *max_key;

  /*
    Flags, for {keypart0, keypart1, ... this_keypart} subtuple.
    min_key_flag may have NULL_RANGE set.
  */
  uint32_t min_key_flag, max_key_flag;

  /* Number of key parts */
  uint32_t min_key_parts, max_key_parts;
  SEL_ARG *key_tree;
} RANGE_SEQ_ENTRY;


/*
  MRR range sequence, SEL_ARG* implementation: SEL_ARG graph traversal context
*/
typedef struct st_sel_arg_range_seq
{
  uint32_t keyno;      /* index of used tree in SEL_TREE structure */
  uint32_t real_keyno; /* Number of the index in tables */
  PARAM *param;
  SEL_ARG *start; /* Root node of the traversed SEL_ARG* graph */

  RANGE_SEQ_ENTRY stack[MAX_REF_PARTS];
  int i; /* Index of last used element in the above array */

  bool at_start; /* true <=> The traversal has just started */
} SEL_ARG_RANGE_SEQ;


/*
  Range sequence interface, SEL_ARG* implementation: Initialize the traversal

  SYNOPSIS
    init()
      init_params  SEL_ARG tree traversal context
      n_ranges     [ignored] The number of ranges obtained
      flags        [ignored] HA_MRR_SINGLE_POINT, HA_MRR_FIXED_KEY

  RETURN
    Value of init_param
*/

static range_seq_t sel_arg_range_seq_init(void *init_param, uint32_t, uint32_t)
{
  SEL_ARG_RANGE_SEQ *seq= (SEL_ARG_RANGE_SEQ*)init_param;
  seq->at_start= true;
  seq->stack[0].key_tree= NULL;
  seq->stack[0].min_key= seq->param->min_key;
  seq->stack[0].min_key_flag= 0;
  seq->stack[0].min_key_parts= 0;

  seq->stack[0].max_key= seq->param->max_key;
  seq->stack[0].max_key_flag= 0;
  seq->stack[0].max_key_parts= 0;
  seq->i= 0;
  return init_param;
}


static void step_down_to(SEL_ARG_RANGE_SEQ *arg, SEL_ARG *key_tree)
{
  RANGE_SEQ_ENTRY *cur= &arg->stack[arg->i+1];
  RANGE_SEQ_ENTRY *prev= &arg->stack[arg->i];

  cur->key_tree= key_tree;
  cur->min_key= prev->min_key;
  cur->max_key= prev->max_key;
  cur->min_key_parts= prev->min_key_parts;
  cur->max_key_parts= prev->max_key_parts;

  uint16_t stor_length= arg->param->key[arg->keyno][key_tree->part].store_length;
  cur->min_key_parts += key_tree->store_min(stor_length, &cur->min_key,
                                            prev->min_key_flag);
  cur->max_key_parts += key_tree->store_max(stor_length, &cur->max_key,
                                            prev->max_key_flag);

  cur->min_key_flag= prev->min_key_flag | key_tree->min_flag;
  cur->max_key_flag= prev->max_key_flag | key_tree->max_flag;

  if (key_tree->is_null_interval())
    cur->min_key_flag |= NULL_RANGE;
  (arg->i)++;
}


/*
  Range sequence interface, SEL_ARG* implementation: get the next interval

  SYNOPSIS
    sel_arg_range_seq_next()
      rseq        Value returned from sel_arg_range_seq_init
      range  OUT  Store information about the range here

  DESCRIPTION
    This is "get_next" function for Range sequence interface implementation
    for SEL_ARG* tree.

  IMPLEMENTATION
    The traversal also updates those param members:
      - is_ror_scan
      - range_count
      - max_key_part

  RETURN
    0  Ok
    1  No more ranges in the sequence
*/

//psergey-merge-todo: support check_quick_keys:max_keypart
static uint32_t sel_arg_range_seq_next(range_seq_t rseq, KEY_MULTI_RANGE *range)
{
  SEL_ARG *key_tree;
  SEL_ARG_RANGE_SEQ *seq= (SEL_ARG_RANGE_SEQ*)rseq;
  if (seq->at_start)
  {
    key_tree= seq->start;
    seq->at_start= false;
    goto walk_up_n_right;
  }

  key_tree= seq->stack[seq->i].key_tree;
  /* Ok, we're at some "full tuple" position in the tree */

  /* Step down if we can */
  if (key_tree->next && key_tree->next != &null_element)
  {
    //step down; (update the tuple, we'll step right and stay there)
    seq->i--;
    step_down_to(seq, key_tree->next);
    key_tree= key_tree->next;
    seq->param->is_ror_scan= false;
    goto walk_right_n_up;
  }

  /* Ok, can't step down, walk left until we can step down */
  while (1)
  {
    if (seq->i == 1) // can't step left
      return 1;
    /* Step left */
    seq->i--;
    key_tree= seq->stack[seq->i].key_tree;

    /* Step down if we can */
    if (key_tree->next && key_tree->next != &null_element)
    {
      // Step down; update the tuple
      seq->i--;
      step_down_to(seq, key_tree->next);
      key_tree= key_tree->next;
      break;
    }
  }

  /*
    Ok, we've stepped down from the path to previous tuple.
    Walk right-up while we can
  */
walk_right_n_up:
  while (key_tree->next_key_part && key_tree->next_key_part != &null_element &&
         key_tree->next_key_part->part == key_tree->part + 1 &&
         key_tree->next_key_part->type == SEL_ARG::KEY_RANGE)
  {
    {
      RANGE_SEQ_ENTRY *cur= &seq->stack[seq->i];
      uint32_t min_key_length= cur->min_key - seq->param->min_key;
      uint32_t max_key_length= cur->max_key - seq->param->max_key;
      uint32_t len= cur->min_key - cur[-1].min_key;
      if (!(min_key_length == max_key_length &&
            !memcmp(cur[-1].min_key, cur[-1].max_key, len) &&
            !key_tree->min_flag && !key_tree->max_flag))
      {
        seq->param->is_ror_scan= false;
        if (!key_tree->min_flag)
          cur->min_key_parts +=
            key_tree->next_key_part->store_min_key(seq->param->key[seq->keyno],
                                                   &cur->min_key,
                                                   &cur->min_key_flag);
        if (!key_tree->max_flag)
          cur->max_key_parts +=
            key_tree->next_key_part->store_max_key(seq->param->key[seq->keyno],
                                                   &cur->max_key,
                                                   &cur->max_key_flag);
        break;
      }
    }

    /*
      Ok, current atomic interval is in form "t.field=const" and there is
      next_key_part interval. Step right, and walk up from there.
    */
    key_tree= key_tree->next_key_part;

walk_up_n_right:
    while (key_tree->prev && key_tree->prev != &null_element)
    {
      /* Step up */
      key_tree= key_tree->prev;
    }
    step_down_to(seq, key_tree);
  }

  /* Ok got a tuple */
  RANGE_SEQ_ENTRY *cur= &seq->stack[seq->i];

  range->ptr= (char*)(int)(key_tree->part);
  {
    range->range_flag= cur->min_key_flag | cur->max_key_flag;

    range->start_key.key=    seq->param->min_key;
    range->start_key.length= cur->min_key - seq->param->min_key;
    range->start_key.keypart_map= make_prev_keypart_map(cur->min_key_parts);
    range->start_key.flag= (cur->min_key_flag & NEAR_MIN ? HA_READ_AFTER_KEY :
                                                           HA_READ_KEY_EXACT);

    range->end_key.key=    seq->param->max_key;
    range->end_key.length= cur->max_key - seq->param->max_key;
    range->end_key.flag= (cur->max_key_flag & NEAR_MAX ? HA_READ_BEFORE_KEY :
                                                         HA_READ_AFTER_KEY);
    range->end_key.keypart_map= make_prev_keypart_map(cur->max_key_parts);

    if (!(cur->min_key_flag & ~NULL_RANGE) && !cur->max_key_flag &&
        (uint32_t)key_tree->part+1 == seq->param->table->key_info[seq->real_keyno].key_parts &&
        (seq->param->table->key_info[seq->real_keyno].flags & (HA_NOSAME)) ==
        HA_NOSAME &&
        range->start_key.length == range->end_key.length &&
        !memcmp(seq->param->min_key,seq->param->max_key,range->start_key.length))
      range->range_flag= UNIQUE_RANGE | (cur->min_key_flag & NULL_RANGE);

    if (seq->param->is_ror_scan)
    {
      /*
        If we get here, the condition on the key was converted to form
        "(keyXpart1 = c1) AND ... AND (keyXpart{key_tree->part - 1} = cN) AND
          somecond(keyXpart{key_tree->part})"
        Check if
          somecond is "keyXpart{key_tree->part} = const" and
          uncovered "tail" of KeyX parts is either empty or is identical to
          first members of clustered primary key.
      */
      if (!(!(cur->min_key_flag & ~NULL_RANGE) && !cur->max_key_flag &&
            (range->start_key.length == range->end_key.length) &&
            !memcmp(range->start_key.key, range->end_key.key, range->start_key.length) &&
            is_key_scan_ror(seq->param, seq->real_keyno, key_tree->part + 1)))
        seq->param->is_ror_scan= false;
    }
  }
  seq->param->range_count++;
  seq->param->max_key_part= max(seq->param->max_key_part,(uint32_t)key_tree->part);
  return 0;
}


/*
  Calculate cost and E(#rows) for a given index and intervals tree

  SYNOPSIS
    check_quick_select()
      param             Parameter from test_quick_select
      idx               Number of index to use in PARAM::key SEL_TREE::key
      index_only        true  - assume only index tuples will be accessed
                        false - assume full table rows will be read
      tree              Transformed selection condition, tree->key[idx] holds
                        the intervals for the given index.
      update_tbl_stats  true <=> update table->quick_* with information
                        about range scan we've evaluated.
      mrr_flags   INOUT MRR access flags
      cost        OUT   Scan cost

  NOTES
    param->is_ror_scan is set to reflect if the key scan is a ROR (see
    is_key_scan_ror function for more info)
    param->table->quick_*, param->range_count (and maybe others) are
    updated with data of given key scan, see quick_range_seq_next for details.

  RETURN
    Estimate # of records to be retrieved.
    HA_POS_ERROR if estimate calculation failed due to table handler problems.
*/

static
ha_rows check_quick_select(PARAM *param, uint32_t idx, bool index_only,
                           SEL_ARG *tree, bool update_tbl_stats,
                           uint32_t *mrr_flags, uint32_t *bufsize, COST_VECT *cost)
{
  SEL_ARG_RANGE_SEQ seq;
  RANGE_SEQ_IF seq_if = {sel_arg_range_seq_init, sel_arg_range_seq_next};
  handler *file= param->table->file;
  ha_rows rows;
  uint32_t keynr= param->real_keynr[idx];

  /* Handle cases when we don't have a valid non-empty list of range */
  if (!tree)
    return(HA_POS_ERROR);
  if (tree->type == SEL_ARG::IMPOSSIBLE)
    return(0L);
  if (tree->type != SEL_ARG::KEY_RANGE || tree->part != 0)
    return(HA_POS_ERROR);

  seq.keyno= idx;
  seq.real_keyno= keynr;
  seq.param= param;
  seq.start= tree;

  param->range_count=0;
  param->max_key_part=0;

  param->is_ror_scan= true;
  if (file->index_flags(keynr, 0, true) & HA_KEY_SCAN_NOT_ROR)
    param->is_ror_scan= false;

  *mrr_flags= param->force_default_mrr? HA_MRR_USE_DEFAULT_IMPL: 0;
  *mrr_flags|= HA_MRR_NO_ASSOCIATION;

  bool pk_is_clustered= file->primary_key_is_clustered();
  if (index_only &&
      (file->index_flags(keynr, param->max_key_part, 1) & HA_KEYREAD_ONLY) &&
      !(pk_is_clustered && keynr == param->table->s->primary_key))
     *mrr_flags |= HA_MRR_INDEX_ONLY;

  if (current_session->lex->sql_command != SQLCOM_SELECT)
    *mrr_flags |= HA_MRR_USE_DEFAULT_IMPL;

  *bufsize= param->session->variables.read_rnd_buff_size;
  rows= file->multi_range_read_info_const(keynr, &seq_if, (void*)&seq, 0,
                                          bufsize, mrr_flags, cost);
  if (rows != HA_POS_ERROR)
  {
    param->table->quick_rows[keynr]=rows;
    if (update_tbl_stats)
    {
      param->table->quick_keys.set(keynr);
      param->table->quick_key_parts[keynr]=param->max_key_part+1;
      param->table->quick_n_ranges[keynr]= param->range_count;
      param->table->quick_condition_rows=
        min(param->table->quick_condition_rows, rows);
    }
  }
  /* Figure out if the key scan is ROR (returns rows in ROWID order) or not */
  enum ha_key_alg key_alg= param->table->key_info[seq.real_keyno].algorithm;
  if ((key_alg != HA_KEY_ALG_BTREE) && (key_alg!= HA_KEY_ALG_UNDEF))
  {
    /*
      All scans are non-ROR scans for those index types.
      TODO: Don't have this logic here, make table engines return
      appropriate flags instead.
    */
    param->is_ror_scan= false;
  }
  else
  {
    /* Clustered PK scan is always a ROR scan (TODO: same as above) */
    if (param->table->s->primary_key == keynr && pk_is_clustered)
      param->is_ror_scan= true;
  }

  return(rows); //psergey-merge:todo: maintain first_null_comp.
}


/*
  Check if key scan on given index with equality conditions on first n key
  parts is a ROR scan.

  SYNOPSIS
    is_key_scan_ror()
      param  Parameter from test_quick_select
      keynr  Number of key in the table. The key must not be a clustered
             primary key.
      nparts Number of first key parts for which equality conditions
             are present.

  NOTES
    ROR (Rowid Ordered Retrieval) key scan is a key scan that produces
    ordered sequence of rowids (ha_xxx::cmp_ref is the comparison function)

    This function is needed to handle a practically-important special case:
    an index scan is a ROR scan if it is done using a condition in form

        "key1_1=c_1 AND ... AND key1_n=c_n"

    where the index is defined on (key1_1, ..., key1_N [,a_1, ..., a_n])

    and the table has a clustered Primary Key defined as
      PRIMARY KEY(a_1, ..., a_n, b1, ..., b_k)

    i.e. the first key parts of it are identical to uncovered parts ot the
    key being scanned. This function assumes that the index flags do not
    include HA_KEY_SCAN_NOT_ROR flag (that is checked elsewhere).

    Check (1) is made in quick_range_seq_next()

  RETURN
    true   The scan is ROR-scan
    false  Otherwise
*/

static bool is_key_scan_ror(PARAM *param, uint32_t keynr, uint8_t nparts)
{
  KEY *table_key= param->table->key_info + keynr;
  KEY_PART_INFO *key_part= table_key->key_part + nparts;
  KEY_PART_INFO *key_part_end= (table_key->key_part +
                                table_key->key_parts);
  uint32_t pk_number;

  for (KEY_PART_INFO *kp= table_key->key_part; kp < key_part; kp++)
  {
    uint16_t fieldnr= param->table->key_info[keynr].
                    key_part[kp - table_key->key_part].fieldnr - 1;
    if (param->table->field[fieldnr]->key_length() != kp->length)
      return false;
  }

  if (key_part == key_part_end)
    return true;

  key_part= table_key->key_part + nparts;
  pk_number= param->table->s->primary_key;
  if (!param->table->file->primary_key_is_clustered() || pk_number == MAX_KEY)
    return false;

  KEY_PART_INFO *pk_part= param->table->key_info[pk_number].key_part;
  KEY_PART_INFO *pk_part_end= pk_part +
                              param->table->key_info[pk_number].key_parts;
  for (;(key_part!=key_part_end) && (pk_part != pk_part_end);
       ++key_part, ++pk_part)
  {
    if ((key_part->field != pk_part->field) ||
        (key_part->length != pk_part->length))
      return false;
  }
  return (key_part == key_part_end);
}


/*
  Create a QUICK_RANGE_SELECT from given key and SEL_ARG tree for that key.

  SYNOPSIS
    get_quick_select()
      param
      idx            Index of used key in param->key.
      key_tree       SEL_ARG tree for the used key
      mrr_flags      MRR parameter for quick select
      mrr_buf_size   MRR parameter for quick select
      parent_alloc   If not NULL, use it to allocate memory for
                     quick select data. Otherwise use quick->alloc.
  NOTES
    The caller must call QUICK_SELECT::init for returned quick select.

    CAUTION! This function may change session->mem_root to a MEM_ROOT which will be
    deallocated when the returned quick select is deleted.

  RETURN
    NULL on error
    otherwise created quick select
*/

QUICK_RANGE_SELECT *
get_quick_select(PARAM *param,uint32_t idx,SEL_ARG *key_tree, uint32_t mrr_flags,
                 uint32_t mrr_buf_size, MEM_ROOT *parent_alloc)
{
  QUICK_RANGE_SELECT *quick;
  bool create_err= false;

  quick=new QUICK_RANGE_SELECT(param->session, param->table,
                               param->real_keynr[idx],
                               test(parent_alloc), NULL, &create_err);

  if (quick)
  {
    if (create_err ||
	get_quick_keys(param,quick,param->key[idx],key_tree,param->min_key,0,
		       param->max_key,0))
    {
      delete quick;
      quick=0;
    }
    else
    {
      quick->mrr_flags= mrr_flags;
      quick->mrr_buf_size= mrr_buf_size;
      quick->key_parts=(KEY_PART*)
        memdup_root(parent_alloc? parent_alloc : &quick->alloc,
                    (char*) param->key[idx],
                    sizeof(KEY_PART)*
                    param->table->key_info[param->real_keynr[idx]].key_parts);
    }
  }
  return quick;
}


/*
** Fix this to get all possible sub_ranges
*/
bool
get_quick_keys(PARAM *param,QUICK_RANGE_SELECT *quick,KEY_PART *key,
	       SEL_ARG *key_tree, unsigned char *min_key,uint32_t min_key_flag,
	       unsigned char *max_key, uint32_t max_key_flag)
{
  QUICK_RANGE *range;
  uint32_t flag;
  int min_part= key_tree->part-1, // # of keypart values in min_key buffer
      max_part= key_tree->part-1; // # of keypart values in max_key buffer

  if (key_tree->left != &null_element)
  {
    if (get_quick_keys(param,quick,key,key_tree->left,
		       min_key,min_key_flag, max_key, max_key_flag))
      return 1;
  }
  unsigned char *tmp_min_key=min_key,*tmp_max_key=max_key;
  min_part+= key_tree->store_min(key[key_tree->part].store_length,
                                 &tmp_min_key,min_key_flag);
  max_part+= key_tree->store_max(key[key_tree->part].store_length,
                                 &tmp_max_key,max_key_flag);

  if (key_tree->next_key_part &&
      key_tree->next_key_part->part == key_tree->part+1 &&
      key_tree->next_key_part->type == SEL_ARG::KEY_RANGE)
  {						  // const key as prefix
    if ((tmp_min_key - min_key) == (tmp_max_key - max_key) &&
         memcmp(min_key, max_key, (uint32_t)(tmp_max_key - max_key))==0 &&
	 key_tree->min_flag==0 && key_tree->max_flag==0)
    {
      if (get_quick_keys(param,quick,key,key_tree->next_key_part,
			 tmp_min_key, min_key_flag | key_tree->min_flag,
			 tmp_max_key, max_key_flag | key_tree->max_flag))
	return 1;
      goto end;					// Ugly, but efficient
    }
    {
      uint32_t tmp_min_flag=key_tree->min_flag,tmp_max_flag=key_tree->max_flag;
      if (!tmp_min_flag)
        min_part+= key_tree->next_key_part->store_min_key(key, &tmp_min_key,
                                                          &tmp_min_flag);
      if (!tmp_max_flag)
        max_part+= key_tree->next_key_part->store_max_key(key, &tmp_max_key,
                                                          &tmp_max_flag);
      flag=tmp_min_flag | tmp_max_flag;
    }
  }
  else
  {
    flag= key_tree->min_flag | key_tree->max_flag;
  }

  /*
    Ensure that some part of min_key and max_key are used.  If not,
    regard this as no lower/upper range
  */
  {
    if (tmp_min_key != param->min_key)
      flag&= ~NO_MIN_RANGE;
    else
      flag|= NO_MIN_RANGE;
    if (tmp_max_key != param->max_key)
      flag&= ~NO_MAX_RANGE;
    else
      flag|= NO_MAX_RANGE;
  }
  if (flag == 0)
  {
    uint32_t length= (uint32_t) (tmp_min_key - param->min_key);
    if (length == (uint32_t) (tmp_max_key - param->max_key) &&
	!memcmp(param->min_key,param->max_key,length))
    {
      KEY *table_key=quick->head->key_info+quick->index;
      flag=EQ_RANGE;
      if ((table_key->flags & (HA_NOSAME)) == HA_NOSAME &&
	  key->part == table_key->key_parts-1)
      {
	if (!(table_key->flags & HA_NULL_PART_KEY) ||
	    !null_part_in_key(key,
			      param->min_key,
			      (uint32_t) (tmp_min_key - param->min_key)))
	  flag|= UNIQUE_RANGE;
	else
	  flag|= NULL_RANGE;
      }
    }
  }

  /* Get range for retrieving rows in QUICK_SELECT::get_next */
  if (!(range= new QUICK_RANGE(param->min_key,
			       (uint32_t) (tmp_min_key - param->min_key),
                               min_part >=0 ? make_keypart_map(min_part) : 0,
			       param->max_key,
			       (uint32_t) (tmp_max_key - param->max_key),
                               max_part >=0 ? make_keypart_map(max_part) : 0,
			       flag)))
    return 1;			// out of memory

  set_if_bigger(quick->max_used_key_length, (uint32_t)range->min_length);
  set_if_bigger(quick->max_used_key_length, (uint32_t)range->max_length);
  set_if_bigger(quick->used_key_parts, (uint32_t) key_tree->part+1);
  if (insert_dynamic(&quick->ranges, (unsigned char*) &range))
    return 1;

 end:
  if (key_tree->right != &null_element)
    return get_quick_keys(param,quick,key,key_tree->right,
			  min_key,min_key_flag,
			  max_key,max_key_flag);
  return 0;
}

/*
  Return 1 if there is only one range and this uses the whole primary key
*/

bool QUICK_RANGE_SELECT::unique_key_range()
{
  if (ranges.elements == 1)
  {
    QUICK_RANGE *tmp= *((QUICK_RANGE**)ranges.buffer);
    if ((tmp->flag & (EQ_RANGE | NULL_RANGE)) == EQ_RANGE)
    {
      KEY *key=head->key_info+index;
      return ((key->flags & (HA_NOSAME)) == HA_NOSAME &&
	      key->key_length == tmp->min_length);
    }
  }
  return 0;
}



/*
  Return true if any part of the key is NULL

  SYNOPSIS
    null_part_in_key()
      key_part  Array of key parts (index description)
      key       Key values tuple
      length    Length of key values tuple in bytes.

  RETURN
    true   The tuple has at least one "keypartX is NULL"
    false  Otherwise
*/

static bool null_part_in_key(KEY_PART *key_part, const unsigned char *key, uint32_t length)
{
  for (const unsigned char *end=key+length ;
       key < end;
       key+= key_part++->store_length)
  {
    if (key_part->null_bit && *key)
      return 1;
  }
  return 0;
}


bool QUICK_SELECT_I::is_keys_used(const MyBitmap *fields)
{
  return is_key_used(head, index, fields);
}

bool QUICK_INDEX_MERGE_SELECT::is_keys_used(const MyBitmap *fields)
{
  QUICK_RANGE_SELECT *quick;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  while ((quick= it++))
  {
    if (is_key_used(head, quick->index, fields))
      return 1;
  }
  return 0;
}

bool QUICK_ROR_INTERSECT_SELECT::is_keys_used(const MyBitmap *fields)
{
  QUICK_RANGE_SELECT *quick;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  while ((quick= it++))
  {
    if (is_key_used(head, quick->index, fields))
      return 1;
  }
  return 0;
}

bool QUICK_ROR_UNION_SELECT::is_keys_used(const MyBitmap *fields)
{
  QUICK_SELECT_I *quick;
  List_iterator_fast<QUICK_SELECT_I> it(quick_selects);
  while ((quick= it++))
  {
    if (quick->is_keys_used(fields))
      return 1;
  }
  return 0;
}


/*
  Create quick select from ref/ref_or_null scan.

  SYNOPSIS
    get_quick_select_for_ref()
      session      Thread handle
      table    Table to access
      ref      ref[_or_null] scan parameters
      records  Estimate of number of records (needed only to construct
               quick select)
  NOTES
    This allocates things in a new memory root, as this may be called many
    times during a query.

  RETURN
    Quick select that retrieves the same rows as passed ref scan
    NULL on error.
*/

QUICK_RANGE_SELECT *get_quick_select_for_ref(Session *session, Table *table,
                                             table_reference_st *ref, ha_rows records)
{
  MEM_ROOT *old_root, *alloc;
  QUICK_RANGE_SELECT *quick;
  KEY *key_info = &table->key_info[ref->key];
  KEY_PART *key_part;
  QUICK_RANGE *range;
  uint32_t part;
  bool create_err= false;
  COST_VECT cost;

  old_root= session->mem_root;
  /* The following call may change session->mem_root */
  quick= new QUICK_RANGE_SELECT(session, table, ref->key, 0, 0, &create_err);
  /* save mem_root set by QUICK_RANGE_SELECT constructor */
  alloc= session->mem_root;
  /*
    return back default mem_root (session->mem_root) changed by
    QUICK_RANGE_SELECT constructor
  */
  session->mem_root= old_root;

  if (!quick || create_err)
    return 0;			/* no ranges found */
  if (quick->init())
    goto err;
  quick->records= records;

  if ((cp_buffer_from_ref(session, ref) && session->is_fatal_error) ||
      !(range= new(alloc) QUICK_RANGE()))
    goto err;                                   // out of memory

  range->min_key= range->max_key= ref->key_buff;
  range->min_length= range->max_length= ref->key_length;
  range->min_keypart_map= range->max_keypart_map=
    make_prev_keypart_map(ref->key_parts);
  range->flag= ((ref->key_length == key_info->key_length &&
                 (key_info->flags & HA_END_SPACE_KEY) == 0) ? EQ_RANGE : 0);


  if (!(quick->key_parts=key_part=(KEY_PART *)
	alloc_root(&quick->alloc,sizeof(KEY_PART)*ref->key_parts)))
    goto err;

  for (part=0 ; part < ref->key_parts ;part++,key_part++)
  {
    key_part->part=part;
    key_part->field=        key_info->key_part[part].field;
    key_part->length=       key_info->key_part[part].length;
    key_part->store_length= key_info->key_part[part].store_length;
    key_part->null_bit=     key_info->key_part[part].null_bit;
    key_part->flag=         (uint8_t) key_info->key_part[part].key_part_flag;
  }
  if (insert_dynamic(&quick->ranges,(unsigned char*)&range))
    goto err;

  /*
     Add a NULL range if REF_OR_NULL optimization is used.
     For example:
       if we have "WHERE A=2 OR A IS NULL" we created the (A=2) range above
       and have ref->null_ref_key set. Will create a new NULL range here.
  */
  if (ref->null_ref_key)
  {
    QUICK_RANGE *null_range;

    *ref->null_ref_key= 1;		// Set null byte then create a range
    if (!(null_range= new (alloc)
          QUICK_RANGE(ref->key_buff, ref->key_length,
                      make_prev_keypart_map(ref->key_parts),
                      ref->key_buff, ref->key_length,
                      make_prev_keypart_map(ref->key_parts), EQ_RANGE)))
      goto err;
    *ref->null_ref_key= 0;		// Clear null byte
    if (insert_dynamic(&quick->ranges,(unsigned char*)&null_range))
      goto err;
  }

  /* Call multi_range_read_info() to get the MRR flags and buffer size */
  quick->mrr_flags= HA_MRR_NO_ASSOCIATION |
                    (table->key_read ? HA_MRR_INDEX_ONLY : 0);
  if (session->lex->sql_command != SQLCOM_SELECT)
    quick->mrr_flags |= HA_MRR_USE_DEFAULT_IMPL;

  quick->mrr_buf_size= session->variables.read_rnd_buff_size;
  if (table->file->multi_range_read_info(quick->index, 1, (uint32_t)records,
                                         &quick->mrr_buf_size,
                                         &quick->mrr_flags, &cost))
    goto err;

  return quick;
err:
  delete quick;
  return 0;
}


/*
  Perform key scans for all used indexes (except CPK), get rowids and merge
  them into an ordered non-recurrent sequence of rowids.

  The merge/duplicate removal is performed using Unique class. We put all
  rowids into Unique, get the sorted sequence and destroy the Unique.

  If table has a clustered primary key that covers all rows (true for bdb
  and innodb currently) and one of the index_merge scans is a scan on PK,
  then rows that will be retrieved by PK scan are not put into Unique and
  primary key scan is not performed here, it is performed later separately.

  RETURN
    0     OK
    other error
*/

int QUICK_INDEX_MERGE_SELECT::read_keys_and_merge()
{
  List_iterator_fast<QUICK_RANGE_SELECT> cur_quick_it(quick_selects);
  QUICK_RANGE_SELECT* cur_quick;
  int result;
  Unique *unique;
  handler *file= head->file;

  file->extra(HA_EXTRA_KEYREAD);
  head->prepare_for_position();

  cur_quick_it.rewind();
  cur_quick= cur_quick_it++;
  assert(cur_quick != 0);

  /*
    We reuse the same instance of handler so we need to call both init and
    reset here.
  */
  if (cur_quick->init() || cur_quick->reset())
    return 0;

  unique= new Unique(refpos_order_cmp, (void *)file,
                     file->ref_length,
                     session->variables.sortbuff_size);
  if (!unique)
    return 0;
  for (;;)
  {
    while ((result= cur_quick->get_next()) == HA_ERR_END_OF_FILE)
    {
      cur_quick->range_end();
      cur_quick= cur_quick_it++;
      if (!cur_quick)
        break;

      if (cur_quick->file->inited != handler::NONE)
        cur_quick->file->ha_index_end();
      if (cur_quick->init() || cur_quick->reset())
        return 0;
    }

    if (result)
    {
      if (result != HA_ERR_END_OF_FILE)
      {
        cur_quick->range_end();
        return result;
      }
      break;
    }

    if (session->killed)
      return 0;

    /* skip row if it will be retrieved by clustered PK scan */
    if (pk_quick_select && pk_quick_select->row_in_ranges())
      continue;

    cur_quick->file->position(cur_quick->record);
    result= unique->unique_add((char*)cur_quick->file->ref);
    if (result)
      return 0;

  }

  /* ok, all row ids are in Unique */
  result= unique->get(head);
  delete unique;
  doing_pk_scan= false;
  /* index_merge currently doesn't support "using index" at all */
  file->extra(HA_EXTRA_NO_KEYREAD);
  /* start table scan */
  init_read_record(&read_record, session, head, (SQL_SELECT*) 0, 1, 1);
  return result;
}


/*
  Get next row for index_merge.
  NOTES
    The rows are read from
      1. rowids stored in Unique.
      2. QUICK_RANGE_SELECT with clustered primary key (if any).
    The sets of rows retrieved in 1) and 2) are guaranteed to be disjoint.
*/

int QUICK_INDEX_MERGE_SELECT::get_next()
{
  int result;

  if (doing_pk_scan)
    return(pk_quick_select->get_next());

  if ((result= read_record.read_record(&read_record)) == -1)
  {
    result= HA_ERR_END_OF_FILE;
    end_read_record(&read_record);
    /* All rows from Unique have been retrieved, do a clustered PK scan */
    if (pk_quick_select)
    {
      doing_pk_scan= true;
      if ((result= pk_quick_select->init()) ||
          (result= pk_quick_select->reset()))
        return result;
      return(pk_quick_select->get_next());
    }
  }

  return result;
}


/*
  Retrieve next record.
  SYNOPSIS
     QUICK_ROR_INTERSECT_SELECT::get_next()

  NOTES
    Invariant on enter/exit: all intersected selects have retrieved all index
    records with rowid <= some_rowid_val and no intersected select has
    retrieved any index records with rowid > some_rowid_val.
    We start fresh and loop until we have retrieved the same rowid in each of
    the key scans or we got an error.

    If a Clustered PK scan is present, it is used only to check if row
    satisfies its condition (and never used for row retrieval).

  RETURN
   0     - Ok
   other - Error code if any error occurred.
*/

int QUICK_ROR_INTERSECT_SELECT::get_next()
{
  List_iterator_fast<QUICK_RANGE_SELECT> quick_it(quick_selects);
  QUICK_RANGE_SELECT* quick;
  int error, cmp;
  uint32_t last_rowid_count=0;

  do
  {
    /* Get a rowid for first quick and save it as a 'candidate' */
    quick= quick_it++;
    error= quick->get_next();
    if (cpk_quick)
    {
      while (!error && !cpk_quick->row_in_ranges())
        error= quick->get_next();
    }
    if (error)
      return(error);

    quick->file->position(quick->record);
    memcpy(last_rowid, quick->file->ref, head->file->ref_length);
    last_rowid_count= 1;

    while (last_rowid_count < quick_selects.elements)
    {
      if (!(quick= quick_it++))
      {
        quick_it.rewind();
        quick= quick_it++;
      }

      do
      {
        if ((error= quick->get_next()))
          return(error);
        quick->file->position(quick->record);
        cmp= head->file->cmp_ref(quick->file->ref, last_rowid);
      } while (cmp < 0);

      /* Ok, current select 'caught up' and returned ref >= cur_ref */
      if (cmp > 0)
      {
        /* Found a row with ref > cur_ref. Make it a new 'candidate' */
        if (cpk_quick)
        {
          while (!cpk_quick->row_in_ranges())
          {
            if ((error= quick->get_next()))
              return(error);
          }
        }
        memcpy(last_rowid, quick->file->ref, head->file->ref_length);
        last_rowid_count= 1;
      }
      else
      {
        /* current 'candidate' row confirmed by this select */
        last_rowid_count++;
      }
    }

    /* We get here if we got the same row ref in all scans. */
    if (need_to_fetch_row)
      error= head->file->rnd_pos(head->record[0], last_rowid);
  } while (error == HA_ERR_RECORD_DELETED);
  return(error);
}


/*
  Retrieve next record.
  SYNOPSIS
    QUICK_ROR_UNION_SELECT::get_next()

  NOTES
    Enter/exit invariant:
    For each quick select in the queue a {key,rowid} tuple has been
    retrieved but the corresponding row hasn't been passed to output.

  RETURN
   0     - Ok
   other - Error code if any error occurred.
*/

int QUICK_ROR_UNION_SELECT::get_next()
{
  int error, dup_row;
  QUICK_SELECT_I *quick;
  unsigned char *tmp;

  do
  {
    do
    {
      if (queue->empty())
        return(HA_ERR_END_OF_FILE);
      /* Ok, we have a queue with >= 1 scans */

      quick= queue->top();
      memcpy(cur_rowid, quick->last_rowid, rowid_length);

      /* put into queue rowid from the same stream as top element */
      if ((error= quick->get_next()))
      {
        if (error != HA_ERR_END_OF_FILE)
          return(error);
        queue->pop();
      }
      else
      {
        quick->save_last_pos();
        queue->pop();
        queue->push(quick);
      }

      if (!have_prev_rowid)
      {
        /* No rows have been returned yet */
        dup_row= false;
        have_prev_rowid= true;
      }
      else
        dup_row= !head->file->cmp_ref(cur_rowid, prev_rowid);
    } while (dup_row);

    tmp= cur_rowid;
    cur_rowid= prev_rowid;
    prev_rowid= tmp;

    error= head->file->rnd_pos(quick->record, prev_rowid);
  } while (error == HA_ERR_RECORD_DELETED);
  return(error);
}


int QUICK_RANGE_SELECT::reset()
{
  uint32_t  buf_size;
  unsigned char *mrange_buff;
  int   error;
  HANDLER_BUFFER empty_buf;
  last_range= NULL;
  cur_range= (QUICK_RANGE**) ranges.buffer;

  if (file->inited == handler::NONE && (error= file->ha_index_init(index,1)))
    return(error);

  /* Allocate buffer if we need one but haven't allocated it yet */
  if (mrr_buf_size && !mrr_buf_desc)
  {
    buf_size= mrr_buf_size;
    while (buf_size && !my_multi_malloc(MYF(MY_WME),
                                        &mrr_buf_desc, sizeof(*mrr_buf_desc),
                                        &mrange_buff, buf_size,
                                        NULL))
    {
      /* Try to shrink the buffers until both are 0. */
      buf_size/= 2;
    }
    if (!mrr_buf_desc)
      return(HA_ERR_OUT_OF_MEM);

    /* Initialize the handler buffer. */
    mrr_buf_desc->buffer= mrange_buff;
    mrr_buf_desc->buffer_end= mrange_buff + buf_size;
    mrr_buf_desc->end_of_used_area= mrange_buff;
  }

  if (!mrr_buf_desc)
    empty_buf.buffer= empty_buf.buffer_end= empty_buf.end_of_used_area= NULL;

  if (sorted)
     mrr_flags |= HA_MRR_SORTED;
  RANGE_SEQ_IF seq_funcs= {quick_range_seq_init, quick_range_seq_next};
  error= file->multi_range_read_init(&seq_funcs, (void*)this, ranges.elements,
                                     mrr_flags, mrr_buf_desc? mrr_buf_desc:
                                                              &empty_buf);
  return(error);
}


/*
  Range sequence interface implementation for array<QUICK_RANGE>: initialize

  SYNOPSIS
    quick_range_seq_init()
      init_param  Caller-opaque paramenter: QUICK_RANGE_SELECT* pointer
      n_ranges    Number of ranges in the sequence (ignored)
      flags       MRR flags (currently not used)

  RETURN
    Opaque value to be passed to quick_range_seq_next
*/

range_seq_t quick_range_seq_init(void *init_param, uint32_t, uint32_t)
{
  QUICK_RANGE_SELECT *quick= (QUICK_RANGE_SELECT*)init_param;
  quick->qr_traversal_ctx.first=  (QUICK_RANGE**)quick->ranges.buffer;
  quick->qr_traversal_ctx.cur=    (QUICK_RANGE**)quick->ranges.buffer;
  quick->qr_traversal_ctx.last=   quick->qr_traversal_ctx.cur +
                                  quick->ranges.elements;
  return &quick->qr_traversal_ctx;
}


/*
  Range sequence interface implementation for array<QUICK_RANGE>: get next

  SYNOPSIS
    quick_range_seq_next()
      rseq        Value returned from quick_range_seq_init
      range  OUT  Store information about the range here

  RETURN
    0  Ok
    1  No more ranges in the sequence
*/

uint32_t quick_range_seq_next(range_seq_t rseq, KEY_MULTI_RANGE *range)
{
  QUICK_RANGE_SEQ_CTX *ctx= (QUICK_RANGE_SEQ_CTX*)rseq;

  if (ctx->cur == ctx->last)
    return 1; /* no more ranges */

  QUICK_RANGE *cur= *(ctx->cur);
  key_range *start_key= &range->start_key;
  key_range *end_key=   &range->end_key;

  start_key->key=    cur->min_key;
  start_key->length= cur->min_length;
  start_key->keypart_map= cur->min_keypart_map;
  start_key->flag=   ((cur->flag & NEAR_MIN) ? HA_READ_AFTER_KEY :
                      (cur->flag & EQ_RANGE) ?
                      HA_READ_KEY_EXACT : HA_READ_KEY_OR_NEXT);
  end_key->key=      cur->max_key;
  end_key->length=   cur->max_length;
  end_key->keypart_map= cur->max_keypart_map;
  /*
    We use HA_READ_AFTER_KEY here because if we are reading on a key
    prefix. We want to find all keys with this prefix.
  */
  end_key->flag=     (cur->flag & NEAR_MAX ? HA_READ_BEFORE_KEY :
                      HA_READ_AFTER_KEY);
  range->range_flag= cur->flag;
  ctx->cur++;
  return 0;
}


/*
  Get next possible record using quick-struct.

  SYNOPSIS
    QUICK_RANGE_SELECT::get_next()

  NOTES
    Record is read into table->record[0]

  RETURN
    0			Found row
    HA_ERR_END_OF_FILE	No (more) rows in range
    #			Error code
*/

int QUICK_RANGE_SELECT::get_next()
{
  char *dummy;
  if (in_ror_merged_scan)
  {
    /*
      We don't need to signal the bitmap change as the bitmap is always the
      same for this head->file
    */
    head->column_bitmaps_set(&column_bitmap, &column_bitmap);
  }

  int result= file->multi_range_read_next(&dummy);

  if (in_ror_merged_scan)
  {
    /* Restore bitmaps set on entry */
    head->column_bitmaps_set(save_read_set, save_write_set);
  }
  return result;
}


/*
  Get the next record with a different prefix.

  SYNOPSIS
    QUICK_RANGE_SELECT::get_next_prefix()
    prefix_length  length of cur_prefix
    cur_prefix     prefix of a key to be searched for

  DESCRIPTION
    Each subsequent call to the method retrieves the first record that has a
    prefix with length prefix_length different from cur_prefix, such that the
    record with the new prefix is within the ranges described by
    this->ranges. The record found is stored into the buffer pointed by
    this->record.
    The method is useful for GROUP-BY queries with range conditions to
    discover the prefix of the next group that satisfies the range conditions.

  TODO
    This method is a modified copy of QUICK_RANGE_SELECT::get_next(), so both
    methods should be unified into a more general one to reduce code
    duplication.

  RETURN
    0                  on success
    HA_ERR_END_OF_FILE if returned all keys
    other              if some error occurred
*/

int QUICK_RANGE_SELECT::get_next_prefix(uint32_t prefix_length,
                                        key_part_map keypart_map,
                                        unsigned char *cur_prefix)
{
  for (;;)
  {
    int result;
    key_range start_key, end_key;
    if (last_range)
    {
      /* Read the next record in the same range with prefix after cur_prefix. */
      assert(cur_prefix != 0);
      result= file->index_read_map(record, cur_prefix, keypart_map,
                                   HA_READ_AFTER_KEY);
      if (result || (file->compare_key(file->end_range) <= 0))
        return result;
    }

    uint32_t count= ranges.elements - (cur_range - (QUICK_RANGE**) ranges.buffer);
    if (count == 0)
    {
      /* Ranges have already been used up before. None is left for read. */
      last_range= 0;
      return HA_ERR_END_OF_FILE;
    }
    last_range= *(cur_range++);

    start_key.key=    (const unsigned char*) last_range->min_key;
    start_key.length= min(last_range->min_length, (uint16_t)prefix_length);
    start_key.keypart_map= last_range->min_keypart_map & keypart_map;
    start_key.flag=   ((last_range->flag & NEAR_MIN) ? HA_READ_AFTER_KEY :
		       (last_range->flag & EQ_RANGE) ?
		       HA_READ_KEY_EXACT : HA_READ_KEY_OR_NEXT);
    end_key.key=      (const unsigned char*) last_range->max_key;
    end_key.length=   min(last_range->max_length, (uint16_t)prefix_length);
    end_key.keypart_map= last_range->max_keypart_map & keypart_map;
    /*
      We use READ_AFTER_KEY here because if we are reading on a key
      prefix we want to find all keys with this prefix
    */
    end_key.flag=     (last_range->flag & NEAR_MAX ? HA_READ_BEFORE_KEY :
		       HA_READ_AFTER_KEY);

    result= file->read_range_first(last_range->min_keypart_map ? &start_key : 0,
				   last_range->max_keypart_map ? &end_key : 0,
                                   test(last_range->flag & EQ_RANGE),
				   sorted);
    if (last_range->flag == (UNIQUE_RANGE | EQ_RANGE))
      last_range= 0;			// Stop searching

    if (result != HA_ERR_END_OF_FILE)
      return result;
    last_range= 0;			// No matching rows; go to next range
  }
}


/*
  Check if current row will be retrieved by this QUICK_RANGE_SELECT

  NOTES
    It is assumed that currently a scan is being done on another index
    which reads all necessary parts of the index that is scanned by this
    quick select.
    The implementation does a binary search on sorted array of disjoint
    ranges, without taking size of range into account.

    This function is used to filter out clustered PK scan rows in
    index_merge quick select.

  RETURN
    true  if current row will be retrieved by this quick select
    false if not
*/

bool QUICK_RANGE_SELECT::row_in_ranges()
{
  QUICK_RANGE *res;
  uint32_t min= 0;
  uint32_t max= ranges.elements - 1;
  uint32_t mid= (max + min)/2;

  while (min != max)
  {
    if (cmp_next(*(QUICK_RANGE**)dynamic_array_ptr(&ranges, mid)))
    {
      /* current row value > mid->max */
      min= mid + 1;
    }
    else
      max= mid;
    mid= (min + max) / 2;
  }
  res= *(QUICK_RANGE**)dynamic_array_ptr(&ranges, mid);
  return (!cmp_next(res) && !cmp_prev(res));
}

/*
  This is a hack: we inherit from QUICK_SELECT so that we can use the
  get_next() interface, but we have to hold a pointer to the original
  QUICK_SELECT because its data are used all over the place.  What
  should be done is to factor out the data that is needed into a base
  class (QUICK_SELECT), and then have two subclasses (_ASC and _DESC)
  which handle the ranges and implement the get_next() function.  But
  for now, this seems to work right at least.
 */

QUICK_SELECT_DESC::QUICK_SELECT_DESC(QUICK_RANGE_SELECT *q, uint32_t, bool *)
 :QUICK_RANGE_SELECT(*q), rev_it(rev_ranges)
{
  QUICK_RANGE *r;

  QUICK_RANGE **pr= (QUICK_RANGE**)ranges.buffer;
  QUICK_RANGE **end_range= pr + ranges.elements;
  for (; pr!=end_range; pr++)
    rev_ranges.push_front(*pr);

  /* Remove EQ_RANGE flag for keys that are not using the full key */
  for (r = rev_it++; r; r = rev_it++)
  {
    if ((r->flag & EQ_RANGE) &&
	head->key_info[index].key_length != r->max_length)
      r->flag&= ~EQ_RANGE;
  }
  rev_it.rewind();
  q->dont_free=1;				// Don't free shared mem
  delete q;
}


int QUICK_SELECT_DESC::get_next()
{
  /* The max key is handled as follows:
   *   - if there is NO_MAX_RANGE, start at the end and move backwards
   *   - if it is an EQ_RANGE, which means that max key covers the entire
   *     key, go directly to the key and read through it (sorting backwards is
   *     same as sorting forwards)
   *   - if it is NEAR_MAX, go to the key or next, step back once, and
   *     move backwards
   *   - otherwise (not NEAR_MAX == include the key), go after the key,
   *     step back once, and move backwards
   */

  for (;;)
  {
    int result;
    if (last_range)
    {						// Already read through key
      result = ((last_range->flag & EQ_RANGE)
		? file->index_next_same(record, last_range->min_key,
					last_range->min_length) :
		file->index_prev(record));
      if (!result)
      {
	if (cmp_prev(*rev_it.ref()) == 0)
	  return 0;
      }
      else if (result != HA_ERR_END_OF_FILE)
	return result;
    }

    if (!(last_range= rev_it++))
      return HA_ERR_END_OF_FILE;		// All ranges used

    if (last_range->flag & NO_MAX_RANGE)        // Read last record
    {
      int local_error;
      if ((local_error=file->index_last(record)))
	return(local_error);		// Empty table
      if (cmp_prev(last_range) == 0)
	return 0;
      last_range= 0;                            // No match; go to next range
      continue;
    }

    if (last_range->flag & EQ_RANGE)
    {
      result = file->index_read_map(record, last_range->max_key,
                                    last_range->max_keypart_map,
                                    HA_READ_KEY_EXACT);
    }
    else
    {
      assert(last_range->flag & NEAR_MAX ||
                  range_reads_after_key(last_range));
      result=file->index_read_map(record, last_range->max_key,
                                  last_range->max_keypart_map,
                                  ((last_range->flag & NEAR_MAX) ?
                                   HA_READ_BEFORE_KEY :
                                   HA_READ_PREFIX_LAST_OR_PREV));
    }
    if (result)
    {
      if (result != HA_ERR_KEY_NOT_FOUND && result != HA_ERR_END_OF_FILE)
	return result;
      last_range= 0;                            // Not found, to next range
      continue;
    }
    if (cmp_prev(last_range) == 0)
    {
      if (last_range->flag == (UNIQUE_RANGE | EQ_RANGE))
	last_range= 0;				// Stop searching
      return 0;				// Found key is in range
    }
    last_range= 0;                              // To next range
  }
}


/*
  Compare if found key is over max-value
  Returns 0 if key <= range->max_key
  TODO: Figure out why can't this function be as simple as cmp_prev().
*/

int QUICK_RANGE_SELECT::cmp_next(QUICK_RANGE *range_arg)
{
  if (range_arg->flag & NO_MAX_RANGE)
    return 0;                                   /* key can't be to large */

  KEY_PART *key_part=key_parts;
  uint32_t store_length;

  for (unsigned char *key=range_arg->max_key, *end=key+range_arg->max_length;
       key < end;
       key+= store_length, key_part++)
  {
    int cmp;
    store_length= key_part->store_length;
    if (key_part->null_bit)
    {
      if (*key)
      {
        if (!key_part->field->is_null())
          return 1;
        continue;
      }
      else if (key_part->field->is_null())
        return 0;
      key++;					// Skip null byte
      store_length--;
    }
    if ((cmp=key_part->field->key_cmp(key, key_part->length)) < 0)
      return 0;
    if (cmp > 0)
      return 1;
  }
  return (range_arg->flag & NEAR_MAX) ? 1 : 0;          // Exact match
}


/*
  Returns 0 if found key is inside range (found key >= range->min_key).
*/

int QUICK_RANGE_SELECT::cmp_prev(QUICK_RANGE *range_arg)
{
  int cmp;
  if (range_arg->flag & NO_MIN_RANGE)
    return 0;					/* key can't be to small */

  cmp= key_cmp(key_part_info, range_arg->min_key,
               range_arg->min_length);
  if (cmp > 0 || (cmp == 0 && (range_arg->flag & NEAR_MIN) == false))
    return 0;
  return 1;                                     // outside of range
}


/*
 * true if this range will require using HA_READ_AFTER_KEY
   See comment in get_next() about this
 */

bool QUICK_SELECT_DESC::range_reads_after_key(QUICK_RANGE *range_arg)
{
  return ((range_arg->flag & (NO_MAX_RANGE | NEAR_MAX)) ||
	  !(range_arg->flag & EQ_RANGE) ||
	  head->key_info[index].key_length != range_arg->max_length) ? 1 : 0;
}


void QUICK_RANGE_SELECT::add_info_string(String *str)
{
  KEY *key_info= head->key_info + index;
  str->append(key_info->name);
}

void QUICK_INDEX_MERGE_SELECT::add_info_string(String *str)
{
  QUICK_RANGE_SELECT *quick;
  bool first= true;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  str->append(STRING_WITH_LEN("sort_union("));
  while ((quick= it++))
  {
    if (!first)
      str->append(',');
    else
      first= false;
    quick->add_info_string(str);
  }
  if (pk_quick_select)
  {
    str->append(',');
    pk_quick_select->add_info_string(str);
  }
  str->append(')');
}

void QUICK_ROR_INTERSECT_SELECT::add_info_string(String *str)
{
  bool first= true;
  QUICK_RANGE_SELECT *quick;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  str->append(STRING_WITH_LEN("intersect("));
  while ((quick= it++))
  {
    KEY *key_info= head->key_info + quick->index;
    if (!first)
      str->append(',');
    else
      first= false;
    str->append(key_info->name);
  }
  if (cpk_quick)
  {
    KEY *key_info= head->key_info + cpk_quick->index;
    str->append(',');
    str->append(key_info->name);
  }
  str->append(')');
}

void QUICK_ROR_UNION_SELECT::add_info_string(String *str)
{
  bool first= true;
  QUICK_SELECT_I *quick;
  List_iterator_fast<QUICK_SELECT_I> it(quick_selects);
  str->append(STRING_WITH_LEN("union("));
  while ((quick= it++))
  {
    if (!first)
      str->append(',');
    else
      first= false;
    quick->add_info_string(str);
  }
  str->append(')');
}


void QUICK_RANGE_SELECT::add_keys_and_lengths(String *key_names,
                                              String *used_lengths)
{
  char buf[64];
  uint32_t length;
  KEY *key_info= head->key_info + index;
  key_names->append(key_info->name);
  length= int64_t2str(max_used_key_length, buf, 10) - buf;
  used_lengths->append(buf, length);
}

void QUICK_INDEX_MERGE_SELECT::add_keys_and_lengths(String *key_names,
                                                    String *used_lengths)
{
  char buf[64];
  uint32_t length;
  bool first= true;
  QUICK_RANGE_SELECT *quick;

  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  while ((quick= it++))
  {
    if (first)
      first= false;
    else
    {
      key_names->append(',');
      used_lengths->append(',');
    }

    KEY *key_info= head->key_info + quick->index;
    key_names->append(key_info->name);
    length= int64_t2str(quick->max_used_key_length, buf, 10) - buf;
    used_lengths->append(buf, length);
  }
  if (pk_quick_select)
  {
    KEY *key_info= head->key_info + pk_quick_select->index;
    key_names->append(',');
    key_names->append(key_info->name);
    length= int64_t2str(pk_quick_select->max_used_key_length, buf, 10) - buf;
    used_lengths->append(',');
    used_lengths->append(buf, length);
  }
}

void QUICK_ROR_INTERSECT_SELECT::add_keys_and_lengths(String *key_names,
                                                      String *used_lengths)
{
  char buf[64];
  uint32_t length;
  bool first= true;
  QUICK_RANGE_SELECT *quick;
  List_iterator_fast<QUICK_RANGE_SELECT> it(quick_selects);
  while ((quick= it++))
  {
    KEY *key_info= head->key_info + quick->index;
    if (first)
      first= false;
    else
    {
      key_names->append(',');
      used_lengths->append(',');
    }
    key_names->append(key_info->name);
    length= int64_t2str(quick->max_used_key_length, buf, 10) - buf;
    used_lengths->append(buf, length);
  }

  if (cpk_quick)
  {
    KEY *key_info= head->key_info + cpk_quick->index;
    key_names->append(',');
    key_names->append(key_info->name);
    length= int64_t2str(cpk_quick->max_used_key_length, buf, 10) - buf;
    used_lengths->append(',');
    used_lengths->append(buf, length);
  }
}

void QUICK_ROR_UNION_SELECT::add_keys_and_lengths(String *key_names,
                                                  String *used_lengths)
{
  bool first= true;
  QUICK_SELECT_I *quick;
  List_iterator_fast<QUICK_SELECT_I> it(quick_selects);
  while ((quick= it++))
  {
    if (first)
      first= false;
    else
    {
      used_lengths->append(',');
      key_names->append(',');
    }
    quick->add_keys_and_lengths(key_names, used_lengths);
  }
}


/*******************************************************************************
* Implementation of QUICK_GROUP_MIN_MAX_SELECT
*******************************************************************************/

static inline uint32_t get_field_keypart(KEY *index, Field *field);
static inline SEL_ARG * get_index_range_tree(uint32_t index, SEL_TREE* range_tree,
                                             PARAM *param, uint32_t *param_idx);
static bool get_constant_key_infix(KEY *index_info, SEL_ARG *index_range_tree,
                       KEY_PART_INFO *first_non_group_part,
                       KEY_PART_INFO *min_max_arg_part,
                       KEY_PART_INFO *last_part, Session *session,
                       unsigned char *key_infix, uint32_t *key_infix_len,
                       KEY_PART_INFO **first_non_infix_part);
static bool check_group_min_max_predicates(COND *cond, Item_field *min_max_arg_item);

static void
cost_group_min_max(Table* table, KEY *index_info, uint32_t used_key_parts,
                   uint32_t group_key_parts, SEL_TREE *range_tree,
                   SEL_ARG *index_tree, ha_rows quick_prefix_records,
                   bool have_min, bool have_max,
                   double *read_cost, ha_rows *records);


/*
  Test if this access method is applicable to a GROUP query with MIN/MAX
  functions, and if so, construct a new TRP object.

  SYNOPSIS
    get_best_group_min_max()
    param    Parameter from test_quick_select
    sel_tree Range tree generated by get_mm_tree

  DESCRIPTION
    Test whether a query can be computed via a QUICK_GROUP_MIN_MAX_SELECT.
    Queries computable via a QUICK_GROUP_MIN_MAX_SELECT must satisfy the
    following conditions:
    A) Table T has at least one compound index I of the form:
       I = <A_1, ...,A_k, [B_1,..., B_m], C, [D_1,...,D_n]>
    B) Query conditions:
    B0. Q is over a single table T.
    B1. The attributes referenced by Q are a subset of the attributes of I.
    B2. All attributes QA in Q can be divided into 3 overlapping groups:
        - SA = {S_1, ..., S_l, [C]} - from the SELECT clause, where C is
          referenced by any number of MIN and/or MAX functions if present.
        - WA = {W_1, ..., W_p} - from the WHERE clause
        - GA = <G_1, ..., G_k> - from the GROUP BY clause (if any)
             = SA              - if Q is a DISTINCT query (based on the
                                 equivalence of DISTINCT and GROUP queries.
        - NGA = QA - (GA union C) = {NG_1, ..., NG_m} - the ones not in
          GROUP BY and not referenced by MIN/MAX functions.
        with the following properties specified below.
    B3. If Q has a GROUP BY WITH ROLLUP clause the access method is not
        applicable.

    SA1. There is at most one attribute in SA referenced by any number of
         MIN and/or MAX functions which, which if present, is denoted as C.
    SA2. The position of the C attribute in the index is after the last A_k.
    SA3. The attribute C can be referenced in the WHERE clause only in
         predicates of the forms:
         - (C {< | <= | > | >= | =} const)
         - (const {< | <= | > | >= | =} C)
         - (C between const_i and const_j)
         - C IS NULL
         - C IS NOT NULL
         - C != const
    SA4. If Q has a GROUP BY clause, there are no other aggregate functions
         except MIN and MAX. For queries with DISTINCT, aggregate functions
         are allowed.
    SA5. The select list in DISTINCT queries should not contain expressions.
    GA1. If Q has a GROUP BY clause, then GA is a prefix of I. That is, if
         G_i = A_j => i = j.
    GA2. If Q has a DISTINCT clause, then there is a permutation of SA that
         forms a prefix of I. This permutation is used as the GROUP clause
         when the DISTINCT query is converted to a GROUP query.
    GA3. The attributes in GA may participate in arbitrary predicates, divided
         into two groups:
         - RNG(G_1,...,G_q ; where q <= k) is a range condition over the
           attributes of a prefix of GA
         - PA(G_i1,...G_iq) is an arbitrary predicate over an arbitrary subset
           of GA. Since P is applied to only GROUP attributes it filters some
           groups, and thus can be applied after the grouping.
    GA4. There are no expressions among G_i, just direct column references.
    NGA1.If in the index I there is a gap between the last GROUP attribute G_k,
         and the MIN/MAX attribute C, then NGA must consist of exactly the
         index attributes that constitute the gap. As a result there is a
         permutation of NGA that coincides with the gap in the index
         <B_1, ..., B_m>.
    NGA2.If BA <> {}, then the WHERE clause must contain a conjunction EQ of
         equality conditions for all NG_i of the form (NG_i = const) or
         (const = NG_i), such that each NG_i is referenced in exactly one
         conjunct. Informally, the predicates provide constants to fill the
         gap in the index.
    WA1. There are no other attributes in the WHERE clause except the ones
         referenced in predicates RNG, PA, PC, EQ defined above. Therefore
         WA is subset of (GA union NGA union C) for GA,NGA,C that pass the
         above tests. By transitivity then it also follows that each WA_i
         participates in the index I (if this was already tested for GA, NGA
         and C).

    C) Overall query form:
       SELECT EXPR([A_1,...,A_k], [B_1,...,B_m], [MIN(C)], [MAX(C)])
         FROM T
        WHERE [RNG(A_1,...,A_p ; where p <= k)]
         [AND EQ(B_1,...,B_m)]
         [AND PC(C)]
         [AND PA(A_i1,...,A_iq)]
       GROUP BY A_1,...,A_k
       [HAVING PH(A_1, ..., B_1,..., C)]
    where EXPR(...) is an arbitrary expression over some or all SELECT fields,
    or:
       SELECT DISTINCT A_i1,...,A_ik
         FROM T
        WHERE [RNG(A_1,...,A_p ; where p <= k)]
         [AND PA(A_i1,...,A_iq)];

  NOTES
    If the current query satisfies the conditions above, and if
    (mem_root! = NULL), then the function constructs and returns a new TRP
    object, that is later used to construct a new QUICK_GROUP_MIN_MAX_SELECT.
    If (mem_root == NULL), then the function only tests whether the current
    query satisfies the conditions above, and, if so, sets
    is_applicable = true.

    Queries with DISTINCT for which index access can be used are transformed
    into equivalent group-by queries of the form:

    SELECT A_1,...,A_k FROM T
     WHERE [RNG(A_1,...,A_p ; where p <= k)]
      [AND PA(A_i1,...,A_iq)]
    GROUP BY A_1,...,A_k;

    The group-by list is a permutation of the select attributes, according
    to their order in the index.

  TODO
  - What happens if the query groups by the MIN/MAX field, and there is no
    other field as in: "select min(a) from t1 group by a" ?
  - We assume that the general correctness of the GROUP-BY query was checked
    before this point. Is this correct, or do we have to check it completely?
  - Lift the limitation in condition (B3), that is, make this access method
    applicable to ROLLUP queries.

  RETURN
    If mem_root != NULL
    - valid TRP_GROUP_MIN_MAX object if this QUICK class can be used for
      the query
    -  NULL o/w.
    If mem_root == NULL
    - NULL
*/

static TRP_GROUP_MIN_MAX *
get_best_group_min_max(PARAM *param, SEL_TREE *tree)
{
  Session *session= param->session;
  JOIN *join= session->lex->current_select->join;
  Table *table= param->table;
  bool have_min= false;              /* true if there is a MIN function. */
  bool have_max= false;              /* true if there is a MAX function. */
  Item_field *min_max_arg_item= NULL; // The argument of all MIN/MAX functions
  KEY_PART_INFO *min_max_arg_part= NULL; /* The corresponding keypart. */
  uint32_t group_prefix_len= 0; /* Length (in bytes) of the key prefix. */
  KEY *index_info= NULL;    /* The index chosen for data access. */
  uint32_t index= 0;            /* The id of the chosen index. */
  uint32_t group_key_parts= 0;  // Number of index key parts in the group prefix.
  uint32_t used_key_parts= 0;   /* Number of index key parts used for access. */
  unsigned char key_infix[MAX_KEY_LENGTH]; /* Constants from equality predicates.*/
  uint32_t key_infix_len= 0;          /* Length of key_infix. */
  TRP_GROUP_MIN_MAX *read_plan= NULL; /* The eventually constructed TRP. */
  uint32_t key_part_nr;
  order_st *tmp_group;
  Item *item;
  Item_field *item_field;

  /* Perform few 'cheap' tests whether this access method is applicable. */
  if (!join)
    return NULL;        /* This is not a select statement. */
  if ((join->tables != 1) ||  /* The query must reference one table. */
      ((!join->group_list) && /* Neither GROUP BY nor a DISTINCT query. */
       (!join->select_distinct)) ||
      (join->select_lex->olap == ROLLUP_TYPE)) /* Check (B3) for ROLLUP */
    return NULL;
  if (table->s->keys == 0)        /* There are no indexes to use. */
    return NULL;

  /* Analyze the query in more detail. */
  List_iterator<Item> select_items_it(join->fields_list);

  /* Check (SA1,SA4) and store the only MIN/MAX argument - the C attribute.*/
  if (join->make_sum_func_list(join->all_fields, join->fields_list, 1))
    return NULL;
  if (join->sum_funcs[0])
  {
    Item_sum *min_max_item;
    Item_sum **func_ptr= join->sum_funcs;
    while ((min_max_item= *(func_ptr++)))
    {
      if (min_max_item->sum_func() == Item_sum::MIN_FUNC)
        have_min= true;
      else if (min_max_item->sum_func() == Item_sum::MAX_FUNC)
        have_max= true;
      else
        return NULL;

      /* The argument of MIN/MAX. */
      Item *expr= min_max_item->args[0]->real_item();
      if (expr->type() == Item::FIELD_ITEM) /* Is it an attribute? */
      {
        if (! min_max_arg_item)
          min_max_arg_item= (Item_field*) expr;
        else if (! min_max_arg_item->eq(expr, 1))
          return NULL;
      }
      else
        return NULL;
    }
  }

  /* Check (SA5). */
  if (join->select_distinct)
  {
    while ((item= select_items_it++))
    {
      if (item->type() != Item::FIELD_ITEM)
        return NULL;
    }
  }

  /* Check (GA4) - that there are no expressions among the group attributes. */
  for (tmp_group= join->group_list; tmp_group; tmp_group= tmp_group->next)
  {
    if ((*tmp_group->item)->type() != Item::FIELD_ITEM)
      return NULL;
  }

  /*
    Check that table has at least one compound index such that the conditions
    (GA1,GA2) are all true. If there is more than one such index, select the
    first one. Here we set the variables: group_prefix_len and index_info.
  */
  KEY *cur_index_info= table->key_info;
  KEY *cur_index_info_end= cur_index_info + table->s->keys;
  KEY_PART_INFO *cur_part= NULL;
  KEY_PART_INFO *end_part; /* Last part for loops. */
  /* Last index part. */
  KEY_PART_INFO *last_part= NULL;
  KEY_PART_INFO *first_non_group_part= NULL;
  KEY_PART_INFO *first_non_infix_part= NULL;
  uint32_t key_infix_parts= 0;
  uint32_t cur_group_key_parts= 0;
  uint32_t cur_group_prefix_len= 0;
  /* Cost-related variables for the best index so far. */
  double best_read_cost= DBL_MAX;
  ha_rows best_records= 0;
  SEL_ARG *best_index_tree= NULL;
  ha_rows best_quick_prefix_records= 0;
  uint32_t best_param_idx= 0;
  double cur_read_cost= DBL_MAX;
  ha_rows cur_records;
  SEL_ARG *cur_index_tree= NULL;
  ha_rows cur_quick_prefix_records= 0;
  uint32_t cur_param_idx=MAX_KEY;
  key_map used_key_parts_map;
  uint32_t cur_key_infix_len= 0;
  unsigned char cur_key_infix[MAX_KEY_LENGTH];
  uint32_t cur_used_key_parts= 0;
  uint32_t pk= param->table->s->primary_key;

  for (uint32_t cur_index= 0 ; cur_index_info != cur_index_info_end ;
       cur_index_info++, cur_index++)
  {
    /* Check (B1) - if current index is covering. */
    if (!table->covering_keys.test(cur_index))
      goto next_index;

    /*
      If the current storage manager is such that it appends the primary key to
      each index, then the above condition is insufficient to check if the
      index is covering. In such cases it may happen that some fields are
      covered by the PK index, but not by the current index. Since we can't
      use the concatenation of both indexes for index lookup, such an index
      does not qualify as covering in our case. If this is the case, below
      we check that all query fields are indeed covered by 'cur_index'.
    */
    if (pk < MAX_KEY && cur_index != pk &&
        (table->file->ha_table_flags() & HA_PRIMARY_KEY_IN_READ_INDEX))
    {
      /* For each table field */
      for (uint32_t i= 0; i < table->s->fields; i++)
      {
        Field *cur_field= table->field[i];
        /*
          If the field is used in the current query ensure that it's
          part of 'cur_index'
        */
        if ((cur_field->isReadSet()) &&
            !cur_field->part_of_key_not_clustered.test(cur_index))
          goto next_index;                  // Field was not part of key
      }
    }

    /*
      Check (GA1) for GROUP BY queries.
    */
    if (join->group_list)
    {
      cur_part= cur_index_info->key_part;
      end_part= cur_part + cur_index_info->key_parts;
      /* Iterate in parallel over the GROUP list and the index parts. */
      for (tmp_group= join->group_list; tmp_group && (cur_part != end_part);
           tmp_group= tmp_group->next, cur_part++)
      {
        /*
          TODO:
          tmp_group::item is an array of Item, is it OK to consider only the
          first Item? If so, then why? What is the array for?
        */
        /* Above we already checked that all group items are fields. */
        assert((*tmp_group->item)->type() == Item::FIELD_ITEM);
        Item_field *group_field= (Item_field *) (*tmp_group->item);
        if (group_field->field->eq(cur_part->field))
        {
          cur_group_prefix_len+= cur_part->store_length;
          ++cur_group_key_parts;
        }
        else
          goto next_index;
      }
    }
    /*
      Check (GA2) if this is a DISTINCT query.
      If GA2, then Store a new order_st object in group_fields_array at the
      position of the key part of item_field->field. Thus we get the order_st
      objects for each field ordered as the corresponding key parts.
      Later group_fields_array of order_st objects is used to convert the query
      to a GROUP query.
    */
    else if (join->select_distinct)
    {
      select_items_it.rewind();
      used_key_parts_map.reset();
      uint32_t max_key_part= 0;
      while ((item= select_items_it++))
      {
        item_field= (Item_field*) item; /* (SA5) already checked above. */
        /* Find the order of the key part in the index. */
        key_part_nr= get_field_keypart(cur_index_info, item_field->field);
        /*
          Check if this attribute was already present in the select list.
          If it was present, then its corresponding key part was alredy used.
        */
        if (used_key_parts_map.test(key_part_nr))
          continue;
        if (key_part_nr < 1 || key_part_nr > join->fields_list.elements)
          goto next_index;
        cur_part= cur_index_info->key_part + key_part_nr - 1;
        cur_group_prefix_len+= cur_part->store_length;
        used_key_parts_map.set(key_part_nr);
        ++cur_group_key_parts;
        max_key_part= max(max_key_part,key_part_nr);
      }
      /*
        Check that used key parts forms a prefix of the index.
        To check this we compare bits in all_parts and cur_parts.
        all_parts have all bits set from 0 to (max_key_part-1).
        cur_parts have bits set for only used keyparts.
      */
      key_map all_parts, cur_parts;
      for (uint32_t pos= 0; pos < max_key_part; pos++)
        all_parts.set(pos);
      cur_parts= used_key_parts_map >> 1;
      if (all_parts != cur_parts)
        goto next_index;
    }
    else
      assert(false);

    /* Check (SA2). */
    if (min_max_arg_item)
    {
      key_part_nr= get_field_keypart(cur_index_info, min_max_arg_item->field);
      if (key_part_nr <= cur_group_key_parts)
        goto next_index;
      min_max_arg_part= cur_index_info->key_part + key_part_nr - 1;
    }

    /*
      Check (NGA1, NGA2) and extract a sequence of constants to be used as part
      of all search keys.
    */

    /*
      If there is MIN/MAX, each keypart between the last group part and the
      MIN/MAX part must participate in one equality with constants, and all
      keyparts after the MIN/MAX part must not be referenced in the query.

      If there is no MIN/MAX, the keyparts after the last group part can be
      referenced only in equalities with constants, and the referenced keyparts
      must form a sequence without any gaps that starts immediately after the
      last group keypart.
    */
    last_part= cur_index_info->key_part + cur_index_info->key_parts;
    first_non_group_part= (cur_group_key_parts < cur_index_info->key_parts) ?
                          cur_index_info->key_part + cur_group_key_parts :
                          NULL;
    first_non_infix_part= min_max_arg_part ?
                          (min_max_arg_part < last_part) ?
                             min_max_arg_part :
                             NULL :
                           NULL;
    if (first_non_group_part &&
        (!min_max_arg_part || (min_max_arg_part - first_non_group_part > 0)))
    {
      if (tree)
      {
        uint32_t dummy;
        SEL_ARG *index_range_tree= get_index_range_tree(cur_index, tree, param,
                                                        &dummy);
        if (!get_constant_key_infix(cur_index_info, index_range_tree,
                                    first_non_group_part, min_max_arg_part,
                                    last_part, session, cur_key_infix, 
                                    &cur_key_infix_len,
                                    &first_non_infix_part))
          goto next_index;
      }
      else if (min_max_arg_part &&
               (min_max_arg_part - first_non_group_part > 0))
      {
        /*
          There is a gap but no range tree, thus no predicates at all for the
          non-group keyparts.
        */
        goto next_index;
      }
      else if (first_non_group_part && join->conds)
      {
        /*
          If there is no MIN/MAX function in the query, but some index
          key part is referenced in the WHERE clause, then this index
          cannot be used because the WHERE condition over the keypart's
          field cannot be 'pushed' to the index (because there is no
          range 'tree'), and the WHERE clause must be evaluated before
          GROUP BY/DISTINCT.
        */
        /*
          Store the first and last keyparts that need to be analyzed
          into one array that can be passed as parameter.
        */
        KEY_PART_INFO *key_part_range[2];
        key_part_range[0]= first_non_group_part;
        key_part_range[1]= last_part;

        /* Check if cur_part is referenced in the WHERE clause. */
        if (join->conds->walk(&Item::find_item_in_field_list_processor, 0,
                              (unsigned char*) key_part_range))
          goto next_index;
      }
    }

    /*
      Test (WA1) partially - that no other keypart after the last infix part is
      referenced in the query.
    */
    if (first_non_infix_part)
    {
      cur_part= first_non_infix_part +
                (min_max_arg_part && (min_max_arg_part < last_part));
      for (; cur_part != last_part; cur_part++)
      {
        if (cur_part->field->isReadSet())
          goto next_index;
      }
    }

    /* If we got to this point, cur_index_info passes the test. */
    key_infix_parts= cur_key_infix_len ?
                     (first_non_infix_part - first_non_group_part) : 0;
    cur_used_key_parts= cur_group_key_parts + key_infix_parts;

    /* Compute the cost of using this index. */
    if (tree)
    {
      /* Find the SEL_ARG sub-tree that corresponds to the chosen index. */
      cur_index_tree= get_index_range_tree(cur_index, tree, param,
                                           &cur_param_idx);
      /* Check if this range tree can be used for prefix retrieval. */
      COST_VECT dummy_cost;
      uint32_t mrr_flags= HA_MRR_USE_DEFAULT_IMPL;
      uint32_t mrr_bufsize=0;
      cur_quick_prefix_records= check_quick_select(param, cur_param_idx,
                                                   false /*don't care*/,
                                                   cur_index_tree, true,
                                                   &mrr_flags, &mrr_bufsize,
                                                   &dummy_cost);
    }
    cost_group_min_max(table, cur_index_info, cur_used_key_parts,
                       cur_group_key_parts, tree, cur_index_tree,
                       cur_quick_prefix_records, have_min, have_max,
                       &cur_read_cost, &cur_records);
    /*
      If cur_read_cost is lower than best_read_cost use cur_index.
      Do not compare doubles directly because they may have different
      representations (64 vs. 80 bits).
    */
    if (cur_read_cost < best_read_cost - (DBL_EPSILON * cur_read_cost))
    {
      assert(tree != 0 || cur_param_idx == MAX_KEY);
      index_info= cur_index_info;
      index= cur_index;
      best_read_cost= cur_read_cost;
      best_records= cur_records;
      best_index_tree= cur_index_tree;
      best_quick_prefix_records= cur_quick_prefix_records;
      best_param_idx= cur_param_idx;
      group_key_parts= cur_group_key_parts;
      group_prefix_len= cur_group_prefix_len;
      key_infix_len= cur_key_infix_len;
      if (key_infix_len)
        memcpy (key_infix, cur_key_infix, sizeof (key_infix));
      used_key_parts= cur_used_key_parts;
    }

  next_index:
    cur_group_key_parts= 0;
    cur_group_prefix_len= 0;
    cur_key_infix_len= 0;
  }
  if (!index_info) /* No usable index found. */
    return NULL;

  /* Check (SA3) for the where clause. */
  if (join->conds && min_max_arg_item &&
      ! check_group_min_max_predicates(join->conds, min_max_arg_item))
    return NULL;

  /* The query passes all tests, so construct a new TRP object. */
  read_plan= new (param->mem_root)
                 TRP_GROUP_MIN_MAX(have_min, have_max, min_max_arg_part,
                                   group_prefix_len, used_key_parts,
                                   group_key_parts, index_info, index,
                                   key_infix_len,
                                   (key_infix_len > 0) ? key_infix : NULL,
                                   tree, best_index_tree, best_param_idx,
                                   best_quick_prefix_records);
  if (read_plan)
  {
    if (tree && read_plan->quick_prefix_records == 0)
      return NULL;

    read_plan->read_cost= best_read_cost;
    read_plan->records=   best_records;
  }

  return read_plan;
}


/*
  Check that the MIN/MAX attribute participates only in range predicates
  with constants.

  SYNOPSIS
    check_group_min_max_predicates()
    cond              tree (or subtree) describing all or part of the WHERE
                      clause being analyzed
    min_max_arg_item  the field referenced by the MIN/MAX function(s)
    min_max_arg_part  the keypart of the MIN/MAX argument if any

  DESCRIPTION
    The function walks recursively over the cond tree representing a WHERE
    clause, and checks condition (SA3) - if a field is referenced by a MIN/MAX
    aggregate function, it is referenced only by one of the following
    predicates: {=, !=, <, <=, >, >=, between, is null, is not null}.

  RETURN
    true  if cond passes the test
    false o/w
*/
static bool check_group_min_max_predicates(COND *cond, Item_field *min_max_arg_item)
{
  assert(cond && min_max_arg_item);

  cond= cond->real_item();
  Item::Type cond_type= cond->type();
  if (cond_type == Item::COND_ITEM) /* 'AND' or 'OR' */
  {
    List_iterator_fast<Item> li(*((Item_cond*) cond)->argument_list());
    Item *and_or_arg;
    while ((and_or_arg= li++))
    {
      if (!check_group_min_max_predicates(and_or_arg, min_max_arg_item))
        return false;
    }
    return true;
  }

  /*
    TODO:
    This is a very crude fix to handle sub-selects in the WHERE clause
    (Item_subselect objects). With the test below we rule out from the
    optimization all queries with subselects in the WHERE clause. What has to
    be done, is that here we should analyze whether the subselect references
    the MIN/MAX argument field, and disallow the optimization only if this is
    so.
  */
  if (cond_type == Item::SUBSELECT_ITEM)
    return false;

  /* We presume that at this point there are no other Items than functions. */
  assert(cond_type == Item::FUNC_ITEM);

  /* Test if cond references only group-by or non-group fields. */
  Item_func *pred= (Item_func*) cond;
  Item **arguments= pred->arguments();
  Item *cur_arg;
  for (uint32_t arg_idx= 0; arg_idx < pred->argument_count (); arg_idx++)
  {
    cur_arg= arguments[arg_idx]->real_item();
    if (cur_arg->type() == Item::FIELD_ITEM)
    {
      if (min_max_arg_item->eq(cur_arg, 1))
      {
       /*
         If pred references the MIN/MAX argument, check whether pred is a range
         condition that compares the MIN/MAX argument with a constant.
       */
        Item_func::Functype pred_type= pred->functype();
        if (pred_type != Item_func::EQUAL_FUNC     &&
            pred_type != Item_func::LT_FUNC        &&
            pred_type != Item_func::LE_FUNC        &&
            pred_type != Item_func::GT_FUNC        &&
            pred_type != Item_func::GE_FUNC        &&
            pred_type != Item_func::BETWEEN        &&
            pred_type != Item_func::ISNULL_FUNC    &&
            pred_type != Item_func::ISNOTNULL_FUNC &&
            pred_type != Item_func::EQ_FUNC        &&
            pred_type != Item_func::NE_FUNC)
          return false;

        /* Check that pred compares min_max_arg_item with a constant. */
        Item *args[3];
        memset(args, 0, 3 * sizeof(Item*));
        bool inv;
        /* Test if this is a comparison of a field and a constant. */
        if (!simple_pred(pred, args, &inv))
          return false;

        /* Check for compatible string comparisons - similar to get_mm_leaf. */
        if (args[0] && args[1] && !args[2] && // this is a binary function
            min_max_arg_item->result_type() == STRING_RESULT &&
            /*
              Don't use an index when comparing strings of different collations.
            */
            ((args[1]->result_type() == STRING_RESULT &&
              ((Field_str*) min_max_arg_item->field)->charset() !=
              pred->compare_collation())
             ||
             /*
               We can't always use indexes when comparing a string index to a
               number.
             */
             (args[1]->result_type() != STRING_RESULT &&
              min_max_arg_item->field->cmp_type() != args[1]->result_type())))
          return false;
      }
    }
    else if (cur_arg->type() == Item::FUNC_ITEM)
    {
      if (! check_group_min_max_predicates(cur_arg, min_max_arg_item))
        return false;
    }
    else if (cur_arg->const_item())
    {
      return true;
    }
    else
      return false;
  }

  return true;
}


/*
  Extract a sequence of constants from a conjunction of equality predicates.

  SYNOPSIS
    get_constant_key_infix()
    index_info             [in]  Descriptor of the chosen index.
    index_range_tree       [in]  Range tree for the chosen index
    first_non_group_part   [in]  First index part after group attribute parts
    min_max_arg_part       [in]  The keypart of the MIN/MAX argument if any
    last_part              [in]  Last keypart of the index
    session                    [in]  Current thread
    key_infix              [out] Infix of constants to be used for index lookup
    key_infix_len          [out] Lenghth of the infix
    first_non_infix_part   [out] The first keypart after the infix (if any)

  DESCRIPTION
    Test conditions (NGA1, NGA2) from get_best_group_min_max(). Namely,
    for each keypart field NGF_i not in GROUP-BY, check that there is a
    constant equality predicate among conds with the form (NGF_i = const_ci) or
    (const_ci = NGF_i).
    Thus all the NGF_i attributes must fill the 'gap' between the last group-by
    attribute and the MIN/MAX attribute in the index (if present). If these
    conditions hold, copy each constant from its corresponding predicate into
    key_infix, in the order its NG_i attribute appears in the index, and update
    key_infix_len with the total length of the key parts in key_infix.

  RETURN
    true  if the index passes the test
    false o/w
*/

static bool
get_constant_key_infix(KEY *, SEL_ARG *index_range_tree,
                       KEY_PART_INFO *first_non_group_part,
                       KEY_PART_INFO *min_max_arg_part,
                       KEY_PART_INFO *last_part,
                       Session *, unsigned char *key_infix, uint32_t *key_infix_len,
                       KEY_PART_INFO **first_non_infix_part)
{
  SEL_ARG       *cur_range;
  KEY_PART_INFO *cur_part;
  /* End part for the first loop below. */
  KEY_PART_INFO *end_part= min_max_arg_part ? min_max_arg_part : last_part;

  *key_infix_len= 0;
  unsigned char *key_ptr= key_infix;
  for (cur_part= first_non_group_part; cur_part != end_part; cur_part++)
  {
    /*
      Find the range tree for the current keypart. We assume that
      index_range_tree points to the leftmost keypart in the index.
    */
    for (cur_range= index_range_tree; cur_range;
         cur_range= cur_range->next_key_part)
    {
      if (cur_range->field->eq(cur_part->field))
        break;
    }
    if (!cur_range)
    {
      if (min_max_arg_part)
        return false; /* The current keypart has no range predicates at all. */
      else
      {
        *first_non_infix_part= cur_part;
        return true;
      }
    }

    /* Check that the current range tree is a single point interval. */
    if (cur_range->prev || cur_range->next)
      return false; /* This is not the only range predicate for the field. */
    if ((cur_range->min_flag & NO_MIN_RANGE) ||
        (cur_range->max_flag & NO_MAX_RANGE) ||
        (cur_range->min_flag & NEAR_MIN) || (cur_range->max_flag & NEAR_MAX))
      return false;

    uint32_t field_length= cur_part->store_length;
    if ((cur_range->maybe_null &&
         cur_range->min_value[0] && cur_range->max_value[0]) ||
        !memcmp(cur_range->min_value, cur_range->max_value, field_length))
    {
      /* cur_range specifies 'IS NULL' or an equality condition. */
      memcpy(key_ptr, cur_range->min_value, field_length);
      key_ptr+= field_length;
      *key_infix_len+= field_length;
    }
    else
      return false;
  }

  if (!min_max_arg_part && (cur_part == last_part))
    *first_non_infix_part= last_part;

  return true;
}


/*
  Find the key part referenced by a field.

  SYNOPSIS
    get_field_keypart()
    index  descriptor of an index
    field  field that possibly references some key part in index

  NOTES
    The return value can be used to get a KEY_PART_INFO pointer by
    part= index->key_part + get_field_keypart(...) - 1;

  RETURN
    Positive number which is the consecutive number of the key part, or
    0 if field does not reference any index field.
*/

static inline uint
get_field_keypart(KEY *index, Field *field)
{
  KEY_PART_INFO *part, *end;

  for (part= index->key_part, end= part + index->key_parts; part < end; part++)
  {
    if (field->eq(part->field))
      return part - index->key_part + 1;
  }
  return 0;
}


/*
  Find the SEL_ARG sub-tree that corresponds to the chosen index.

  SYNOPSIS
    get_index_range_tree()
    index     [in]  The ID of the index being looked for
    range_tree[in]  Tree of ranges being searched
    param     [in]  PARAM from SQL_SELECT::test_quick_select
    param_idx [out] Index in the array PARAM::key that corresponds to 'index'

  DESCRIPTION

    A SEL_TREE contains range trees for all usable indexes. This procedure
    finds the SEL_ARG sub-tree for 'index'. The members of a SEL_TREE are
    ordered in the same way as the members of PARAM::key, thus we first find
    the corresponding index in the array PARAM::key. This index is returned
    through the variable param_idx, to be used later as argument of
    check_quick_select().

  RETURN
    Pointer to the SEL_ARG subtree that corresponds to index.
*/

SEL_ARG * get_index_range_tree(uint32_t index, SEL_TREE* range_tree, PARAM *param,
                               uint32_t *param_idx)
{
  uint32_t idx= 0; /* Index nr in param->key_parts */
  while (idx < param->keys)
  {
    if (index == param->real_keynr[idx])
      break;
    idx++;
  }
  *param_idx= idx;
  return range_tree->keys[idx];
}


/*
  Compute the cost of a quick_group_min_max_select for a particular index.

  SYNOPSIS
    cost_group_min_max()
    table                [in] The table being accessed
    index_info           [in] The index used to access the table
    used_key_parts       [in] Number of key parts used to access the index
    group_key_parts      [in] Number of index key parts in the group prefix
    range_tree           [in] Tree of ranges for all indexes
    index_tree           [in] The range tree for the current index
    quick_prefix_records [in] Number of records retrieved by the internally
			      used quick range select if any
    have_min             [in] True if there is a MIN function
    have_max             [in] True if there is a MAX function
    read_cost           [out] The cost to retrieve rows via this quick select
    records             [out] The number of rows retrieved

  DESCRIPTION
    This method computes the access cost of a TRP_GROUP_MIN_MAX instance and
    the number of rows returned. It updates this->read_cost and this->records.

  NOTES
    The cost computation distinguishes several cases:
    1) No equality predicates over non-group attributes (thus no key_infix).
       If groups are bigger than blocks on the average, then we assume that it
       is very unlikely that block ends are aligned with group ends, thus even
       if we look for both MIN and MAX keys, all pairs of neighbor MIN/MAX
       keys, except for the first MIN and the last MAX keys, will be in the
       same block.  If groups are smaller than blocks, then we are going to
       read all blocks.
    2) There are equality predicates over non-group attributes.
       In this case the group prefix is extended by additional constants, and
       as a result the min/max values are inside sub-groups of the original
       groups. The number of blocks that will be read depends on whether the
       ends of these sub-groups will be contained in the same or in different
       blocks. We compute the probability for the two ends of a subgroup to be
       in two different blocks as the ratio of:
       - the number of positions of the left-end of a subgroup inside a group,
         such that the right end of the subgroup is past the end of the buffer
         containing the left-end, and
       - the total number of possible positions for the left-end of the
         subgroup, which is the number of keys in the containing group.
       We assume it is very unlikely that two ends of subsequent subgroups are
       in the same block.
    3) The are range predicates over the group attributes.
       Then some groups may be filtered by the range predicates. We use the
       selectivity of the range predicates to decide how many groups will be
       filtered.

  TODO
     - Take into account the optional range predicates over the MIN/MAX
       argument.
     - Check if we have a PK index and we use all cols - then each key is a
       group, and it will be better to use an index scan.

  RETURN
    None
*/

void cost_group_min_max(Table* table, KEY *index_info, uint32_t used_key_parts,
                        uint32_t group_key_parts, SEL_TREE *range_tree,
                        SEL_ARG *, ha_rows quick_prefix_records,
                        bool have_min, bool have_max,
                        double *read_cost, ha_rows *records)
{
  ha_rows table_records;
  uint32_t num_groups;
  uint32_t num_blocks;
  uint32_t keys_per_block;
  uint32_t keys_per_group;
  uint32_t keys_per_subgroup; /* Average number of keys in sub-groups */
                          /* formed by a key infix. */
  double p_overlap; /* Probability that a sub-group overlaps two blocks. */
  double quick_prefix_selectivity;
  double io_cost;
  double cpu_cost= 0; /* TODO: CPU cost of index_read calls? */

  table_records= table->file->stats.records;
  keys_per_block= (table->file->stats.block_size / 2 /
                   (index_info->key_length + table->file->ref_length)
                        + 1);
  num_blocks= (uint32_t)(table_records / keys_per_block) + 1;

  /* Compute the number of keys in a group. */
  keys_per_group= index_info->rec_per_key[group_key_parts - 1];
  if (keys_per_group == 0) /* If there is no statistics try to guess */
    /* each group contains 10% of all records */
    keys_per_group= (uint32_t)(table_records / 10) + 1;
  num_groups= (uint32_t)(table_records / keys_per_group) + 1;

  /* Apply the selectivity of the quick select for group prefixes. */
  if (range_tree && (quick_prefix_records != HA_POS_ERROR))
  {
    quick_prefix_selectivity= (double) quick_prefix_records /
                              (double) table_records;
    num_groups= (uint32_t) rint(num_groups * quick_prefix_selectivity);
    set_if_bigger(num_groups, 1U);
  }

  if (used_key_parts > group_key_parts)
  { /*
      Compute the probability that two ends of a subgroup are inside
      different blocks.
    */
    keys_per_subgroup= index_info->rec_per_key[used_key_parts - 1];
    if (keys_per_subgroup >= keys_per_block) /* If a subgroup is bigger than */
      p_overlap= 1.0;       /* a block, it will overlap at least two blocks. */
    else
    {
      double blocks_per_group= (double) num_blocks / (double) num_groups;
      p_overlap= (blocks_per_group * (keys_per_subgroup - 1)) / keys_per_group;
      p_overlap= min(p_overlap, 1.0);
    }
    io_cost= (double) min(num_groups * (1 + p_overlap), (double)num_blocks);
  }
  else
    io_cost= (keys_per_group > keys_per_block) ?
             (have_min && have_max) ? (double) (num_groups + 1) :
                                      (double) num_groups :
             (double) num_blocks;

  /*
    TODO: If there is no WHERE clause and no other expressions, there should be
    no CPU cost. We leave it here to make this cost comparable to that of index
    scan as computed in SQL_SELECT::test_quick_select().
  */
  cpu_cost= (double) num_groups / TIME_FOR_COMPARE;

  *read_cost= io_cost + cpu_cost;
  *records= num_groups;
}


/*
  Construct a new quick select object for queries with group by with min/max.

  SYNOPSIS
    TRP_GROUP_MIN_MAX::make_quick()
    param              Parameter from test_quick_select
    retrieve_full_rows ignored
    parent_alloc       Memory pool to use, if any.

  NOTES
    Make_quick ignores the retrieve_full_rows parameter because
    QUICK_GROUP_MIN_MAX_SELECT always performs 'index only' scans.
    The other parameter are ignored as well because all necessary
    data to create the QUICK object is computed at this TRP creation
    time.

  RETURN
    New QUICK_GROUP_MIN_MAX_SELECT object if successfully created,
    NULL otherwise.
*/

QUICK_SELECT_I *
TRP_GROUP_MIN_MAX::make_quick(PARAM *param, bool, MEM_ROOT *parent_alloc)
{
  QUICK_GROUP_MIN_MAX_SELECT *quick;

  quick= new QUICK_GROUP_MIN_MAX_SELECT(param->table,
                                        param->session->lex->current_select->join,
                                        have_min, have_max, min_max_arg_part,
                                        group_prefix_len, group_key_parts,
                                        used_key_parts, index_info, index,
                                        read_cost, records, key_infix_len,
                                        key_infix, parent_alloc);
  if (!quick)
    return NULL;

  if (quick->init())
  {
    delete quick;
    return NULL;
  }

  if (range_tree)
  {
    assert(quick_prefix_records > 0);
    if (quick_prefix_records == HA_POS_ERROR)
      quick->quick_prefix_select= NULL; /* Can't construct a quick select. */
    else
      /* Make a QUICK_RANGE_SELECT to be used for group prefix retrieval. */
      quick->quick_prefix_select= get_quick_select(param, param_idx,
                                                   index_tree,
                                                   HA_MRR_USE_DEFAULT_IMPL, 0,
                                                   &quick->alloc);

    /*
      Extract the SEL_ARG subtree that contains only ranges for the MIN/MAX
      attribute, and create an array of QUICK_RANGES to be used by the
      new quick select.
    */
    if (min_max_arg_part)
    {
      SEL_ARG *min_max_range= index_tree;
      while (min_max_range) /* Find the tree for the MIN/MAX key part. */
      {
        if (min_max_range->field->eq(min_max_arg_part->field))
          break;
        min_max_range= min_max_range->next_key_part;
      }
      /* Scroll to the leftmost interval for the MIN/MAX argument. */
      while (min_max_range && min_max_range->prev)
        min_max_range= min_max_range->prev;
      /* Create an array of QUICK_RANGEs for the MIN/MAX argument. */
      while (min_max_range)
      {
        if (quick->add_range(min_max_range))
        {
          delete quick;
          quick= NULL;
          return NULL;
        }
        min_max_range= min_max_range->next;
      }
    }
  }
  else
    quick->quick_prefix_select= NULL;

  quick->update_key_stat();
  quick->adjust_prefix_ranges();

  return quick;
}


/*
  Construct new quick select for group queries with min/max.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::QUICK_GROUP_MIN_MAX_SELECT()
    table             The table being accessed
    join              Descriptor of the current query
    have_min          true if the query selects a MIN function
    have_max          true if the query selects a MAX function
    min_max_arg_part  The only argument field of all MIN/MAX functions
    group_prefix_len  Length of all key parts in the group prefix
    prefix_key_parts  All key parts in the group prefix
    index_info        The index chosen for data access
    use_index         The id of index_info
    read_cost         Cost of this access method
    records           Number of records returned
    key_infix_len     Length of the key infix appended to the group prefix
    key_infix         Infix of constants from equality predicates
    parent_alloc      Memory pool for this and quick_prefix_select data

  RETURN
    None
*/

QUICK_GROUP_MIN_MAX_SELECT::
QUICK_GROUP_MIN_MAX_SELECT(Table *table, JOIN *join_arg, bool have_min_arg,
                           bool have_max_arg,
                           KEY_PART_INFO *min_max_arg_part_arg,
                           uint32_t group_prefix_len_arg, uint32_t group_key_parts_arg,
                           uint32_t used_key_parts_arg, KEY *index_info_arg,
                           uint32_t use_index, double read_cost_arg,
                           ha_rows records_arg, uint32_t key_infix_len_arg,
                           unsigned char *key_infix_arg, MEM_ROOT *parent_alloc)
  :join(join_arg), index_info(index_info_arg),
   group_prefix_len(group_prefix_len_arg),
   group_key_parts(group_key_parts_arg), have_min(have_min_arg),
   have_max(have_max_arg), seen_first_key(false),
   min_max_arg_part(min_max_arg_part_arg), key_infix(key_infix_arg),
   key_infix_len(key_infix_len_arg), min_functions_it(NULL),
   max_functions_it(NULL)
{
  head=       table;
  file=       head->file;
  index=      use_index;
  record=     head->record[0];
  tmp_record= head->record[1];
  read_time= read_cost_arg;
  records= records_arg;
  used_key_parts= used_key_parts_arg;
  real_key_parts= used_key_parts_arg;
  real_prefix_len= group_prefix_len + key_infix_len;
  group_prefix= NULL;
  min_max_arg_len= min_max_arg_part ? min_max_arg_part->store_length : 0;

  /*
    We can't have parent_alloc set as the init function can't handle this case
    yet.
  */
  assert(!parent_alloc);
  if (!parent_alloc)
  {
    init_sql_alloc(&alloc, join->session->variables.range_alloc_block_size, 0);
    join->session->mem_root= &alloc;
  }
  else
    memset(&alloc, 0, sizeof(MEM_ROOT));  // ensure that it's not used
}


/*
  Do post-constructor initialization.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::init()

  DESCRIPTION
    The method performs initialization that cannot be done in the constructor
    such as memory allocations that may fail. It allocates memory for the
    group prefix and inifix buffers, and for the lists of MIN/MAX item to be
    updated during execution.

  RETURN
    0      OK
    other  Error code
*/

int QUICK_GROUP_MIN_MAX_SELECT::init()
{
  if (group_prefix) /* Already initialized. */
    return 0;

  if (!(last_prefix= (unsigned char*) alloc_root(&alloc, group_prefix_len)))
      return 1;
  /*
    We may use group_prefix to store keys with all select fields, so allocate
    enough space for it.
  */
  if (!(group_prefix= (unsigned char*) alloc_root(&alloc,
                                         real_prefix_len + min_max_arg_len)))
    return 1;

  if (key_infix_len > 0)
  {
    /*
      The memory location pointed to by key_infix will be deleted soon, so
      allocate a new buffer and copy the key_infix into it.
    */
    unsigned char *tmp_key_infix= (unsigned char*) alloc_root(&alloc, key_infix_len);
    if (!tmp_key_infix)
      return 1;
    memcpy(tmp_key_infix, this->key_infix, key_infix_len);
    this->key_infix= tmp_key_infix;
  }

  if (min_max_arg_part)
  {
    if (my_init_dynamic_array(&min_max_ranges, sizeof(QUICK_RANGE*), 16, 16))
      return 1;

    if (have_min)
    {
      if (!(min_functions= new List<Item_sum>))
        return 1;
    }
    else
      min_functions= NULL;
    if (have_max)
    {
      if (!(max_functions= new List<Item_sum>))
        return 1;
    }
    else
      max_functions= NULL;

    Item_sum *min_max_item;
    Item_sum **func_ptr= join->sum_funcs;
    while ((min_max_item= *(func_ptr++)))
    {
      if (have_min && (min_max_item->sum_func() == Item_sum::MIN_FUNC))
        min_functions->push_back(min_max_item);
      else if (have_max && (min_max_item->sum_func() == Item_sum::MAX_FUNC))
        max_functions->push_back(min_max_item);
    }

    if (have_min)
    {
      if (!(min_functions_it= new List_iterator<Item_sum>(*min_functions)))
        return 1;
    }

    if (have_max)
    {
      if (!(max_functions_it= new List_iterator<Item_sum>(*max_functions)))
        return 1;
    }
  }
  else
    min_max_ranges.elements= 0;

  return 0;
}


QUICK_GROUP_MIN_MAX_SELECT::~QUICK_GROUP_MIN_MAX_SELECT()
{
  if (file->inited != handler::NONE)
    file->ha_index_end();
  if (min_max_arg_part)
    delete_dynamic(&min_max_ranges);
  free_root(&alloc,MYF(0));
  delete min_functions_it;
  delete max_functions_it;
  delete quick_prefix_select;
}


/*
  Eventually create and add a new quick range object.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::add_range()
    sel_range  Range object from which a

  NOTES
    Construct a new QUICK_RANGE object from a SEL_ARG object, and
    add it to the array min_max_ranges. If sel_arg is an infinite
    range, e.g. (x < 5 or x > 4), then skip it and do not construct
    a quick range.

  RETURN
    false on success
    true  otherwise
*/

bool QUICK_GROUP_MIN_MAX_SELECT::add_range(SEL_ARG *sel_range)
{
  QUICK_RANGE *range;
  uint32_t range_flag= sel_range->min_flag | sel_range->max_flag;

  /* Skip (-inf,+inf) ranges, e.g. (x < 5 or x > 4). */
  if ((range_flag & NO_MIN_RANGE) && (range_flag & NO_MAX_RANGE))
    return false;

  if (!(sel_range->min_flag & NO_MIN_RANGE) &&
      !(sel_range->max_flag & NO_MAX_RANGE))
  {
    if (sel_range->maybe_null &&
        sel_range->min_value[0] && sel_range->max_value[0])
      range_flag|= NULL_RANGE; /* IS NULL condition */
    else if (memcmp(sel_range->min_value, sel_range->max_value,
                    min_max_arg_len) == 0)
      range_flag|= EQ_RANGE;  /* equality condition */
  }
  range= new QUICK_RANGE(sel_range->min_value, min_max_arg_len,
                         make_keypart_map(sel_range->part),
                         sel_range->max_value, min_max_arg_len,
                         make_keypart_map(sel_range->part),
                         range_flag);
  if (!range)
    return true;
  if (insert_dynamic(&min_max_ranges, (unsigned char*)&range))
    return true;
  return false;
}


/*
  Opens the ranges if there are more conditions in quick_prefix_select than
  the ones used for jumping through the prefixes.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::adjust_prefix_ranges()

  NOTES
    quick_prefix_select is made over the conditions on the whole key.
    It defines a number of ranges of length x.
    However when jumping through the prefixes we use only the the first
    few most significant keyparts in the range key. However if there
    are more keyparts to follow the ones we are using we must make the
    condition on the key inclusive (because x < "ab" means
    x[0] < 'a' OR (x[0] == 'a' AND x[1] < 'b').
    To achive the above we must turn off the NEAR_MIN/NEAR_MAX
*/
void QUICK_GROUP_MIN_MAX_SELECT::adjust_prefix_ranges ()
{
  if (quick_prefix_select &&
      group_prefix_len < quick_prefix_select->max_used_key_length)
  {
    DYNAMIC_ARRAY *arr;
    uint32_t inx;

    for (inx= 0, arr= &quick_prefix_select->ranges; inx < arr->elements; inx++)
    {
      QUICK_RANGE *range;

      get_dynamic(arr, (unsigned char*)&range, inx);
      range->flag &= ~(NEAR_MIN | NEAR_MAX);
    }
  }
}


/*
  Determine the total number and length of the keys that will be used for
  index lookup.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::update_key_stat()

  DESCRIPTION
    The total length of the keys used for index lookup depends on whether
    there are any predicates referencing the min/max argument, and/or if
    the min/max argument field can be NULL.
    This function does an optimistic analysis whether the search key might
    be extended by a constant for the min/max keypart. It is 'optimistic'
    because during actual execution it may happen that a particular range
    is skipped, and then a shorter key will be used. However this is data
    dependent and can't be easily estimated here.

  RETURN
    None
*/

void QUICK_GROUP_MIN_MAX_SELECT::update_key_stat()
{
  max_used_key_length= real_prefix_len;
  if (min_max_ranges.elements > 0)
  {
    QUICK_RANGE *cur_range;
    if (have_min)
    { /* Check if the right-most range has a lower boundary. */
      get_dynamic(&min_max_ranges, (unsigned char*)&cur_range,
                  min_max_ranges.elements - 1);
      if (!(cur_range->flag & NO_MIN_RANGE))
      {
        max_used_key_length+= min_max_arg_len;
        used_key_parts++;
        return;
      }
    }
    if (have_max)
    { /* Check if the left-most range has an upper boundary. */
      get_dynamic(&min_max_ranges, (unsigned char*)&cur_range, 0);
      if (!(cur_range->flag & NO_MAX_RANGE))
      {
        max_used_key_length+= min_max_arg_len;
        used_key_parts++;
        return;
      }
    }
  }
  else if (have_min && min_max_arg_part &&
           min_max_arg_part->field->real_maybe_null())
  {
    /*
      If a MIN/MAX argument value is NULL, we can quickly determine
      that we're in the beginning of the next group, because NULLs
      are always < any other value. This allows us to quickly
      determine the end of the current group and jump to the next
      group (see next_min()) and thus effectively increases the
      usable key length.
    */
    max_used_key_length+= min_max_arg_len;
    used_key_parts++;
  }
}


/*
  Initialize a quick group min/max select for key retrieval.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::reset()

  DESCRIPTION
    Initialize the index chosen for access and find and store the prefix
    of the last group. The method is expensive since it performs disk access.

  RETURN
    0      OK
    other  Error code
*/

int QUICK_GROUP_MIN_MAX_SELECT::reset(void)
{
  int result;

  file->extra(HA_EXTRA_KEYREAD); /* We need only the key attributes */
  if ((result= file->ha_index_init(index,1)))
    return result;
  if (quick_prefix_select && quick_prefix_select->reset())
    return 0;
  result= file->index_last(record);
  if (result == HA_ERR_END_OF_FILE)
    return 0;
  /* Save the prefix of the last group. */
  key_copy(last_prefix, record, index_info, group_prefix_len);

  return 0;
}



/*
  Get the next key containing the MIN and/or MAX key for the next group.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::get_next()

  DESCRIPTION
    The method finds the next subsequent group of records that satisfies the
    query conditions and finds the keys that contain the MIN/MAX values for
    the key part referenced by the MIN/MAX function(s). Once a group and its
    MIN/MAX values are found, store these values in the Item_sum objects for
    the MIN/MAX functions. The rest of the values in the result row are stored
    in the Item_field::result_field of each select field. If the query does
    not contain MIN and/or MAX functions, then the function only finds the
    group prefix, which is a query answer itself.

  NOTES
    If both MIN and MAX are computed, then we use the fact that if there is
    no MIN key, there can't be a MAX key as well, so we can skip looking
    for a MAX key in this case.

  RETURN
    0                  on success
    HA_ERR_END_OF_FILE if returned all keys
    other              if some error occurred
*/

int QUICK_GROUP_MIN_MAX_SELECT::get_next()
{
  int min_res= 0;
  int max_res= 0;
  int result;
  int is_last_prefix= 0;

  /*
    Loop until a group is found that satisfies all query conditions or the last
    group is reached.
  */
  do
  {
    result= next_prefix();
    /*
      Check if this is the last group prefix. Notice that at this point
      this->record contains the current prefix in record format.
    */
    if (!result)
    {
      is_last_prefix= key_cmp(index_info->key_part, last_prefix,
                              group_prefix_len);
      assert(is_last_prefix <= 0);
    }
    else
    {
      if (result == HA_ERR_KEY_NOT_FOUND)
        continue;
      break;
    }

    if (have_min)
    {
      min_res= next_min();
      if (min_res == 0)
        update_min_result();
    }
    /* If there is no MIN in the group, there is no MAX either. */
    if ((have_max && !have_min) ||
        (have_max && have_min && (min_res == 0)))
    {
      max_res= next_max();
      if (max_res == 0)
        update_max_result();
      /* If a MIN was found, a MAX must have been found as well. */
      assert(((have_max && !have_min) ||
                  (have_max && have_min && (max_res == 0))));
    }
    /*
      If this is just a GROUP BY or DISTINCT without MIN or MAX and there
      are equality predicates for the key parts after the group, find the
      first sub-group with the extended prefix.
    */
    if (!have_min && !have_max && key_infix_len > 0)
      result= file->index_read_map(record, group_prefix,
                                   make_prev_keypart_map(real_key_parts),
                                   HA_READ_KEY_EXACT);

    result= have_min ? min_res : have_max ? max_res : result;
  } while ((result == HA_ERR_KEY_NOT_FOUND || result == HA_ERR_END_OF_FILE) &&
           is_last_prefix != 0);

  if (result == 0)
  {
    /*
      Partially mimic the behavior of end_select_send. Copy the
      field data from Item_field::field into Item_field::result_field
      of each non-aggregated field (the group fields, and optionally
      other fields in non-ANSI SQL mode).
    */
    copy_fields(&join->tmp_table_param);
  }
  else if (result == HA_ERR_KEY_NOT_FOUND)
    result= HA_ERR_END_OF_FILE;

  return result;
}


/*
  Retrieve the minimal key in the next group.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::next_min()

  DESCRIPTION
    Find the minimal key within this group such that the key satisfies the query
    conditions and NULL semantics. The found key is loaded into this->record.

  IMPLEMENTATION
    Depending on the values of min_max_ranges.elements, key_infix_len, and
    whether there is a  NULL in the MIN field, this function may directly
    return without any data access. In this case we use the key loaded into
    this->record by the call to this->next_prefix() just before this call.

  RETURN
    0                    on success
    HA_ERR_KEY_NOT_FOUND if no MIN key was found that fulfills all conditions.
    HA_ERR_END_OF_FILE   - "" -
    other                if some error occurred
*/

int QUICK_GROUP_MIN_MAX_SELECT::next_min()
{
  int result= 0;

  /* Find the MIN key using the eventually extended group prefix. */
  if (min_max_ranges.elements > 0)
  {
    if ((result= next_min_in_range()))
      return result;
  }
  else
  {
    /* Apply the constant equality conditions to the non-group select fields */
    if (key_infix_len > 0)
    {
      if ((result= file->index_read_map(record, group_prefix,
                                        make_prev_keypart_map(real_key_parts),
                                        HA_READ_KEY_EXACT)))
        return result;
    }

    /*
      If the min/max argument field is NULL, skip subsequent rows in the same
      group with NULL in it. Notice that:
      - if the first row in a group doesn't have a NULL in the field, no row
      in the same group has (because NULL < any other value),
      - min_max_arg_part->field->ptr points to some place in 'record'.
    */
    if (min_max_arg_part && min_max_arg_part->field->is_null())
    {
      /* Find the first subsequent record without NULL in the MIN/MAX field. */
      key_copy(tmp_record, record, index_info, 0);
      result= file->index_read_map(record, tmp_record,
                                   make_keypart_map(real_key_parts),
                                   HA_READ_AFTER_KEY);
      /*
        Check if the new record belongs to the current group by comparing its
        prefix with the group's prefix. If it is from the next group, then the
        whole group has NULLs in the MIN/MAX field, so use the first record in
        the group as a result.
        TODO:
        It is possible to reuse this new record as the result candidate for the
        next call to next_min(), and to save one lookup in the next call. For
        this add a new member 'this->next_group_prefix'.
      */
      if (!result)
      {
        if (key_cmp(index_info->key_part, group_prefix, real_prefix_len))
          key_restore(record, tmp_record, index_info, 0);
      }
      else if (result == HA_ERR_KEY_NOT_FOUND || result == HA_ERR_END_OF_FILE)
        result= 0; /* There is a result in any case. */
    }
  }

  /*
    If the MIN attribute is non-nullable, this->record already contains the
    MIN key in the group, so just return.
  */
  return result;
}


/*
  Retrieve the maximal key in the next group.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::next_max()

  DESCRIPTION
    Lookup the maximal key of the group, and store it into this->record.

  RETURN
    0                    on success
    HA_ERR_KEY_NOT_FOUND if no MAX key was found that fulfills all conditions.
    HA_ERR_END_OF_FILE	 - "" -
    other                if some error occurred
*/

int QUICK_GROUP_MIN_MAX_SELECT::next_max()
{
  int result;

  /* Get the last key in the (possibly extended) group. */
  if (min_max_ranges.elements > 0)
    result= next_max_in_range();
  else
    result= file->index_read_map(record, group_prefix,
                                 make_prev_keypart_map(real_key_parts),
                                 HA_READ_PREFIX_LAST);
  return result;
}


/*
  Determine the prefix of the next group.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::next_prefix()

  DESCRIPTION
    Determine the prefix of the next group that satisfies the query conditions.
    If there is a range condition referencing the group attributes, use a
    QUICK_RANGE_SELECT object to retrieve the *first* key that satisfies the
    condition. If there is a key infix of constants, append this infix
    immediately after the group attributes. The possibly extended prefix is
    stored in this->group_prefix. The first key of the found group is stored in
    this->record, on which relies this->next_min().

  RETURN
    0                    on success
    HA_ERR_KEY_NOT_FOUND if there is no key with the formed prefix
    HA_ERR_END_OF_FILE   if there are no more keys
    other                if some error occurred
*/
int QUICK_GROUP_MIN_MAX_SELECT::next_prefix()
{
  int result;

  if (quick_prefix_select)
  {
    unsigned char *cur_prefix= seen_first_key ? group_prefix : NULL;
    if ((result= quick_prefix_select->get_next_prefix(group_prefix_len,
                         make_prev_keypart_map(group_key_parts), cur_prefix)))
      return result;
    seen_first_key= true;
  }
  else
  {
    if (!seen_first_key)
    {
      result= file->index_first(record);
      if (result)
        return result;
      seen_first_key= true;
    }
    else
    {
      /* Load the first key in this group into record. */
      result= file->index_read_map(record, group_prefix,
                                   make_prev_keypart_map(group_key_parts),
                                   HA_READ_AFTER_KEY);
      if (result)
        return result;
    }
  }

  /* Save the prefix of this group for subsequent calls. */
  key_copy(group_prefix, record, index_info, group_prefix_len);
  /* Append key_infix to group_prefix. */
  if (key_infix_len > 0)
    memcpy(group_prefix + group_prefix_len,
           key_infix, key_infix_len);

  return 0;
}


/*
  Find the minimal key in a group that satisfies some range conditions for the
  min/max argument field.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::next_min_in_range()

  DESCRIPTION
    Given the sequence of ranges min_max_ranges, find the minimal key that is
    in the left-most possible range. If there is no such key, then the current
    group does not have a MIN key that satisfies the WHERE clause. If a key is
    found, its value is stored in this->record.

  RETURN
    0                    on success
    HA_ERR_KEY_NOT_FOUND if there is no key with the given prefix in any of
                         the ranges
    HA_ERR_END_OF_FILE   - "" -
    other                if some error
*/

int QUICK_GROUP_MIN_MAX_SELECT::next_min_in_range()
{
  ha_rkey_function find_flag;
  key_part_map keypart_map;
  QUICK_RANGE *cur_range;
  bool found_null= false;
  int result= HA_ERR_KEY_NOT_FOUND;
  basic_string<unsigned char> max_key;

  max_key.reserve(real_prefix_len + min_max_arg_len);

  assert(min_max_ranges.elements > 0);

  for (uint32_t range_idx= 0; range_idx < min_max_ranges.elements; range_idx++)
  { /* Search from the left-most range to the right. */
    get_dynamic(&min_max_ranges, (unsigned char*)&cur_range, range_idx);

    /*
      If the current value for the min/max argument is bigger than the right
      boundary of cur_range, there is no need to check this range.
    */
    if (range_idx != 0 && !(cur_range->flag & NO_MAX_RANGE) &&
        (key_cmp(min_max_arg_part, (const unsigned char*) cur_range->max_key,
                 min_max_arg_len) == 1))
      continue;

    if (cur_range->flag & NO_MIN_RANGE)
    {
      keypart_map= make_prev_keypart_map(real_key_parts);
      find_flag= HA_READ_KEY_EXACT;
    }
    else
    {
      /* Extend the search key with the lower boundary for this range. */
      memcpy(group_prefix + real_prefix_len, cur_range->min_key,
             cur_range->min_length);
      keypart_map= make_keypart_map(real_key_parts);
      find_flag= (cur_range->flag & (EQ_RANGE | NULL_RANGE)) ?
                 HA_READ_KEY_EXACT : (cur_range->flag & NEAR_MIN) ?
                 HA_READ_AFTER_KEY : HA_READ_KEY_OR_NEXT;
    }

    result= file->index_read_map(record, group_prefix, keypart_map, find_flag);
    if (result)
    {
      if ((result == HA_ERR_KEY_NOT_FOUND || result == HA_ERR_END_OF_FILE) &&
          (cur_range->flag & (EQ_RANGE | NULL_RANGE)))
        continue; /* Check the next range. */

      /*
        In all other cases (HA_ERR_*, HA_READ_KEY_EXACT with NO_MIN_RANGE,
        HA_READ_AFTER_KEY, HA_READ_KEY_OR_NEXT) if the lookup failed for this
        range, it can't succeed for any other subsequent range.
      */
      break;
    }

    /* A key was found. */
    if (cur_range->flag & EQ_RANGE)
      break; /* No need to perform the checks below for equal keys. */

    if (cur_range->flag & NULL_RANGE)
    {
      /*
        Remember this key, and continue looking for a non-NULL key that
        satisfies some other condition.
      */
      memcpy(tmp_record, record, head->s->rec_buff_length);
      found_null= true;
      continue;
    }

    /* Check if record belongs to the current group. */
    if (key_cmp(index_info->key_part, group_prefix, real_prefix_len))
    {
      result= HA_ERR_KEY_NOT_FOUND;
      continue;
    }

    /* If there is an upper limit, check if the found key is in the range. */
    if ( !(cur_range->flag & NO_MAX_RANGE) )
    {
      /* Compose the MAX key for the range. */
      max_key.clear();
      max_key.append(group_prefix, real_prefix_len);
      max_key.append(cur_range->max_key, cur_range->max_length);
      /* Compare the found key with max_key. */
      int cmp_res= key_cmp(index_info->key_part,
                           max_key.data(),
                           real_prefix_len + min_max_arg_len);
      if (!(((cur_range->flag & NEAR_MAX) && (cmp_res == -1)) ||
            (cmp_res <= 0)))
      {
        result= HA_ERR_KEY_NOT_FOUND;
        continue;
      }
    }
    /* If we got to this point, the current key qualifies as MIN. */
    assert(result == 0);
    break;
  }
  /*
    If there was a key with NULL in the MIN/MAX field, and there was no other
    key without NULL from the same group that satisfies some other condition,
    then use the key with the NULL.
  */
  if (found_null && result)
  {
    memcpy(record, tmp_record, head->s->rec_buff_length);
    result= 0;
  }
  return result;
}


/*
  Find the maximal key in a group that satisfies some range conditions for the
  min/max argument field.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::next_max_in_range()

  DESCRIPTION
    Given the sequence of ranges min_max_ranges, find the maximal key that is
    in the right-most possible range. If there is no such key, then the current
    group does not have a MAX key that satisfies the WHERE clause. If a key is
    found, its value is stored in this->record.

  RETURN
    0                    on success
    HA_ERR_KEY_NOT_FOUND if there is no key with the given prefix in any of
                         the ranges
    HA_ERR_END_OF_FILE   - "" -
    other                if some error
*/

int QUICK_GROUP_MIN_MAX_SELECT::next_max_in_range()
{
  ha_rkey_function find_flag;
  key_part_map keypart_map;
  QUICK_RANGE *cur_range;
  int result;
  basic_string<unsigned char> min_key;
  min_key.reserve(real_prefix_len + min_max_arg_len);

  assert(min_max_ranges.elements > 0);

  for (uint32_t range_idx= min_max_ranges.elements; range_idx > 0; range_idx--)
  { /* Search from the right-most range to the left. */
    get_dynamic(&min_max_ranges, (unsigned char*)&cur_range, range_idx - 1);

    /*
      If the current value for the min/max argument is smaller than the left
      boundary of cur_range, there is no need to check this range.
    */
    if (range_idx != min_max_ranges.elements &&
        !(cur_range->flag & NO_MIN_RANGE) &&
        (key_cmp(min_max_arg_part, (const unsigned char*) cur_range->min_key,
                 min_max_arg_len) == -1))
      continue;

    if (cur_range->flag & NO_MAX_RANGE)
    {
      keypart_map= make_prev_keypart_map(real_key_parts);
      find_flag= HA_READ_PREFIX_LAST;
    }
    else
    {
      /* Extend the search key with the upper boundary for this range. */
      memcpy(group_prefix + real_prefix_len, cur_range->max_key,
             cur_range->max_length);
      keypart_map= make_keypart_map(real_key_parts);
      find_flag= (cur_range->flag & EQ_RANGE) ?
                 HA_READ_KEY_EXACT : (cur_range->flag & NEAR_MAX) ?
                 HA_READ_BEFORE_KEY : HA_READ_PREFIX_LAST_OR_PREV;
    }

    result= file->index_read_map(record, group_prefix, keypart_map, find_flag);

    if (result)
    {
      if ((result == HA_ERR_KEY_NOT_FOUND || result == HA_ERR_END_OF_FILE) &&
          (cur_range->flag & EQ_RANGE))
        continue; /* Check the next range. */

      /*
        In no key was found with this upper bound, there certainly are no keys
        in the ranges to the left.
      */
      return result;
    }
    /* A key was found. */
    if (cur_range->flag & EQ_RANGE)
      return 0; /* No need to perform the checks below for equal keys. */

    /* Check if record belongs to the current group. */
    if (key_cmp(index_info->key_part, group_prefix, real_prefix_len))
      continue;                                 // Row not found

    /* If there is a lower limit, check if the found key is in the range. */
    if ( !(cur_range->flag & NO_MIN_RANGE) )
    {
      /* Compose the MIN key for the range. */
      min_key.clear();
      min_key.append(group_prefix, real_prefix_len);
      min_key.append(cur_range->min_key, cur_range->min_length);

      /* Compare the found key with min_key. */
      int cmp_res= key_cmp(index_info->key_part,
                           min_key.data(),
                           real_prefix_len + min_max_arg_len);
      if (!(((cur_range->flag & NEAR_MIN) && (cmp_res == 1)) ||
            (cmp_res >= 0)))
        continue;
    }
    /* If we got to this point, the current key qualifies as MAX. */
    return result;
  }
  return HA_ERR_KEY_NOT_FOUND;
}


/*
  Update all MIN function results with the newly found value.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::update_min_result()

  DESCRIPTION
    The method iterates through all MIN functions and updates the result value
    of each function by calling Item_sum::reset(), which in turn picks the new
    result value from this->head->record[0], previously updated by
    next_min(). The updated value is stored in a member variable of each of the
    Item_sum objects, depending on the value type.

  IMPLEMENTATION
    The update must be done separately for MIN and MAX, immediately after
    next_min() was called and before next_max() is called, because both MIN and
    MAX take their result value from the same buffer this->head->record[0]
    (i.e.  this->record).

  RETURN
    None
*/

void QUICK_GROUP_MIN_MAX_SELECT::update_min_result()
{
  Item_sum *min_func;

  min_functions_it->rewind();
  while ((min_func= (*min_functions_it)++))
    min_func->reset();
}


/*
  Update all MAX function results with the newly found value.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::update_max_result()

  DESCRIPTION
    The method iterates through all MAX functions and updates the result value
    of each function by calling Item_sum::reset(), which in turn picks the new
    result value from this->head->record[0], previously updated by
    next_max(). The updated value is stored in a member variable of each of the
    Item_sum objects, depending on the value type.

  IMPLEMENTATION
    The update must be done separately for MIN and MAX, immediately after
    next_max() was called, because both MIN and MAX take their result value
    from the same buffer this->head->record[0] (i.e.  this->record).

  RETURN
    None
*/

void QUICK_GROUP_MIN_MAX_SELECT::update_max_result()
{
  Item_sum *max_func;

  max_functions_it->rewind();
  while ((max_func= (*max_functions_it)++))
    max_func->reset();
}


/*
  Append comma-separated list of keys this quick select uses to key_names;
  append comma-separated list of corresponding used lengths to used_lengths.

  SYNOPSIS
    QUICK_GROUP_MIN_MAX_SELECT::add_keys_and_lengths()
    key_names    [out] Names of used indexes
    used_lengths [out] Corresponding lengths of the index names

  DESCRIPTION
    This method is used by select_describe to extract the names of the
    indexes used by a quick select.

*/

void QUICK_GROUP_MIN_MAX_SELECT::add_keys_and_lengths(String *key_names,
                                                      String *used_lengths)
{
  char buf[64];
  uint32_t length;
  key_names->append(index_info->name);
  length= int64_t2str(max_used_key_length, buf, 10) - buf;
  used_lengths->append(buf, length);
}

static void print_sel_tree(PARAM *param, SEL_TREE *tree, key_map *tree_map, const char *)
{
  SEL_ARG **key,**end;
  int idx;
  char buff[1024];

  String tmp(buff,sizeof(buff),&my_charset_bin);
  tmp.length(0);
  for (idx= 0,key=tree->keys, end=key+param->keys ;
       key != end ;
       key++,idx++)
  {
    if (tree_map->test(idx))
    {
      uint32_t keynr= param->real_keynr[idx];
      if (tmp.length())
        tmp.append(',');
      tmp.append(param->table->key_info[keynr].name);
    }
  }
  if (!tmp.length())
    tmp.append(STRING_WITH_LEN("(empty)"));
}


static void print_ror_scans_arr(Table *table,
                                const char *, struct st_ror_scan_info **start,
                                struct st_ror_scan_info **end)
{
  char buff[1024];
  String tmp(buff,sizeof(buff),&my_charset_bin);
  tmp.length(0);
  for (;start != end; start++)
  {
    if (tmp.length())
      tmp.append(',');
    tmp.append(table->key_info[(*start)->keynr].name);
  }
  if (!tmp.length())
    tmp.append(STRING_WITH_LEN("(empty)"));
}

/*****************************************************************************
** Instantiate templates
*****************************************************************************/

#ifdef HAVE_EXPLICIT_TEMPLATE_INSTANTIATION
template class List<QUICK_RANGE>;
template class List_iterator<QUICK_RANGE>;
#endif
