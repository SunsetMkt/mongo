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

#include <cstdint>
#include <utility>

#include "mongo/executor/task_executor_pool.h"
#include "mongo/executor/task_executor_pool_parameters_gen.h"  // IWYU pragma: keep
#include "mongo/util/assert_util_core.h"
#include "mongo/util/processinfo.h"  // IWYU pragma: keep

namespace mongo {
namespace executor {

size_t TaskExecutorPool::getSuggestedPoolSize() {
#if (defined __linux__)
    // Always use a pool of size 1 on Linux machines running mongo v4.2 and higher.
    // Changing it past the default value can cause performance regressions.
    return 1;
#else
    auto poolSize = taskExecutorPoolSize.load();
    if (poolSize > 0) {
        return poolSize;
    }

    ProcessInfo p;
    auto numCores = p.getNumAvailableCores();

    // Never suggest a number outside the range [4, 64].
    return std::max<size_t>(4U, std::min<size_t>(64U, numCores));
#endif  //__linux__
}

void TaskExecutorPool::startup() {
    invariant(!_executors.empty());
    invariant(_fixedExecutor);

    _fixedExecutor->startup();
    for (auto&& exec : _executors) {
        exec->startup();
    }
}

void TaskExecutorPool::shutdownAndJoin() {
    _fixedExecutor->shutdown();
    _fixedExecutor->join();
    for (auto&& exec : _executors) {
        exec->shutdown();
        exec->join();
    }
}

void TaskExecutorPool::shutdown_forTest() {
    _fixedExecutor->shutdown();
    for (auto&& exec : _executors) {
        exec->shutdown();
    }
}

void TaskExecutorPool::join_forTest() {
    _fixedExecutor->join();
    for (auto&& exec : _executors) {
        exec->join();
    }
}

void TaskExecutorPool::addExecutors(std::vector<std::shared_ptr<TaskExecutor>> executors,
                                    std::shared_ptr<TaskExecutor> fixedExecutor) {
    invariant(_executors.empty());
    invariant(fixedExecutor);
    invariant(!_fixedExecutor);

    _fixedExecutor = std::move(fixedExecutor);
    _executors = std::move(executors);
}

const std::shared_ptr<TaskExecutor>& TaskExecutorPool::getArbitraryExecutor() {
    invariant(!_executors.empty());
    uint64_t idx = (_counter.fetchAndAdd(1) % _executors.size());
    return _executors[idx];
}

const std::shared_ptr<TaskExecutor>& TaskExecutorPool::getFixedExecutor() {
    invariant(_fixedExecutor);
    return _fixedExecutor;
}

void TaskExecutorPool::appendConnectionStats(ConnectionPoolStats* stats) const {
    // Get stats from our fixed executor.
    _fixedExecutor->appendConnectionStats(stats);
    // Get stats from our pooled executors.
    for (auto&& executor : _executors) {
        executor->appendConnectionStats(stats);
    }
}

void TaskExecutorPool::appendNetworkInterfaceStats(BSONObjBuilder& bob) const {
    _fixedExecutor->appendNetworkInterfaceStats(bob);
    for (auto&& executor : _executors) {
        executor->appendNetworkInterfaceStats(bob);
    }
}

}  // namespace executor
}  // namespace mongo
