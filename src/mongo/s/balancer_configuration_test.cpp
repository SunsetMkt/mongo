/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include <boost/date_time/gregorian/gregorian_types.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/optional.hpp>
#include <vector>

#include "mongo/bson/bsonmisc.h"
#include "mongo/client/remote_command_targeter_mock.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/query/query_request_helper.h"
#include "mongo/db/vector_clock.h"
#include "mongo/executor/remote_command_request.h"
#include "mongo/rpc/metadata/repl_set_metadata.h"
#include "mongo/rpc/metadata/tracking_metadata.h"
#include "mongo/s/balancer_configuration.h"
#include "mongo/s/catalog/sharding_catalog_client.h"
#include "mongo/s/sharding_router_test_fixture.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/net/hostandport.h"

namespace mongo {
namespace {

using executor::RemoteCommandRequest;
using std::vector;
using unittest::assertGet;

boost::gregorian::date currentDate() {
    boost::posix_time::ptime now = boost::posix_time::second_clock::local_time();
    return now.date();
}


BSONObj getReplSecondaryOkMetadata() {
    BSONObjBuilder o;
    ReadPreferenceSetting(ReadPreference::Nearest).toContainingBSON(&o);
    o.append(rpc::kReplSetMetadataFieldName, 1);
    return o.obj();
}

class BalancerConfigurationTestFixture : public ShardingTestFixture {
protected:
    /**
     * Expects a correct find command to be dispatched for the config.settings namespace and returns
     * the specified result. If an empty boost::optional is passed, returns an empty results.
     */
    void expectSettingsQuery(StringData key, StatusWith<boost::optional<BSONObj>> result) {
        onFindCommand([&](const RemoteCommandRequest& request) {
            ASSERT_BSONOBJ_EQ(getReplSecondaryOkMetadata(),
                              rpc::TrackingMetadata::removeTrackingData(request.metadata));

            auto opMsg = OpMsgRequest::fromDBAndBody(request.dbname, request.cmdObj);
            auto findCommand = query_request_helper::makeFromFindCommandForTests(opMsg.body);

            ASSERT_EQ(findCommand->getNamespaceOrUUID().nss()->ns(), "config.settings");
            ASSERT_BSONOBJ_EQ(findCommand->getFilter(), BSON("_id" << key));

            checkReadConcern(request.cmdObj,
                             VectorClock::kInitialComponentTime.asTimestamp(),
                             repl::OpTime::kUninitializedTerm);

            if (!result.isOK()) {
                return StatusWith<vector<BSONObj>>(result.getStatus());
            }

            if (result.getValue()) {
                return StatusWith<vector<BSONObj>>(vector<BSONObj>{*(result.getValue())});
            }

            return StatusWith<vector<BSONObj>>(vector<BSONObj>{});
        });
    }
};

TEST_F(BalancerConfigurationTestFixture, NoConfigurationDocuments) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoSplitSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoMergeSettingsType::kKey, boost::optional<BSONObj>());

    future.default_timed_get();

    ASSERT(config.shouldBalance());
    ASSERT(config.shouldBalanceForAutoSplit());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(ChunkSizeSettingsType::kDefaultMaxChunkSizeBytes, config.getMaxChunkSizeBytes());
    ASSERT(config.getShouldAutoSplit());
}

TEST_F(BalancerConfigurationTestFixture, ChunkSizeSettingsDocumentOnly) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>(BSON("value" << 3)));
    expectSettingsQuery(AutoSplitSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoMergeSettingsType::kKey, boost::optional<BSONObj>());

    future.default_timed_get();

    ASSERT(config.shouldBalance());
    ASSERT(config.shouldBalanceForAutoSplit());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(3 * 1024 * 1024ULL, config.getMaxChunkSizeBytes());
    ASSERT(config.getShouldAutoSplit());
}

TEST_F(BalancerConfigurationTestFixture, BalancerSettingsDocumentOnly) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("stopped" << true)));
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoSplitSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoMergeSettingsType::kKey, boost::optional<BSONObj>());

    future.default_timed_get();

    ASSERT(!config.shouldBalance());
    ASSERT(!config.shouldBalanceForAutoSplit());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(ChunkSizeSettingsType::kDefaultMaxChunkSizeBytes, config.getMaxChunkSizeBytes());
    ASSERT(config.getShouldAutoSplit());
}

