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

#ifndef DRIZZLED_PLUGIN_STORAGE_ENGINE_H
#define DRIZZLED_PLUGIN_STORAGE_ENGINE_H


#include <drizzled/definitions.h>
#include <drizzled/plugin.h>
#include <drizzled/handler_structs.h>
#include <drizzled/message/table.pb.h>
#include "drizzled/plugin/plugin.h"
#include <drizzled/name_map.h>

#include "mysys/cached_directory.h"

#include <bitset>
#include <string>
#include <vector>

class TableList;
class Session;
class XID;
class Cursor;

class TableShare;
typedef struct st_mysql_lex_string LEX_STRING;
typedef bool (stat_print_fn)(Session *session, const char *type, uint32_t type_len,
                             const char *file, uint32_t file_len,
                             const char *status, uint32_t status_len);

/* Possible flags of a StorageEngine (there can be 32 of them) */
enum engine_flag_bits {
  HTON_BIT_ALTER_NOT_SUPPORTED,       // Engine does not support alter
  HTON_BIT_HIDDEN,                    // Engine does not appear in lists
  HTON_BIT_NOT_USER_SELECTABLE,
  HTON_BIT_TEMPORARY_NOT_SUPPORTED,   // Having temporary tables not supported
  HTON_BIT_TEMPORARY_ONLY,
  HTON_BIT_FILE_BASED, // use for check_lowercase_names
  HTON_BIT_HAS_DATA_DICTIONARY,
  HTON_BIT_DOES_TRANSACTIONS,
  HTON_BIT_STATS_RECORDS_IS_EXACT,
  HTON_BIT_NULL_IN_KEY,
  HTON_BIT_CAN_INDEX_BLOBS,
  HTON_BIT_PRIMARY_KEY_REQUIRED_FOR_POSITION,
  HTON_BIT_PRIMARY_KEY_IN_READ_INDEX,
  HTON_BIT_PARTIAL_COLUMN_READ,
  HTON_BIT_TABLE_SCAN_ON_INDEX,
  HTON_BIT_MRR_CANT_SORT,
  HTON_BIT_SIZE
};

static const std::bitset<HTON_BIT_SIZE> HTON_NO_FLAGS(0);
static const std::bitset<HTON_BIT_SIZE> HTON_ALTER_NOT_SUPPORTED(1 << HTON_BIT_ALTER_NOT_SUPPORTED);
static const std::bitset<HTON_BIT_SIZE> HTON_HIDDEN(1 << HTON_BIT_HIDDEN);
static const std::bitset<HTON_BIT_SIZE> HTON_NOT_USER_SELECTABLE(1 << HTON_BIT_NOT_USER_SELECTABLE);
static const std::bitset<HTON_BIT_SIZE> HTON_TEMPORARY_NOT_SUPPORTED(1 << HTON_BIT_TEMPORARY_NOT_SUPPORTED);
static const std::bitset<HTON_BIT_SIZE> HTON_TEMPORARY_ONLY(1 << HTON_BIT_TEMPORARY_ONLY);
static const std::bitset<HTON_BIT_SIZE> HTON_FILE_BASED(1 << HTON_BIT_FILE_BASED);
static const std::bitset<HTON_BIT_SIZE> HTON_HAS_DATA_DICTIONARY(1 << HTON_BIT_HAS_DATA_DICTIONARY);
static const std::bitset<HTON_BIT_SIZE> HTON_HAS_DOES_TRANSACTIONS(1 << HTON_BIT_DOES_TRANSACTIONS);
static const std::bitset<HTON_BIT_SIZE> HTON_STATS_RECORDS_IS_EXACT(1 << HTON_BIT_STATS_RECORDS_IS_EXACT);
static const std::bitset<HTON_BIT_SIZE> HTON_NULL_IN_KEY(1 << HTON_BIT_NULL_IN_KEY);
static const std::bitset<HTON_BIT_SIZE> HTON_CAN_INDEX_BLOBS(1 << HTON_BIT_CAN_INDEX_BLOBS);
static const std::bitset<HTON_BIT_SIZE> HTON_PRIMARY_KEY_REQUIRED_FOR_POSITION(1 << HTON_BIT_PRIMARY_KEY_REQUIRED_FOR_POSITION);
static const std::bitset<HTON_BIT_SIZE> HTON_PRIMARY_KEY_IN_READ_INDEX(1 << HTON_BIT_PRIMARY_KEY_IN_READ_INDEX);
static const std::bitset<HTON_BIT_SIZE> HTON_PARTIAL_COLUMN_READ(1 << HTON_BIT_PARTIAL_COLUMN_READ);
static const std::bitset<HTON_BIT_SIZE> HTON_TABLE_SCAN_ON_INDEX(1 << HTON_BIT_TABLE_SCAN_ON_INDEX);
static const std::bitset<HTON_BIT_SIZE> HTON_MRR_CANT_SORT(1 << HTON_BIT_MRR_CANT_SORT);


