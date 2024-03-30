/**
 * Tests that operation metrics are not increased while waiting for write concern.
 *
 * @tags: [
 *   # j:true requires persistence
 *   requires_persistence,
 *   requires_replication,
 * ]
 */

// This test reproduces the diagram below, which shows how Writer 1 can end up reading the oplog
// entry generated by Writer 2 when waiting for write concern. On serverless environments this can
// cause a user to be billed improperly billed for RPUs caused by reading large entries written by
// other tenants.
//
//  | Writer 1          | Writer 2     |
//  |-------------------+--------------|
//  | BeginTxn          |              |
//  | Timestamp 10      |              |
//  |                   | BeginTxn     |
//  | Write A           |              |
//  |                   | Update B     |
//  |                   | Timestamp 11 |
//  | Commit            |              |
//  | OnCommit hooks    |              |
//  |                   | Commit       |
//  | WaitForTopOfOplog |              |
//
// TODO(SERVER-84271): Remove the diagram above and keep only the section about when
//                     featureFlagReplicateVectoredInsertsTransactionally is set.
//
// When featureFlagReplicateVectoredInsertsTransactionally is set, the potential for the issue
// happening is smaller but can still occur:
//  | Writer 1          | Writer 2     |
//  |-------------------+--------------|
//  | BeginTxn          |              |
//  | Write A           |              |
//  | Timestamp 10      |              |
//  | Commit            |              |
//  | OnCommit hooks    |              |
//  |                   | BeginTxn     |
//  |                   | Update B     |
//  |                   | Timestamp 11 |
//  |                   | Commit       |
//  | WaitForTopOfOplog |              |
//
// The cause of the issue is that WaitForTopOfOplog reads the top of the oplog in real time, not
// the latest timestamp that the current client has read.

import {configureFailPoint} from "jstests/libs/fail_point_util.js";

// Returns metrics aggregated by database name.
const getDBMetrics = (adminDB) => {
    const cursor = adminDB.aggregate([{$operationMetrics: {}}]);
    let allMetrics = {};
    while (cursor.hasNext()) {
        let doc = cursor.next();
        // Remove localTime field as it prevents us from comparing objects since it always changes.
        delete doc.localTime;
        allMetrics[doc.db] = doc;
    }
    return allMetrics;
};

const setParams = {
    "aggregateOperationResourceConsumptionMetrics": true,
};

const replSet = new ReplSetTest({nodes: 1, nodeOptions: {setParameter: setParams}});
replSet.startSet();
replSet.initiate();

const primary = replSet.getPrimary();
const adminDB = primary.getDB('admin');

const db1Name = "db1";
const db1 = primary.getDB(db1Name);

// Create coll to avoid implicit creation.
db1.createCollection("coll");
// Insert document to be updated by Writer 2.
primary.getDB("otherDB").othercoll.insert({_id: 1, a: 'a'});

var doInsert = function() {
    jsTestLog("Writer 1 performing an insert.");
    assert.commandWorked(
        db.getSiblingDB("db1").coll.insertOne({a: 'a'}, {writeConcern: {w: "majority", j: true}}));
};

function doUpdate() {
    jsTestLog("Writer 2 performing an update.");
    // Write a large record which is going to be the top of the oplog.
    assert.commandWorked(
        db.getSiblingDB("otherDB").othercoll.update({_id: 1}, {a: 'a'.repeat(100 * 1024)}));
}

// Stop the primary from calling into awaitReplication()
const hangBeforeWaitingForWriteConcern =
    configureFailPoint(primary, "hangBeforeWaitingForWriteConcern");

var joinWriter1 = startParallelShell(doInsert, primary.port);
// Wait for writer1 to finish before starting writer 2.
hangBeforeWaitingForWriteConcern.wait();

// We want Writer 2 to perform the update after Writer 1 has finished but before it waits for
// write concern.
const hangAfterUpdate = configureFailPoint(primary, "hangAfterBatchUpdate");
var joinWriter2 = startParallelShell(doUpdate, primary.port);
hangAfterUpdate.wait();

// Unblock Writer 2, which has committed after Writer 1.
hangAfterUpdate.off();

assert.soon(() => {
    // Make sure waitForWriteConcernDurationMillis exists in the currentOp query.
    let allOps = db1.currentOp({ns: "db1.coll"});
    for (const j in allOps.inprog) {
        let op = allOps.inprog[j];
        if (op.hasOwnProperty("waitForWriteConcernDurationMillis")) {
            return true;
        }
    }
    return false;
}, "did not find waitForWriteConcernDurationMillis in currentOp");

// Unblock write concern wait.
hangBeforeWaitingForWriteConcern.off();

joinWriter1();
joinWriter2();

// Check for the existence of waitForWriteConcernDurationMillis field in slow query.
const predicate = /Slow query.*"appName":"MongoDB Shell".*"waitForWriteConcernDurationMillis":.*/;
assert.soon(() => {
    return checkLog.checkContainsOnce(primary, predicate);
}, "did not find waitForWriteConcernDurationMillis in slow query log");

const metrics = getDBMetrics(adminDB);
jsTestLog(metrics);
// docBytesRead should be much smaller than 100kb. A threshold at 10kb should be enough.
assert.lt(metrics.db1.primaryMetrics.docBytesRead,
          10 * 1024,
          "Writer 1 wait for write concern caused undue consumption metrics increase.");

replSet.stopSet();
