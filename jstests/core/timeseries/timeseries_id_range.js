/**
 * Verifies that the we can internalize match predicates generated by time series rewrites on _id as
 * range scan using a combination of minRecord and maxRecord.
 *
 * @tags: [
 *     # The test assumes no index exists on the time field. shardCollection implicitly creates an
 *     # index.
 *     assumes_unsharded_collection,
 *     does_not_support_transactions,
 *     requires_fcv_49,
 *     requires_find_command,
 *     requires_getmore,
 * ]
 */
(function() {
"use strict";

load('jstests/libs/analyze_plan.js');
load("jstests/core/timeseries/libs/timeseries.js");

TimeseriesTest.run((insert) => {
    // These dates will all be inserted into individual buckets.
    const dates = [
        ISODate("2021-04-01T00:00:00.000Z"),
        ISODate("2021-04-02T00:00:00.000Z"),
        ISODate("2021-04-03T00:00:00.000Z"),
        ISODate("2021-04-04T00:00:00.000Z"),
        ISODate("2021-04-05T00:00:00.000Z"),
        ISODate("2021-04-06T00:00:00.000Z"),
        ISODate("2021-04-07T00:00:00.000Z"),
        ISODate("2021-04-08T00:00:00.000Z"),
        ISODate("2021-04-09T00:00:00.000Z"),
        ISODate("2021-04-10T00:00:00.000Z"),
    ];

    const coll = db.timeseries_id_range;
    const timeFieldName = "time";

    function init() {
        coll.drop();

        assert.commandWorked(
            db.createCollection(coll.getName(), {timeseries: {timeField: timeFieldName}}));
    }

    (function testEQ() {
        init();

        let expl = assert.commandWorked(db.runCommand({
            explain: {
                update: "system.buckets.timeseries_id_range",
                updates: [{q: {"_id": dates[5]}, u: {$set: {a: 1}}}]
            }
        }));

        assert(dates[5], getPlanStage(expl, "COLLSCAN").minRecord);
        assert(dates[5], getPlanStage(expl, "COLLSCAN").maxRecord);
    })();

    (function testLTE() {
        init();
        // Just for this test, use a more complex pipeline with unwind.
        const pipeline = [{$match: {time: {$lte: dates[5]}}}, {$unwind: '$x'}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN"), expl);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"), expl);
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i], x: [1, 2]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(12, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(7, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testLT() {
        init();
        const pipeline = [{$match: {time: {$lt: dates[5]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(5, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(6, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testGTE() {
        init();
        const pipeline = [{$match: {time: {$gte: dates[5]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(5, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(6, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testGT() {
        init();
        const pipeline = [{$match: {time: {$gt: dates[5]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(4, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(6, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testRange1() {
        init();

        const pipeline = [{$match: {time: {$gte: dates[5], $lte: dates[7]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(3, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(5, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testRange2() {
        init();

        const pipeline = [{$match: {time: {$gt: dates[5], $lt: dates[7]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(1, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(4, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testRange3() {
        init();

        const pipeline = [{$match: {time: {$lt: dates[5], $gt: dates[7]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(1, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();

    (function testRange4() {
        init();

        const pipeline = [{$match: {time: {$gte: dates[3], $gt: dates[5], $lt: dates[7]}}}];
        let res = coll.aggregate(pipeline).toArray();
        assert.eq(0, res.length);

        let expl = coll.explain("executionStats").aggregate(pipeline);
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("minRecord"));
        assert(getAggPlanStage(expl, "COLLSCAN").hasOwnProperty("maxRecord"));
        assert.eq(0, expl.stages[0].$cursor.executionStats.executionStages.nReturned);

        for (let i = 0; i < 10; i++) {
            assert.commandWorked(insert(coll, {_id: i, [timeFieldName]: dates[i]}));
        }

        res = coll.aggregate(pipeline).toArray();
        assert.eq(1, res.length);

        expl = coll.explain("executionStats").aggregate(pipeline);
        assert.eq(4, expl.stages[0].$cursor.executionStats.totalDocsExamined);
    })();
});
})();