class Table;

namespace drizzled
{
namespace plugin
{

const std::string UNKNOWN_STRING("UNKNOWN");
const std::string DEFAULT_DEFINITION_FILE_EXT(".dfe");


/*
  StorageEngine is a singleton structure - one instance per storage engine -
  to provide access to storage engine functionality that works on the
  "global" level (unlike Cursor class that works on a per-table basis)

  usually StorageEngine instance is defined statically in ha_xxx.cc as

  static StorageEngine { ... } xxx_engine;

  savepoint_*, prepare, recover, and *_by_xid pointers can be 0.
*/
class StorageEngine : public Plugin
{
public:
  typedef uint64_t Table_flags;

private:

  /*
    Name used for storage engine.
  */
  const bool two_phase_commit;
  bool enabled;

  const std::bitset<HTON_BIT_SIZE> flags; /* global Cursor flags */
  /*
    to store per-savepoint data storage engine is provided with an area
    of a requested size (0 is ok here).
    savepoint_offset must be initialized statically to the size of
    the needed memory to store per-savepoint information.
    After xxx_init it is changed to be an offset to savepoint storage
    area and need not be used by storage engine.
    see binlog_engine and binlog_savepoint_set/rollback for an example.
  */
  size_t savepoint_offset;
  size_t orig_savepoint_offset;

  void setTransactionReadWrite(Session& session);

protected:
  std::string table_definition_ext;

public:
  const std::string& getTableDefinitionFileExtension()
  {
    return table_definition_ext;
  }

protected:

  /**
    @brief
    Used as a protobuf storage currently by TEMP only engines.
  */
  typedef std::map <std::string, drizzled::message::Table> ProtoCache;
  ProtoCache proto_cache;
  pthread_mutex_t proto_cache_mutex;

  /**
   * Implementing classes should override these to provide savepoint
   * functionality.
   */
  virtual int savepoint_set_hook(Session *, void *) { return 0; }

  virtual int savepoint_rollback_hook(Session *, void *) { return 0; }

  virtual int savepoint_release_hook(Session *, void *) { return 0; }

public:

  StorageEngine(const std::string name_arg,
                const std::bitset<HTON_BIT_SIZE> &flags_arg= HTON_NO_FLAGS,
                size_t savepoint_offset_arg= 0,
                bool support_2pc= false);

  virtual ~StorageEngine();

  virtual int doGetTableDefinition(Session& session,
                                   const char *path,
                                   const char *db,
                                   const char *table_name,
                                   const bool is_tmp,
                                   drizzled::message::Table *table_proto)
  {
    (void)session;
    (void)path;
    (void)db;
    (void)table_name;
    (void)is_tmp;
    (void)table_proto;

    return ENOENT;
  }

  /* Old style cursor errors */
protected:
  void print_keydup_error(uint32_t key_nr, const char *msg, Table &table);
  void print_error(int error, myf errflag, Table *table= NULL);
  virtual bool get_error_message(int error, String *buf);
public:
  virtual void print_error(int error, myf errflag, Table& table);

  /*
    each storage engine has it's own memory area (actually a pointer)
    in the session, for storing per-connection information.
    It is accessed as

      session->ha_data[xxx_engine.slot]

   slot number is initialized by MySQL after xxx_init() is called.
  */
  uint32_t slot;

  virtual Table_flags table_flags(void) const= 0;

  inline uint32_t getSlot (void) { return slot; }
  inline void setSlot (uint32_t value) { slot= value; }

  bool has_2pc()
  {
    return two_phase_commit;
  }


  bool is_enabled() const
  {
    return enabled;
  }

  bool is_user_selectable() const
  {
    return not flags.test(HTON_BIT_NOT_USER_SELECTABLE);
  }

  bool check_flag(const engine_flag_bits flag) const
  {
    return flags.test(flag);
  }

  void enable() { enabled= true; }
  void disable() { enabled= false; }

  /*
    StorageEngine methods:

    close_connection is only called if
    session->ha_data[xxx_engine.slot] is non-zero, so even if you don't need
    this storage area - set it to something, so that MySQL would know
    this storage engine was accessed in this connection
  */
  virtual int close_connection(Session  *)
  {
    return 0;
  }
  /*
    'all' is true if it's a real commit, that makes persistent changes
    'all' is false if it's not in fact a commit but an end of the
    statement that is part of the transaction.
    NOTE 'all' is also false in auto-commit mode where 'end of statement'
    and 'real commit' mean the same event.
  */
  virtual int  commit(Session *, bool)
  {
    return 0;
  }

