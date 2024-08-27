/**
 *    Copyright (C) 2024-present MongoDB, Inc.
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

#include "mongo/bson/bson_validate.h"
#include "mongo/bson/bsonelement.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/bson/util/bsoncolumn.h"
#include "mongo/bson/util/bsoncolumn_test_util.h"
#include "mongo/bson/util/bsoncolumnbuilder.h"
#include "mongo/bson/util/simple8b_helpers.h"

using namespace mongo;

// in element creation functions, we use a common elementMemory as storage
// to hold content in scope for the lifetime on the fuzzer

BSONElement createBSONColumn(const char* buffer,
                             int size,
                             std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendBinData(""_sd, size, BinDataType::Column, buffer);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

template <typename T>
BSONElement createElement(T val, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.append("0"_sd, val);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createElementDouble(double val, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.append("0"_sd, val);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createObjectId(OID val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createTimestamp(Timestamp val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createElementInt64(int64_t val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createElementInt32(int32_t val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createElementDecimal128(Decimal128 val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createDate(Date_t dt, std::forward_list<BSONObj>& elementMemory) {
    return createElement(dt, elementMemory);
}

BSONElement createBool(bool b, std::forward_list<BSONObj>& elementMemory) {
    return createElement(b, elementMemory);
}

BSONElement createElementMinKey(std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendMinKey("0"_sd);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createElementMaxKey(std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendMaxKey("0"_sd);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createNull(std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendNull("0"_sd);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createUndefined(std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendUndefined("0"_sd);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createRegex(StringData options, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendRegex("0"_sd, options);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createDBRef(StringData ns, const OID& oid, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendDBRef("0"_sd, ns, oid);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createElementCode(StringData code, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendCode("0"_sd, code);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createCodeWScope(StringData code,
                             const BSONObj& scope,
                             std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendCodeWScope("0"_sd, code, scope);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createSymbol(StringData symbol, std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendSymbol("0"_sd, symbol);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createElementBinData(BinDataType binDataType,
                                 const char* buf,
                                 size_t len,
                                 std::forward_list<BSONObj>& elementMemory) {
    BSONObjBuilder ob;
    ob.appendBinData("f", len, binDataType, buf);
    elementMemory.emplace_front(ob.obj());
    return elementMemory.front().firstElement();
}

BSONElement createElementString(StringData val, std::forward_list<BSONObj>& elementMemory) {
    return createElement(val, elementMemory);
}

BSONElement createElementObj(BSONObj obj, std::forward_list<BSONObj>& elementMemory) {
    return createElement(obj, elementMemory);
}

BSONElement createElementArray(BSONArray arr, std::forward_list<BSONObj>& elementMemory) {
    return createElement(arr, elementMemory);
}

bool createFuzzedObj(const char*& ptr,
                     const char* end,
                     std::forward_list<BSONObj>& elementMemory,
                     BSONObj& result);


// We restrict lengths of generated bufs to 25 bytes, this is not exhaustive but is
// enough to exercise the ways bsoncolumn behaves with these data (i.e. having some
// length variance both above and below the 128-bit cutoff where strings are treated
// differently by bsoncolumn) and adding more would slow down the fuzzer in finding
// edge cases more than help
constexpr size_t kMaxBufLength = 25;

// Reusable code for generating fuzzed buf content
bool generateBuf(const char*& ptr, const char* end, const char* buf, size_t& len) {
    // Generate len
    if (static_cast<size_t>(end - ptr) < sizeof(uint8_t))
        return false;
    uint8_t lenRead;
    memcpy(&lenRead, ptr, sizeof(uint8_t));
    len = lenRead % (kMaxBufLength + 1);
    ptr += sizeof(uint8_t);

    // Pull out buf
    if (static_cast<size_t>(end - ptr) < len)
        return false;
    memcpy((void*)buf, ptr, len);
    ptr += len;
    return true;
};

/**
 * Interpret the fuzzer input as a distribution on the full range of valid
 * BSONElement that could be passed to the builder.
 *
 * We could try to make this more compact, i.e. using the minimum number
 * of distinct bytes possible to represent the distribution of valid BSONElement.
 * However this is not what would most assist the fuzzer in exploring the
 * BSONElement space in a manner that exercises as many of the edge cases in the
 * code as quickly as possible.
 *
 * The fuzzer will try to build up a library of input strings, saving ones that
 * reach new code paths, and mutating to produce new ones by doing byte inserts,
 * deletes, and substitutions.  Thus, when a new code path is reached and we save
 * a new input string, we want the saved string to represent just the amount of
 * input that got us to the new path, without carrying "extra" state that would
 * point us to the next element or sub-element, since having such extra state
 * would restrain the variety of mutations we follow up with.
 *
 * Therefore, rather than trying to make the encoding compact by maximizing
 * utilization of the range of values, it is better to have each byte have
 * distinct meaning, and allow the fuzzer to navigate each range we want to
 * exercise along byte boundaries.  So we will use a distinct byte for type, and
 * use the next byte for content, etc.
 *
 * Additionally we will reuse values in the byte to make all 256 values have
 * semantic meaning, even if redundant, to minimize the times the fuzzer needs to
 * reject strings and reattempt mutations to find new BSONElement to feed to the
 * builder.
 *
 * ptr - pointer into the original fuzzer input, will be advanced
 * end - end of the original fuzzer input
 * elementMemory - needs to stay in scope for the lifetime of when we expect
 *                 generated elements to remain valid
 * repetition - number of instances to emit, may be discarded or used by caller
 * result - receives new BSONElement
 * return - true if successful, false if fuzzer input is invalid
 */
