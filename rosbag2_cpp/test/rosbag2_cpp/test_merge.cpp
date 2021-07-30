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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <rosbag2_cpp/reader.hpp>

#include "test_msgs/message_fixtures.hpp"

#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <rosbag2_test_common/memory_management.hpp>
#include <rosbag2_test_common/mock_sequential_reader.hpp>

using namespace ::testing;  // NOLINT


template<typename MessageT>
std::shared_ptr<rosbag2_storage::SerializedBagMessage>
serialize_test_message(
  const std::string & topic,
  int64_t milliseconds,
  std::shared_ptr<MessageT> message)
{
  rosbag2_test_common::MemoryManagement memory_management;
  auto bag_msg = std::make_shared<rosbag2_storage::SerializedBagMessage>();
  bag_msg->serialized_data = memory_management.serialize_message(message);
  bag_msg->time_stamp = milliseconds * 1000000;
  bag_msg->topic_name = topic;

  return bag_msg;
}

TEST(merge, all_messages_from_all_readers_are_played)
{
  auto primitive_message1 = get_messages_basic_types()[0];
  primitive_message1->int32_value = 1;
  auto primitive_message2 = get_messages_basic_types()[1];
  primitive_message2->int32_value = 2;
  auto primitive_message3 = get_messages_basic_types()[2];
  primitive_message3->int32_value = 3;
  auto primitive_message4 = get_messages_basic_types()[3];
  primitive_message4->int32_value = 4;
  auto primitive_message5 = get_messages_basic_types()[0];
  primitive_message5->int32_value = 5;
  auto primitive_message6 = get_messages_basic_types()[1];
  primitive_message6->int32_value = 6;
  auto primitive_message7 = get_messages_basic_types()[2];
  primitive_message7->int32_value = 7;
  auto primitive_message8 = get_messages_basic_types()[3];
  primitive_message8->int32_value = 8;

  auto bag1_topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {"bag1topic1", "test_msgs/BasicTypes", "", ""},
    {"bag1topic2", "test_msgs/BasicTypes", "", ""},
  };
  auto bag2_topic_types = std::vector<rosbag2_storage::TopicMetadata>{
    {"bag2topic1", "test_msgs/BasicTypes", "", ""},
    {"bag2topic2", "test_msgs/BasicTypes", "", ""},
  };

  // The timestamps of these messages are ordered such that interleaving them should give us
  // primitive_message1..primitive_message8 in order when read out
  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> bag1_messages =
  {serialize_test_message("bag1topic1", 100, primitive_message2),
    serialize_test_message("bag1topic1", 150, primitive_message3),
    serialize_test_message("bag1topic2", 250, primitive_message5),
    serialize_test_message("bag1topic1", 350, primitive_message8),
  };
  std::vector<std::shared_ptr<rosbag2_storage::SerializedBagMessage>> bag2_messages =
  {serialize_test_message("bag2topic1", 50, primitive_message1),
    serialize_test_message("bag2topic2", 175, primitive_message4),
    serialize_test_message("bag2topic2", 250, primitive_message6),
    serialize_test_message("bag2topic1", 300, primitive_message7),
  };

  auto bag1_reader = std::make_shared<MockSequentialReader>();
  bag1_reader->prepare(bag1_messages, bag1_topic_types);
  auto bag2_reader = std::make_shared<MockSequentialReader>();
  bag2_reader->prepare(bag2_messages, bag2_topic_types);
  rosbag2_cpp::Reader::ReaderImplVector readers({bag1_reader, bag2_reader});

  auto reader =
    std::make_shared<rosbag2_cpp::Reader>(readers);
  reader->open(rosbag2_storage::StorageOptions(), rosbag2_cpp::ConverterOptions());

  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 1);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 2);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 3);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 4);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 5);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 6);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 7);
  }
  {
    EXPECT_TRUE(reader->has_next());
    auto next_msg = reader->read_next<test_msgs::msg::BasicTypes>();
    EXPECT_EQ(next_msg.int32_value, 8);
  }
}
