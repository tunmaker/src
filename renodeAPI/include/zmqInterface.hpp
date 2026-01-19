#pragma once

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

constexpr uint8_t RUN_FOR     = 0x01;
constexpr uint8_t GET_TIME    = 0x02;
constexpr uint8_t GET_MACHINE = 0x03;
constexpr uint8_t ADC         = 0x04;
constexpr uint8_t GPIO        = 0x05;
constexpr uint8_t SYSTEM_BUS  = 0x06;

class ExternalControlClient {
public:
    ExternalControlClient(const std::string& server_address);
    ~ExternalControlClient();

    bool init();
    // Send a command and receive the payload bytes (empty vector if none or on error)
    std::vector<uint8_t> send_command(uint8_t commandId, const std::vector<uint8_t>& payload);

    // Hex printable representation (static helper)
    static std::string bytes_to_string(const std::vector<uint8_t>& v);

private:
    // Handshake: vector of (commandId, version)
    bool handshake_activate(const std::vector<std::pair<uint8_t,uint8_t>>& activations);
    void send_bytes(const uint8_t* data, size_t len);
    std::vector<uint8_t> recv_response(uint8_t expected_command = 0xFF);
    int sock_fd_;

    std::vector<std::pair<uint8_t,uint8_t>> command_versions = {
    //{0x00, 0x00} // reserved for size  
    {RUN_FOR, 0x0}      // 0x01, version 0  
    ,{GET_TIME, 0x0}     // 0x02, version 0  
    ,{GET_MACHINE, 0x0}  // 0x03, version 0  
    ,{ADC, 0x0}          // 0x04, version 0  
    ,{GPIO, 0x1}         // 0x05, version 1  
    ,{SYSTEM_BUS, 0x0}};   // 0x06, version 0  
    
};

/*
Renode's External Control API doesn't use ZeroMQ messaging patterns - it uses a custom binary protocol over TCP sockets. Here's the actual message structure:

## Message Structure

### Protocol Header
All messages use a 7-byte header structure [1](#1-0) :

```c
struct ExternalControlProtocolHeader {
    byte[2] MagicField;     // "RE" (0x52, 0x45)
    byte   Command;         // Command ID (1-6)
    uint32 DataSize;        // Little-endian payload size
}
```

### Message Flow
1. **Handshake Phase**: Client sends 2-byte count of commands to activate, then command-version pairs [2](#1-1) 
2. **Command Phase**: Header + payload for each command [3](#1-2) 

## Example: ADC Channel Value Request

### Client Send (from `renode_api.c`) [4](#1-3) :

```c
// Header: "RE" + ADC_COMMAND(4) + data_size(16)
uint8_t header[7] = {'R', 'E', 0x04, 0x10, 0x00, 0x00, 0x00};

// Payload: ADC frame structure
adc_frame_t frame = {
    .out = {
        .id = 0,           // ADC instance ID
        .command = 1,      // GET_CHANNEL_VALUE
        .channel = 0,      // Channel number
    }
};
```

### Server Response Format [5](#1-4) :

```c
// Response frame starts with return code byte:
// 0x03 = SUCCESS_WITH_DATA
// 0x04 = ADC_COMMAND (echoed back)
// 0x04, 0x00, 0x00, 0x00 = data size (4 bytes)
// [4-byte payload] = ADC value
```

## Command IDs [6](#1-5) :
- `0x01` = RunFor
- `0x02` = GetTime  
- `0x03` = GetMachine
- `0x04` = ADC
- `0x05` = GPIOPort
- `0x06` = SystemBus

## Notes

This is a proprietary protocol, not ZeroMQ. While NetMQ is used internally for the TCP implementation [7](#1-6) , the external interface exposes a raw TCP socket with this custom binary protocol. You connect using standard TCP sockets, not ZeroMQ libraries.


the client sends the command versions array renode_api.c:221-225 :

the handshake:

static uint8_t command_versions[][2] = {  
    { 0x0, 0x0 }, // reserved for size  
    { RUN_FOR, 0x0 },      // 0x01, version 0  
    { GET_TIME, 0x0 },     // 0x02, version 0  
    { GET_MACHINE, 0x0 },  // 0x03, version 0  
    { ADC, 0x0 },          // 0x04, version 0  
    { GPIO, 0x1 },         // 0x05, version 1  
    { SYSTEM_BUS, 0x0 },   // 0x06, version 0  
};

The first element is overwritten with the count of commands to activate renode_api.c:223 .
State Machine

The server uses a state machine to handle  ExternalControlServer.cs:466-474 :

    Handshake - Waiting for command count
    WaitingForHandshakeData - Waiting for command-version pairs
    WaitingForHeader - Ready for regular commands

*/