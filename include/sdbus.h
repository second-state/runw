// SPDX-License-Identifier: Apache-2.0

#include <experimental/expected.hpp>
#include <functional>
#include <systemd/sd-bus.h>

namespace RUNW {

class SDBusMessage;

class SDBus {
public:
  constexpr SDBus() noexcept = default;
  constexpr SDBus(sd_bus *Bus) noexcept : Bus(Bus) {}
  SDBus(const SDBus &RHS) noexcept = delete;
  SDBus &operator=(const SDBus &RHS) noexcept = delete;
  SDBus(SDBus &&RHS) noexcept : Bus(std::exchange(RHS.Bus, nullptr)) {}
  SDBus &operator=(SDBus &&RHS) noexcept {
    std::swap(Bus, RHS.Bus);
    return *this;
  }

  ~SDBus() noexcept;
  static cxx20::expected<SDBus, int> defaultUser() noexcept;
  static cxx20::expected<SDBus, int> defaultSystem() noexcept;

  cxx20::expected<void, int>
  matchSignalAsync(const char *Sender, const char *Path, const char *Interface,
                   const char *Member,
                   std::function<int(SDBusMessage &)> &Callback) noexcept;

  cxx20::expected<SDBusMessage, int> methodCall(const char *Destination,
                                                const char *Path,
                                                const char *Interface,
                                                const char *Member) noexcept;

  cxx20::expected<SDBusMessage, int> call(SDBusMessage Message,
                                          uint64_t USec) noexcept;

  cxx20::expected<bool, int> process() noexcept;

  cxx20::expected<void, int> wait(uint64_t TimeoutUSec) noexcept;

private:
  static int matchSignalAsyncCallback(sd_bus_message *m, void *userdata,
                                      sd_bus_error *ret_error) noexcept;

  sd_bus *Bus = nullptr;
};

class SDBusMessage {
public:
  constexpr SDBusMessage() noexcept = default;
  constexpr SDBusMessage(sd_bus_message *Msg) noexcept : Msg(Msg) {}
  SDBusMessage(const SDBusMessage &RHS) noexcept = delete;
  SDBusMessage &operator=(const SDBusMessage &RHS) noexcept = delete;
  SDBusMessage(SDBusMessage &&RHS) noexcept
      : Msg(std::exchange(RHS.Msg, nullptr)) {}
  SDBusMessage &operator=(SDBusMessage &&RHS) noexcept {
    std::swap(Msg, RHS.Msg);
    return *this;
  }
  ~SDBusMessage() noexcept;

  template <typename... ArgsT>
  cxx20::expected<void, int> read(const char *Types, ArgsT &...Args) noexcept {
    if (const int Err = sd_bus_message_read(Msg, Types, &Args...); Err < 0) {
      return cxx20::unexpected(-Err);
    }
    return {};
  }

  template <typename... ArgsT>
  cxx20::expected<void, int> append(const char *Types, ArgsT... Args) noexcept {
    if (const int Err = sd_bus_message_append(Msg, Types, Args...); Err < 0) {
      return cxx20::unexpected(-Err);
    }
    return {};
  }

  cxx20::expected<void, int> openContainer(char Type,
                                           const char *Contents) noexcept;

  cxx20::expected<void, int> closeContainer() noexcept;

  sd_bus_message *release() noexcept { return std::exchange(Msg, nullptr); }

private:
  sd_bus_message *Msg = nullptr;
};

class SDBusError {
public:
  SDBusError() noexcept = default;
  SDBusError(const SDBusError &) = delete;
  SDBusError(SDBusError &&) = delete;
  SDBusError &operator=(const SDBusError &) = delete;
  SDBusError &operator=(SDBusError &&) = delete;
  ~SDBusError() noexcept { sd_bus_error_free(&Error); }

  sd_bus_error *get() noexcept { return &Error; }

  int getErrNo() const noexcept { return sd_bus_error_get_errno(&Error); }

private:
  sd_bus_error Error{};
};

} // namespace RUNW