TEST_F(BalancerConfigurationTestFixture, AutoSplitSettingsDocumentOnly) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoSplitSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("enabled" << false)));
    expectSettingsQuery(AutoMergeSettingsType::kKey, boost::optional<BSONObj>());

    future.default_timed_get();

    ASSERT(config.shouldBalance());
    ASSERT(config.shouldBalanceForAutoSplit());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(ChunkSizeSettingsType::kDefaultMaxChunkSizeBytes, config.getMaxChunkSizeBytes());
    ASSERT(!config.getShouldAutoSplit());
}

TEST_F(BalancerConfigurationTestFixture, BalancerSettingsDocumentBalanceForAutoSplitOnly) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("mode"
                                                      << "autoSplitOnly")));
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoSplitSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("enabled" << true)));
    expectSettingsQuery(AutoMergeSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("enabled" << true)));

    future.default_timed_get();

    ASSERT(!config.shouldBalance());
    ASSERT(config.shouldBalanceForAutoSplit());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(ChunkSizeSettingsType::kDefaultMaxChunkSizeBytes, config.getMaxChunkSizeBytes());
}

TEST_F(BalancerConfigurationTestFixture, BalancerSettingsDocumentBalanceForAutoMergeOnly) {
    configTargeter()->setFindHostReturnValue(HostAndPort("TestHost1"));

    BalancerConfiguration config;

    auto future = launchAsync([&] { ASSERT_OK(config.refreshAndCheck(operationContext())); });

    expectSettingsQuery(BalancerSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("mode"
                                                      << "autoMergeOnly")));
    expectSettingsQuery(ChunkSizeSettingsType::kKey, boost::optional<BSONObj>());
    expectSettingsQuery(AutoSplitSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("enabled" << true)));
    expectSettingsQuery(AutoMergeSettingsType::kKey,
                        boost::optional<BSONObj>(BSON("enabled" << true)));

    future.default_timed_get();

    ASSERT(!config.shouldBalance());
    ASSERT(config.shouldBalanceForAutoMerge());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              config.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT_EQ(ChunkSizeSettingsType::kDefaultMaxChunkSizeBytes, config.getMaxChunkSizeBytes());
}

TEST(BalancerSettingsType, Defaults) {
    BalancerSettingsType settings = assertGet(BalancerSettingsType::fromBSON(BSONObj()));
    ASSERT_EQ(BalancerSettingsType::kFull, settings.getMode());
    ASSERT_EQ(MigrationSecondaryThrottleOptions::kDefault,
              settings.getSecondaryThrottle().getSecondaryThrottle());
    ASSERT(!settings.getSecondaryThrottle().isWriteConcernSpecified());
}

TEST(BalancerSettingsType, BalancerDisabledThroughStoppedOption) {
    BalancerSettingsType settings =
        assertGet(BalancerSettingsType::fromBSON(BSON("stopped" << true)));
    ASSERT_EQ(BalancerSettingsType::kOff, settings.getMode());
}

TEST(BalancerSettingsType, AllValidBalancerModeOptions) {
    ASSERT_EQ(BalancerSettingsType::kFull,
              assertGet(BalancerSettingsType::fromBSON(BSON("mode"
                                                            << "full")))
                  .getMode());
    ASSERT_EQ(BalancerSettingsType::kAutoSplitOnly,
              assertGet(BalancerSettingsType::fromBSON(BSON("mode"
                                                            << "autoSplitOnly")))
                  .getMode());
    ASSERT_EQ(BalancerSettingsType::kAutoMergeOnly,
              assertGet(BalancerSettingsType::fromBSON(BSON("mode"
                                                            << "autoMergeOnly")))
                  .getMode());
    ASSERT_EQ(BalancerSettingsType::kOff,
              assertGet(BalancerSettingsType::fromBSON(BSON("mode"
                                                            << "off")))
                  .getMode());
}

TEST(BalancerSettingsType, InvalidBalancerModeOption) {
    ASSERT_EQ(ErrorCodes::BadValue,
              BalancerSettingsType::fromBSON(BSON("mode"
                                                  << "BAD"))
                  .getStatus()
                  .code());
}

