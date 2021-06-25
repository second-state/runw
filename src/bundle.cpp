// SPDX-License-Identifier: Apache-2.0

#define ELPP_STL_LOGGING
#include "bundle.h"
#include "defines.h"
#include <common/log.h>
#include <simdjson.h>

using namespace std::literals;

namespace RUNW {

namespace {

template <typename T, std::enable_if_t<std::is_integral_v<T>> * = nullptr>
simdjson::error_code get_int(const simdjson::dom::element &Element, T &Value) {
  std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t> Buffer;
  if (auto Error = Element.get(Buffer)) {
    return Error;
  }
  if (Buffer < std::numeric_limits<T>::min() ||
      Buffer > std::numeric_limits<T>::max()) {
    return simdjson::error_code::NUMBER_OUT_OF_RANGE;
  }
  Value = Buffer;
  return simdjson::error_code::SUCCESS;
}

} // namespace

bool Bundle::load(const std::filesystem::path &Path,
                  std::string_view ConfigFileName) {
  const auto ConfigPath = Path / ConfigFileName;
  simdjson::dom::parser Parser;
  simdjson::dom::object Config;
  if (auto Error = Parser.load(ConfigPath).get(Config)) {
    spdlog::error("{} load failed: {}"sv, ConfigPath.u8string(),
                  simdjson::error_message(Error));
    return false;
  }

  // load optional process object
  auto loadProcess = [this](const simdjson::dom::object &Process) {
    for (const auto &[Key, Element] : Process) {
      switch (Key[0]) {
      case 'a':
        if (Key == "args"sv) {
          simdjson::dom::array Args;
          if (auto Error = Element.get(Args)) {
            spdlog::error("load args failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          for (const auto &Arg : Args) {
            std::string_view ArgStr;
            if (auto Error = Arg.get(ArgStr)) {
              spdlog::error("load args arg failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            this->Args.emplace_back(ArgStr);
          }
        }
        break;
      case 'c':
        if (Key == "consoleSize"sv) {
          simdjson::dom::object ConsoleSize;
          if (auto Error = Element.get(ConsoleSize)) {
            spdlog::error("load consoleSize failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          for (const auto &[Key, Element] : ConsoleSize) {
            switch (Key[0]) {
            case 'h':
              if (Key == "height"sv) {
                if (auto Error = get_int(Element, ConsoleHeight)) {
                  spdlog::error("load height failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
              }
              break;
            case 'w':
              if (Key == "width"sv) {
                if (auto Error = get_int(Element, ConsoleWidth)) {
                  spdlog::error("load width failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
              }
              break;
            default:
              break;
            }
          }
        } else if (Key == "cwd"sv) {
          std::string_view Cwd;
          if (auto Error = Element.get(Cwd)) {
            spdlog::error("load cwd failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          this->Cwd = Cwd;
        } else if (Key == "capabilities"sv) {
          simdjson::dom::object Capabilities;
          if (auto Error = Element.get(Capabilities)) {
            spdlog::error("load capabilities failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }

          auto loadCapability = [](const simdjson::dom::object &Capabilities,
                                   std::string_view Key,
                                   std::vector<std::string> &Caps) {
            simdjson::dom::array Names;
            if (auto Error = Capabilities[Key].get(Names)) {
              // optional field
              if (Error == simdjson::error_code::NO_SUCH_FIELD) {
                return true;
              }
              spdlog::error("load capability failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            for (const auto &Name : Names) {
              std::string_view String;
              if (auto Error = Name.get(String)) {
                spdlog::error("load capability name failed: {}"sv,
                              simdjson::error_message(Error));
                return false;
              }
              Caps.emplace_back(String);
            }
            return true;
          };

          if (!loadCapability(Capabilities, "effective"sv,
                              EffectiveCapabilities)) {
            return false;
          }
          if (!loadCapability(Capabilities, "bounding"sv,
                              BoundingCapabilities)) {
            return false;
          }
          if (!loadCapability(Capabilities, "inheritable"sv,
                              InheritableCapabilities)) {
            return false;
          }
          if (!loadCapability(Capabilities, "permitted"sv,
                              PermittedCapabilities)) {
            return false;
          }
          if (!loadCapability(Capabilities, "ambient"sv, AmbientCapabilities)) {
            return false;
          }
        }
        break;
      case 'e':
        if (Key == "env"sv) {
          simdjson::dom::array Envs;
          if (auto Error = Element.get(Envs)) {
            spdlog::error("load env failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          for (const auto &Env : Envs) {
            std::string_view EnvStr;
            if (auto Error = Env.get(EnvStr)) {
              spdlog::error("load env str failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            this->Envs.emplace_back(EnvStr);
          }
        }
        break;
      case 'n':
        if (Key == "noNewPrivileges"sv) {
          if (auto Error = Element.get(NoNewPrivileges)) {
            spdlog::error("load noNewPrivileges failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
        }
        break;
      case 'o':
        if (Key == "oomScoreAdj"sv) {
          if (auto Error = get_int(Element, OomScoreAdj)) {
            spdlog::error("load oomScoreAdj failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
        }
        break;
      case 'r':
        if (Key == "rlimits"sv) {
          simdjson::dom::array RLimits;
          if (auto Error = Process["rlimits"sv].get(RLimits)) {
            spdlog::error("load rlimits failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          for (const auto &RLimit : RLimits) {
            simdjson::dom::object RLimitObject;
            if (auto Error = RLimit.get(RLimitObject)) {
              spdlog::error("load rlimits object failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            std::string_view Type;
            int64_t Soft;
            int64_t Hard;
            if (auto Error = RLimitObject["type"sv].get(Type)) {
              spdlog::error("load rlimits type failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            if (auto Error = RLimitObject["soft"sv].get(Soft)) {
              spdlog::error("load rlimits soft failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            if (auto Error = RLimitObject["hard"sv].get(Hard)) {
              spdlog::error("load rlimits hard failed: {}"sv,
                            simdjson::error_message(Error));
              return false;
            }
            this->RLimits.emplace_back(Type, Soft, Hard);
          }
        }
        break;
      case 't':
        if (Key == "terminal"sv) {
          if (auto Error = Element.get(Terminal)) {
            spdlog::error("load terminal failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
        }
        break;
      case 'u':
        if (Key == "user"sv) {
          simdjson::dom::object User;
          if (auto Error = Element.get(User)) {
            spdlog::error("load user failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }

          for (const auto &[Key, Element] : User) {
            switch (Key[0]) {
            case 'u':
              if (Key == "uid"sv) {
                if (auto Error = get_int(Element, Uid)) {
                  spdlog::error("load user uid failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
              } else if (Key == "umask"sv) {
                if (auto Error = get_int(Element, UMask)) {
                  spdlog::error("load user umask failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
              }
              break;
            case 'g':
              if (Key == "gid"sv) {
                if (auto Error = get_int(Element, Gid)) {
                  spdlog::error("load user gid failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
              }
              break;
            case 'a':
              if (Key == "additionalGids"sv) {
                simdjson::dom::array AGids;
                if (auto Error = Element.get(AGids)) {
                  spdlog::error("load user additionalGids failed: {}"sv,
                                simdjson::error_message(Error));
                  return false;
                }
                for (const auto &AGid : AGids) {
                  int32_t Value;
                  if (auto Error = get_int(AGid, Value)) {
                    spdlog::error("load user additionalGids gid failed: {}"sv,
                                  simdjson::error_message(Error));
                    return false;
                  }
                  AdditionalGids.push_back(Value);
                }
              }
              break;
            default:
              break;
            }
          }
        }
        break;
      default:
        break;
      }
    }

    return true;
  };

  auto loadRoot = [this](const simdjson::dom::object &Root) {
    for (const auto &[Key, Element] : Root) {
      switch (Key[0]) {
      case 'p':
        if (Key == "path"sv) {
          std::string_view RootPath;
          if (auto Error = Element.get(RootPath)) {
            spdlog::error("load root path failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
          this->RootPath = RootPath;
        }
        break;
      case 'r':
        if (Key == "readonly"sv) {
          if (auto Error = Element.get(RootReadonly)) {
            spdlog::error("load root readonly failed: {}"sv,
                          simdjson::error_message(Error));
            return false;
          }
        }
        break;
      default:
        break;
      }
    }
    return true;
  };

  auto loadMounts = [this](const simdjson::dom::array &Mounts) {
    for (const auto &Mount : Mounts) {
      simdjson::dom::object MountObject;
      if (Mount.get(MountObject)) {
        return false;
      }
      MountDesc Desc;
      for (const auto &[Key, Element] : MountObject) {
        switch (Key[0]) {
        case 'd':
          if (Key == "destination"sv) {
            std::string_view Destination;
            if (Element.get(Destination)) {
              return false;
            }
            Desc.Destination = Destination;
          }
          break;
        case 'o':
          if (Key == "options"sv) {
            simdjson::dom::array Options;
            if (Element.get(Options)) {
              return false;
            }
            for (const auto &Option : Options) {
              std::string_view String;
              if (Option.get(String)) {
                return false;
              }
              Desc.Options.emplace_back(String);
            }
          }
          break;
        case 's':
          if (Key == "source"sv) {
            std::string_view Source;
            if (Element.get(Source)) {
              return false;
            }
            Desc.Source = Source;
          }
          break;
        case 't':
          if (Key == "type"sv) {
            std::string_view Type;
            if (Element.get(Type)) {
              return false;
            }
            Desc.Type = Type;
          }
          break;
        default:
          break;
        }
      }
      this->Mounts.push_back(std::move(Desc));
    }
    return true;
  };

#if defined(RUNW_OS_LINUX)
  auto loadLinux = [this](const simdjson::dom::object &Linux) {
    for (const auto &[Key, Element] : Linux) {
      switch (Key[0]) {
      case 'n':
        if (Key == "namespaces"sv) {
          simdjson::dom::array Namespaces;
          if (Element.get(Namespaces)) {
            return false;
          }
          for (const auto &Namespace : Namespaces) {
            simdjson::dom::object NamespaceObject;
            if (Namespace.get(NamespaceObject)) {
              return false;
            }
            NamespaceDesc Desc;
            for (const auto &[Key, Element] : NamespaceObject) {
              switch (Key[0]) {
              case 'p':
                if (Key == "path"sv) {
                  std::string_view Path;
                  if (Element.get(Path)) {
                    return false;
                  }
                  Desc.Path = Path;
                }
                break;
              case 't':
                if (Key == "type"sv) {
                  std::string_view Type;
                  if (Element.get(Type)) {
                    return false;
                  }
                  Desc.Type = Type;
                }
                break;
              default:
                break;
              }
            }
            this->Namespaces.push_back(std::move(Desc));
          }
        }
        break;
      case 'u':
        if (Key == "uidMappings"sv) {
          simdjson::dom::array UidMappings;
          if (Element.get(UidMappings)) {
            return false;
          }
          for (const auto &UidMapping : UidMappings) {
            simdjson::dom::object UidMappingObject;
            if (UidMapping.get(UidMappingObject)) {
              return false;
            }
            IdMappingDesc Desc;
            for (const auto &[Key, Element] : UidMappingObject) {
              switch (Key[0]) {
              case 'c':
                if (Key == "containerID"sv) {
                  if (get_int(Element, Desc.ContainerId)) {
                    return false;
                  }
                }
                break;
              case 'h':
                if (Key == "hostID"sv) {
                  if (get_int(Element, Desc.HostId)) {
                    return false;
                  }
                }
                break;
              case 's':
                if (Key == "size"sv) {
                  if (get_int(Element, Desc.Size)) {
                    return false;
                  }
                }
                break;
              default:
                break;
              }
            }
            this->UidMappings.push_back(std::move(Desc));
          }
        }
        break;
      case 'g':
        if (Key == "gidMappings"sv) {
          simdjson::dom::array GidMappings;
          if (Element.get(GidMappings)) {
            return false;
          }
          for (const auto &GidMapping : GidMappings) {
            simdjson::dom::object GidMappingObject;
            if (GidMapping.get(GidMappingObject)) {
              return false;
            }
            IdMappingDesc Desc;
            for (const auto &[Key, Element] : GidMappingObject) {
              switch (Key[0]) {
              case 'c':
                if (Key == "containerID"sv) {
                  if (get_int(Element, Desc.ContainerId)) {
                    return false;
                  }
                }
                break;
              case 'h':
                if (Key == "hostID"sv) {
                  if (get_int(Element, Desc.HostId)) {
                    return false;
                  }
                }
                break;
              case 's':
                if (Key == "size"sv) {
                  if (get_int(Element, Desc.Size)) {
                    return false;
                  }
                }
                break;
              default:
                break;
              }
            }
            this->UidMappings.push_back(std::move(Desc));
          }
        }
        break;
      case 'd':
        if (Key == "devices"sv) {
          simdjson::dom::array Devices;
          if (Element.get(Devices)) {
            return false;
          }
          for (const auto &Device : Devices) {
            simdjson::dom::object DeviceObject;
            if (Device.get(DeviceObject)) {
              return false;
            }
            DeviceDesc Desc;
            for (const auto &[Key, Element] : DeviceObject) {
              switch (Key[0]) {
              case 't':
                if (Key == "type"sv) {
                  std::string_view Type;
                  if (Element.get(Type)) {
                    return false;
                  }
                  Desc.Type = Type;
                }
                break;
              case 'p':
                if (Key == "path"sv) {
                  std::string_view Path;
                  if (Element.get(Path)) {
                    return false;
                  }
                  Desc.Path = Path;
                }
                break;
              case 'm':
                if (Key == "major"sv) {
                  if (Element.get(Desc.Major)) {
                    return false;
                  }
                } else if (Key == "minor"sv) {
                  if (Element.get(Desc.Minor)) {
                    return false;
                  }
                }
                break;
              case 'f':
                if (Key == "fileMode"sv) {
                  if (get_int(Element, Desc.FileMode)) {
                    return false;
                  }
                }
                break;
              case 'u':
                if (Key == "uid"sv) {
                  if (get_int(Element, Desc.Uid)) {
                    return false;
                  }
                }
                break;
              case 'g':
                if (Key == "gid"sv) {
                  if (get_int(Element, Desc.Gid)) {
                    return false;
                  }
                }
                break;
              default:
                break;
              }
            }
            this->Devices.push_back(std::move(Desc));
          }
        }
        break;
      case 'c':
        if (Key == "cgroupsPath"sv) {
          std::string_view CgroupsPath;
          if (Element.get(CgroupsPath)) {
            return false;
          }
          this->CgroupsPath = CgroupsPath;
        }
        break;
      case 'r':
        if (Key == "resources"sv) {
          simdjson::dom::object Resources;
          if (Element.get(Resources)) {
            return false;
          }
          auto loadResources = [this](const simdjson::dom::object &Resources) {
            for (const auto &[Key, Element] : Resources) {
              switch (Key[0]) {
              case 'm':
                if (Key == "memory"sv) {
                  simdjson::dom::object Memory;
                  if (Element.get(Memory)) {
                    return false;
                  }
                  for (const auto &[Key, Element] : Memory) {
                    switch (Key[0]) {
                    case 'l':
                      if (Key == "limit"sv) {
                        if (get_int(Element, ResourcesMemoryLimit)) {
                          return false;
                        }
                      }
                      break;
                    case 'r':
                      if (Key == "reservation"sv) {
                        if (get_int(Element, ResourcesMemoryReservation)) {
                          return false;
                        }
                      }
                      break;
                    default:
                      break;
                    }
                  }
                }
                break;
              case 'd':
                if (Key == "devices"sv) {
                }
                break;
              default:
                break;
              }
            }
            return true;
          };
          if (!loadResources(Resources)) {
            return false;
          }
        }
        break;
      default:
        break;
      }
    }
    return true;
  };
#endif

#if defined(RUNW_OS_SOLARIS)
  auto loadSolaris = [](const simdjson::dom::object &Solaris) {
    for (const auto &[Key, Element] : Solaris) {
      switch (Key[0]) {
      default:
        break;
      }
    }
    return true;
  };
#endif

#if defined(RUNW_OS_WINDOWS)
  auto loadWindows = [](const simdjson::dom::object &Windows) {
    for (const auto &[Key, Element] : Windows) {
      switch (Key[0]) {
      default:
        break;
      }
    }
    return true;
  };
#endif

  for (const auto &[Key, Element] : Config) {
    switch (Key[0]) {
    case 'm':
      if (Key == "mounts"sv) {
        simdjson::dom::array Mounts;
        if (Element.get(Mounts)) {
          return false;
        }
        if (!loadMounts(Mounts)) {
          return false;
        }
      }
      break;
    case 'o':
      if (Key == "ociVersion"sv) {
        std::string_view OCIVersion;
        if (Element.get(OCIVersion)) {
          return false;
        }
        this->OCIVersion = OCIVersion;
      }
      break;
    case 'p':
      if (Key == "process"sv) {
        simdjson::dom::object Process;
        if (Element.get(Process)) {
          return false;
        }
        if (!loadProcess(Process)) {
          return false;
        }
      }
      break;
    case 'r':
      if (Key == "root"sv) {
        simdjson::dom::object Root;
        if (Element.get(Root)) {
          return false;
        }
        if (!loadRoot(Root)) {
          return false;
        }
      }
      break;
#if defined(RUNW_OS_LINUX)
    case 'l':
      if (Key == "linux"sv) {
        simdjson::dom::object Linux;
        if (Element.get(Linux)) {
          return false;
        }
        if (!loadLinux(Linux)) {
          return false;
        }
      }
      break;
#endif
#if defined(RUNW_OS_SOLARIS)
    case 's':
      if (Key == "solaris"sv) {
        simdjson::dom::object Solaris;
        if (Element.get(Solaris)) {
          return false;
        }
        if (!loadSolaris(Solaris)) {
          return false;
        }
      }
      break;
#endif
#if defined(RUNW_OS_WINDOWS)
    case 'w':
      if (Key == "windows"sv) {
        simdjson::dom::object Windows;
        if (Element.get(Windows)) {
          return false;
        }
        if (!loadWindows(Windows)) {
          return false;
        }
      }
      break;
#endif
    default:
      break;
    }
  }

  return true;
}

} // namespace RUNW
