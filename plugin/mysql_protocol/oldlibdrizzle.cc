/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2008 Sun Microsystems
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <drizzled/server_includes.h>
#include <drizzled/gettext.h>
#include <drizzled/error.h>
#include <drizzled/query_id.h>
#include <drizzled/sql_state.h>
#include <drizzled/session.h>
#include <algorithm>

#include "pack.h"
#include "errmsg.h"
#include "oldlibdrizzle.h"
#include "options.h"

using namespace std;
using namespace drizzled;

#define PROTOCOL_VERSION 10

static const unsigned int PACKET_BUFFER_EXTRA_ALLOC= 1024;
static uint32_t port;
static uint32_t connect_timeout;
static uint32_t read_timeout;
static uint32_t write_timeout;
static uint32_t retry_count;
static uint32_t buffer_length;
static char* bind_address;

const char* ListenMySQLProtocol::getHost(void) const
{
  return bind_address;
}

in_port_t ListenMySQLProtocol::getPort(void) const
{
  return (in_port_t) port;
}

plugin::Client *ListenMySQLProtocol::getClient(int fd)
{
  int new_fd;
  new_fd= acceptTcp(fd);
  if (new_fd == -1)
    return NULL;

  return new (nothrow) ClientMySQLProtocol(new_fd, using_mysql41_protocol);
}

ClientMySQLProtocol::ClientMySQLProtocol(int fd, bool using_mysql41_protocol_arg):
  using_mysql41_protocol(using_mysql41_protocol_arg)
{
  net.vio= 0;

  if (fd == -1)
    return;

  if (drizzleclient_net_init_sock(&net, fd, 0, buffer_length))
    throw bad_alloc();

  drizzleclient_net_set_read_timeout(&net, read_timeout);
  drizzleclient_net_set_write_timeout(&net, write_timeout);
  net.retry_count=retry_count;
}

ClientMySQLProtocol::~ClientMySQLProtocol()
{
  if (net.vio)
    drizzleclient_vio_close(net.vio);
}

int ClientMySQLProtocol::getFileDescriptor(void)
{
  return drizzleclient_net_get_sd(&net);
}

bool ClientMySQLProtocol::isConnected()
{
  return net.vio != 0;
}

bool ClientMySQLProtocol::isReading(void)
{
  return net.reading_or_writing == 1;
}

bool ClientMySQLProtocol::isWriting(void)
{
  return net.reading_or_writing == 2;
}

bool ClientMySQLProtocol::flush()
{
  if (net.vio == NULL)
    return false;
  bool ret= drizzleclient_net_write(&net, (unsigned char*) packet.ptr(),
                           packet.length());
  packet.length(0);
  return ret;
}

void ClientMySQLProtocol::close(void)
{
  if (net.vio)
  { 
    drizzleclient_net_close(&net);
    drizzleclient_net_end(&net);
  }
}

bool ClientMySQLProtocol::authenticate()
{
  bool connection_is_valid;

  /* Use "connect_timeout" value during connection phase */
  drizzleclient_net_set_read_timeout(&net, connect_timeout);
  drizzleclient_net_set_write_timeout(&net, connect_timeout);

  connection_is_valid= checkConnection();

  if (connection_is_valid)
    sendOK();
  else
  {
    sendError(session->main_da.sql_errno(), session->main_da.message());
    return false;
  }

  /* Connect completed, set read/write timeouts back to default */
  drizzleclient_net_set_read_timeout(&net, read_timeout);
  drizzleclient_net_set_write_timeout(&net, write_timeout);
  return true;
}

