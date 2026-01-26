#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <cstddef>
#include <iostream>

#include "renodeInterface.h"

using namespace renode;

int main(int argc, char *argv[]) {
  QGuiApplication app(argc, argv);

  QQmlApplicationEngine engine;
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
  engine.loadFromModule("digitwin", "Main");

  auto renode = ExternalControlClient::connect("127.0.0.1", 5555);
  if (renode != nullptr) {
    if (renode->performHandshake()) {
      std::cout << "handshake success" << '\n';
    } else {
      std::cout << "handshake failed" << '\n';
    }

  } else {
    std::cout << "connection failed" << '\n';
  }

  Error err;
  renode->getMachine("test-machine", err);
  std::cout << "code:" << err.code << ";message:" << err.message << '\n';


  /*
      // Example: GetTime (no payload)
      {
          auto resp = client.send_command(0x01, {});
          std::cout << "GetTime response (" << resp.size() << " bytes): "
                    << ExternalControlClient::bytes_to_string(resp) <<
     std::endl;
      }

      // Example: ADC read channel 0 (payload: single byte channel)
      {
          std::vector<uint8_t> payload = { 0x00 }; // channel 0
          auto resp = client.send_command(0x02, payload);
          std::cout << "ADC response (" << resp.size() << " bytes): "
                    << ExternalControlClient::bytes_to_string(resp) <<
     std::endl;
      }

      // Example: loop reading ADC channel 0 every second, 5 times
      for(int i=0;i<5;i++) {
          auto resp = client.send_command(0x02, std::vector<uint8_t>{0x00});
          std::cout << "ADC loop resp: " <<
     ExternalControlClient::bytes_to_string(resp) << std::endl;
          std::this_thread::sleep_for(std::chrono::seconds(1));
      }

  */
  return app.exec();
}