bool createFuzzedElement(const char*& ptr,
                         const char* end,
                         std::forward_list<BSONObj>& elementMemory,
                         int& repetition,
                         BSONElement& result) {
    if (ptr >= end)
        return false;

    // Interpret first byte as a BSONType inclusively
    // Valid types range from -1 to 19 and 127
    uint8_t typeRun = *ptr;
    ptr++;
    // There are 22 distinct types, interpret every possible value as one of them
    uint8_t typeMagnitude = typeRun % 22;
    BSONType type = typeMagnitude <= 19 ? static_cast<BSONType>(typeMagnitude)
                                        :  // EOO - NumberDecimal
        typeMagnitude == 20 ? BSONType::MaxKey
                            :  // reinterpret 20 -> 127
        BSONType::MinKey;      // reinterpret 21 -> -1
    // Use the remainder of the type entropy to represent repetition factor, this helps
    // bias the probability to trigger the RLE encoding more often
    //
    // We effectively have 3 remaining bits of entropy to work with, we use this to add
    // any or all of +1 (for more 0 deltas), +120 (the minimum amount to create an RLE
    // block), and +(16 * 120) (the maximum amount in an RLE block)
    //
    // Effectively, this means in a single execution we could create a run of
    // 1+1+120+(16 * 120) = 2042
    // More individual variety in how much of each tier we add will come from the fuzzer's
    // natural variation and random creation of duplicate elements
    uint8_t repetitionFactor = typeMagnitude / 22;
    repetition = 1;
    if (repetitionFactor % 2 == 1)  // 1st bit: add one more repetition
        repetition++;
    repetitionFactor /= 2;
    if (repetitionFactor % 2 == 1)  // 2nd bit: add one more rle block
        repetition += mongo::simple8b_internal::kRleMultiplier;
    repetitionFactor /= 2;
    if (repetitionFactor % 2 == 1)  // 3rd bit: add a max rle block
        repetition +=
            mongo::simple8b_internal::kRleMultiplier * mongo::simple8b_internal::kMaxRleCount;

    // Construct a BSONElement based on type and add it to the generatedElements vector
    size_t len;
    char buf[kMaxBufLength];
    switch (type) {
        case Array: {
            // Get a count up to 255
            uint8_t count;
            if (ptr >= end)
                return false;
            count = static_cast<uint8_t>(*ptr);
            ptr++;

            UniqueBSONArrayBuilder bab;
            for (uint8_t i = 0; i < count; ++i) {
                BSONElement elem;
                int dummy;  // do not use repetition for arrays; we don't rle on this axis
                if (!createFuzzedElement(ptr, end, elementMemory, dummy, elem))
                    return false;
                if (elem.eoo())
                    return false;
                bab.append(elem);
            }
            bab.done();
            result = createElementArray(bab.arr(), elementMemory);
            return true;
        }
        case BinData: {
            if (ptr >= end)
                return false;
            uint8_t binDataTypeMagnitude = *ptr;
            ptr++;
            binDataTypeMagnitude %= 10;
            BinDataType binDataType = binDataTypeMagnitude <= 8
                ? static_cast<BinDataType>(binDataTypeMagnitude)
                : BinDataType::bdtCustom;
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            result = createElementBinData(binDataType, &buf[0], len, elementMemory);
            return true;
        }
        case Code: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            result = createElementCode(StringData(buf, len), elementMemory);
            return true;
        }
        case CodeWScope: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            BSONObj obj;
            if (!createFuzzedObj(ptr, end, elementMemory, obj))
                return false;
            result = createCodeWScope(StringData(buf, len), obj, elementMemory);
            return true;
        }
        case DBRef: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            // Initialize an OID from a 12 byte array
            if (end - ptr < 12)
                return false;
            unsigned char arr[12];
            memcpy(arr, ptr, 12);
            ptr += 12;
            OID oid(arr);
            result = createDBRef(StringData(buf, len), oid, elementMemory);
            return true;
        }
        case Object: {
            BSONObj obj;
            if (!createFuzzedObj(ptr, end, elementMemory, obj))
                return false;
            result = createElementObj(obj, elementMemory);
            return true;
        }
        case RegEx: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            result = createRegex(StringData(buf, len), elementMemory);
            return true;
        }
        case String: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            result = createElementString(StringData(buf, len), elementMemory);
            return true;
        }
        case Symbol: {
            if (!generateBuf(ptr, end, &buf[0], len))
                return false;
            result = createSymbol(StringData(buf, len), elementMemory);
            return true;
        }
        case Bool: {
            if (ptr >= end)
                return false;
            result = createBool(*ptr % 2 == 1, elementMemory);
            ptr++;
            return true;
        }
        case bsonTimestamp: {
            if (static_cast<size_t>(end - ptr) < sizeof(long long))
                return false;
            long long val;
            memcpy(&val, ptr, sizeof(long long));
            ptr += sizeof(long long);
            Timestamp timestamp(val);
            result = createTimestamp(timestamp, elementMemory);
            return true;
        }
        case Date: {
            if (static_cast<size_t>(end - ptr) < sizeof(long long))
                return false;
            long long millis;
            memcpy(&millis, ptr, sizeof(long long));
            ptr += sizeof(long long);
            Date_t val = Date_t::fromMillisSinceEpoch(millis);
            result = createDate(val, elementMemory);
            return true;
        }
        case EOO: {
            result = BSONElement();
            return true;
        }
        case jstNULL: {
            result = createNull(elementMemory);
            return true;
        }
        case jstOID: {
            // Initialize an OID from a 12 byte array
            if (end - ptr < 12)
                return false;
            unsigned char arr[12];
            memcpy(arr, ptr, 12);
            ptr += 12;
            OID val(arr);
            result = createObjectId(val, elementMemory);
            return true;
        }
        case MaxKey: {
            result = createElementMaxKey(elementMemory);
            return true;
        }
        case MinKey: {
            result = createElementMinKey(elementMemory);
            return true;
        }
        case NumberDecimal: {
            // Initialize a Decimal128 from parts
            if (static_cast<size_t>(end - ptr) < 4 * sizeof(uint64_t))
                return false;
            uint64_t sign, exponent, coeffHigh, coeffLow;
            memcpy(&sign, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            memcpy(&exponent, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            memcpy(&coeffHigh, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            memcpy(&coeffLow, ptr, sizeof(uint64_t));
            ptr += sizeof(uint64_t);
            if (!Decimal128::isValid(sign, exponent, coeffHigh, coeffLow))
                return false;
            Decimal128 val(sign, exponent, coeffHigh, coeffLow);
            result = createElementDecimal128(val, elementMemory);
            return true;
        }
        case NumberDouble: {
            if (static_cast<size_t>(end - ptr) < sizeof(double))
                return false;
            double val;
            memcpy(&val, ptr, sizeof(double));
            ptr += sizeof(double);
            result = createElementDouble(val, elementMemory);
            return true;
        }
        case NumberInt: {
            if (static_cast<size_t>(end - ptr) < sizeof(int32_t))
                return false;
            int32_t val;
            memcpy(&val, ptr, sizeof(int32_t));
            ptr += sizeof(int32_t);
            result = createElementInt32(val, elementMemory);
            return true;
        }
        case NumberLong: {
            if (static_cast<size_t>(end - ptr) < sizeof(int64_t))
                return false;
            int64_t val;
            memcpy(&val, ptr, sizeof(int64_t));
            ptr += sizeof(int64_t);
            result = createElementInt64(val, elementMemory);
            return true;
        }
        case Undefined: {
            result = createUndefined(elementMemory);
            return true;
        }
        default:
            MONGO_UNREACHABLE;
    }

    return false;
}

/* Obj fuzzing requires recursion to handle subobjects
 *
 * ptr - pointer into the original fuzzer input, will be advanced
 * end - end of the original fuzzer input
 * elementMemory - needs to stay in scope for the lifetime of when we expect
 *                 generated elements to remain valid
 * result - receives new BSONObj
 * return - true if successful, false if fuzzer input is invalid
 */
bool createFuzzedObj(const char*& ptr,
                     const char* end,
                     std::forward_list<BSONObj>& elementMemory,
                     BSONObj& result) {
    // Use branching factor of objects of up to 255
    uint8_t count;
    if (ptr >= end)
        return false;
    count = static_cast<uint8_t>(*ptr);
    ptr++;

    BSONObjBuilder bob;
    for (uint8_t i = 0; i < count; ++i) {
        // Generate a field name
        size_t len;
        char buf[kMaxBufLength + 1];  // Extra byte to hold terminating 0
        if (!generateBuf(ptr, end, &buf[0], len))
            return false;
        for (size_t i = 0; i < len; ++i)
            if (buf[i] == 0)
                buf[i] = 1;
        buf[len] = 0;

        BSONElement elem;
        int dummy;  // do not use repetition for obj; we don't rle on this axis
        if (!createFuzzedElement(ptr, end, elementMemory, dummy, elem))
            return false;

        if (elem.eoo())
            return false;

        bob.appendAs(elem, StringData(buf, len));
    }
    bob.done();
    result = bob.obj();
    return true;
}


/**
 * Check that the BSONElement sequence passed to BSONColumnBuilder does not
 * fatal, and that the result decodes to the original sequence we passed.
 */
extern "C" int LLVMFuzzerTestOneInput(const char* Data, size_t Size) {
    using namespace mongo;
    std::forward_list<BSONObj> elementMemory;
    std::vector<BSONElement> generatedElements;

    // Generate elements from input data
    const char* ptr = Data;
    const char* end = Data + Size;
    while (ptr < end) {
        BSONElement element;
        int repetition;
        if (createFuzzedElement(ptr, end, elementMemory, repetition, element)) {
            for (int i = 0; i < repetition; ++i)
                generatedElements.push_back(element);
        } else {
            return 0;  // bad input string, continue fuzzer
        }
    }

    // Exercise the builder
    BSONColumnBuilder builder;
    for (auto element : generatedElements) {
        builder.append(element);
    }

    // Verify decoding gives us original elements
    BSONBinData binData = builder.finalize();
    BSONObjBuilder obj;
    obj.append(""_sd, binData);
    BSONElement columnElement = obj.done().firstElement();
    BSONColumn col(columnElement);
    auto it = col.begin();
    for (auto elem : generatedElements) {
        BSONElement other = *it;
        invariant(elem.binaryEqualValues(other),
                  str::stream() << "Decoded element " << it->toString()
                                << " does not match original " << elem.toString());
        invariant(it.more(), "There were fewer decoded elements than original");
        ++it;
    }
    invariant(!it.more(), "There were more decoded elements than original");

    return 0;
}