bool ClientMySQLProtocol::readCommand(char **l_packet, uint32_t *packet_length)
{
  /*
    This thread will do a blocking read from the client which
    will be interrupted when the next command is received from
    the client, the connection is closed or "net_wait_timeout"
    number of seconds has passed
  */
#ifdef NEVER
  /* We can do this much more efficiently with poll timeouts or watcher thread,
     disabling for now, which means net_wait_timeout == read_timeout. */
  drizzleclient_net_set_read_timeout(&net,
                                     session->variables.net_wait_timeout);
#endif

  net.pkt_nr=0;

  *packet_length= drizzleclient_net_read(&net);
  if (*packet_length == packet_error)
  {
    /* Check if we can continue without closing the connection */

    if(net.last_errno== CR_NET_PACKET_TOO_LARGE)
      my_error(ER_NET_PACKET_TOO_LARGE, MYF(0));
    if (session->main_da.status() == Diagnostics_area::DA_ERROR)
      sendError(session->main_da.sql_errno(), session->main_da.message());
    else
      sendOK();

    if (net.error != 3)
      return false;                       // We have to close it.

    net.error= 0;
    *packet_length= 0;
    return true;
  }

  *l_packet= (char*) net.read_pos;

  /*
    'packet_length' contains length of data, as it was stored in packet
    header. In case of malformed header, drizzleclient_net_read returns zero.
    If packet_length is not zero, drizzleclient_net_read ensures that the returned
    number of bytes was actually read from network.
    There is also an extra safety measure in drizzleclient_net_read:
    it sets packet[packet_length]= 0, but only for non-zero packets.
  */

  if (*packet_length == 0)                       /* safety */
  {
    /* Initialize with COM_SLEEP packet */
    (*l_packet)[0]= (unsigned char) COM_SLEEP;
    *packet_length= 1;
  }
  else if (using_mysql41_protocol)
  {
    /* Map from MySQL commands to Drizzle commands. */
    switch ((int)(*l_packet)[0])
    {
    case 0: /* SLEEP */
    case 1: /* QUIT */
    case 2: /* INIT_DB */
    case 3: /* QUERY */
      break;

    case 8: /* SHUTDOWN */
      (*l_packet)[0]= (unsigned char) COM_SHUTDOWN;
      break;

    case 14: /* PING */
      (*l_packet)[0]= (unsigned char) COM_SHUTDOWN;
      break;


    default:
      /* Just drop connection for MySQL commands we don't support. */
      (*l_packet)[0]= (unsigned char) COM_QUIT;
      *packet_length= 1;
      break;
    }
  }

  /* Do not rely on drizzleclient_net_read, extra safety against programming errors. */
  (*l_packet)[*packet_length]= '\0';                  /* safety */

#ifdef NEVER
  /* See comment above. */
  /* Restore read timeout value */
  drizzleclient_net_set_read_timeout(&net,
                                     session->variables.net_read_timeout);
#endif

  return true;
}

/**
  Return ok to the client.

  The ok packet has the following structure:

  - 0               : Marker (1 byte)
  - affected_rows    : Stored in 1-9 bytes
  - id        : Stored in 1-9 bytes
  - server_status    : Copy of session->server_status;  Can be used by client
  to check if we are inside an transaction.
  New in 4.0 client
  - warning_count    : Stored in 2 bytes; New in 4.1 client
  - message        : Stored as packed length (1-9 bytes) + message.
  Is not stored if no message.

  @param session           Thread handler
  @param affected_rows       Number of rows changed by statement
  @param id           Auto_increment id for first row (if used)
  @param message       Message to send to the client (Used by mysql_status)
*/

void ClientMySQLProtocol::sendOK()
{
  unsigned char buff[DRIZZLE_ERRMSG_SIZE+10],*pos;
  const char *message= NULL;
  uint32_t tmp;

  if (!net.vio)    // hack for re-parsing queries
  {
    return;
  }

  buff[0]=0;                    // No fields
  if (session->main_da.status() == Diagnostics_area::DA_OK)
  {
    if (client_capabilities & CLIENT_FOUND_ROWS && session->main_da.found_rows())
      pos=drizzleclient_net_store_length(buff+1,session->main_da.found_rows());
    else
      pos=drizzleclient_net_store_length(buff+1,session->main_da.affected_rows());
    pos=drizzleclient_net_store_length(pos, session->main_da.last_insert_id());
    int2store(pos, session->main_da.server_status());
    pos+=2;
    tmp= min(session->main_da.total_warn_count(), (uint32_t)65535);
    message= session->main_da.message();
  }
  else
  {
    pos=drizzleclient_net_store_length(buff+1,0);
    pos=drizzleclient_net_store_length(pos, 0);
    int2store(pos, session->server_status);
    pos+=2;
    tmp= min(session->total_warn_count, (uint32_t)65535);
  }

  /* We can only return up to 65535 warnings in two bytes */
  int2store(pos, tmp);
  pos+= 2;

  session->main_da.can_overwrite_status= true;

  if (message && message[0])
  {
    size_t length= strlen(message);
    pos=drizzleclient_net_store_length(pos,length);
    memcpy(pos,(unsigned char*) message,length);
    pos+=length;
  }
  drizzleclient_net_write(&net, buff, (size_t) (pos-buff));
  drizzleclient_net_flush(&net);

  session->main_da.can_overwrite_status= false;
}

