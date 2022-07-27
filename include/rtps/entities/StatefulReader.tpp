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
#include "lwip/tcpip.h"
#include "rtps/entities/StatefulReader.h"
#include "rtps/messages/MessageFactory.h"
#include "rtps/utils/Lock.h"

#if SFR_VERBOSE && RTPS_GLOBAL_VERBOSE
#include "rtps/utils/printutils.h"
#define SFR_LOG(...)                                                           \
  if (true) {                                                                  \
    printf("[StatefulReader %s] ", &m_attributes.topicName[0]);                \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  }
#else
#define SFR_LOG(...) //
#endif

using rtps::StatefulReaderT;

template <class NetworkDriver>
StatefulReaderT<NetworkDriver>::~StatefulReaderT() {
  //  if(sys_mutex_valid(&m_mutex)){ // Getting invalid pointer error, there
  //  seems sth strange
  //    sys_mutex_free(&m_mutex);
  //  }
}

template <class NetworkDriver>
void StatefulReaderT<NetworkDriver>::init(const TopicData &attributes,
                                          NetworkDriver &driver) {
  if (sys_mutex_new(&m_proxies_mutex) != ERR_OK || sys_mutex_new(&m_callback_mutex) != ERR_OK) {

    SFR_LOG("StatefulReader: Failed to create mutex.\n");

    return;
  }
  m_attributes = attributes;
  m_transport = &driver;
  m_packetInfo.srcPort = attributes.unicastLocator.port;
  m_is_initialized_ = true;
}

template <class NetworkDriver>
void StatefulReaderT<NetworkDriver>::newChange(
    const ReaderCacheChange &cacheChange) {
  if (m_callback_count == 0) {
    return;
  }
  Lock lock{m_proxies_mutex};
  for (auto &proxy : m_proxies) {
    if (proxy.remoteWriterGuid == cacheChange.writerGuid) {
      if (proxy.expectedSN == cacheChange.sn) {
        executeCallbacks(cacheChange);
        ++proxy.expectedSN;
        return;
      }
    }
  }
}

template <class NetworkDriver>
bool StatefulReaderT<NetworkDriver>::addNewMatchedWriter(
    const WriterProxy &newProxy) {
#if SFR_VERBOSE && RTPS_GLOBAL_VERBOSE
  SFR_LOG("New writer added with id: ");
  printGuid(newProxy.remoteWriterGuid);
  SFR_LOG("\n");
#endif
  return m_proxies.add(newProxy);
}

template <class NetworkDriver>
bool StatefulReaderT<NetworkDriver>::onNewHeartbeat(
    const SubmessageHeartbeat &msg, const GuidPrefix_t &sourceGuidPrefix) {
  Lock lock(m_proxies_mutex);
  PacketInfo info;
  info.srcPort = m_packetInfo.srcPort;
  WriterProxy *writer = nullptr;
  // Search for writer
  for (WriterProxy &proxy : m_proxies) {
    if (proxy.remoteWriterGuid.prefix == sourceGuidPrefix &&
        proxy.remoteWriterGuid.entityId == msg.writerId) {
      writer = &proxy;
      break;
    }
  }

  if (writer == nullptr) {

#if SFR_VERBOSE && RTPS_GLOBAL_VERBOSE
    SFR_LOG("Ignore heartbeat. Couldn't find a matching "
            "writer with id:");
    printEntityId(msg.writerId);
    SFR_LOG("\n");
#endif
    return false;
  }

  if (msg.count.value <= writer->hbCount.value) {

    SFR_LOG("Ignore heartbeat. Count too low.\n");
    return false;
  }

  writer->hbCount.value = msg.count.value;
  info.destAddr = writer->remoteLocator.getIp4Address();
  info.destPort = writer->remoteLocator.port;
  rtps::MessageFactory::addHeader(info.buffer,
                                  m_attributes.endpointGuid.prefix);
  rtps::MessageFactory::addAckNack(info.buffer, msg.writerId, msg.readerId,
                                   writer->getMissing(msg.firstSN, msg.lastSN),
                                   writer->getNextAckNackCount(), false);

  SFR_LOG("Sending acknack.\n");
  m_transport->sendPacket(info);
  return true;
}

#undef SFR_VERBOSE
