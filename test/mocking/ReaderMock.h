/*
 *
 * Author: Andreas Wüstenberg (andreas.wuestenberg@rwth-aachen.de)
 */

#ifndef RTPS_READERMOCK_H
#define RTPS_READERMOCK_H

#include <gmock/gmock.h>

#include "rtps/entities/Reader.h"
#include "rtps/storages/HistoryCache.h"

using rtps::Reader;

class ReaderMock final: public Reader{
public:

    explicit ReaderMock(rtps::EntityId_t id){
        entityId = id;
    }
    ~ReaderMock() override = default;

    MOCK_METHOD1(newChange, void(rtps::ReaderCacheChange&));
    MOCK_METHOD2(registerCallback, void(rtps::ddsReaderCallback_fp, void*));

};

#endif //RTPS_READERMOCK_H