/**
  Send eof (= end of result set) to the client.

  The eof packet has the following structure:

  - 254    (DRIZZLE_PROTOCOL_NO_MORE_DATA)    : Marker (1 byte)
  - warning_count    : Stored in 2 bytes; New in 4.1 client
  - status_flag    : Stored in 2 bytes;
  For flags like SERVER_MORE_RESULTS_EXISTS.

  Note that the warning count will not be sent if 'no_flush' is set as
  we don't want to report the warning count until all data is sent to the
  client.
*/

void ClientMySQLProtocol::sendEOF()
{
  /* Set to true if no active vio, to work well in case of --init-file */
  if (net.vio != 0)
  {
    session->main_da.can_overwrite_status= true;
    writeEOFPacket(session->main_da.server_status(),
                   session->main_da.total_warn_count());
    drizzleclient_net_flush(&net);
    session->main_da.can_overwrite_status= false;
  }
  packet.shrink(buffer_length);
}


void ClientMySQLProtocol::sendError(uint32_t sql_errno, const char *err)
{
  uint32_t length;
  /*
    buff[]: sql_errno:2 + ('#':1 + SQLSTATE_LENGTH:5) + DRIZZLE_ERRMSG_SIZE:512
  */
  unsigned char buff[2+1+SQLSTATE_LENGTH+DRIZZLE_ERRMSG_SIZE], *pos;

  assert(sql_errno);
  assert(err && err[0]);

  /*
    It's one case when we can push an error even though there
    is an OK or EOF already.
  */
  session->main_da.can_overwrite_status= true;

  /* Abort multi-result sets */
  session->server_status&= ~SERVER_MORE_RESULTS_EXISTS;

  /**
    Send a error string to client.

    For SIGNAL/RESIGNAL and GET DIAGNOSTICS functionality it's
    critical that every error that can be intercepted is issued in one
    place only, my_message_sql.
  */

  if (net.vio == 0)
  {
    return;
  }

  int2store(buff,sql_errno);
  pos= buff+2;

  /* The first # is to make the client backward compatible */
  buff[2]= '#';
  pos= (unsigned char*) strcpy((char*) buff+3, drizzle_errno_to_sqlstate(sql_errno));
  pos+= strlen(drizzle_errno_to_sqlstate(sql_errno));

  char *tmp= strncpy((char*)pos, err, DRIZZLE_ERRMSG_SIZE-1);
  tmp+= strlen((char*)pos);
  tmp[0]= '\0';
  length= (uint32_t)(tmp-(char*)buff);
  err= (char*) buff;

  drizzleclient_net_write_command(&net,(unsigned char) 255, (unsigned char*) "", 0, (unsigned char*) err, length);

  session->main_da.can_overwrite_status= false;
}

