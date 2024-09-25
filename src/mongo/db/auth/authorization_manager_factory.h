/**
 *    Copyright (C) 2023-present MongoDB, Inc.
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

#include "mongo/db/auth/authorization_backend_interface.h"
#include "mongo/db/auth/authorization_client_handle.h"
#include "mongo/db/auth/authorization_manager.h"
#include "mongo/db/service_context.h"

namespace mongo {

/**
 * Factory class for generating the correct authorization manager for the
 * process. createRouter creates an authorization manager that connects to
 * config servers to get authorization information, and createShard creates
 * an authorization manager that may search locally for authorization
 * information unless the user is registered to $external.
 */

class AuthorizationManagerFactory {

public:
    virtual ~AuthorizationManagerFactory() = default;

    virtual std::unique_ptr<AuthorizationManager> createRouter(Service* service) = 0;
    virtual std::unique_ptr<AuthorizationManager> createShard(Service* service) = 0;

    // TODO: SERVER-83663 replace create function with create AuthorizationRouter.
    virtual std::unique_ptr<AuthorizationClientHandle> createClientHandleRouter(
        Service* service) = 0;
    virtual std::unique_ptr<AuthorizationClientHandle> createClientHandleShard(
        Service* service) = 0;

    virtual std::unique_ptr<auth::AuthorizationBackendInterface> createBackendInterface(
        Service* service) = 0;
};

extern std::unique_ptr<AuthorizationManagerFactory> globalAuthzManagerFactory;

}  // namespace mongo
