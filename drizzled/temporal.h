/* - mode: c; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 *  Authors:
 *
 *  Jay Pipes <jay.pipes@sun.com>
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

/**
 * @file 
 *
 * Defines the API for dealing with temporal data inside the server.
 *
 * The Temporal class is the base class for all data of any temporal
 * type.  A number of derived classes define specialized classes 
 * representng various date, date-time, time, or timestamp types.
 *
 * All Temporal derived classes are ValueObjects.  That is to say that
 * Temporal class instances are not part of the Item hierarchy and serve
 * <em>only</em> to represent a time or date-related piece of data.
 *
 * @note
 *
 * Low-level calendrical calculations are done via routines in the
 * calendar.cc file.
 *
 * @see drizzled/calendar.cc
 */

#ifndef DRIZZLED_TEMPORAL_H
#define DRIZZLED_TEMPORAL_H

#define DRIZZLE_MAX_SECONDS 59
#define DRIZZLE_MAX_MINUTES 59
#define DRIZZLE_MAX_HOURS 23
#define DRIZZLE_MAX_DAYS 31
#define DRIZZLE_MAX_MONTHS 12
#define DRIZZLE_MAX_YEARS_SQL 9999
#define DRIZZLE_MAX_YEARS_EPOCH 2038
#define DRIZZLE_MIN_SECONDS 0
#define DRIZZLE_MIN_MINUTES 0
#define DRIZZLE_MIN_HOURS 0
#define DRIZZLE_MIN_DAYS 1
#define DRIZZLE_MIN_MONTHS 1
#define DRIZZLE_MIN_YEARS_SQL 1
#define DRIZZLE_MIN_YEARS_EPOCH 1970

#define DRIZZLE_SECONDS_IN_MINUTE 60
#define DRIZZLE_SECONDS_IN_HOUR (60*60)
#define DRIZZLE_SECONDS_IN_DAY (60*60*24)
#define DRIZZLE_NANOSECONDS_IN_MICROSECOND 1000

#define DRIZZLE_YY_PART_YEAR  70

#include <sys/time.h>
#include <time.h>

#include "drizzled/global.h"
#include "drizzled/calendar.h"

/* Outside forward declarations */
class my_decimal;

namespace drizzled 
{

/**
 * Base class for all temporal data classes.
 */
class Temporal
{
protected:
  enum calendar _calendar;
  uint32_t _years;
  uint32_t _months;
  uint32_t _days;
  uint32_t _hours;
  uint32_t _minutes;
  uint32_t _seconds;
  time_t _epoch_seconds;
  uint32_t _useconds;
  uint32_t _nseconds;
  /** Returns number of seconds in time components (hour + minute + second) */
  uint64_t _cumulative_seconds_in_time() const;
public:
  Temporal();
  virtual ~Temporal() {}

  /** Returns the calendar component. */
  inline enum calendar calendar() const {return _calendar;}
  /** Returns the nanoseconds component. */
  inline uint32_t nseconds() const {return _nseconds;}
  /** Sets the useconds component. */
  inline void set_useconds(const uint32_t usecond) {_useconds= usecond;}
  /** Returns the microsseconds component. */
  inline uint32_t useconds() const {return _useconds;}
  /** Sets the epoch_seconds component. */
  virtual void set_epoch_seconds();
  /** Returns the UNIX epoch seconds component. */
  inline time_t epoch_seconds() const {return _epoch_seconds;}
  /** Sets the seconds component. */
  inline void set_seconds(const uint32_t second) {_seconds= second;}
  /** Returns the seconds component. */
  inline uint32_t seconds() const {return _seconds;}
  /** Sets the days component. */
  inline void set_minutes(const uint32_t minute) {_minutes= minute;}
  /** Returns the minutes component. */
  inline uint32_t minutes() const {return _minutes;}
  /** Sets the hours component. */
  inline void set_hours(const uint32_t hour) {_hours= hour;}
  /** Returns the hours component. */
  inline uint32_t hours() const {return _hours;}
  /** Sets the days component. */
  inline void set_days(const uint32_t day) {_days= day;}
  /** Returns the days component. */
  inline uint32_t days() const {return _days;}
  /** Sets the months component. */
  inline void set_months(const uint32_t month) {_months= month;}
  /** Returns the months component. */
  inline uint32_t months() const {return _months;}
  /** Sets the years component. */
  inline void set_years(const uint32_t year) {_years= year;}
  /** Returns the years component. */
  inline uint32_t years() const {return _years;}

