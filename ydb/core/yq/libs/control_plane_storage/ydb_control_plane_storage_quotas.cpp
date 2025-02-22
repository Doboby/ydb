#include "ydb_control_plane_storage_impl.h"

#include <cstdint>

#include <util/datetime/base.h>
#include <util/generic/yexception.h>
#include <util/string/join.h>

#include <ydb/core/yq/libs/common/entity_id.h>
#include <ydb/core/yq/libs/control_plane_storage/events/events.h>
#include <ydb/core/yq/libs/control_plane_storage/schema.h>
#include <ydb/core/yq/libs/db_schema/db_schema.h>
#include <ydb/core/yq/libs/quota_manager/quota_manager.h>

#include <ydb/public/api/protos/yq.pb.h>
#include <ydb/public/sdk/cpp/client/ydb_value/value.h>

#include <util/digest/multi.h>

#include <ydb/core/yq/libs/shared_resources/db_exec.h>

namespace NYq {

using TQuotaCountExecuter = TDbExecuter<THashMap<TString, ui32>>;

void TYdbControlPlaneStorageActor::Handle(TEvQuotaService::TQuotaUsageRequest::TPtr& ev) {

    if (ev->Get()->SubjectType != SUBJECT_TYPE_CLOUD 
        || (ev->Get()->MetricName != QUOTA_ANALYTICS_COUNT_LIMIT && ev->Get()->MetricName != QUOTA_STREAMING_COUNT_LIMIT) ) {
        Send(ev->Sender, new TEvQuotaService::TQuotaUsageResponse(ev->Get()->SubjectType, ev->Get()->SubjectId, ev->Get()->MetricName, 0));
    }

    if (QuotasUpdatedAt + Config.QuotaTtl > Now()) {
        Send(ev->Sender, new TEvQuotaService::TQuotaUsageResponse(SUBJECT_TYPE_CLOUD, ev->Get()->SubjectId, ev->Get()->MetricName, QueryQuotas.Value(ev->Get()->SubjectId, 0)));
    }

    QueryQuotaRequests[ev->Get()->SubjectId] = ev;

    if (QuotasUpdating) {
        return;
    }

    QuotasUpdating = true;
    QueryQuotas.clear();

    TDbExecutable::TPtr executable;
    auto& executer = TQuotaCountExecuter::Create(executable, [](TQuotaCountExecuter& executer) { executer.State.clear(); } );

    executer.Read(
        [=](TQuotaCountExecuter&, TSqlQueryBuilder& builder) {
            builder.AddText(
                "SELECT `" SCOPE_COLUMN_NAME "`, COUNT(`" SCOPE_COLUMN_NAME "`) AS PENDING_COUNT\n"
                "FROM `" PENDING_SMALL_TABLE_NAME "`\n"
                "GROUP BY `" SCOPE_COLUMN_NAME "`\n"
            );
        },
        [=](TQuotaCountExecuter& executer, const TVector<NYdb::TResultSet>& resultSets) {
            TResultSetParser parser(resultSets.front());
            while (parser.TryNextRow()) {
                auto scope = *parser.ColumnParser(SCOPE_COLUMN_NAME).GetOptionalString();
                auto count = parser.ColumnParser("PENDING_COUNT").GetUint64();
                executer.Read(
                    [=](TQuotaCountExecuter&, TSqlQueryBuilder& builder) {
                        builder.AddText(
                            "SELECT `" INTERNAL_COLUMN_NAME "`\n"
                            "FROM `" QUERIES_TABLE_NAME "`\n"
                            "WHERE `" SCOPE_COLUMN_NAME "` = $scope LIMIT 1;\n"
                        );
                        builder.AddString("scope", scope);
                    },
                    [=](TQuotaCountExecuter& executer, const TVector<NYdb::TResultSet>& resultSets) {
                        TResultSetParser parser(resultSets.front());
                        if (parser.TryNextRow()) {
                            YandexQuery::Internal::QueryInternal internal;
                            ParseProto(executer, internal, parser, INTERNAL_COLUMN_NAME);
                            executer.State[internal.cloud_id()] += count;
                        }
                    },
                    "GetScopeCloud_" + scope
                );
            }
        },
        "GroupByPendingSmall"
    ).Process(SelfId(),
        [=, this](TQuotaCountExecuter& executer) {
            this->QuotasUpdatedAt = Now();
            this->QueryQuotas.swap(executer.State);
            for (auto& it : this->QueryQuotaRequests) {
                auto ev = it.second;
                this->Send(ev->Sender, new TEvQuotaService::TQuotaUsageResponse(SUBJECT_TYPE_CLOUD, it.first, QUOTA_ANALYTICS_COUNT_LIMIT, this->QueryQuotas.Value(it.first, 0)));
                this->Send(ev->Sender, new TEvQuotaService::TQuotaUsageResponse(SUBJECT_TYPE_CLOUD, it.first, QUOTA_STREAMING_COUNT_LIMIT, this->QueryQuotas.Value(it.first, 0)));
            }
            this->QueryQuotaRequests.clear();
            this->QuotasUpdating = false;
        }
    );

    Exec(DbPool, executable);
}

void TYdbControlPlaneStorageActor::Handle(TEvQuotaService::TQuotaLimitChangeRequest::TPtr& ev) {
    auto& record = *ev->Get();
    // LimitRequested is always accepted
    Send(ev->Sender, new TEvQuotaService::TQuotaLimitChangeResponse(record.SubjectType, record.SubjectId, record.MetricName, 
      record.LimitRequested, record.LimitRequested));
}

} // NYq