/**
  Send name and type of result to client.

  Sum fields has table name empty and field_name.

  @param Session        Thread data object
  @param list            List of items to send to client
  @param flag            Bit mask with the following functions:
                        - 1 send number of rows
                        - 2 send default values
                        - 4 don't write eof packet

  @retval
    0    ok
  @retval
    1    Error  (Note that in this case the error is not sent to the
    client)
*/
bool ClientMySQLProtocol::sendFields(List<Item> *list)
{
  List_iterator_fast<Item> it(*list);
  Item *item;
  unsigned char buff[80];
  String tmp((char*) buff,sizeof(buff),&my_charset_bin);

  unsigned char *row_pos= drizzleclient_net_store_length(buff, list->elements);
  (void) drizzleclient_net_write(&net, buff, (size_t) (row_pos-buff));

  while ((item=it++))
  {
    char *pos;
    SendField field;
    item->make_field(&field);

    packet.length(0);

    if (store(STRING_WITH_LEN("def")) ||
        store(field.db_name) ||
        store(field.table_name) ||
        store(field.org_table_name) ||
        store(field.col_name) ||
        store(field.org_col_name) ||
        packet.realloc(packet.length()+12))
      goto err;

    /* Store fixed length fields */
    pos= (char*) packet.ptr()+packet.length();
    *pos++= 12;                // Length of packed fields
    /* No conversion */
    int2store(pos, field.charsetnr);
    int4store(pos+2, field.length);

    if (using_mysql41_protocol)
    {
      /* Switch to MySQL field numbering. */
      switch (field.type)
      {
      case DRIZZLE_TYPE_LONG:
        pos[6]= 3;
        break;

      case DRIZZLE_TYPE_DOUBLE:
        pos[6]= 5;
        break;

      case DRIZZLE_TYPE_NULL:
        pos[6]= 6;
        break;

      case DRIZZLE_TYPE_TIMESTAMP:
        pos[6]= 7;
        break;

      case DRIZZLE_TYPE_LONGLONG:
        pos[6]= 8;
        break;

      case DRIZZLE_TYPE_DATETIME:
        pos[6]= 12;
        break;

      case DRIZZLE_TYPE_DATE:
        pos[6]= 14;
        break;

      case DRIZZLE_TYPE_VARCHAR:
        pos[6]= 15;
        break;

      case DRIZZLE_TYPE_DECIMAL:
        pos[6]= (char)246;
        break;

      case DRIZZLE_TYPE_ENUM:
        pos[6]= (char)247;
        break;

      case DRIZZLE_TYPE_BLOB:
        pos[6]= (char)252;
        break;
      }
    }
    else
    {
      /* Add one to compensate for tinyint removal from enum. */
      pos[6]= field.type + 1;
    }

    int2store(pos+7,field.flags);
    pos[9]= (char) field.decimals;
    pos[10]= 0;                // For the future
    pos[11]= 0;                // For the future
    pos+= 12;

    packet.length((uint32_t) (pos - packet.ptr()));
    if (flush())
      break;
  }

  /*
    Mark the end of meta-data result set, and store session->server_status,
    to show that there is no cursor.
    Send no warning information, as it will be sent at statement end.
  */
  writeEOFPacket(session->server_status, session->total_warn_count);
  return 0;

err:
  my_message(ER_OUT_OF_RESOURCES, ER(ER_OUT_OF_RESOURCES),
             MYF(0));
  return 1;
}

bool ClientMySQLProtocol::store(Field *from)
{
  if (from->is_null())
    return store();
  char buff[MAX_FIELD_WIDTH];
  String str(buff,sizeof(buff), &my_charset_bin);

  from->val_str(&str);

  return netStoreData((const unsigned char *)str.ptr(), str.length());
}

bool ClientMySQLProtocol::store(void)
{
  char buff[1];
  buff[0]= (char)251;
  return packet.append(buff, sizeof(buff), PACKET_BUFFER_EXTRA_ALLOC);
}

bool ClientMySQLProtocol::store(int32_t from)
{
  char buff[12];
  return netStoreData((unsigned char*) buff,
                      (size_t) (int10_to_str(from, buff, -10) - buff));
}

bool ClientMySQLProtocol::store(uint32_t from)
{
  char buff[11];
  return netStoreData((unsigned char*) buff,
                      (size_t) (int10_to_str(from, buff, 10) - buff));
}

bool ClientMySQLProtocol::store(int64_t from)
{
  char buff[22];
  return netStoreData((unsigned char*) buff,
                      (size_t) (int64_t10_to_str(from, buff, -10) - buff));
}

bool ClientMySQLProtocol::store(uint64_t from)
{
  char buff[21];
  return netStoreData((unsigned char*) buff,
                      (size_t) (int64_t10_to_str(from, buff, 10) - buff));
}

bool ClientMySQLProtocol::store(double from, uint32_t decimals, String *buffer)
{
  buffer->set_real(from, decimals, session->charset());
  return netStoreData((unsigned char*) buffer->ptr(), buffer->length());
}

bool ClientMySQLProtocol::store(const char *from, size_t length)
{
  return netStoreData((const unsigned char *)from, length);
}

bool ClientMySQLProtocol::wasAborted(void)
{
  return net.error && net.vio != 0;
}

bool ClientMySQLProtocol::haveMoreData(void)
{
  return drizzleclient_net_more_data(&net);
}

bool ClientMySQLProtocol::haveError(void)
{
  return net.error || net.vio == 0;
}

