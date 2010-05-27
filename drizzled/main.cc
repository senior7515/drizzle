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

#include "config.h"

#include <pthread.h>
#include <signal.h>
#include <sys/resource.h>
#include <unistd.h>

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#if defined(HAVE_LOCALE_H)
# include <locale.h>
#endif


#include "drizzled/plugin.h"
#include "drizzled/gettext.h"
#include "drizzled/configmake.h"
#include "drizzled/session.h"
#include "drizzled/internal/my_sys.h"
#include "drizzled/unireg.h"
#include "drizzled/stacktrace.h"
#include "drizzled/drizzled.h"
#include "drizzled/errmsg_print.h"
#include "drizzled/data_home.h"
#include "drizzled/plugin/listen.h"
#include "drizzled/plugin/client.h"
#include "drizzled/pthread_globals.h"
#include "drizzled/tztime.h"
#include "drizzled/signal_handler.h"
#include "drizzled/replication_services.h"

using namespace drizzled;
using namespace std;

static pthread_t select_thread;
static uint32_t thr_kill_signal;

/**
  All global error messages are sent here where the first one is stored
  for the client.
*/
static void my_message_sql(uint32_t error, const char *str, myf MyFlags)
{
  Session *session;
  /*
    Put here following assertion when situation with EE_* error codes
    will be fixed
  */
  if ((session= current_session))
  {
    if (MyFlags & ME_FATALERROR)
      session->is_fatal_error= 1;

    /*
      TODO: There are two exceptions mechanism (Session and sp_rcontext),
      this could be improved by having a common stack of handlers.
    */
    if (session->handle_error(error, str,
                          DRIZZLE_ERROR::WARN_LEVEL_ERROR))
      return;;

    /*
      session->lex->current_select == 0 if lex structure is not inited
      (not query command (COM_QUERY))
    */
    if (! (session->lex->current_select &&
        session->lex->current_select->no_error && !session->is_fatal_error))
    {
      if (! session->main_da.is_error())            // Return only first message
      {
        if (error == 0)
          error= ER_UNKNOWN_ERROR;
        if (str == NULL)
          str= ER(error);
        session->main_da.set_error_status(error, str);
      }
    }

    if (!session->no_warnings_for_error && !session->is_fatal_error)
    {
      /*
        Suppress infinite recursion if there a memory allocation error
        inside push_warning.
      */
      session->no_warnings_for_error= true;
      push_warning(session, DRIZZLE_ERROR::WARN_LEVEL_ERROR, error, str);
      session->no_warnings_for_error= false;
      }
    }
    if (!session || MyFlags & ME_NOREFRESH)
        errmsg_printf(ERRMSG_LVL_ERROR, "%s: %s",internal::my_progname,str);
}

static void init_signals(void)
{
  sigset_t set;
  struct sigaction sa;

  if (!(test_flags.test(TEST_NO_STACKTRACE) || 
        test_flags.test(TEST_CORE_ON_SIGNAL)))
  {
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigprocmask(SIG_SETMASK,&sa.sa_mask,NULL);

    init_stacktrace();
    sa.sa_handler= drizzled_handle_segfault;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, NULL);
#endif
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
  }

  if (test_flags.test(TEST_CORE_ON_SIGNAL))
  {
    /* Change limits so that we will get a core file */
    struct rlimit rl;
    rl.rlim_cur = rl.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &rl) && global_system_variables.log_warnings)
        errmsg_printf(ERRMSG_LVL_WARN,
                      _("setrlimit could not change the size of core files "
                        "to 'infinity';  We may not be able to generate a "
                        "core file on signals"));
  }
  (void) sigemptyset(&set);
  ignore_signal(SIGPIPE);
  sigaddset(&set,SIGPIPE);
#ifndef IGNORE_SIGHUP_SIGQUIT
  sigaddset(&set,SIGQUIT);
  sigaddset(&set,SIGHUP);
#endif
  sigaddset(&set,SIGTERM);

  /* Fix signals if blocked by parents (can happen on Mac OS X) */
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sa.sa_handler = drizzled_print_signal_warning;
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_flags = 0;
  sa.sa_handler = drizzled_print_signal_warning;
  sigaction(SIGHUP, &sa, NULL);
#ifdef SIGTSTP
  sigaddset(&set,SIGTSTP);
