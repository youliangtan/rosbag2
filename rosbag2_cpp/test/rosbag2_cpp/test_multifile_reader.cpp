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

#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "rcpputils/filesystem_helper.hpp"

#include "rosbag2_cpp/reader.hpp"
#include "rosbag2_cpp/readers/sequential_reader.hpp"

#include "rosbag2_storage/bag_metadata.hpp"
#include "rosbag2_storage/topic_metadata.hpp"

#include "mock_converter_factory.hpp"
#include "mock_metadata_io.hpp"
#include "mock_storage.hpp"
#include "mock_storage_factory.hpp"

using namespace testing;  // NOLINT

class MultifileReaderTest : public Test
{
public:
  MultifileReaderTest()
  : storage_(std::make_shared<NiceMock<MockStorage>>()),
    converter_factory_(std::make_shared<StrictMock<MockConverterFactory>>()),
    storage_serialization_format_("rmw1_format"),
    storage_uri_(rcpputils::fs::temp_directory_path().string()),
    relative_path_1_("some_relative_path_1"),
    relative_path_2_("some_relative_path_2"),
    absolute_path_1_((rcpputils::fs::path(storage_uri_) / "some/folder").string()),
    default_storage_options_({storage_uri_, ""})
  {}

  virtual void init()
  {
    auto topic_with_type = rosbag2_storage::TopicMetadata{
      "topic", "test_msgs/BasicTypes", storage_serialization_format_, ""};
    auto topics_and_types = std::vector<rosbag2_storage::TopicMetadata>{topic_with_type};
    metadata_ = get_metadata();
    metadata_.topics_with_message_count.push_back({topic_with_type, 10});

    auto message = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    message->topic_name = topic_with_type.name;

    EXPECT_CALL(*storage_, get_all_topics_and_types())
    .Times(AtMost(1)).WillRepeatedly(Return(topics_and_types));
    ON_CALL(*storage_, read_next()).WillByDefault(Return(message));
  }

  virtual ~MultifileReaderTest() = default;

  std::unique_ptr<rosbag2_storage::StorageFactoryInterface> make_storage_mock()
  {
    auto storage_factory = std::make_unique<StrictMock<MockStorageFactory>>();
    EXPECT_CALL(*storage_factory, open_read_only(_)).WillRepeatedly(Return(storage_));
    return storage_factory;
  }

  std::unique_ptr<rosbag2_storage::MetadataIo> make_metadataio_mock()
  {
    auto metadata_io = std::make_unique<NiceMock<MockMetadataIo>>();
    ON_CALL(*metadata_io, read_metadata(_)).WillByDefault(Return(metadata_));
    EXPECT_CALL(*metadata_io, metadata_file_exists(_)).WillRepeatedly(Return(true));
    return metadata_io;
  }

  virtual rosbag2_storage::BagMetadata get_metadata() const
  {
    rosbag2_storage::BagMetadata metadata;

    metadata.relative_file_paths =
      {relative_path_1_, relative_path_2_, absolute_path_1_};

    return metadata;
  }

  rosbag2_storage::BagMetadata metadata_;
  std::shared_ptr<NiceMock<MockStorage>> storage_;
  std::shared_ptr<StrictMock<MockConverterFactory>> converter_factory_;
  std::string storage_serialization_format_;
  std::string storage_uri_;
  std::string relative_path_1_;
  std::string relative_path_2_;
  std::string absolute_path_1_;
  rosbag2_storage::StorageOptions default_storage_options_;
};

class MultifileReaderTestVersion3 : public MultifileReaderTest
{
public:
  MultifileReaderTestVersion3()
  : MultifileReaderTest()
  {
    relative_path_1_ = "rosbag_name/some_relative_path_1";
    relative_path_2_ = "rosbag_name/some_relative_path_2";
    absolute_path_1_ = (rcpputils::fs::path(storage_uri_) / "some/folder").string();
  }

  rosbag2_storage::BagMetadata get_metadata() const override
  {
    auto metadata = MultifileReaderTest::get_metadata();
    metadata.version = 3;

    return metadata;
  }
};

