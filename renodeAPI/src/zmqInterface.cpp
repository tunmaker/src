#include "zmqInterface.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Helpers to write little-endian
static void write_u16_le(std::vector<uint8_t> &buf, uint16_t v) {
  buf.push_back(uint8_t(v & 0xFF));
  buf.push_back(uint8_t((v >> 8) & 0xFF));
}
static void write_u32_le(std::vector<uint8_t> &buf, uint32_t v) {
  buf.push_back(uint8_t(v & 0xFF));
  buf.push_back(uint8_t((v >> 8) & 0xFF));
  buf.push_back(uint8_t((v >> 16) & 0xFF));
  buf.push_back(uint8_t((v >> 24) & 0xFF));
}

// Full-send helper
static bool write_all(int fd, const uint8_t *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t r = send(fd, buf + sent, len - sent, 0);
    if (r <= 0)
      return false;
    sent += static_cast<size_t>(r);
  }
  return true;
}

// Full-read helper
static bool read_all(int fd, uint8_t *buf, size_t len) {
  size_t got = 0;
  while (got < len) {
    ssize_t r = recv(fd, buf + got, len - got, 0);
    if (r <= 0)
      return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

ExternalControlClient::ExternalControlClient(const std::string &server_address)
    : sock_fd_(-1) {
  // Accept "tcp://host:port" or "host:port"
  std::string addr = server_address;
  if (addr.rfind("tcp://", 0) == 0)
    addr = addr.substr(6);
  auto p = addr.find(':');
  if (p == std::string::npos)
    throw std::runtime_error(
        "ExternalControlClient: bad address (expect host:port)");

  std::string host = addr.substr(0, p);
  std::string port = addr.substr(p + 1);

  struct addrinfo hints;
  struct addrinfo *res = nullptr;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;

  int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
  if (rc != 0 || !res) {
    throw std::runtime_error(std::string("getaddrinfo: ") + gai_strerror(rc));
  }

  for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
    sock_fd_ = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock_fd_ < 0)
      continue;
    if (connect(sock_fd_, ai->ai_addr, ai->ai_addrlen) == 0) {
      break; // connected
    }
    close(sock_fd_);
    sock_fd_ = -1;
  }

  freeaddrinfo(res);

  if (sock_fd_ < 0) {
    throw std::runtime_error("ExternalControlClient: unable to connect");
  }
}

ExternalControlClient::~ExternalControlClient() {
  if (sock_fd_ >= 0) {
    close(sock_fd_);
    sock_fd_ = -1;
  }
}

bool ExternalControlClient::init(){
    if(!this->handshake_activate(command_versions)) {
        std::cerr << "Handshake failed or empty response." << '\n';
        return true;
    }
    std::cout << "Handshake complete." << '\n';
    return false;;
}

// helper to read one byte, returning true on success
static bool read_byte(int fd, uint8_t &out) {
    return read_all(fd, &out, 1);
}

// Modified handshake_activate
bool ExternalControlClient::handshake_activate(const std::vector<std::pair<uint8_t,uint8_t>>& activations) {
    
    const uint8_t SUCCESS_HANDSHAKE = 0x05;
    
    if (activations.size() > UINT16_MAX) return false;
    std::vector<uint8_t> buf;
    write_u16_le(buf, static_cast<uint16_t>(activations.size()));
    for (auto &p : activations) {
        buf.push_back(p.first);
        buf.push_back(p.second);
    }
    send_bytes(buf.data(), buf.size());

    // Read single-byte server response for handshake
    uint8_t response = 0;
    if (!read_byte(sock_fd_, response)) {
        std::cerr << "handshake_activate: failed to read handshake response\n";
        return false;
    }

    if (response != SUCCESS_HANDSHAKE) {
        // If server sent an error return code, try to read error payload following protocol:
        // many error codes send an echoed command byte + 4-byte size + payload; but handshake uses single-byte response on success.
        // To best-effort parse an error, read next byte if available (non-blocking would be nicer).
        std::cerr << "handshake_activate: unexpected handshake response 0x"
                  << std::hex << int(response) << std::dec << "\n";
        return false;
    }

    return true;
}

// Build and send header+payload, then parse the response payload bytes and
// return them. expected_command is used to assert server echoed command (not
// enforced if 0xFF)
std::vector<uint8_t>
ExternalControlClient::send_command(uint8_t commandId,
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

// Response parsing according to Renode External Control:
// First byte: return_code
// For many return codes, next byte is received_command (echo)
// For those with data, next 4 bytes are data size (LE) followed by that many
// bytes.
std::vector<uint8_t>
ExternalControlClient::recv_response(uint8_t expected_command) {
  if (sock_fd_ < 0)
    throw std::runtime_error("socket closed");

  uint8_t return_code = 0;
  if (!read_all(sock_fd_, &return_code, 1)) {
    throw std::runtime_error("recv_response: failed to read return code");
  }

  // Return code meanings (from Renode):
  // COMMAND_FAILED (0x01), INVALID_COMMAND (0x02), SUCCESS_WITH_DATA (0x03),
  // SUCCESS_WITHOUT_DATA (0x05), FATAL_ERROR (0x06), ASYNC_EVENT (0x07)
  // Implementation: treat SUCCESS_WITH_DATA and SUCCESS_WITHOUT_DATA specially.
  const uint8_t COMMAND_FAILED = 0x01;
  const uint8_t INVALID_COMMAND = 0x02;
  const uint8_t SUCCESS_WITH_DATA = 0x03;
  const uint8_t SUCCESS_WITHOUT_DATA = 0x05;
  const uint8_t FATAL_ERROR = 0x06;
  const uint8_t ASYNC_EVENT = 0x07;
  const uint8_t SUCCESS_HANDSHAKE = 0x05;

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

std::string
ExternalControlClient::bytes_to_string(const std::vector<uint8_t> &v) {
  static const char *hex = "0123456789abcdef";
  std::string s;
  s.reserve(v.size() * 2);
  for (uint8_t b : v) {
    s.push_back(hex[(b >> 4) & 0xF]);
    s.push_back(hex[b & 0xF]);
  }
  return s;
}
