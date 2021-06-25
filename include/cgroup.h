// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <experimental/expected.hpp>
#include <string_view>

namespace RUNW {

class State;
class CGroup {
public:
  enum class Mode {
    Unknown,
    Unified,
    Legacy,
    Hybird,
  };
  static cxx20::expected<void, int> enter(std::string_view ContainerId,
                                          const State &State) noexcept;

  static cxx20::expected<void, int> finalize(const State &State);

private:
  static const Mode CGroupMode;
};

} // namespace RUNW
