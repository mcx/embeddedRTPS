/*
The MIT License
Copyright (c) 2019 Lehrstuhl Informatik 11 - RWTH Aachen University
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE

This file is part of embeddedRTPS.

Author: i11 - Embedded Software, RWTH Aachen University
*/

#include "lwip/sys.h"
#include "rtps/entities/StatefulWriter.h"
#include "rtps/messages/MessageFactory.h"
#include "rtps/messages/MessageTypes.h"
#include "rtps/utils/Log.h"
#include <cstring>
#include <stdio.h>

using rtps::StatefulWriterT;

#if SFW_VERBOSE && RTPS_GLOBAL_VERBOSE
#include "rtps/utils/printutils.h"
#define SFW_LOG(...)                                                           \
  if (true) {                                                                  \
    printf("[Stateful Writer %s] ", this->m_attributes.topicName);             \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  }
#else
#define SFW_LOG(...) //
#endif

template <class NetworkDriver>
StatefulWriterT<NetworkDriver>::~StatefulWriterT() {
  m_running = false;
  while (m_thread_running) {
    sys_msleep(500); // Required for tests/ Join currently not available /
    //  increased because Segfault in Tests if(sys_mutex_valid(&m_mutex)){
    //    sys_mutex_free(&m_mutex);
    //  }
  }
}

template <class NetworkDriver>
bool StatefulWriterT<NetworkDriver>::init(TopicData attributes,
                                          TopicKind_t topicKind,
                                          ThreadPool *threadPool,
                                          NetworkDriver &driver,
                                          bool enfUnicast) {
  
  if(m_mutex == nullptr){
    if (sys_mutex_new(&m_mutex) != ERR_OK) {

      SFW_LOG("Failed to create mutex.\n");

      return false;
    }
  }
  
  m_attributes = attributes;

  mp_threadPool = threadPool;
  m_srcPort = attributes.unicastLocator.port;
  m_enforceUnicast = enfUnicast;
  m_topicKind = topicKind;

  m_nextSequenceNumberToSend = {0, 1};
  m_proxies.clear();

  m_transport = &driver;
  m_history.clear();
  m_hbCount = {1};

  // Thread already exists, do not create new one (reusing slot case)
  m_is_initialized_ = true;
  
  if(m_heartbeatThread == nullptr || !m_thread_running){

    m_running = true;
    m_thread_running = false;

    if (m_attributes.endpointGuid.entityId ==
        ENTITYID_SEDP_BUILTIN_PUBLICATIONS_WRITER) {
      m_heartbeatThread = sys_thread_new("HBThreadPub", hbFunctionJumppad, this,
                                        Config::HEARTBEAT_STACKSIZE,
                                        Config::THREAD_POOL_WRITER_PRIO);
    } else if (m_attributes.endpointGuid.entityId ==
              ENTITYID_SEDP_BUILTIN_SUBSCRIPTIONS_WRITER) {
      m_heartbeatThread = sys_thread_new("HBThreadSub", hbFunctionJumppad, this,
                                        Config::HEARTBEAT_STACKSIZE,
                                        Config::THREAD_POOL_WRITER_PRIO);
    } else {
      m_heartbeatThread = sys_thread_new("HBThread", hbFunctionJumppad, this,
                                        Config::HEARTBEAT_STACKSIZE,
                                        Config::THREAD_POOL_WRITER_PRIO);
    }
  }

  return true;
}

template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::reset(){
  m_is_initialized_ = false;
	// TODO
}

template <class NetworkDriver>
const rtps::CacheChange *StatefulWriterT<NetworkDriver>::newChange(
    ChangeKind_t kind, const uint8_t *data, DataSize_t size) {
  INIT_GUARD()
  if (isIrrelevant(kind)) {
    return nullptr;
  }

  Lock lock{m_mutex};

  if (m_history.isFull()) {
    // Right now we drop elements anyway because we cannot detect non-responding
    // readers yet. return nullptr;
    SequenceNumber_t newMin = ++SequenceNumber_t(m_history.getSeqNumMin());
    if (m_nextSequenceNumberToSend < newMin) {
      m_nextSequenceNumberToSend =
          newMin; // Make sure we have the correct sn to send
    }
  }

  auto *result = m_history.addChange(data, size);
  if (mp_threadPool != nullptr) {
    mp_threadPool->addWorkload(this);
  }

  SFW_LOG("Adding new data.\n");

  return result;
}