  /** Returns whether the temporal value is valid as a date. */
  virtual bool is_valid_date() const= 0;
  /** Returns whether the temporal value is valid as a datetime. */
  virtual bool is_valid_datetime() const= 0;
  /** Returns whether the temporal value is valid as a time. */
  virtual bool is_valid_time() const= 0;
  /** Returns whether the temporal value is valid as a UNIX timestamp. */
  virtual bool is_valid_timestamp() const= 0;

  /**
   * Returns whether the temporal
   * value is valid. Each subclass defines what is
   * valid for the range of temporal data it contains.
   */
  virtual bool is_valid() const= 0;

  /**
   * All Temporal derived classes must implement
   * conversion routines for converting to and from
   * a string. Subclasses implement other conversion 
   * routines, but should always follow these notes:
   *
   * 1) Ensure that ALL from_xxx methods call is_valid()
   * 2) Ensure that ALL to_xxx methods are void returns and
   *    do not call is_valid()
   *
   * This minimizes the repeated bounds-checking to
   * just the conversion from_xxx routines.
   */

  friend class TemporalFormat;
};

/**
 * Class representing temporal components in a valid
 * SQL date range, with no time component
 */
class Date: public Temporal 
{
public:
  /**
   * Comparison operator overloads to compare a Date against
   * another Date value.
   *
   * @param Date to compare against.
   */
  bool operator==(const Date &rhs);
  bool operator!=(const Date &rhs);
  bool operator>(const Date &rhs);
  bool operator>=(const Date &rhs);
  bool operator<(const Date &rhs);
  bool operator<=(const Date &rhs);
  /**
   * Operator overload for adding/subtracting another Date
   * (or subclass) to/from this temporal.  When subtracting 
   * or adding two Dates, we return a new Date instance.
   *
   * @param Temporal instance to add/subtract to/from
   */
  const Date operator-(const Date &rhs);
  const Date operator+(const Date &rhs);
  Date& operator+=(const Date &rhs);
  Date& operator-=(const Date &rhs);

  virtual bool is_valid_date() const {return is_valid();}
  virtual bool is_valid_datetime() const {return is_valid();}
  virtual bool is_valid_time() const {return false;}
  virtual bool is_valid_timestamp() const {return is_valid() && in_unix_epoch();}
  /** Returns whether the temporal value is valid date. */
  virtual bool is_valid() const;
  /* Returns whether the Date (or subclass) instance is in the Unix Epoch. */
  virtual bool in_unix_epoch() const;

  /**
   * Fills a supplied char string with a
   * string representation of the Date  
   * value.
   *
   * @param C-String to fill.
   * @param Length of filled string (out param)
   */
  virtual void to_string(char *to, size_t *to_len) const;

  /**
   * Attempts to populate the Date instance based
   * on the contents of a supplied string.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param String to convert from
   * @param Length of supplied string
   */
  virtual bool from_string(const char *from, size_t from_len);

  /**
   * Fills a supplied 8-byte integer pointer with an
   * integer representation of the Date
   * value.
   *
   * @param Integer to fill.
   */
  virtual void to_int64_t(int64_t *to) const;

  /**
   * Fills a supplied 4-byte integer pointer with an
   * integer representation of the Date
   * value.
   *
   * @param Integer to fill.
   */
  virtual void to_int32_t(int32_t *to) const;

  /**
   * Attempts to populate the Date instance based
   * on the contents of a supplied 4-byte integer.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param Integer to convert from
   */
  virtual bool from_int32_t(const int32_t from);

  /**
   * Attempts to populate the Date instance based
   * on the contents of a supplied Julian Day Number
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param Integer to convert from
   */
  virtual bool from_julian_day_number(const int64_t from);

  /**
   * Fills a supplied tm pointer with an
   * representation of the Date 
   * value.
   *
   * @param tm to fill.
   */
  virtual void to_tm(struct tm *to) const;

