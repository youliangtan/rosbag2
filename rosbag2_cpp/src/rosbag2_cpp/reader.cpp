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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rosbag2_cpp/info.hpp"
#include "rosbag2_cpp/reader_interfaces/base_reader_interface.hpp"

namespace rosbag2_cpp
{

Reader::Reader(std::shared_ptr<reader_interfaces::BaseReaderInterface> reader_impl)
: reader_impl_(std::move(reader_impl))
{}

Reader::Reader(const std::string & uri)
{
  rosbag2_storage::StorageOptions storage_options;
  storage_options.uri = uri;
  storage_options.storage_id = "sqlite3";

  rosbag2_cpp::ConverterOptions converter_options{};
  reader_impl_ = std::make_shared<readers::SequentialReader>(storage_options, converter_options);
}

Reader::~Reader()
{
  // Do nothing; the held reader will close itself when it destructs
}

void Reader::reopen()
{
  reader_impl_->reopen();
}

bool Reader::has_next()
{
  return reader_impl_->has_next();
}

std::shared_ptr<rosbag2_storage::SerializedBagMessage> Reader::read_next()
{
  return reader_impl_->read_next();
}

const rosbag2_storage::BagMetadata & Reader::get_metadata() const
{
  auto impl = std::dynamic_pointer_cast<rosbag2_cpp::reader_interfaces::MetadataReaderInterface>(
      reader_impl_);
  if (!impl) {
    throw std::runtime_error("Cannot read metadata from reader that does not support it");
  }
  return impl->get_metadata();
}

std::vector<rosbag2_storage::TopicMetadata> Reader::get_all_topics_and_types() const
{
  return reader_impl_->get_all_topics_and_types();
}

void Reader::set_filter(const rosbag2_storage::StorageFilter & storage_filter)
{
  auto impl = std::dynamic_pointer_cast<rosbag2_cpp::reader_interfaces::FilteredReaderInterface>(
      reader_impl_);
  if (!impl) {
    throw std::runtime_error("Cannot set a filter on a reader that does not support it");
  }
  impl->set_filter(storage_filter);
}

void Reader::reset_filter()
{
  auto impl = std::dynamic_pointer_cast<rosbag2_cpp::reader_interfaces::FilteredReaderInterface>(
      reader_impl_);
  if (!impl) {
    throw std::runtime_error("Cannot reset filters on a reader that does not support it");
  }
  impl->reset_filter();
}

}  // namespace rosbag2_cpp
