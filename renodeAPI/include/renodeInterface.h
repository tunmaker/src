// renodeInterface.h
#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "defs.h"

namespace renode {
// Forward declarations
class ExternalControlClient;
class AMachine;
class Adc;
class Gpio;
class SysBus;
class BusContext;

// Lightweight recoverable error object (returned by methods that don't throw)
struct Error {
  int code = 0;
  std::string message;
  explicit operator bool() const noexcept { return code != 0; }
};

// Fatal exception for unrecoverable errors
class RenodeException : public std::runtime_error {
public:
  explicit RenodeException(const std::string &msg) : std::runtime_error(msg) {}
};

// Result<T>: simple non-throwing holder for value + Error
template <typename T> struct Result {
  T value;
  Error error;
  explicit operator bool() const noexcept { return !error; }
};

// Void specialization
template <> struct Result<void> {
  Error error;
  explicit operator bool() const noexcept { return !error; }
};

static int sock_fd_;

class ExternalControlClient {
public:
  // Non-copyable
  ExternalControlClient(const ExternalControlClient &) = delete;
  ExternalControlClient &operator=(const ExternalControlClient &) = delete;

  // Movable
  ExternalControlClient(ExternalControlClient &&) noexcept;
  ExternalControlClient &operator=(ExternalControlClient &&) noexcept;

  // Destructor will close connection and free resources.
  ~ExternalControlClient();
  struct Impl;

  // Connect to renode server on host:port. Throws RenodeException on failure.
  static std::unique_ptr<ExternalControlClient>
  connect(const std::string &host = "127.0.0.1", uint16_t port = 5555);

  // Disconnect explicitly, Destructor will disconnect.
  void disconnect() noexcept;

  // Handshake: vector of (commandId, version)
  bool performHandshake();

  // Get machine by name. Returns nullptr if not found (no exception); error
  // populated in err.
  std::shared_ptr<AMachine> getMachine(const std::string &name,
                                       Error &err) noexcept;

  // Get machine or throw if not found.
  std::shared_ptr<AMachine> getMachineOrThrow(const std::string &name);

  // Run emulation for `duration` in given unit. Returns Error on failure.
  Error runFor(uint64_t duration, TimeUnit unit) noexcept;

  // Async runFor — returns future with Error.
  std::future<Error> asyncRunFor(uint64_t duration, TimeUnit unit);

  // Get current emulation time in microseconds. Returns std::nullopt on error.
  std::optional<uint64_t> getCurrentTimeMicroseconds(Error &err) noexcept;

  // Get current emulation time with unit conversion helper
  Result<uint64_t> getCurrentTime(uint64_t &outValue, TimeUnit unit) noexcept;
  
private:
  void send_bytes(const uint8_t *data, size_t len);

  std::vector<uint8_t> recv_response(ApiCommand expected_command);
  // Send a command and receive the payload bytes (empty vector if none or on
  // error)
  std::vector<uint8_t> send_command(ApiCommand commandId,
                                    const std::vector<uint8_t> &payload);
  static std::string bytes_to_string(const std::vector<uint8_t> &v);

private:
  std::unique_ptr<Impl> pimpl_;
  explicit ExternalControlClient(std::unique_ptr<Impl> impl) noexcept;
};

class AMachine : public std::enable_shared_from_this<AMachine> {
public:
  ~AMachine();
  struct Impl;

  // Identification & metadata
  std::string id() const noexcept;
  std::string path() const noexcept;
  std::string name() const noexcept;

  // Get tag or metadata value; returns empty optional if not present
  std::optional<std::string> metadata(const std::string &key) const noexcept;
  // Set metadata key/value (non-throwing)
  Error setMetadata(const std::string &key, const std::string &value) noexcept;

  // Lifecycle controls
  Error loadConfiguration(const std::string &config) noexcept;
  Error reset() noexcept;
  Error pause() noexcept;
  Error resume() noexcept;
  Result<bool> isRunning() const noexcept; // bool:true = running

