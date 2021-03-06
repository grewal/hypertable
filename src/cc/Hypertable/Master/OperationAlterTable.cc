/*
 * Copyright (C) 2007-2013 Hypertable, Inc.
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

/// @file
/// Definitions for OperationAlterTable.
/// This file contains definitions for OperationAlterTable, an Operation class
/// for carrying out an ALTER TABLE operation.

#include <Common/Compat.h>
#include "OperationAlterTable.h"

#include <Hypertable/Master/DispatchHandlerOperationAlterTable.h>
#include <Hypertable/Master/OperationCreateTable.h>
#include <Hypertable/Master/OperationDropTable.h>
#include <Hypertable/Master/OperationToggleTableMaintenance.h>
#include <Hypertable/Master/ReferenceManager.h>
#include <Hypertable/Master/Utility.h>

#include <Hyperspace/Session.h>

#include <Hypertable/Lib/Key.h>
#include <Hypertable/Lib/TableParts.h>

#include <Common/Error.h>
#include <Common/FailureInducer.h>
#include <Common/ScopeGuard.h>
#include <Common/Serialization.h>
#include <Common/Time.h>

#include <boost/algorithm/string.hpp>

#include <poll.h>

using namespace Hypertable;
using namespace Hyperspace;

OperationAlterTable::OperationAlterTable(ContextPtr &context,
                                         const MetaLog::EntityHeader &header_)
  : Operation(context, header_) {
}

OperationAlterTable::OperationAlterTable(ContextPtr &context, EventPtr &event)
  : Operation(context, event, MetaLog::EntityType::OPERATION_ALTER_TABLE) {
  const uint8_t *ptr = event->payload;
  size_t remaining = event->payload_len;
  m_decode_version = 4;
  decode_request(&ptr, &remaining);
  initialize_dependencies();
}

void OperationAlterTable::initialize_dependencies() {
  boost::trim_if(m_name, boost::is_any_of("/ "));
  m_name = String("/") + m_name;
  m_exclusivities.insert(m_name);
  add_dependency(Dependency::INIT);
}

void OperationAlterTable::execute() {
  String filename;
  bool is_namespace;
  StringSet servers;
  DispatchHandlerOperationPtr op_handler;
  TableIdentifier table;
  int32_t state = get_state();
  DependencySet dependencies;
  Operation *op;
  SchemaPtr original_schema;
  SchemaPtr alter_schema;

  HT_INFOF("Entering AlterTable-%lld(%s) state=%s",
           (Lld)header.id, m_name.c_str(), OperationState::get_text(state));

  switch (state) {

  case OperationState::INITIAL:
    // Check to see if namespace exists
    if(m_context->namemap->name_to_id(m_name, m_id, &is_namespace)) {
      if (is_namespace) {
        complete_error(Error::INVALID_ARGUMENT, format("%s is a namespace", m_name.c_str()));
        break;
      }
      set_state(OperationState::VALIDATE_SCHEMA);
      m_context->mml_writer->record_state(this);
    }
    else {
      complete_error(Error::TABLE_NOT_FOUND, m_name);
      break;
    }
    HT_MAYBE_FAIL("alter-table-INITIAL");

  case OperationState::VALIDATE_SCHEMA:
    if (!get_schemas(original_schema, alter_schema))
      break;
    if (!m_force) {
      try {
        if (original_schema->get_generation() != alter_schema->get_generation())
          HT_THROWF(Error::MASTER_SCHEMA_GENERATION_MISMATCH,
                    "Expected altered schema generation %lld to match original"
                    " %lld", (Lld)alter_schema->get_generation(),
                    (Lld)original_schema->get_generation());

        if (!alter_schema->clear_generation_if_changed(*original_schema)) {
          // No change, therefore nothing to do
          complete_ok();
          break;
        }

        // Assign new generation number
        int64_t generation = get_ts64();
        alter_schema->update_generation(generation);
      }
      catch (Exception &e) {
        if (e.code() != Error::MASTER_SCHEMA_GENERATION_MISMATCH)
          HT_ERROR_OUT << e << HT_END;
        complete_error(e);
        break;
      }
    }
    m_schema = alter_schema->render_xml(true);
    {
      ScopedLock lock(m_mutex);
      m_dependencies.clear();
      m_dependencies.insert(Dependency::METADATA);
      m_state = OperationState::CREATE_INDICES;
    }
    m_parts = get_create_index_parts(original_schema, alter_schema);
    m_context->mml_writer->record_state(this);

  case OperationState::CREATE_INDICES:
    if (m_parts.value_index() || m_parts.qualifier_index()) {
      op = new OperationCreateTable(m_context, m_name, m_schema, m_parts);
      stage_subop(op);
    }
    set_state(OperationState::SCAN_METADATA);
    record_state();
    break;

  case OperationState::SCAN_METADATA:
    if (!validate_subops())
      break;
    servers.clear();
    Utility::get_table_server_set(m_context, m_id, "", servers);
    {
      ScopedLock lock(m_mutex);
      m_servers.clear();
      m_dependencies.clear();
      for (StringSet::iterator iter=servers.begin(); iter!=servers.end(); ++iter) {
        if (m_completed.count(*iter) == 0) {
          m_dependencies.insert(*iter);
          m_servers.insert(*iter);
        }
      }
      m_state = OperationState::ISSUE_REQUESTS;
    }
    record_state();
    break;

  case OperationState::ISSUE_REQUESTS:
    table.id = m_id.c_str();
    table.generation = 0;
    op_handler = new DispatchHandlerOperationAlterTable(m_context, table, m_schema);
    op_handler->start(m_servers);
    if (!op_handler->wait_for_completion()) {
      std::set<DispatchHandlerOperation::Result> results;
      op_handler->get_results(results);
      foreach_ht (const DispatchHandlerOperation::Result &result, results) {
        if (result.error == Error::OK ||
            result.error == Error::TABLE_NOT_FOUND) {
          ScopedLock lock(m_mutex);
          m_completed.insert(result.location);
        }
        else
          HT_WARNF("Alter table error at %s - %s (%s)", result.location.c_str(),
                   Error::get_text(result.error), result.msg.c_str());
      }
      {
        ScopedLock lock(m_mutex);
        m_servers.clear();
        m_dependencies.clear();
        m_dependencies.insert(Dependency::METADATA);
        m_state = OperationState::SCAN_METADATA;
      }
      m_context->mml_writer->record_state(this);
      // Sleep a little bit to prevent busy wait
      poll(0, 0, 5000);
      return;
    }
    set_state(OperationState::UPDATE_HYPERSPACE);
    m_context->mml_writer->record_state(this);

  case OperationState::UPDATE_HYPERSPACE:
    if (!get_schemas(original_schema, alter_schema))
      break;
    m_parts = get_drop_index_parts(original_schema, alter_schema);
    {
      filename = m_context->toplevel_dir + "/tables/" + m_id;
      m_context->hyperspace->attr_set(filename, OPEN_FLAG_READ|OPEN_FLAG_WRITE|OPEN_FLAG_LOCK_EXCLUSIVE,
                                      "schema", m_schema.c_str(), m_schema.length());
    }
    if (m_parts.value_index() || m_parts.qualifier_index()) {
      set_state(OperationState::SUSPEND_TABLE_MAINTENANCE);
      m_context->mml_writer->record_state(this);
    }
    else
      complete_ok();
    break;

  case OperationState::SUSPEND_TABLE_MAINTENANCE:
    op = new OperationToggleTableMaintenance(m_context, m_name,
                                             TableMaintenance::OFF);
    stage_subop(op);
    set_state(OperationState::DROP_INDICES);
    record_state();
    break;

  case OperationState::DROP_INDICES:
    if (!validate_subops())
      break;
    op = new OperationDropTable(m_context, m_name, true, m_parts);
    stage_subop(op);
    set_state(OperationState::RESUME_TABLE_MAINTENANCE);
    record_state();
    break;

  case OperationState::RESUME_TABLE_MAINTENANCE:
    if (!validate_subops())
      break;
    op = new OperationToggleTableMaintenance(m_context, m_name,
                                             TableMaintenance::ON);
    stage_subop(op);
    set_state(OperationState::FINALIZE);
    record_state();
    break;

  case OperationState::FINALIZE:
    if (!validate_subops())
      break;
    complete_ok();
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
  }

  HT_INFOF("Leaving AlterTable-%lld (%s) state=%s", (Lld)header.id,
           m_name.c_str(), OperationState::get_text(get_state()));
}


void OperationAlterTable::display_state(std::ostream &os) {
  os << " name=" << m_name << " id=" << m_id << " ";
}

#define OPERATION_ALTER_TABLE_VERSION 4

uint16_t OperationAlterTable::encoding_version() const {
  return OPERATION_ALTER_TABLE_VERSION;
}

size_t OperationAlterTable::encoded_state_length() const {
  size_t length = Serialization::encoded_length_vstr(m_name) +
    Serialization::encoded_length_vstr(m_schema) + 1 +
    Serialization::encoded_length_vstr(m_id);
  length += 4;
  foreach_ht (const String &location, m_completed)
    length += Serialization::encoded_length_vstr(location);
  length += 4;
  foreach_ht (const String &location, m_servers)
    length += Serialization::encoded_length_vstr(location);
  length += m_parts.encoded_length();
  return length;
}

void OperationAlterTable::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_name);
  Serialization::encode_vstr(bufp, m_schema);
  Serialization::encode_bool(bufp, m_force);
  Serialization::encode_vstr(bufp, m_id);
  Serialization::encode_i32(bufp, m_completed.size());
  foreach_ht (const String &location, m_completed)
    Serialization::encode_vstr(bufp, location);
  Serialization::encode_i32(bufp, m_servers.size());
  foreach_ht (const String &location, m_servers)
    Serialization::encode_vstr(bufp, location);
  m_parts.encode(bufp);
}

void OperationAlterTable::decode_state(const uint8_t **bufp, size_t *remainp) {
  decode_request(bufp, remainp);
  m_id = Serialization::decode_vstr(bufp, remainp);
  size_t length = Serialization::decode_i32(bufp, remainp);
  for (size_t i=0; i<length; i++)
    m_completed.insert( Serialization::decode_vstr(bufp, remainp) );
  if (m_decode_version >= 2) {
    length = Serialization::decode_i32(bufp, remainp);
    for (size_t i=0; i<length; i++)
      m_servers.insert( Serialization::decode_vstr(bufp, remainp) );
    if (m_decode_version >= 3)
      m_parts.decode(bufp, remainp);
  }
}

void OperationAlterTable::decode_request(const uint8_t **bufp, size_t *remainp) {
  m_name = Serialization::decode_vstr(bufp, remainp);
  m_schema = Serialization::decode_vstr(bufp, remainp);
  if (m_decode_version >= 4)
    m_force = Serialization::decode_bool(bufp, remainp);
}

const String OperationAlterTable::name() {
  return "OperationAlterTable";
}

const String OperationAlterTable::label() {
  return String("AlterTable ") + m_name;
}

bool OperationAlterTable::get_schemas(SchemaPtr &original_schema,
                                      SchemaPtr &alter_schema) {
  if (!original_schema || !alter_schema) {
    try {
      DynamicBuffer value_buf;
      string filename = m_context->toplevel_dir + "/tables/" + m_id;
      m_context->hyperspace->attr_get(filename, "schema", value_buf);
      original_schema = Schema::new_instance((const char *)value_buf.base);
      alter_schema = Schema::new_instance(m_schema);
    }
    catch (Exception &e) {
      complete_error(e);
      return false;
    }
  }
  return true;
}

TableParts OperationAlterTable::get_drop_index_parts(SchemaPtr &original_schema,
                                                     SchemaPtr &alter_schema) {
  int8_t parts {};
  TableParts original_parts = original_schema->get_table_parts();
  TableParts alter_parts = alter_schema->get_table_parts();
  if (original_parts.value_index() && !alter_parts.value_index())
    parts |= TableParts::VALUE_INDEX;
  if (original_parts.qualifier_index() && !alter_parts.qualifier_index())
    parts |= TableParts::QUALIFIER_INDEX;
  return TableParts(parts);
}

TableParts OperationAlterTable::get_create_index_parts(SchemaPtr &original_schema,
                                                     SchemaPtr &alter_schema) {
  int8_t parts {};
  TableParts original_parts = original_schema->get_table_parts();
  TableParts alter_parts = alter_schema->get_table_parts();
  if (!original_parts.value_index() && alter_parts.value_index())
    parts |= TableParts::VALUE_INDEX;
  if (!original_parts.qualifier_index() && alter_parts.qualifier_index())
    parts |= TableParts::QUALIFIER_INDEX;
  return TableParts(parts);
}
