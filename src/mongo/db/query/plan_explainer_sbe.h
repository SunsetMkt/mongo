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

#include <boost/move/utility_core.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mongo/base/string_data.h"
#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/db/exec/sbe/stages/stages.h"
#include "mongo/db/exec/sbe/util/debug_print.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/query/explain_options.h"
#include "mongo/db/query/optimizer/explain_interface.h"
#include "mongo/db/query/plan_cache/plan_cache_debug_info.h"
#include "mongo/db/query/plan_explainer.h"
#include "mongo/db/query/plan_summary_stats.h"
#include "mongo/db/query/query_solution.h"
#include "mongo/db/query/sbe_plan_ranker.h"
#include "mongo/db/query/stage_builder/sbe/builder_data.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/duration.h"

namespace mongo {

class PlanExplainerSBEBase : public PlanExplainer {
public:
    PlanExplainerSBEBase(const sbe::PlanStage* root,
                         const stage_builder::PlanStageData* data,
                         const QuerySolution* solution,
                         bool isMultiPlan,
                         bool isCachedPlan,
                         boost::optional<size_t> cachedPlanHash,
                         std::shared_ptr<const plan_cache_debug_info::DebugInfoSBE> debugInfo,
                         RemoteExplainVector* remoteExplains);

    bool isMultiPlan() const final {
        return _isMultiPlan;
    }
    bool isFromCache() const {
        return _isFromPlanCache;
    }
    bool matchesCachedPlan() const;
    const ExplainVersion& getVersion() const final;
    std::string getPlanSummary() const final;
    void getSummaryStats(PlanSummaryStats* statsOut) const final;
    void getSecondarySummaryStats(const NamespaceString& secondaryColl,
                                  PlanSummaryStats* statsOut) const override;
    PlanStatsDetails getWinningPlanStats(ExplainOptions::Verbosity verbosity) const final;

protected:
    static boost::optional<BSONObj> buildExecPlanDebugInfo(
        const sbe::PlanStage* root, const stage_builder::PlanStageData* data) {
        if (root && data) {
            return BSON("slots" << data->debugString() << "stages"
                                << sbe::DebugPrinter().print(*root));
        }
        return boost::none;
    }

    boost::optional<BSONArray> buildRemotePlanInfo() const;

    // These fields are are owned elsewhere (e.g. the PlanExecutor or CandidatePlan).
    const sbe::PlanStage* _root{nullptr};
    const stage_builder::PlanStageData* _rootData{nullptr};

    const bool _isMultiPlan{false};
    const bool _isFromPlanCache{false};
    const boost::optional<size_t> _cachedPlanHash{boost::none};
    // Pre-computed debugging info so we don't necessarily have to collect them from QuerySolution.
    // All plans recovered from the same cached entry share the same debug info.
    const std::shared_ptr<const plan_cache_debug_info::DebugInfoSBE> _debugInfo;
    const RemoteExplainVector* _remoteExplains;
};

/**
 * A PlanExplainer implementation for SBE execution plans that were selected using the SBE
 * multi-planner.
 */
class PlanExplainerSBE final : public PlanExplainerSBEBase {
public:
    PlanExplainerSBE(const sbe::PlanStage* root,
                     const stage_builder::PlanStageData* data,
                     const QuerySolution* solution,
                     std::vector<sbe::plan_ranker::CandidatePlan> rejectedCandidates,
                     bool isMultiPlan,
                     bool isCachedPlan,
                     boost::optional<size_t> cachedPlanHash,
                     std::shared_ptr<const plan_cache_debug_info::DebugInfoSBE> debugInfo,
                     RemoteExplainVector* remoteExplains = nullptr);

    PlanStatsDetails getWinningPlanTrialStats() const final;
    std::vector<PlanStatsDetails> getRejectedPlansStats(
        ExplainOptions::Verbosity verbosity) const final;

private:
    const std::vector<sbe::plan_ranker::CandidatePlan> _rejectedCandidates;
};

class PlanExplainerClassicRuntimePlannerForSBE final : public PlanExplainerSBEBase {
public:
    PlanExplainerClassicRuntimePlannerForSBE(
        const sbe::PlanStage* root,
        const stage_builder::PlanStageData* data,
        const QuerySolution* solution,
        bool isMultiPlan,
        bool isCachedPlan,
        boost::optional<size_t> cachedPlanHash,
        std::shared_ptr<const plan_cache_debug_info::DebugInfoSBE> debugInfo,
        std::unique_ptr<PlanStage> classicRuntimePlannerStage,
        RemoteExplainVector* remoteExplains);

    PlanStatsDetails getWinningPlanTrialStats() const final;
    std::vector<PlanStatsDetails> getRejectedPlansStats(
        ExplainOptions::Verbosity verbosity) const final;

private:
    // Using a pointer to a MultiPlanStage, we can create a classic PlanExplainerImpl from which we
    // can extract the necessary information regarding the classic multi-planner's trial period
    // using the same format as we would for the classic engine.
    const std::unique_ptr<PlanStage> _classicRuntimePlannerStage;
    const std::unique_ptr<PlanExplainer> _classicRuntimePlannerExplainer;
};
}  // namespace mongo
