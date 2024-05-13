#include "catch2/catch_all.hpp"
#include "mockedserver.hpp"
#include "defaults.hpp"
#include "yaml_utils.hpp"

TestConfig config(R"(
modmqttd:
modbus:
  networks:
    - name: tcptest
      address: localhost
      port: 501
      delay_before_first_command: 5ms
      slaves:
        - address: 1
          delay_before_first_command: 100ms

mqtt:
  client_id: mqtt_test
  broker:
    host: localhost
  objects:
    - topic: test_switch
      commands:
        - name: set
          register: tcptest.1.2
          register_type: holding
)");
TEST_CASE ("Write value in write-only configuration should succeed") {
    MockedModMqttServerThread server(config.toString());
    server.setModbusRegisterValue("tcptest", 1, 2, modmqttd::RegisterType::HOLDING, 0);
    server.start();

    server.waitForSubscription("test_switch/set");

    server.publish("test_switch/set", "7");
    server.waitForModbusValue("tcptest",1,2, modmqttd::RegisterType::HOLDING, 0x7);

    server.stop();
}


TEST_CASE ("Write should respect slave delay_before_command") {
    MockedModMqttServerThread server(config.toString());
    server.setModbusRegisterValue("tcptest", 1, 2, modmqttd::RegisterType::HOLDING, 0);
    server.start();

    server.waitForSubscription("test_switch/set");

    auto now = std::chrono::steady_clock::now();
    server.publish("test_switch/set", "7");
    server.publish("test_switch/set", "8");
    server.waitForModbusValue("tcptest",1,2, modmqttd::RegisterType::HOLDING, 0x8, std::chrono::milliseconds(700));
    auto after = std::chrono::steady_clock::now();
    //global is ignored
    REQUIRE(after - now >= std::chrono::milliseconds(100));

    server.stop();
}

TEST_CASE ("When network delay") {


TestConfig global_delay_config(R"(
modmqttd:
modbus:
    networks:
    - name: tcptest
      delay_before_command: 500ms
      address: localhost
      port: 501
mqtt:
  client_id: mqtt_test
  broker:
    host: localhost
  objects:
    - topic: test_switch
      commands:
        - name: set
          register: tcptest.1.2
          register_type: holding
)");

SECTION("is set to delay_before_command write should wait") {
    MockedModMqttServerThread server(global_delay_config.toString());
    server.setModbusRegisterValue("tcptest", 1, 2, modmqttd::RegisterType::HOLDING, 0);
    server.start();

    server.waitForSubscription("test_switch/set");

    auto now = std::chrono::steady_clock::now();
    server.publish("test_switch/set", "7");
    server.publish("test_switch/set", "8");
    server.waitForModbusValue("tcptest",1,2, modmqttd::RegisterType::HOLDING, 0x8, std::chrono::milliseconds(700));
    auto after = std::chrono::steady_clock::now();

    REQUIRE(after - now >= std::chrono::milliseconds(500));

    server.stop();
}

SECTION("is set to delay_before_first_command write should wait") {
    global_delay_config.mYAML["modbus"]["networks"][0].remove("delay_before_command");
    global_delay_config.mYAML["modbus"]["networks"][0]["delay_before_first_command"] = "500ms";

    MockedModMqttServerThread server(global_delay_config.toString());
    server.setModbusRegisterValue("tcptest", 1, 2, modmqttd::RegisterType::HOLDING, 0);
    server.start();

    server.waitForSubscription("test_switch/set");

    auto now = std::chrono::steady_clock::now();
    server.publish("test_switch/set", "7");
    server.publish("test_switch/set", "8");
    server.waitForModbusValue("tcptest",1,2, modmqttd::RegisterType::HOLDING, 0x8, std::chrono::milliseconds(700));
    auto after = std::chrono::steady_clock::now();

    REQUIRE(after - now >= std::chrono::milliseconds(500));

    server.stop();
}


}
