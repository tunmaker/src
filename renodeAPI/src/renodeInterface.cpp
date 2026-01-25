//renodeInterca.cpp
#include "renodeInterface.h"
#include "defs.h"

#include <arpa/inet.h>
#include <charconv>
#include <memory>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace renode {

// Implementation details (socket, protocol) hidden here.
struct ExternalControlClient::Impl {
    std::string host;
    uint16_t port;
    bool connected = false;
    std::mutex mtx;

    // Simulated resources for the example:
    //std::map<std::string, std::weak_ptr<renode::AMachine>> machines;
    Impl(const std::string &h, uint16_t p) : host(h), port(p) {}
};

std::unique_ptr<ExternalControlClient> ExternalControlClient::connect(const std::string &host, uint16_t port){

  auto impl = std::make_unique<Impl>(host, port);
  struct addrinfo hints;
  struct addrinfo *res = nullptr;
  //int sock_fd_;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  int rc = getaddrinfo(host.c_str(),  std::to_string(port).c_str(), &hints, &res);
  if (rc != 0 || !res) {
    throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));
  }

  for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
    sock_fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock_fd_ < 0)
      continue;
    if (::connect(sock_fd_, ai->ai_addr, ai->ai_addrlen) == 0) {
      impl->connected = true;
      break; // connected
    }
    close(sock_fd_);
    sock_fd_ = -1;
  }

  freeaddrinfo(res);

  if (sock_fd_ < 0) {
    throw std::runtime_error("ExternalControlClient: unable to connect");
  }

  // ToDo query server for machines, but we need handshake first ?

  auto c = std::unique_ptr<ExternalControlClient>(new ExternalControlClient(std::move(impl)));
  return c;
}

ExternalControlClient::ExternalControlClient(std::unique_ptr<Impl> impl) noexcept : pimpl_(std::move(impl)) {}
ExternalControlClient::~ExternalControlClient() { disconnect(); }

void ExternalControlClient::disconnect() noexcept {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;

  }
}

bool ExternalControlClient::performHandshake() {

    if (command_versions.size() > UINT16_MAX) return false;
    std::vector<uint8_t> buf;
    write_u16_le(buf, static_cast<uint16_t>(command_versions.size()));
    for (auto &p : command_versions) {
        buf.push_back(p.first);
        buf.push_back(p.second);
    }
    send_bytes(buf.data(), buf.size());

    // Read single-byte server response for handshake
    uint8_t response = 0;
    if (!read_byte(sock_fd_, response)) {
        std::cerr << "handshake: failed to read handshake response\n";
        return false;
    }

    if (response != renode_return_code::OK_HANDSHAKE) {
        // If server sent an error return code, try to read error payload following protocol:
        // many error codes send an echoed command byte + 4-byte size + payload; but handshake uses single-byte response on success.
        // To best-effort parse an error, read next byte if available (non-blocking would be nicer).
        std::cerr << "handshake: unexpected handshake response 0x"
                  << std::hex << int(response) << std::dec << "\n";
        return false;
    }

    return true;
}

// Build and send header+payload, then parse the response payload bytes and
// return them. expected_command is used to assert server echoed command (not
// enforced if 0xFF)
std::vector<uint8_t> ExternalControlClient::send_command(uint8_t commandId,
                                    const std::vector<uint8_t> &payload) {
  // Build 7-byte header: 'R','E', command, data_size (4 bytes LE)
  uint8_t header[7];
  header[0] = static_cast<uint8_t>('R');
  header[1] = static_cast<uint8_t>('E');
  header[2] = commandId;
  uint32_t data_size = static_cast<uint32_t>(payload.size());
  header[3] = static_cast<uint8_t>(data_size & 0xFF);
  header[4] = static_cast<uint8_t>((data_size >> 8) & 0xFF);
  header[5] = static_cast<uint8_t>((data_size >> 16) & 0xFF);
  header[6] = static_cast<uint8_t>((data_size >> 24) & 0xFF);

  // Send header then payload
  send_bytes(header, sizeof(header));
  if (!payload.empty())
    send_bytes(payload.data(), payload.size());

  // Receive and return the response payload
  return recv_response(commandId);
}

void ExternalControlClient::send_bytes(const uint8_t *data, size_t len) {
  if (sock_fd_ < 0)
    throw std::runtime_error("socket closed");
  if (!write_all(sock_fd_, data, len)) {
    throw std::runtime_error("send_bytes: write failed");
  }
}


std::vector<uint8_t> ExternalControlClient::recv_response(uint8_t expected_command) {
  if (sock_fd_ < 0)
    throw std::runtime_error("socket closed");

  uint8_t return_code = 0;
  if (!read_all(sock_fd_, &return_code, 1)) {
    throw std::runtime_error("recv_response: failed to read return code");
  }

  uint8_t received_command = 0xFF;
  // For many codes we read the echoed command
  if (return_code == COMMAND_FAILED || return_code == INVALID_COMMAND ||
      return_code == SUCCESS_WITH_DATA || return_code == SUCCESS_WITHOUT_DATA) {
    if (!read_all(sock_fd_, &received_command, 1)) {
      throw std::runtime_error("recv_response: failed to read echoed command");
    }
  }

  uint32_t data_size = 0;
  std::vector<uint8_t> payload;

  auto safe_read_size = [&](uint32_t &out_size) -> bool {
    uint8_t sizebuf[4];
    if (!read_all(sock_fd_, sizebuf, 4))
      return false;
    out_size = uint32_t(sizebuf[0]) | (uint32_t(sizebuf[1]) << 8) |
               (uint32_t(sizebuf[2]) << 16) | (uint32_t(sizebuf[3]) << 24);
    return true;
  };

  switch (return_code) {
    case COMMAND_FAILED:
    case FATAL_ERROR:
    case SUCCESS_WITH_DATA:
        if (!safe_read_size(data_size)) {
        // server closed or sent truncated frame — return empty and log
        std::cerr << "recv_response: truncated data_size (return_code=0x"
                    << std::hex << int(return_code) << std::dec << ")\n";
        return {};
        }
        if (data_size) {
        payload.resize(data_size);
        if (!read_all(sock_fd_, payload.data(), data_size)) {
            std::cerr << "recv_response: truncated payload (expected " << data_size
                      << " bytes)\n";
            return {};
        }
        }
        break;
  case INVALID_COMMAND:
  case SUCCESS_WITHOUT_DATA:
    data_size = 0;
    break;
  case ASYNC_EVENT:
    // For simplicity, treat async event as error here
    throw std::runtime_error("recv_response: unexpected async event");
  default:
    std::cerr << ("recv_response: unexpected return code ") << return_code;
  }

  // Validate echoed command if requested
  if (expected_command != 0xFF && received_command != 0xFF &&
      received_command != expected_command) {
    // If server returned INVALID_COMMAND, it sets return_code accordingly;
    // here we detect mismatches.
    throw std::runtime_error(
        "recv_response: command mismatch (server echoed different command)");
  }

  // For SUCCESS_WITHOUT_DATA, payload is empty
  // For COMMAND_FAILED/FATAL_ERROR, payload is textual error description;
  // caller may interpret it.
  return payload;
}

std::string ExternalControlClient::bytes_to_string(const std::vector<uint8_t> &v) {
  static const char *hex = "0123456789abcdef";
  std::string s;
  s.reserve(v.size() * 2);
  for (uint8_t b : v) {
    s.push_back(hex[(b >> 4) & 0xF]);
    s.push_back(hex[b & 0xF]);
  }
  return s;
}
}