TEST(BalancerSettingsType, BalancingWindowStartLessThanStop) {
    BalancerSettingsType settings =
        assertGet(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "9:00"
                                                                             << "stop"
                                                                             << "19:00"))));

    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(9) + boost::posix_time::minutes(0))));
    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(10) + boost::posix_time::minutes(30))));
    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(19) + boost::posix_time::minutes(0))));

    ASSERT(!settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(8) + boost::posix_time::minutes(59))));
    ASSERT(!settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(19) + boost::posix_time::minutes(1))));
}

TEST(BalancerSettingsType, BalancingWindowStopLessThanStart) {
    BalancerSettingsType settings =
        assertGet(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "23:00"
                                                                             << "stop"
                                                                             << "8:00"))));

    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(23) + boost::posix_time::minutes(0))));
    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(2) + boost::posix_time::minutes(30))));
    ASSERT(settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(7) + boost::posix_time::minutes(59))));

    ASSERT(!settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(8) + boost::posix_time::minutes(1))));
    ASSERT(!settings.isTimeInBalancingWindow(boost::posix_time::ptime(
        currentDate(), boost::posix_time::hours(22) + boost::posix_time::minutes(00))));
}

TEST(BalancerSettingsType, InvalidBalancingWindowStartEqualsStop) {
    ASSERT_NOT_OK(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "00:00"
                                                                             << "stop"
                                                                             << "00:00")))
                      .getStatus());
}

TEST(BalancerSettingsType, InvalidBalancingWindowTimeFormat) {
    ASSERT_NOT_OK(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "23"
                                                                             << "stop"
                                                                             << "6")))
                      .getStatus());

    ASSERT_NOT_OK(
        BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start" << 23LL << "stop"
                                                                           << "6:00")))
            .getStatus());

    ASSERT_NOT_OK(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "23:00"
                                                                             << "stop" << 6LL)))
                      .getStatus());
}

TEST(BalancerSettingsType, InvalidBalancingWindowFormat) {
    ASSERT_NOT_OK(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("begin"
                                                                             << "23:00"
                                                                             << "stop"
                                                                             << "6:00")))
                      .getStatus());

    ASSERT_NOT_OK(BalancerSettingsType::fromBSON(BSON("activeWindow" << BSON("start"
                                                                             << "23:00"
                                                                             << "end"
                                                                             << "6:00")))
                      .getStatus());
}

TEST(ChunkSizeSettingsType, NormalValues) {
    ASSERT_EQ(
        1024 * 1024ULL,
        assertGet(ChunkSizeSettingsType::fromBSON(BSON("value" << 1))).getMaxChunkSizeBytes());
    ASSERT_EQ(
        10 * 1024 * 1024ULL,
        assertGet(ChunkSizeSettingsType::fromBSON(BSON("value" << 10))).getMaxChunkSizeBytes());
    ASSERT_EQ(
        1024 * 1024 * 1024ULL,
        assertGet(ChunkSizeSettingsType::fromBSON(BSON("value" << 1024))).getMaxChunkSizeBytes());
}

TEST(ChunkSizeSettingsType, BackwardsCompatibilityDueToExtraKeys) {
    ASSERT_EQ(1024 * 1024ULL,
              assertGet(ChunkSizeSettingsType::fromBSON(BSON("value" << 1 << "SomeFutureKey"
                                                                     << "SomeFutureValue")))
                  .getMaxChunkSizeBytes());
}

TEST(ChunkSizeSettingsType, IllegalValues) {
    ASSERT_NOT_OK(ChunkSizeSettingsType::fromBSON(BSON("value" << 0)).getStatus());
    ASSERT_NOT_OK(ChunkSizeSettingsType::fromBSON(BSON("value" << -1)).getStatus());
    ASSERT_NOT_OK(ChunkSizeSettingsType::fromBSON(BSON("value" << 1025)).getStatus());
    ASSERT_NOT_OK(ChunkSizeSettingsType::fromBSON(BSON("value"
                                                       << "WrongType"))
                      .getStatus());
    ASSERT_NOT_OK(ChunkSizeSettingsType::fromBSON(BSON("IllegalKey" << 1)).getStatus());
}

}  // namespace
}  // namespace mongo
