#pragma once

#include <stdint.h>
#include <string>
#include <array>
#include <vector>
#include <netdb.h>
#include <functional>
#include <map>

#define SERVER_START_COMMAND "emulation CreateExternalControlServer \"NAME\" PORT"
namespace renode{

// ADC channel value type
using AdcValue = double;

// GPIO state
enum class GpioState : uint8_t { Low = 0, High = 1 };

using GpioCallback = std::function<void(int pin, GpioState newState)>;

// Peripheral descriptor (type + path + optional metadata)
struct PeripheralDescriptor {
    std::string type;
    std::string path;
    std::map<std::string, std::string> metadata;
};

typedef enum {
    ANY_COMMAND = 0,
    RUN_FOR = 1,
    GET_TIME,
    GET_MACHINE,
    ADC,
    GPIO,
    SYSTEM_BUS,
    EVENT = -1,
} ApiCommand;

// constexpr fixed-size container with pairs of (command, version)
constexpr std::array<std::pair<uint8_t, uint8_t>, 6> command_versions{{
    { RUN_FOR,     0x0 }, // 1
    { GET_TIME,    0x0 }, // 2
    { GET_MACHINE, 0x0 }, // 3
    { ADC,         0x0 }, // 4
    { GPIO,        0x1 }, // 5
    { SYSTEM_BUS,  0x0 }  // 6
}};

/* Simple error enum – mirrors the original renode_error_t API */
enum class RenodeError {
    Ok = 0,
    ConnectionFailed,
    Fatal
};

enum renode_return_code {
  COMMAND_FAILED,       // code, command, data
  FATAL_ERROR,          // code, data
  INVALID_COMMAND,      // code, command
  SUCCESS_WITH_DATA,    // code, command, data
  SUCCESS_WITHOUT_DATA, // code, command
  OK_HANDSHAKE = 5,     // code
  ASYNC_EVENT,          // code, command, callback id, data
} ;

enum renode_error_code {
    ERR_CONNECTION_FAILED,
    ERR_FATAL,
    ERR_NOT_CONNECTED,
    ERR_PERIPHERAL_INIT_FAILED,
    ERR_TIMEOUT,
    ERR_COMMAND_FAILED,
    ERR_NO_ERROR = -1,
} ;

struct renode_error{
    renode_error_code code;
    int flags;
    std::string message;
    void *data;
} ;

enum class TimeUnit{
    TU_MICROSECONDS =       1,
    TU_MILLISECONDS =    1000,
    TU_SECONDS      = 1000000,
} ;

struct renode_gpio_event_data {
    uint64_t timestamp_us;
    bool state;
} ;


// Bus access widths enum class AccessWidth { Byte = 1, Word = 2, DWord = 4, QWord = 8 };
enum class AccessWidth  {
    AW_MULTI_BYTE  = 0,
    AW_BYTE        = 1,
    AW_WORD        = 2,
    AW_DWord       = 4,
    AW_QWord       = 8,
};

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

// helper to read one byte, returning true on success
static bool read_byte(int fd, uint8_t &out) {
    return read_all(fd, &out, 1);
}
}