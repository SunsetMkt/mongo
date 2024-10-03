/**
 * Tests that a change stream will correctly unwind applyOps entries generated by a vectored
 * insert.
 * @tags: [
 *   change_stream_does_not_expect_txns,
 *   requires_fcv_80
 * ]
 */

import {assertDropAndRecreateCollection} from "jstests/libs/collection_drop_recreate.js";
import {FixtureHelpers} from "jstests/libs/fixture_helpers.js";
import {ChangeStreamTest} from "jstests/libs/query/change_stream_util.js";

const otherCollName = "change_stream_apply_ops_vi_2";
const coll = assertDropAndRecreateCollection(db, "change_stream_apply_ops_vi");
assertDropAndRecreateCollection(db, otherCollName);

const otherDbName = "change_stream_apply_ops_vi_db";
const otherDbCollName = "someColl";
assertDropAndRecreateCollection(db.getSiblingDB(otherDbName), otherDbCollName);

const numShards = FixtureHelpers.numberOfShardsForCollection(coll);
// Pick a reasonable number of inserts per batch so we can test multiple batches.
const insertsPerBatch = 4;
FixtureHelpers.runCommandOnAllShards({
    db: db.getSiblingDB("admin"),
    cmdObj: {setParameter: 1, internalInsertMaxBatchSize: insertsPerBatch}
});
// Try and get every shard to have at least two batches.
const numInserts = numShards * insertsPerBatch * 2;

const testStartTime = db.runCommand({hello: 1}).$clusterTime.clusterTime;
testStartTime.i++;

let cst = new ChangeStreamTest(db);
let changeStream = cst.startWatchingChanges({
    pipeline: [{$changeStream: {}}, {$project: {"lsid.uid": 0}}],
    collection: coll,
    doNotModifyInPassthroughs:
        true  // A collection drop only invalidates single-collection change streams.
});

const sessionOptions = {
    causalConsistency: false,
    retryWrites: true
};

const session = db.getMongo().startSession(sessionOptions);

const sessionDb = session.getDatabase(db.getName());
const sessionColl = sessionDb[coll.getName()];
const sessionOtherColl = sessionDb[otherCollName];
const sessionOtherDbColl = session.getDatabase(otherDbName)[otherDbCollName];

const documents = Array.from(Array(numInserts), (_, i) => ({_id: i + 1, a: 0}));

// A vectored insert on the main test collection.
assert.commandWorked(sessionColl.insert(documents));
const expectedTxnNumber = session.getTxnNumber_forTesting();

// One insert on a collection that we're not watching. This should be skipped by the
// single-collection changestream.
assert.commandWorked(sessionOtherColl.insert({_id: 111, a: "Doc on other collection"}));

// One insert on a collection in a different database. This should be skipped by the single
// collection and single-db changestreams.
assert.commandWorked(sessionOtherDbColl.insert({_id: 222, a: "Doc on other DB"}));

assert.commandWorked(sessionColl.updateOne({_id: 1}, {$inc: {a: 1}}));

// Drop the collection. This will trigger an "invalidate" event at the end of the stream.
assert.commandWorked(db.runCommand({drop: coll.getName()}));

// Define the set of changes expected for the single-collection case per the operations above.
let expectedChanges = Array.from(documents, (doc, _) => ({
                                                documentKey: {_id: doc._id},
                                                fullDocument: doc,
                                                ns: {db: db.getName(), coll: coll.getName()},
                                                operationType: "insert",
                                            }));

expectedChanges.push(
    {
        documentKey: {_id: 1},
        ns: {db: db.getName(), coll: coll.getName()},
        operationType: "update",
        updateDescription: {removedFields: [], updatedFields: {a: 1}, truncatedArrays: []},
    },
);

expectedChanges.push({
    operationType: "drop",
    ns: {db: db.getName(), coll: coll.getName()},
});

// If we are running in a sharded passthrough, then this may have been a multi-shard insert.
// Change streams will interleave the events from across the shards in (clusterTime, txnOpIndex)
// order, and so may not reflect the ordering of writes in the test. We thus verify that exactly the
// expected set of events are observed, but we relax the ordering requirements.
function assertNextChangesEqual({cursor, expectedChanges, expectInvalidate}) {
    const assertEqualFunc = FixtureHelpers.isMongos(db) ? cst.assertNextChangesEqualUnordered
                                                        : cst.assertNextChangesEqual;
    return assertEqualFunc(
        {cursor: cursor, expectedChanges: expectedChanges, expectInvalidate: expectInvalidate});
}

//
// Test behavior of single-collection change streams with apply ops.
//

// Verify that the stream returns the expected sequence of changes.
assertNextChangesEqual({cursor: changeStream, expectedChanges: expectedChanges});

// Single collection change stream should also be invalidated by the drop.
assertNextChangesEqual({
    cursor: changeStream,
    expectedChanges: [{operationType: "invalidate"}],
    expectInvalidate: true
});

//
// Test behavior of whole-db change streams with apply ops.
//

// In a sharded cluster, whole-db-or-cluster streams will see a collection drop from each shard.
for (let i = 1; i < FixtureHelpers.numberOfShardsForCollection(coll); ++i) {
    expectedChanges.push({operationType: "drop", ns: {db: db.getName(), coll: coll.getName()}});
}

// Add an entry for the insert on db.otherColl into expectedChanges.
expectedChanges.splice(numInserts, 0, {
    documentKey: {_id: 111},
    fullDocument: {_id: 111, a: "Doc on other collection"},
    ns: {db: db.getName(), coll: otherCollName},
    operationType: "insert",
});

// Verify that a whole-db stream returns the expected sequence of changes, including the insert
// on the other collection but NOT the changes on the other DB.
changeStream = cst.startWatchingChanges({
    pipeline: [{$changeStream: {startAtOperationTime: testStartTime}}, {$project: {"lsid.uid": 0}}],
    collection: 1
});
assertNextChangesEqual({cursor: changeStream, expectedChanges: expectedChanges});

//
// Test behavior of whole-cluster change streams with apply ops.
//

// Add an entry for the insert on otherDb.otherDbColl into expectedChanges.
expectedChanges.splice(numInserts + 1, 0, {
    documentKey: {_id: 222},
    fullDocument: {_id: 222, a: "Doc on other DB"},
    ns: {db: otherDbName, coll: otherDbCollName},
    operationType: "insert",
});

// Verify that a whole-cluster stream returns the expected sequence of changes, including the
// inserts on the other collection and the other database.
cst = new ChangeStreamTest(db.getSiblingDB("admin"));
changeStream = cst.startWatchingChanges({
    pipeline: [
        {$changeStream: {startAtOperationTime: testStartTime, allChangesForCluster: true}},
        {$project: {"lsid.uid": 0}}
    ],
    collection: 1
});
assertNextChangesEqual({cursor: changeStream, expectedChanges: expectedChanges});

cst.cleanUp();