bool ClientMySQLProtocol::checkConnection(void)
{
  uint32_t pkt_len= 0;
  char *end;

  // TCP/IP connection
  {
    char ip[NI_MAXHOST];
    uint16_t peer_port;

    if (drizzleclient_net_peer_addr(&net, ip, &peer_port, NI_MAXHOST))
    {
      my_error(ER_BAD_HOST_ERROR, MYF(0), session->security_ctx.ip.c_str());
      return false;
    }

    session->security_ctx.ip.assign(ip);
  }
  drizzleclient_net_keepalive(&net, true);

  uint32_t server_capabilites;
  {
    /* buff[] needs to big enough to hold the server_version variable */
    char buff[SERVER_VERSION_LENGTH + SCRAMBLE_LENGTH + 64];

    server_capabilites= CLIENT_BASIC_FLAGS;

    if (using_mysql41_protocol)
      server_capabilites|= CLIENT_PROTOCOL_MYSQL41;

#ifdef HAVE_COMPRESS
    server_capabilites|= CLIENT_COMPRESS;
#endif /* HAVE_COMPRESS */

    end= buff + strlen(VERSION);
    if ((end - buff) >= SERVER_VERSION_LENGTH)
      end= buff + (SERVER_VERSION_LENGTH - 1);
    memcpy(buff, VERSION, end - buff);
    *end= 0;
    end++;

    int4store((unsigned char*) end, global_thread_id);
    end+= 4;

    /* We don't use scramble anymore. */
    memset(end, 'X', SCRAMBLE_LENGTH_323);
    end+= SCRAMBLE_LENGTH_323;
    *end++= 0; /* an empty byte for some reason */

    int2store(end, server_capabilites);
    /* write server characteristics: up to 16 bytes allowed */
    end[2]=(char) default_charset_info->number;
    int2store(end+3, session->server_status);
    memset(end+5, 0, 13);
    end+= 18;

    /* Write scramble tail. */
    memset(end, 'X', SCRAMBLE_LENGTH - SCRAMBLE_LENGTH_323);
    end+= (SCRAMBLE_LENGTH - SCRAMBLE_LENGTH_323);
    *end++= 0; /* an empty byte for some reason */

    /* At this point we write connection message and read reply */
    if (drizzleclient_net_write_command(&net
          , (unsigned char) PROTOCOL_VERSION
          , (unsigned char*) ""
          , 0
          , (unsigned char*) buff
          , (size_t) (end-buff)) 
        ||    (pkt_len= drizzleclient_net_read(&net)) == packet_error 
        || pkt_len < MIN_HANDSHAKE_SIZE)
    {
      my_error(ER_HANDSHAKE_ERROR, MYF(0), session->security_ctx.ip.c_str());
      return false;
    }
  }
  if (packet.alloc(buffer_length))
    return false; /* The error is set by alloc(). */

  client_capabilities= uint2korr(net.read_pos);


  client_capabilities|= ((uint32_t) uint2korr(net.read_pos + 2)) << 16;
  session->max_client_packet_length= uint4korr(net.read_pos + 4);
  end= (char*) net.read_pos + 32;

  /*
    Disable those bits which are not supported by the server.
    This is a precautionary measure, if the client lies. See Bug#27944.
  */
  client_capabilities&= server_capabilites;

  if (end >= (char*) net.read_pos + pkt_len + 2)
  {
    my_error(ER_HANDSHAKE_ERROR, MYF(0), session->security_ctx.ip.c_str());
    return false;
  }

  net.return_status= &session->server_status;

  char *user= end;
  char *passwd= strchr(user, '\0')+1;
  uint32_t user_len= passwd - user - 1;
  char *l_db= passwd;

  /*
    Old clients send null-terminated string as password; new clients send
    the size (1 byte) + string (not null-terminated). Hence in case of empty
    password both send '\0'.

    This strlen() can't be easily deleted without changing client.

    Cast *passwd to an unsigned char, so that it doesn't extend the sign for
    *passwd > 127 and become 2**32-127+ after casting to uint.
  */
  uint32_t passwd_len= client_capabilities & CLIENT_SECURE_CONNECTION ?
    (unsigned char)(*passwd++) : strlen(passwd);
  l_db= client_capabilities & CLIENT_CONNECT_WITH_DB ? l_db + passwd_len + 1 : 0;

  /* strlen() can't be easily deleted without changing client */
  uint32_t db_len= l_db ? strlen(l_db) : 0;

  if (passwd + passwd_len + db_len > (char *) net.read_pos + pkt_len)
  {
    my_error(ER_HANDSHAKE_ERROR, MYF(0), session->security_ctx.ip.c_str());
    return false;
  }

  /* If username starts and ends in "'", chop them off */
  if (user_len > 1 && user[0] == '\'' && user[user_len - 1] == '\'')
  {
    user[user_len-1]= 0;
    user++;
    user_len-= 2;
  }

  session->security_ctx.user.assign(user);

  return session->checkUser(passwd, passwd_len, l_db);
}