  /**
   * Attempts to populate the Date instance based
   * on the contents of a supplied pointer to struct tm
   * (broken time).
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param Pointe rto the struct tm to convert from
   */
  virtual bool from_tm(const struct tm *from);

  /**
   * Attempts to convert the Date value into
   * a supplied time_t.
   *
   * @param Pointer to a time_t to convert to
   */
  virtual void to_time_t(time_t *to) const;

  /**
   * Attempts to populate the Date instance based
   * on the contents of a supplied time_t
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param time_t to convert from
   */
  virtual bool from_time_t(const time_t from);

  /**
   * Fills a supplied my_decimal with a representation of 
   * the Date value.
   *
   * @param Pointer to the my_decimal to fill
   */
  virtual void to_decimal(my_decimal *to) const;
};

/* Forward declare needed for friendship */
class DateTime;

/**
 * Class representing temporal components having only
 * a time component, with no date structure
 */
class Time: public Temporal
{
public:

  /**
   * Comparison operator overloads to compare a Time against
   * another Time value.
   *
   * @param Time to compare against.
   */
  bool operator==(const Time &rhs);
  bool operator!=(const Time &rhs);
  bool operator>(const Time &rhs);
  bool operator>=(const Time &rhs);
  bool operator<(const Time &rhs);
  bool operator<=(const Time &rhs);
  /**
   * Operator to add/subtract a Time from a Time.  
   * We can return a Time new temporal instance.
   *
   * @param Temporal instance to add/subtract to/from
   */
  const Time operator-(const Time &rhs);
  const Time operator+(const Time &rhs);
  Time& operator-=(const Time &rhs);
  Time& operator+=(const Time &rhs);

  virtual bool is_valid_date() const {return false;}
  virtual bool is_valid_datetime() const {return false;}
  virtual bool is_valid_time() const {return is_valid();}
  virtual bool is_valid_timestamp() const {return false;}
  /** Returns whether the temporal value is valid date. */
  virtual bool is_valid() const;

  /**
   * Fills a supplied char string with a
   * string representation of the Time  
   * value.
   *
   * @param C-String to fill.
   * @param Length of filled string (out param)
   */
  virtual void to_string(char *to, size_t *to_len) const;

  /**
   * Attempts to populate the Time instance based
   * on the contents of a supplied string.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param String to convert from
   * @param Length of supplied string
   */
  virtual bool from_string(const char *from, size_t from_len);

  /**
   * Fills a supplied 4-byte integer pointer with an
   * integer representation of the Time
   * value.
   *
   * @param Integer to fill.
   */
  virtual void to_int32_t(int32_t *to) const;

  /**
   * Attempts to populate the Time instance based
   * on the contents of a supplied 4-byte integer.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param Integer to convert from
   */
  virtual bool from_int32_t(const int32_t from);

  /**
   * Fills a supplied my_decimal with a representation of 
   * the Time value.
   *
   * @param Pointer to the my_decimal to fill
   */
  virtual void to_decimal(my_decimal *to) const;

  friend class DateTime;
};

/**
 * Class representing temporal components in a valid
 * SQL datetime range, including a time component
 */
class DateTime: public Date
{
public:
  /**
   * Comparison operator overloads to compare a DateTime against
   * another DateTime value.
   *
   * @param DateTime to compare against.
   */
  bool operator==(const DateTime &rhs);
  bool operator!=(const DateTime &rhs);
  bool operator>(const DateTime &rhs);
  bool operator>=(const DateTime &rhs);
  bool operator<(const DateTime &rhs);
  bool operator<=(const DateTime &rhs);
  /**
   * Operator to add/subtract a Time from a Time.  
   * We can return a Time new temporal instance.
   *
   * @param Temporal instance to add/subtract to/from
   */
  const DateTime operator-(const Time &rhs);
  const DateTime operator+(const Time &rhs);
  DateTime& operator-=(const Time &rhs);
  DateTime& operator+=(const Time &rhs);
  /**
   * Operator overload for adding/subtracting another DateTime
   * (or subclass) to/from this temporal.  When subtracting 
   * or adding two DateTimes, we return a new DateTime instance.
   *
   * @param Temporal instance to add/subtract to/from
   */
  const DateTime operator-(const DateTime &rhs);
  const DateTime operator+(const DateTime &rhs);
  DateTime& operator+=(const DateTime &rhs);
  DateTime& operator-=(const DateTime &rhs);

