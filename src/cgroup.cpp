// SPDX-License-Identifier: Apache-2.0

#define ELPP_STL_LOGGING

#include "cgroup.h"
#include "sdbus.h"
#include "state.h"
#include <common/log.h>
#include <streambuf>
#include <sys/vfs.h>

using namespace std::literals;

namespace RUNW {

namespace {

class JobStatusChecker {
public:
  std::experimental::expected<void, int> setup(SDBus &Bus) noexcept {
    return Bus.matchSignalAsync(
        "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
        "org.freedesktop.systemd1.Manager", "JobRemoved", Callback);
  }
  std::experimental::expected<void, int> check(SDBus &Bus, const char *Path,
                                               const char *Op) noexcept {
    this->Path = Path;
    this->Op = Op;
    while (!Terminated) {
      if (auto Res = Bus.process(); !Res) {
        LOG(ERROR) << "sd-bus process:"sv << strerror(Res.error());
        return Res.map([](auto) {});
      } else {
        if (*Res) {
          continue;
        }
      }
      if (auto Res = Bus.wait(std::numeric_limits<uint64_t>::max()); !Res) {
        LOG(ERROR) << "sd-bus wait:"sv << strerror(Res.error());
        return Res;
      }
    }
    if (Error) {
      return std::experimental::unexpected(EFAULT);
    }
    return {};
  }

private:
  std::function<int(SDBusMessage &)> Callback =
      [this](SDBusMessage &Msg) -> int {
    uint32_t MId;
    const char *MPath, *MUnit, *MResult;
    if (auto Res = Msg.read("uoss", MId, MPath, MUnit, MResult); !Res) {
      return -1;
    }
    if (std::strcmp(Path, MPath) == 0) {
      Terminated = true;
      if (std::strcmp(MResult, "done") != 0) {
        LOG(ERROR) << "error "sv << Op << " systemd unit `"sv << MUnit
                   << "`: got `"sv << MResult << '`';
        Error = true;
      }
    }
    return 0;
  };
  const char *Path = nullptr;
  const char *Op = nullptr;
  bool Terminated = false;
  bool Error = false;
};

CGroup::Mode checkMode() noexcept {
  static constexpr const uint32_t kCgroup2SuperMagic = UINT32_C(0x63677270);
  static constexpr const uint32_t kTmpFsMagic = UINT32_C(0x01021994);
  struct statfs Stat;

  if (int Ret = statfs("/sys/fs/cgroup", &Stat); Ret < 0) {
    return CGroup::Mode::Unknown;
  }
  if (Stat.f_type == kCgroup2SuperMagic) {
    return CGroup::Mode::Unified;
  }
  if (Stat.f_type != kTmpFsMagic) {
    return CGroup::Mode::Unknown;
  }

  if (int Ret = statfs("/sys/fs/cgroup/unified", &Stat);
      Ret < 0 && errno != ENOENT) {
    return CGroup::Mode::Unknown;
  } else if (Ret < 0) {
    return CGroup::Mode::Legacy;
  }
  if (Stat.f_type == kCgroup2SuperMagic) {
    return CGroup::Mode::Hybird;
  }
  return CGroup::Mode::Legacy;
}

std::experimental::expected<std::string, int>
readAll(std::filesystem::path Path) {
  std::string Content;
  std::filebuf File;
  if (!File.open(Path, std::ios_base::in | std::ios_base::binary)) {
    return std::experimental::unexpected(errno);
  }

  std::array<char, 4096> Buffer;
  while (const auto Size = File.sgetn(Buffer.data(), Buffer.size())) {
    Content.append(Buffer.data(), Size);
  }
  return Content;
}

} // namespace

const CGroup::Mode CGroup::CGroupMode = checkMode();

std::experimental::expected<void, int>
CGroup::enter(std::string_view ContainerId, const State &State) noexcept {
  SDBus Bus;
  if (auto Res = SDBus::defaultUser()) {
    Bus = std::move(*Res);
  } else if (auto Res = SDBus::defaultSystem()) {
    Bus = std::move(*Res);
  } else {
    LOG(ERROR) << "cannot open sd-bus:"sv << strerror(Res.error());
    return Res.map([](const auto &) {});
  }

  JobStatusChecker Checker;
  if (auto Res = Checker.setup(Bus); !Res) {
    LOG(ERROR) << "sd-bus match signal:"sv << strerror(Res.error());
    return Res;
  }

  SDBusMessage Msg;
  if (auto Res = Bus.methodCall(
          "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
          "org.freedesktop.systemd1.Manager", "StartTransientUnit");
      Res) {
    Msg = std::move(*Res);
  } else {
    LOG(ERROR) << "set up dbus message:"sv << strerror(Res.error());
    return Res.map([](const auto &) {});
  }

  auto CgroupsPath = State.bundle().linuxCgroupsPath();
  std::string Scope, Slice;
  if (CgroupsPath.empty()) {
    Scope = "runw-"sv;
    Scope += ContainerId;
    Scope += ".scope"sv;
  } else {
    const auto FirstColon = CgroupsPath.find(':');
    if (FirstColon == std::string_view::npos) {
      Scope = CgroupsPath;
      Scope += ".scope"sv;
    } else {
      Scope = CgroupsPath.substr(FirstColon + 1);
      const auto SecondColon = Scope.find(':', FirstColon + 1);
      if (SecondColon != std::string_view::npos) {
        Scope[SecondColon] = '-';
      }
      Scope += ".scope"sv;
    }
    Slice = CgroupsPath.substr(0, FirstColon);
  }

  if (auto Res = Msg.append("ss", Scope.c_str(), "fail"); !Res) {
    LOG(ERROR) << "sd-bus message append scope:"sv << strerror(Res.error());
    return Res;
  }

  if (auto Res = Msg.openContainer('a', "(sv)"); !Res) {
    LOG(ERROR) << "sd_bus open container:"sv << strerror(Res.error());
    return Res;
  }

  if (!Slice.empty()) {
    if (auto Res = Msg.append("(sv)", "Slice", "s", Slice.c_str()); !Res) {
      LOG(ERROR) << "sd-bus message append Slice:"sv << strerror(Res.error());
      return Res;
    }
  }

  // TODO: systemd annotations "org.systemd.property."

  if (auto Res = Msg.append("(sv)", "Description", "s", "runw container");
      !Res) {
    LOG(ERROR) << "sd-bus message append Description:"sv
               << strerror(Res.error());
    return Res;
  }

  if (auto Res = Msg.append("(sv)", "PIDs", "au", 1, State.getPid()); !Res) {
    LOG(ERROR) << "sd-bus message append Description:"sv
               << strerror(Res.error());
    return Res;
  }

  for (auto [Name, Value] : {std::pair{"Delegate", true}}) {
    if (!Value) {
      continue;
    }
    if (auto Res = Msg.append("(sv)", Name, "b", 1); !Res) {
      LOG(ERROR) << "sd-bus message append "sv << Name << ':'
                 << strerror(Res.error());
      return Res;
    }
  }

  if (auto Res = Msg.closeContainer(); !Res) {
    LOG(ERROR) << "sd-bus close container:"sv << strerror(Res.error());
    return Res;
  }

  if (auto Res = Msg.append("a(sa(sv))", nullptr); !Res) {
    LOG(ERROR) << "sd-bus message append:"sv << strerror(Res.error());
    return Res;
  }

  if (auto Res = Bus.call(std::move(Msg), 0)) {
    Msg = std::move(*Res);
  } else {
    LOG(ERROR) << "sd-bus call:"sv << strerror(Res.error());
    return Res.map([](const auto &) {});
  }

  const char *Object;
  if (auto Res = Msg.read("o", Object); !Res) {
    LOG(ERROR) << "sd-bus message read:"sv << strerror(Res.error());
    return Res;
  }

  return Checker.check(Bus, Object, "creating");
}

std::experimental::expected<void, int> CGroup::finalize(const State &State) {
  if (CGroupMode == Mode::Unknown) {
    LOG(ERROR) << "unknown cgroup mode"sv;
    return std::experimental::unexpected(EINVAL);
  }

  const auto CgroupPath =
      std::filesystem::u8path("/proc"sv) /
      std::filesystem::u8path(std::to_string(State.getPid())) /
      std::filesystem::u8path("cgroup"sv);

  std::string Content;
  if (auto Res = readAll(CgroupPath)) {
    Content = std::move(*Res);
  } else {
    return Res.map([](auto &) {});
  }

  if (CGroupMode == Mode::Legacy) {
    auto From = Content.find(":memory"sv);
    if (From == std::string::npos) {
      LOG(ERROR) << "cannot find memory controller for the current process"sv;
      return std::experimental::unexpected(EINVAL);
    }
    From += 8;
    auto To = Content.find('\n', From);
    if (To == std::string::npos) {
      LOG(ERROR) << "cannot parse /proc/self/cgroup"sv;
      return std::experimental::unexpected(EINVAL);
    }
    std::string Path = Content.substr(From, To - From);
  } else {
    auto From = Content.find("0::"sv);
    if (From == std::string::npos) {
      LOG(ERROR) << "cannot find cgroup2 for the current process"sv;
      return std::experimental::unexpected(EINVAL);
    }
    From += 3;
    auto To = Content.find('\n', From);
    if (To == std::string::npos) {
      LOG(ERROR) << "cannot parse /proc/self/cgroup"sv;
      return std::experimental::unexpected(EINVAL);
    }
    std::string Path = Content.substr(From, To - From);
  }
  // TODO: support "run.oci.systemd.subgroup" annotation suffix

  if (CGroupMode != Mode::Unified && geteuid() != 0) {
    return {};
  }

  return {};
}

} // namespace RUNW
