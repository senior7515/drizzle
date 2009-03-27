/* -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
 *  vim:expandtab:shiftwidth=2:tabstop=2:smarttab:
 *
 *  Copyright (C) 2009 Sun Microsystems
 *  Copyright 2005-2008 Intel Corporation.  All Rights Reserved.
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

#ifndef DRIZZLED_ATOMIC_PTHREAD_TRAITS_H
#define DRIZZLED_ATOMIC_PTHREAD_TRAITS_H

namespace tbb {

namespace internal {

class mutex_wrapper
{
private:
  pthread_mutex_t the_mutex;
  bool locked;
public:
  mutex_wrapper(void): locked(false)
  {
    locked= false;
    (void) pthread_mutex_init(&the_mutex,  NULL);
  }
  ~mutex_wrapper(void)
  {
    if (locked)
      unlock();
    pthread_mutex_destroy(&the_mutex);
  }
  void lock(void)
  {
    pthread_mutex_lock(&the_mutex);
    locked=true;
  }
  void unlock(void)
  {
    pthread_mutex_unlock(&the_mutex);
    locked=false;
  }
};

template<typename T, typename D>
class pthread_traits
{
private:
  mutex_wrapper my_lock;

public:

  typedef T value_type;

  pthread_traits() {}

  inline value_type fetch_and_add(volatile value_type *value, D addend )
  {
    my_lock.lock();
    *value += addend;
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

  inline value_type fetch_and_increment(volatile value_type *value)
  {
    my_lock.lock();
    *value++;
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

  inline value_type fetch_and_decrement(volatile value_type *value)
  {
    my_lock.lock();
    *value--;
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

  inline value_type fetch_and_store(volatile value_type *value,
                                    value_type new_value )
  {
    my_lock.lock();
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

  inline value_type compare_and_swap(volatile value_type *value,
                                     value_type new_value,
                                     value_type comparand )
  {
    my_lock.lock();
    if (*value == comparand)
      *value= new_value;
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

  inline value_type fetch(const volatile value_type *value) const volatile
  {
    return *value;
  }

  inline value_type store_with_release(volatile value_type *value,
                                       value_type new_value)
  {
    my_lock.lock();
    *value= new_value;
    value_type ret= *value;
    my_lock.unlock();
    return ret;
  }

}; /* pthread_traits */


} /* namespace internal */
} /* namespace tbb */


#endif /* DRIZZLED_ATOMIC_PTHREAD_TRAITS_H */