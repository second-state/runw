// SPDX-License-Identifier: Apache-2.0

#include "state.h"
#include <simdjson.h>

using namespace std::literals;

namespace {

std::string jsonEscape(std::string_view String) {
  std::vector<char> Buffer;
  Buffer.reserve(String.size() * 2);
  for (char C : String) {
    switch (C) {
    case '\b':
      Buffer.push_back('\\');
      Buffer.push_back('b');
      break;
    case '\f':
      Buffer.push_back('\\');
      Buffer.push_back('f');
      break;
    case '\n':
      Buffer.push_back('\\');
      Buffer.push_back('n');
      break;
    case '\r':
      Buffer.push_back('\\');
      Buffer.push_back('r');
      break;
    case '\t':
      Buffer.push_back('\\');
      Buffer.push_back('t');
      break;
    case '"':
      Buffer.push_back('\\');
      Buffer.push_back('"');
      break;
    case '\\':
      Buffer.push_back('\\');
      Buffer.push_back('\\');
      break;
    default:
      Buffer.push_back(C);
    }
  }
  return std::string(Buffer.data(), Buffer.size());
}

RUNW::State::StatusCode parseStatus(std::string_view StatusString) {
  do {
    if (StatusString.size() < 6) {
      break;
    }
    switch (StatusString[0] + StatusString[5]) {
    case 'c' + 'i':
      if (StatusString == RUNW::State::kStatusCreating) {
        return RUNW::State::StatusCode::Creating;
      }
      break;
    case 'c' + 'e':
      if (StatusString == RUNW::State::kStatusCreated) {
        return RUNW::State::StatusCode::Created;
      }
      break;
    case 'r' + 'n':
      if (StatusString == RUNW::State::kStatusRunning) {
        return RUNW::State::StatusCode::Running;
      }
      break;
    case 's' + 'e':
      if (StatusString == RUNW::State::kStatusStopped) {
        return RUNW::State::StatusCode::Stopped;
      }
      break;
    default:
      break;
    }
  } while (false);
  return RUNW::State::StatusCode::Unknown;
}

} // namespace

namespace RUNW {

const std::string_view State::kOCIVersion = "1.0.2"sv;
const std::string_view State::kStatusUnknown = "unknown"sv;
const std::string_view State::kStatusCreating = "creating"sv;
const std::string_view State::kStatusCreated = "created"sv;
const std::string_view State::kStatusRunning = "running"sv;
const std::string_view State::kStatusStopped = "stopped"sv;

bool State::load(const std::filesystem::path &Path,
                 std::string_view ConfigFileName) {
  simdjson::dom::parser Parser;

  simdjson::dom::element State;
  if (auto Error = Parser.load(Path).get(State); Error) {
    return false;
  }

  {
    // Read and verify version
    std::string_view OCIVersion;
    if (auto Error = State["ociVersion"sv].get(OCIVersion); Error) {
      return false;
    }
    if (OCIVersion != kOCIVersion) {
      return false;
    }
  }

  {
    // Read and verify status
    std::string_view StatusString;
    if (auto Error = State["status"sv].get(StatusString); Error) {
      return false;
    }
    Status = parseStatus(StatusString);
    if (Status == StatusCode::Unknown) {
      return false;
    }
  }

  // Read id
  std::string_view String;
  if (auto Error = State["id"sv].get(String); Error) {
    return false;
  }
  ContainerId = String;

  // Read bundle
  if (auto Error = State["bundle"sv].get(String); Error) {
    return false;
  }
  BundlePath = String;

  // Read systemd_cgroup
  if (auto Error = State["systemd-cgroup"sv].get(SystemdCgroup); Error) {
    return false;
  }

  if (Status == StatusCode::Created || Status == StatusCode::Running) {
    int64_t Integer;
    if (auto Error = State["pid"sv].get(Integer); Error) {
      return false;
    }
    Pid = Integer;
  }

  if (Status == StatusCode::Created || Status == StatusCode::Running ||
      Status == StatusCode::Stopped) {
    std::string_view Created;
    if (auto Error = State["created"sv].get(Created); Error) {
      return false;
    }
    CreatedTimestamp = Created;
  }

  if (Status == StatusCode::Running || Status == StatusCode::Stopped) {
    std::string_view Started;
    if (auto Error = State["started"sv].get(Started); Error) {
      return false;
    }
    StartedTimestamp = Started;
  }

  if (Status == StatusCode::Stopped) {
    int64_t Integer;
    if (auto Error = State["exitCode"sv].get(Integer); Error) {
      return false;
    }
    ExitCode = Integer;

    std::string_view Finished;
    if (auto Error = State["finished"sv].get(Finished); Error) {
      return false;
    }
    FinishedTimestamp = Finished;
  }

  if (!loadBundle(ConfigFileName)) {
    return false;
  }

  return true;
}

bool State::loadBundle(std::string_view ConfigFileName) {
  return Config.load(std::filesystem::u8path(BundlePath), ConfigFileName);
}

void State::print(std::ostream &Stream) const {
  Stream << R"({"ociVersion":")"sv << kOCIVersion << R"(","id":")"sv
         << jsonEscape(ContainerId) << R"(","status":")"sv << getStatusString()
         << R"(","bundle":")"sv << jsonEscape(BundlePath)
         << R"(","systemd-cgroup":)"sv << std::boolalpha << SystemdCgroup;
  if (Status == StatusCode::Created || Status == StatusCode::Running) {
    Stream << R"(,"pid":)"sv << Pid;
  }
  if (Status == StatusCode::Created || Status == StatusCode::Running ||
      Status == StatusCode::Stopped) {
    Stream << R"(,"created":")"sv << CreatedTimestamp << '"';
  }
  if (Status == StatusCode::Running || Status == StatusCode::Stopped) {
    Stream << R"(,"started":")"sv << StartedTimestamp << '"';
  }
  if (Status == StatusCode::Stopped) {
    Stream << R"(,"exitCode":)"sv << ExitCode << R"(,"finished":")"sv
           << FinishedTimestamp << '"';
  }
  Stream << "}\n"sv;
}

void State::setCreating() noexcept { Status = StatusCode::Creating; }

void State::setCreated() noexcept {
  Status = StatusCode::Created;
  {
    char Buffer[64];
    std::time_t Now = std::time(NULL);
    const auto Size = std::strftime(
        Buffer, sizeof(Buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&Now));
    CreatedTimestamp.assign(Buffer, Size);
  }
#if defined(RUNW_OS_LINUX) || defined(RUNW_OS_MACOS)
  Pid = getpid();
#elif defined(RUNW_OS_WINDOWS)
  Pid = _getpid();
#else
#error unknown platform
#endif
}

void State::setRunning() noexcept {
  Status = StatusCode::Running;
  {
    char Buffer[64];
    std::time_t Now = std::time(NULL);
    const auto Size = std::strftime(
        Buffer, sizeof(Buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&Now));
    StartedTimestamp.assign(Buffer, Size);
  }
}

void State::setStopped(int ExitCode) noexcept {
  Status = StatusCode::Stopped;
  this->ExitCode = ExitCode;
  {
    char Buffer[64];
    std::time_t Now = std::time(NULL);
    const auto Size = std::strftime(
        Buffer, sizeof(Buffer), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&Now));
    FinishedTimestamp.assign(Buffer, Size);
  }
}

} // namespace RUNW
