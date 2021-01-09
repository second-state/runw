// SPDX-License-Identifier: Apache-2.0

#define ELPP_STL_LOGGING

#include "sdbus.h"
#include <common/log.h>

using namespace std::literals;
using std::experimental::expected;
using std::experimental::unexpected;

namespace RUNW {

expected<SDBus, int> SDBus::defaultUser() noexcept {
  SDBus Bus;
  if (const int Err = sd_bus_default_user(&Bus.Bus); Err < 0) {
    return unexpected(-Err);
  }
  return Bus;
}
expected<SDBus, int> SDBus::defaultSystem() noexcept {
  SDBus Bus;
  if (const int Err = sd_bus_default_system(&Bus.Bus); Err < 0) {
    return unexpected(-Err);
  }
  return Bus;
}

SDBus::~SDBus() noexcept { sd_bus_unref(Bus); }

expected<void, int>
SDBus::matchSignalAsync(const char *Sender, const char *Path,
                        const char *Interface, const char *Member,
                        std::function<int(SDBusMessage &)> &Callback) noexcept {
  if (const int Err = sd_bus_match_signal_async(
          Bus, nullptr, Sender, Path, Interface, Member,
          &SDBus::matchSignalAsyncCallback, nullptr, &Callback);
      Err < 0) {
    return unexpected(-Err);
  }
  return {};
}

int SDBus::matchSignalAsyncCallback(sd_bus_message *Msg, void *UserData,
                                    sd_bus_error *RetError
                                    [[maybe_unused]]) noexcept {
  SDBusMessage Message(Msg);
  auto &Callback = *static_cast<std::function<int(SDBusMessage &)> *>(UserData);
  return Callback(Message);
}

expected<SDBusMessage, int> SDBus::methodCall(const char *Destination,
                                              const char *Path,
                                              const char *Interface,
                                              const char *Member) noexcept {
  sd_bus_message *Msg;
  if (const int Err = sd_bus_message_new_method_call(Bus, &Msg, Destination,
                                                     Path, Interface, Member);
      Err < 0) {
    return unexpected(-Err);
  }
  return SDBusMessage(Msg);
}

expected<SDBusMessage, int> SDBus::call(SDBusMessage Message,
                                        uint64_t USec) noexcept {
  sd_bus_message *Msg = Message.release();
  SDBusError Error;
  if (const int Err = sd_bus_call(Bus, Msg, USec, Error.get(), &Msg); Err < 0) {
    LOG(ERROR) << "call failed:"sv << Error.get()->message;
    return unexpected(Error.getErrNo());
  }
  return SDBusMessage(Msg);
}

expected<bool, int> SDBus::process() noexcept {
  if (const int Err = sd_bus_process(Bus, nullptr); Err < 0) {
    return unexpected(-Err);
  } else {
    return Err > 0;
  }
}

expected<void, int> SDBus::wait(uint64_t TimeoutUSec) noexcept {
  if (const int Err = sd_bus_wait(Bus, TimeoutUSec); Err < 0) {
    return unexpected(-Err);
  }
  return {};
}

SDBusMessage::~SDBusMessage() noexcept { sd_bus_message_unref(Msg); }

expected<void, int> SDBusMessage::openContainer(char Type,
                                                const char *Contents) noexcept {
  if (const int Err = sd_bus_message_open_container(Msg, Type, Contents);
      Err < 0) {
    return unexpected(-Err);
  }
  return {};
}

expected<void, int> SDBusMessage::closeContainer() noexcept {
  if (const int Err = sd_bus_message_close_container(Msg); Err < 0) {
    return unexpected(-Err);
  }
  return {};
}

} // namespace RUNW
