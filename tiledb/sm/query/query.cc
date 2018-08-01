/**
 * @file   query.cc
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2017-2018 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * This file implements class Query.
 */

#include "tiledb/sm/query/query.h"
#include "tiledb/sm/misc/logger.h"
#include "tiledb/sm/misc/stats.h"

#include <cassert>
#include <iostream>

namespace tiledb {
namespace sm {

/* ****************************** */
/*   CONSTRUCTORS & DESTRUCTORS   */
/* ****************************** */

Query::Query(
    StorageManager* storage_manager,
    QueryType type,
    const ArraySchema* array_schema,
    const std::vector<FragmentMetadata*>& fragment_metadata)
    : type_(type) {
  callback_ = nullptr;
  callback_data_ = nullptr;
  layout_ = Layout::ROW_MAJOR;
  status_ = QueryStatus::UNINITIALIZED;
  set_storage_manager(storage_manager);
  set_array_schema(array_schema);
  set_fragment_metadata(fragment_metadata);
}

Query::Query(const Query& query) {
  type_ = query.type();
  callback_ = query.callback_;
  callback_data_ = query.callback_data_;
  set_layout(query.layout());
  status_ = query.status();
  set_storage_manager(query.storage_manager());
  set_array_schema(query.array_schema());
  set_fragment_metadata(query.fragment_metadata());
}

Query::~Query() = default;

/* ****************************** */
/*               API              */
/* ****************************** */

const ArraySchema* Query::array_schema() const {
  if (type_ == QueryType::WRITE)
    return writer_.array_schema();
  return reader_.array_schema();
}

std::vector<std::string> Query::attributes() const {
  if (type_ == QueryType::WRITE)
    return writer_.attributes();
  return reader_.attributes();
}

std::unordered_map<std::string, AttributeBuffer> Query::attribute_buffers()
    const {
  if (type_ == QueryType::WRITE)
    return writer_.attribute_buffers();
  return reader_.attribute_buffers();
}

Status Query::finalize() {
  if (status_ == QueryStatus::UNINITIALIZED)
    return Status::Ok();

  RETURN_NOT_OK(writer_.finalize());
  status_ = QueryStatus::COMPLETED;
  return Status::Ok();
}

std::vector<FragmentMetadata*> Query::fragment_metadata() const {
  if (type_ == QueryType::READ)
    return reader_.fragment_metadata();
  return {};
}

unsigned Query::fragment_num() const {
  if (type_ == QueryType::WRITE)
    return 0;
  return reader_.fragment_num();
}

std::vector<URI> Query::fragment_uris() const {
  std::vector<URI> uris;
  if (type_ == QueryType::WRITE)
    return uris;
  return reader_.fragment_uris();
}

bool Query::has_results() const {
  if (status_ == QueryStatus::UNINITIALIZED || type_ == QueryType::WRITE)
    return false;
  return !reader_.no_results();
}

Status Query::init() {
  // Only if the query has not been initialized before
  if (status_ == QueryStatus::UNINITIALIZED) {
    if (type_ == QueryType::READ) {
      RETURN_NOT_OK(reader_.init());
    } else {  // Write
      RETURN_NOT_OK(writer_.init());
    }
  }

  status_ = QueryStatus::INPROGRESS;

  return Status::Ok();
}

URI Query::last_fragment_uri() const {
  if (type_ == QueryType::WRITE)
    return URI();
  return reader_.last_fragment_uri();
}

Layout Query::layout() const {
  return layout_;
}

Status Query::cancel() {
  status_ = QueryStatus::FAILED;
  return Status::Ok();
}

Status Query::check_var_attr_offsets(
    const uint64_t* buffer_off,
    const uint64_t* buffer_off_size,
    const uint64_t* buffer_val_size) {
  if (buffer_off == nullptr || buffer_off_size == nullptr ||
      buffer_val_size == nullptr)
    return LOG_STATUS(Status::QueryError("Cannot use null offset buffers."));

  auto num_offsets = *buffer_off_size / sizeof(uint64_t);
  if (num_offsets == 0)
    return Status::Ok();

  uint64_t prev_offset = buffer_off[0];
  if (prev_offset >= *buffer_val_size)
    return LOG_STATUS(Status::QueryError(
        "Invalid offsets; offset " + std::to_string(prev_offset) +
        " specified for buffer of size " + std::to_string(*buffer_val_size)));

  for (uint64_t i = 1; i < num_offsets; i++) {
    if (buffer_off[i] <= prev_offset)
      return LOG_STATUS(
          Status::QueryError("Invalid offsets; offsets must be given in "
                             "strictly ascending order."));

    if (buffer_off[i] >= *buffer_val_size)
      return LOG_STATUS(Status::QueryError(
          "Invalid offsets; offset " + std::to_string(buffer_off[i]) +
          " specified for buffer of size " + std::to_string(*buffer_val_size)));

    prev_offset = buffer_off[i];
  }

  return Status::Ok();
}

Status Query::copy_buffers(const Query& query) {
  STATS_FUNC_IN(serialization_copy_buffers);
  auto buffers = query.attribute_buffers();
  auto existing_buffers = attribute_buffers();
  // Loop through each buffer from query we are copying
  for (auto& buffer : buffers) {
    // Check to see if the query already has said buffer
    auto existing_buffer = existing_buffers.find(buffer.first);
    if (existing_buffer != existing_buffers.end()) {
      // If buffer sizes are different error
      if (*existing_buffer->second.buffer_size_ !=
          *buffer.second.buffer_size_) {
        return Status::QueryError(
            "Existing buffer in query object is different size (" +
            std::to_string(*existing_buffer->second.buffer_size_) +
            ") vs new query object buffer size (" +
            std::to_string(*buffer.second.buffer_size_) + ")");
      }
      memcpy(
          existing_buffer->second.buffer_,
          buffer.second.buffer_,
          *buffer.second.buffer_size_);
      free(buffer.second.buffer_);
      free(buffer.second.buffer_size_);
      if (buffer.second.buffer_var_ != nullptr) {
        if (*existing_buffer->second.buffer_var_size_ !=
            *buffer.second.buffer_var_size_) {
          return Status::QueryError(
              "Existing buffer_var_ in query object is different size (" +
              std::to_string(*existing_buffer->second.buffer_var_size_) +
              ") vs new query object buffer_var size (" +
              std::to_string(*buffer.second.buffer_var_size_) + ")");
        }
        memcpy(
            existing_buffer->second.buffer_var_,
            buffer.second.buffer_var_,
            *buffer.second.buffer_var_size_);
        free(buffer.second.buffer_var_);
        free(buffer.second.buffer_var_size_);
      }
    } else {
      if (buffer.second.buffer_var_ != nullptr) {
        set_buffer(
            buffer.first,
            static_cast<uint64_t*>(buffer.second.buffer_var_),
            buffer.second.buffer_var_size_,
            buffer.second.buffer_,
            buffer.second.buffer_size_);
      } else {
        set_buffer(
            buffer.first, buffer.second.buffer_, buffer.second.buffer_size_);
      }
    }
  }
  return Status::Ok();
  STATS_FUNC_OUT(serialization_copy_buffers);
}

Status Query::copy_json_wip(const Query& query) {
  STATS_FUNC_IN(serialization_copy_json_wip);
  type_ = query.type();
  status_ = query.status();
  set_layout(query.layout());
  // set_array_schema(query.array_schema());
  switch (array_schema()->domain()->type()) {
    case tiledb::sm::Datatype::INT8:
      set_subarray(query.subarray<int8_t>().data());
      break;

    case tiledb::sm::Datatype::UINT8:
      set_subarray(query.subarray<uint8_t>().data());
      break;
    case tiledb::sm::Datatype::INT16:
      set_subarray(query.subarray<int16_t>().data());
      break;
    case tiledb::sm::Datatype::UINT16:
      set_subarray(query.subarray<uint16_t>().data());
      break;
    case tiledb::sm::Datatype::INT32:
      set_subarray(query.subarray<int32_t>().data());
      break;
    case tiledb::sm::Datatype::UINT32:
      set_subarray(query.subarray<uint32_t>().data());
      break;
    case tiledb::sm::Datatype::INT64:
      set_subarray(query.subarray<int64_t>().data());
      break;

    case tiledb::sm::Datatype::UINT64:
      set_subarray(query.subarray<uint64_t>().data());
      break;
    case tiledb::sm::Datatype::FLOAT32:
      set_subarray(query.subarray<float>().data());
      break;
    case tiledb::sm::Datatype::FLOAT64:
      set_subarray(query.subarray<double>().data());
      break;
    case tiledb::sm::Datatype::CHAR:
    case tiledb::sm::Datatype::STRING_ASCII:
    case tiledb::sm::Datatype::STRING_UTF8:
    case tiledb::sm::Datatype::STRING_UTF16:
    case tiledb::sm::Datatype::STRING_UTF32:
    case tiledb::sm::Datatype::STRING_UCS2:
    case tiledb::sm::Datatype::STRING_UCS4:
    case tiledb::sm::Datatype::ANY:
      // Not supported domain type
      assert(false);
      break;
  }

  return copy_buffers(query);
  STATS_FUNC_OUT(serialization_copy_json_wip);
}

Status Query::process() {
  if (status_ == QueryStatus::UNINITIALIZED)
    return LOG_STATUS(
        Status::QueryError("Cannot process query; Query is not initialized"));
  status_ = QueryStatus::INPROGRESS;

  // Process query
  Status st = Status::Ok();
  if (type_ == QueryType::READ)
    st = reader_.read();
  else  // WRITE MODE
    st = writer_.write();

  // Handle error
  if (!st.ok()) {
    status_ = QueryStatus::FAILED;
    return st;
  }

  // Check if the query is complete
  bool completed = (type_ == QueryType::WRITE) ? true : !reader_.incomplete();

  // Handle callback and status
  if (completed) {
    if (callback_ != nullptr)
      callback_(callback_data_);
    status_ = QueryStatus::COMPLETED;
  } else {  // Incomplete
    status_ = QueryStatus::INCOMPLETE;
  }

  return Status::Ok();
}

Status Query::set_buffer(
    const std::string& attribute, void* buffer, uint64_t* buffer_size) {
  if (type_ == QueryType::WRITE)
    return writer_.set_buffer(attribute, buffer, buffer_size);
  return reader_.set_buffer(attribute, buffer, buffer_size);
}

Status Query::set_buffer(
    const std::string& attribute,
    uint64_t* buffer_off,
    uint64_t* buffer_off_size,
    void* buffer_val,
    uint64_t* buffer_val_size) {
  if (type_ == QueryType::WRITE)
    return writer_.set_buffer(
        attribute, buffer_off, buffer_off_size, buffer_val, buffer_val_size);
  return reader_.set_buffer(
      attribute, buffer_off, buffer_off_size, buffer_val, buffer_val_size);
}

void Query::set_callback(
    const std::function<void(void*)>& callback, void* callback_data) {
  callback_ = callback;
  callback_data_ = callback_data;
}

void Query::set_fragment_uri(const URI& fragment_uri) {
  if (type_ == QueryType::WRITE)
    writer_.set_fragment_uri(fragment_uri);
  // Non-applicable to reads
}

void Query::set_writer(nlohmann::json j) {
  STATS_FUNC_VOID_IN(serialization_query_set_writer);
  const ArraySchema* array_schema = writer_.array_schema();
  writer_ = Writer(j);
  writer_.set_array_schema(array_schema);
  STATS_FUNC_VOID_OUT(serialization_query_set_writer);
}

Status Query::set_layout(Layout layout) {
  layout_ = layout;
  if (type_ == QueryType::WRITE)
    return writer_.set_layout(layout);
  return reader_.set_layout(layout);
}

void Query::set_status(QueryStatus status) {
  status_ = status;
}

void Query::set_storage_manager(StorageManager* storage_manager) {
  if (type_ == QueryType::WRITE)
    writer_.set_storage_manager(storage_manager);
  else
    reader_.set_storage_manager(storage_manager);
}

Status Query::set_subarray(const void* subarray) {
  RETURN_NOT_OK(check_subarray_bounds(subarray));
  if (type_ == QueryType::WRITE) {
    RETURN_NOT_OK(writer_.set_subarray(subarray));
  } else {  // READ
    RETURN_NOT_OK(reader_.set_subarray(subarray));
  }

  status_ = QueryStatus::UNINITIALIZED;

  return Status::Ok();
}

QueryStatus Query::status() const {
  return status_;
}

StorageManager* Query::storage_manager() const {
  if (type_ == QueryType::WRITE)
    return writer_.storage_manager();
  return reader_.storage_manager();
}

QueryType Query::type() const {
  return type_;
}

nlohmann::json Query::writer_to_json() const {
  return writer_.to_json();
}

/* ****************************** */
/*          PRIVATE METHODS       */
/* ****************************** */

Status Query::check_subarray_bounds(const void* subarray) const {
  if (subarray == nullptr)
    return Status::Ok();

  auto array_schema = this->array_schema();
  if (array_schema == nullptr)
    return LOG_STATUS(
        Status::QueryError("Cannot check subarray; Array schema not set"));

  switch (array_schema->domain()->type()) {
    case Datatype::INT8:
      return check_subarray_bounds<int8_t>(
          static_cast<const int8_t*>(subarray));
    case Datatype::UINT8:
      return check_subarray_bounds<uint8_t>(
          static_cast<const uint8_t*>(subarray));
    case Datatype::INT16:
      return check_subarray_bounds<int16_t>(
          static_cast<const int16_t*>(subarray));
    case Datatype::UINT16:
      return check_subarray_bounds<uint16_t>(
          static_cast<const uint16_t*>(subarray));
    case Datatype::INT32:
      return check_subarray_bounds<int32_t>(
          static_cast<const int32_t*>(subarray));
    case Datatype::UINT32:
      return check_subarray_bounds<uint32_t>(
          static_cast<const uint32_t*>(subarray));
    case Datatype::INT64:
      return check_subarray_bounds<int64_t>(
          static_cast<const int64_t*>(subarray));
    case Datatype::UINT64:
      return check_subarray_bounds<uint64_t>(
          static_cast<const uint64_t*>(subarray));
    case Datatype::FLOAT32:
      return check_subarray_bounds<float>(static_cast<const float*>(subarray));
    case Datatype::FLOAT64:
      return check_subarray_bounds<double>(
          static_cast<const double*>(subarray));
    case Datatype::CHAR:
    case Datatype::STRING_ASCII:
    case Datatype::STRING_UTF8:
    case Datatype::STRING_UTF16:
    case Datatype::STRING_UTF32:
    case Datatype::STRING_UCS2:
    case Datatype::STRING_UCS4:
    case Datatype::ANY:
      // Not supported domain type
      assert(false);
      break;
  }

  return Status::Ok();
}

template <class T>
Status Query::check_subarray_bounds(const T* subarray) const {
  // Check subarray bounds
  auto array_schema = this->array_schema();
  auto domain = array_schema->domain();
  auto dim_num = domain->dim_num();
  for (unsigned int i = 0; i < dim_num; ++i) {
    auto dim_domain = static_cast<const T*>(domain->dimension(i)->domain());
    if (subarray[2 * i] < dim_domain[0] || subarray[2 * i + 1] > dim_domain[1])
      return LOG_STATUS(Status::QueryError("Subarray out of bounds"));
    if (subarray[2 * i] > subarray[2 * i + 1])
      return LOG_STATUS(Status::QueryError(
          "Subarray lower bound is larger than upper bound"));
  }

  return Status::Ok();
}

void Query::set_array_schema(const ArraySchema* array_schema) {
  if (type_ == QueryType::READ)
    reader_.set_array_schema(array_schema);
  else
    writer_.set_array_schema(array_schema);
}

void Query::set_fragment_metadata(
    const std::vector<FragmentMetadata*>& fragment_metadata) {
  if (type_ == QueryType::READ)
    reader_.set_fragment_metadata(fragment_metadata);
}

}  // namespace sm
}  // namespace tiledb
