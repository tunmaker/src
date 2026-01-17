**foulaBean**

foulaBean is an open-source electro-mechanical machine simulation platform — a cross-platform desktop application for creating digital twins to simulate devices and protocols. The platform provides a user-friendly interface for developers to build, test, and integrate custom device models with minimal overhead.
 
**Foula divided into two** 🌱 **فولة و تقسمت على إثنين**  

Architecture :

The platform consists of the following key components:

    Core Simulation Engine: A C++ real-time loop that handles simulation logic using renode for embedded systems and other tools as the backend
    Qt GUI: A Qt-based QML desktop application that provides a live pin dashboard and controls for interacting with simulated devices and 3D model importer that the user can interact with.
    Protocol Adapters: transplarently stream and read sensors data between backend and renode
    Interfaces : ETH, CAN and ... exposure from backend to other simulated components
    Scripting/API: Embed Python (pybind11) or Lua for scenarios and test scripts.

The platform aims to achieve the following goals:

    Provide a user-friendly interface for developers to create and integrate custom device models.
    Offer a lightweight footprint with minimal overhead compared to traditional simulation tools.
    Support soft real-time simulations with configurable tick rates.
    Integrate seamlessly with Continuous Integration (CI) pipelines using headless CLI runners.

Key Features

    Interactive Machine 3D model importer
    JSON device import/export with schema v0.1 rendoe compatible
    Soft real-time sim loop with configurable tick rate
    Signal types: analog, digital, PWM, UART, CAN, ETH
    Headless CLI for CI integration
    Python scenario API for test scripts
    Qt desktop with live pin dashboard and controls