  // Ownership / query
  Result<std::vector<PeripheralDescriptor>> listPeripherals() noexcept;

  // Templated peripheral getter:
  // - T must be one of Adc, Gpio, SysBus, etc.
  template <typename T>
  Result<std::shared_ptr<T>> getPeripheral(const std::string &path) noexcept {
    static_assert(std::is_same<T, Adc>::value || std::is_same<T, Gpio>::value ||
                      std::is_same<T, SysBus>::value,
                  "getPeripheral<T>: unsupported peripheral type");
    Error err;
    if constexpr (std::is_same<T, Adc>::value) {
      auto p = getAdc(path, err);
      return {p, err};
    } else if constexpr (std::is_same<T, Gpio>::value) {
      auto p = getGpio(path, err);
      return {p, err};
    } else { // SysBus
      auto p = getSysBus(path, err);
      return {p, err};
    }
  }

  // Non-templated accessors (used internally / for explicit API)
  std::shared_ptr<Adc> getAdc(const std::string &path, Error &err) noexcept;
  std::shared_ptr<Gpio> getGpio(const std::string &path, Error &err) noexcept;
  std::shared_ptr<SysBus> getSysBus(const std::string &path,
                                    Error &err) noexcept;

  // Synchronous vs async time controls
  Error runFor(uint64_t duration, TimeUnit unit) noexcept;
  std::future<Error> asyncRunFor(uint64_t duration, TimeUnit unit);

  // Time conveniences
  Error runUntil(uint64_t timestampMicroseconds) noexcept; // run until absolute
                                                           // simulation time
  Error stepInstructions(
      uint64_t count) noexcept; // step N instructions on CPU (if supported)
  Result<uint64_t> getTime(TimeUnit unit) const noexcept;

  // Convenience: boolean validity
  explicit operator bool() const noexcept;

private:
  std::unique_ptr<Impl> pimpl_;
  explicit AMachine(std::unique_ptr<Impl> impl) noexcept;

  friend class ExternalControlClient;
};

// Adc: per-machine ADC peripheral
class Adc {
public:
  ~Adc();

  // Get number of channels
  Error getChannelCount(int &outCount) noexcept;

  // Read channel value
  Error getChannelValue(int channel, AdcValue &outValue) noexcept;

  // Set channel value (inject)
  Error setChannelValue(int channel, AdcValue value) noexcept;

  explicit operator bool() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  explicit Adc(std::unique_ptr<Impl> impl) noexcept;

  friend class AMachine;
};

// Gpio: per-machine GPIO peripheral
class Gpio {
public:
  ~Gpio();

  Error getState(int pin, GpioState &outState) noexcept;
  Error setState(int pin, GpioState state) noexcept;

  // Register callback; returns a handle id to later unregister. Callback
  // invoked on state change.
  Error registerStateChangeCallback(GpioCallback cb, int &outHandle) noexcept;
  Error unregisterStateChangeCallback(int handle) noexcept;

  explicit operator bool() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  explicit Gpio(std::unique_ptr<Impl> impl) noexcept;

  friend class AMachine;
};

// SysBus: represents system bus; can create BusContext for a target node
class SysBus {
public:
  ~SysBus();

  // Create a bus context for a specific address space / node path
  std::shared_ptr<BusContext> getBusContext(const std::string &nodePath,
                                            Error &err) noexcept;

  explicit operator bool() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  explicit SysBus(std::unique_ptr<Impl> impl) noexcept;

  friend class AMachine;
};

// BusContext: provides read/write
class BusContext {
public:
  ~BusContext();

  Error read(uint64_t address, AccessWidth width, uint64_t &outValue) noexcept;
  Error write(uint64_t address, AccessWidth width, uint64_t value) noexcept;

  explicit operator bool() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;

  explicit BusContext(std::unique_ptr<Impl> impl) noexcept;

  friend class SysBus;
};

} // namespace renode