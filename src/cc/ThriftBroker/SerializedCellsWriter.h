/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Hypertable. If not, see <http://www.gnu.org/licenses/>
 */

#ifndef HYPERTABLE_SERIALIZEDCELLSWRITER_H
#define HYPERTABLE_SERIALIZEDCELLSWRITER_H

#include "Common/DynamicBuffer.h"

#include "Hypertable/Lib/Cell.h"
#include "Hypertable/Lib/KeySpec.h"

#include "SerializedCellsFlag.h"

namespace Hypertable {

  class SerializedCellsWriter {
  public:
 
    SerializedCellsWriter(int32_t size, bool grow = false)
      :  m_buf(size), m_finalized(false), m_grow(grow), m_version_sent(false),
         m_previous_row_offset(-1), m_previous_row_length(0) { }

    bool add(Cell &cell) {
      return add(cell.row_key, cell.column_family, cell.column_qualifier,
                 cell.timestamp, cell.value, cell.value_len, cell.flag);
    }

    bool add(const char *row, const char *column_family,
             const char *column_qualifier, int64_t timestamp,
             const char *value, int32_t value_length,
			 int cell_flag) {
      return add(row, column_family, column_qualifier, timestamp,
                (const void *)value, value_length, (uint8_t)cell_flag);
    }

    bool add(const char *row, const char *column_family,
             const char *column_qualifier, int64_t timestamp,
             const void *value, int32_t value_length,
			 uint8_t cell_flag = FLAG_INSERT);

    void finalize(uint8_t flag) {
      if (m_grow)
        m_buf.ensure(m_buf.empty() ? 5 : 1);
      if (m_buf.empty())
        Serialization::encode_i32(&m_buf.ptr, SerializedCellsVersion::VERSION);
      *m_buf.ptr++ = SerializedCellsFlag::EOB | flag;
      m_finalized = true;
    }

    uint8_t *get_buffer() { return m_buf.base; }
    uint8_t *get_buffer() const { return m_buf.base; }
    int32_t get_buffer_length() { return m_buf.fill(); }
    int32_t get_buffer_length() const { return m_buf.fill(); }

    void get_buffer(const uint8_t **bufp, int32_t *lenp) {
      if (!m_finalized)
        finalize(SerializedCellsFlag::EOB);
      *bufp = m_buf.base;
      *lenp = m_buf.fill();
    }

    bool empty() { return m_buf.empty(); }

    void clear();

  private:
    DynamicBuffer m_buf;
    bool m_finalized;
    bool m_grow;
    bool m_version_sent;
    size_t m_previous_row_offset;
    int32_t m_previous_row_length;
  };

}

#endif // HYPERTABLE_SERIALIZEDCELLSWRITER_H
