/*
 * TDD was used here. Tests can be seen as example usage. There are no more features than listed here.
 *
 * Author: Andreas Wüstenberg (andreas.wuestenberg@rwth-aachen.de)
 */

#include <gtest/gtest.h>

#include "rtps/entities/StatelessWriter.h"
#include "rtps/rtps.h"

#include "test/CustomMatcher.h"

using namespace rtps;

class EmptyRTPSWriterWithoutKey : public ::testing::Test{
protected:
    StatelessWriter writer{TopicKind_t::NO_KEY};
    static const data_size_t size = 5;
    const uint8_t data[size] = {0, 1, 2, 3, 4};

    void SetUp() override{
        rtps::init();
    }
};

TEST_F(EmptyRTPSWriterWithoutKey, StartsWithSequenceNumberZero){
    SequenceNumber_t expectedResult{0,0};
    EXPECT_EQ(writer.getLastSequenceNumber(), expectedResult);
}

TEST_F(EmptyRTPSWriterWithoutKey, newChange_IncreasesSequenceNumber){
    SequenceNumber_t expectedResult {0,1};

    writer.newChange(ChangeKind_t::ALIVE, nullptr, 0);

    EXPECT_EQ(writer.getLastSequenceNumber(), expectedResult);
}

TEST_F(EmptyRTPSWriterWithoutKey, newChange_ReturnsChange){
    ChangeKind_t expectedKind = ChangeKind_t::ALIVE;

    const CacheChange* change = writer.newChange(expectedKind, data, size);

    EXPECT_NE(change, nullptr);
}

TEST_F(EmptyRTPSWriterWithoutKey, newChange_SetCorrectValues){
    ChangeKind_t expectedKind = ChangeKind_t::ALIVE;
    const data_size_t size = 5;
    uint8_t data[size] = {};

    const CacheChange* change = writer.newChange(expectedKind, data, size);

    EXPECT_EQ(change->kind, expectedKind);
    EXPECT_EQ(change->sequenceNumber, writer.getLastSequenceNumber());
    EXPECT_THAT(change->data, PBufContains(data, size));
}

TEST_F(EmptyRTPSWriterWithoutKey, newChange_IgnoresAllKindThatAreNotAlive){
    SequenceNumber_t current = writer.getLastSequenceNumber();

    ChangeKind_t irrelevantKinds[] = {ChangeKind_t::INVALID,
                           ChangeKind_t::NOT_ALIVE_DISPOSED,
                           ChangeKind_t::NOT_ALIVE_UNREGISTERED};
    for(auto kind : irrelevantKinds){
        const CacheChange* change = writer.newChange(kind, nullptr, 0);
        EXPECT_EQ(change->kind, ChangeKind_t::INVALID);
        EXPECT_EQ(current, writer.getLastSequenceNumber());
    }
}

class EmptyRTPSWriterWithKey : public ::testing::Test{
protected:
    StatelessWriter writer{TopicKind_t::WITH_KEY};

};

TEST_F(EmptyRTPSWriterWithKey, newChange_IgnoresKindInvalid){
    SequenceNumber_t current = writer.getLastSequenceNumber();

    const CacheChange* change = writer.newChange(ChangeKind_t::INVALID, nullptr, 0);

    EXPECT_EQ(change->kind, ChangeKind_t::INVALID);
    EXPECT_EQ(current, writer.getLastSequenceNumber());

}
TEST_F(EmptyRTPSWriterWithKey, newChange_AddsAllKindsBesideInvalid){
    ChangeKind_t relevantKinds[] = {ChangeKind_t::ALIVE,
                                    ChangeKind_t::NOT_ALIVE_DISPOSED,
                                    ChangeKind_t::NOT_ALIVE_UNREGISTERED};
    for(auto kind : relevantKinds){
        SequenceNumber_t expected = ++writer.getLastSequenceNumber();

        const CacheChange* change = writer.newChange(kind, nullptr, 0);
        EXPECT_EQ(change->kind, kind);
        EXPECT_EQ(writer.getLastSequenceNumber(), expected);
    }
}

/*
class WriterWithoutThreadNotification : StatelessWriter{
public:
    using StatelessWriter::StatelessWriter;
    const CacheChange* newChange(ChangeKind_t kind, const uint8_t* data, data_size_t size){
        return nullptr;
    }
};

TEST(BasicWriter, newChange_NotifiesThreadPool){
    static const data_size_t size = 5;
    const uint8_t data[size] = {0, 1, 2, 3, 4};
    WriterWithoutThreadNotification writer{TopicKind_t::NO_KEY};

    EXPECT_CALL()
    writer.newChange(ChangeKind_t::ALIVE, data, size), nullptr;
}

*/






