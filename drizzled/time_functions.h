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

#ifndef DRIZZLED_TIME_FUNCTIONS_H
#define DRIZZLED_TIME_FUNCTIONS_H

#include "drizzled/sql_error.h"
#include "mysys/drizzle_time.h"

typedef struct st_drizzle_time DRIZZLE_TIME;

/* Calc weekday from daynr */
/* Returns 0 for monday, 1 for tuesday .... */
int calc_weekday(long daynr, bool sunday_first_day_of_week);

/*
  The bits in week_format has the following meaning:
   WEEK_MONDAY_FIRST (0)  If not set	Sunday is first day of week
      		   	  If set	Monday is first day of week
   WEEK_YEAR (1)	  If not set	Week is in range 0-53

   	Week 0 is returned for the the last week of the previous year (for
	a date at start of january) In this case one can get 53 for the
	first week of next year.  This flag ensures that the week is
	relevant for the given year. Note that this flag is only
	releveant if WEEK_JANUARY is not set.

			  If set	 Week is in range 1-53.

	In this case one may get week 53 for a date in January (when
	the week is that last week of previous year) and week 1 for a
	date in December.

  WEEK_FIRST_WEEKDAY (2)  If not set	Weeks are numbered according
			   		to ISO 8601:1988
			  If set	The week that contains the first
					'first-day-of-week' is week 1.

	ISO 8601:1988 means that if the week containing January 1 has
	four or more days in the new year, then it is week 1;
	Otherwise it is the last week of the previous year, and the
	next week is week 1.
*/
uint32_t calc_week(DRIZZLE_TIME *l_time, uint32_t week_behaviour, uint32_t *year);

/* Change a daynr to year, month and day */
/* Daynr 0 is returned as date 00.00.00 */
void get_date_from_daynr(long daynr,
                         uint32_t *year, 
                         uint32_t *month,
                         uint32_t *day);

/*
  Convert a timestamp string to a DRIZZLE_TIME value and produce a warning
  if string was truncated during conversion.

  NOTE
    See description of str_to_datetime() for more information.
*/
enum enum_drizzle_timestamp_type str_to_datetime_with_warn(const char *str, 
                                                           uint32_t length,
                                                           DRIZZLE_TIME *l_time, 
                                                           uint32_t flags);

/*
  Convert a time string to a DRIZZLE_TIME struct and produce a warning
  if string was cut during conversion.

  NOTE
    See str_to_time() for more info.
*/
bool str_to_time_with_warn(const char *str, uint32_t length, DRIZZLE_TIME *l_time);

/*
  Convert a system time structure to TIME
*/
void localtime_to_TIME(DRIZZLE_TIME *to, struct tm *from);

void make_date(const DRIZZLE_TIME *l_time, String *str);

void make_datetime(const DRIZZLE_TIME *l_time, String *str);

void make_truncated_value_warning(Session *session, 
                                  DRIZZLE_ERROR::enum_warning_level level,
                                  const char *str_val,
                                  uint32_t str_length, 
                                  enum enum_drizzle_timestamp_type time_type,
                                  const char *field_name);

/*
  Calculate difference between two datetime values as seconds + microseconds.

  SYNOPSIS
    calc_time_diff()
      l_time1         - TIME/DATE/DATETIME value
      l_time2         - TIME/DATE/DATETIME value
      l_sign          - 1 absolute values are substracted,
                        -1 absolute values are added.
      seconds_out     - Out parameter where difference between
                        l_time1 and l_time2 in seconds is stored.
      microseconds_out- Out parameter where microsecond part of difference
                        between l_time1 and l_time2 is stored.

  NOTE
    This function calculates difference between l_time1 and l_time2 absolute
    values. So one should set l_sign and correct result if he want to take
    signs into account (i.e. for DRIZZLE_TIME values).

  RETURN VALUES
    Returns sign of difference.
    1 means negative result
    0 means positive result

*/
bool calc_time_diff(DRIZZLE_TIME *l_time1, 
                    DRIZZLE_TIME *l_time2, 
                    int l_sign,
                    int64_t *seconds_out, 
                    long *microseconds_out);

#endif /* DRIZZLED_TIME_FUNCTIONS_H */