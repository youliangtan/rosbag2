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

#ifndef ROSBAG2_CPP__READERS__MERGED_READER_HPP_
#define ROSBAG2_CPP__READERS__MERGED_READER_HPP_

#include <memory>
#include <string>
#include <vector>

#include "rosbag2_cpp/converter.hpp"
#include "rosbag2_cpp/reader_interfaces/base_reader_interface.hpp"
#include "rosbag2_cpp/reader_interfaces/multiple_bag_opener_interface.hpp"
#include "rosbag2_cpp/reader_interfaces/filtered_reader_interface.hpp"
#include "rosbag2_cpp/visibility_control.hpp"

namespace rosbag2_cpp
{
namespace readers
{

class ROSBAG2_CPP_PUBLIC AggregateReader
  : public ::rosbag2_cpp::reader_interfaces::BaseReaderInterface
{
public:
  using ReaderVector = std::vector<std::shared_ptr<reader_interfaces::BaseReaderInterface>>;

  AggregateReader(ReaderVector &child_readers);

  virtual ~AggregateReader();

  bool has_next() override;

  std::shared_ptr<rosbag2_storage::SerializedBagMessage> read_next() override;

  std::vector<rosbag2_storage::TopicMetadata> get_all_topics_and_types() const override;

private:
  ReaderVector child_readers_;
};

}  // namespace readers
}  // namespace rosbag2_cpp

#endif  // ROSBAG2_CPP__READERS__MERGED_READER_HPP_
