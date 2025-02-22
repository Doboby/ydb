#include "service_table.h"
#include <ydb/core/grpc_services/base/base.h>
#include "rpc_calls.h"
#include "rpc_scheme_base.h"
#include "rpc_common.h"
#include "table_settings.h"

#include <ydb/core/cms/console/configs_dispatcher.h>
#include <ydb/core/engine/mkql_proto.h>
#include <ydb/core/protos/console_config.pb.h>
#include <ydb/core/ydb_convert/column_families.h>
#include <ydb/core/ydb_convert/table_description.h>
#include <ydb/core/ydb_convert/table_profiles.h>

namespace NKikimr {
namespace NGRpcService {

using namespace NSchemeShard;
using namespace NActors;
using namespace NConsole;
using namespace Ydb;
using namespace Ydb::Table;

using TEvCreateTableRequest = TGrpcRequestOperationCall<Ydb::Table::CreateTableRequest,
    Ydb::Table::CreateTableResponse>;

class TCreateTableRPC : public TRpcSchemeRequestActor<TCreateTableRPC, TEvCreateTableRequest> {
    using TBase = TRpcSchemeRequestActor<TCreateTableRPC, TEvCreateTableRequest>;

public:
    TCreateTableRPC(IRequestOpCtx* msg)
        : TBase(msg) {}

    void Bootstrap(const TActorContext &ctx) {
        TBase::Bootstrap(ctx);

        SendConfigRequest(ctx);
        ctx.Schedule(TDuration::Seconds(15), new TEvents::TEvWakeup(WakeupTagGetConfig));
        Become(&TCreateTableRPC::StateGetConfig);
    }

private:
    void StateGetConfig(TAutoPtr<IEventHandle>& ev, const TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            HFunc(TEvConfigsDispatcher::TEvGetConfigResponse, Handle);
            HFunc(TEvents::TEvUndelivered, Handle);
            HFunc(TEvents::TEvWakeup, HandleWakeup);
            default: TBase::StateFuncBase(ev, ctx);
        }
    }

    void StateWork(TAutoPtr<IEventHandle>& ev, const TActorContext& ctx) {
        switch (ev->GetTypeRewrite()) {
            default: TBase::StateWork(ev, ctx);
        }
    }

    void Handle(TEvents::TEvUndelivered::TPtr &/*ev*/, const TActorContext &ctx)
    {
        LOG_CRIT_S(ctx, NKikimrServices::GRPC_PROXY,
                   "TCreateTableRPC: cannot deliver config request to Configs Dispatcher"
                   " (empty default profile is available only)");
        SendProposeRequest(ctx);
        Become(&TCreateTableRPC::StateWork);
    }

    void Handle(TEvConfigsDispatcher::TEvGetConfigResponse::TPtr &ev, const TActorContext &ctx) {
        auto &config = ev->Get()->Config->GetTableProfilesConfig();
        Profiles.Load(config);

        SendProposeRequest(ctx);
        Become(&TCreateTableRPC::StateWork);
    }

    void HandleWakeup(TEvents::TEvWakeup::TPtr &ev, const TActorContext &ctx) {
        switch (ev->Get()->Tag) {
            case WakeupTagGetConfig: {
                LOG_CRIT_S(ctx, NKikimrServices::GRPC_PROXY, "TCreateTableRPC: cannot get table profiles (timeout)");
                NYql::TIssues issues;
                issues.AddIssue(NYql::TIssue("Tables profiles config not available."));
                return Reply(StatusIds::UNAVAILABLE, issues, ctx);
            }
            default:
                TBase::HandleWakeup(ev, ctx);
        }
    }

    void SendConfigRequest(const TActorContext &ctx) {
        ui32 configKind = (ui32)NKikimrConsole::TConfigItem::TableProfilesConfigItem;
        ctx.Send(MakeConfigsDispatcherID(ctx.SelfID.NodeId()),
                 new TEvConfigsDispatcher::TEvGetConfigRequest(configKind),
                 IEventHandle::FlagTrackDelivery);
    }

    // Mutually exclusive settings
    void MEWarning(const TString& settingName) {
        Request_->RaiseIssue(
            NYql::TIssue(TStringBuilder() << "Table profile and " << settingName
                << " are set. They are mutually exclusive. Use either one of them.")
            .SetCode(NKikimrIssues::TIssuesIds::WARNING, NYql::TSeverityIds::S_WARNING)
        );
    }

