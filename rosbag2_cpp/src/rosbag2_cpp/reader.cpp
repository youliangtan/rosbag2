// Copyright 2018, Bosch Software Innovations GmbH.
// Copyright 2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rosbag2_cpp/reader.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "rosbag2_cpp/info.hpp"
#include "rosbag2_cpp/reader_interfaces/base_reader_interface.hpp"

namespace rosbag2_cpp
{

Reader::Reader(ReaderImplVector reader_impls)
{
  for (auto r: reader_impls) {
    readers_.push_back(ChildReader({r, std::nullopt}));
  }
}

Reader::Reader(ReaderImplPtr reader_impl)
{
  readers_.push_back(ChildReader({reader_impl, std::nullopt}));
}

Reader::~Reader()
{
  close();
}

void Reader::open(const std::string & uri)
{
  rosbag2_storage::StorageOptions storage_options;
  storage_options.uri = uri;
  storage_options.storage_id = "sqlite3";

  rosbag2_cpp::ConverterOptions converter_options{};
  return open(
    StorageOptionsVector({std::move(storage_options)}),
    ConverterOptionsVector({std::move(converter_options)}));
}

void Reader::open(
  const rosbag2_storage::StorageOptions & storage_options,
  const ConverterOptions & converter_options)
{
  StorageOptionsVector storage_options_vector;
  ConverterOptionsVector converter_options_vector;
  for (size_t ii = 0; ii < readers_.size(); ++ii) {
    storage_options_vector.push_back(storage_options);
    converter_options_vector.push_back(converter_options);
  }
  return open(
    std::move(storage_options_vector),
    std::move(converter_options_vector));
}

void Reader::open(
  const StorageOptionsVector & storage_options,
  const ConverterOptionsVector & converter_options)
{
  // TODO(geoff): Change these to rcutils arg checks
  assert(storage_options.size() == converter_options.size());
  assert(storage_options.size() == readers_.size());

  // Open each bag and populate the cache of next messages
  for (size_t ii = 0; ii < readers_.size(); ++ii) {
    readers_[ii].reader->open(storage_options[ii], converter_options[ii]);
    if (readers_[ii].reader->has_next()) {
      readers_[ii].next_message = readers_[ii].reader->read_next();
    }
  }
}

void Reader::close()
{
  for (auto& r: readers_) {
    r.reader->close();
  }
}

bool Reader::has_next()
{
  bool has_next = false;
  for (const auto& r: readers_) {
    if (r.next_message != std::nullopt) {
      has_next = true;
      break;
    }
  }
  return has_next;
}

std::shared_ptr<rosbag2_storage::SerializedBagMessage> Reader::read_next()
{
  // Find the index of the reader with the earliest message
  size_t earliest_index = std::numeric_limits<size_t>::max();
  size_t current_index = 0;
  rcutils_time_point_value_t earliest_time = std::numeric_limits<rcutils_time_point_value_t>::max();

  for (const auto& r: readers_) {
    if (r.next_message != std::nullopt) {
      if (r.next_message.value()->time_stamp < earliest_time) {
        earliest_time = r.next_message.value()->time_stamp;
        earliest_index = current_index;
      }
    }

    ++current_index;
  }

  if (earliest_index == std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("No next message available");
  } else {
    auto result = readers_[earliest_index].next_message.value();
    if (readers_[earliest_index].reader->has_next()) {
      readers_[earliest_index].next_message = readers_[earliest_index].reader->read_next();
    } else {
      readers_[earliest_index].next_message = std::nullopt;
    }
    return result;
  }
}

const rosbag2_storage::BagMetadata & Reader::get_metadata() const
{
  return readers_[0].reader->get_metadata();
}

std::vector<rosbag2_storage::TopicMetadata> Reader::get_all_topics_and_types() const
{
  return readers_[0].reader->get_all_topics_and_types();
}

void Reader::set_filter(const rosbag2_storage::StorageFilter & storage_filter)
{
  for (auto& r: readers_) {
    r.reader->set_filter(storage_filter);
  }
}

void Reader::reset_filter()
{
  for (auto& r: readers_) {
    r.reader->reset_filter();
  }
}

}  // namespace rosbag2_cpp
