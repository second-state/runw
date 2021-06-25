// SPDX-License-Identifier: Apache-2.0

#include "cgroup.h"
#include "config.h"
#include "defines.h"
#include "state.h"
#include <algorithm>
#include <aot/cache.h>
#include <aot/compiler.h>
#include <boost/scope_exit.hpp>
#include <common/filesystem.h>
#include <common/log.h>
#include <cstdlib>
#include <host/wasi/wasimodule.h>
#include <iostream>
#include <po/argument_parser.h>
#include <po/subcommand.h>
#include <random>
#include <spdlog/sinks/basic_file_sink.h>
#include <vm/vm.h>

#ifdef RUNW_OS_LINUX
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

using namespace std::literals;

std::filesystem::path getTempFilename(const std::filesystem::path &Prefix) {
  std::random_device Device;
  std::default_random_engine Engine(Device());
  std::uniform_int_distribution<char> Distribution('a', 'z');
  while (true) {
    std::array<char, 6> Suffix;
    for (size_t I = 0; I < 6; ++I) {
      Suffix[I] = Distribution(Engine);
    }
    std::filesystem::path Path = Prefix;
    Path.concat(std::string_view(Suffix.data(), Suffix.size()));
    std::ofstream File(Path, std::ios::app);
    if (File.tellp() == 0) {
      return Path;
    }
  }
}

template <typename FuncT>
bool atomicCreateAndWriteFile(const std::filesystem::path &Path, FuncT &&Func) {
  if (std::error_code ErrCode;
      std::filesystem::is_regular_file(Path, ErrCode)) {
    spdlog::error(ErrCode.message());
    return false;
  }

  const auto TempFile = getTempFilename(Path);

  if (std::ofstream Stream(TempFile); !Stream) {
    return false;
  } else {
    Func(Stream);
  }

  if (std::error_code ErrCode;
      std::filesystem::rename(TempFile, Path, ErrCode), ErrCode) {
    spdlog::error(ErrCode.message());
    std::filesystem::remove(TempFile, ErrCode);
    return false;
  }
  return true;
}

template <typename FuncT>
bool atomicUpdateFile(const std::filesystem::path &Path, FuncT &&Func) {
  if (std::error_code ErrCode;
      !std::filesystem::is_regular_file(Path, ErrCode)) {
    spdlog::error(ErrCode.message());
    return false;
  }

  const auto TempFile = getTempFilename(Path);

  if (std::ofstream Stream(TempFile); !Stream) {
    return false;
  } else {
    Func(Stream);
  }

  if (std::error_code ErrCode;
      std::filesystem::rename(TempFile, Path, ErrCode), ErrCode) {
    spdlog::error(ErrCode.message());
    std::filesystem::remove(TempFile, ErrCode);
    return false;
  }
  return true;
}

std::vector<char> readAll(const std::filesystem::path &Path) {
  std::error_code ErrCode;
  if (!std::filesystem::is_regular_file(Path, ErrCode) || ErrCode) {
    return {};
  }

  const auto Size = std::filesystem::file_size(Path, ErrCode);
  if (ErrCode) {
    return {};
  }

  std::ifstream Stream(Path);
  if (!Stream) {
    return {};
  }

  std::vector<char> Buffer(Size);
  Stream.read(Buffer.data(), Size);
  Buffer.resize(Stream.gcount());
  return Buffer;
}

int parseNumeric(std::string_view Name) {
  int Value = 0;
  for (const char C : Name) {
    if (isdigit(C)) {
      Value = Value * 10 + (C - '0');
    } else {
      return -1;
    }
  }

  return Value;
}

