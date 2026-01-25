// renodeInterface.h
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace renode {
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

  // Connect to renode server on host:port. Throws RenodeException on failure.
  static std::unique_ptr<ExternalControlClient>
  connect(const std::string &host = "127.0.0.1", uint16_t port = 5555);

  // Disconnect explicitly, Destructor will disconnect.
  void disconnect() noexcept;

  // Handshake: vector of (commandId, version)
  bool performHandshake();

  // Send a command and receive the payload bytes (empty vector if none or on
  // error)
  std::vector<uint8_t> send_command(uint8_t commandId,
                                    const std::vector<uint8_t> &payload);

  // Hex printable representation (static helper)
  static std::string bytes_to_string(const std::vector<uint8_t> &v);
  bool connected;

private:
  void send_bytes(const uint8_t *data, size_t len);
  std::vector<uint8_t> recv_response(uint8_t expected_command = 0xFF);

private:
  struct Impl;
  std::unique_ptr<Impl> pimpl_;
  explicit ExternalControlClient(std::unique_ptr<Impl> impl) noexcept;
};
} // namespace renode