template <class NetworkDriver> void StatefulWriterT<NetworkDriver>::progress() {
  INIT_GUARD()
  for (const auto &proxy : m_proxies) {
    bool success;
    if (!m_enforceUnicast) {
      success = sendDataWRMulticast(proxy, m_nextSequenceNumberToSend);
    } else {
      success = sendData(proxy, m_nextSequenceNumberToSend);
    }
    if (!success) {
      continue;
    }
  }
  ++m_nextSequenceNumberToSend;
}


template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::setAllChangesToUnsent() {
  INIT_GUARD()
  Lock lock(m_mutex);

  m_nextSequenceNumberToSend = m_history.getSeqNumMin();

  if (mp_threadPool != nullptr) {
    mp_threadPool->addWorkload(this);
  }
}

template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::onNewAckNack(
    const SubmessageAckNack &msg, const GuidPrefix_t &sourceGuidPrefix) {
  INIT_GUARD()
  ReaderProxy *reader = nullptr;
  for (auto &proxy : m_proxies) {
    if (proxy.remoteReaderGuid.prefix == sourceGuidPrefix &&
        proxy.remoteReaderGuid.entityId == msg.readerId) {
      reader = &proxy;
      break;
    }
  }

  if (reader == nullptr) {
#if SFW_VERBOSE && RTPS_GLOBAL_VERBOSE
    SFW_LOG("No proxy found with id: ");
    printEntityId(msg.readerId);
    SFW_LOG(" Dropping acknack.\n");
#endif
    return;
  }

  if (msg.count.value <= reader->ackNackCount.value) {

    SFW_LOG("Count too small. Dropping acknack.\n");

    return;
  }

  reader->ackNackCount = msg.count;
  reader->finalFlag = msg.header.finalFlag();
  reader->lastAckNackSequenceNumber = msg.readerSNState.base;

  // Send missing packets
  SequenceNumber_t nextSN = msg.readerSNState.base;

  if (nextSN.low == 0 && nextSN.high == 0) {
    SFW_LOG("Received preemptive acknack. Ignored.\n");
  } else {
    SFW_LOG("Received non-preemptive acknack.\n");
  }

  for (uint32_t i = 0; i < msg.readerSNState.numBits; ++i, ++nextSN) {
    if (msg.readerSNState.isSet(i)) {

      SFW_LOG("Send Packet on acknack.\n");

      sendData(*reader, nextSN);
    }
  }
  // Check for sequence numbers after defined range
  SequenceNumber_t maxSN;
  {
    Lock lock(m_mutex);
    maxSN = m_history.getSeqNumMax();
  }
  while (nextSN <= maxSN) {
    sendData(*reader, nextSN);
    ++nextSN;
  }
}

template <class NetworkDriver>
bool StatefulWriterT<NetworkDriver>::sendData(
    const ReaderProxy &reader, const SequenceNumber_t &snMissing) {
  INIT_GUARD()
  // TODO smarter packaging e.g. by creating MessageStruct and serialize after
  // adjusting values Reusing the pbuf is not possible. See
  // https://www.nongnu.org/lwip/2_0_x/raw_api.html (Zero-Copy MACs)

  PacketInfo info;
  info.srcPort = m_srcPort;

  MessageFactory::addHeader(info.buffer, m_attributes.endpointGuid.prefix);
  MessageFactory::addSubMessageTimeStamp(info.buffer);

  // Just usable for IPv4
  const LocatorIPv4 &locator = reader.remoteLocator;

  info.destAddr = locator.getIp4Address();
  info.destPort = (Ip4Port_t)locator.port;

  {
    Lock lock(m_mutex);
    const CacheChange *next = m_history.getChangeBySN(snMissing);
    if (next == nullptr) {

      SFW_LOG("Couldn't get a CacheChange with SN (%i,%u)\n", snMissing.high,
              snMissing.low);

      return false;
    }
    MessageFactory::addSubMessageData(
        info.buffer, next->data, false, next->sequenceNumber,
        m_attributes.endpointGuid.entityId, reader.remoteReaderGuid.entityId);
  }

  m_transport->sendPacket(info);
  return true;
}

