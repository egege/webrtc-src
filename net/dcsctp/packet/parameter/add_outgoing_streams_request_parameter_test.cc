/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/packet/parameter/add_outgoing_streams_request_parameter.h"

#include <cstdint>
#include <vector>

#include "net/dcsctp/common/internal_types.h"
#include "net/dcsctp/testing/testing_macros.h"
#include "test/gtest.h"

namespace dcsctp {
namespace {

TEST(AddOutgoingStreamsRequestParameterTest, SerializeAndDeserialize) {
  AddOutgoingStreamsRequestParameter parameter(ReconfigRequestSN(1), 2);

  std::vector<uint8_t> serialized;
  parameter.SerializeTo(serialized);

  ASSERT_HAS_VALUE_AND_ASSIGN(
      AddOutgoingStreamsRequestParameter deserialized,
      AddOutgoingStreamsRequestParameter::Parse(serialized));

  EXPECT_EQ(*deserialized.request_sequence_number(), 1u);
  EXPECT_EQ(deserialized.nbr_of_new_streams(), 2u);
}

}  // namespace
}  // namespace dcsctp