bool ClientMySQLProtocol::netStoreData(const unsigned char *from, size_t length)
{
  size_t packet_length= packet.length();
  /*
     The +9 comes from that strings of length longer than 16M require
     9 bytes to be stored (see drizzleclient_net_store_length).
  */
  if (packet_length+9+length > packet.alloced_length() &&
      packet.realloc(packet_length+9+length))
    return 1;
  unsigned char *to= drizzleclient_net_store_length((unsigned char*) packet.ptr()+packet_length, length);
  memcpy(to,from,length);
  packet.length((size_t) (to+length-(unsigned char*) packet.ptr()));
  return 0;
}

/**
  Format EOF packet according to the current client and
  write it to the network output buffer.
*/

void ClientMySQLProtocol::writeEOFPacket(uint32_t server_status,
                                         uint32_t total_warn_count)
{
  unsigned char buff[5];
  /*
    Don't send warn count during SP execution, as the warn_list
    is cleared between substatements, and mysqltest gets confused
  */
  uint32_t tmp= min(total_warn_count, (uint32_t)65535);
  buff[0]= DRIZZLE_PROTOCOL_NO_MORE_DATA;
  int2store(buff+1, tmp);
  /*
    The following test should never be true, but it's better to do it
    because if 'is_fatal_error' is set the server is not going to execute
    other queries (see the if test in dispatch_command / COM_QUERY)
  */
  if (session->is_fatal_error)
    server_status&= ~SERVER_MORE_RESULTS_EXISTS;
  int2store(buff + 3, server_status);
  drizzleclient_net_write(&net, buff, 5);
}

static ListenMySQLProtocol *listen_obj= NULL;

static int init(drizzled::plugin::Registry &registry)
{
  listen_obj= new ListenMySQLProtocol("mysql_protocol", true);
  registry.add(listen_obj); 
  return 0;
}

static int deinit(drizzled::plugin::Registry &registry)
{
  registry.remove(listen_obj);
  delete listen_obj;
  return 0;
}

static DRIZZLE_SYSVAR_UINT(port, port, PLUGIN_VAR_RQCMDARG,
                           N_("Port number to use for connection or 0 for default to with MySQL "
                              "protocol."),
                           NULL, NULL, 3306, 0, 65535, 0);
static DRIZZLE_SYSVAR_UINT(connect_timeout, connect_timeout,
                           PLUGIN_VAR_RQCMDARG, N_("Connect Timeout."),
                           NULL, NULL, 10, 1, 300, 0);
static DRIZZLE_SYSVAR_UINT(read_timeout, read_timeout, PLUGIN_VAR_RQCMDARG,
                           N_("Read Timeout."), NULL, NULL, 30, 1, 300, 0);
static DRIZZLE_SYSVAR_UINT(write_timeout, write_timeout, PLUGIN_VAR_RQCMDARG,
                           N_("Write Timeout."), NULL, NULL, 60, 1, 300, 0);
static DRIZZLE_SYSVAR_UINT(retry_count, retry_count, PLUGIN_VAR_RQCMDARG,
                           N_("Retry Count."), NULL, NULL, 10, 1, 100, 0);
static DRIZZLE_SYSVAR_UINT(buffer_length, buffer_length, PLUGIN_VAR_RQCMDARG,
                           N_("Buffer length."), NULL, NULL, 16384, 1024,
                           1024*1024, 0);
static DRIZZLE_SYSVAR_STR(bind_address, bind_address, PLUGIN_VAR_READONLY,
                          N_("Address to bind to."), NULL, NULL, NULL);

static struct st_mysql_sys_var* system_variables[]= {
  DRIZZLE_SYSVAR(port),
  DRIZZLE_SYSVAR(connect_timeout),
  DRIZZLE_SYSVAR(read_timeout),
  DRIZZLE_SYSVAR(write_timeout),
  DRIZZLE_SYSVAR(retry_count),
  DRIZZLE_SYSVAR(buffer_length),
  DRIZZLE_SYSVAR(bind_address),
  NULL
};

drizzle_declare_plugin
{
  "mysql_protocol",
  "0.1",
  "Eric Day",
  "MySQL Protocol Module",
  PLUGIN_LICENSE_GPL,
  init,             /* Plugin Init */
  deinit,           /* Plugin Deinit */
  NULL,             /* status variables */
  system_variables, /* system variables */
  NULL              /* config options */
}
drizzle_declare_plugin_end;