TEST_F(MultifileReaderTest, has_next_reads_next_file)
{
  init();

  // storage::has_next() is called twice when reader::has_next() is called
  EXPECT_CALL(*storage_, has_next()).Times(6)
  .WillOnce(Return(true)).WillOnce(Return(true))  // We have a message
  .WillOnce(Return(false))  // No message, load next file
  .WillOnce(Return(true))  // True since we now have a message
  .WillOnce(Return(false))  // No message, load next file
  .WillOnce(Return(true));  // True since we now have a message
  EXPECT_CALL(*converter_factory_, load_deserializer(storage_serialization_format_)).Times(0);
  EXPECT_CALL(*converter_factory_, load_serializer(storage_serialization_format_)).Times(0);
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));

  auto & sr = static_cast<rosbag2_cpp::readers::SequentialReader &>(
    reader->get_implementation_handle());

  auto resolved_relative_path_1 =
    (rcpputils::fs::path(storage_uri_) / relative_path_1_).string();
  auto resolved_relative_path_2 =
    (rcpputils::fs::path(storage_uri_) / relative_path_2_).string();
  auto resolved_absolute_path_1 =
    rcpputils::fs::path(absolute_path_1_).string();
  EXPECT_EQ(sr.get_current_file(), resolved_relative_path_1);
  reader->has_next();
  reader->read_next();
  reader->has_next();
  EXPECT_EQ(sr.get_current_file(), resolved_relative_path_2);
  reader->read_next();
  reader->has_next();
  EXPECT_EQ(sr.get_current_file(), resolved_absolute_path_1);
}

TEST_F(MultifileReaderTestVersion3, has_next_reads_next_file_version3)
{
  init();

  // storage::has_next() is called twice when reader::has_next() is called
  EXPECT_CALL(*storage_, has_next()).Times(6)
  .WillOnce(Return(true)).WillOnce(Return(true))  // We have a message
  .WillOnce(Return(false))  // No message, load next file
  .WillOnce(Return(true))  // True since we now have a message
  .WillOnce(Return(false))  // No message, load next file
  .WillOnce(Return(true));  // True since we now have a message
  EXPECT_CALL(*converter_factory_, load_deserializer(storage_serialization_format_)).Times(0);
  EXPECT_CALL(*converter_factory_, load_serializer(storage_serialization_format_)).Times(0);
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));

  auto & sr = static_cast<rosbag2_cpp::readers::SequentialReader &>(
    reader->get_implementation_handle());

  // Legacy version <=3 have a parent_path() prefixed in the relative files
  auto resolved_relative_path_1 =
    (rcpputils::fs::path(storage_uri_).parent_path() / relative_path_1_).string();
  auto resolved_relative_path_2 =
    (rcpputils::fs::path(storage_uri_).parent_path() / relative_path_2_).string();
  auto resolved_absolute_path_1 =
    rcpputils::fs::path(absolute_path_1_).string();
  EXPECT_EQ(sr.get_current_file(), resolved_relative_path_1);
  reader->has_next();
  reader->read_next();
  reader->has_next();
  EXPECT_EQ(sr.get_current_file(), resolved_relative_path_2);
  reader->read_next();
  reader->has_next();
  EXPECT_EQ(sr.get_current_file(), resolved_absolute_path_1);
}

TEST_F(MultifileReaderTest, has_next_throws_if_no_storage)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  EXPECT_ANY_THROW(reader->has_next());
}

TEST_F(MultifileReaderTest, read_next_throws_if_no_storage)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  EXPECT_ANY_THROW(reader->read_next());
}

TEST_F(MultifileReaderTest, get_metadata_throws_if_not_open)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  EXPECT_ANY_THROW(reader->get_metadata());
}

TEST_F(MultifileReaderTest, get_all_topics_and_types_throws_if_not_open)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  EXPECT_ANY_THROW(reader->get_all_topics_and_types());
}

TEST_F(MultifileReaderTest, get_metadata_returns_metadata_from_io)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  const auto & metadata = reader->get_metadata();
  EXPECT_FALSE(metadata.topics_with_message_count.empty());
}

TEST_F(MultifileReaderTest, get_all_topics_and_types_returns_from_io_metadata)
{
  init();
  auto sequential_reader = std::make_unique<rosbag2_cpp::readers::SequentialReader>(
    default_storage_options_, rosbag2_cpp::ConverterOptions{"", storage_serialization_format_},
    make_storage_mock(), converter_factory_, make_metadataio_mock());
  auto reader = std::make_unique<rosbag2_cpp::Reader>(std::move(sequential_reader));
  const auto all_topics_and_types = reader->get_all_topics_and_types();
  EXPECT_FALSE(all_topics_and_types.empty());
}
