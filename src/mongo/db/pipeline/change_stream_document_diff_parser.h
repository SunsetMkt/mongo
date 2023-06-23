/**
 *    Copyright (C) 2020-present MongoDB, Inc.
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

#include <vector>

#include "mongo/db/exec/document_value/document.h"
#include "mongo/db/exec/document_value/value.h"
#include "mongo/db/update/document_diff_serialization.h"

namespace mongo {
namespace change_stream_document_diff_parser {
struct DeltaUpdateDescription {
    DeltaUpdateDescription(const DeltaUpdateDescription& other) = delete;
    DeltaUpdateDescription(DeltaUpdateDescription&& other) = default;
    DeltaUpdateDescription() = default;

    Document updatedFields;
    std::vector<Value> removedFields;
    std::vector<Value> truncatedArrays;
    Document disambiguatedPaths;
};

/**
 * Parses a document diff to generate update fields in the format required by the change streams.
 */
DeltaUpdateDescription parseDiff(const doc_diff::Diff& diff);

}  // namespace change_stream_document_diff_parser
}  // namespace mongo
