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

#ifndef DRIZZLED_ERROR_H
#define DRIZZLED_ERROR_H

#include "drizzled/definitions.h"

/* Max width of screen (for error messages) */
#define SC_MAXWIDTH 256
#define ERRMSGSIZE	(SC_MAXWIDTH)	/* Max length of a error message */
#define NRERRBUFFS	(2)	/* Buffers for parameters */
#define MY_FILE_ERROR	((size_t) -1)
#define ME_FATALERROR   1024    /* Fatal statement error */

namespace drizzled
{

typedef void (*error_handler_func)(uint32_t my_err,
                                   const char *str,
                                   myf MyFlags);
extern error_handler_func error_handler_hook;

bool init_errmessage(void);
const char * error_message(unsigned int err_index);

enum drizzled_error_code {

  EE_ERROR_FIRST=1,
  EE_CANTCREATEFILE,
  EE_READ,
  EE_WRITE,
  EE_BADCLOSE,
  EE_OUTOFMEMORY,
  EE_DELETE,
  EE_LINK,
  EE_EOFERR,
  EE_CANTLOCK,
  EE_CANTUNLOCK,
  EE_DIR,
  EE_STAT,
  EE_CANT_CHSIZE,
  EE_CANT_OPEN_STREAM,
  EE_GETWD,
  EE_SETWD,
  EE_LINK_WARNING,
  EE_OPEN_WARNING,
  EE_DISK_FULL,
  EE_CANT_MKDIR,
  EE_UNKNOWN_CHARSET,
  EE_OUT_OF_FILERESOURCES,
  EE_CANT_READLINK,
  EE_CANT_SYMLINK,
  EE_REALPATH,
  EE_SYNC,
  EE_UNKNOWN_COLLATION,
  EE_FILENOTFOUND,
  EE_FILE_NOT_CLOSED,
  EE_ERROR_LAST= EE_FILE_NOT_CLOSED,

