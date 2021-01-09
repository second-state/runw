// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "defines.h"
#include <common/filesystem.h>
#include <string_view>
#if defined(RUNW_OS_LINUX) || defined(RUNW_OS_MACOS) || defined(RUNW_OS_SOLARIS)
#include <unistd.h>
#elif defined(RUNW_OS_WINDOWS)
#include <process.h>
using pid_t = int;
#endif

#include "bundle.h"

namespace RUNW {

class State {
public:
  enum class StatusCode {
    Unknown,
    Creating,
    Created,
    Running,
    Stopped,
  };
  static const std::string_view kOCIVersion;
  static const std::string_view kStatusUnknown;
  static const std::string_view kStatusCreating;
  static const std::string_view kStatusCreated;
  static const std::string_view kStatusRunning;
  static const std::string_view kStatusStopped;

  State() = default;
  State(std::string_view ContainerId, std::string_view BundlePath)
      : ContainerId(ContainerId), BundlePath(BundlePath) {}
  bool load(const std::filesystem::path &Path, std::string_view ConfigFileName);
  bool loadBundle(std::string_view ConfigFileName);
  void print(std::ostream &Stream) const;

  pid_t getPid() const noexcept { return Pid; }
  const Bundle &bundle() const noexcept { return Config; }
  void setCreating() noexcept;
  void setCreated() noexcept;
  void setRunning() noexcept;
  void setStopped(int ExitCode) noexcept;

  void setSystemdCgroup(bool Value) noexcept { SystemdCgroup = Value; }

private:
  std::string_view getStatusString() const {
    switch (Status) {
    case StatusCode::Creating:
      return kStatusCreating;
    case StatusCode::Created:
      return kStatusCreated;
    case StatusCode::Running:
      return kStatusRunning;
    case StatusCode::Stopped:
      return kStatusStopped;
    default:
      return kStatusUnknown;
    }
  }

  std::string ContainerId;
  std::string BundlePath;
  std::string CreatedTimestamp;
  std::string StartedTimestamp;
  std::string FinishedTimestamp;
  Bundle Config;
  StatusCode Status = StatusCode::Unknown;
  int ExitCode = 0;
  pid_t Pid = -1;
  bool SystemdCgroup = false;
};

} // namespace RUNW
