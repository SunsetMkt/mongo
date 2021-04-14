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

/**
 * This file contains tests for mongo/db/query/query_planner.cpp
 */

#define MONGO_LOGV2_DEFAULT_COMPONENT ::mongo::logv2::LogComponent::kTest

#include "mongo/db/query/query_planner_test_lib.h"

#include <ostream>

#include "mongo/bson/simple_bsonobj_comparator.h"
#include "mongo/db/jsobj.h"
#include "mongo/db/json.h"
#include "mongo/db/matcher/expression_parser.h"
#include "mongo/db/pipeline/expression_context_for_test.h"
#include "mongo/db/query/collation/collator_factory_mock.h"
#include "mongo/db/query/projection_ast_util.h"
#include "mongo/db/query/projection_parser.h"
#include "mongo/db/query/query_planner.h"
#include "mongo/db/query/query_solution.h"
#include "mongo/logv2/log.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/assert_util.h"

namespace {

using namespace mongo;

using std::string;


Status filterMatches(const BSONObj& testFilter,
                     const BSONObj& testCollation,
                     const QuerySolutionNode* trueFilterNode) {
    if (nullptr == trueFilterNode->filter) {
        return {ErrorCodes::Error{3155107}, "No filter found in query solution node"};
    }

    std::unique_ptr<CollatorInterface> testCollator;
    if (!testCollation.isEmpty()) {
        CollatorFactoryMock collatorFactoryMock;
        auto collator = collatorFactoryMock.makeFromBSON(testCollation);
        if (!collator.isOK()) {
            return collator.getStatus().withContext(
                "collation provided by the test did not parse successfully");
        }
        testCollator = std::move(collator.getValue());
    }

    boost::intrusive_ptr<ExpressionContextForTest> expCtx(new ExpressionContextForTest());
    expCtx->setCollator(std::move(testCollator));
    StatusWithMatchExpression statusWithMatcher = MatchExpressionParser::parse(testFilter, expCtx);
    if (!statusWithMatcher.isOK()) {
        return statusWithMatcher.getStatus().withContext(
            "match expression provided by the test did not parse successfully");
    }
    const std::unique_ptr<MatchExpression> root = std::move(statusWithMatcher.getValue());
    MatchExpression::sortTree(root.get());
    std::unique_ptr<MatchExpression> trueFilter(trueFilterNode->filter->shallowClone());
    MatchExpression::sortTree(trueFilter.get());
    if (trueFilter->equivalent(root.get())) {
        return Status::OK();
    }
    return {
        ErrorCodes::Error{3155108},
        str::stream() << "Provided filter did not match filter on query solution node. Expected: "
                      << root->toString() << ". Found: " << trueFilter->toString()};
}

void appendIntervalBound(BSONObjBuilder& bob, BSONElement& el) {
    if (el.type() == String) {
        std::string data = el.String();
        if (data == "MaxKey") {
            bob.appendMaxKey("");
        } else if (data == "MinKey") {
            bob.appendMinKey("");
        } else {
            bob.appendAs(el, "");
        }
    } else {
        bob.appendAs(el, "");
    }
}

Status intervalMatches(const BSONObj& testInt, const Interval trueInt) {
    BSONObjIterator it(testInt);
    if (!it.more()) {
        return {ErrorCodes::Error{3155118},
                "Interval has no elements, expected 4 (start, end, inclusiveStart, inclusiveEnd)"};
    }
    BSONElement low = it.next();
    if (!it.more()) {
        return {
            ErrorCodes::Error{3155119},
            "Interval has only 1 element, expected 4 (start, end, inclusiveStart, inclusiveEnd)"};
    }
    BSONElement high = it.next();
    if (!it.more()) {
        return {
            ErrorCodes::Error{3155120},
            "Interval has only 2 elements, expected 4 (start, end, inclusiveStart, inclusiveEnd)"};
    }
    bool startInclusive = it.next().Bool();
    if (!it.more()) {
        return {
            ErrorCodes::Error{3155120},
            "Interval has only 3 elements, expected 4 (start, end, inclusiveStart, inclusiveEnd)"};
    }
    bool endInclusive = it.next().Bool();
    if (it.more()) {
        return {ErrorCodes::Error{3155121},
                "Interval has >4 elements, expected exactly 4: (start, end, inclusiveStart, "
                "inclusiveEnd)"};
    }

    BSONObjBuilder bob;
    appendIntervalBound(bob, low);
    appendIntervalBound(bob, high);
    Interval toCompare(bob.obj(), startInclusive, endInclusive);
    if (trueInt.equals(toCompare)) {
        return Status::OK();
    }
    return {ErrorCodes::Error{3155122},
            str::stream() << "provided interval did not match. Expected: " << toCompare.toString()
                          << " Found: " << trueInt.toString()};
}

bool bsonObjFieldsAreInSet(BSONObj obj, const std::set<std::string>& allowedFields) {
    BSONObjIterator i(obj);
    while (i.more()) {
        BSONElement child = i.next();
        if (!allowedFields.count(child.fieldName())) {
            LOGV2_ERROR(23932, "Unexpected field", "field"_attr = child.fieldName());
            return false;
        }
    }

    return true;
}

}  // namespace