    void SendProposeRequest(const TActorContext &ctx) {
        const auto req = GetProtoRequest();
        std::pair<TString, TString> pathPair;
        try {
            pathPair = SplitPath(Request_->GetDatabaseName(), req->path());
        } catch (const std::exception& ex) {
            Request_->RaiseIssue(NYql::ExceptionToIssue(ex));
            return Reply(StatusIds::BAD_REQUEST, ctx);
        }

        const auto& workingDir = pathPair.first;
        const auto& name = pathPair.second;
        if (!req->columnsSize()) {
            auto issue = NYql::TIssue("At least one column shoult be in table");
            Request_->RaiseIssue(issue);
            return Reply(StatusIds::BAD_REQUEST, ctx);
        }

        if (!req->primary_keySize()) {
            auto issue = NYql::TIssue("At least one primary key should be specified");
            Request_->RaiseIssue(issue);
            return Reply(StatusIds::BAD_REQUEST, ctx);
        }

        std::unique_ptr<TEvTxUserProxy::TEvProposeTransaction> proposeRequest = CreateProposeTransaction();
        NKikimrTxUserProxy::TEvProposeTransaction& record = proposeRequest->Record;
        NKikimrSchemeOp::TModifyScheme* modifyScheme = record.MutableTransaction()->MutableModifyScheme();
        modifyScheme->SetWorkingDir(workingDir);
        NKikimrSchemeOp::TTableDescription* tableDesc = nullptr;
        if (req->indexesSize()) {
            modifyScheme->SetOperationType(NKikimrSchemeOp::EOperationType::ESchemeOpCreateIndexedTable);
            tableDesc = modifyScheme->MutableCreateIndexedTable()->MutableTableDescription();
        } else {
            modifyScheme->SetOperationType(NKikimrSchemeOp::EOperationType::ESchemeOpCreateTable);
            tableDesc = modifyScheme->MutableCreateTable();
        }

        tableDesc->SetName(name);

        StatusIds::StatusCode code = StatusIds::SUCCESS;
        TString error;

        if (!FillColumnDescription(*tableDesc, req->columns(), code, error)) {
            NYql::TIssues issues;
            issues.AddIssue(NYql::TIssue(error));
            return Reply(code, issues, ctx);
        }

        tableDesc->MutableKeyColumnNames()->CopyFrom(req->primary_key());

        if (!FillIndexDescription(*modifyScheme->MutableCreateIndexedTable(), *req, code, error)) {
            NYql::TIssues issues;
            issues.AddIssue(NYql::TIssue(error));
            return Reply(code, issues, ctx);
        }

        bool tableProfileSet = false;
        if (req->has_profile()) {
            const auto& profile = req->profile();
            tableProfileSet = profile.preset_name() || profile.has_compaction_policy() || profile.has_execution_policy()
                || profile.has_partitioning_policy() || profile.has_storage_policy() || profile.has_replication_policy()
                || profile.has_caching_policy();
        }

        if (!Profiles.ApplyTableProfile(req->profile(), *tableDesc, code, error)) {
            NYql::TIssues issues;
            issues.AddIssue(NYql::TIssue(error));
            return Reply(code, issues, ctx);
        }

        TColumnFamilyManager families(tableDesc->MutablePartitionConfig());

        // Apply storage settings to the default column family
        if (req->has_storage_settings()) {
            if (tableProfileSet) {
                MEWarning("StorageSettings");
            }
            if (!families.ApplyStorageSettings(req->storage_settings(), &code, &error)) {
                NYql::TIssues issues;
                issues.AddIssue(NYql::TIssue(error));
                return Reply(code, issues, ctx);
            }
        }

        if (tableProfileSet && req->column_familiesSize()) {
            MEWarning("ColumnFamilies");
        }
        for (const auto& familySettings : req->column_families()) {
            if (!families.ApplyFamilySettings(familySettings, &code, &error)) {
                NYql::TIssues issues;
                issues.AddIssue(NYql::TIssue(error));
                return Reply(code, issues, ctx);
            }
        }

        if (families.Modified && !families.ValidateColumnFamilies(&code, &error)) {
            NYql::TIssues issues;
            issues.AddIssue(NYql::TIssue(error));
            return Reply(code, issues, ctx);
        }

        // Attributes
        for (auto [key, value] : req->attributes()) {
            auto& attr = *modifyScheme->MutableAlterUserAttributes()->AddUserAttributes();
            attr.SetKey(key);
            attr.SetValue(value);
        }

        TList<TString> warnings;
        if (!FillCreateTableSettingsDesc(*tableDesc, *req, Profiles, code, error, warnings)) {
            NYql::TIssues issues;
            issues.AddIssue(NYql::TIssue(error));
            return Reply(code, issues, ctx);
        }
        for (const auto& warning : warnings) {
            Request_->RaiseIssue(
                NYql::TIssue(warning)
                .SetCode(NKikimrIssues::TIssuesIds::WARNING, NYql::TSeverityIds::S_WARNING)
            );
        }

        ctx.Send(MakeTxProxyID(), proposeRequest.release());
    }

private:
    TTableProfiles Profiles;
};

void DoCreateTableRequest(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider &) {
    TActivationContext::AsActorContext().Register(new TCreateTableRPC(p.release()));
}

} // namespace NGRpcService
} // namespace NKikimr
