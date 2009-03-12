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

#ifndef DRIZZLE_SERVER_SLAVE_H
#define DRIZZLE_SERVER_SLAVE_H

/**
  @defgroup Replication Replication
  @{

  @file
*/

/** 
   Some of defines are need in parser even though replication is not 
   compiled in (embedded).
*/


#include <drizzled/log.h>
#include <mysys/my_list.h>
#include <libdrizzle/libdrizzle.h>
#include <mysys/hash.h>
#include <drizzled/replication/tblmap.h>


// Forward declarations
class Relay_log_info;
class Master_info;


/*****************************************************************************

  MySQL Replication

  Replication is implemented via two types of threads:

    I/O Thread - One of these threads is started for each master server.
                 They maintain a connection to their master server, read log
                 events from the master as they arrive, and queues them into
                 a single, shared relay log file.  A Master_info 
                 represents each of these threads.

    SQL Thread - One of these threads is started and reads from the relay log
                 file, executing each event.  A Relay_log_info 
                 represents this thread.

  Buffering in the relay log file makes it unnecessary to reread events from
  a master server across a slave restart.  It also decouples the slave from
  the master where long-running updates and event logging are concerned--ie
  it can continue to log new events while a slow query executes on the slave.

*****************************************************************************/

/*
  MUTEXES in replication:

  LOCK_active_mi: [note: this was originally meant for multimaster, to switch
  from a master to another, to protect active_mi] It is used to SERIALIZE ALL
  administrative commands of replication: START SLAVE, STOP SLAVE, CHANGE
  MASTER, RESET SLAVE, end_slave() (when mysqld stops) [init_slave() does not
  need it it's called early]. Any of these commands holds the mutex from the
  start till the end. This thus protects us against a handful of deadlocks
  (consider start_slave_thread() which, when starting the I/O thread, releases
  mi->run_lock, keeps rli->run_lock, and tries to re-acquire mi->run_lock).

  Currently active_mi never moves (it's created at startup and deleted at
  shutdown, and not changed: it always points to the same Master_info struct),
  because we don't have multimaster. So for the moment, mi does not move, and
  mi->rli does not either.

  In Master_info: run_lock, data_lock
  run_lock protects all information about the run state: slave_running, session
  and the existence of the I/O thread to stop/start it, you need this mutex).
  data_lock protects some moving members of the struct: counters (log name,
  position) and relay log (DRIZZLE_BIN_LOG object).

  In Relay_log_info: run_lock, data_lock
  see Master_info
  
  Order of acquisition: if you want to have LOCK_active_mi and a run_lock, you
  must acquire LOCK_active_mi first.

  In DRIZZLE_BIN_LOG: LOCK_log, LOCK_index of the binlog and the relay log
  LOCK_log: when you write to it. LOCK_index: when you create/delete a binlog
  (so that you have to update the .index file).
*/

extern uint32_t master_retry_count;
extern MY_BITMAP slave_error_mask;
extern bool use_slave_mask;
extern char *slave_load_tmpdir;
extern char *master_info_file, *relay_log_info_file;
extern char *opt_relay_logname, *opt_relaylog_index_name;
extern bool opt_skip_slave_start;
extern bool opt_reckless_slave;
extern bool opt_log_slave_updates;
extern uint64_t relay_log_space_limit;

/*
  3 possible values for Master_info::slave_running and
  Relay_log_info::slave_running.
  The values 0,1,2 are very important: to keep the diff small, I didn't
  substitute places where we use 0/1 with the newly defined symbols. So don't change
  these values.
  The same way, code is assuming that in Relay_log_info we use only values
  0/1.
  I started with using an enum, but
  enum_variable=1; is not legal so would have required many line changes.
*/
#define DRIZZLE_SLAVE_NOT_RUN         0
#define DRIZZLE_SLAVE_RUN_NOT_CONNECT 1
#define DRIZZLE_SLAVE_RUN_CONNECT     2

