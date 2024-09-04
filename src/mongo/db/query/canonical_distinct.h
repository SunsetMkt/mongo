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

#pragma once

#include <boost/move/utility_core.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <string>
#include <utility>

#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/query/collation/collator_interface.h"
#include "mongo/util/uuid.h"

namespace mongo {

class BSONObj;
class ExtensionsCallback;
class NamespaceString;
class OperationContext;

/**
 * The canonical form of the distinct query.
 */
class CanonicalDistinct {
public:
    static const char kKeyField[];
    static const char kQueryField[];
    static const char kCollationField[];
    static const char kCommentField[];
    static const char kUnwoundArrayFieldForViewUnwind[];
    static const char kHintField[];

    CanonicalDistinct(const std::string key,
                      const bool mirrored = false,
                      const boost::optional<UUID> sampleId = boost::none,
                      boost::optional<BSONObj> projSpec = boost::none)
        : _key(std::move(key)),
          _mirrored(std::move(mirrored)),
          _sampleId(std::move(sampleId)),
          _projSpec(std::move(projSpec)) {}

    const std::string& getKey() const {
        return _key;
    }

    boost::optional<UUID> getSampleId() const {
        return _sampleId;
    }

    bool isMirrored() const {
        return _mirrored;
    }

    const boost::optional<BSONObj>& getProjectionSpec() const {
        return _projSpec;
    }

    static boost::intrusive_ptr<ExpressionContext> makeExpressionContext(
        OperationContext* opCtx,
        const NamespaceString& nss,
        const DistinctCommandRequest& distinctCommand,
        const CollatorInterface* defaultCollator,
        boost::optional<ExplainOptions::Verbosity> verbosity = boost::none);

private:
    // The field for which we are getting distinct values.
    const std::string _key;

    // Indicates that this was a mirrored operation.
    bool _mirrored = false;

    // The unique sample id for this operation if it has been chosen for sampling.
    boost::optional<UUID> _sampleId;

    // This is used when we have a covered distinct scan in order to materialize the output.
    boost::optional<BSONObj> _projSpec;
};

}  // namespace mongo