  virtual int  rollback(Session *, bool)
  {
    return 0;
  }

  /*
    The void * points to an uninitialized storage area of requested size
    (see savepoint_offset description)
  */
  int savepoint_set(Session *session, void *sp)
  {
    return savepoint_set_hook(session, (unsigned char *)sp+savepoint_offset);
  }

  /*
    The void * points to a storage area, that was earlier passed
    to the savepoint_set call
  */
  int savepoint_rollback(Session *session, void *sp)
  {
     return savepoint_rollback_hook(session,
                                    (unsigned char *)sp+savepoint_offset);
  }

  int savepoint_release(Session *session, void *sp)
  {
    return savepoint_release_hook(session,
                                  (unsigned char *)sp+savepoint_offset);
  }

  virtual int  prepare(Session *, bool) { return 0; }
  virtual int  recover(XID *, uint32_t) { return 0; }
  virtual int  commit_by_xid(XID *) { return 0; }
  virtual int  rollback_by_xid(XID *) { return 0; }
  virtual Cursor *create(TableShare &, MEM_ROOT *)= 0;
  /* args: path */
  virtual void drop_database(char*) { }
  virtual int start_consistent_snapshot(Session *) { return 0; }
  virtual bool flush_logs() { return false; }
  virtual bool show_status(Session *, stat_print_fn *, enum ha_stat_type)
  {
    return false;
  }

  /* args: current_session, tables, cond */
  virtual int fill_files_table(Session *, TableList *,
                               Item *) { return 0; }
  virtual int release_temporary_latches(Session *) { return false; }

  /**
    If frm_error() is called then we will use this to find out what file
    extentions exist for the storage engine. This is also used by the default
    rename_table and delete_table method in Cursor.cc.

    For engines that have two file name extentions (separate meta/index file
    and data file), the order of elements is relevant. First element of engine
    file name extentions array should be meta/index file extention. Second
    element - data file extention. This order is assumed by
    prepare_for_repair() when REPAIR Table ... USE_FRM is issued.
  */
  virtual const char **bas_ext() const =0;

protected:
  virtual int doCreateTable(Session *session,
                            const char *table_name,
                            Table& table_arg,
                            drizzled::message::Table& proto)= 0;

  virtual int doRenameTable(Session* session,
                            const char *from, const char *to);


public:

  int renameTable(Session *session, const char *from, const char *to) 
  {
    setTransactionReadWrite(*session);

    return doRenameTable(session, from, to);
  }

  // TODO: move these to protected
  virtual void doGetTableNames(CachedDirectory &directory, std::string& db_name, std::set<std::string>& set_of_names);
  virtual int doDropTable(Session& session,
                          const std::string table_path)= 0;

  const char *checkLowercaseNames(const char *path, char *tmp_path);

  /* Class Methods for operating on plugin */
  static bool addPlugin(plugin::StorageEngine *engine);
  static void removePlugin(plugin::StorageEngine *engine);

  static int getTableDefinition(Session& session,
                                TableIdentifier &identifier,
                                message::Table *table_proto= NULL);
  static int getTableDefinition(Session& session,
                                const char* path,
                                const char *db,
                                const char *table_name,
                                const bool is_tmp,
                                message::Table *table_proto= NULL);

  static plugin::StorageEngine *findByName(std::string find_str);
  static plugin::StorageEngine *findByName(Session& session,
                                           std::string find_str);
  static void closeConnection(Session* session);
  static void dropDatabase(char* path);
  static int commitOrRollbackByXID(XID *xid, bool commit);
  static int releaseTemporaryLatches(Session *session);
  static bool flushLogs(plugin::StorageEngine *db_type);
  static int recover(HASH *commit_list);
  static int startConsistentSnapshot(Session *session);
  static int dropTable(Session& session,
                       drizzled::TableIdentifier &identifier,
                       bool generate_warning);
  static void getTableNames(std::string& db_name, std::set<std::string> &set_of_names);

  static inline const std::string &resolveName(const StorageEngine *engine)
  {
    return engine == NULL ? UNKNOWN_STRING : engine->getName();
  }

  static int createTable(Session& session,
                         drizzled::TableIdentifier &identifier,
                         bool update_create_info,
                         drizzled::message::Table& table_proto,
                         bool used= true);

  static void removeLostTemporaryTables(Session &session, const char *directory);

  Cursor *getCursor(TableShare &share, MEM_ROOT *alloc);
};

} /* namespace plugin */
} /* namespace drizzled */

#endif /* DRIZZLED_PLUGIN_STORAGE_ENGINE_H */
