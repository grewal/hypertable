/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
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

#ifndef HYPERTABLE_RESPONSECALLBACKPHANTOMUPDATE_H
#define HYPERTABLE_RESPONSECALLBACKPHANTOMUPDATE_H

#include "Common/Error.h"


#include "AsyncComm/CommBuf.h"
#include "AsyncComm/ResponseCallback.h"

#include "Hypertable/Lib/Types.h"

namespace Hypertable {

  class ResponseCallbackPhantomUpdate : public ResponseCallback {
  public:
    ResponseCallbackPhantomUpdate(Comm *comm, EventPtr &event_ptr)
      : ResponseCallback(comm, event_ptr) {
    }
    
    void initialize(const QualifiedRangeSpec &range, uint32_t fragment) {
      m_range=range;
      m_fragment=fragment;
    }

    int response();
    virtual int error(int error, const String &msg);
    virtual int response_ok();

  private:
    QualifiedRangeSpec m_range;
    uint32_t m_fragment;
  };

}


#endif // HYPERTABLE_RESPONSECALLBACKPHANTOMUPDATE_H
