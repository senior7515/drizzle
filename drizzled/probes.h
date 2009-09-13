/*
 * Generated by dtrace(1M).
 */

#ifndef	_PROBES_H
#define	_PROBES_H

#include <unistd.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if _DTRACE_VERSION

#define	DRIZZLE_COMMAND_DONE(arg0) \
	__dtrace_drizzle___command__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_COMMAND_DONE_ENABLED() \
	__dtraceenabled_drizzle___command__done()
#else
#define	DRIZZLE_COMMAND_DONE_ENABLED() \
	__dtraceenabled_drizzle___command__done(0)
#endif
#define	DRIZZLE_COMMAND_START(arg0, arg1) \
	__dtrace_drizzle___command__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_COMMAND_START_ENABLED() \
	__dtraceenabled_drizzle___command__start()
#else
#define	DRIZZLE_COMMAND_START_ENABLED() \
	__dtraceenabled_drizzle___command__start(0)
#endif
#define	DRIZZLE_CONNECTION_DONE(arg0) \
	__dtrace_drizzle___connection__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_CONNECTION_DONE_ENABLED() \
	__dtraceenabled_drizzle___connection__done()
#else
#define	DRIZZLE_CONNECTION_DONE_ENABLED() \
	__dtraceenabled_drizzle___connection__done(0)
#endif
#define	DRIZZLE_CONNECTION_START(arg0) \
	__dtrace_drizzle___connection__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_CONNECTION_START_ENABLED() \
	__dtraceenabled_drizzle___connection__start()
#else
#define	DRIZZLE_CONNECTION_START_ENABLED() \
	__dtraceenabled_drizzle___connection__start(0)
