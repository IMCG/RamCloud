/* Copyright (c) 2011 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "TestUtil.h"
#include "CoordinatorClient.h"
#include "CoordinatorService.h"
#include "MasterService.h"
#include "Metrics.h"
#include "MetricsHash.h"
#include "PingService.h"
#include "TransportManager.h"
#include "BindTransport.h"
#include "RamCloud.h"

namespace RAMCloud {

class RamCloudTest : public ::testing::Test {
  public:
    BindTransport transport;
    CoordinatorService coordinatorService;
    CoordinatorClient* coordinatorClient1;
    CoordinatorClient* coordinatorClient2;
    ServerConfig masterConfig1;
    ServerConfig masterConfig2;
    MasterService* master1;
    MasterService* master2;
    PingService ping1;
    PingService ping2;
    RamCloud* ramcloud;
    uint32_t tableId1;
    uint32_t tableId2;

  public:
    RamCloudTest()
        : transport()
        , coordinatorService()
        , coordinatorClient1(NULL)
        , coordinatorClient2(NULL)
        , masterConfig1()
        , masterConfig2()
        , master1(NULL)
        , master2(NULL)
        , ping1()
        , ping2()
        , ramcloud(NULL)
        , tableId1(-1)
        , tableId2(-2)
    {
        masterConfig1.coordinatorLocator = "mock:host=coordinatorService";
        masterConfig1.localLocator = "mock:host=master1";
        MasterService::sizeLogAndHashTable("16", "1", &masterConfig1);
        masterConfig2.coordinatorLocator = "mock:host=coordinator";
        masterConfig2.localLocator = "mock:host=master2";
        MasterService::sizeLogAndHashTable("16", "1", &masterConfig2);

        Context::get().transportManager->registerMock(&transport);
        transport.addService(coordinatorService,
                              "mock:host=coordinator", COORDINATOR_SERVICE);

        coordinatorClient1 = new CoordinatorClient(
                             "mock:host=coordinator");
        master1 = new MasterService(masterConfig1, coordinatorClient1, 0);
        transport.addService(*master1, "mock:host=master1", MASTER_SERVICE);
        master1->init();

        coordinatorClient2 = new CoordinatorClient(
                             "mock:host=coordinator");
        master2 = new MasterService(masterConfig2, coordinatorClient2, 0);
        transport.addService(*master2, "mock:host=master2", MASTER_SERVICE);
        master2->init();

        transport.addService(ping1, "mock:host=ping1", PING_SERVICE);
        transport.addService(ping2, "mock:host=master1", PING_SERVICE);

        ramcloud = new RamCloud(Context::get(), "mock:host=coordinator");
        ramcloud->createTable("table1");
        tableId1 = ramcloud->openTable("table1");
        ramcloud->createTable("table2");
        tableId2 = ramcloud->openTable("table2");
        TestLog::enable();
    }

    ~RamCloudTest()
    {
        TestLog::disable();
        delete ramcloud;
        delete master1;
        delete master2;
        delete coordinatorClient1;
        delete coordinatorClient2;
        Context::get().transportManager->unregisterMock();
    }

    DISALLOW_COPY_AND_ASSIGN(RamCloudTest);
};

TEST_F(RamCloudTest, getAllMetrics) {
    // Enlist several "masters" and "backups", with lots of redundancy
    // in the locators to test duplicate elimination.  In reality,
    // most of these are just PingServices, since that's all the
    // functionality that's needed for this test.
    //
    // Note: master1 and master2 are already enlisted automatically
    // (but we create extra redundant enlistments).
    coordinatorClient1->enlistServer(MASTER, "mock:host=master1");
    coordinatorClient1->enlistServer(MASTER, "mock:host=ping1");
    // Make sure each existing server has an associated PingService.
    PingService pingforCoordinator;
    transport.addService(pingforCoordinator, "mock:host=coordinator",
            PING_SERVICE);
    PingService pingforMaster2;
    transport.addService(pingforMaster2, "mock:host=master2", PING_SERVICE);
    PingService ping3;
    transport.addService(ping3, "mock:host=ping3", PING_SERVICE);
    coordinatorClient1->enlistServer(BACKUP, "mock:host=ping1");
    coordinatorClient1->enlistServer(BACKUP, "mock:host=ping3");
    coordinatorClient1->enlistServer(BACKUP, "mock:host=ping3");

    std::vector<MetricsHash> metricList;
    metricList.resize(1);
    metricList[0]["bogusValue"] = 12345;
    metrics->temp.count3 = 30303;
    ramcloud->getAllMetrics(metricList);
    EXPECT_EQ(5U, metricList.size());
    // Make sure the vector was cleared.
    EXPECT_EQ(0U, metricList[0]["bogusValue"]);
    EXPECT_EQ(30303U, metricList[0]["temp.count3"]);
    EXPECT_EQ(30303U, metricList[3]["temp.count3"]);
}

TEST_F(RamCloudTest, getMetrics) {
    metrics->temp.count3 = 10101;
    MetricsHash metrics;
    ramcloud->getMetrics("mock:host=master1", metrics);
    EXPECT_EQ(10101U, metrics["temp.count3"]);
}

TEST_F(RamCloudTest, getMetrics_byTableId) {
    metrics->temp.count3 = 20202;
    MetricsHash metrics;
    ramcloud->getMetrics(tableId1, 0, metrics);
    EXPECT_EQ(20202U, metrics["temp.count3"]);
}

TEST_F(RamCloudTest, ping) {
    EXPECT_EQ(12345U, ramcloud->ping("mock:host=ping1", 12345U, 100000));
}

TEST_F(RamCloudTest, proxyPing) {
    EXPECT_NE(0xffffffffffffffffU, ramcloud->proxyPing("mock:host=ping1",
                "mock:host=master1", 100000, 100000));
}

TEST_F(RamCloudTest, multiRead) {
    // Create objects to be read later
    uint64_t version1;
    ramcloud->create(tableId1, "firstVal", 8, &version1, false);

    uint64_t version2;
    ramcloud->create(tableId2, "secondVal", 9, &version2, false);
    uint64_t version3;
    ramcloud->create(tableId2, "thirdVal", 8, &version3, false);

    // Create requests and read
    MasterClient::ReadObject* requests[3];

    Tub<Buffer> readValue1;
    MasterClient::ReadObject request1(tableId1, 0, &readValue1);
    request1.status = STATUS_RETRY;
    requests[0] = &request1;

    Tub<Buffer> readValue2;
    MasterClient::ReadObject request2(tableId2, 0, &readValue2);
    request2.status = STATUS_RETRY;
    requests[1] = &request2;

    Tub<Buffer> readValue3;
    MasterClient::ReadObject request3(tableId2, 1, &readValue3);
    request3.status = STATUS_RETRY;
    requests[2] = &request3;

    ramcloud->multiRead(requests, 3);

    EXPECT_STREQ("STATUS_OK", statusToSymbol(request1.status));
    EXPECT_EQ(1U, request1.version);
    EXPECT_EQ("firstVal", TestUtil::toString(readValue1.get()));
    EXPECT_STREQ("STATUS_OK", statusToSymbol(request2.status));
    EXPECT_EQ(1U, request2.version);
    EXPECT_EQ("secondVal", TestUtil::toString(readValue2.get()));
    EXPECT_STREQ("STATUS_OK", statusToSymbol(request3.status));
    EXPECT_EQ(2U, request3.version);
    EXPECT_EQ("thirdVal", TestUtil::toString(readValue3.get()));
}

TEST_F(RamCloudTest, writeString) {
    uint32_t tableId1 = ramcloud->openTable("table1");
    ramcloud->write(tableId1, 99, "abcdef");
    Buffer value;
    ramcloud->read(tableId1, 99, &value);
    EXPECT_EQ(6U, value.getTotalLength());
    char buffer[200];
    value.copy(0, value.getTotalLength(), buffer);
    EXPECT_STREQ("abcdef", buffer);
}

}  // namespace RAMCloud
