/*
 * Copyright (C) 2021 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include "rosbag2_cpp/readers/aggregate_reader.hpp"

#include <algorithm>
#include <stdexcept>

namespace rosbag2_cpp
{
namespace readers
{

AggregateReader::AggregateReader(ReaderVector && child_readers)
{
  for (auto&& r: child_readers) {
    readers_.emplace_back(ChildReader{std::move(r), std::nullopt});
  }
}

AggregateReader::~AggregateReader()
{
  // Do nothing (child readers will automatically close when destructed)
}

bool AggregateReader::has_next()
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

std::shared_ptr<rosbag2_storage::SerializedBagMessage> AggregateReader::read_next()
{
  std::optional<size_t> earliest_index = std::nullopt;
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

  if (earliest_index == std::nullopt) {
    throw std::runtime_error("No next message available");
  } else {
    auto result = readers_[earliest_index.value()].next_message.value();
    if (readers_[earliest_index.value()].reader->has_next()) {
      readers_[earliest_index.value()].next_message =
        readers_[earliest_index.value()].reader->read_next();
    } else {
      readers_[earliest_index.value()].next_message = std::nullopt;
    }
    return result;
  }
}

std::vector<rosbag2_storage::TopicMetadata> AggregateReader::get_all_topics_and_types() const
{
  std::vector<rosbag2_storage::TopicMetadata> result;
  for (const auto& r: readers_) {
    auto topic_metadata = r.reader->get_all_topics_and_types();
    for (auto&& t: topic_metadata) {
      auto existing_topic = std::find_if(
        result.begin(),
        result.end(),
        [&t](auto topic){
          return topic == t;
        });
      if (existing_topic == result.end()) {
        result.emplace_back(std::move(t));
      }
      // Ignore already-listed topics
    }
  }
  return result;
}

}  // namespace readers
}  // namespace rosbag2_cpp
