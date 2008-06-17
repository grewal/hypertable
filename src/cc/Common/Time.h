/**
 * Copyright (C) 2007 Luke Lu (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
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

#ifndef HYPERTABLE_HIRES_TIME_H
#define HYPERTABLE_HIRES_TIME_H

#include <boost/thread/xtime.hpp>

namespace Hypertable {
  using boost::TIME_UTC;

  /** High-Resolution Time
   */
  struct HiResTime : public boost::xtime {
    HiResTime(int type = TIME_UTC) { reset(type); }

    /** get the current hi-res time
     *  return true on success; otherwise false
     */
    bool reset(int type = TIME_UTC) {
      return boost::xtime_get(this, type) != 0;
    }

    /** get the current hi-res time in seconds
     *  returns a double-precision floating-point number
     *  returns 0 on failure.
     */
    double get_double(int type = TIME_UTC) {
      if (!reset(type)) return 0.0;
      return double(sec) + double(nsec)/1e9;
    }

    /** compare with other HiResTime
     *  <: -1; ==: 0; >: 1
     */
    int cmp(const HiResTime &y) const {
      return boost::xtime_cmp(*this, y);
    }

    bool operator<(const HiResTime &y) const { return cmp(y) < 0; }
    bool operator==(const HiResTime &y) const { return cmp(y) == 0; }
  };

}

#endif // HYPERTABLE_HIRES_TIME_H
