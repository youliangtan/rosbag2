// Copyright 2018, Bosch Software Innovations GmbH.
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

#include "rosbag2_cpp/writers/sequential_writer.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rcpputils/filesystem_helper.hpp"

#include "rosbag2_cpp/info.hpp"

#include "rosbag2_storage/storage_options.hpp"

namespace rosbag2_cpp
{
namespace writers
{

namespace
{
std::string strip_parent_path(const std::string & relative_path)
{
  return rcpputils::fs::path(relative_path).filename().string();
}

rosbag2_cpp::cache::CacheConsumer::consume_callback_function_t make_callback(
  std::shared_ptr<rosbag2_storage::storage_interfaces::ReadWriteInterface> storage_interface,
  std::unordered_map<std::string, rosbag2_storage::TopicInformation> & topics_info_map,
  std::mutex & topics_mutex)
{
  return [callback_interface = storage_interface, &topics_info_map, &topics_mutex](
    const std::vector<std::shared_ptr<const rosbag2_storage::SerializedBagMessage>> & msgs) {
           callback_interface->write(msgs);
           for (const auto & msg : msgs) {
             // count messages as successfully written
             std::lock_guard<std::mutex> lock(topics_mutex);
             if (topics_info_map.find(msg->topic_name) != topics_info_map.end()) {
               topics_info_map[msg->topic_name].message_count++;
             }
           }
         };
}
}  // namespace

SequentialWriter::SequentialWriter(
  std::unique_ptr<rosbag2_storage::StorageFactoryInterface> storage_factory,
  std::shared_ptr<SerializationFormatConverterFactoryInterface> converter_factory,
  std::unique_ptr<rosbag2_storage::MetadataIo> metadata_io)
: storage_factory_(std::move(storage_factory)),
  converter_factory_(std::move(converter_factory)),
  storage_(nullptr),
  metadata_io_(std::move(metadata_io)),
  converter_(nullptr),
  topics_names_to_info_(),
  metadata_()
{}

SequentialWriter::~SequentialWriter()
{
  close();
}

void SequentialWriter::init_metadata()
{
  metadata_ = rosbag2_storage::BagMetadata{};
  metadata_.storage_identifier = storage_->get_storage_identifier();
  metadata_.starting_time = std::chrono::time_point<std::chrono::high_resolution_clock>(
    std::chrono::nanoseconds::max());
  metadata_.relative_file_paths = {strip_parent_path(storage_->get_relative_file_path())};
}

void SequentialWriter::open(
  const rosbag2_storage::StorageOptions & storage_options,
  const ConverterOptions & converter_options)
{
  base_folder_ = storage_options.uri;
  storage_options_ = storage_options;

  if (converter_options.output_serialization_format !=
    converter_options.input_serialization_format)
  {
    converter_ = std::make_unique<Converter>(converter_options, converter_factory_);
  }

  rcpputils::fs::path db_path(storage_options.uri);
  if (db_path.is_directory()) {
    std::stringstream error;
    error << "Database directory already exists (" << db_path.string() <<
      "), can't overwrite existing database";
    throw std::runtime_error{error.str()};
  }

  bool dir_created = rcpputils::fs::create_directories(db_path);
  if (!dir_created) {
    std::stringstream error;
    error << "Failed to create database directory (" << db_path.string() << ").";
    throw std::runtime_error{error.str()};
  }

  storage_options_.uri = format_storage_uri(base_folder_, 0);
  storage_ = storage_factory_->open_read_write(storage_options_);
  if (!storage_) {
    throw std::runtime_error("No storage could be initialized. Abort");
  }

  if (storage_options_.max_bagfile_size != 0 &&
    storage_options_.max_bagfile_size < storage_->get_minimum_split_file_size())
  {
    std::stringstream error;
    error << "Invalid bag splitting size given. Please provide a value greater than " <<
      storage_->get_minimum_split_file_size() << ". Specified value of " <<
      storage_options.max_bagfile_size;
    throw std::runtime_error{error.str()};
  }

  use_cache_ = storage_options.max_cache_size > 0u;
  if (use_cache_) {
    message_cache_ = std::make_shared<rosbag2_cpp::cache::MessageCache>(
      storage_options.max_cache_size);
    cache_consumer_ = std::make_unique<rosbag2_cpp::cache::CacheConsumer>(
      message_cache_,
      make_callback(
        storage_,
        topics_names_to_info_,
        topics_info_mutex_));
  }
  init_metadata();
}

void SequentialWriter::close()
{
  if (use_cache_) {
    // destructor will flush message cache
    cache_consumer_.reset();
    message_cache_.reset();
  }

  if (!base_folder_.empty()) {
    finalize_metadata();
    metadata_io_->write_metadata(base_folder_, metadata_);
  }

  storage_.reset();  // Necessary to ensure that the storage is destroyed before the factory
  storage_factory_.reset();
}

void SequentialWriter::create_topic(const rosbag2_storage::TopicMetadata & topic_with_type)
{
  if (topics_names_to_info_.find(topic_with_type.name) !=
    topics_names_to_info_.end())
  {
    // nothing to do, topic already created
    return;
  }

  if (!storage_) {
    throw std::runtime_error("Bag is not open. Call open() before writing.");
  }

  rosbag2_storage::TopicInformation info{};
  info.topic_metadata = topic_with_type;

  bool insert_succeded = false;
  {
    std::lock_guard<std::mutex> lock(topics_info_mutex_);
    const auto insert_res = topics_names_to_info_.insert(
      std::make_pair(topic_with_type.name, info));
    insert_succeded = insert_res.second;
  }

  if (!insert_succeded) {
    std::stringstream errmsg;
    errmsg << "Failed to insert topic \"" << topic_with_type.name << "\"!";

    throw std::runtime_error(errmsg.str());
  }

  storage_->create_topic(topic_with_type);

  if (converter_) {
    converter_->add_topic(topic_with_type.name, topic_with_type.type);
  }
}

void SequentialWriter::remove_topic(const rosbag2_storage::TopicMetadata & topic_with_type)
{
  if (!storage_) {
    throw std::runtime_error("Bag is not open. Call open() before removing.");
  }

  bool erased = false;
  {
    std::lock_guard<std::mutex> lock(topics_info_mutex_);
    erased = topics_names_to_info_.erase(topic_with_type.name) > 0;
  }

  if (erased) {
    storage_->remove_topic(topic_with_type);
  } else {
    std::stringstream errmsg;
    errmsg << "Failed to remove the non-existing topic \"" <<
      topic_with_type.name << "\"!";

    throw std::runtime_error(errmsg.str());
  }
}

std::string SequentialWriter::format_storage_uri(
  const std::string & base_folder, uint64_t storage_count)
{
  // Right now `base_folder_` is always just the folder name for where to install the bagfile.
  // The name of the folder needs to be queried in case
  // SequentialWriter is opened with a relative path.
  std::stringstream storage_file_name;
  storage_file_name << rcpputils::fs::path(base_folder).filename().string() << "_" << storage_count;

  return (rcpputils::fs::path(base_folder) / storage_file_name.str()).string();
}

void SequentialWriter::switch_to_next_storage()
{
  // consumer remaining message cache
  if (use_cache_) {
    cache_consumer_->close();
    message_cache_->log_dropped();
  }

  storage_options_.uri = format_storage_uri(
    base_folder_,
    metadata_.relative_file_paths.size());
  storage_ = storage_factory_->open_read_write(storage_options_);

  if (!storage_) {
    std::stringstream errmsg;
    errmsg << "Failed to rollover bagfile to new file: \"" << storage_options_.uri << "\"!";

    throw std::runtime_error(errmsg.str());
  }

  // Re-register all topics since we rolled-over to a new bagfile.
  for (const auto & topic : topics_names_to_info_) {
    storage_->create_topic(topic.second.topic_metadata);
  }

  // Set new storage in buffer layer and restart consumer thread
  if (use_cache_) {
    cache_consumer_->change_consume_callback(
      make_callback(
        storage_,
        topics_names_to_info_,
        topics_info_mutex_));
  }
}

void SequentialWriter::split_bagfile()
{
  auto info = std::make_shared<bag_events::OutputFileSplitInfo>();
  info->closed_file = storage_options_.uri;
  switch_to_next_storage();
  info->opened_file = storage_options_.uri;

  metadata_.relative_file_paths.push_back(strip_parent_path(storage_->get_relative_file_path()));

  callback_manager_.execute_callbacks(bag_events::BagEvent::OUTPUT_FILE_SPLIT, info);
}

void SequentialWriter::write(std::shared_ptr<rosbag2_storage::SerializedBagMessage> message)
{
  if (!storage_) {
    throw std::runtime_error("Bag is not open. Call open() before writing.");
  }

  // Get TopicInformation handler for counting messages.
  rosbag2_storage::TopicInformation * topic_information {nullptr};
  try {
    topic_information = &topics_names_to_info_.at(message->topic_name);
  } catch (const std::out_of_range & /* oor */) {
    std::stringstream errmsg;
    errmsg << "Failed to write on topic '" << message->topic_name <<
      "'. Call create_topic() before first write.";
    throw std::runtime_error(errmsg.str());
  }

  if (should_split_bagfile()) {
    split_bagfile();

    // Update bagfile starting time
    metadata_.starting_time = std::chrono::high_resolution_clock::now();
  }

  const auto message_timestamp = std::chrono::time_point<std::chrono::high_resolution_clock>(
    std::chrono::nanoseconds(message->time_stamp));
  metadata_.starting_time = std::min(metadata_.starting_time, message_timestamp);

  const auto duration = message_timestamp - metadata_.starting_time;
  metadata_.duration = std::max(metadata_.duration, duration);

  auto converted_msg = get_writeable_message(message);

  if (storage_options_.max_cache_size == 0u) {
    // If cache size is set to zero, we write to storage directly
    storage_->write(converted_msg);
    ++topic_information->message_count;
  } else {
    // Otherwise, use cache buffer
    message_cache_->push(converted_msg);
  }
}

std::shared_ptr<rosbag2_storage::SerializedBagMessage>
SequentialWriter::get_writeable_message(
  std::shared_ptr<rosbag2_storage::SerializedBagMessage> message)
{
  return converter_ ? converter_->convert(message) : message;
}

bool SequentialWriter::should_split_bagfile() const
{
  // Assume we aren't splitting
  bool should_split = false;

  // Splitting by size
  if (storage_options_.max_bagfile_size !=
    rosbag2_storage::storage_interfaces::MAX_BAGFILE_SIZE_NO_SPLIT)
  {
    should_split = should_split ||
      (storage_->get_bagfile_size() > storage_options_.max_bagfile_size);
  }

  // Splitting by time
  if (storage_options_.max_bagfile_duration !=
    rosbag2_storage::storage_interfaces::MAX_BAGFILE_DURATION_NO_SPLIT)
  {
    auto max_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::seconds(storage_options_.max_bagfile_duration));
    should_split = should_split ||
      ((std::chrono::high_resolution_clock::now() - metadata_.starting_time) > max_duration_ns);
  }

  return should_split;
}

void SequentialWriter::finalize_metadata()
{
  metadata_.bag_size = 0;

  for (const auto & path : metadata_.relative_file_paths) {
    const auto bag_path = rcpputils::fs::path{path};

    if (bag_path.exists()) {
      metadata_.bag_size += bag_path.file_size();
    }
  }

  metadata_.topics_with_message_count.clear();
  metadata_.topics_with_message_count.reserve(topics_names_to_info_.size());
  metadata_.message_count = 0;

  for (const auto & topic : topics_names_to_info_) {
    metadata_.topics_with_message_count.push_back(topic.second);
    metadata_.message_count += topic.second.message_count;
  }
}

void SequentialWriter::add_event_callbacks(bag_events::WriterEventCallbacks & callbacks)
{
  if (callbacks.output_file_split_callback) {
    callback_manager_.add_event_callback(
      callbacks.output_file_split_callback,
      bag_events::BagEvent::OUTPUT_FILE_SPLIT);
  }
}

}  // namespace writers
}  // namespace rosbag2_cpp
