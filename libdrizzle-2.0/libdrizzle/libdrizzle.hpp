/* Drizzle Client Library
 * Copyright (C) 2011 Olaf van der Spek
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
 */

#pragma once

#include <cstring>
#include <libdrizzle/libdrizzle.h>
#include <stdexcept>

namespace drizzle {

class bad_query : public std::runtime_error
{
};

class drizzle_c
{
public:
  drizzle_c()
  {
    drizzle_create(&b_);
  }

  ~drizzle_c()
  {
    drizzle_free(&b_);
  }

  drizzle_st b_;
};

class result_c
{
public:
  result_c()
  {
    memset(&b_, 0, sizeof(b_));
  }

  ~result_c()
  {
    drizzle_result_free(&b_);
  }

  const char* error()
  {
    return drizzle_result_error(&b_);
  }

  uint16_t error_code()
  {
    return drizzle_result_error_code(&b_);
  }

  uint16_t column_count()
  {
    return drizzle_result_column_count(&b_);    
  }

  uint64_t row_count()
  {
    return drizzle_result_row_count(&b_);
  }

  drizzle_column_st* column_next()
  {
    return drizzle_column_next(&b_);
  }

  drizzle_row_t row_next()
  {
    return drizzle_row_next(&b_);
  }

  void column_seek(uint16_t i)
  {
    drizzle_column_seek(&b_, i);
  }

  void row_seek(uint64_t i)
  {
    drizzle_row_seek(&b_, i);
  }

  size_t* row_field_sizes()
  {
    return drizzle_row_field_sizes(&b_);
  }

  drizzle_result_st b_;
};

class connection_c
{
public:
  explicit connection_c(drizzle_c& drizzle)
  {
    drizzle_con_create(&drizzle.b_, &b_);
  }

  ~connection_c()
  {
    drizzle_con_free(&b_);
  }

  const char* error()
  {
    return drizzle_con_error(&b_);
  }

  void set_tcp(const char* host, in_port_t port)
  {
    drizzle_con_set_tcp(&b_, host, port);
  }

  void set_auth(const char* user, const char* password)
  {
    drizzle_con_set_auth(&b_, user, password);
  }

  void set_db(const char* db)
  {
    drizzle_con_set_db(&b_, db);
  }

  drizzle_return_t query(result_c& result, const char* str, size_t str_size)
  {
    drizzle_return_t ret;
    drizzle_query(&b_, &result.b_, str, str_size, &ret);
    if (ret == DRIZZLE_RETURN_OK)
      ret = drizzle_result_buffer(&result.b_);
    return ret;
  }

  drizzle_return_t query(result_c& result, const std::string& str)
  {
    return query(result, str.data(), str.size());
  }

  drizzle_return_t query(result_c& result, const char* str)
  {
    return query(result, str, strlen(str));
  }

  drizzle_con_st b_;
};

class query_c
{
public:
  query_c(connection_c& con, const std::string& in = "") :
    con_(con),
    in_(in)
  {
  }

  void operator=(const std::string& v)
  {
    in_ = v;
    out_.clear();
  }

  void operator+=(const std::string& v)
  {
    in_ += v;
  }

  query_c& p_name(const std::string& v)
  {
    std::vector<char> r(2 * v.size() + 2);
    r.resize(drizzle_escape_string(&r.front() + 1, v.data(), v.size()) + 2);    
    r.front() = '`';
    r.back() = '`';
    p_raw(&r.front(), r.size());
    return *this;
  }

  query_c& p_raw(const char* v, size_t sz)
  {
    size_t i = in_.find('?');
    assert(i != std::string::npos);
    if (i == std::string::npos)
      return *this;
    out_.append(in_.substr(0, i));
    in_.erase(0, i + 1);
    out_.append(v, sz);
    return *this;
  }

  query_c& p_raw(const std::string& v)
  {
    return p_raw(v.data(), v.size());
  }

  query_c& p(const std::string& v)
  {
    std::vector<char> r(2 * v.size() + 2);
    r.resize(drizzle_escape_string(&r.front() + 1, v.data(), v.size()) + 2);    
    r.front() = '\'';
    r.back() = '\'';
    p_raw(&r.front(), r.size());
    return *this;
  }

  query_c& p(long long)
  {
    return *this;
  }

  drizzle_return_t execute(result_c& result)
  {
    return con_.query(result, read());
  }

  std::string read() const
  {
    return out_ + in_;
  }
private:
  connection_c& con_;
  std::string in_;
  std::string out_;
};

}
