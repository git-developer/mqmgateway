#pragma once

#include "config.hpp"
#include "common.hpp"
#include "mqttobject.hpp"
#include "modbus_client.hpp"
#include "imqttimpl.hpp"
#include "default_command_converter.hpp"

namespace modmqttd {

class ModMqtt;

class MqttClient {
    public:
        typedef std::map<MqttObjectRegisterIdent, std::vector<std::shared_ptr<MqttObject>>, MqttObjectRegisterIdent::Compare> MqttObjMap;

        enum State {
            DISCONNECTED,
            CONNECTING,
            CONNECTED,
            DISCONNECTING
        };

        MqttClient(ModMqtt& modmqttd);
        void setClientId(const std::string& clientId);
        void setBrokerConfig(const MqttBrokerConfig& config);
        void setModbusClients(const std::vector<std::shared_ptr<ModbusClient>>& clients) { mModbusClients = clients; }
        void start() ;//TODO throw(MosquittoException) - deprecated?;
        bool isStarted() { return mIsStarted; }
        void shutdown();
        bool isConnected() const { return mConnectionState == State::CONNECTED; }
        void reconnect() { mMqttImpl->reconnect(); }
        void setObjects(const MqttObjMap& objects) { mObjects = objects; };
        void addCommand(const MqttObjectCommand& pCommand);

        //publish all data after broker is reconnected
        void publishAll();
        void publishState(const MqttObject& obj);
        void publishAvailabilityChange(const MqttObject& obj);

        void processRegisterValues(const std::string& modbusNetworkName, const MsgRegisterValues& values);
        void processRegistersOperationFailed(const std::string& modbusNetworkName, const MsgRegisterMessageBase& values);
        void processModbusNetworkState(const std::string& modbusNetworkName, bool isUp);

        //mqtt communication callbacks
        void onDisconnect();
        void onConnect();
        void onMessage(const char* topic, const void* payload, int payload_len);

        //for unit tests
        void setMqttImplementation(const std::shared_ptr<IMqttImpl>& impl) { mMqttImpl = impl; }
    private:
        std::shared_ptr<IMqttImpl> mMqttImpl;

        void subscribeToCommandTopic(const std::string& objectName, const MqttObjectCommand& cmd);

        static boost::log::sources::severity_logger<Log::severity> log;
        ModMqtt& mOwner;
        MqttBrokerConfig mBrokerConfig;

        void checkAvailabilityChange(MqttObject& object, const MqttObjectRegisterIdent& ident, uint16_t value);
        const MqttObjectCommand& findCommand(const char* topic) const;

        std::vector<std::shared_ptr<ModbusClient>> mModbusClients;

        // TODO check in which thread context mConnectionState is changed.
        // Now it looks like callbacks use mosquitto internal thread and ModMqtt main thread.
        State mConnectionState = State::DISCONNECTED;
        bool mIsStarted = false;
        //std::vector<MqttObject> mObjects;
        /**
         * Assuming that PollGroups do not overlap hold separate list
         * per poll group ident. This way for each MsgRegisterValues we can update
         * objects from single list only.
         * MqttObject can be a member of multiple lists on this map
        */
        MqttObjMap mObjects;
        std::map<std::string, MqttObjectCommand> mCommands;

        DefaultCommandConverter mDefaultConverter;
};

}
