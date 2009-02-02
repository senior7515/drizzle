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

#ifndef DRIZZLED_XID_H
#define DRIZZLED_XID_H

extern uint32_t server_id;

/**
  class XID _may_ be binary compatible with the XID structure as
  in the X/Open CAE Specification, Distributed Transaction Processing:
  The XA Specification, X/Open Company Ltd., 1991.
  http://www.opengroup.org/bookstore/catalog/c193.htm

*/

typedef uint64_t my_xid; // this line is the same as in log_event.h

#define DRIZZLE_XIDDATASIZE 128
#define DRIZZLE_XID_PREFIX "MySQLXid"
#define DRIZZLE_XID_PREFIX_LEN 8 // must be a multiple of 8
#define DRIZZLE_XID_OFFSET (DRIZZLE_XID_PREFIX_LEN+sizeof(server_id))
#define DRIZZLE_XID_GTRID_LEN (DRIZZLE_XID_OFFSET+sizeof(my_xid))

#define XIDDATASIZE DRIZZLE_XIDDATASIZE

class XID {

public:

  long formatID;
  long gtrid_length;
  long bqual_length;
  char data[XIDDATASIZE];  // not \0-terminated !

  XID();
  bool eq(XID *xid);
  bool eq(long g, long b, const char *d);
  void set(XID *xid);
  void set(long f, const char *g, long gl, const char *b, long bl);
  void set(uint64_t xid);
  void set(long g, long b, const char *d);
  bool is_null();
  void null();
  my_xid quick_get_my_xid();
  my_xid get_my_xid();
  uint32_t length();
  unsigned char *key();
  uint32_t key_length();
};

/**
  struct st_drizzle_xid is binary compatible with the XID structure as
  in the X/Open CAE Specification, Distributed Transaction Processing:
  The XA Specification, X/Open Company Ltd., 1991.
  http://www.opengroup.org/bookstore/catalog/c193.htm

*/
struct st_drizzle_xid {
  long formatID;
  long gtrid_length;
  long bqual_length;
  char data[DRIZZLE_XIDDATASIZE];  /* Not \0-terminated */
};
typedef struct st_drizzle_xid DRIZZLE_XID;


/* for recover() handlerton call */
#define MIN_XID_LIST_SIZE  128
#define MAX_XID_LIST_SIZE  (1024*128)

#endif /* DRIZZLED_XID_H */
