/* Copyright (C) 2000-2001, 2003-2004 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/* Mallocs for used in threads */

#include <drizzled/server_includes.h>
#include <drizzled/current_session.h>
#include <drizzled/error.h>


extern "C" {
  void sql_alloc_error_handler(void)
  {
    sql_print_error("%s",ER(ER_OUT_OF_RESOURCES));
  }
}

void init_sql_alloc(MEM_ROOT *mem_root, size_t block_size, size_t pre_alloc)
{
  init_alloc_root(mem_root, block_size, pre_alloc);
  mem_root->error_handler=sql_alloc_error_handler;
}


void *sql_alloc(size_t Size)
{
  MEM_ROOT *root= current_mem_root();
  return alloc_root(root,Size);
}


void *sql_calloc(size_t size)
{
  void *ptr;
  if ((ptr=sql_alloc(size)))
    memset(ptr, 0, size);
  return ptr;
}


char *sql_strdup(const char *str)
{
  size_t len= strlen(str)+1;
  char *pos;
  if ((pos= (char*) sql_alloc(len)))
    memcpy(pos,str,len);
  return pos;
}


char *sql_strmake(const char *str, size_t len)
{
  char *pos;
  if ((pos= (char*) sql_alloc(len+1)))
  {
    memcpy(pos,str,len);
    pos[len]=0;
  }
  return pos;
}


void* sql_memdup(const void *ptr, size_t len)
{
  void *pos;
  if ((pos= sql_alloc(len)))
    memcpy(pos,ptr,len);
  return pos;
}

void sql_element_free(void *)
{} /* purecov: deadcode */



char *sql_strmake_with_convert(const char *str, size_t arg_length,
                               const CHARSET_INFO * const from_cs,
                               size_t max_res_length,
                               const CHARSET_INFO * const to_cs,
                               size_t *result_length)
{
  char *pos;
  size_t new_length= to_cs->mbmaxlen*arg_length;
  max_res_length--;				// Reserve place for end null

  set_if_smaller(new_length, max_res_length);
  if (!(pos= (char*) sql_alloc(new_length+1)))
    return pos;					// Error

  if ((from_cs == &my_charset_bin) || (to_cs == &my_charset_bin))
  {
    // Safety if to_cs->mbmaxlen > 0
    new_length= cmin(arg_length, max_res_length);
    memcpy(pos, str, new_length);
  }
  else
  {
    uint32_t dummy_errors;
    new_length= copy_and_convert((char*) pos, new_length, to_cs, str,
				 arg_length, from_cs, &dummy_errors);
  }
  pos[new_length]= 0;
  *result_length= new_length;
  return pos;
}