  /* Returns whether the DateTime (or subclass) instance is in the Unix Epoch. */
  bool in_unix_epoch() const;
  /** Returns whether the temporal value is valid datetime. */
  virtual bool is_valid() const;

  /**
   * It's not possible to convert to and from a DateTime and 
   * a 4-byte integer, so let us know if we try and do it!
   */
  inline void to_int32_t(int32_t *) {assert(0);}
  inline bool from_int32_t(int32_t) {assert(0); return false;}

  /**
   * Fills a supplied char string with a
   * string representation of the DateTime
   * value.
   *
   * @param C-String to fill.
   * @param Length of filled string (out param)
   */
  virtual void to_string(char *to, size_t *to_len) const;

  /**
   * Attempts to populate the DateTime instance based
   * on the contents of a supplied string.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param String to convert from
   * @param Length of supplied string
   */
  virtual bool from_string(const char *from, size_t from_len);

  /**
   * Fills a supplied 8-byte integer pointer with an
   * integer representation of the DateTime
   * value.
   *
   * @param Integer to fill.
   */
  virtual void to_int64_t(int64_t *to) const;

  /**
   * Attempts to populate the DateTime instance based
   * on the contents of a supplied time_t
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param time_t to convert from
   */
  virtual bool from_time_t(const time_t from);

  /**
   * Attempts to populate the DateTime instance based
   * on the contents of a supplied 8-byte integer.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param Integer to convert from
   */
  virtual bool from_int64_t(const int64_t from);

  /**
   * Fills a supplied tm pointer with an
   * representation of the DateTime
   * value.
   *
   * @param tm to fill.
   */
  virtual void to_tm(struct tm *to) const;

  /**
   * Fills a supplied my_decimal with a representation of 
   * the DateTime value.
   *
   * @param Pointer to the my_decimal to fill
   */
  virtual void to_decimal(my_decimal *to) const;
};

/**
 * Class representing temporal components in the UNIX epoch
 */
class Timestamp: public DateTime 
{
public:
  /**
   * Comparison operator overloads to compare a Timestamp against
   * another Timestamp value.
   *
   * @param Timestamp to compare against.
   */
  bool operator==(const Timestamp &rhs);
  bool operator!=(const Timestamp &rhs);
  bool operator>(const Timestamp &rhs);
  bool operator>=(const Timestamp &rhs);
  bool operator<(const Timestamp &rhs);
  bool operator<=(const Timestamp &rhs);

  inline bool is_valid_timestamp() const {return is_valid();}
  /** Returns whether the temporal value is valid timestamp. */
  virtual bool is_valid() const;

  /**
   * Attempts to convert the Timestamp value into
   * a supplied time_t.
   *
   * @param Pointer to a time_t to convert to
   */
  virtual void to_time_t(time_t *to) const;
};

/**
 * Class representing temporal components in the UNIX epoch
 * with an additional microsecond component.
 */
class MicroTimestamp: public Timestamp
{
public:
  /** Returns whether the temporal value is valid micro-timestamp. */
  bool is_valid() const;

  /**
   * Fills a supplied char string with a
   * string representation of the MicroTimestamp
   * value.
   *
   * @param C-String to fill.
   * @param Length of filled string (out param)
   */
  virtual void to_string(char *to, size_t *to_len) const;

  /**
   * Fills a supplied timeval pointer with an
   * representation of the MicroTimestamp
   * value.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param timeval to fill.
   */
  virtual void to_timeval(struct timeval *to) const;
};

/**
 * Class representing temporal components in the UNIX epoch
 * with an additional nanosecond component.
 */
class NanoTimestamp: public Timestamp
{
public:
  /** Returns whether the temporal value is valid nano-timestamp. */
  bool is_valid() const;

  /**
   * Fills a supplied timespec pointer with an
   * representation of the NanoTimestamp
   * value.
   *
   * Returns whether the conversion was 
   * successful.
   *
   * @param timespec to fill.
   */
  void to_timespec(struct timespec *to) const;
};

} /* end namespace drizzled */

#endif /* DRIZZLED_TEMPORAL_H */