  ER_ERROR_FIRST= 1000,
  ER_UNUSED1000= ER_ERROR_FIRST,
  ER_UNUSED1001,
  ER_NO,
  ER_YES,
  ER_CANT_CREATE_FILE,
  ER_CANT_CREATE_TABLE,
  ER_CANT_CREATE_DB,
  ER_DB_CREATE_EXISTS,
  ER_DB_DROP_EXISTS,
  ER_DB_DROP_DELETE,
  ER_DB_DROP_RMDIR,
  ER_CANT_DELETE_FILE,
  ER_CANT_FIND_SYSTEM_REC,
  ER_CANT_GET_STAT,
  ER_CANT_GET_WD,
  ER_CANT_LOCK,
  ER_CANT_OPEN_FILE,
  ER_FILE_NOT_FOUND,
  ER_CANT_READ_DIR,
  ER_CANT_SET_WD,
  ER_CHECKREAD,
  ER_DISK_FULL,
  ER_DUP_KEY,
  ER_ERROR_ON_CLOSE,
  ER_ERROR_ON_READ,
  ER_ERROR_ON_RENAME,
  ER_ERROR_ON_WRITE,
  ER_FILE_USED,
  ER_FILSORT_ABORT,
  ER_FORM_NOT_FOUND,
  ER_GET_ERRNO,
  ER_ILLEGAL_HA,
  ER_KEY_NOT_FOUND,
  ER_NOT_FORM_FILE,
  ER_NOT_KEYFILE,
  ER_OLD_KEYFILE,
  ER_OPEN_AS_READONLY,
  ER_OUTOFMEMORY,
  ER_OUT_OF_SORTMEMORY,
  ER_UNEXPECTED_EOF,
  ER_CON_COUNT_ERROR,
  ER_OUT_OF_RESOURCES,
  ER_BAD_HOST_ERROR,
  ER_HANDSHAKE_ERROR,
  ER_DBACCESS_DENIED_ERROR,
  ER_ACCESS_DENIED_ERROR,
  ER_NO_DB_ERROR,
  ER_UNKNOWN_COM_ERROR,
  ER_BAD_NULL_ERROR,
  ER_BAD_DB_ERROR,
  ER_TABLE_EXISTS_ERROR,
  ER_BAD_TABLE_ERROR,
  ER_NON_UNIQ_ERROR,
  ER_SERVER_SHUTDOWN,
  ER_BAD_FIELD_ERROR,
  ER_WRONG_FIELD_WITH_GROUP,
  ER_WRONG_GROUP_FIELD,
  ER_WRONG_SUM_SELECT,
  ER_WRONG_VALUE_COUNT,
  ER_TOO_LONG_IDENT,
  ER_DUP_FIELDNAME,
  ER_DUP_KEYNAME,
  ER_DUP_ENTRY,
  ER_WRONG_FIELD_SPEC,
  ER_PARSE_ERROR,
  ER_EMPTY_QUERY,
  ER_NONUNIQ_TABLE,
  ER_INVALID_DEFAULT,
  ER_MULTIPLE_PRI_KEY,
  ER_TOO_MANY_KEYS,
  ER_TOO_MANY_KEY_PARTS,
  ER_TOO_LONG_KEY,
  ER_KEY_COLUMN_DOES_NOT_EXITS,
  ER_BLOB_USED_AS_KEY,
  ER_TOO_BIG_FIELDLENGTH,
  ER_WRONG_AUTO_KEY,
  ER_READY,
  ER_NORMAL_SHUTDOWN,
  ER_GOT_SIGNAL,
  ER_SHUTDOWN_COMPLETE,
  ER_FORCING_CLOSE,
  ER_IPSOCK_ERROR,
  ER_NO_SUCH_INDEX,
  ER_WRONG_FIELD_TERMINATORS,
  ER_BLOBS_AND_NO_TERMINATED,
  ER_TEXTFILE_NOT_READABLE,
  ER_FILE_EXISTS_ERROR,
  ER_LOAD_INFO,
  ER_ALTER_INFO,
  ER_WRONG_SUB_KEY,
  ER_CANT_REMOVE_ALL_FIELDS,
  ER_CANT_DROP_FIELD_OR_KEY,
  ER_INSERT_INFO,
  ER_UPDATE_TABLE_USED,
  ER_NO_SUCH_THREAD,
  ER_KILL_DENIED_ERROR,
  ER_NO_TABLES_USED,
  ER_TOO_BIG_SET,
  ER_NO_UNIQUE_LOGFILE,
  ER_TABLE_NOT_LOCKED_FOR_WRITE,
  ER_TABLE_NOT_LOCKED,
  ER_BLOB_CANT_HAVE_DEFAULT,
  ER_WRONG_DB_NAME,
  ER_WRONG_TABLE_NAME,
  ER_TOO_BIG_SELECT,
  ER_UNKNOWN_ERROR,
  ER_UNKNOWN_PROCEDURE,
  ER_WRONG_PARAMCOUNT_TO_PROCEDURE,
  ER_WRONG_PARAMETERS_TO_PROCEDURE,
  ER_UNKNOWN_TABLE,
  ER_FIELD_SPECIFIED_TWICE,
  ER_INVALID_GROUP_FUNC_USE,
  ER_UNSUPPORTED_EXTENSION,
  ER_TABLE_MUST_HAVE_COLUMNS,
  ER_RECORD_FILE_FULL,
  ER_UNKNOWN_CHARACTER_SET,
  ER_TOO_MANY_TABLES,
  ER_TOO_MANY_FIELDS,
  ER_TOO_BIG_ROWSIZE,
  ER_STACK_OVERRUN,
  ER_WRONG_OUTER_JOIN,
  ER_NULL_COLUMN_IN_INDEX,
  ER_CANT_FIND_UDF,
  ER_CANT_INITIALIZE_UDF,
  ER_PLUGIN_NO_PATHS,
  ER_PLUGIN_EXISTS,
  ER_CANT_OPEN_LIBRARY,
  ER_CANT_FIND_DL_ENTRY,
  ER_FUNCTION_NOT_DEFINED,
  ER_HOST_IS_BLOCKED,
  ER_HOST_NOT_PRIVILEGED,
  ER_PASSWORD_ANONYMOUS_USER,
  ER_PASSWORD_NOT_ALLOWED,
  ER_PASSWORD_NO_MATCH,
  ER_UPDATE_INFO,
  ER_CANT_CREATE_THREAD,
  ER_WRONG_VALUE_COUNT_ON_ROW,
  ER_CANT_REOPEN_TABLE,
  ER_INVALID_USE_OF_NULL,
  ER_REGEXP_ERROR,
  ER_MIX_OF_GROUP_FUNC_AND_FIELDS,
  ER_NONEXISTING_GRANT,
  ER_TABLEACCESS_DENIED_ERROR,
  ER_COLUMNACCESS_DENIED_ERROR,
  ER_ILLEGAL_GRANT_FOR_TABLE,
  ER_GRANT_WRONG_HOST_OR_USER,
  ER_NO_SUCH_TABLE,
  ER_NONEXISTING_TABLE_GRANT,
  ER_NOT_ALLOWED_COMMAND,
  ER_SYNTAX_ERROR,
  ER_DELAYED_CANT_CHANGE_LOCK,
  ER_TOO_MANY_DELAYED_THREADS,
  ER_ABORTING_CONNECTION,
  ER_NET_PACKET_TOO_LARGE,
  ER_NET_READ_ERROR_FROM_PIPE,
  ER_NET_FCNTL_ERROR,
  ER_NET_PACKETS_OUT_OF_ORDER,
  ER_NET_UNCOMPRESS_ERROR,
  ER_NET_READ_ERROR,
  ER_NET_READ_INTERRUPTED,
  ER_NET_ERROR_ON_WRITE,
  ER_NET_WRITE_INTERRUPTED,
  ER_TOO_LONG_STRING,
  ER_TABLE_CANT_HANDLE_BLOB,
  ER_TABLE_CANT_HANDLE_AUTO_INCREMENT,
  ER_DELAYED_INSERT_TABLE_LOCKED,
  ER_WRONG_COLUMN_NAME,
  ER_WRONG_KEY_COLUMN,
  ER_WRONG_MRG_TABLE,
  ER_DUP_UNIQUE,
  ER_BLOB_KEY_WITHOUT_LENGTH,
  ER_PRIMARY_CANT_HAVE_NULL,
  ER_TOO_MANY_ROWS,
  ER_REQUIRES_PRIMARY_KEY,
  ER_NO_RAID_COMPILED,
  ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE,
  ER_KEY_DOES_NOT_EXITS,
  ER_CHECK_NO_SUCH_TABLE,
  ER_CHECK_NOT_IMPLEMENTED,
  ER_CANT_DO_THIS_DURING_AN_TRANSACTION,
  ER_ERROR_DURING_COMMIT,
  ER_ERROR_DURING_ROLLBACK,
  ER_ERROR_DURING_FLUSH_LOGS,
  ER_ERROR_DURING_CHECKPOINT,
  ER_NEW_ABORTING_CONNECTION,
  ER_DUMP_NOT_IMPLEMENTED,
  ER_FLUSH_MASTER_BINLOG_CLOSED,
  ER_INDEX_REBUILD,
  ER_MASTER,
  ER_MASTER_NET_READ,
  ER_MASTER_NET_WRITE,
  ER_FT_MATCHING_KEY_NOT_FOUND,
  ER_LOCK_OR_ACTIVE_TRANSACTION,
  ER_UNKNOWN_SYSTEM_VARIABLE,
  ER_CRASHED_ON_USAGE,
  ER_CRASHED_ON_REPAIR,
  ER_WARNING_NOT_COMPLETE_ROLLBACK,
  ER_TRANS_CACHE_FULL,
  ER_SLAVE_MUST_STOP,
  ER_SLAVE_NOT_RUNNING,
  ER_BAD_SLAVE,
  ER_MASTER_INFO,
  ER_SLAVE_THREAD,
  ER_TOO_MANY_USER_CONNECTIONS,
  ER_SET_CONSTANTS_ONLY,
  ER_LOCK_WAIT_TIMEOUT,
  ER_LOCK_TABLE_FULL,
  ER_READ_ONLY_TRANSACTION,
  ER_DROP_DB_WITH_READ_LOCK,
  ER_CREATE_DB_WITH_READ_LOCK,
  ER_WRONG_ARGUMENTS,
  ER_NO_PERMISSION_TO_CREATE_USER,
  ER_UNION_TABLES_IN_DIFFERENT_DIR,
  ER_LOCK_DEADLOCK,
  ER_TABLE_CANT_HANDLE_FT,
  ER_CANNOT_ADD_FOREIGN,
  ER_NO_REFERENCED_ROW,
  ER_ROW_IS_REFERENCED,
  ER_CONNECT_TO_MASTER,
  ER_QUERY_ON_MASTER,
  ER_ERROR_WHEN_EXECUTING_COMMAND,
  ER_WRONG_USAGE,
  ER_WRONG_NUMBER_OF_COLUMNS_IN_SELECT,
  ER_CANT_UPDATE_WITH_READLOCK,
  ER_MIXING_NOT_ALLOWED,
  ER_DUP_ARGUMENT,
  ER_USER_LIMIT_REACHED,
  ER_SPECIFIC_ACCESS_DENIED_ERROR,
  ER_LOCAL_VARIABLE,
  ER_GLOBAL_VARIABLE,
  ER_NO_DEFAULT,
  ER_WRONG_VALUE_FOR_VAR,
  ER_WRONG_TYPE_FOR_VAR,
  ER_VAR_CANT_BE_READ,
  ER_CANT_USE_OPTION_HERE,
  ER_NOT_SUPPORTED_YET,
  ER_MASTER_FATAL_ERROR_READING_BINLOG,
  ER_SLAVE_IGNORED_TABLE,
  ER_INCORRECT_GLOBAL_LOCAL_VAR,
  ER_WRONG_FK_DEF,
  ER_KEY_REF_DO_NOT_MATCH_TABLE_REF,
  ER_OPERAND_COLUMNS,
  ER_SUBQUERY_NO_1_ROW,
  ER_UNKNOWN_STMT_HANDLER,
  ER_CORRUPT_HELP_DB,
  ER_CYCLIC_REFERENCE,
  ER_AUTO_CONVERT,
  ER_ILLEGAL_REFERENCE,
  ER_DERIVED_MUST_HAVE_ALIAS,
  ER_SELECT_REDUCED,
  ER_TABLENAME_NOT_ALLOWED_HERE,
  ER_NOT_SUPPORTED_AUTH_MODE,
  ER_SPATIAL_CANT_HAVE_NULL,
  ER_COLLATION_CHARSET_MISMATCH,
  ER_SLAVE_WAS_RUNNING,
  ER_SLAVE_WAS_NOT_RUNNING,
  ER_TOO_BIG_FOR_UNCOMPRESS,
  ER_ZLIB_Z_MEM_ERROR,
  ER_ZLIB_Z_BUF_ERROR,
  ER_ZLIB_Z_DATA_ERROR,
  ER_CUT_VALUE_GROUP_CONCAT,
  ER_WARN_TOO_FEW_RECORDS,
  ER_WARN_TOO_MANY_RECORDS,
  ER_WARN_NULL_TO_NOTNULL,
  ER_WARN_DATA_OUT_OF_RANGE,
  ER_WARN_DATA_TRUNCATED,
  ER_WARN_USING_OTHER_HANDLER,
  ER_CANT_AGGREGATE_2COLLATIONS,
  ER_DROP_USER,
  ER_REVOKE_GRANTS,
  ER_CANT_AGGREGATE_3COLLATIONS,
  ER_CANT_AGGREGATE_NCOLLATIONS,
  ER_VARIABLE_IS_NOT_STRUCT,
  ER_UNKNOWN_COLLATION,
  ER_SLAVE_IGNORED_SSL_PARAMS,
  ER_SERVER_IS_IN_SECURE_AUTH_MODE,
  ER_WARN_FIELD_RESOLVED,
  ER_BAD_SLAVE_UNTIL_COND,
  ER_MISSING_SKIP_SLAVE,
  ER_UNTIL_COND_IGNORED,
  ER_WRONG_NAME_FOR_INDEX,
  ER_WRONG_NAME_FOR_CATALOG,
  ER_WARN_QC_RESIZE,
  ER_BAD_FT_COLUMN,
  ER_UNKNOWN_KEY_CACHE,
  ER_WARN_HOSTNAME_WONT_WORK,
  ER_UNKNOWN_STORAGE_ENGINE,
  ER_WARN_DEPRECATED_SYNTAX,
  ER_NON_UPDATABLE_TABLE,
  ER_FEATURE_DISABLED,
  ER_OPTION_PREVENTS_STATEMENT,
  ER_DUPLICATED_VALUE_IN_TYPE,
  ER_TRUNCATED_WRONG_VALUE,
  ER_TOO_MUCH_AUTO_TIMESTAMP_COLS,
  ER_INVALID_ON_UPDATE,
  ER_UNSUPPORTED_PS,
  ER_GET_ERRMSG,
  ER_GET_TEMPORARY_ERRMSG,
  ER_UNKNOWN_TIME_ZONE,
  ER_WARN_INVALID_TIMESTAMP,
  ER_INVALID_CHARACTER_STRING,
  ER_WARN_ALLOWED_PACKET_OVERFLOWED,
  ER_CONFLICTING_DECLARATIONS,
  ER_SP_NO_RECURSIVE_CREATE,
  ER_SP_ALREADY_EXISTS,
  ER_SP_DOES_NOT_EXIST,
  ER_SP_DROP_FAILED,
  ER_SP_STORE_FAILED,
  ER_SP_LILABEL_MISMATCH,
  ER_SP_LABEL_REDEFINE,
  ER_SP_LABEL_MISMATCH,
  ER_SP_UNINIT_VAR,
  ER_SP_BADSELECT,
  ER_SP_BADRETURN,
  ER_SP_BADSTATEMENT,
  ER_UPDATE_LOG_DEPRECATED_IGNORED,
  ER_UPDATE_LOG_DEPRECATED_TRANSLATED,
  ER_QUERY_INTERRUPTED,
  ER_SP_WRONG_NO_OF_ARGS,
  ER_SP_COND_MISMATCH,
  ER_SP_NORETURN,
  ER_SP_NORETURNEND,
  ER_SP_BAD_CURSOR_QUERY,
  ER_SP_BAD_CURSOR_SELECT,
  ER_SP_CURSOR_MISMATCH,
  ER_SP_CURSOR_ALREADY_OPEN,
  ER_SP_CURSOR_NOT_OPEN,
  ER_SP_UNDECLARED_VAR,
  ER_SP_WRONG_NO_OF_FETCH_ARGS,
  ER_SP_FETCH_NO_DATA,
  ER_SP_DUP_PARAM,
  ER_SP_DUP_VAR,
  ER_SP_DUP_COND,
  ER_SP_DUP_CURS,
  ER_SP_CANT_ALTER,
  ER_SP_SUBSELECT_NYI,
  ER_STMT_NOT_ALLOWED_IN_SF_OR_TRG,
  ER_SP_VARCOND_AFTER_CURSHNDLR,
  ER_SP_CURSOR_AFTER_HANDLER,
  ER_SP_CASE_NOT_FOUND,
  ER_FPARSER_TOO_BIG_FILE,
  ER_FPARSER_BAD_HEADER,
  ER_FPARSER_EOF_IN_COMMENT,
  ER_FPARSER_ERROR_IN_PARAMETER,
  ER_FPARSER_EOF_IN_UNKNOWN_PARAMETER,
  ER_VIEW_NO_EXPLAIN,
  ER_UNUSED1346,
  ER_WRONG_OBJECT,
  ER_NONUPDATEABLE_COLUMN,
  ER_VIEW_SELECT_DERIVED,
  ER_VIEW_SELECT_CLAUSE,
  ER_VIEW_SELECT_VARIABLE,
  ER_VIEW_SELECT_TMPTABLE,
  ER_VIEW_WRONG_LIST,
  ER_WARN_VIEW_MERGE,
  ER_WARN_VIEW_WITHOUT_KEY,
  ER_VIEW_INVALID,
  ER_SP_NO_DROP_SP,
  ER_SP_GOTO_IN_HNDLR,
  ER_TRG_ALREADY_EXISTS,
  ER_TRG_DOES_NOT_EXIST,
  ER_TRG_ON_VIEW_OR_TEMP_TABLE,
  ER_TRG_CANT_CHANGE_ROW,
  ER_TRG_NO_SUCH_ROW_IN_TRG,
  ER_NO_DEFAULT_FOR_FIELD,
  ER_DIVISION_BY_ZERO,
  ER_TRUNCATED_WRONG_VALUE_FOR_FIELD,
  ER_ILLEGAL_VALUE_FOR_TYPE,
  ER_VIEW_NONUPD_CHECK,
  ER_VIEW_CHECK_FAILED,
  ER_PROCACCESS_DENIED_ERROR,
  ER_RELAY_LOG_FAIL,
  ER_PASSWD_LENGTH,
  ER_UNKNOWN_TARGET_BINLOG,
  ER_IO_ERR_LOG_INDEX_READ,
  ER_BINLOG_PURGE_PROHIBITED,
  ER_FSEEK_FAIL,
  ER_BINLOG_PURGE_FATAL_ERR,
  ER_LOG_IN_USE,
  ER_LOG_PURGE_UNKNOWN_ERR,
  ER_RELAY_LOG_INIT,
  ER_NO_BINARY_LOGGING,
  ER_RESERVED_SYNTAX,
  ER_WSAS_FAILED,
  ER_DIFF_GROUPS_PROC,
  ER_NO_GROUP_FOR_PROC,
  ER_ORDER_WITH_PROC,
  ER_LOGGING_PROHIBIT_CHANGING_OF,
  ER_NO_FILE_MAPPING,
  ER_WRONG_MAGIC,
  ER_PS_MANY_PARAM,
  ER_KEY_PART_0,
  ER_VIEW_CHECKSUM,
  ER_VIEW_MULTIUPDATE,
  ER_VIEW_NO_INSERT_FIELD_LIST,
  ER_VIEW_DELETE_MERGE_VIEW,
  ER_CANNOT_USER,
  ER_XAER_NOTA,
  ER_XAER_INVAL,
  ER_XAER_RMFAIL,
  ER_XAER_OUTSIDE,
  ER_XAER_RMERR,
  ER_XA_RBROLLBACK,
  ER_NONEXISTING_PROC_GRANT,
  ER_PROC_AUTO_GRANT_FAIL,
  ER_PROC_AUTO_REVOKE_FAIL,
  ER_DATA_TOO_LONG,
  ER_SP_BAD_SQLSTATE,
  ER_STARTUP,
  ER_LOAD_FROM_FIXED_SIZE_ROWS_TO_VAR,
  ER_CANT_CREATE_USER_WITH_GRANT,
  ER_WRONG_VALUE_FOR_TYPE,
  ER_TABLE_DEF_CHANGED,
  ER_SP_DUP_HANDLER,
  ER_SP_NOT_VAR_ARG,
  ER_SP_NO_RETSET,
  ER_CANT_CREATE_GEOMETRY_OBJECT,
  ER_FAILED_ROUTINE_BREAK_BINLOG,
  ER_BINLOG_UNSAFE_ROUTINE,
  ER_BINLOG_CREATE_ROUTINE_NEED_SUPER,
  ER_EXEC_STMT_WITH_OPEN_CURSOR,
  ER_STMT_HAS_NO_OPEN_CURSOR,
  ER_COMMIT_NOT_ALLOWED_IN_SF_OR_TRG,
  ER_NO_DEFAULT_FOR_VIEW_FIELD,
  ER_SP_NO_RECURSION,
  ER_TOO_BIG_SCALE,
  ER_TOO_BIG_PRECISION,
  ER_M_BIGGER_THAN_D,
  ER_WRONG_LOCK_OF_SYSTEM_TABLE,
  ER_CONNECT_TO_FOREIGN_DATA_SOURCE,
  ER_QUERY_ON_FOREIGN_DATA_SOURCE,
  ER_FOREIGN_DATA_SOURCE_DOESNT_EXIST,
  ER_FOREIGN_DATA_STRING_INVALID_CANT_CREATE,
  ER_FOREIGN_DATA_STRING_INVALID,
  ER_CANT_CREATE_FEDERATED_TABLE,
  ER_TRG_IN_WRONG_SCHEMA,
  ER_STACK_OVERRUN_NEED_MORE=1436, // TODO: Test case looks for this int
  ER_TOO_LONG_BODY,
  ER_WARN_CANT_DROP_DEFAULT_KEYCACHE,
  ER_TOO_BIG_DISPLAYWIDTH,
  ER_XAER_DUPID,
  ER_DATETIME_FUNCTION_OVERFLOW,
  ER_CANT_UPDATE_USED_TABLE_IN_SF_OR_TRG,
  ER_VIEW_PREVENT_UPDATE,
  ER_PS_NO_RECURSION,
  ER_SP_CANT_SET_AUTOCOMMIT,
  ER_MALFORMED_DEFINER,
  ER_VIEW_FRM_NO_USER,
  ER_VIEW_OTHER_USER,
  ER_NO_SUCH_USER,
  ER_FORBID_SCHEMA_CHANGE,
  ER_ROW_IS_REFERENCED_2,
  ER_NO_REFERENCED_ROW_2,
  ER_SP_BAD_VAR_SHADOW,
  ER_TRG_NO_DEFINER,
  ER_OLD_FILE_FORMAT,
  ER_SP_RECURSION_LIMIT,
  ER_SP_PROC_TABLE_CORRUPT,
  ER_SP_WRONG_NAME,
  ER_TABLE_NEEDS_UPGRADE,
  ER_SP_NO_AGGREGATE,
  ER_MAX_PREPARED_STMT_COUNT_REACHED,
  ER_VIEW_RECURSIVE,
  ER_NON_GROUPING_FIELD_USED,
  ER_TABLE_CANT_HANDLE_SPKEYS,
  ER_NO_TRIGGERS_ON_SYSTEM_SCHEMA,
  ER_REMOVED_SPACES,
  ER_AUTOINC_READ_FAILED,
  ER_USERNAME,
  ER_HOSTNAME,
  ER_WRONG_STRING_LENGTH,
  ER_NON_INSERTABLE_TABLE,
  ER_ADMIN_WRONG_MRG_TABLE,
  ER_TOO_HIGH_LEVEL_OF_NESTING_FOR_SELECT,
  ER_NAME_BECOMES_EMPTY,
  ER_AMBIGUOUS_FIELD_TERM,
  ER_FOREIGN_SERVER_EXISTS,
  ER_FOREIGN_SERVER_DOESNT_EXIST,
  ER_ILLEGAL_HA_CREATE_OPTION,
  ER_PARTITION_REQUIRES_VALUES_ERROR,
  ER_PARTITION_WRONG_VALUES_ERROR,
  ER_PARTITION_MAXVALUE_ERROR,
  ER_PARTITION_SUBPARTITION_ERROR,
  ER_PARTITION_SUBPART_MIX_ERROR,
  ER_PARTITION_WRONG_NO_PART_ERROR,
  ER_PARTITION_WRONG_NO_SUBPART_ERROR,
  ER_CONST_EXPR_IN_PARTITION_FUNC_ERROR,
  ER_NO_CONST_EXPR_IN_RANGE_OR_LIST_ERROR,
  ER_FIELD_NOT_FOUND_PART_ERROR,
  ER_LIST_OF_FIELDS_ONLY_IN_HASH_ERROR,
  ER_INCONSISTENT_PARTITION_INFO_ERROR,
  ER_PARTITION_FUNC_NOT_ALLOWED_ERROR,
  ER_PARTITIONS_MUST_BE_DEFINED_ERROR,
  ER_RANGE_NOT_INCREASING_ERROR,
  ER_INCONSISTENT_TYPE_OF_FUNCTIONS_ERROR,
  ER_MULTIPLE_DEF_CONST_IN_LIST_PART_ERROR,
  ER_PARTITION_ENTRY_ERROR,
  ER_MIX_HANDLER_ERROR,
  ER_PARTITION_NOT_DEFINED_ERROR,
  ER_TOO_MANY_PARTITIONS_ERROR,
  ER_SUBPARTITION_ERROR,
  ER_CANT_CREATE_HANDLER_FILE,
  ER_BLOB_FIELD_IN_PART_FUNC_ERROR,
  ER_UNIQUE_KEY_NEED_ALL_FIELDS_IN_PF,
  ER_NO_PARTS_ERROR,
  ER_PARTITION_MGMT_ON_NONPARTITIONED,
  ER_FOREIGN_KEY_ON_PARTITIONED,
  ER_DROP_PARTITION_NON_EXISTENT,
  ER_DROP_LAST_PARTITION,
  ER_COALESCE_ONLY_ON_HASH_PARTITION,
  ER_REORG_HASH_ONLY_ON_SAME_NO,
  ER_REORG_NO_PARAM_ERROR,
  ER_ONLY_ON_RANGE_LIST_PARTITION,
  ER_ADD_PARTITION_SUBPART_ERROR,
  ER_ADD_PARTITION_NO_NEW_PARTITION,
  ER_COALESCE_PARTITION_NO_PARTITION,
  ER_REORG_PARTITION_NOT_EXIST,
  ER_SAME_NAME_PARTITION,
  ER_NO_BINLOG_ERROR,
  ER_CONSECUTIVE_REORG_PARTITIONS,
  ER_REORG_OUTSIDE_RANGE,
  ER_PARTITION_FUNCTION_FAILURE,
  ER_PART_STATE_ERROR,
  ER_LIMITED_PART_RANGE,
  ER_PLUGIN_IS_NOT_LOADED,
  ER_WRONG_VALUE,
  ER_NO_PARTITION_FOR_GIVEN_VALUE,
  ER_FILEGROUP_OPTION_ONLY_ONCE,
  ER_CREATE_FILEGROUP_FAILED,
  ER_DROP_FILEGROUP_FAILED,
  ER_TABLESPACE_AUTO_EXTEND_ERROR,
  ER_WRONG_SIZE_NUMBER,
  ER_SIZE_OVERFLOW_ERROR,
  ER_ALTER_FILEGROUP_FAILED,
  ER_BINLOG_ROW_LOGGING_FAILED,
  ER_BINLOG_ROW_WRONG_TABLE_DEF,
  ER_BINLOG_ROW_RBR_TO_SBR,
  ER_EVENT_ALREADY_EXISTS,
  ER_EVENT_STORE_FAILED,
  ER_EVENT_DOES_NOT_EXIST,
  ER_EVENT_CANT_ALTER,
  ER_EVENT_DROP_FAILED,
  ER_EVENT_INTERVAL_NOT_POSITIVE_OR_TOO_BIG,
  ER_EVENT_ENDS_BEFORE_STARTS,
  ER_EVENT_EXEC_TIME_IN_THE_PAST,
  ER_EVENT_OPEN_TABLE_FAILED,
  ER_EVENT_NEITHER_M_EXPR_NOR_M_AT,
  ER_COL_COUNT_DOESNT_MATCH_CORRUPTED,
  ER_CANNOT_LOAD_FROM_TABLE,
  ER_EVENT_CANNOT_DELETE,
  ER_EVENT_COMPILE_ERROR,
  ER_EVENT_SAME_NAME,
  ER_EVENT_DATA_TOO_LONG,
  ER_DROP_INDEX_FK,
  ER_WARN_DEPRECATED_SYNTAX_WITH_VER,
  ER_CANT_WRITE_LOCK_LOG_TABLE,
  ER_CANT_LOCK_LOG_TABLE,
  ER_FOREIGN_DUPLICATE_KEY,
  ER_COL_COUNT_DOESNT_MATCH_PLEASE_UPDATE,
  ER_TEMP_TABLE_PREVENTS_SWITCH_OUT_OF_RBR,
  ER_STORED_FUNCTION_PREVENTS_SWITCH_BINLOG_FORMAT,
  ER_NDB_CANT_SWITCH_BINLOG_FORMAT,
  ER_PARTITION_NO_TEMPORARY,
  ER_PARTITION_CONST_DOMAIN_ERROR,
  ER_PARTITION_FUNCTION_IS_NOT_ALLOWED,
  ER_DDL_LOG_ERROR,
  ER_NULL_IN_VALUES_LESS_THAN,
  ER_WRONG_PARTITION_NAME,
  ER_CANT_CHANGE_TX_ISOLATION,
  ER_DUP_ENTRY_AUTOINCREMENT_CASE,
  ER_EVENT_MODIFY_QUEUE_ERROR,
  ER_EVENT_SET_VAR_ERROR,
  ER_PARTITION_MERGE_ERROR,
  ER_CANT_ACTIVATE_LOG,
  ER_RBR_NOT_AVAILABLE,
  ER_BASE64_DECODE_ERROR,
  ER_EVENT_RECURSION_FORBIDDEN,
  ER_EVENTS_DB_ERROR,
  ER_ONLY_INTEGERS_ALLOWED,
  ER_UNSUPORTED_LOG_ENGINE,
  ER_BAD_LOG_STATEMENT,
  ER_CANT_RENAME_LOG_TABLE,
  ER_WRONG_PARAMCOUNT_TO_FUNCTION,
  ER_WRONG_PARAMETERS_TO_NATIVE_FCT,
  ER_WRONG_PARAMETERS_TO_STORED_FCT,
  ER_NATIVE_FCT_NAME_COLLISION,
  ER_DUP_ENTRY_WITH_KEY_NAME,
  ER_BINLOG_PURGE_EMFILE,
  ER_EVENT_CANNOT_CREATE_IN_THE_PAST,
  ER_EVENT_CANNOT_ALTER_IN_THE_PAST,
  ER_SLAVE_INCIDENT,
  ER_NO_PARTITION_FOR_GIVEN_VALUE_SILENT,
  ER_BINLOG_UNSAFE_STATEMENT,
  ER_SLAVE_FATAL_ERROR,
  ER_SLAVE_RELAY_LOG_READ_FAILURE,
  ER_SLAVE_RELAY_LOG_WRITE_FAILURE,
  ER_SLAVE_CREATE_EVENT_FAILURE,
  ER_SLAVE_MASTER_COM_FAILURE,
  ER_BINLOG_LOGGING_IMPOSSIBLE,
  ER_VIEW_NO_CREATION_CTX,
  ER_VIEW_INVALID_CREATION_CTX,
  ER_SR_INVALID_CREATION_CTX,
  ER_TRG_CORRUPTED_FILE,
  ER_TRG_NO_CREATION_CTX,
  ER_TRG_INVALID_CREATION_CTX,
  ER_EVENT_INVALID_CREATION_CTX,
  ER_TRG_CANT_OPEN_TABLE,
  ER_CANT_CREATE_SROUTINE,
  ER_SLAVE_AMBIGOUS_EXEC_MODE,
  ER_NO_FORMAT_DESCRIPTION_EVENT_BEFORE_BINLOG_STATEMENT,
  ER_SLAVE_CORRUPT_EVENT,
  ER_LOAD_DATA_INVALID_COLUMN,
  ER_LOG_PURGE_NO_FILE,
  ER_WARN_AUTO_CONVERT_LOCK,
  ER_NO_AUTO_CONVERT_LOCK_STRICT,
  ER_NO_AUTO_CONVERT_LOCK_TRANSACTION,
  ER_NO_STORAGE_ENGINE,
  ER_BACKUP_BACKUP_START,
  ER_BACKUP_BACKUP_DONE,
  ER_BACKUP_RESTORE_START,
  ER_BACKUP_RESTORE_DONE,
  ER_BACKUP_NOTHING_TO_BACKUP,
  ER_BACKUP_CANNOT_INCLUDE_DB,
  ER_BACKUP_BACKUP,
  ER_BACKUP_RESTORE,
  ER_BACKUP_RUNNING,
  ER_BACKUP_BACKUP_PREPARE,
  ER_BACKUP_RESTORE_PREPARE,
  ER_BACKUP_INVALID_LOC,
  ER_BACKUP_READ_LOC,
  ER_BACKUP_WRITE_LOC,
  ER_BACKUP_LIST_DBS,
  ER_BACKUP_LIST_TABLES,
  ER_BACKUP_LIST_DB_TABLES,
  ER_BACKUP_SKIP_VIEW,
  ER_BACKUP_NO_ENGINE,
  ER_BACKUP_TABLE_OPEN,
  ER_BACKUP_READ_HEADER,
  ER_BACKUP_WRITE_HEADER,
  ER_BACKUP_NO_BACKUP_DRIVER,
  ER_BACKUP_NOT_ACCEPTED,
  ER_BACKUP_CREATE_BACKUP_DRIVER,
  ER_BACKUP_CREATE_RESTORE_DRIVER,
  ER_BACKUP_TOO_MANY_IMAGES,
  ER_BACKUP_WRITE_META,
  ER_BACKUP_READ_META,
  ER_BACKUP_CREATE_META,
  ER_BACKUP_GET_BUF,
  ER_BACKUP_WRITE_DATA,
  ER_BACKUP_READ_DATA,
  ER_BACKUP_NEXT_CHUNK,
  ER_BACKUP_INIT_BACKUP_DRIVER,
  ER_BACKUP_INIT_RESTORE_DRIVER,
  ER_BACKUP_STOP_BACKUP_DRIVER,
  ER_BACKUP_STOP_RESTORE_DRIVERS,
  ER_BACKUP_PREPARE_DRIVER,
  ER_BACKUP_CREATE_VP,
  ER_BACKUP_UNLOCK_DRIVER,
  ER_BACKUP_CANCEL_BACKUP,
  ER_BACKUP_CANCEL_RESTORE,
  ER_BACKUP_GET_DATA,
  ER_BACKUP_SEND_DATA,
  ER_BACKUP_SEND_DATA_RETRY,
  ER_BACKUP_OPEN_TABLES,
  ER_BACKUP_THREAD_INIT,
  ER_BACKUP_PROGRESS_TABLES,
  ER_TABLESPACE_EXIST,
  ER_NO_SUCH_TABLESPACE,
  ER_SLAVE_HEARTBEAT_FAILURE,
  ER_SLAVE_HEARTBEAT_VALUE_OUT_OF_RANGE,
  ER_BACKUP_LOG_WRITE_ERROR,
  ER_TABLESPACE_NOT_EMPTY,
  ER_BACKUP_TS_CHANGE,
  ER_VCOL_BASED_ON_VCOL,
  ER_VIRTUAL_COLUMN_FUNCTION_IS_NOT_ALLOWED,
  ER_DATA_CONVERSION_ERROR_FOR_VIRTUAL_COLUMN,
  ER_PRIMARY_KEY_BASED_ON_VIRTUAL_COLUMN,
  ER_KEY_BASED_ON_GENERATED_VIRTUAL_COLUMN,
  ER_WRONG_FK_OPTION_FOR_VIRTUAL_COLUMN,
  ER_WARNING_NON_DEFAULT_VALUE_FOR_VIRTUAL_COLUMN,
  ER_UNSUPPORTED_ACTION_ON_VIRTUAL_COLUMN,
  ER_CONST_EXPR_IN_VCOL,
  ER_UNKNOWN_TEMPORAL_TYPE,
  ER_INVALID_STRING_FORMAT_FOR_DATE,
  ER_INVALID_STRING_FORMAT_FOR_TIME,
  ER_INVALID_UNIX_TIMESTAMP_VALUE,
  ER_INVALID_DATETIME_VALUE,
  ER_INVALID_NULL_ARGUMENT,
  ER_INVALID_NEGATIVE_ARGUMENT,
  ER_ARGUMENT_OUT_OF_RANGE,
  ER_INVALID_TIME_VALUE,
  ER_INVALID_ENUM_VALUE,
  ER_NO_PRIMARY_KEY_ON_REPLICATED_TABLE,
  ER_CORRUPT_TABLE_DEFINITION,
  ER_ERROR_LAST= ER_CORRUPT_TABLE_DEFINITION
};

enum drizzle_exit_codes {
  EXIT_UNSPECIFIED_ERROR = 1,
  EXIT_UNKNOWN_OPTION,
  EXIT_AMBIGUOUS_OPTION,
  EXIT_NO_ARGUMENT_ALLOWED,
  EXIT_ARGUMENT_REQUIRED,
  EXIT_VAR_PREFIX_NOT_UNIQUE,
  EXIT_UNKNOWN_VARIABLE,
  EXIT_OUT_OF_MEMORY,
  EXIT_UNKNOWN_SUFFIX,
  EXIT_NO_PTR_TO_VARIABLE,
  EXIT_CANNOT_CONNECT_TO_SERVICE,
  EXIT_OPTION_DISABLED,
  EXIT_ARGUMENT_INVALID
};


#define GLOBERRS (EE_ERROR_LAST - EE_ERROR_FIRST + 1) /* Nr of global errors */
#define EE(X)    (globerrs[(X) - EE_ERROR_FIRST])

/* Error message numbers in global map */
extern const char * globerrs[GLOBERRS];

void init_glob_errs(void);
void my_error(int nr,myf MyFlags, ...);
void my_printf_error(uint32_t my_err, const char *format,
                     myf MyFlags, ...)
                     __attribute__((format(printf, 2, 4)));
int my_error_register(const char **errmsgs, int first, int last);
void my_error_unregister_all(void);
const char **my_error_unregister(int first, int last);
void my_message(uint32_t my_err, const char *str,myf MyFlags);
void my_message_no_curses(uint32_t my_err, const char *str,myf MyFlags);

} /* namespace drizzled */

#endif /* DRIZZLED_ERROR_H */
