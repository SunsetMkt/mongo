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

#include "mongo/s/chunk_version.h"

#include "mongo/util/str.h"

namespace mongo {

constexpr StringData ChunkVersion::kShardVersionField;

StatusWith<ChunkVersion> ChunkVersion::parseWithField(const BSONObj& obj, StringData field) {
    BSONElement versionElem = obj[field];
    if (versionElem.eoo())
        return {ErrorCodes::NoSuchKey,
                str::stream() << "Expected field " << field << " not found."};

    if (versionElem.type() != Array)
        return {ErrorCodes::TypeMismatch,
                str::stream() << "Invalid type " << versionElem.type()
                              << " for shardVersion element. Expected an array"};

    return fromBSON(versionElem.Obj());
}

StatusWith<ChunkVersion> ChunkVersion::fromBSON(const BSONObj& obj) {
    BSONObjIterator it(obj);
    if (!it.more())
        return {ErrorCodes::BadValue, "Unexpected empty version array"};

    ChunkVersion version;

    // Expect the major and minor versions
    {
        BSONElement tsPart = it.next();
        if (tsPart.type() != bsonTimestamp)
            return {ErrorCodes::TypeMismatch,
                    str::stream() << "Invalid type " << tsPart.type()
                                  << " for version major and minor part."};

        version._combined = tsPart.timestamp().asULL();
    }

    // Expect the epoch OID
    {
        BSONElement epochPart = it.next();
        if (epochPart.type() != jstOID)
            return {ErrorCodes::TypeMismatch,
                    str::stream() << "Invalid type " << epochPart.type()
                                  << " for version epoch part."};

        version._epoch = epochPart.OID();
    }

    BSONElement nextElem = it.next();

    // TODO SERVER-59105: remove once 6.0 is last-lts. For backward compatibility reasons 5.0
    // routers sends canThrowSSVOnIgnored even though it is not used, so we attempt to parse and
    // ignore it.
    if (nextElem.type() == BSONType::Bool) {
        nextElem = it.next();
    }

    // Check for timestamp
    if (nextElem.type() == bsonTimestamp) {
        version._timestamp = nextElem.timestamp();
    } else if (nextElem.eoo() && version.is50IgnoredOrUnsharded()) {
        // In 5.0 binaries, the timestamp is not present in UNSHARDED and IGNORED versions
        version._timestamp =
            (version.epoch() == UNSHARDED().epoch()) ? Timestamp() : Timestamp::max();
    } else {
        // TODO (SERVER-64813): remove this code once 6.0 becomes lastLTS
        // Hack to solve a complex problem related to the addition of the timestamp in 5.0
        uasserted(ErrorCodes::StaleShardVersion,
                  str::stream() << "Failed to parse " << obj.toString()
                                << " as a ChunkVersion because it is missing the timestamp field.");
    }

    return version;
}

StatusWith<ChunkVersion> ChunkVersion::parseLegacyWithField(const BSONObj& obj, StringData field) {
    auto versionElem = obj[field];
    if (versionElem.eoo())
        return {ErrorCodes::NoSuchKey,
                str::stream() << "Expected field " << field << " not found."};

    ChunkVersion version;

    // Expect the major and minor
    {
        if (versionElem.type() == bsonTimestamp || versionElem.type() == Date) {
            version._combined = versionElem._numberLong();
        } else {
            return {ErrorCodes::TypeMismatch,
                    str::stream() << "Invalid type " << versionElem.type()
                                  << " for version major and minor part."};
        }
    }

    bool fullVersion = false;
    // Expect the epoch OID
    {
        const auto epochField = field + "Epoch";
        auto epochElem = obj[epochField];
        if (epochElem.type() == jstOID) {
            version._epoch = epochElem.OID();
            fullVersion = true;
        } else if (!epochElem.eoo()) {
            return {ErrorCodes::TypeMismatch,
                    str::stream() << "Invalid type " << epochElem.type()
                                  << " for version epoch part."};
        }
    }

    // Expect the timestamp
    {
        const auto timestampField = field + "Timestamp";
        auto timestampElem = obj[timestampField];
        if (fullVersion) {
            if (timestampElem.type() == bsonTimestamp) {
                version._timestamp = timestampElem.timestamp();
            } else if (timestampElem.eoo() && version.is50IgnoredOrUnsharded()) {
                // In 5.0 binaries, the timestamp is not present in UNSHARDED and IGNORED versions
                version._timestamp =
                    (version.epoch() == UNSHARDED().epoch()) ? Timestamp() : Timestamp::max();
            } else {
                // TODO (SERVER-64813): remove this code once 6.0 becomes lastLTS
                // Hack to solve a complex problem related to the addition of the timestamp in 5.0
                uasserted(ErrorCodes::StaleShardVersion,
                          str::stream()
                              << "Failed to parse { epoch: " << version._epoch.toString()
                              << ", combined: " << version._combined
                              << "} as a ChunkVersion because it is missing the timestamp field.");
            }
        } else {
            invariant(timestampElem.eoo());
        }
    }

    return version;
}

void ChunkVersion::appendWithField(BSONObjBuilder* out, StringData field) const {
    BSONArrayBuilder arr(out->subarrayStart(field));
    arr.appendTimestamp(_combined);
    arr.append(_epoch);
    arr.append(_timestamp);
}

void ChunkVersion::appendLegacyWithField(BSONObjBuilder* out, StringData field) const {
    out->appendTimestamp(field, _combined);
    out->append(field + "Epoch", _epoch);
    out->append(field + "Timestamp", _timestamp);
}

BSONObj ChunkVersion::toBSON() const {
    BSONArrayBuilder b;
    b.appendTimestamp(_combined);
    b.append(_epoch);
    b.append(_timestamp);
    return b.arr();
}

void ChunkVersion::legacyToBSON(StringData field, BSONObjBuilder* out) const {
    out->appendTimestamp(field, this->toLong());
}

std::string ChunkVersion::toString() const {
    return str::stream() << majorVersion() << "|" << minorVersion() << "||" << _epoch << "||"
                         << _timestamp.toString();
}

}  // namespace mongo
