/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/packet/error_cause/unresolvable_address_cause.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "api/array_view.h"
#include "net/dcsctp/packet/bounded_byte_reader.h"
#include "net/dcsctp/packet/bounded_byte_writer.h"

namespace dcsctp {

// https://tools.ietf.org/html/rfc4960#section-3.3.10.5

//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |     Cause Code=5              |      Cause Length             |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  /                  Unresolvable Address                         /
//  \                                                               \
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
constexpr int UnresolvableAddressCause::kType;

std::optional<UnresolvableAddressCause> UnresolvableAddressCause::Parse(
    webrtc::ArrayView<const uint8_t> data) {
  std::optional<BoundedByteReader<kHeaderSize>> reader = ParseTLV(data);
  if (!reader.has_value()) {
    return std::nullopt;
  }
  return UnresolvableAddressCause(reader->variable_data());
}

void UnresolvableAddressCause::SerializeTo(std::vector<uint8_t>& out) const {
  BoundedByteWriter<kHeaderSize> writer =
      AllocateTLV(out, unresolvable_address_.size());
  writer.CopyToVariableData(unresolvable_address_);
}

std::string UnresolvableAddressCause::ToString() const {
  return "Unresolvable Address";
}
}  // namespace dcsctp
