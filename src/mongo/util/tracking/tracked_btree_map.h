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

#pragma once

#include <absl/container/btree_map.h>
#include <memory>
#include <scoped_allocator>

#include "mongo/util/tracking_allocator.h"
#include "mongo/util/tracking_context.h"

namespace mongo {

// TODO use std::scoped_allocator_adaptor. In v4 toolchain its copy-constructor is not nothrow which
// is a requirement for the absl btree_map.
template <class Key, class T, class Compare = std::less<Key>>
using tracked_btree_map =
    absl::btree_map<Key, T, Compare, TrackingAllocator<std::pair<const Key, T>>>;

template <class Key, class T, class Compare = std::less<Key>>
tracked_btree_map<Key, T, Compare> make_tracked_btree_map(TrackingContext& trackingContext) {
    return tracked_btree_map<Key, T, Compare>(trackingContext.makeAllocator<T>());
}

}  // namespace mongo
