#include "modbus_thread.hpp"

#include "debugtools.hpp"
#include "modmqtt.hpp"
#include "modbus_types.hpp"
#include "modbus_context.hpp"


namespace modmqttd {

void
ModbusThread::sendMessageFromModbus(moodycamel::BlockingReaderWriterQueue<QueueItem>& fromModbusQueue, const QueueItem& item) {
    fromModbusQueue.enqueue(item);
    modmqttd::notifyQueues();
}

ModbusThread::ModbusThread(
    moodycamel::BlockingReaderWriterQueue<QueueItem>& toModbusQueue,
    moodycamel::BlockingReaderWriterQueue<QueueItem>& fromModbusQueue)
    : mToModbusQueue(toModbusQueue),
      mFromModbusQueue(fromModbusQueue),
      mExecutor(fromModbusQueue, toModbusQueue)
{
}

void
ModbusThread::configure(const ModbusNetworkConfig& config) {
    mNetworkName = config.mName;
    mModbus = ModMqtt::getModbusFactory().getContext(config.mName);
    mModbus->init(config);
    mExecutor.init(mModbus);
    // if you experience read timeouts on RTU then increase
    // min_delay_before_poll config value
    mDelayBeforeCommand = config.mDelayBeforeCommand;
    mDelayBeforeFirstCommand = config.mDelayBeforeFirstCommand;
    if (mDelayBeforeCommand.count() != 0 || mDelayBeforeFirstCommand.count() != 0) {
        BOOST_LOG_SEV(log, Log::info) << "Global minimum delays set. Delay before every command "
            << std::chrono::duration_cast<std::chrono::milliseconds>(mDelayBeforeCommand).count() << "ms"
            << ", delay when slave changes "
            << std::chrono::duration_cast<std::chrono::milliseconds>(mDelayBeforeFirstCommand).count() << "ms";
    }
}

void
ModbusThread::setPollSpecification(const MsgRegisterPollSpecification& spec) {
    std::map<int, std::vector<std::shared_ptr<RegisterPoll>>> registerMap;
    for(std::vector<MsgRegisterPoll>::const_iterator it = spec.mRegisters.begin();
        it != spec.mRegisters.end(); it++)
    {
        // do not poll a poll group declared in modbus config section
        // that was not merged with any mqtt register declaration
        if (it->mRefreshMsec != MsgRegisterPoll::INVALID_REFRESH) {
            std::shared_ptr<RegisterPoll> reg(new RegisterPoll(it->mRegister, it->mRegisterType, it->mCount, it->mRefreshMsec));
            std::map<int, ModbusSlaveConfig>::const_iterator slave_cfg = mSlaves.find(it->mSlaveId);

            if (mDelayBeforeCommand != std::chrono::milliseconds::zero()) {
                reg->mDelay = mDelayBeforeCommand;
                reg->mDelay.delay_type = ModbusCommandDelay::EVERYTIME;
            } else if (mDelayBeforeFirstCommand != std::chrono::milliseconds::zero()) {
                reg->mDelay = mDelayBeforeFirstCommand;
                reg->mDelay.delay_type = ModbusCommandDelay::ON_SLAVE_CHANGE;
            }

            if (slave_cfg != mSlaves.end())
                reg->updateFromSlaveConfig(slave_cfg->second);

            registerMap[it->mSlaveId].push_back(reg);
        }
    }

    mScheduler.setPollSpecification(registerMap);
    BOOST_LOG_SEV(log, Log::debug) << "Poll specification set, got " << registerMap.size() << " slaves," << spec.mRegisters.size() << " registers to poll:";
    for (auto sit = registerMap.begin(); sit != registerMap.end(); sit++) {
        for (auto it = sit->second.begin(); it != sit->second.end(); it++) {

            BOOST_LOG_SEV(log, Log::debug)
            << mNetworkName
            << ", slave " << sit->first
            << ", register " << (*it)->mRegister << ":" << (*it)->mRegisterType
            << ", count=" << (*it)->getCount()
            << ", poll every " << std::chrono::duration_cast<std::chrono::milliseconds>((*it)->mRefresh).count() << "ms"
            << ", min delay " << std::chrono::duration_cast<std::chrono::milliseconds>((*it)->mDelay).count() << "ms";
        }
    }
    mExecutor.setupInitialPoll(registerMap);
    //now wait for MqttNetworkState(up)
}

void
ModbusThread::processWrite(const std::shared_ptr<MsgRegisterValues>& msg) {
    auto cmd = std::shared_ptr<RegisterWrite>(new RegisterWrite(*msg));
    //send state change immediately if we
    //are polling this register
    std::shared_ptr<RegisterPoll> poll(mScheduler.findRegisterPoll(*msg));
    if (poll != nullptr) {
        cmd->mReturnMessage = msg;
    }

    mExecutor.addWriteCommand(msg->mSlaveId, cmd);
}

void
ModbusThread::dispatchMessages(const QueueItem& read) {
    QueueItem item(read);
    bool gotItem = true;
    do {
        if (item.isSameAs(typeid(ModbusNetworkConfig))) {
            configure(*item.getData<ModbusNetworkConfig>());
        } else if (item.isSameAs(typeid(MsgRegisterPollSpecification))) {
            setPollSpecification(*item.getData<MsgRegisterPollSpecification>());
        } else if (item.isSameAs(typeid(EndWorkMessage))) {
            BOOST_LOG_SEV(log, Log::debug) << "Got exit command";
            item.getData<EndWorkMessage>(); //free QueueItem memory
            mShouldRun = false;
        } else if (item.isSameAs(typeid(MsgRegisterValues))) {
            processWrite(item.getData<MsgRegisterValues>());
        } else if (item.isSameAs(typeid(MsgMqttNetworkState))) {
            std::unique_ptr<MsgMqttNetworkState> netstate(item.getData<MsgMqttNetworkState>());
            mMqttConnected = netstate->mIsUp;
        } else if (item.isSameAs(typeid(ModbusSlaveConfig))) {
            //no per-slave config attributes defined yet
            updateFromSlaveConfig(*item.getData<ModbusSlaveConfig>());
        } else {
            BOOST_LOG_SEV(log, Log::error) << "Unknown message received, ignoring";
        }
        gotItem = mToModbusQueue.try_dequeue(item);
    } while(gotItem);
}

void
ModbusThread::sendMessage(const QueueItem& item) {
    sendMessageFromModbus(mFromModbusQueue, item);
}

void
ModbusThread::updateFromSlaveConfig(const ModbusSlaveConfig& pConfig) {
    std::pair<std::map<int, ModbusSlaveConfig>::iterator, bool> result = mSlaves.emplace(std::make_pair(pConfig.mAddress, pConfig));
    if(!result.second)  {
        result.first->second = pConfig;
    }

    auto& registers = mScheduler.getPollSpecification();
    std::map<int, std::vector<std::shared_ptr<RegisterPoll>>>::const_iterator slave_registers = registers.find(pConfig.mAddress);
    if (slave_registers != registers.end()) {
        for (auto it = slave_registers->second.begin(); it != slave_registers->second.end(); it++) {
            (*it)->mDelay = pConfig.mDelayBeforeCommand;
        }
    }
}

std::string
constructIdleWaitMessage(const std::chrono::steady_clock::duration& idleWaitDuration) {
    std::stringstream out;
    if (idleWaitDuration == std::chrono::steady_clock::duration::zero()) {
        out << "Checking for incoming message";
    } else {
        out << "Waiting for messages";
        if (idleWaitDuration != std::chrono::steady_clock::duration::max()) {
            out << ", next poll in " <<  std::chrono::duration_cast<std::chrono::milliseconds>(idleWaitDuration).count() << "ms";
        }
    }
    return out.str();
}

void
ModbusThread::run() {
    try {
        BOOST_LOG_SEV(log, Log::debug) << "Modbus thread started";
        const int maxReconnectTime = 60;
        std::chrono::steady_clock::duration idleWaitDuration = std::chrono::steady_clock::duration::max();
        std::chrono::steady_clock::time_point nextPollTimePoint = std::chrono::steady_clock::now();

        while(mShouldRun) {
            if (mModbus) {
                if (!mModbus->isConnected()) {
                    if (idleWaitDuration > std::chrono::seconds(maxReconnectTime))
                        idleWaitDuration = std::chrono::seconds(0);
                    BOOST_LOG_SEV(log, Log::info) << "modbus: connecting";
                    mModbus->connect();
                    if (mModbus->isConnected()) {
                        BOOST_LOG_SEV(log, Log::info) << "modbus: connected";
                        sendMessage(QueueItem::create(MsgModbusNetworkState(mNetworkName, true)));
                        // if modbus network was disconnected
                        // we need to refresh everything
                        if (!mExecutor.isInitial()) {
                            mExecutor.setupInitialPoll(mScheduler.getPollSpecification());
                        }
                    }
                }

                if (mModbus->isConnected()) {
                    // start polling only if Mosquitto
                    // have succesfully connected to Mqtt broker
                    // to avoid growing mFromModbusQueue with queued register updates
                    // and if we already got the first MsgPollSpecification
                    if (mMqttConnected) {

                        auto now = std::chrono::steady_clock::now();
                        if (!mExecutor.isInitial() && nextPollTimePoint < now) {
                            std::chrono::steady_clock::duration schedulerWaitDuration;
                            std::map<int, std::vector<std::shared_ptr<RegisterPoll>>> regsToPoll = mScheduler.getRegistersToPoll(schedulerWaitDuration, now);
                            nextPollTimePoint = now + schedulerWaitDuration;
                            mExecutor.addPollList(regsToPoll);
                            BOOST_LOG_SEV(log, Log::trace) << "Scheduling " << regsToPoll.size() << " registers to execute" <<
                                ", next schedule in " << std::chrono::duration_cast<std::chrono::milliseconds>(schedulerWaitDuration).count() << "ms";
                        }

                        if (mExecutor.allDone()) {
                            idleWaitDuration = (nextPollTimePoint - now);
                        } else {
                            idleWaitDuration = mExecutor.pollNext();
                        }
                    } else {
                        if (!mMqttConnected)
                            BOOST_LOG_SEV(log, Log::info) << "Waiting for mqtt network to become online";

                        idleWaitDuration = std::chrono::steady_clock::duration::max();
                    }
                } else {
                    sendMessage(QueueItem::create(MsgModbusNetworkState(mNetworkName, false)));
                    if (idleWaitDuration < std::chrono::seconds(maxReconnectTime))
                        idleWaitDuration += std::chrono::seconds(5);
                };
            } else {
                //wait for modbus network config
                idleWaitDuration = std::chrono::steady_clock::duration::max();
            }

            //register polling can change mShouldRun flag, do not wait
            //for next poll if we are exiting
            if (mShouldRun) {
                QueueItem item;
                BOOST_LOG_SEV(log, Log::debug) << constructIdleWaitMessage(idleWaitDuration);
                if (!mToModbusQueue.wait_dequeue_timed(item, idleWaitDuration))
                    continue;
                dispatchMessages(item);
                while(mToModbusQueue.try_dequeue(item))
                    dispatchMessages(item);
            }
        };
        if (mModbus && mModbus->isConnected())
            mModbus->disconnect();
        BOOST_LOG_SEV(log, Log::debug) << "Modbus thread " << mNetworkName << " ended";
    } catch (const std::exception& ex) {
        BOOST_LOG_SEV(log, Log::critical) << "Error in modbus thread " << mNetworkName << ": " << ex.what();
    } catch (...) {
        BOOST_LOG_SEV(log, Log::critical) << "Unknown error in modbus thread " << mNetworkName;
    }
}

}
