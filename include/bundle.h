// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <common/filesystem.h>
#include <experimental/span.hpp>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace RUNW {

class Bundle {
private:
  struct NamespaceDesc;

public:
  Bundle() = default;
  bool load(const std::filesystem::path &Path, std::string_view ConfigFileName);
  std::string_view ociVersion() const noexcept { return OCIVersion; }
  bool terminal() const noexcept { return Terminal; }
  uint32_t consoleWidth() const noexcept { return ConsoleWidth; }
  uint32_t consoleHeight() const noexcept { return ConsoleHeight; }
  std::string_view cwd() const noexcept { return Cwd; }
  cxx20::span<const std::string> envs() const noexcept { return Envs; }
  cxx20::span<const std::string> args() const noexcept { return Args; }
  std::string_view rootPath() const noexcept { return RootPath; }
  std::string_view linuxCgroupsPath() const noexcept { return CgroupsPath; }
  cxx20::span<const NamespaceDesc> linuxNamespaces() const noexcept {
    return Namespaces;
  }

private:
  std::string OCIVersion{};

  // Process
  bool Terminal{};
  uint32_t ConsoleWidth{};
  uint32_t ConsoleHeight{};
  std::string Cwd{};
  std::vector<std::string> Envs{};
  std::vector<std::string> Args{};
  std::string CommandLine{};

  // POSIX Process
  std::vector<std::tuple<std::string, int64_t, int64_t>> RLimits{};

  // Linux Process
  std::string ApparmorProfile{};
  std::string SelinuxLabel{};
  std::vector<std::string> EffectiveCapabilities{};
  std::vector<std::string> BoundingCapabilities{};
  std::vector<std::string> InheritableCapabilities{};
  std::vector<std::string> PermittedCapabilities{};
  std::vector<std::string> AmbientCapabilities{};
  bool NoNewPrivileges{};
  int32_t OomScoreAdj{};

  // POSIX-platform User
  int32_t Uid{};
  int32_t Gid{};
  int32_t UMask{};
  std::vector<int32_t> AdditionalGids{};

  // Windows User
  std::string Username{};

  // Hostname
  std::string Hostname{};

  // Root
  std::string RootPath{};
  bool RootReadonly = false;

  // Mounts
  struct MountDesc {
    std::string Destination{};
    std::string Source{};
    std::string Type{};
    std::vector<std::string> Options{};
    MountDesc() = default;
    MountDesc(const MountDesc &) = default;
    MountDesc(MountDesc &&) = default;
  };
  std::vector<MountDesc> Mounts;

  // Namespaces
  struct NamespaceDesc {
    std::string Type{};
    std::string Path{};
    NamespaceDesc() = default;
    NamespaceDesc(const NamespaceDesc &) = default;
    NamespaceDesc(NamespaceDesc &&) = default;
  };
  std::vector<NamespaceDesc> Namespaces{};

  // IdMappings
  struct IdMappingDesc {
    uint32_t ContainerId{};
    uint32_t HostId{};
    uint32_t Size{};
    IdMappingDesc() = default;
    IdMappingDesc(const IdMappingDesc &) = default;
    IdMappingDesc(IdMappingDesc &&) = default;
  };
  std::vector<IdMappingDesc> UidMappings{};
  std::vector<IdMappingDesc> GidMappings{};

  // Devices
  struct DeviceDesc {
    std::string Type{};
    std::string Path{};
    int64_t Major{};
    int64_t Minor{};
    uint32_t FileMode{};
    uint32_t Uid{};
    uint32_t Gid{};
    DeviceDesc() = default;
    DeviceDesc(const DeviceDesc &) = default;
    DeviceDesc(DeviceDesc &&) = default;
  };
  std::vector<DeviceDesc> Devices{};

  // Cgroups Path
  std::string CgroupsPath{};
  uint64_t ResourcesMemoryLimit{};
  uint64_t ResourcesMemoryReservation{};
};

} // namespace RUNW