#define RPL_LOG_NAME (rli->group_master_log_name.length() ? rli->group_master_log_name.c_str() : "FIRST")
#define IO_RPL_LOG_NAME (mi->getLogName() ? mi->getLogName() : "FIRST")

/*
  If the following is set, if first gives an error, second will be
  tried. Otherwise, if first fails, we fail.
*/
#define SLAVE_FORCE_ALL 4

int32_t init_slave();
void init_slave_skip_errors(const char* arg);
bool flush_relay_log_info(Relay_log_info* rli);
int32_t register_slave_on_master(DRIZZLE *drizzle);
int32_t terminate_slave_threads(Master_info* mi, int32_t thread_mask,
			     bool skip_lock = 0);
int32_t start_slave_threads(bool need_slave_mutex, bool wait_for_start,
			Master_info* mi, const char* master_info_fname,
			const char* slave_info_fname, int32_t thread_mask);
/*
  cond_lock is usually same as start_lock. It is needed for the case when
  start_lock is 0 which happens if start_slave_thread() is called already
  inside the start_lock section, but at the same time we want a
  pthread_cond_wait() on start_cond,start_lock
*/
int32_t start_slave_thread(pthread_handler h_func, pthread_mutex_t* start_lock,
		       pthread_mutex_t *cond_lock,
		       pthread_cond_t* start_cond,
		       volatile uint32_t *slave_running,
		       volatile uint32_t *slave_run_id,
		       Master_info* mi,
                       bool high_priority);

/* If fd is -1, dump to NET */
int32_t mysql_table_dump(Session* session, const char* db,
		     const char* tbl_name, int32_t fd = -1);

/* retrieve table from master and copy to slave*/
int32_t fetch_master_table(Session* session, const char* db_name, const char* table_name,
		       Master_info* mi, DRIZZLE *drizzle, bool overwrite);

bool show_master_info(Session* session, Master_info* mi);
bool show_binlog_info(Session* session);
bool rpl_master_has_bug(Relay_log_info *rli, uint32_t bug_id, bool report= true);
bool rpl_master_erroneous_autoinc(Session* session);

const char *print_slave_db_safe(const char *db);
int32_t check_expected_error(Session* session, Relay_log_info const *rli, int32_t error_code);
void skip_load_data_infile(NET* net);

void end_slave(); /* clean up */
void clear_until_condition(Relay_log_info* rli);
void clear_slave_error(Relay_log_info* rli);
void end_relay_log_info(Relay_log_info* rli);
void lock_slave_threads(Master_info* mi);
void unlock_slave_threads(Master_info* mi);
void init_thread_mask(int32_t* mask,Master_info* mi,bool inverse);
int32_t init_relay_log_pos(Relay_log_info* rli,const char* log,uint64_t pos,
		       bool need_data_lock, const char** errmsg,
                       bool look_for_description_event);

int32_t purge_relay_logs(Relay_log_info* rli, Session *session, bool just_reset,
		     const char** errmsg);
void set_slave_thread_options(Session* session);
void rotate_relay_log(Master_info* mi);
int32_t apply_event_and_update_pos(Log_event* ev, Session* session, Relay_log_info* rli,
                               bool skip);

pthread_handler_t handle_slave_io(void *arg);
pthread_handler_t handle_slave_sql(void *arg);
extern bool volatile abort_loop;
extern Master_info main_mi, *active_mi; /* active_mi for multi-master */
extern LIST master_list;
extern bool replicate_same_server_id;

extern int32_t disconnect_slave_event_count, abort_slave_event_count ;

/* the master variables are defaults read from drizzle.cnf or command line */
extern uint32_t master_port, master_connect_retry, report_port;
extern char * master_user, *master_password, *master_host;
extern char *master_info_file, *relay_log_info_file, *report_user;
extern char *report_host, *report_password;

extern bool master_ssl;
extern char *master_ssl_ca, *master_ssl_capath, *master_ssl_cert;
extern char *master_ssl_cipher, *master_ssl_key;
       
extern I_List<Session> threads;


/**
  @} (end of group Replication)
*/

#endif /* DRIZZLE_SERVER_SLAVE_H */