namespace mongo {

/**
 * Looks in the children stored in the 'nodes' field of 'testSoln'
 * to see if thet match the 'children' field of 'trueSoln'.
 *
 * This does an unordered comparison, i.e. childrenMatch returns
 * true as long as the set of subtrees in testSoln's 'nodes' matches
 * the set of subtrees in trueSoln's 'children' vector.
 */
static Status childrenMatch(const BSONObj& testSoln,
                            const QuerySolutionNode* trueSoln,
                            bool relaxBoundsCheck) {

    BSONElement children = testSoln["nodes"];
    if (children.eoo() || !children.isABSONObj()) {
        return {ErrorCodes::Error{3155150},
                "found a stage in the solution which was expected to have 'nodes' children, but no "
                "'nodes' object in the provided JSON"};
    }

    // The order of the children array in testSoln might not match
    // the order in trueSoln, so we have to check all combos with
    // these nested loops.
    stdx::unordered_set<size_t> matchedNodeIndexes;
    BSONObjIterator i(children.Obj());
    while (i.more()) {
        BSONElement child = i.next();
        if (child.eoo() || !child.isABSONObj()) {
            return {
                ErrorCodes::Error{3155151},
                str::stream() << "found a child which was expected to be an object but was not: "
                              << child};
        }

        LOGV2_DEBUG(
            3155154, 2, "Attempting to find matching child for {plan}", "plan"_attr = child.Obj());
        // try to match against one of the QuerySolutionNode's children
        bool found = false;
        for (size_t j = 0; j < trueSoln->children.size(); ++j) {
            if (matchedNodeIndexes.find(j) != matchedNodeIndexes.end()) {
                // Do not match a child of the QuerySolutionNode more than once.
                continue;
            }
            auto matchStatus = QueryPlannerTestLib::solutionMatches(
                child.Obj(), trueSoln->children[j], relaxBoundsCheck);
            if (matchStatus.isOK()) {
                LOGV2_DEBUG(3155152, 2, "Found a matching child");
                found = true;
                matchedNodeIndexes.insert(j);
                break;
            }
            LOGV2_DEBUG(3155153,
                        2,
                        "Child at index {j} did not match test solution: {reason}",
                        "j"_attr = j,
                        "reason"_attr = matchStatus.reason());
        }

        // we couldn't match child
        if (!found) {
            return {ErrorCodes::Error{3155155},
                    str::stream() << "could not find a matching plan for child: " << child};
        }
    }

    // Ensure we've matched all children of the QuerySolutionNode.
    if (matchedNodeIndexes.size() == trueSoln->children.size()) {
        return Status::OK();
    }
    return {ErrorCodes::Error{3155156},
            str::stream() << "Did not match the correct number of children. Found "
                          << matchedNodeIndexes.size() << " matching children but "
                          << trueSoln->children.size() << " children in the observed plan"};
}

Status QueryPlannerTestLib::boundsMatch(const BSONObj& testBounds,
                                        const IndexBounds trueBounds,
                                        bool relaxBoundsCheck) {
    // Iterate over the fields on which we have index bounds.
    BSONObjIterator fieldIt(testBounds);
    size_t fieldItCount = 0;
    while (fieldIt.more()) {
        BSONElement arrEl = fieldIt.next();
        if (arrEl.fieldNameStringData() != trueBounds.getFieldName(fieldItCount)) {
            return {ErrorCodes::Error{3155116},
                    str::stream() << "mismatching field name at index " << fieldItCount
                                  << ": expected '" << arrEl.fieldNameStringData()
                                  << "' but found '" << trueBounds.getFieldName(fieldItCount)
                                  << "'"};
        }
        if (arrEl.type() != Array) {
            return {ErrorCodes::Error{3155117},
                    str::stream() << "bounds are expected to be arrays. Found: " << arrEl
                                  << " (type " << arrEl.type() << ")"};
        }
        // Iterate over an ordered interval list for a particular field.
        BSONObjIterator oilIt(arrEl.Obj());
        size_t oilItCount = 0;
        while (oilIt.more()) {
            BSONElement intervalEl = oilIt.next();
            if (intervalEl.type() != Array) {
                return {ErrorCodes::Error{3155117},
                        str::stream()
                            << "intervals within bounds are expected to be arrays. Found: "
                            << intervalEl << " (type " << intervalEl.type() << ")"};
            }
            Interval trueInt = trueBounds.getInterval(fieldItCount, oilItCount);
            if (auto matchStatus = intervalMatches(intervalEl.Obj(), trueInt);
                !matchStatus.isOK()) {
                return matchStatus.withContext(
                    str::stream() << "mismatching interval found at index " << oilItCount
                                  << " within the bounds at index " << fieldItCount);
            }
            ++oilItCount;
        }

        if (!relaxBoundsCheck && oilItCount != trueBounds.getNumIntervals(fieldItCount)) {
            return {
                ErrorCodes::Error{3155123},
                str::stream() << "true bounds have more intervals than provided (bounds at index "
                              << fieldItCount << "). Expected: " << oilItCount
                              << " Found: " << trueBounds.getNumIntervals(fieldItCount)};
        }

        ++fieldItCount;
    }

    return Status::OK();
}

// static
Status QueryPlannerTestLib::solutionMatches(const BSONObj& testSoln,
                                            const QuerySolutionNode* trueSoln,
                                            bool relaxBoundsCheck) {
    //
    // leaf nodes
    //
    if (STAGE_COLLSCAN == trueSoln->getType()) {
        const CollectionScanNode* csn = static_cast<const CollectionScanNode*>(trueSoln);
        BSONElement el = testSoln["cscan"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155100},
                    "found a collection scan in the solution but no corresponding 'cscan' object "
                    "in the provided JSON"};
        }
        BSONObj csObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(csObj, {"dir", "filter", "collation"}));

        BSONElement dir = csObj["dir"];
        if (dir.eoo() || !dir.isNumber()) {
            return {ErrorCodes::Error{3155101},
                    "found a collection scan in the solution but no numeric 'dir' in the provided "
                    "JSON"};
        }
        if (dir.numberInt() != csn->direction) {
            return {ErrorCodes::Error{3155102},
                    str::stream() << "Solution does not match: found a collection scan in "
                                     "the solution but in the wrong direction. Found "
                                  << csn->direction << " but was expecting " << dir.numberInt()};
        }

        BSONElement filter = csObj["filter"];
        if (filter.eoo()) {
            LOGV2(3155103,
                  "Found a collection scan which was expected. No filter provided to check");
            return Status::OK();
        } else if (filter.isNull()) {
            if (csn->filter == nullptr) {
                return Status::OK();
            }
            return {
                ErrorCodes::Error{3155104},
                str::stream() << "Expected a collection scan without a filter, but found a filter: "
                              << csn->filter->toString()};
        } else if (!filter.isABSONObj()) {
            return {ErrorCodes::Error{3155105},
                    str::stream() << "Provided JSON gave a 'cscan' with a 'filter', but the filter "
                                     "was not an object."
                                  << filter};
        }

        BSONObj collation;
        if (BSONElement collationElt = csObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {ErrorCodes::Error{3155106},
                        str::stream()
                            << "Provided JSON gave a 'cscan' with a 'collation', but the collation "
                               "was not an object:"
                            << collationElt};
            }
            collation = collationElt.Obj();
        }

        return filterMatches(filter.Obj(), collation, trueSoln)
            .withContext("mismatching 'filter' for 'cscan' node");
    } else if (STAGE_IXSCAN == trueSoln->getType()) {
        const IndexScanNode* ixn = static_cast<const IndexScanNode*>(trueSoln);
        BSONElement el = testSoln["ixscan"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155109},
                    "found an index scan in the solution but no corresponding 'ixscan' object in "
                    "the provided JSON"};
        }
        BSONObj ixscanObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(
            ixscanObj, {"pattern", "name", "bounds", "dir", "filter", "collation"}));

        BSONElement pattern = ixscanObj["pattern"];
        if (!pattern.eoo()) {
            if (!pattern.isABSONObj()) {
                return {ErrorCodes::Error{3155110},
                        str::stream()
                            << "Provided JSON gave a 'ixscan' with a 'pattern', but the pattern "
                               "was not an object: "
                            << pattern};
            }
            if (SimpleBSONObjComparator::kInstance.evaluate(pattern.Obj() !=
                                                            ixn->index.keyPattern)) {
                return {ErrorCodes::Error{3155111},
                        str::stream() << "Provided JSON gave a 'ixscan' with a 'pattern' which did "
                                         "not match. Expected: "
                                      << pattern.Obj() << " Found: " << ixn->index.keyPattern};
            }
        }

        BSONElement name = ixscanObj["name"];
        if (!name.eoo()) {
            if (name.type() != BSONType::String) {
                return {ErrorCodes::Error{3155112},
                        str::stream()
                            << "Provided JSON gave a 'ixscan' with a 'name', but the name "
                               "was not an string: "
                            << name};
            }
            if (name.valueStringData() != ixn->index.identifier.catalogName) {
                return {ErrorCodes::Error{3155113},
                        str::stream() << "Provided JSON gave a 'ixscan' with a 'name' which did "
                                         "not match. Expected: "
                                      << name << " Found: " << ixn->index.identifier.catalogName};
            }
        }

        if (name.eoo() && pattern.eoo()) {
            return {ErrorCodes::Error{3155114},
                    "Provided JSON gave a 'ixscan' without a 'name' or a 'pattern.'"};
        }

        BSONElement bounds = ixscanObj["bounds"];
        if (!bounds.eoo()) {
            if (!bounds.isABSONObj()) {
                return {ErrorCodes::Error{3155115},
                        str::stream()
                            << "Provided JSON gave a 'ixscan' with a 'bounds', but the bounds "
                               "was not an object: "
                            << bounds};
            } else if (auto boundsStatus = boundsMatch(bounds.Obj(), ixn->bounds, relaxBoundsCheck);
                       !boundsStatus.isOK()) {
                return boundsStatus.withContext(
                    "Provided JSON gave a 'ixscan' with 'bounds' which did not match");
            }
        }

        BSONElement dir = ixscanObj["dir"];
        if (!dir.eoo() && dir.isNumber()) {
            if (dir.numberInt() != ixn->direction) {
                return {ErrorCodes::Error{3155124},
                        str::stream()
                            << "Solution does not match: found an index scan in "
                               "the solution but in the wrong direction. Found "
                            << ixn->direction << " but was expecting " << dir.numberInt()};
            }
        }

        BSONElement filter = ixscanObj["filter"];
        if (filter.eoo()) {
            return Status::OK();
        } else if (filter.isNull()) {
            if (ixn->filter == nullptr) {
                return Status::OK();
            }
            return {ErrorCodes::Error{3155125},
                    str::stream() << "Expected an index scan without a filter, but found a filter: "
                                  << ixn->filter->toString()};
        } else if (!filter.isABSONObj()) {
            return {
                ErrorCodes::Error{3155126},
                str::stream() << "Provided JSON gave an 'ixscan' with a 'filter', but the filter "
                                 "was not an object: "
                              << filter};
        }

        BSONObj collation;
        if (BSONElement collationElt = ixscanObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {
                    ErrorCodes::Error{3155127},
                    str::stream()
                        << "Provided JSON gave an 'ixscan' with a 'collation', but the collation "
                           "was not an object:"
                        << collationElt};
            }
            collation = collationElt.Obj();
        }

        return filterMatches(filter.Obj(), collation, trueSoln)
            .withContext("mismatching 'filter' for 'ixscan' node");
    } else if (STAGE_GEO_NEAR_2D == trueSoln->getType()) {
        const GeoNear2DNode* node = static_cast<const GeoNear2DNode*>(trueSoln);
        BSONElement el = testSoln["geoNear2d"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155128},
                    "found a geoNear2d stage in the solution but no "
                    "corresponding 'geoNear2d' object in the provided JSON"};
        }
        BSONObj geoObj = el.Obj();
        if (SimpleBSONObjComparator::kInstance.evaluate(geoObj == node->index.keyPattern)) {
            return Status::OK();
        }
        return {
            ErrorCodes::Error{3155129},
            str::stream()
                << "found a geoNear2d stage in the solution with mismatching keyPattern. Expected: "
                << geoObj << " Found: " << node->index.keyPattern};
    } else if (STAGE_GEO_NEAR_2DSPHERE == trueSoln->getType()) {
        const GeoNear2DSphereNode* node = static_cast<const GeoNear2DSphereNode*>(trueSoln);
        BSONElement el = testSoln["geoNear2dsphere"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155130},
                    "found a geoNear2dsphere stage in the solution but no "
                    "corresponding 'geoNear2dsphere' object in the provided JSON"};
        }
        BSONObj geoObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(geoObj, {"pattern", "bounds"}));

        BSONElement pattern = geoObj["pattern"];
        if (pattern.eoo() || !pattern.isABSONObj()) {
            return {ErrorCodes::Error{3155131},
                    "found a geoNear2dsphere stage in the solution but no 'pattern' object "
                    "in the provided JSON"};
        }
        if (SimpleBSONObjComparator::kInstance.evaluate(pattern.Obj() != node->index.keyPattern)) {
            return {ErrorCodes::Error{3155132},
                    str::stream() << "found a geoNear2dsphere stage in the solution with "
                                     "mismatching keyPattern. Expected: "
                                  << pattern.Obj() << " Found: " << node->index.keyPattern};
        }

        BSONElement bounds = geoObj["bounds"];
        if (!bounds.eoo()) {
            if (!bounds.isABSONObj()) {
                return {
                    ErrorCodes::Error{3155133},
                    str::stream()
                        << "Provided JSON gave a 'geoNear2dsphere' with a 'bounds', but the bounds "
                           "was not an object: "
                        << bounds};
            } else if (auto boundsStatus =
                           boundsMatch(bounds.Obj(), node->baseBounds, relaxBoundsCheck);
                       !boundsStatus.isOK()) {
                return boundsStatus.withContext(
                    "Provided JSON gave a 'geoNear2dsphere' with 'bounds' which did not match");
            }
        }

        return Status::OK();
    } else if (STAGE_TEXT_MATCH == trueSoln->getType()) {
        // {text: {search: "somestr", language: "something", filter: {blah: 1}}}
        const TextMatchNode* node = static_cast<const TextMatchNode*>(trueSoln);
        BSONElement el = testSoln["text"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155134},
                    "found a text stage in the solution but no "
                    "corresponding 'text' object in the provided JSON"};
        }
        BSONObj textObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(textObj,
                                        {"text",
                                         "search",
                                         "language",
                                         "caseSensitive",
                                         "diacriticSensitive",
                                         "prefix",
                                         "collation",
                                         "filter"}));

        BSONElement searchElt = textObj["search"];
        if (!searchElt.eoo()) {
            if (searchElt.String() != node->ftsQuery->getQuery()) {
                return {ErrorCodes::Error{3155135},
                        str::stream()
                            << "found a text stage in the solution with "
                               "mismatching 'search'. Expected: "
                            << searchElt.String() << " Found: " << node->ftsQuery->getQuery()};
            }
        }

        BSONElement languageElt = textObj["language"];
        if (!languageElt.eoo()) {
            if (languageElt.String() != node->ftsQuery->getLanguage()) {
                return {ErrorCodes::Error{3155136},
                        str::stream()
                            << "found a text stage in the solution with "
                               "mismatching 'language'. Expected: "
                            << languageElt.String() << " Found: " << node->ftsQuery->getLanguage()};
            }
        }

        BSONElement caseSensitiveElt = textObj["caseSensitive"];
        if (!caseSensitiveElt.eoo()) {
            if (caseSensitiveElt.trueValue() != node->ftsQuery->getCaseSensitive()) {
                return {ErrorCodes::Error{3155137},
                        str::stream() << "found a text stage in the solution with "
                                         "mismatching 'caseSensitive'. Expected: "
                                      << caseSensitiveElt.trueValue()
                                      << " Found: " << node->ftsQuery->getCaseSensitive()};
            }
        }

        BSONElement diacriticSensitiveElt = textObj["diacriticSensitive"];
        if (!diacriticSensitiveElt.eoo()) {
            if (diacriticSensitiveElt.trueValue() != node->ftsQuery->getDiacriticSensitive()) {
                return {ErrorCodes::Error{3155138},
                        str::stream() << "found a text stage in the solution with "
                                         "mismatching 'diacriticSensitive'. Expected: "
                                      << diacriticSensitiveElt.trueValue()
                                      << " Found: " << node->ftsQuery->getDiacriticSensitive()};
            }
        }

        BSONElement indexPrefix = textObj["prefix"];
        if (!indexPrefix.eoo()) {
            if (!indexPrefix.isABSONObj()) {
                return {ErrorCodes::Error{3155139},
                        str::stream()
                            << "Provided JSON gave a 'text' with a 'prefix', but the prefix "
                               "was not an object: "
                            << indexPrefix};
            }

            if (0 != indexPrefix.Obj().woCompare(node->indexPrefix)) {
                return {ErrorCodes::Error{3155140},
                        str::stream() << "found a text stage in the solution with "
                                         "mismatching 'prefix'. Expected: "
                                      << indexPrefix.Obj() << " Found: " << node->indexPrefix};
            }
        }

        BSONObj collation;
        if (BSONElement collationElt = textObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {ErrorCodes::Error{3155141},
                        str::stream() << "Provided JSON gave a 'text' stage with a 'collation', "
                                         "but the collation was not an object:"
                                      << collationElt};
            }
            collation = collationElt.Obj();
        }

        BSONElement filter = textObj["filter"];
        if (!filter.eoo()) {
            if (filter.isNull()) {
                if (nullptr != node->filter) {
                    return {ErrorCodes::Error{3155142},
                            str::stream()
                                << "Expected a text stage without a filter, but found a filter: "
                                << node->filter->toString()};
                }
            } else if (!filter.isABSONObj()) {
                return {ErrorCodes::Error{3155143},
                        str::stream()
                            << "Provided JSON gave a 'text' stage with a 'filter', but the filter "
                               "was not an object."
                            << filter};
            } else {
                return filterMatches(filter.Obj(), collation, trueSoln)
                    .withContext("mismatching 'filter' for 'text' node");
            }
        }

        return Status::OK();
    }

    //
    // internal nodes
    //
    if (STAGE_FETCH == trueSoln->getType()) {
        const FetchNode* fn = static_cast<const FetchNode*>(trueSoln);

        BSONElement el = testSoln["fetch"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155144},
                    "found a fetch in the solution but no corresponding 'fetch' object in "
                    "the provided JSON"};
        }
        BSONObj fetchObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(fetchObj, {"collation", "filter", "node"}));

        BSONObj collation;
        if (BSONElement collationElt = fetchObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {ErrorCodes::Error{3155145},
                        str::stream()
                            << "Provided JSON gave a 'fetch' with a 'collation', but the collation "
                               "was not an object:"
                            << collationElt};
            }
            collation = collationElt.Obj();
        }

        BSONElement filter = fetchObj["filter"];
        if (!filter.eoo()) {
            if (filter.isNull()) {
                if (nullptr != fn->filter) {
                    return {ErrorCodes::Error{3155146},
                            str::stream()
                                << "Expected a fetch stage without a filter, but found a filter: "
                                << fn->filter->toString()};
                }
            } else if (!filter.isABSONObj()) {
                return {ErrorCodes::Error{3155147},
                        str::stream()
                            << "Provided JSON gave a 'fetch' stage with a 'filter', but the filter "
                               "was not an object."
                            << filter};
            } else if (auto filterStatus = filterMatches(filter.Obj(), collation, trueSoln);
                       !filterStatus.isOK()) {
                return filterStatus.withContext("mismatching 'filter' for 'fetch' node");
            }
        }

        BSONElement child = fetchObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155148},
                    "found a fetch stage in the solution but no 'node' sub-object in the provided "
                    "JSON"};
        }
        return solutionMatches(child.Obj(), fn->children[0], relaxBoundsCheck)
            .withContext("mismatch beneath fetch node");
    } else if (STAGE_OR == trueSoln->getType()) {
        const OrNode* orn = static_cast<const OrNode*>(trueSoln);
        BSONElement el = testSoln["or"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155149},
                    "found an OR stage in the solution but no "
                    "corresponding 'or' object in the provided JSON"};
        }
        BSONObj orObj = el.Obj();
        return childrenMatch(orObj, orn, relaxBoundsCheck).withContext("mismatch beneath or node");
    } else if (STAGE_AND_HASH == trueSoln->getType()) {
        const AndHashNode* ahn = static_cast<const AndHashNode*>(trueSoln);
        BSONElement el = testSoln["andHash"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155157},
                    "found an AND_HASH stage in the solution but no "
                    "corresponding 'andHash' object in the provided JSON"};
        }
        BSONObj andHashObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(andHashObj, {"collation", "filter", "nodes"}));

        BSONObj collation;
        if (BSONElement collationElt = andHashObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {ErrorCodes::Error{3155158},
                        str::stream()
                            << "Provided JSON gave an 'andHash' stage with a 'collation', "
                               "but the collation was not an object:"
                            << collationElt};
            }
            collation = collationElt.Obj();
        }

        BSONElement filter = andHashObj["filter"];
        if (!filter.eoo()) {
            if (filter.isNull()) {
                if (nullptr != ahn->filter) {
                    return {
                        ErrorCodes::Error{3155159},
                        str::stream()
                            << "Expected an AND_HASH stage without a filter, but found a filter: "
                            << ahn->filter->toString()};
                }
            } else if (!filter.isABSONObj()) {
                return {
                    ErrorCodes::Error{3155160},
                    str::stream()
                        << "Provided JSON gave an AND_HASH stage with a 'filter', but the filter "
                           "was not an object."
                        << filter};
            } else if (auto matchStatus = filterMatches(filter.Obj(), collation, trueSoln);
                       !matchStatus.isOK()) {
                return matchStatus.withContext("mismatching 'filter' for AND_HASH node");
            }
        }

        return childrenMatch(andHashObj, ahn, relaxBoundsCheck)
            .withContext("mismatching children beneath AND_HASH node");
    } else if (STAGE_AND_SORTED == trueSoln->getType()) {
        const AndSortedNode* asn = static_cast<const AndSortedNode*>(trueSoln);
        BSONElement el = testSoln["andSorted"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155161},
                    "found an AND_SORTED stage in the solution but no "
                    "corresponding 'andSorted' object in the provided JSON"};
        }
        BSONObj andSortedObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(andSortedObj, {"collation", "filter", "nodes"}));

        BSONObj collation;
        if (BSONElement collationElt = andSortedObj["collation"]) {
            if (!collationElt.isABSONObj()) {
                return {ErrorCodes::Error{3155162},
                        str::stream()
                            << "Provided JSON gave an 'andSorted' stage with a 'collation', "
                               "but the collation was not an object:"
                            << collationElt};
            }
            collation = collationElt.Obj();
        }

        BSONElement filter = andSortedObj["filter"];
        if (!filter.eoo()) {
            if (filter.isNull()) {
                if (nullptr != asn->filter) {
                    return {
                        ErrorCodes::Error{3155163},
                        str::stream()
                            << "Expected an AND_SORTED stage without a filter, but found a filter: "
                            << asn->filter->toString()};
                }
            } else if (!filter.isABSONObj()) {
                return {
                    ErrorCodes::Error{3155164},
                    str::stream()
                        << "Provided JSON gave an AND_SORTED stage with a 'filter', but the filter "
                           "was not an object."
                        << filter};
            } else if (auto matchStatus = filterMatches(filter.Obj(), collation, trueSoln);
                       !matchStatus.isOK()) {
                return matchStatus.withContext("mismatching 'filter' for AND_SORTED node");
            }
        }

        return childrenMatch(andSortedObj, asn, relaxBoundsCheck)
            .withContext("mismatching children beneath AND_SORTED node");
    } else if (isProjectionStageType(trueSoln->getType())) {
        const ProjectionNode* pn = static_cast<const ProjectionNode*>(trueSoln);

        BSONElement el = testSoln["proj"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155165},
                    "found a projection stage in the solution but no "
                    "corresponding 'proj' object in the provided JSON"};
        }
        BSONObj projObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(projObj, {"type", "spec", "node"}));

        BSONElement projType = projObj["type"];
        if (!projType.eoo()) {
            string projTypeStr = projType.str();
            switch (pn->getType()) {
                case StageType::STAGE_PROJECTION_DEFAULT:
                    if (projTypeStr != "default")
                        return {ErrorCodes::Error{3155166},
                                str::stream() << "found a projection stage in the solution with "
                                                 "mismatching 'type'. Expected: "
                                              << projTypeStr << " Found: 'default'"};
                    break;
                case StageType::STAGE_PROJECTION_COVERED:
                    if (projTypeStr != "coveredIndex")
                        return {ErrorCodes::Error{3155167},
                                str::stream() << "found a projection stage in the solution with "
                                                 "mismatching 'type'. Expected: "
                                              << projTypeStr << " Found: 'coveredIndex'"};
                    break;
                case StageType::STAGE_PROJECTION_SIMPLE:
                    if (projTypeStr != "simple")
                        return {ErrorCodes::Error{3155168},
                                str::stream() << "found a projection stage in the solution with "
                                                 "mismatching 'type'. Expected: "
                                              << projTypeStr << " Found: 'simple'"};
                    break;
                default:
                    MONGO_UNREACHABLE;
            }
        }

        BSONElement spec = projObj["spec"];
        if (spec.eoo() || !spec.isABSONObj()) {
            return {ErrorCodes::Error{3155169},
                    "found a projection stage in the solution but no 'spec' object in the provided "
                    "JSON"};
        }
        BSONElement child = projObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {
                ErrorCodes::Error{3155170},
                "found a projection stage in the solution but no 'node' sub-object in the provided "
                "JSON"};
        }

        // Create an empty/dummy expression context without access to the operation context and
        // collator. This should be sufficient to parse a projection.
        auto expCtx =
            make_intrusive<ExpressionContext>(nullptr, nullptr, NamespaceString("test.dummy"));
        auto projection =
            projection_ast::parse(expCtx, spec.Obj(), ProjectionPolicies::findProjectionPolicies());
        auto specProjObj = projection_ast::astToDebugBSON(projection.root());
        auto solnProjObj = projection_ast::astToDebugBSON(pn->proj.root());
        if (!SimpleBSONObjComparator::kInstance.evaluate(specProjObj == solnProjObj)) {
            return {ErrorCodes::Error{3155171},
                    str::stream() << "found a projection stage in the solution with "
                                     "mismatching 'spec'. Expected: "
                                  << specProjObj << " Found: " << solnProjObj};
        }
        return solutionMatches(child.Obj(), pn->children[0], relaxBoundsCheck)
            .withContext("mismatch below projection stage");
    } else if (isSortStageType(trueSoln->getType())) {
        const SortNode* sn = static_cast<const SortNode*>(trueSoln);
        BSONElement el = testSoln["sort"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155172},
                    "found a sort stage in the solution but no "
                    "corresponding 'sort' object in the provided JSON"};
        }
        BSONObj sortObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(sortObj, {"pattern", "limit", "type", "node"}));

        BSONElement patternEl = sortObj["pattern"];
        if (patternEl.eoo() || !patternEl.isABSONObj()) {
            return {
                ErrorCodes::Error{3155173},
                "found a sort stage in the solution but no 'pattern' object in the provided JSON"};
        }
        BSONElement limitEl = sortObj["limit"];
        if (limitEl.eoo()) {
            return {ErrorCodes::Error{3155174},
                    "found a sort stage in the solution but no 'limit' was provided. Specify '0' "
                    "for no limit."};
        }
        if (!limitEl.isNumber()) {
            return {
                ErrorCodes::Error{3155175},
                str::stream() << "found a sort stage in the solution but 'limit' was not numeric: "
                              << limitEl};
        }

        BSONElement sortType = sortObj["type"];
        if (sortType) {
            if (sortType.type() != BSONType::String) {
                return {ErrorCodes::Error{3155176},
                        str::stream()
                            << "found a sort stage in the solution but 'type' was not a string: "
                            << sortType};
            }

            auto sortTypeString = sortType.valueStringData();
            switch (sn->getType()) {
                case StageType::STAGE_SORT_DEFAULT: {
                    if (sortTypeString != "default") {
                        return {ErrorCodes::Error{3155177},
                                str::stream() << "found a sort stage in the solution with "
                                                 "mismatching 'type'. Expected: "
                                              << sortTypeString << " Found: 'default'"};
                    }
                    break;
                }
                case StageType::STAGE_SORT_SIMPLE: {
                    if (sortTypeString != "simple") {
                        return {ErrorCodes::Error{3155178},
                                str::stream() << "found a sort stage in the solution with "
                                                 "mismatching 'type'. Expected: "
                                              << sortTypeString << " Found: 'simple'"};
                    }
                    break;
                }
                default: { MONGO_UNREACHABLE; }
            }
        }

        BSONElement child = sortObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {
                ErrorCodes::Error{3155179},
                "found a sort stage in the solution but no 'node' sub-object in the provided JSON"};
        }

        size_t expectedLimit = limitEl.numberInt();
        if (!SimpleBSONObjComparator::kInstance.evaluate(patternEl.Obj() == sn->pattern)) {
            return {ErrorCodes::Error{3155180},
                    str::stream() << "found a sort stage in the solution with "
                                     "mismatching 'pattern'. Expected: "
                                  << patternEl << " Found: " << sn->pattern};
        }
        if (expectedLimit != sn->limit) {
            return {ErrorCodes::Error{3155181},
                    str::stream() << "found a projection stage in the solution with "
                                     "mismatching 'limit'. Expected: "
                                  << expectedLimit << " Found: " << sn->limit};
        }
        return solutionMatches(child.Obj(), sn->children[0], relaxBoundsCheck)
            .withContext("mismatch below sort stage");
    } else if (STAGE_SORT_KEY_GENERATOR == trueSoln->getType()) {
        const SortKeyGeneratorNode* keyGenNode = static_cast<const SortKeyGeneratorNode*>(trueSoln);
        BSONElement el = testSoln["sortKeyGen"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155182},
                    "found a sort key generator stage in the solution but no "
                    "corresponding 'sortKeyGen' object in the provided JSON"};
        }
        BSONObj keyGenObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(keyGenObj, {"node"}));

        BSONElement child = keyGenObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155183},
                    "found a sort key generator stage in the solution but no 'node' sub-object in "
                    "the provided JSON"};
        }

        return solutionMatches(child.Obj(), keyGenNode->children[0], relaxBoundsCheck)
            .withContext("mismatch below sortKeyGen");
    } else if (STAGE_SORT_MERGE == trueSoln->getType()) {
        const MergeSortNode* msn = static_cast<const MergeSortNode*>(trueSoln);
        BSONElement el = testSoln["mergeSort"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155184},
                    "found a merge sort stage in the solution but no "
                    "corresponding 'mergeSort' object in the provided JSON"};
        }
        BSONObj mergeSortObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(mergeSortObj, {"nodes"}));
        return childrenMatch(mergeSortObj, msn, relaxBoundsCheck)
            .withContext("mismatching children below merge sort");
    } else if (STAGE_SKIP == trueSoln->getType()) {
        const SkipNode* sn = static_cast<const SkipNode*>(trueSoln);
        BSONElement el = testSoln["skip"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155185},
                    "found a skip stage in the solution but no "
                    "corresponding 'skip' object in the provided JSON"};
        }
        BSONObj skipObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(skipObj, {"n", "node"}));

        BSONElement skipEl = skipObj["n"];
        if (!skipEl.isNumber()) {
            return {ErrorCodes::Error{3155186},
                    str::stream() << "found a skip stage in the solution but 'n' was not numeric: "
                                  << skipEl};
        }
        BSONElement child = skipObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155187},
                    "found a skip stage in the solution but no 'node' sub-object in "
                    "the provided JSON"};
        }

        if (skipEl.numberInt() != sn->skip) {
            return {ErrorCodes::Error{3155188},
                    str::stream() << "found a skip stage in the solution with "
                                     "mismatching 'n'. Expected: "
                                  << skipEl.numberInt() << " Found: " << sn->skip};
        }
        return solutionMatches(child.Obj(), sn->children[0], relaxBoundsCheck)
            .withContext("mismatch below skip stage");
    } else if (STAGE_LIMIT == trueSoln->getType()) {
        const LimitNode* ln = static_cast<const LimitNode*>(trueSoln);
        BSONElement el = testSoln["limit"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155189},
                    "found a limit stage in the solution but no "
                    "corresponding 'limit' object in the provided JSON"};
        }
        BSONObj limitObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(limitObj, {"n", "node"}));

        BSONElement limitEl = limitObj["n"];
        if (!limitEl.isNumber()) {
            return {ErrorCodes::Error{3155190},
                    str::stream() << "found a limit stage in the solution but 'n' was not numeric: "
                                  << limitEl};
        }
        BSONElement child = limitObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155191},
                    "found a limit stage in the solution but no 'node' sub-object in "
                    "the provided JSON"};
        }

        if (limitEl.numberInt() != ln->limit) {
            return {ErrorCodes::Error{3155192},
                    str::stream() << "found a limit stage in the solution with "
                                     "mismatching 'n'. Expected: "
                                  << limitEl.numberInt() << " Found: " << ln->limit};
        }
        return solutionMatches(child.Obj(), ln->children[0], relaxBoundsCheck)
            .withContext("mismatch below limit stage");
    } else if (STAGE_SHARDING_FILTER == trueSoln->getType()) {
        const ShardingFilterNode* fn = static_cast<const ShardingFilterNode*>(trueSoln);

        BSONElement el = testSoln["sharding_filter"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155193},
                    "found a sharding filter stage in the solution but no "
                    "corresponding 'sharding_filter' object in the provided JSON"};
        }
        BSONObj keepObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(keepObj, {"node"}));

        BSONElement child = keepObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155194},
                    "found a sharding filter stage in the solution but no 'node' sub-object in "
                    "the provided JSON"};
        }

        return solutionMatches(child.Obj(), fn->children[0], relaxBoundsCheck)
            .withContext("mismatch below shard filter stage");
    } else if (STAGE_ENSURE_SORTED == trueSoln->getType()) {
        const EnsureSortedNode* esn = static_cast<const EnsureSortedNode*>(trueSoln);

        BSONElement el = testSoln["ensureSorted"];
        if (el.eoo() || !el.isABSONObj()) {
            return {ErrorCodes::Error{3155195},
                    "found a ensureSorted stage in the solution but no "
                    "corresponding 'ensureSorted' object in the provided JSON"};
        }
        BSONObj esObj = el.Obj();
        invariant(bsonObjFieldsAreInSet(esObj, {"node", "pattern"}));

        BSONElement patternEl = esObj["pattern"];
        if (patternEl.eoo() || !patternEl.isABSONObj()) {
            return {ErrorCodes::Error{3155196},
                    "found an ensureSorted stage in the solution but no 'pattern' object in the "
                    "provided JSON"};
        }
        BSONElement child = esObj["node"];
        if (child.eoo() || !child.isABSONObj()) {
            return {ErrorCodes::Error{3155197},
                    "found an ensureSorted stage in the solution but no 'node' sub-object in "
                    "the provided JSON"};
        }

        if (!SimpleBSONObjComparator::kInstance.evaluate(patternEl.Obj() == esn->pattern)) {
            return {ErrorCodes::Error{3155198},
                    str::stream() << "found an ensureSorted stage in the solution with "
                                     "mismatching 'pattern'. Expected: "
                                  << patternEl << " Found: " << esn->pattern};
        }
        return solutionMatches(child.Obj(), esn->children[0], relaxBoundsCheck)
            .withContext("mismatch below ensureSorted stage");
    }

    return {ErrorCodes::Error{31551103},
            str::stream() << "Unknown query solution node found: " << trueSoln->toString()};
}  // namespace mongo

}  // namespace mongo