int parseSignal(std::string_view Name) {
  if (const int Value = parseNumeric(Name); Value != -1) {
    return Value;
  }

  // Try name parsing
  std::string Upper(Name.size(), '\0');
  std::transform(Name.begin(), Name.end(), Upper.begin(),
                 [](char C) { return std::toupper(C); });

  if (std::string_view(Upper).substr(0, 3) != "SIG"sv) {
    Upper = "SIG"s + Upper;
  }
  static const std::unordered_map<std::string, int> kSignalNames = {
#if defined(RUNW_OS_LINUX)
    {"SIGABRT", SIGABRT},
    {"SIGALRM", SIGALRM},
    {"SIGBUS", SIGBUS},
    {"SIGCHLD", SIGCHLD},
    {"SIGCONT", SIGCONT},
    {"SIGFPE", SIGFPE},
    {"SIGHUP", SIGHUP},
    {"SIGILL", SIGILL},
    {"SIGINT", SIGINT},
    {"SIGIO", SIGIO},
    {"SIGKILL", SIGKILL},
    {"SIGPIPE", SIGPIPE},
    {"SIGPROF", SIGPROF},
    {"SIGPWR", SIGPWR},
    {"SIGQUIT", SIGQUIT},
    {"SIGSEGV", SIGSEGV},
    {"SIGSTKFLT", SIGSTKFLT},
    {"SIGSTOP", SIGSTOP},
    {"SIGSYS", SIGSYS},
    {"SIGTERM", SIGTERM},
    {"SIGTRAP", SIGTRAP},
    {"SIGTSTP", SIGTSTP},
    {"SIGTTIN", SIGTTIN},
    {"SIGTTOU", SIGTTOU},
    {"SIGURG", SIGURG},
    {"SIGUSR1", SIGUSR1},
    {"SIGUSR2", SIGUSR2},
    {"SIGVTALRM", SIGVTALRM},
    {"SIGWINCH", SIGWINCH},
    {"SIGXCPU", SIGXCPU},
    {"SIGXFSZ", SIGXFSZ},
#elif defined(RUNW_OS_MACOS)
    {"SIGABRT", SIGABRT},
    {"SIGALRM", SIGALRM},
    {"SIGBUS", SIGBUS},
    {"SIGCHLD", SIGCHLD},
    {"SIGCONT", SIGCONT},
    {"SIGEMT", SIGEMT},
    {"SIGFPE", SIGFPE},
    {"SIGHUP", SIGHUP},
    {"SIGILL", SIGILL},
    {"SIGINFO", SIGINFO},
    {"SIGINT", SIGINT},
    {"SIGIO", SIGIO},
    {"SIGKILL", SIGKILL},
    {"SIGPIPE", SIGPIPE},
    {"SIGPROF", SIGPROF},
    {"SIGQUIT", SIGQUIT},
    {"SIGSEGV", SIGSEGV},
    {"SIGSTOP", SIGSTOP},
    {"SIGSYS", SIGSYS},
    {"SIGTERM", SIGTERM},
    {"SIGTRAP", SIGTRAP},
    {"SIGTSTP", SIGTSTP},
    {"SIGTTIN", SIGTTIN},
    {"SIGTTOU", SIGTTOU},
    {"SIGURG", SIGURG},
    {"SIGUSR1", SIGUSR1},
    {"SIGUSR2", SIGUSR2},
    {"SIGVTALRM", SIGVTALRM},
    {"SIGWINCH", SIGWINCH},
    {"SIGXCPU", SIGXCPU},
    {"SIGXFSZ", SIGXFSZ},
#elif defined(RUNW_OS_SOLARIS)
    {"SIGALRM", SIGALRM},
    {"SIGBUS", SIGBUS},
    {"SIGCANCEL", SIGCANCEL},
    {"SIGCHLD", SIGCHLD},
    {"SIGCONT", SIGCONT},
    {"SIGEMT", SIGEMT},
    {"SIGFPE", SIGFPE},
    {"SIGFREEZE", SIGFREEZE},
    {"SIGHUP", SIGHUP},
    {"SIGILL", SIGILL},
    {"SIGINT", SIGINT},
    {"SIGJVM1", SIGJVM1},
    {"SIGJVM2", SIGJVM2},
    {"SIGKILL", SIGKILL},
    {"SIGLOST", SIGLOST},
    {"SIGLWP", SIGLWP},
    {"SIGPIPE", SIGPIPE},
    {"SIGPOLL", SIGPOLL},
    {"SIGPROF", SIGPROF},
    {"SIGPWR", SIGPWR},
    {"SIGQUIT", SIGQUIT},
    {"SIGSEGV", SIGSEGV},
    {"SIGSTOP", SIGSTOP},
    {"SIGSYS", SIGSYS},
    {"SIGTERM", SIGTERM},
    {"SIGTHAW", SIGTHAW},
    {"SIGTRAP", SIGTRAP},
    {"SIGTSTP", SIGTSTP},
    {"SIGTTIN", SIGTTIN},
    {"SIGTTOU", SIGTTOU},
    {"SIGURG", SIGURG},
    {"SIGUSR1", SIGUSR1},
    {"SIGUSR2", SIGUSR2},
    {"SIGVTALRM", SIGVTALRM},
    {"SIGWAITING", SIGWAITING},
    {"SIGWINCH", SIGWINCH},
    {"SIGXCPU", SIGXCPU},
    {"SIGXFSZ", SIGXFSZ},
    {"SIGXRES", SIGXRES},
#elif defined(RUNW_OS_WINDOWS)
    {"SIGABRT", SIGABRT},
    {"SIGFPE", SIGFPE},
    {"SIGILL", SIGILL},
    {"SIGINT", SIGINT},
    {"SIGSEGV", SIGSEGV},
    {"SIGTERM", SIGTERM},
#endif
  };

  if (auto Iter = kSignalNames.find(Upper); Iter != kSignalNames.end()) {
    return Iter->second;
  }
  return -1;
}

