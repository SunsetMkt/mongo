// Test that we can limit number of keys being generated by compounded 2dsphere indexes
// Launch mongod reduced max allowed keys per document.
var runner = MongoRunner.runMongod({setParameter: "indexMaxNumGeneratedKeysPerDocument=200"});

const dbName = jsTestName();
const testDB = runner.getDB(dbName);
var coll = testDB.t;

const runTest = (indexDefinition) => {
    coll.drop();

    // Create compound 2dsphere index.
    assert.commandWorked(coll.createIndex(indexDefinition));

    // This polygon will generate 11 keys
    let polygon = {
        "type": "Polygon",
        "coordinates": [[[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [0.0, 0.0]]]
    };

    // We can insert this just fine
    assert.commandWorked(coll.insert({x: polygon, y: 1}));

    // However, compounding with an array of 20 elements will exceed the number of keys (11*20 >
    // 200) and should fail
    assert.commandFailed(coll.insert(
        {x: polygon, y: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]}));

    // Now let's set a failpoint to relax the constraint so we can insert some bad documents.
    assert.commandWorked(testDB.adminCommand(
        {configureFailPoint: "relaxIndexMaxNumGeneratedKeysPerDocument", mode: "alwaysOn"}));

    assert.commandWorked(coll.insert([
        {
            _id: 'problem1',
            x: polygon,
            y: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]
        },
        {
            _id: 'problem2',
            x: polygon,
            y: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]
        }
    ]));

    // And let's disable the failpoint again. At this point, the documents inserted above simulate
    // old documents inserted prior to SERVER-61184.
    assert.commandWorked(testDB.adminCommand(
        {configureFailPoint: "relaxIndexMaxNumGeneratedKeysPerDocument", mode: "off"}));

    // Confirm we cannot continue to insert bad documents.
    assert.commandFailed(coll.insert(
        {x: polygon, y: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]}));

    // We should also get a warning when we try to validate.
    const validation = assert.commandWorked(coll.validate());
    assert.eq(validation.warnings.length, 1);

    // We should be able to remove a problem document.
    assert.commandWorked(coll.deleteOne({_id: 'problem1'}));

    // We should also be able to update a problem document to fix it.
    assert.commandWorked(coll.updateOne({_id: 'problem2'}, {"$set": {y: []}}));

    // But we shouldn't be able to update it to break it again.
    try {
        coll.updateOne(
            {_id: 'problem2'},
            {"$set": {y: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20]}});
        assert(false);
    } catch (res) {
        assert(res instanceof WriteError);
    }
};

// Test both key orderings
runTest({x: "2dsphere", y: 1});
runTest({y: 1, x: "2dsphere"});

MongoRunner.stopMongod(runner);
