#include <rtps/entities/Reader.h>
#include <rtps/entities/StatefulReader.h>
#include <rtps/entities/StatelessReader.h>
#include <rtps/utils/Lock.h>

using namespace rtps;

void Reader::executeCallbacks(const ReaderCacheChange &cacheChange) {
  Lock lock(m_callback_mutex);
  for (unsigned int i = 0; i < m_callbacks.size(); i++) {
    if (m_callbacks[i] != nullptr) {
      m_callbacks[i](m_callback_arg[i], cacheChange);
    }
  }
}

bool Reader::initMutex(){
    if(m_proxies_mutex == nullptr){
    if (sys_mutex_new(&m_proxies_mutex) != ERR_OK){
      SFR_LOG("StatefulReader: Failed to create mutex.\n");
      return false;
    }
  }
  
  if(m_callback_mutex == nullptr){
    if (sys_mutex_new(&m_callback_mutex) != ERR_OK){
      SFR_LOG("StatefulReader: Failed to create mutex.\n");
      return false;
    }
  }

  return true;
}

void Reader::reset(){
  Lock{m_proxies_mutex};
  Lock{m_callback_mutex};

  m_proxies.clear();
  for(unsigned int i = 0; i < m_callbacks.size(); i++){
    m_callbacks[i] = nullptr;
    m_callback_arg[i] = nullptr;
  }

  m_callback_count = 0;
  m_is_initialized_ = false;
}

bool Reader::isProxy(const Guid_t &guid){
    for (const auto &proxy : m_proxies) {
      if (proxy.remoteWriterGuid.operator==(guid)) {
        return true;
      }
    }
    return false;
}

WriterProxy* Reader::getProxy(Guid_t guid){
  auto isElementToFind = [&](const WriterProxy &proxy) {
    return proxy.remoteWriterGuid == guid;
  };
  auto thunk = [](void *arg, const WriterProxy &value) {
    return (*static_cast<decltype(isElementToFind) *>(arg))(value);
  };
  return m_proxies.find(thunk, &isElementToFind);
}

void Reader::registerCallback(ddsReaderCallback_fp cb, void *arg) {
  Lock lock(m_callback_mutex);
  if (m_callback_count == m_callbacks.size() || cb == nullptr) {
    return;
  }

  for (unsigned int i = 0; i < m_callbacks.size(); i++) {
    if (m_callbacks[i] == nullptr) {
      m_callbacks[i] = cb;
      m_callback_arg[i] = arg;
      m_callback_count++;
      return;
    }
  }
}

uint32_t Reader::getProxiesCount(){
	return m_proxies.getNumElements();
}

void Reader::removeCallback(ddsReaderCallback_fp cb) {
  Lock lock(m_callback_mutex);
  for (unsigned int i = 0; i < m_callbacks.size(); i++) {
    if (m_callbacks[i] == cb) {
      m_callbacks[i] = nullptr;
      m_callback_arg[i] = nullptr;
      m_callback_count--;
      return;
    }
  }
}

void Reader::removeAllProxiesOfParticipant(const GuidPrefix_t &guidPrefix) {
  Lock lock(m_proxies_mutex);
  auto isElementToRemove = [&](const WriterProxy &proxy) {
    return proxy.remoteWriterGuid.prefix == guidPrefix;
  };
  auto thunk = [](void *arg, const WriterProxy &value) {
    return (*static_cast<decltype(isElementToRemove) *>(arg))(value);
  };

  m_proxies.remove(thunk, &isElementToRemove);
}

bool Reader::removeProxy(const Guid_t &guid) {
  Lock lock(m_proxies_mutex);
  auto isElementToRemove = [&](const WriterProxy &proxy) {
    return proxy.remoteWriterGuid == guid;
  };
  auto thunk = [](void *arg, const WriterProxy &value) {
    return (*static_cast<decltype(isElementToRemove) *>(arg))(value);
  };

  return m_proxies.remove(thunk, &isElementToRemove);
}

bool Reader::addNewMatchedWriter(const WriterProxy &newProxy) {
  Lock lock(m_proxies_mutex);
#if (SFR_VERBOSE || SLR_VERBOSE) && RTPS_GLOBAL_VERBOSE
  SFR_LOG("New writer added with id: ");
  printGuid(newProxy.remoteWriterGuid);
  SFR_LOG("\n");
#endif
  return m_proxies.add(newProxy);
}

void rtps::Reader::setSEDPSequenceNumber(const SequenceNumber_t& sn){
	m_sedp_sequence_number = sn;
}
const rtps::SequenceNumber_t& rtps::Reader::getSEDPSequenceNumber(){
	return m_sedp_sequence_number;
}