template <class NetworkDriver>
bool StatefulWriterT<NetworkDriver>::sendDataWRMulticast(
    const ReaderProxy &reader, const SequenceNumber_t &snMissing) {
  INIT_GUARD()

  if (reader.useMulticast || reader.suppressUnicast == false) {
    PacketInfo info;
    info.srcPort = m_srcPort;

    MessageFactory::addHeader(info.buffer, m_attributes.endpointGuid.prefix);
    MessageFactory::addSubMessageTimeStamp(info.buffer);

    // Deceide whether multicast or not
    if (reader.useMulticast) {
      const LocatorIPv4 &locator = reader.remoteMulticastLocator;
      info.destAddr = locator.getIp4Address();
      info.destPort = (Ip4Port_t)locator.port;
    } else {
      const LocatorIPv4 &locator = reader.remoteLocator;
      info.destAddr = locator.getIp4Address();
      info.destPort = (Ip4Port_t)locator.port;
    }

    {
      Lock lock(m_mutex);
      const CacheChange *next = m_history.getChangeBySN(snMissing);
      if (next == nullptr) {

        SFW_LOG("Couldn't get a CacheChange with SN (%i,%u)\n", snMissing.high,
                snMissing.low);

        return false;
      }

      EntityId_t reid;
      if (reader.useMulticast) {
        reid = ENTITYID_UNKNOWN;
      } else {
        reid = reader.remoteReaderGuid.entityId;
      }

      MessageFactory::addSubMessageData(
          info.buffer, next->data, false, next->sequenceNumber,
          m_attributes.endpointGuid.entityId, reid);
    }

    m_transport->sendPacket(info);
  }
  return true;
}

template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::hbFunctionJumppad(void *thisPointer) {
  auto *writer = static_cast<StatefulWriterT<NetworkDriver> *>(thisPointer);
  writer->sendHeartBeatLoop();
}

template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::sendHeartBeatLoop() {
  m_thread_running = true;
  while (m_running) {
    sendHeartBeat();
#ifdef OS_IS_FREERTOS
    vTaskDelay(pdMS_TO_TICKS(Config::SF_WRITER_HB_PERIOD_MS));
#else
    sys_msleep(Config::SF_WRITER_HB_PERIOD_MS);
#endif
  }
  m_thread_running = false;
}

template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::setCacheChangeKind(const SequenceNumber_t& s, ChangeKind_t kind){
  Lock lock(m_mutex);
  m_history.setCacheChangeKind(s, kind);
}


template <class NetworkDriver>
void StatefulWriterT<NetworkDriver>::sendHeartBeat() {
  INIT_GUARD()
  if (m_proxies.isEmpty() || !m_is_initialized_) {

    SFW_LOG("Skipping heartbeat. No proxies.\n");
    return;
  }

  for (auto &proxy : m_proxies) {

    PacketInfo info;
    info.srcPort = m_srcPort;

    SequenceNumber_t firstSN;
    SequenceNumber_t lastSN;
    MessageFactory::addHeader(info.buffer, m_attributes.endpointGuid.prefix);
    {
      Lock lock(m_mutex);
      firstSN = m_history.getSeqNumMin();
      lastSN = m_history.getSeqNumMax();

      // Proxy has confirmed all sequence numbers and set final flag
      if((proxy.lastAckNackSequenceNumber > lastSN) && proxy.finalFlag){
        continue;
      }

    }
    if (firstSN == SEQUENCENUMBER_UNKNOWN || lastSN == SEQUENCENUMBER_UNKNOWN) {

      if (strlen(&this->m_attributes.typeName[0]) != 0) {
        SFW_LOG("Skipping heartbeat. No data.\n");
      }
      return;
    }


    MessageFactory::addHeartbeat(
        info.buffer, m_attributes.endpointGuid.entityId,
        proxy.remoteReaderGuid.entityId, firstSN, lastSN, m_hbCount);

    info.destAddr = proxy.remoteLocator.getIp4Address();
    info.destPort = proxy.remoteLocator.port;
    m_transport->sendPacket(info);
  }
  m_hbCount.value++;
}

#undef SFW_VERBOSE
