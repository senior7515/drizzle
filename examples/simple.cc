/*
 * Drizzle Client & Protocol Library
 *
 * Copyright (C) 2008 Eric Day (eday@oddments.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 *     * The names of its contributors may not be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <libdrizzle/libdrizzle.hpp>
#include <netdb.h>
#include <unistd.h>

using namespace std;

int main(int argc, char *argv[])
{
  const char* db= "information_schema";
  const char* host= NULL;
  const char* user= NULL;
  const char* password= NULL;
  bool mysql= false;
  in_port_t port= 0;
  drizzle_verbose_t verbose= DRIZZLE_VERBOSE_NEVER;

  for (int c; (c = getopt(argc, argv, "d:h:mp:u:P:q:v")) != -1; )
  {
    switch (c)
    {
    case 'd':
      db= optarg;
      break;

    case 'h':
      host= optarg;
      break;

    case 'm':
      mysql= true;
      break;

    case 'p':
      port= static_cast<in_port_t>(atoi(optarg));
      break;

    case 'u':
      user= optarg;
      break;

    case 'P':
      password = optarg;
      break;

    case 'v':
      if (verbose < DRIZZLE_VERBOSE_MAX)
        verbose= static_cast<drizzle_verbose_t>(verbose + 1);
      break;

    default:
      cout << 
        "usage:\n"
        "\t-d <db>    - Database to use for query\n"
        "\t-h <host>  - Host to connect to\n"
        "\t-m         - Use the MySQL protocol\n"
        "\t-p <port>  - Port to connect to\n"
        "\t-u <user>  - User\n"
        "\t-P <pass>  - Password\n"
        "\t-q <query> - Query to run\n"
        "\t-v         - Increase verbosity level\n";
      return 1;
    }
  }

  drizzle::drizzle_c drizzle;
  drizzle_set_verbose(&drizzle.b_, verbose);
  drizzle::connection_c* con= new drizzle::connection_c(drizzle);
  if (mysql)
    drizzle_con_add_options(&con->b_, DRIZZLE_CON_MYSQL);
  con->set_tcp(host, port);
  con->set_auth(user, password);
  con->set_db(db);
  drizzle::result_c result;
  drizzle::query_c q(*con, "select table_schema, table_name from tables where table_name like ?");
  q.p("%");
  cout << q.read() << endl;
  if (q.execute(result))
  {
    cerr << "query: " << con->error() << endl;
    return 1;
  }
  while (drizzle_row_t row= result.row_next())
  {
    for (int x= 0; x < result.column_count(); x++)
    {
      if (x)
        cout << ", ";
      cout << (row[x] ? row[x] : "NULL");
    }
    cout << endl;
  }
  return 0;
}