#endif
  if (test_flags.test(TEST_SIGINT))
  {
    sa.sa_flags= 0;
    sa.sa_handler= drizzled_end_thread_signal;
    sigaction(thr_kill_signal, &sa, NULL);

    // May be SIGINT
    sigdelset(&set, thr_kill_signal);
  }
  else
    sigaddset(&set,SIGINT);
  sigprocmask(SIG_SETMASK,&set,NULL);
  pthread_sigmask(SIG_SETMASK,&set,NULL);
  return;
}


int main(int argc, char **argv)
{
#if defined(ENABLE_NLS)
# if defined(HAVE_LOCALE_H)
  setlocale(LC_ALL, "");
# endif
  bindtextdomain("drizzle", LOCALEDIR);
  textdomain("drizzle");
#endif

  module::Registry &modules= module::Registry::singleton();
  plugin::Client *client;
  Session *session;

  MY_INIT(argv[0]);		// init my_sys library & pthreads
  /* nothing should come before this line ^^^ */

  /* Set signal used to kill Drizzle */
#if defined(SIGUSR2)
  thr_kill_signal= internal::thd_lib_detected == THD_LIB_LT ? SIGINT : SIGUSR2;
#else
  thr_kill_signal= SIGINT;
#endif

  if (init_common_variables(DRIZZLE_CONFIG_NAME,
			    argc, argv, load_default_groups))
    unireg_abort(1);				// Will do exit

  init_signals();


  select_thread=pthread_self();
  select_thread_in_use=1;

  if (chdir(data_home_real) && !opt_help)
  {
    errmsg_printf(ERRMSG_LVL_ERROR, _("Data directory %s does not exist\n"), data_home_real);
    unireg_abort(1);
  }
  data_home= data_home_buff;
  data_home[0]=FN_CURLIB;		// all paths are relative from here
  data_home[1]=0;
  data_home_len= 2;

  if ((user_info= check_user(drizzled_user)))
  {
    set_user(drizzled_user, user_info);
  }

  if (server_id == 0)
  {
    server_id= 1;
  }

  if (init_server_components(modules))
    unireg_abort(1);

  /**
   * This check must be done after init_server_components for now
   * because we don't yet have plugin dependency tracking...
   *
   * ReplicationServices::evaluateRegisteredPlugins() will print error messages to stderr
   * via errmsg_printf().
   *
   * @todo
   *
   * not checking return since unireg_abort() hangs
   */
  ReplicationServices &replication_services= ReplicationServices::singleton();
    (void) replication_services.evaluateRegisteredPlugins();

  if (plugin::Listen::setup())
    unireg_abort(1);

  /*
    init signals & alarm
    After this we can't quit by a simple unireg_abort
  */
  error_handler_hook= my_message_sql;

  assert(plugin::num_trx_monitored_objects > 0);
  if (drizzle_rm_tmp_tables() ||
      my_tz_init((Session *)0, default_tz_name))
  {
    abort_loop= true;
    select_thread_in_use=0;
    (void) pthread_kill(signal_thread, SIGTERM);

    (void) unlink(pidfile_name);	// Not needed anymore

    exit(1);
  }

  init_status_vars();

  errmsg_printf(ERRMSG_LVL_INFO, _(ER(ER_STARTUP)), internal::my_progname,
                PANDORA_RELEASE_VERSION, COMPILATION_COMMENT);


  /* Listen for new connections and start new session for each connection
     accepted. The listen.getClient() method will return NULL when the server
     should be shutdown. */
  while ((client= plugin::Listen::getClient()) != NULL)
  {
    if (!(session= new Session(client)))
    {
      delete client;
      continue;
    }

    /* If we error on creation we drop the connection and delete the session. */
    if (session->schedule())
      Session::unlink(session);
  }

  /* (void) pthread_attr_destroy(&connection_attrib); */


  (void) pthread_mutex_lock(&LOCK_thread_count);
  select_thread_in_use=0;			// For close_connections
  (void) pthread_mutex_unlock(&LOCK_thread_count);
  (void) pthread_cond_broadcast(&COND_thread_count);

  /* Wait until cleanup is done */
  (void) pthread_mutex_lock(&LOCK_thread_count);
  while (!ready_to_exit)
    pthread_cond_wait(&COND_server_end,&LOCK_thread_count);
  (void) pthread_mutex_unlock(&LOCK_thread_count);

  clean_up(1);
  module::Registry::shutdown();
  clean_up_mutexes();
  internal::my_end();
  return 0;
}