int doRunInternal(std::string_view ContainerId, std::string_view PidFile,
                  RUNW::State &State, const std::filesystem::path &StateFile,
                  const int ExecFifoFd,
                  const int ConsoleSocketFd [[maybe_unused]]) {
  WasmEdge::Configure Conf;
  Conf.addProposal(WasmEdge::Proposal::BulkMemoryOperations);
  Conf.addProposal(WasmEdge::Proposal::ReferenceTypes);
  Conf.addProposal(WasmEdge::Proposal::SIMD);

  Conf.addHostRegistration(WasmEdge::HostRegistration::Wasi);
  Conf.addHostRegistration(WasmEdge::HostRegistration::WasmEdge_Process);

  WasmEdge::VM::VM VM(Conf);
  WasmEdge::Host::WasiModule *WasiMod =
      dynamic_cast<WasmEdge::Host::WasiModule *>(
          VM.getImportModule(WasmEdge::HostRegistration::Wasi));

  const auto &Bundle = State.bundle();

  auto RootPath = std::filesystem::u8path(Bundle.rootPath());
  auto Cwd = RootPath;
  Cwd += std::filesystem::u8path(Bundle.cwd());
  std::vector<std::string> Args(Bundle.args().begin(), Bundle.args().end());
  std::vector<std::string> Envs(Bundle.envs().begin(), Bundle.envs().end());
  auto WasmPath = Cwd / std::filesystem::u8path(Args[0]);

  spdlog::info("cwd: {}"sv, Cwd);
  spdlog::info("mount: {}"sv, "/:"s + RootPath.u8string());
  spdlog::info("wasm path: {}"sv, WasmPath.u8string());
  spdlog::info("args:"sv);
  for (auto &Arg : Args) {
    spdlog::info("\targ: {}", Arg);
  }
  spdlog::info("envs:"sv);
  for (auto &Env : Envs) {
    spdlog::info("\tenv: {}", Env);
  }

  WasiMod->getEnv().init(
      std::array{"/:"s + RootPath.u8string()}, WasmPath.u8string(),
      WasmEdge::Span<const std::string>(Args).subspan(1), Envs);

  std::filesystem::path SoPath;
  {
    WasmEdge::Loader::Loader Loader(Conf);
    std::vector<WasmEdge::Byte> Data;
    if (auto Res = Loader.loadFile(WasmPath)) {
      Data = std::move(*Res);
    } else {
      const auto Err = static_cast<uint32_t>(Res.error());
      spdlog::info("Load failed. Error code: {}", Err);
      return EXIT_FAILURE;
    }

    if (auto Res = WasmEdge::AOT::Cache::getPath(
            Data, WasmEdge::AOT::Cache::StorageScope::Global, ContainerId)) {
      SoPath = *Res;
      SoPath.replace_extension(std::filesystem::u8path(".so"sv));
    } else {
      const auto Err = static_cast<uint32_t>(Res.error());
      spdlog::info("Cache path get failed. Error code: {}", Err);
      return EXIT_FAILURE;
    }

    if (!std::filesystem::is_regular_file(SoPath)) {
      if (std::error_code ErrCode;
          !std::filesystem::create_directories(SoPath.parent_path(), ErrCode)) {
        spdlog::error(ErrCode.message());
      }

      const pid_t CompilerPid = fork();
      if (WasmEdge::unlikely(CompilerPid < 0)) {
        spdlog::error("fork failed: {}"sv, std::strerror(errno));
        return EXIT_FAILURE;
      }
      if (CompilerPid == 0) {
        std::unique_ptr<WasmEdge::AST::Module> Module;
        if (auto Res = Loader.parseModule(Data)) {
          Module = std::move(*Res);
        } else {
          const auto Err = static_cast<uint32_t>(Res.error());
          spdlog::error("Load failed. Error code: {}", Err);
          exit(EXIT_FAILURE);
        }

        {
          WasmEdge::Validator::Validator ValidatorEngine(Conf);
          if (auto Res = ValidatorEngine.validate(*Module); !Res) {
            const auto Err = static_cast<uint32_t>(Res.error());
            spdlog::error("Validate failed. Error code: {}", Err);
            exit(EXIT_FAILURE);
          }
        }

        WasmEdge::AOT::Compiler Compiler(Conf);
        if (auto Res = Compiler.compile(Data, *Module, SoPath); !Res) {
          const auto Err = static_cast<uint32_t>(Res.error());
          spdlog::error("Compile failed. Error code: {}", Err);
          exit(EXIT_FAILURE);
        }

        exit(EXIT_SUCCESS);
      } else {
        int Status;
        do {
          spdlog::info("wait compiling"sv);
          if (auto Result = waitpid(CompilerPid, &Status, 0); Result < 0) {
            if (errno == EINTR) {
              continue;
            }
          }
        } while (false);
        if (WEXITSTATUS(Status) != EXIT_SUCCESS) {
          spdlog::error("compiling failed, status: {}"sv, Status);
          return EXIT_FAILURE;
        }
      }
    }
  }

  if (auto Res = VM.loadWasm(SoPath); !Res) {
    return EXIT_FAILURE;
  }

  spdlog::info("wasm loaded"sv);

  if (auto Res = VM.validate(); !Res) {
    return EXIT_FAILURE;
  }

  spdlog::info("wasm validated"sv);

  if (auto Res = VM.instantiate(); !Res) {
    return EXIT_FAILURE;
  }

  spdlog::info("wasm instantiate"sv);

  const pid_t WasmPid = fork();
  if (WasmEdge::unlikely(WasmPid < 0)) {
    spdlog::error("fork failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  if (WasmPid > 0) {
    return EXIT_SUCCESS;
  }

  if (!atomicCreateAndWriteFile(std::filesystem::u8path(PidFile),
                                [](auto &Stream) { Stream << getpid(); })) {
    spdlog::error("pid file update failed"sv);
    return EXIT_FAILURE;
  }

  State.setCreated();
  {
    int UnshareFlags = 0;
    std::vector<std::pair<int, int>> SetNsFlags;
    auto &&UpdateFlags = [&UnshareFlags, &SetNsFlags](
                             const int Flag, const std::string &Path) noexcept {
      if (Path.empty()) {
        UnshareFlags |= Flag;
        return true;
      }
      int Fd = open(Path.c_str(), O_RDONLY);
      if (Fd < 0) {
        spdlog::error("open {}:{}"sv, Path, std::strerror(errno));
        return false;
      }
      SetNsFlags.emplace_back(Fd, Flag);
      return true;
    };
    for (const auto &Desc : Bundle.linuxNamespaces()) {
      switch (Desc.Type.front()) {
#if defined(CLONE_NEWCGROUP)
      case 'c':
        if (Desc.Type == "cgroup"sv) {
          if (!UpdateFlags(CLONE_NEWCGROUP, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
#endif
      case 'i':
        if (Desc.Type == "ipc"sv) {
          if (!UpdateFlags(CLONE_NEWIPC, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
      case 'm':
        if (Desc.Type == "mount"sv) {
          if (!UpdateFlags(CLONE_NEWNS, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
      case 'n':
        if (Desc.Type == "network"sv) {
          if (!UpdateFlags(CLONE_NEWNET, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
      case 'p':
        if (Desc.Type == "pid"sv) {
          if (!UpdateFlags(CLONE_NEWPID, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
#if defined(CLONE_NEWTIME)
      case 't':
        if (Desc.Type == "time"sv) {
          if (!UpdateFlags(CLONE_NEWTIME, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
#endif
      case 'u':
        if (Desc.Type == "uts"sv) {
          if (!UpdateFlags(CLONE_NEWUTS, Desc.Path)) {
            return EXIT_FAILURE;
          }
        } else if (Desc.Type == "user"sv) {
          if (!UpdateFlags(CLONE_NEWUSER, Desc.Path)) {
            return EXIT_FAILURE;
          }
        }
        break;
      }
    }
    if (UnshareFlags != 0) {
      unshare(UnshareFlags);
    }
    bool SetNsFailed = false;
    for (const auto &[Fd, Flag] : SetNsFlags) {
      if (int Ret = setns(Fd, Flag); Ret < 0) {
        spdlog::error("cannot setns: {}"sv, std::strerror(errno));
        SetNsFailed = true;
      }
      close(Fd);
    }
    if (SetNsFailed) {
      return EXIT_FAILURE;
    }
  }
  if (auto Res = RUNW::CGroup::enter(ContainerId, State); !Res) {
    return EXIT_FAILURE;
  }
  if (!atomicUpdateFile(StateFile,
                        [&](auto &Stream) { State.print(Stream); })) {
    spdlog::error("state file update failed"sv);
    return EXIT_FAILURE;
  }

  if (ExecFifoFd >= 0) {
    char Buffer[1];
    fd_set ReadSet;

    FD_ZERO(&ReadSet);
    FD_SET(ExecFifoFd, &ReadSet);
    do {
      if (int Ret = select(ExecFifoFd + 1, &ReadSet, NULL, NULL, NULL);
          Ret < 0) {
        spdlog::error("select exec fifo failed: {}"sv, std::strerror(errno));
        return EXIT_FAILURE;
      }

      do {
        if (int Ret = read(ExecFifoFd, Buffer, sizeof(Buffer)); Ret < 0) {
          if (errno == EINTR) {
            continue;
          }
          spdlog::error("read exec fifo failed: {}"sv, std::strerror(errno));
          return EXIT_FAILURE;
        }
      } while (false);
    } while (false);
  }

  State.setRunning();
  if (!atomicUpdateFile(StateFile,
                        [&](auto &Stream) { State.print(Stream); })) {
    return EXIT_FAILURE;
  }

  spdlog::info("wasm running"sv);

  auto Res = VM.execute("_start"sv);
  if (!Res) {
    spdlog::error("execute failed: {}"sv, WasmEdge::ErrCodeStr[Res.error()]);
  }

  spdlog::info("wasm stopped"sv);

  const int ExitCode = Res ? WasiMod->getEnv().getExitCode() : EXIT_FAILURE;
  State.setStopped(ExitCode);

  if (!atomicUpdateFile(StateFile,
                        [&](auto &Stream) { State.print(Stream); })) {
    return EXIT_FAILURE;
  }

  return ExitCode;
}

int doCreate(std::string_view Root, bool SystemdCgroup [[maybe_unused]],
             std::string_view ConfigFileName, std::string_view ContainerId,
             std::string_view Path, std::string_view ConsoleSocket,
             std::string_view PidFile) {
  const auto ContainerRoot = std::filesystem::u8path(Root) / ContainerId;
  if (std::error_code ErrCode;
      !std::filesystem::create_directories(ContainerRoot, ErrCode)) {
    spdlog::error(ErrCode.message());
    return EXIT_FAILURE;
  }

  bool Success = false;
  BOOST_SCOPE_EXIT_ALL(&) {
    if (!Success) {
      std::error_code ErrCode;
      std::filesystem::remove_all(ContainerRoot, ErrCode);
    }
  };

  const auto StateFile = ContainerRoot / "state.json"sv;
  RUNW::State State(ContainerId, Path);
  if (!State.loadBundle(ConfigFileName)) {
    spdlog::error("load bundle failed"sv);
    return EXIT_FAILURE;
  }

  State.setSystemdCgroup(SystemdCgroup);

  State.setCreating();
  if (!atomicCreateAndWriteFile(StateFile,
                                [&](auto &Stream) { State.print(Stream); })) {
    spdlog::error("state file update failed"sv);
    return EXIT_FAILURE;
  }

  const auto ExecFifoFile = ContainerRoot / "exec.fifo"sv;
  if (int Ret = mkfifo(ExecFifoFile.u8string().c_str(), 0600); Ret < 0) {
    spdlog::error("mkfifo failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  int ExecFifoFd = open(ExecFifoFile.u8string().c_str(), O_RDONLY | O_NONBLOCK);
  BOOST_SCOPE_EXIT_ALL(&ExecFifoFd) {
    if (ExecFifoFd >= 0) {
      close(ExecFifoFd);
    }
  };
  if (ExecFifoFd < 0) {
    spdlog::error("open fifo failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  int Pipe[2];
  if (WasmEdge::unlikely(pipe(Pipe) < 0)) {
    spdlog::error("pipe failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }
  const pid_t ChildPid = fork();
  if (WasmEdge::unlikely(ChildPid < 0)) {
    spdlog::error("fork failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }
  if (ChildPid > 0) {
    // server
    close(Pipe[1]);
    do {
      if (auto Result = waitpid(ChildPid, nullptr, 0); Result < 0) {
        if (errno == EINTR) {
          continue;
        }
      }
    } while (false);
    int ExitCode;
    do {
      if (auto Result = read(Pipe[0], &ExitCode, sizeof(ExitCode));
          Result < 0) {
        if (errno == EINTR) {
          continue;
        }
        spdlog::error("parent read failed: {}"sv, std::strerror(errno));
        return EXIT_FAILURE;
      } else if (Result > 0) {
        if (ExitCode != 0) {
          return EXIT_FAILURE;
        }
      }
    } while (false);

    close(Pipe[0]);
    Success = true;
    return EXIT_SUCCESS;
  }

  // child
  close(Pipe[0]);
  if (setsid() < 0) {
    return EXIT_FAILURE;
  }
  pid_t DaemonPid = fork();
  if (DaemonPid < 0) {
    return EXIT_FAILURE;
  }
  if (DaemonPid > 0) {
    // child
    Success = true;
    _Exit(EXIT_SUCCESS);
  }

  // daemon

  int ConsoleSocketFd = -1;
  BOOST_SCOPE_EXIT_ALL(&ConsoleSocketFd) {
    if (ConsoleSocketFd >= 0) {
      close(ConsoleSocketFd);
    }
  };
  if (!ConsoleSocket.empty()) {
    ConsoleSocketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ConsoleSocketFd < 0) {
      spdlog::error("socket failed: {}"sv, std::strerror(errno));
      return EXIT_FAILURE;
    }
    struct sockaddr_un Addr = {};
    if (ConsoleSocket.size() >= sizeof(Addr.sun_path)) {
      spdlog::error("socket path too long: {}"sv, ConsoleSocket);
      return EXIT_FAILURE;
    }
    std::copy(ConsoleSocket.begin(), ConsoleSocket.end(), Addr.sun_path);
    Addr.sun_family = AF_UNIX;
    if (auto Ret =
            connect(ConsoleSocketFd, reinterpret_cast<struct sockaddr *>(&Addr),
                    sizeof(Addr));
        Ret < 0) {
      spdlog::error("socket connect failed: {}"sv, std::strerror(errno));
      return EXIT_FAILURE;
    }
  }

  {
    int ExitCode = doRunInternal(ContainerId, PidFile, State, StateFile,
                                 ExecFifoFd, ConsoleSocketFd);
    write(Pipe[1], &ExitCode, sizeof(ExitCode));
    close(Pipe[1]);
  }

  Success = true;
  return EXIT_FAILURE;
}

int doDelete(std::string_view Root, std::string_view ContainerId, bool Force) {
  const auto ContainerRoot = std::filesystem::u8path(Root) / ContainerId;

  if (std::error_code ErrCode;
      !std::filesystem::is_directory(ContainerRoot, ErrCode)) {
    spdlog::error(ErrCode.message());
    if (!Force) {
      return EXIT_FAILURE;
    }
  }

  if (std::error_code ErrCode;
      std::filesystem::remove_all(ContainerRoot, ErrCode), ErrCode) {
    spdlog::error(ErrCode.message());
    if (!Force) {
      return EXIT_FAILURE;
    }
  }

  WasmEdge::AOT::Cache::clear(WasmEdge::AOT::Cache::StorageScope::Global,
                              ContainerId);

  return EXIT_SUCCESS;
}

int doKill(std::string_view Root, std::string_view ConfigFileName,
           std::string_view ContainerId, std::string_view SignalName) {
  const int Signal = parseSignal(SignalName);
  if (Signal < 0) {
    return EXIT_FAILURE;
  }

  const auto ContainerRoot = std::filesystem::u8path(Root) / ContainerId;
  const auto StateFile = ContainerRoot / "state.json"sv;
  if (std::error_code ErrCode;
      !std::filesystem::is_regular_file(StateFile, ErrCode)) {
    return EXIT_FAILURE;
  }

  RUNW::State State;
  if (!State.load(StateFile, ConfigFileName)) {
    return EXIT_FAILURE;
  }

  const pid_t PidValue = State.getPid();
  if (PidValue < 0) {
    return EXIT_FAILURE;
  }

#if defined(RUNW_OS_LINUX) || defined(RUNW_OS_MACOS) || defined(RUNW_OS_SOLARIS)
  if (::kill(PidValue, Signal) != 0) {
    return EXIT_FAILURE;
  }
#elif defined(RUNW_OS_WINDOWS)
  // TODO: Support signal
  return EXIT_FAILURE;
#endif

  return EXIT_SUCCESS;
}

int doStart(std::string_view Root, std::string_view ConfigFileName,
            std::string_view ContainerId) {
  const auto ContainerRoot = std::filesystem::u8path(Root) / ContainerId;
  const auto StateFile = ContainerRoot / "state.json"sv;
  if (std::error_code ErrCode;
      !std::filesystem::is_regular_file(StateFile, ErrCode)) {
    spdlog::error(ErrCode.message());
    return EXIT_FAILURE;
  }

  RUNW::State State;
  if (!State.load(StateFile, ConfigFileName)) {
    return EXIT_FAILURE;
  }

  const auto ExecFifoFile = ContainerRoot / "exec.fifo"sv;

  int ExecFifoFd = open(ExecFifoFile.u8string().c_str(), O_WRONLY | O_NONBLOCK);
  BOOST_SCOPE_EXIT_ALL(&ExecFifoFd) {
    if (ExecFifoFd >= 0) {
      close(ExecFifoFd);
    }
  };
  if (ExecFifoFd < 0) {
    spdlog::error("open fifo failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  if (int Ret = unlink(ExecFifoFile.u8string().c_str()); Ret < 0) {
    spdlog::error("unlink exec fifo failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  char Buffer[1] = {};
  if (int Ret = write(ExecFifoFd, Buffer, sizeof(Buffer)); Ret < 0) {
    spdlog::error("read exec fifo failed: {}"sv, std::strerror(errno));
    return EXIT_FAILURE;
  }

  close(ExecFifoFd);

  return EXIT_SUCCESS;
}

int doState(std::string_view Root, std::string_view ContainerId) {
  const auto ContainerRoot = std::filesystem::u8path(Root) / ContainerId;
  if (std::error_code ErrCode;
      !std::filesystem::is_directory(ContainerRoot, ErrCode)) {
    spdlog::error(ErrCode.message());
    return EXIT_FAILURE;
  }

  const auto StateFile = ContainerRoot / "state.json"sv;
  auto Data = readAll(StateFile);
  if (Data.empty()) {
    return EXIT_FAILURE;
  }
  std::cout << std::string_view(Data.data(), Data.size()) << std::endl;

  return EXIT_SUCCESS;
}

} // namespace

int main(int Argc, const char *Argv[]) {
  namespace PO = WasmEdge::PO;

  std::ios::sync_with_stdio(false);
  auto FileLogger = spdlog::basic_logger_mt("file_logger", "/tmp/runw.log");
  spdlog::set_default_logger(FileLogger);
  WasmEdge::Log::setDebugLoggingLevel();

  {
    auto MainArgs = std::vector(Argv, Argv + Argc);
    spdlog::info("MainArgs:"sv);
    for (auto &Arg : MainArgs) {
      spdlog::info("\tArg: {}", Arg);
    }
  }

  PO::SubCommand Create(PO::Description("Create a container"sv));
  PO::SubCommand Delete(
      PO::Description("Delete any resources held by the container"sv));
  PO::SubCommand Kill(PO::Description(
      "Kill sends the specified signal to the container's init process"sv));
  PO::SubCommand Start(PO::Description(
      "Executes the user defined process in a created container"sv));
  PO::SubCommand State(PO::Description("Output the state of a container"sv));

  PO::Option<std::string> Root(
      PO::Description("Root path"sv), PO::MetaVar("PATH"sv),
      PO::DefaultValue<std::string>(std::string(RUNW::kContainerDir)));
  PO::Option<PO::Toggle> SystemdCgroup(PO::Description(
      "enable systemd cgroup support, expects cgroupsPath to be of form "
      "\"slice:prefix:name\" for e.g. \"system.slice:runc:434234\""sv));
  PO::Option<std::string> ConfigFileName(
      PO::Description("Override the config file name"sv),
      PO::MetaVar("FILENAME"sv), PO::DefaultValue<std::string>("config.json"s));

  PO::Option<std::string> ContainerId(PO::Description("Container ID"sv),
                                      PO::MetaVar("ID"sv));
  PO::Option<std::string> Path(
      PO::Description("Path to the root of the bundle directory, defaults to "
                      "the current directory"sv),
      PO::MetaVar("PATH"sv));
  PO::Option<std::string> ConsoleSocket(
      PO::Description(
          "Path to an AF_UNIX socket which will receive a file descriptor "
          "referencing the master end of the console's pseudoterminal"sv),
      PO::MetaVar("FD"sv), PO::DefaultValue<std::string>({}));
  PO::Option<std::string> PidFile(
      PO::Description("Specify the file to write the process id to"sv),
      PO::MetaVar("PATH"sv));

  PO::Option<PO::Toggle> Force(PO::Description(
      "Forcibly deletes the container if it is still running (uses SIGKILL)"sv));

  PO::Option<std::string> Signal(PO::Description("Signal name"sv),
                                 PO::DefaultValue<std::string>("SIGTERM"s),
                                 PO::MetaVar("SIGNAL"sv));

  auto Parser = PO::ArgumentParser();
  if (!Parser.add_option("root"sv, Root)
           .add_option("systemd-cgroup"sv, SystemdCgroup)
           .add_option("config"sv, ConfigFileName)
           .begin_subcommand(Create, "create"sv)
           .add_option(ContainerId)
           .add_option("bundle"sv, Path)
           .add_option("console-socket"sv, ConsoleSocket)
           .add_option("pid-file"sv, PidFile)
           .end_subcommand()
           .begin_subcommand(Delete, "delete"sv)
           .add_option(ContainerId)
           .add_option("force"sv, Force)
           .end_subcommand()
           .begin_subcommand(Kill, "kill"sv)
           .add_option(ContainerId)
           .add_option(Signal)
           .end_subcommand()
           .begin_subcommand(Start, "start"sv)
           .add_option(ContainerId)
           .end_subcommand()
           .begin_subcommand(State, "state"sv)
           .add_option(ContainerId)
           .end_subcommand()
           .parse(Argc, Argv)) {
    return EXIT_FAILURE;
  }

  if (Parser.isVersion()) {
    std::cout << Argv[0] << " version "sv << RUNW::kVersionString << '\n';
    return EXIT_SUCCESS;
  }

  if (ConfigFileName.value().empty()) {
    ConfigFileName.default_argument();
  }

  if (Start.is_selected()) {
    return doStart(Root.value(), ConfigFileName.value(), ContainerId.value());
  } else if (Create.is_selected()) {
    return doCreate(Root.value(), SystemdCgroup.value(), ConfigFileName.value(),
                    ContainerId.value(), Path.value(), ConsoleSocket.value(),
                    PidFile.value());
  } else if (Delete.is_selected()) {
    return doDelete(Root.value(), ContainerId.value(), Force.value());
  } else if (Kill.is_selected()) {
    return doKill(Root.value(), ConfigFileName.value(), ContainerId.value(),
                  Signal.value());
  } else if (State.is_selected()) {
    return doState(Root.value(), ContainerId.value());
  }

  Parser.help();
  return EXIT_FAILURE;
}
