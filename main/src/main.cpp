#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <iostream>
#include <thread>
#include "zmqInterface.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("digitwin", "Main");

     // Replace tcp://127.0.0.1:12345 with your server address
    ExternalControlClient client("tcp://127.0.0.1:5555");

    // Example: activate GetTime (id 0x01 v0x01) and ADC (id 0x02 v0x01).
    // Replace ids/versions with real ones from server ICommand classes.
    if(!client.handshake_activate({ {0x01, 0x00}, {0x02, 0x00} })) {
        std::cerr << "Handshake failed or empty response." << std::endl;
        return 1;
    }
    std::cout << "Handshake complete." << std::endl;

    // Example: GetTime (no payload)
    {
        auto resp = client.send_command(0x01, {});
        std::cout << "GetTime response (" << resp.size() << " bytes): "
                  << ExternalControlClient::bytes_to_string(resp) << std::endl;
    }

    // Example: ADC read channel 0 (payload: single byte channel)
    {
        std::vector<uint8_t> payload = { 0x00 }; // channel 0
        auto resp = client.send_command(0x02, payload);
        std::cout << "ADC response (" << resp.size() << " bytes): "
                  << ExternalControlClient::bytes_to_string(resp) << std::endl;
    }

    // Example: loop reading ADC channel 0 every second, 5 times
    for(int i=0;i<5;i++) {
        auto resp = client.send_command(0x02, std::vector<uint8_t>{0x00});
        std::cout << "ADC loop resp: " << ExternalControlClient::bytes_to_string(resp) << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }


    return app.exec();
}
