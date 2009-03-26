/** -*- c++ -*-
 * Copyright (C) 2009 Doug Judd (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
 * License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include "Common/Compat.h"
#include <cassert>

#include "TableIdCache.h"

using namespace Hypertable;

bool TableIdCache::contains(int id) {
  ScopedLock lock(m_mutex);
  HashIndex &hash_index = m_cache.get<1>();
  return hash_index.find(id) != hash_index.end();
}

void TableIdCache::insert(int id) {
  ScopedLock lock(m_mutex);
  m_cache.push_back(id);
  if (m_cache.size() > (size_t)m_max_entries)
    m_cache.pop_front();
}
