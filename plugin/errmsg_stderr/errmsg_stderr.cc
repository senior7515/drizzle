/* drizzle/plugin/errmsg_stderr/errmsg_stderr.cc */

/* need to define DRIZZLE_SERVER to get inside the THD */
#define DRIZZLE_SERVER 1
#include <drizzled/server_includes.h>
#include <drizzled/plugin_errmsg.h>

#include <stdio.h>  /* for vsnprintf */
#include <stdarg.h>  /* for va_list */
#include <unistd.h>  /* for write(2) */

bool errmsg_stderr_func (THD *thd, int priority, const char *format, va_list ap)
{
  char msgbuf[MAX_MSG_LEN];
  int prv, wrv;

  if (priority > 0)
    prv = vsnprintf(msgbuf, MAX_MSG_LEN, format, ap);
  if (prv < 0) return true;

  /* a single write has a OS level thread lock
     so there is no need to have mutexes guarding this write,
  */
  wrv= write(2, msgbuf, prv);
  if ((wrv < 0) || (wrv != prv)) return true;

  return false;
}

static int errmsg_stderr_plugin_init(void *p)
{
  errmsg_t *l= (errmsg_t *) p;

  l->errmsg_func= errmsg_stderr_func;

  return 0;
}

static int errmsg_stderr_plugin_deinit(void *p)
{
  errmsg_st *l= (errmsg_st *) p;

  l->errmsg_func= NULL;

  return 0;
}

mysql_declare_plugin(errmsg_stderr)
{
  DRIZZLE_LOGGER_PLUGIN,
  "errmsg_stderr",
  "0.1",
  "Mark Atwood <mark@fallenpegasus.com>",
  "Error Messages to stderr",
  PLUGIN_LICENSE_GPL,
  errmsg_stderr_plugin_init,
  errmsg_stderr_plugin_deinit,
  NULL,   /* status variables */
  NULL,  /* system variables */
  NULL
}
mysql_declare_plugin_end;