#endif
#define	DRIZZLE_DELETE_DONE(arg0, arg1) \
	__dtrace_drizzle___delete__done(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_DELETE_DONE_ENABLED() \
	__dtraceenabled_drizzle___delete__done()
#else
#define	DRIZZLE_DELETE_DONE_ENABLED() \
	__dtraceenabled_drizzle___delete__done(0)
#endif
#define	DRIZZLE_DELETE_ROW_DONE(arg0) \
	__dtrace_drizzle___delete__row__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_DELETE_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___delete__row__done()
#else
#define	DRIZZLE_DELETE_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___delete__row__done(0)
#endif
#define	DRIZZLE_DELETE_ROW_START(arg0, arg1) \
	__dtrace_drizzle___delete__row__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_DELETE_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___delete__row__start()
#else
#define	DRIZZLE_DELETE_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___delete__row__start(0)
#endif
#define	DRIZZLE_DELETE_START(arg0) \
	__dtrace_drizzle___delete__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_DELETE_START_ENABLED() \
	__dtraceenabled_drizzle___delete__start()
#else
#define	DRIZZLE_DELETE_START_ENABLED() \
	__dtraceenabled_drizzle___delete__start(0)
#endif
#define	DRIZZLE_FILESORT_DONE(arg0, arg1) \
	__dtrace_drizzle___filesort__done(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_FILESORT_DONE_ENABLED() \
	__dtraceenabled_drizzle___filesort__done()
#else
#define	DRIZZLE_FILESORT_DONE_ENABLED() \
	__dtraceenabled_drizzle___filesort__done(0)
#endif
#define	DRIZZLE_FILESORT_START(arg0, arg1) \
	__dtrace_drizzle___filesort__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_FILESORT_START_ENABLED() \
	__dtraceenabled_drizzle___filesort__start()
#else
#define	DRIZZLE_FILESORT_START_ENABLED() \
	__dtraceenabled_drizzle___filesort__start(0)
#endif
#define	DRIZZLE_HANDLER_RDLOCK_DONE(arg0) \
	__dtrace_drizzle___handler__rdlock__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_RDLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__rdlock__done()
#else
#define	DRIZZLE_HANDLER_RDLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__rdlock__done(0)
#endif
#define	DRIZZLE_HANDLER_RDLOCK_START(arg0, arg1) \
	__dtrace_drizzle___handler__rdlock__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_RDLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__rdlock__start()
#else
#define	DRIZZLE_HANDLER_RDLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__rdlock__start(0)
#endif
#define	DRIZZLE_HANDLER_UNLOCK_DONE(arg0) \
	__dtrace_drizzle___handler__unlock__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_UNLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__unlock__done()
#else
#define	DRIZZLE_HANDLER_UNLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__unlock__done(0)
#endif
#define	DRIZZLE_HANDLER_UNLOCK_START(arg0, arg1) \
	__dtrace_drizzle___handler__unlock__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_UNLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__unlock__start()
#else
#define	DRIZZLE_HANDLER_UNLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__unlock__start(0)
#endif
#define	DRIZZLE_HANDLER_WRLOCK_DONE(arg0) \
	__dtrace_drizzle___handler__wrlock__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_WRLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__wrlock__done()
#else
#define	DRIZZLE_HANDLER_WRLOCK_DONE_ENABLED() \
	__dtraceenabled_drizzle___handler__wrlock__done(0)
#endif
#define	DRIZZLE_HANDLER_WRLOCK_START(arg0, arg1) \
	__dtrace_drizzle___handler__wrlock__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_HANDLER_WRLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__wrlock__start()
#else
#define	DRIZZLE_HANDLER_WRLOCK_START_ENABLED() \
	__dtraceenabled_drizzle___handler__wrlock__start(0)
#endif
#define	DRIZZLE_INSERT_DONE(arg0, arg1) \
	__dtrace_drizzle___insert__done(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_INSERT_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__done()
#else
#define	DRIZZLE_INSERT_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__done(0)
#endif
#define	DRIZZLE_INSERT_ROW_DONE(arg0) \
	__dtrace_drizzle___insert__row__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_INSERT_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__row__done()
#else
#define	DRIZZLE_INSERT_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__row__done(0)
#endif
#define	DRIZZLE_INSERT_ROW_START(arg0, arg1) \
	__dtrace_drizzle___insert__row__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_INSERT_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___insert__row__start()
#else
#define	DRIZZLE_INSERT_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___insert__row__start(0)
#endif
#define	DRIZZLE_INSERT_SELECT_DONE(arg0, arg1) \
	__dtrace_drizzle___insert__select__done(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_INSERT_SELECT_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__select__done()
#else
#define	DRIZZLE_INSERT_SELECT_DONE_ENABLED() \
	__dtraceenabled_drizzle___insert__select__done(0)
#endif
#define	DRIZZLE_INSERT_SELECT_START(arg0) \
	__dtrace_drizzle___insert__select__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_INSERT_SELECT_START_ENABLED() \
	__dtraceenabled_drizzle___insert__select__start()
#else
#define	DRIZZLE_INSERT_SELECT_START_ENABLED() \
	__dtraceenabled_drizzle___insert__select__start(0)
#endif
#define	DRIZZLE_INSERT_START(arg0) \
	__dtrace_drizzle___insert__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_INSERT_START_ENABLED() \
	__dtraceenabled_drizzle___insert__start()
#else
#define	DRIZZLE_INSERT_START_ENABLED() \
	__dtraceenabled_drizzle___insert__start(0)
#endif
#define	DRIZZLE_QUERY_DONE(arg0) \
	__dtrace_drizzle___query__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_QUERY_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__done()
#else
#define	DRIZZLE_QUERY_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__done(0)
#endif
#define	DRIZZLE_QUERY_EXEC_DONE(arg0) \
	__dtrace_drizzle___query__exec__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_QUERY_EXEC_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__exec__done()
#else
#define	DRIZZLE_QUERY_EXEC_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__exec__done(0)
#endif
#define	DRIZZLE_QUERY_EXEC_START(arg0, arg1, arg2, arg3) \
	__dtrace_drizzle___query__exec__start(arg0, arg1, arg2, arg3)
#ifndef	__sparc
#define	DRIZZLE_QUERY_EXEC_START_ENABLED() \
	__dtraceenabled_drizzle___query__exec__start()
#else
#define	DRIZZLE_QUERY_EXEC_START_ENABLED() \
	__dtraceenabled_drizzle___query__exec__start(0)
#endif
#define	DRIZZLE_QUERY_PARSE_DONE(arg0) \
	__dtrace_drizzle___query__parse__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_QUERY_PARSE_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__parse__done()
#else
#define	DRIZZLE_QUERY_PARSE_DONE_ENABLED() \
	__dtraceenabled_drizzle___query__parse__done(0)
#endif
#define	DRIZZLE_QUERY_PARSE_START(arg0) \
	__dtrace_drizzle___query__parse__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_QUERY_PARSE_START_ENABLED() \
	__dtraceenabled_drizzle___query__parse__start()
#else
#define	DRIZZLE_QUERY_PARSE_START_ENABLED() \
	__dtraceenabled_drizzle___query__parse__start(0)
#endif
#define	DRIZZLE_QUERY_START(arg0, arg1, arg2) \
	__dtrace_drizzle___query__start(arg0, arg1, arg2)
#ifndef	__sparc
#define	DRIZZLE_QUERY_START_ENABLED() \
	__dtraceenabled_drizzle___query__start()
#else
#define	DRIZZLE_QUERY_START_ENABLED() \
	__dtraceenabled_drizzle___query__start(0)
#endif
#define	DRIZZLE_SELECT_DONE(arg0, arg1) \
	__dtrace_drizzle___select__done(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_SELECT_DONE_ENABLED() \
	__dtraceenabled_drizzle___select__done()
#else
#define	DRIZZLE_SELECT_DONE_ENABLED() \
	__dtraceenabled_drizzle___select__done(0)
#endif
#define	DRIZZLE_SELECT_START(arg0) \
	__dtrace_drizzle___select__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_SELECT_START_ENABLED() \
	__dtraceenabled_drizzle___select__start()
#else
#define	DRIZZLE_SELECT_START_ENABLED() \
	__dtraceenabled_drizzle___select__start(0)
#endif
#define	DRIZZLE_UPDATE_DONE(arg0, arg1, arg2) \
	__dtrace_drizzle___update__done(arg0, arg1, arg2)
#ifndef	__sparc
#define	DRIZZLE_UPDATE_DONE_ENABLED() \
	__dtraceenabled_drizzle___update__done()
#else
#define	DRIZZLE_UPDATE_DONE_ENABLED() \
	__dtraceenabled_drizzle___update__done(0)
#endif
#define	DRIZZLE_UPDATE_ROW_DONE(arg0) \
	__dtrace_drizzle___update__row__done(arg0)
#ifndef	__sparc
#define	DRIZZLE_UPDATE_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___update__row__done()
#else
#define	DRIZZLE_UPDATE_ROW_DONE_ENABLED() \
	__dtraceenabled_drizzle___update__row__done(0)
#endif
#define	DRIZZLE_UPDATE_ROW_START(arg0, arg1) \
	__dtrace_drizzle___update__row__start(arg0, arg1)
#ifndef	__sparc
#define	DRIZZLE_UPDATE_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___update__row__start()
#else
#define	DRIZZLE_UPDATE_ROW_START_ENABLED() \
	__dtraceenabled_drizzle___update__row__start(0)
#endif
#define	DRIZZLE_UPDATE_START(arg0) \
	__dtrace_drizzle___update__start(arg0)
#ifndef	__sparc
#define	DRIZZLE_UPDATE_START_ENABLED() \
	__dtraceenabled_drizzle___update__start()
#else
#define	DRIZZLE_UPDATE_START_ENABLED() \
	__dtraceenabled_drizzle___update__start(0)
#endif


extern void __dtrace_drizzle___command__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___command__done(void);
#else
extern int __dtraceenabled_drizzle___command__done(long);
#endif
extern void __dtrace_drizzle___command__start(unsigned long, int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___command__start(void);
#else
extern int __dtraceenabled_drizzle___command__start(long);
#endif
extern void __dtrace_drizzle___connection__done(unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___connection__done(void);
#else
extern int __dtraceenabled_drizzle___connection__done(long);
#endif
extern void __dtrace_drizzle___connection__start(unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___connection__start(void);
#else
extern int __dtraceenabled_drizzle___connection__start(long);
#endif
extern void __dtrace_drizzle___delete__done(int, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___delete__done(void);
#else
extern int __dtraceenabled_drizzle___delete__done(long);
#endif
extern void __dtrace_drizzle___delete__row__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___delete__row__done(void);
#else
extern int __dtraceenabled_drizzle___delete__row__done(long);
#endif
extern void __dtrace_drizzle___delete__row__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___delete__row__start(void);
#else
extern int __dtraceenabled_drizzle___delete__row__start(long);
#endif
extern void __dtrace_drizzle___delete__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___delete__start(void);
#else
extern int __dtraceenabled_drizzle___delete__start(long);
#endif
extern void __dtrace_drizzle___filesort__done(int, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___filesort__done(void);
#else
extern int __dtraceenabled_drizzle___filesort__done(long);
#endif
extern void __dtrace_drizzle___filesort__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___filesort__start(void);
#else
extern int __dtraceenabled_drizzle___filesort__start(long);
#endif
extern void __dtrace_drizzle___handler__rdlock__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__rdlock__done(void);
#else
extern int __dtraceenabled_drizzle___handler__rdlock__done(long);
#endif
extern void __dtrace_drizzle___handler__rdlock__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__rdlock__start(void);
#else
extern int __dtraceenabled_drizzle___handler__rdlock__start(long);
#endif
extern void __dtrace_drizzle___handler__unlock__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__unlock__done(void);
#else
extern int __dtraceenabled_drizzle___handler__unlock__done(long);
#endif
extern void __dtrace_drizzle___handler__unlock__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__unlock__start(void);
#else
extern int __dtraceenabled_drizzle___handler__unlock__start(long);
#endif
extern void __dtrace_drizzle___handler__wrlock__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__wrlock__done(void);
#else
extern int __dtraceenabled_drizzle___handler__wrlock__done(long);
#endif
extern void __dtrace_drizzle___handler__wrlock__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___handler__wrlock__start(void);
#else
extern int __dtraceenabled_drizzle___handler__wrlock__start(long);
#endif
extern void __dtrace_drizzle___insert__done(int, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__done(void);
#else
extern int __dtraceenabled_drizzle___insert__done(long);
#endif
extern void __dtrace_drizzle___insert__row__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__row__done(void);
#else
extern int __dtraceenabled_drizzle___insert__row__done(long);
#endif
extern void __dtrace_drizzle___insert__row__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__row__start(void);
#else
extern int __dtraceenabled_drizzle___insert__row__start(long);
#endif
extern void __dtrace_drizzle___insert__select__done(int, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__select__done(void);
#else
extern int __dtraceenabled_drizzle___insert__select__done(long);
#endif
extern void __dtrace_drizzle___insert__select__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__select__start(void);
#else
extern int __dtraceenabled_drizzle___insert__select__start(long);
#endif
extern void __dtrace_drizzle___insert__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___insert__start(void);
#else
extern int __dtraceenabled_drizzle___insert__start(long);
#endif
extern void __dtrace_drizzle___query__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__done(void);
#else
extern int __dtraceenabled_drizzle___query__done(long);
#endif
extern void __dtrace_drizzle___query__exec__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__exec__done(void);
#else
extern int __dtraceenabled_drizzle___query__exec__done(long);
#endif
extern void __dtrace_drizzle___query__exec__start(char *, unsigned long, char *, int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__exec__start(void);
#else
extern int __dtraceenabled_drizzle___query__exec__start(long);
#endif
extern void __dtrace_drizzle___query__parse__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__parse__done(void);
#else
extern int __dtraceenabled_drizzle___query__parse__done(long);
#endif
extern void __dtrace_drizzle___query__parse__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__parse__start(void);
#else
extern int __dtraceenabled_drizzle___query__parse__start(long);
#endif
extern void __dtrace_drizzle___query__start(char *, unsigned long, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___query__start(void);
#else
extern int __dtraceenabled_drizzle___query__start(long);
#endif
extern void __dtrace_drizzle___select__done(int, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___select__done(void);
#else
extern int __dtraceenabled_drizzle___select__done(long);
#endif
extern void __dtrace_drizzle___select__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___select__start(void);
#else
extern int __dtraceenabled_drizzle___select__start(long);
#endif
extern void __dtrace_drizzle___update__done(int, unsigned long, unsigned long);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___update__done(void);
#else
extern int __dtraceenabled_drizzle___update__done(long);
#endif
extern void __dtrace_drizzle___update__row__done(int);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___update__row__done(void);
#else
extern int __dtraceenabled_drizzle___update__row__done(long);
#endif
extern void __dtrace_drizzle___update__row__start(char *, char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___update__row__start(void);
#else
extern int __dtraceenabled_drizzle___update__row__start(long);
#endif
extern void __dtrace_drizzle___update__start(char *);
#ifndef	__sparc
extern int __dtraceenabled_drizzle___update__start(void);
#else
extern int __dtraceenabled_drizzle___update__start(long);
#endif

#else

#define	DRIZZLE_COMMAND_DONE(arg0)
#define	DRIZZLE_COMMAND_DONE_ENABLED() (0)
#define	DRIZZLE_COMMAND_START(arg0, arg1)
#define	DRIZZLE_COMMAND_START_ENABLED() (0)
#define	DRIZZLE_CONNECTION_DONE(arg0)
#define	DRIZZLE_CONNECTION_DONE_ENABLED() (0)
#define	DRIZZLE_CONNECTION_START(arg0)
#define	DRIZZLE_CONNECTION_START_ENABLED() (0)
#define	DRIZZLE_DELETE_DONE(arg0, arg1)
#define	DRIZZLE_DELETE_DONE_ENABLED() (0)
#define	DRIZZLE_DELETE_ROW_DONE(arg0)
#define	DRIZZLE_DELETE_ROW_DONE_ENABLED() (0)
#define	DRIZZLE_DELETE_ROW_START(arg0, arg1)
#define	DRIZZLE_DELETE_ROW_START_ENABLED() (0)
#define	DRIZZLE_DELETE_START(arg0)
#define	DRIZZLE_DELETE_START_ENABLED() (0)
#define	DRIZZLE_FILESORT_DONE(arg0, arg1)
#define	DRIZZLE_FILESORT_DONE_ENABLED() (0)
#define	DRIZZLE_FILESORT_START(arg0, arg1)
#define	DRIZZLE_FILESORT_START_ENABLED() (0)
#define	DRIZZLE_HANDLER_RDLOCK_DONE(arg0)
#define	DRIZZLE_HANDLER_RDLOCK_DONE_ENABLED() (0)
#define	DRIZZLE_HANDLER_RDLOCK_START(arg0, arg1)
#define	DRIZZLE_HANDLER_RDLOCK_START_ENABLED() (0)
#define	DRIZZLE_HANDLER_UNLOCK_DONE(arg0)
#define	DRIZZLE_HANDLER_UNLOCK_DONE_ENABLED() (0)
#define	DRIZZLE_HANDLER_UNLOCK_START(arg0, arg1)
#define	DRIZZLE_HANDLER_UNLOCK_START_ENABLED() (0)
#define	DRIZZLE_HANDLER_WRLOCK_DONE(arg0)
#define	DRIZZLE_HANDLER_WRLOCK_DONE_ENABLED() (0)
#define	DRIZZLE_HANDLER_WRLOCK_START(arg0, arg1)
#define	DRIZZLE_HANDLER_WRLOCK_START_ENABLED() (0)
#define	DRIZZLE_INSERT_DONE(arg0, arg1)
#define	DRIZZLE_INSERT_DONE_ENABLED() (0)
#define	DRIZZLE_INSERT_ROW_DONE(arg0)
#define	DRIZZLE_INSERT_ROW_DONE_ENABLED() (0)
#define	DRIZZLE_INSERT_ROW_START(arg0, arg1)
#define	DRIZZLE_INSERT_ROW_START_ENABLED() (0)
#define	DRIZZLE_INSERT_SELECT_DONE(arg0, arg1)
#define	DRIZZLE_INSERT_SELECT_DONE_ENABLED() (0)
#define	DRIZZLE_INSERT_SELECT_START(arg0)
#define	DRIZZLE_INSERT_SELECT_START_ENABLED() (0)
#define	DRIZZLE_INSERT_START(arg0)
#define	DRIZZLE_INSERT_START_ENABLED() (0)
#define	DRIZZLE_QUERY_DONE(arg0)
#define	DRIZZLE_QUERY_DONE_ENABLED() (0)
#define	DRIZZLE_QUERY_EXEC_DONE(arg0)
#define	DRIZZLE_QUERY_EXEC_DONE_ENABLED() (0)
#define	DRIZZLE_QUERY_EXEC_START(arg0, arg1, arg2, arg3)
#define	DRIZZLE_QUERY_EXEC_START_ENABLED() (0)
#define	DRIZZLE_QUERY_PARSE_DONE(arg0)
#define	DRIZZLE_QUERY_PARSE_DONE_ENABLED() (0)
#define	DRIZZLE_QUERY_PARSE_START(arg0)
#define	DRIZZLE_QUERY_PARSE_START_ENABLED() (0)
#define	DRIZZLE_QUERY_START(arg0, arg1, arg2)
#define	DRIZZLE_QUERY_START_ENABLED() (0)
#define	DRIZZLE_SELECT_DONE(arg0, arg1)
#define	DRIZZLE_SELECT_DONE_ENABLED() (0)
#define	DRIZZLE_SELECT_START(arg0)
#define	DRIZZLE_SELECT_START_ENABLED() (0)
#define	DRIZZLE_UPDATE_DONE(arg0, arg1, arg2)
#define	DRIZZLE_UPDATE_DONE_ENABLED() (0)
#define	DRIZZLE_UPDATE_ROW_DONE(arg0)
#define	DRIZZLE_UPDATE_ROW_DONE_ENABLED() (0)
#define	DRIZZLE_UPDATE_ROW_START(arg0, arg1)
#define	DRIZZLE_UPDATE_ROW_START_ENABLED() (0)
#define	DRIZZLE_UPDATE_START(arg0)
#define	DRIZZLE_UPDATE_START_ENABLED() (0)

#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _PROBES_H */
