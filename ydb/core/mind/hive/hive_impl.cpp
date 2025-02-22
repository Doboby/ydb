#include "hive_impl.h"
#include "hive_log.h"
#include <ydb/core/cms/console/console.h>
#include <ydb/core/cms/console/configs_dispatcher.h>
#include <ydb/core/protos/counters_hive.pb.h>
#include <ydb/core/util/tuples.h>
#include <ydb/core/util/yverify_stream.h>
#include <library/cpp/actors/interconnect/interconnect.h>
#include <util/generic/array_ref.h>

template <>
inline IOutputStream& operator <<(IOutputStream& out, const TArrayRef<const NKikimrHive::TDataCentersGroup*>& vec) {
    out << '[';
    for (auto it = vec.begin(); it != vec.end(); ++it) {
        if (it != vec.begin())
            out << ';';
        out << (*it)->ShortDebugString();
    }
    out << ']';
    return out;
}

namespace NKikimr {
namespace NHive {

void THive::Handle(TEvHive::TEvCreateTablet::TPtr& ev) {
    NKikimrHive::TEvCreateTablet& rec = ev->Get()->Record;
    if (rec.HasOwner() && rec.HasOwnerIdx() && rec.HasTabletType() && rec.BindedChannelsSize() != 0) {
        BLOG_D("Handle TEvHive::TEvCreateTablet(" << rec.GetTabletType() << '(' << rec.GetOwner() << ',' << rec.GetOwnerIdx() << "))");
        Execute(CreateCreateTablet(std::move(rec), ev->Sender, ev->Cookie));
    } else {
        BLOG_ERROR("Invalid arguments specified to TEvCreateTablet: " << rec.DebugString());
        THolder<TEvHive::TEvCreateTabletReply> reply = MakeHolder<TEvHive::TEvCreateTabletReply>();
        reply->Record.SetStatus(NKikimrProto::EReplyStatus::ERROR);
        reply->Record.SetErrorReason(NKikimrHive::EErrorReason::ERROR_REASON_INVALID_ARGUMENTS);
        if (rec.HasOwner()) {
            reply->Record.SetOwner(rec.GetOwner());
        }
        if (rec.HasOwnerIdx()) {
            reply->Record.SetOwnerIdx(rec.GetOwnerIdx());
        }
        Send(ev->Sender, reply.Release(), 0, ev->Cookie);
    }
}

void THive::Handle(TEvHive::TEvAdoptTablet::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvAdoptTablet");
    NKikimrHive::TEvAdoptTablet& rec = ev->Get()->Record;
    Y_VERIFY(rec.HasOwner() && rec.HasOwnerIdx() && rec.HasTabletType());
    Execute(CreateAdoptTablet(rec, ev->Sender, ev->Cookie));
}

void THive::Handle(TEvents::TEvPoisonPill::TPtr&) {
    BLOG_D("Handle TEvents::TEvPoisonPill");
    Send(Tablet(), new TEvents::TEvPoisonPill);
}

void THive::Handle(TEvTabletPipe::TEvClientConnected::TPtr& ev) {
    TEvTabletPipe::TEvClientConnected *msg = ev->Get();
    if (msg->ClientId == BSControllerPipeClient && msg->Status != NKikimrProto::OK) {
        RestartBSControllerPipe();
        return;
    }
    if (msg->ClientId == RootHivePipeClient && msg->Status != NKikimrProto::OK) {
        RestartRootHivePipe();
        return;
    }
    if (!PipeClientCache->OnConnect(ev)) {
        BLOG_ERROR("Failed to connect to tablet " << ev->Get()->TabletId << " from tablet " << TabletID());
        RestartPipeTx(ev->Get()->TabletId);
    } else {
        BLOG_D("Connected to tablet " << ev->Get()->TabletId << " from tablet " << TabletID());
    }
}

void THive::Handle(TEvTabletPipe::TEvClientDestroyed::TPtr& ev) {
    TEvTabletPipe::TEvClientDestroyed *msg = ev->Get();
    if (msg->ClientId == BSControllerPipeClient) {
        RestartBSControllerPipe();
        return;
    }
    if (msg->ClientId == RootHivePipeClient) {
        RestartRootHivePipe();
        return;
    }
    BLOG_D("Client pipe to tablet " << ev->Get()->TabletId << " from " << TabletID() << " is reset");
    PipeClientCache->OnDisconnect(ev);
    RestartPipeTx(ev->Get()->TabletId);
}

void THive::RestartPipeTx(ui64 tabletId) {
    for (auto txid : PipeTracker.FindTx(tabletId)) {
        BLOG_D("Pipe reset to tablet " << tabletId << " caused restart of txid# " << txid << " at tablet " << TabletID());
        // TODO: restart all the dependent transactions
    }
}

void THive::Handle(TEvTabletPipe::TEvServerConnected::TPtr& ev) {
    if (ev->Get()->TabletId == TabletID()) {
        BLOG_TRACE("Handle TEvTabletPipe::TEvServerConnected(" << ev->Get()->ClientId << ") " << ev->Get()->ServerId);
        TNodeInfo& node = GetNode(ev->Get()->ClientId.NodeId());
        node.PipeServers.emplace_back(ev->Get()->ServerId);
    }
}

void THive::Handle(TEvTabletPipe::TEvServerDisconnected::TPtr& ev) {
    if (ev->Get()->TabletId == TabletID()) {
        BLOG_TRACE("Handle TEvTabletPipe::TEvServerDisconnected(" << ev->Get()->ClientId << ") " << ev->Get()->ServerId);
        TNodeInfo* node = FindNode(ev->Get()->ClientId.NodeId());
        if (node != nullptr) {
            Erase(node->PipeServers, ev->Get()->ServerId);
            if (node->PipeServers.empty() && node->IsUnknown() && node->CanBeDeleted()) {
                DeleteNode(node->Id);
            }
        }
    }
}

void THive::Handle(TEvLocal::TEvRegisterNode::TPtr& ev) {
    NKikimrLocal::TEvRegisterNode& record = ev->Get()->Record;
    if (record.GetHiveId() == TabletID()) {
        const TActorId &local = ev->Sender;
        BLOG_D("Handle TEvLocal::TEvRegisterNode from " << ev->Sender << " " << record.ShortDebugString());
        Send(GetNameserviceActorId(), new TEvInterconnect::TEvGetNode(ev->Sender.NodeId()));
        Execute(CreateRegisterNode(local, std::move(record)));
    } else {
        BLOG_W("Handle incorrect TEvLocal::TEvRegisterNode from " << ev->Sender << " " << record.ShortDebugString());
    }
}

bool THive::OnRenderAppHtmlPage(NMon::TEvRemoteHttpInfo::TPtr ev, const TActorContext& ctx) {
    if (!Executor() || !Executor()->GetStats().IsActive)
        return false;

    if (!ev)
        return true;

    CreateEvMonitoring(ev, ctx);
    return true;
}

void THive::Handle(TEvHive::TEvStopTablet::TPtr& ev) {
    BLOG_D("Handle StopTablet");
    NKikimrHive::TEvStopTablet& rec = ev->Get()->Record;
    const TActorId actorToNotify = rec.HasActorToNotify() ? ActorIdFromProto(rec.GetActorToNotify()) : ev->Sender;
    if (rec.HasTabletID()) {

    } else {
        Y_ENSURE_LOG(rec.HasTabletID(), rec.ShortDebugString());
        Send(actorToNotify, new TEvHive::TEvStopTabletResult(NKikimrProto::ERROR, 0), 0, ev->Cookie);
    }
}

void THive::Handle(TEvHive::TEvDeleteTablet::TPtr& ev) {
    Execute(CreateDeleteTablet(ev));
}

void THive::Handle(TEvHive::TEvDeleteOwnerTablets::TPtr& ev) {
    Execute(CreateDeleteOwnerTablets(ev));
}

void THive::DeleteTabletWithoutStorage(TLeaderTabletInfo* tablet) {
    Y_ENSURE_LOG(tablet->IsDeleting(), "tablet " << tablet->Id);
    Y_ENSURE_LOG(tablet->TabletStorageInfo->Channels.empty() || tablet->TabletStorageInfo->Channels[0].History.empty(), "tablet " << tablet->Id);

    // Tablet has no storage, so there's nothing to block or delete
    // Simulate a response from CreateTabletReqDelete as if all steps have been completed
    Send(SelfId(), new TEvTabletBase::TEvDeleteTabletResult(NKikimrProto::OK, tablet->Id));
}

void THive::DeleteTabletWithoutStorage(TLeaderTabletInfo* tablet, TSideEffects& sideEffects) {
    Y_ENSURE_LOG(tablet->IsDeleting(), "tablet " << tablet->Id);
    Y_ENSURE_LOG(tablet->TabletStorageInfo->Channels.empty() || tablet->TabletStorageInfo->Channels[0].History.empty(), "tablet " << tablet->Id);

    // Tablet has no storage, so there's nothing to block or delete
    // Simulate a response from CreateTabletReqDelete as if all steps have been completed
    sideEffects.Send(SelfId(), new TEvTabletBase::TEvDeleteTabletResult(NKikimrProto::OK, tablet->Id));
}

void THive::ExecuteProcessBootQueue(NIceDb::TNiceDb& db, TSideEffects& sideEffects) {
    TInstant now = TActivationContext::Now();
    BLOG_D("Handle ProcessBootQueue (size: " << BootQueue.BootQueue.size() << ")");
    THPTimer bootQueueProcessingTimer;
    if (ProcessWaitQueueScheduled) {
        BLOG_D("Handle ProcessWaitQueue (size: " << BootQueue.WaitQueue.size() << ")");
        BootQueue.MoveFromWaitQueueToBootQueue();
        ProcessWaitQueueScheduled = false;
    }
    ProcessBootQueueScheduled = false;
    ui64 processedItems = 0;
    TInstant postponedStart;
    TStackVec<TBootQueue::TBootQueueRecord> delayedTablets;
    while (!BootQueue.BootQueue.empty() && processedItems < GetMaxBootBatchSize()) {
        TBootQueue::TBootQueueRecord record = BootQueue.PopFromBootQueue();
        ++processedItems;
        TTabletInfo* tablet = FindTablet(record.TabletId);
        if (tablet == nullptr) {
            continue;
        }
        if (tablet->IsAlive()) {
            continue;
        }
        if (tablet->IsReadyToStart(now)) {
            TBestNodeResult bestNodeResult = FindBestNode(*tablet);
            if (bestNodeResult.BestNode != nullptr) {
                if (tablet->InitiateStart(bestNodeResult.BestNode)) {
                    continue;
                }
            } else {
                if (!bestNodeResult.TryToContinue) {
                    delayedTablets.push_back(record);
                    break;
                } else {
                    for (const TActorId actorToNotify : tablet->ActorsToNotifyOnRestart) {
                        sideEffects.Send(actorToNotify, new TEvPrivate::TEvRestartComplete(tablet->GetFullTabletId(), "boot delay"));
                    }
                    tablet->ActorsToNotifyOnRestart.clear();
                    if (tablet->IsFollower()) {
                        TLeaderTabletInfo& leader = tablet->GetLeader();
                        UpdateTabletFollowersNumber(leader, db, sideEffects);
                    }
                    BootQueue.AddToWaitQueue(record); // waiting for new node
                    continue;
                }
            }
        } else {
            TInstant tabletPostponedStart = tablet->PostponedStart;
            if (tabletPostponedStart > now) {
                if (postponedStart) {
                    postponedStart = std::min(postponedStart, tabletPostponedStart);
                } else {
                    postponedStart = tabletPostponedStart;
                }
            }
        }
        if (tablet->IsBooting()) {
            delayedTablets.push_back(record);
        }
    }
    for (TBootQueue::TBootQueueRecord record : delayedTablets) {
        BootQueue.AddToBootQueue(record);
    }
    if (TabletCounters != nullptr) {
        UpdateCounterBootQueueSize(BootQueue.BootQueue.size());
        TabletCounters->Simple()[NHive::COUNTER_WAITQUEUE_SIZE].Set(BootQueue.WaitQueue.size());
        TabletCounters->Cumulative()[NHive::COUNTER_BOOTQUEUE_PROCESSED].Increment(1);
        TabletCounters->Cumulative()[NHive::COUNTER_BOOTQUEUE_TIME].Increment(ui64(1000000. * bootQueueProcessingTimer.PassedReset()));
    }
    if (BootQueue.BootQueue.empty()) {
        BLOG_D("ProcessBootQueue - BootQueue empty (WaitQueue: " << BootQueue.WaitQueue.size() << ")");
    }
    if (processedItems > 0) {
        if (processedItems == delayedTablets.size() && postponedStart < now) {
            BLOG_D("ProcessBootQueue - BootQueue throttling (size: " << BootQueue.BootQueue.size() << ")");
            return;
        }
        if (processedItems == GetMaxBootBatchSize()) {
            BLOG_D("ProcessBootQueue - rescheduling");
            ProcessBootQueue();
        } else if (postponedStart > now) {
            BLOG_D("ProcessBootQueue - postponing");
            PostponeProcessBootQueue(postponedStart - now);
        }
    }
}

void THive::Handle(TEvPrivate::TEvProcessBootQueue::TPtr&) {
    BLOG_TRACE("ProcessBootQueue - executing");
    Execute(CreateProcessBootQueue());
}

void THive::Handle(TEvPrivate::TEvPostponeProcessBootQueue::TPtr&) {
    ProcessBootQueuePostponed = false;
    ProcessBootQueue();
}

void THive::ProcessBootQueue() {
    BLOG_D("ProcessBootQueue (" << BootQueue.BootQueue.size() << ")");
    if (!ProcessBootQueueScheduled) {
        BLOG_TRACE("ProcessBootQueue - sending");
        ProcessBootQueueScheduled = true;
        Send(SelfId(), new TEvPrivate::TEvProcessBootQueue());
    }
}

void THive::PostponeProcessBootQueue(TDuration after) {
    if (!ProcessBootQueuePostponed) {
        BLOG_D("PostponeProcessBootQueue (" << after << ")");
        ProcessBootQueuePostponed = true;
        Schedule(after, new TEvPrivate::TEvPostponeProcessBootQueue());
    }
}

void THive::ProcessWaitQueue() {
    BLOG_D("ProcessWaitQueue (" << BootQueue.WaitQueue.size() << ")");
    ProcessWaitQueueScheduled = true;
    ProcessBootQueue();
}

void THive::AddToBootQueue(TTabletInfo* tablet) {
    tablet->UpdateWeight();
    tablet->BootState = BootStateBooting;
    BootQueue.AddToBootQueue(*tablet);
    UpdateCounterBootQueueSize(BootQueue.BootQueue.size());
}

void THive::Handle(TEvPrivate::TEvProcessPendingOperations::TPtr&) {
    BLOG_D("Handle ProcessPendingOperations");
}

void THive::Handle(TEvHive::TEvBootTablet::TPtr& ev) {
    TTabletId tabletId = ev->Get()->Record.GetTabletID();
    TTabletInfo* tablet = FindTablet(tabletId);
    Y_VERIFY(tablet != nullptr);
    if (tablet->IsReadyToBoot()) {
        tablet->InitiateBoot();
    }
}

TVector<TTabletId> THive::UpdateStoragePools(const google::protobuf::RepeatedPtrField<NKikimrBlobStorage::TEvControllerSelectGroupsResult::TGroupParameters>& groups) {
    TVector<TTabletId> tabletsToUpdate;
    std::unordered_map<TString, std::vector<const NKikimrBlobStorage::TEvControllerSelectGroupsResult::TGroupParameters*>> poolToGroup;
    for (const auto& gp : groups) {
        poolToGroup[gp.GetStoragePoolName()].emplace_back(&gp);
    }
    for (const auto& [poolName, groupParams] : poolToGroup) {
        std::unordered_set<TStorageGroupId> groups;
        TStoragePoolInfo& storagePool = GetStoragePool(poolName);
        std::transform(storagePool.Groups.begin(), storagePool.Groups.end(), std::inserter(groups, groups.end()), [](const auto& pr) { return pr.first; });
        for (const NKikimrBlobStorage::TEvControllerSelectGroupsResult::TGroupParameters* group : groupParams) {
            TStorageGroupId groupId = group->GetGroupID();
            groups.erase(groupId);
            storagePool.UpdateStorageGroup(groupId, *group);
        }
        for (TStorageGroupId groupId : groups) {
            storagePool.DeleteStorageGroup(groupId);
        }
        if (storagePool.RefreshRequestInFlight > 0) {
            --storagePool.RefreshRequestInFlight;
        } else {
            BLOG_W("THive::Handle TEvControllerSelectGroupsResult: Out of inflight counter response received");
        }
        storagePool.SetAsFresh();
        storagePool.ConfigurationGeneration = ConfigurationGeneration;
        TVector<TTabletId> tabletsWaiting = storagePool.PullWaitingTablets();
        tabletsToUpdate.reserve(tabletsToUpdate.size() + tabletsWaiting.size());
        for (TTabletId tabletId : tabletsWaiting) {
            tabletsToUpdate.emplace_back(tabletId);
        }
    }
    return tabletsToUpdate;
}

void THive::Handle(TEvBlobStorage::TEvControllerSelectGroupsResult::TPtr& ev) {
    NKikimrBlobStorage::TEvControllerSelectGroupsResult& rec = ev->Get()->Record;
    if (rec.GetStatus() == NKikimrProto::OK) {
        BLOG_D("THive::Handle TEvControllerSelectGroupsResult: success " << rec.ShortDebugString());
        if (rec.MatchingGroupsSize()) {
            TVector<TTabletId> tablets;
            for (const auto& matchingGroups : rec.GetMatchingGroups()) {
                if (matchingGroups.GroupsSize() == 0) {
                    BLOG_ERROR("THive::Handle TEvControllerSelectGroupsResult: BSC didn't return matching groups set");
                    continue;
                }
                TVector<TTabletId> tabletsWaiting = UpdateStoragePools(matchingGroups.GetGroups());
                tablets.reserve(tablets.size() + tabletsWaiting.size());
                for (TTabletId tablet : tabletsWaiting) {
                    tablets.emplace_back(tablet);
                }
            }
            std::sort(tablets.begin(), tablets.end());
            tablets.erase(std::unique(tablets.begin(), tablets.end()), tablets.end());
            for (TTabletId tabletId : tablets) {
                TLeaderTabletInfo* tablet = FindTablet(tabletId);
                if (!tablet) {
                    BLOG_ERROR("THive::Handle TEvControllerSelectGroupsResult: tablet# " << tabletId << " not found");
                } else {
                    Execute(CreateUpdateTabletGroups(tabletId));
                }
            }
        } else {
            BLOG_ERROR("THive::Handle TEvControllerSelectGroupsResult: obsolete BSC response");
        }
    } else {
        BLOG_ERROR("THive::Handle TEvControllerSelectGroupsResult: " << rec.GetStatus());
    }
}

void THive::Handle(TEvLocal::TEvTabletStatus::TPtr& ev) {
    TNodeId nodeId = ev->Sender.NodeId();
    TNodeInfo* node = FindNode(nodeId);
    if (node != nullptr) {
        if (node->IsDisconnected()) {
            BLOG_W("Handle TEvLocal::TEvTabletStatus, NodeId " << nodeId << " disconnected, reconnecting");
            node->SendReconnect(ev->Sender);
            return;
        }
    }
    TEvLocal::TEvTabletStatus* msg = ev->Get();
    NKikimrLocal::TEvTabletStatus& record = msg->Record;
    BLOG_D("Handle TEvLocal::TEvTabletStatus, TabletId: " << record.GetTabletID());
    if (FindTablet(record.GetTabletID(), record.GetFollowerId()) != nullptr) {
        Execute(CreateUpdateTabletStatus(
                    record.GetTabletID(),
                    ev->Sender,
                    record.GetGeneration(),
                    record.GetFollowerId(),
                    static_cast<TEvLocal::TEvTabletStatus::EStatus>(record.GetStatus()),
                    static_cast<TEvTablet::TEvTabletDead::EReason>(record.GetReason())
                ));
    } else {
        BLOG_W("Handle TEvLocal::TEvTabletStatus from node " << nodeId << ", TabletId: " << record.GetTabletID() << " not found");
    }
}

void THive::Handle(TEvPrivate::TEvBootTablets::TPtr&) {
    BLOG_D("Handle BootTablets");
    RequestPoolsInformation();
    for (auto& [id, node] : Nodes) {
        if (node.IsUnknown() && node.Local) {
            node.Ping();
        }
    }
    TVector<TTabletId> tabletsToReleaseFromParent;
    TSideEffects sideEffects;
    sideEffects.Reset(SelfId());
    for (auto& tab : Tablets) {
        TLeaderTabletInfo& tablet = tab.second;
        if (tablet.NeedToReleaseFromParent) {
            BLOG_D("Need to release from parent tablet " << tablet.ToString());
            tabletsToReleaseFromParent.push_back(tablet.Id);
        } else if (tablet.IsReadyToBoot()) {
            tablet.InitiateBoot();
        } else if (tablet.IsReadyToAssignGroups()) {
            tablet.InitiateAssignTabletGroups();
        } else if (tablet.IsReadyToBlockStorage()) {
            tablet.InitiateBlockStorage(sideEffects);
        } else if (tablet.IsDeleting()) {
            if (!tablet.InitiateBlockStorage(sideEffects, std::numeric_limits<ui32>::max())) {
                DeleteTabletWithoutStorage(&tablet);
            }
        } else if (tablet.IsStopped() && tablet.State == ETabletState::Stopped) {
            ReportStoppedToWhiteboard(tablet);
            BLOG_D("Report tablet " << tablet.ToString() << " as stopped to Whiteboard");
        } else {
            BLOG_W("The tablet "
                   << tablet.ToString()
                   << " is not ready for anything State:"
                   << ETabletStateName(tablet.State)
                   << " VolatileState:"
                   << TTabletInfo::EVolatileStateName(tablet.GetVolatileState()));
        }
        for (const auto& domain : tablet.EffectiveAllowedDomains) {
            SeenDomain(domain);
        }
        if (tablet.ObjectDomain) {
            SeenDomain(tablet.ObjectDomain);
        }
    }
    sideEffects.Complete(DEPRECATED_CTX);
    SignalTabletActive(DEPRECATED_CTX);
    ReadyForConnections = true;
    if (AreWeRootHive()) {
        BLOG_D("Root Hive is ready");
    } else {
        BLOG_D("SubDomain Hive is ready");

        if (!TabletOwnersSynced) {
            // this code should be removed later
            THolder<TEvHive::TEvRequestTabletOwners> request(new TEvHive::TEvRequestTabletOwners());
            request->Record.SetOwnerID(TabletID());
            BLOG_D("Requesting TabletOwners from the Root");
            SendToRootHivePipe(request.Release());
            // this code should be removed later
        }
    }
    if (!tabletsToReleaseFromParent.empty()) {
        THolder<TEvHive::TEvReleaseTablets> request(new TEvHive::TEvReleaseTablets());
        request->Record.SetNewOwnerID(TabletID());
        for (TTabletId tabletId : tabletsToReleaseFromParent) {
            request->Record.AddTabletIDs(tabletId);
        }
        SendToRootHivePipe(request.Release());
    }
    ProcessPendingOperations();
}

void THive::Handle(TEvHive::TEvInitMigration::TPtr& ev) {
    BLOG_D("Handle InitMigration " << ev->Get()->Record);
    if (MigrationState == NKikimrHive::EMigrationState::MIGRATION_READY || MigrationState == NKikimrHive::EMigrationState::MIGRATION_COMPLETE) {
        if (ev->Get()->Record.GetMigrationFilter().GetFilterDomain().GetSchemeShard() == 0 && GetMySubDomainKey().GetSchemeShard() == 0) {
            BLOG_ERROR("Migration ignored - unknown domain");
            Send(ev->Sender, new TEvHive::TEvInitMigrationReply(NKikimrProto::ERROR));
            return;
        }
        MigrationFilter = ev->Get()->Record.GetMigrationFilter();
        if (!MigrationFilter.HasFilterDomain()) {
            MigrationFilter.MutableFilterDomain()->CopyFrom(GetMySubDomainKey());
        }
        MigrationState = NKikimrHive::EMigrationState::MIGRATION_ACTIVE;
        // MigrationProgress = 0;
        // ^ we want cumulative statistics
        if (MigrationFilter.GetMaxTabletsToSeize() == 0) {
            MigrationFilter.SetMaxTabletsToSeize(1);
        }
        MigrationFilter.SetNewOwnerID(TabletID());
        BLOG_D("Requesting migration " << MigrationFilter.ShortDebugString());
        SendToRootHivePipe(new TEvHive::TEvSeizeTablets(MigrationFilter));
        Send(ev->Sender, new TEvHive::TEvInitMigrationReply(NKikimrProto::OK));
    } else {
        BLOG_D("Migration already in progress " << MigrationProgress);
        Send(ev->Sender, new TEvHive::TEvInitMigrationReply(NKikimrProto::ALREADY));
    }
}

void THive::Handle(TEvHive::TEvQueryMigration::TPtr& ev) {
    BLOG_D("Handle QueryMigration");
    Send(ev->Sender, new TEvHive::TEvQueryMigrationReply(MigrationState, MigrationProgress));
}

void THive::OnDetach(const TActorContext&) {
    BLOG_D("THive::OnDetach");
    Cleanup();
    PassAway();
}

void THive::OnTabletDead(TEvTablet::TEvTabletDead::TPtr&, const TActorContext&) {
    BLOG_I("OnTabletDead: " << TabletID());
    Cleanup();
    return PassAway();
}

void THive::BuildLocalConfig() {
    LocalConfig.Clear();
    if (ResourceProfiles)
        ResourceProfiles->StoreProfiles(*LocalConfig.MutableResourceProfiles());
}

void THive::BuildCurrentConfig() {
    BLOG_D("THive::BuildCurrentConfig ClusterConfig = " << ClusterConfig.ShortDebugString());
    CurrentConfig = ClusterConfig;
    BLOG_D("THive::BuildCurrentConfig DatabaseConfig = " << DatabaseConfig.ShortDebugString());
    CurrentConfig.MergeFrom(DatabaseConfig);
    BLOG_D("THive::BuildCurrentConfig CurrentConfig = " << CurrentConfig.ShortDebugString());
    TabletLimit.clear();
    for (const auto& tabletLimit : CurrentConfig.GetDefaultTabletLimit()) {
        TabletLimit.emplace(tabletLimit.GetType(), tabletLimit);
    }
    DefaultDataCentersPreference.clear();
    for (const NKikimrConfig::THiveTabletPreference& tabletPreference : CurrentConfig.GetDefaultTabletPreference()) {
        DefaultDataCentersPreference[tabletPreference.GetType()] = tabletPreference.GetDataCentersPreference();
    }
}

void THive::Cleanup() {
    BLOG_D("THive::Cleanup");

    Send(NConsole::MakeConfigsDispatcherID(SelfId().NodeId()),
        new NConsole::TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest());

    while (!SubActors.empty()) {
        SubActors.front()->Cleanup();
    }

    PipeClientCache->Detach(DEPRECATED_CTX);

    if (BSControllerPipeClient) {
        NTabletPipe::CloseClient(SelfId(), BSControllerPipeClient);
        BSControllerPipeClient = TActorId();
    }

    if (RootHivePipeClient) {
        NTabletPipe::CloseClient(SelfId(), RootHivePipeClient);
        RootHivePipeClient = TActorId();
    }

    if (ResponsivenessPinger) {
        ResponsivenessPinger->Detach(TlsActivationContext->ActorContextFor(ResponsivenessActorID));
        ResponsivenessPinger = nullptr;
    }
}

void THive::Handle(TEvLocal::TEvStatus::TPtr& ev) {
    BLOG_D("Handle TEvLocal::TEvStatus for Node " << ev->Sender.NodeId() << ": " << ev->Get()->Record.ShortDebugString());
    Execute(CreateStatus(ev->Sender, ev->Get()->Record));
}

void THive::Handle(TEvLocal::TEvSyncTablets::TPtr& ev) {
    BLOG_D("THive::Handle::TEvSyncTablets");
    Execute(CreateSyncTablets(ev->Sender, ev->Get()->Record));
}

void THive::Handle(TEvPrivate::TEvProcessDisconnectNode::TPtr& ev) {
    TAutoPtr<TEvPrivate::TEvProcessDisconnectNode> event = ev->Release();
    TNodeInfo& node = GetNode(event->NodeId);
    if (node.IsDisconnecting()) {
        auto itCategory = event->Tablets.begin();
        if (itCategory != event->Tablets.end()) {
            BLOG_D("THive::Handle::TEvProcessDisconnectNode: Node " << event->NodeId << " Category " << itCategory->first);
            for (std::pair<TTabletId, TFollowerId> tabletId : itCategory->second) {
                TTabletInfo* tablet = FindTablet(tabletId);
                if (tablet != nullptr) {
                    if (tablet->IsAlive()) {
                        Execute(CreateRestartTablet(tabletId));
                    }
                }
            }
            event->Tablets.erase(itCategory);
        }
        ScheduleDisconnectNode(event);
    }
}

void THive::Handle(TEvHive::TEvTabletMetrics::TPtr& ev) {
    TNodeId nodeId = ev->Sender.NodeId();
    BLOG_TRACE("THive::Handle::TEvTabletMetrics, NodeId " << nodeId << " " << ev->Get()->Record.ShortDebugString());
    if (UpdateTabletMetricsInProgress < MAX_UPDATE_TABLET_METRICS_IN_PROGRESS) {
        UpdateTabletMetricsInProgress++;
        if (UpdateTabletMetricsInProgress > (MAX_UPDATE_TABLET_METRICS_IN_PROGRESS / 2)) {
            BLOG_W("THive::Handle::TEvTabletMetrics, NodeId " << nodeId << " transactions in progress is over 50% of MAX_UPDATE_TABLET_METRICS_IN_PROGRESS");
        }
        Execute(CreateUpdateTabletMetrics(ev));
    } else {
        BLOG_ERROR("THive::Handle::TEvTabletMetrics, NodeId " << nodeId << " was skipped due to reaching of MAX_UPDATE_TABLET_METRICS_IN_PROGRESS");
        Send(ev->Sender, new TEvLocal::TEvTabletMetricsAck);
    }
}

void THive::Handle(TEvInterconnect::TEvNodeConnected::TPtr &ev) {
    TNodeId nodeId = ev->Get()->NodeId;
    if (ConnectedNodes.insert(nodeId).second) {
        BLOG_W("Handle TEvInterconnect::TEvNodeConnected, NodeId " << nodeId << " Cookie " << ev->Cookie);
        Send(GetNameserviceActorId(), new TEvInterconnect::TEvGetNode(nodeId));
    } else {
        BLOG_TRACE("Handle TEvInterconnect::TEvNodeConnected (duplicate), NodeId " << nodeId << " Cookie " << ev->Cookie);
    }
}

void THive::Handle(TEvInterconnect::TEvNodeDisconnected::TPtr &ev) {
    TNodeId nodeId = ev->Get()->NodeId;
    BLOG_W("Handle TEvInterconnect::TEvNodeDisconnected, NodeId " << nodeId);
    ConnectedNodes.erase(nodeId);
    Execute(CreateDisconnectNode(THolder<TEvInterconnect::TEvNodeDisconnected>(ev->Release().Release())));
}

void THive::Handle(TEvInterconnect::TEvNodeInfo::TPtr &ev) {
    THolder<TEvInterconnect::TNodeInfo>& node = ev->Get()->Node;
    if (node) {
        TEvInterconnect::TNodeInfo& nodeInfo = *node;
        NodesInfo[node->NodeId] = nodeInfo;
        TNodeInfo* hiveNodeInfo = FindNode(nodeInfo.NodeId);
        if (hiveNodeInfo != nullptr) {
            hiveNodeInfo->Location = nodeInfo.Location;
            hiveNodeInfo->LocationAcquired = true;
            BLOG_D("TEvInterconnect::TEvNodeInfo NodeId " << nodeInfo.NodeId << " Location " << GetLocationString(hiveNodeInfo->Location));
        }
    }
}

void THive::Handle(TEvInterconnect::TEvNodesInfo::TPtr &ev) {
    THashSet<TDataCenterId> dataCenters;
    for (const TEvInterconnect::TNodeInfo& node : ev->Get()->Nodes) {
        NodesInfo[node.NodeId] = node;
        dataCenters.insert(node.Location.GetDataCenterId());
    }
    dataCenters.erase(0); // remove default data center id if exists
    if (!dataCenters.empty()) {
        if (DataCenters != dataCenters.size()) {
            DataCenters = dataCenters.size();
            BLOG_D("TEvInterconnect::TEvNodesInfo DataCenters=" << DataCenters << " RegisteredDataCenters=" << RegisteredDataCenters);
        }
    }
    Execute(CreateLoadEverything());
}

void THive::ScheduleDisconnectNode(THolder<TEvPrivate::TEvProcessDisconnectNode> event) {
    auto itCategory = event->Tablets.begin();
    if (itCategory != event->Tablets.end()) {
        TTabletCategoryInfo& category = GetTabletCategory(itCategory->first);
        TDuration spentTime = TActivationContext::Now() - event->StartTime;
        TDuration disconnectTimeout = TDuration::MilliSeconds(category.MaxDisconnectTimeout);
        if (disconnectTimeout > spentTime) {
            Schedule(disconnectTimeout - spentTime, event.Release());
        } else {
            Send(SelfId(), event.Release());
        }
    } else {
        KillNode(event->NodeId, event->Local);
    }
}

void THive::Handle(TEvPrivate::TEvKickTablet::TPtr &ev) {
    TFullTabletId tabletId(ev->Get()->TabletId);
    TTabletInfo* tablet = FindTablet(tabletId);
    if (tablet == nullptr) {
        BLOG_W("THive::Handle::TEvKickTablet" <<
                   " TabletId=" << tabletId <<
                   " tablet not found");
        return;
    }

    if (!tablet->IsAlive()) {
        BLOG_D("THive::Handle::TEvKickTablet" <<
                    " TabletId=" << tabletId <<
                    " tablet isn't alive");
        return;
    }

    BLOG_D("THive::Handle::TEvKickTablet TabletId=" << tabletId);
    TBestNodeResult result = FindBestNode(*tablet);
    if (result.BestNode == nullptr) {
        Execute(CreateRestartTablet(tabletId));
    } else if (result.BestNode != tablet->Node) {
        if (IsTabletMoveExpedient(*tablet, *result.BestNode)) {
            Execute(CreateRestartTablet(tabletId));
        }
    }
}

void THive::Handle(TEvHive::TEvInitiateBlockStorage::TPtr& ev) {
    TTabletId tabletId = ev->Get()->TabletId;
    BLOG_D("THive::Handle::TEvInitiateBlockStorage TabletId=" << tabletId);
    TSideEffects sideEffects;
    sideEffects.Reset(SelfId());
    TLeaderTabletInfo* tablet = FindTabletEvenInDeleting(tabletId);
    if (tablet != nullptr) {
        if (tablet->IsDeleting()) {
            if (!tablet->InitiateBlockStorage(sideEffects, std::numeric_limits<ui32>::max())) {
                DeleteTabletWithoutStorage(tablet);
            }
        } else
        if (tablet->IsReadyToBlockStorage()) {
            tablet->InitiateBlockStorage(sideEffects);
        }
    }
    sideEffects.Complete(DEPRECATED_CTX);
}

void THive::Handle(TEvHive::TEvInitiateDeleteStorage::TPtr &ev) {
    TTabletId tabletId = ev->Get()->TabletId;
    BLOG_D("THive::Handle::TEvInitiateDeleteStorage TabletId=" << tabletId);
    TSideEffects sideEffects;
    sideEffects.Reset(SelfId());
    TLeaderTabletInfo* tablet = FindTabletEvenInDeleting(tabletId);
    if (tablet != nullptr) {
        tablet->InitiateDeleteStorage(sideEffects);
    }
    sideEffects.Complete(DEPRECATED_CTX);
}

void THive::Handle(TEvHive::TEvGetTabletStorageInfo::TPtr& ev) {
    TTabletId tabletId = ev->Get()->Record.GetTabletID();
    BLOG_D("THive::Handle::TEvGetTabletStorageInfo TabletId=" << tabletId);

    TLeaderTabletInfo* tablet = FindTabletEvenInDeleting(tabletId);
    if (tablet == nullptr) {
        // Tablet doesn't exist
        Send(
            ev->Sender,
            new TEvHive::TEvGetTabletStorageInfoResult(tabletId, NKikimrProto::ERROR, "Tablet doesn't exist"),
            0, ev->Cookie);
        return;
    }

    switch (tablet->State) {
    case ETabletState::Unknown:
    case ETabletState::StoppingInGroupAssignment:
        // Subscribing in these states doesn't make sense, as it will never complete
        BLOG_ERROR("Requesting TabletStorageInfo with tablet State="
                << ETabletStateName(tablet->State));
        Send(
            ev->Sender,
            new TEvHive::TEvGetTabletStorageInfoResult(tabletId, NKikimrProto::ERROR, "Tablet is in an unexpected state"),
            0, ev->Cookie);
        break;
    case ETabletState::Deleting:
    case ETabletState::GroupAssignment:
        // We need to subscribe until group assignment or deletion is finished
        tablet->StorageInfoSubscribers.emplace_back(ev->Sender);
        Send(ev->Sender, new TEvHive::TEvGetTabletStorageInfoRegistered(tabletId), 0, ev->Cookie);
        break;
    default:
        // Return what we have right now
        Send(
            ev->Sender,
            new TEvHive::TEvGetTabletStorageInfoResult(tabletId, *tablet->TabletStorageInfo),
            0, ev->Cookie);
        break;
    }
}

void THive::Handle(TEvents::TEvUndelivered::TPtr &ev) {
    BLOG_W("THive::Handle::TEvUndelivered Sender=" << ev->Sender << ", Type=" << ev->Get()->SourceType );
    switch (ev->Get()->SourceType) {
    case TEvLocal::EvBootTablet: {
        // restart boot of the tablet (on different node)
        TTabletId tabletId = ev->Cookie;
        TLeaderTabletInfo* tablet = FindTablet(tabletId);
        if (tablet != nullptr && tablet->IsStarting()) {
            Execute(CreateRestartTablet(tablet->GetFullTabletId()));
        }
        break;
    }
    case TEvLocal::EvPing: {
        TNodeId nodeId = ev->Cookie;
        TNodeInfo* node = FindNode(nodeId);
        if (node != nullptr && ev->Sender == node->Local) {
            if (node->IsDisconnecting()) {
                // ping continiousily until we fully disconnected from the node
                node->Ping();
            } else {
                KillNode(node->Id, node->Local);
            }
        }
        break;
    }
    };
}

void THive::Handle(TEvHive::TEvReassignTablet::TPtr &ev) {
    BLOG_D("THive::TEvReassignTablet " << ev->Get()->Record.ShortUtf8DebugString());
    TLeaderTabletInfo* tablet = FindTablet(ev->Get()->Record.GetTabletID());
    if (tablet != nullptr) {
        tablet->ChannelProfileReassignReason = ev->Get()->Record.GetReassignReason();
        std::bitset<MAX_TABLET_CHANNELS> channelProfileNewGroup;
        const auto& record(ev->Get()->Record);
        auto channels = tablet->GetChannelCount();
        for (ui32 channel : record.GetChannels()) {
            if (channel >= channels) {
                break;
            }
            channelProfileNewGroup.set(channel);
        }
        auto forcedGroupsSize = record.ForcedGroupIDsSize();
        if (forcedGroupsSize > 0) {
            TVector<NKikimrBlobStorage::TEvControllerSelectGroupsResult::TGroupParameters> groups;
            tablet->ChannelProfileNewGroup = channelProfileNewGroup;
            groups.resize(forcedGroupsSize);
            for (ui32 i = 0; i < forcedGroupsSize; ++i) {
                ui32 channel = record.GetChannels(i);
                Y_VERIFY(channel < channels);
                THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters> parameters = BuildGroupParametersForChannel(*tablet, channel);
                if (parameters->HasStoragePoolSpecifier()) {
                    groups[i].SetStoragePoolName(parameters->GetStoragePoolSpecifier().GetName());
                }
                if (parameters->HasErasureSpecies()) {
                    groups[i].SetErasureSpecies(parameters->GetErasureSpecies());
                }
                groups[i].SetGroupID(record.GetForcedGroupIDs(i));
            }
            Execute(CreateUpdateTabletGroups(tablet->Id, std::move(groups)));
        } else {
            Execute(CreateReassignGroups(tablet->Id, ev.Get()->Sender, channelProfileNewGroup));
        }
    }
}

void THive::OnActivateExecutor(const TActorContext&) {
    BLOG_D("THive::OnActivateExecutor");
    TDomainsInfo* domainsInfo = AppData()->DomainsInfo.Get();
    HiveUid = domainsInfo->GetDefaultHiveUid(domainsInfo->Domains.begin()->first);
    HiveDomain = domainsInfo->GetHiveDomainUid(HiveUid);
    const TDomainsInfo::TDomain& domain = domainsInfo->GetDomain(HiveDomain);
    RootHiveId = domainsInfo->GetHive(domain.DefaultHiveUid);
    Y_VERIFY(HiveUid != Max<ui32>() && HiveDomain != TDomainsInfo::BadDomainId);
    HiveId = TabletID();
    HiveGeneration = Executor()->Generation();
    RootDomainKey = TSubDomainKey(domain.SchemeRoot, 1);
    RootDomainName = "/" + domain.Name;
    Executor()->RegisterExternalTabletCounters(TabletCountersPtr);
    ResourceProfiles = AppData()->ResourceProfiles ? AppData()->ResourceProfiles : new TResourceProfiles;
    BuildLocalConfig();
    ClusterConfig = AppData()->HiveConfig;
    Send(NConsole::MakeConfigsDispatcherID(SelfId().NodeId()),
        new NConsole::TEvConfigsDispatcher::TEvSetConfigSubscriptionRequest(NKikimrConsole::TConfigItem::HiveConfigItem));
    Execute(CreateInitScheme());
    if (!ResponsivenessPinger) {
        ResponsivenessPinger = new TTabletResponsivenessPinger(TabletCounters->Simple()[NHive::COUNTER_RESPONSE_TIME_USEC], TDuration::Seconds(1));
        ResponsivenessActorID = RegisterWithSameMailbox(ResponsivenessPinger);
    }
}

void THive::DefaultSignalTabletActive(const TActorContext& ctx) {
    Y_UNUSED(ctx);
}


void THive::AssignTabletGroups(TLeaderTabletInfo& tablet) {
    ui32 channels = tablet.GetChannelCount();
    THashSet<TString> storagePoolsToRefresh;
    // was this method called for the first time for this tablet?
    bool firstInvocation = tablet.ChannelProfileNewGroup.none();

    for (ui32 channelId = 0; channelId < channels; ++channelId) {
        if (firstInvocation || tablet.ChannelProfileNewGroup.test(channelId)) {
            tablet.ChannelProfileNewGroup.set(channelId);
            TStoragePoolInfo& storagePool = tablet.GetStoragePool(channelId);
            if (!storagePool.IsFresh() || storagePool.ConfigurationGeneration != ConfigurationGeneration) {
                storagePoolsToRefresh.insert(storagePool.Name);
            }
        }
    }

    if (!storagePoolsToRefresh.empty()) {
        // we need to refresh storage pool state from BSC
        TVector<THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters>> requests;
        for (TString storagePoolName : storagePoolsToRefresh) {
            TStoragePoolInfo& storagePool = GetStoragePool(storagePoolName);
            if (storagePool.AddTabletToWait(tablet.Id)) {
                THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters> item = storagePool.BuildRefreshRequest();
                ++storagePool.RefreshRequestInFlight;
                requests.emplace_back(std::move(item));
            }
        }
        if (!requests.empty()) {
            THolder<TEvBlobStorage::TEvControllerSelectGroups> ev = MakeHolder<TEvBlobStorage::TEvControllerSelectGroups>();
            NKikimrBlobStorage::TEvControllerSelectGroups& record = ev->Record;
            record.SetReturnAllMatchingGroups(true);
            for (auto& request : requests) {
                record.MutableGroupParameters()->AddAllocated(std::move(request).Release());
            }
            BLOG_D("THive::AssignTabletGroups TEvControllerSelectGroups tablet " << tablet.Id << " " << ev->Record.ShortDebugString());
            SendToBSControllerPipe(ev.Release());
        } else {
            BLOG_D("THive::AssignTabletGroups TEvControllerSelectGroups tablet " << tablet.Id << " waiting for response");
        }
    } else {
        // we ready to update tablet groups immediately
        BLOG_D("THive::AssignTabletGroups CreateUpdateTabletGroups tablet " << tablet.Id);
        Execute(CreateUpdateTabletGroups(tablet.Id));
    }
}

void THive::SendToBSControllerPipe(IEventBase* payload) {
    if (!BSControllerPipeClient) {
        Y_VERIFY(AppData()->DomainsInfo);
        ui32 domainUid = HiveDomain;
        ui64 defaultStateStorageGroup = AppData()->DomainsInfo->GetDefaultStateStorageGroup(domainUid);

        NTabletPipe::TClientConfig pipeConfig;
        pipeConfig.RetryPolicy = NTabletPipe::TClientRetryPolicy::WithRetries();
        BSControllerPipeClient = Register(NTabletPipe::CreateClient(
            SelfId(), MakeBSControllerID(defaultStateStorageGroup), pipeConfig));
    }
    NTabletPipe::SendData(SelfId(), BSControllerPipeClient, payload);
}

void THive::SendToRootHivePipe(IEventBase* payload) {
    if (!RootHivePipeClient) {
        NTabletPipe::TClientConfig pipeConfig;
        pipeConfig.RetryPolicy = NTabletPipe::TClientRetryPolicy::WithRetries();
        RootHivePipeClient = Register(NTabletPipe::CreateClient(SelfId(), RootHiveId, pipeConfig));
    }
    NTabletPipe::SendData(SelfId(), RootHivePipeClient, payload);
}

void THive::RestartBSControllerPipe() {
    BLOG_D("THive::RestartBSControllerPipe");
    if (BSControllerPipeClient) {
        NTabletPipe::CloseClient(SelfId(), BSControllerPipeClient);
        BSControllerPipeClient = TActorId();
    }
    RequestPoolsInformation();
    for (auto it = Tablets.begin(); it != Tablets.end(); ++it) {
        TLeaderTabletInfo& tablet(it->second);
        if (tablet.IsReadyToAssignGroups()) {
            tablet.ResetTabletGroupsRequests();
            tablet.InitiateAssignTabletGroups();
        }
    }
}

void THive::RestartRootHivePipe() {
    BLOG_D("THive::RestartRootHivePipe");
    if (RootHivePipeClient) {
        NTabletPipe::CloseClient(SelfId(), RootHivePipeClient);
        RootHivePipeClient = TActorId();
    }
    // trying to retry for free sequence request
    if (RequestingSequenceNow) {
        RequestFreeSequence();
    }
    // trying to restart migration
    if (MigrationState == NKikimrHive::EMigrationState::MIGRATION_ACTIVE) {
        SendToRootHivePipe(new TEvHive::TEvSeizeTablets(MigrationFilter));
    }
}

void THive::Handle(TEvTabletBase::TEvBlockBlobStorageResult::TPtr &ev) {
    Execute(CreateBlockStorageResult(ev));
}

void THive::Handle(TEvTabletBase::TEvDeleteTabletResult::TPtr &ev) {
    Execute(CreateDeleteTabletResult(ev));
}

template <>
TNodeInfo* THive::SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM>(const std::vector<THive::TSelectedNode>& selectedNodes) {
    if (selectedNodes.empty()) {
        return nullptr;
    }
    return selectedNodes[TAppData::RandomProvider->GenRand() % selectedNodes.size()].Node;
}

template <>
TNodeInfo* THive::SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_WEIGHTED_RANDOM>(const std::vector<THive::TSelectedNode>& selectedNodes) {
    if (selectedNodes.empty()) {
        return nullptr;
    }
    double sumUsage = 0;
    double maxUsage = 0;
    for (const TSelectedNode& selectedNode : selectedNodes) {
        double usage = selectedNode.Usage;
        sumUsage += usage;
        maxUsage = std::max(maxUsage, usage);
    }
    double sumAvail = maxUsage * selectedNodes.size() - sumUsage;
    if (sumAvail > 0) {
        double pos = TAppData::RandomProvider->GenRandReal2() * sumAvail;
        for (const TSelectedNode& selectedNode : selectedNodes) {
            double avail = maxUsage - selectedNode.Usage;
            if (pos < avail) {
                return selectedNode.Node;
            } else {
                pos -= avail;
            }
        }
    }
    return SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM>(selectedNodes);
}

template <>
TNodeInfo* THive::SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_EXACT_MIN>(const std::vector<THive::TSelectedNode>& selectedNodes) {
    if (selectedNodes.empty()) {
        return nullptr;
    }
    auto itMin = std::min_element(selectedNodes.begin(), selectedNodes.end());
    return itMin->Node;
}

template <>
TNodeInfo* THive::SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM_MIN_7P>(const std::vector<THive::TSelectedNode>& selectedNodes) {
    if (selectedNodes.empty()) {
        return nullptr;
    }
    std::vector<TSelectedNode> nodes(selectedNodes);
    auto itNode = nodes.begin();
    auto itPartition = itNode;
    size_t percent7 = std::max<size_t>(nodes.size() * 7 / 100, 1);
    std::advance(itPartition, percent7);
    std::nth_element(nodes.begin(), itPartition, nodes.end());
    std::advance(itNode, TAppData::RandomProvider->GenRand64() % percent7);
    return itNode->Node;
}

THive::TBestNodeResult THive::FindBestNode(const TTabletInfo& tablet) {
    BLOG_D("[FBN] Finding best node for tablet " << tablet.ToString());
    BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " family " << tablet.FamilyString());

    if (tablet.PreferredNodeId != 0) {
        TNodeInfo* node = FindNode(tablet.PreferredNodeId);
        if (node != nullptr) {
            if (node->IsAlive() && node->IsAllowedToRunTablet(tablet) && node->IsAbleToScheduleTablet() && node->IsAbleToRunTablet(tablet)) {
                BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " choose node " << node->Id << " because of preferred node");
                return TBestNodeResult(*node);
            }
            if (node->Freeze) {
                BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " preferred to freezed node " << node->Id);
                tablet.BootState = TStringBuilder() << "Preferred to freezed node " << node->Id;
                return TBestNodeResult(true);
            }
        }
    }

    /*
    TNodeInfo* bestNodeInfo = nullptr;
    double bestUsage = 0;
    if (tablet.IsAlive() && tablet.Node->IsAllowedToRunTablet(tablet) && !tablet.Node->IsOverloaded()) {
        bestNodeInfo = &(Nodes.find(tablet.Node->Id)->second);
        bestUsage = tablet.Node->GetNodeUsageForTablet(tablet);
        BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " starting with usage " << Sprintf("%.9f", bestUsage) << " of node " << bestNodeInfo->Id);
    }
    */

    TTabletDebugState debugState;
    std::vector<NKikimrHive::TDataCentersGroup> dataCentersGroupsHolder;
    std::vector<const NKikimrHive::TDataCentersGroup*> dataCentersGroupsPointers;
    TArrayRef<const NKikimrHive::TDataCentersGroup*> dataCentersGroups; // std::span

    if (tablet.IsLeader()) {
        const TLeaderTabletInfo& leader(tablet.GetLeader());
        dataCentersGroups = TArrayRef<const NKikimrHive::TDataCentersGroup*>(
                    const_cast<const NKikimrHive::TDataCentersGroup**>(leader.DataCentersPreference.GetDataCentersGroups().data()),
                    leader.DataCentersPreference.GetDataCentersGroups().size());
        if (dataCentersGroups.empty()) {
            dataCentersGroups = GetDefaultDataCentersPreference(leader.Type);
        }
        if (dataCentersGroups.empty()) {
            if (leader.Category) {
                std::unordered_map<TDataCenterId, ui32> dcTablets;
                for (TLeaderTabletInfo* tab : leader.Category->Tablets) {
                    if (tab->IsAlive()) {
                        TDataCenterId dc = tab->Node->GetDataCenter();
                        dcTablets[dc]++;
                    }
                }
                if (!dcTablets.empty()) {
                    dataCentersGroupsHolder.resize(dcTablets.size());
                    dataCentersGroupsPointers.resize(dcTablets.size());
                    std::vector<TDataCenterId> dcs;
                    for (const auto& [dc, count] : dcTablets) {
                        dcs.push_back(dc);
                    }
                    std::sort(dcs.begin(), dcs.end(), [&](TDataCenterId a, TDataCenterId b) -> bool {
                        return dcTablets[a] > dcTablets[b];
                    });
                    for (size_t i = 0; i < dcs.size(); ++i) {
                        dataCentersGroupsHolder[i].AddDataCenter(dcs[i]);
                        dataCentersGroupsHolder[i].AddDataCenterNum(DataCenterFromString(dcs[i]));
                        dataCentersGroupsPointers[i] = dataCentersGroupsHolder.data() + i;
                    }
                    dataCentersGroups = TArrayRef<const NKikimrHive::TDataCentersGroup*>(dataCentersGroupsPointers.data(), dcTablets.size());
                }
            }
        }
        if (!dataCentersGroups.empty()) {
            BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " using DC preference: " << dataCentersGroups);
        }
    }

    std::vector<std::vector<TNodeInfo*>> candidateGroups;
    candidateGroups.resize(dataCentersGroups.size() + 1);
    std::unordered_map<TDataCenterId, std::vector<TNodeInfo*>*> indexDC2Group;
    for (size_t numGroup = 0; numGroup < dataCentersGroups.size(); ++numGroup) {
        const NKikimrHive::TDataCentersGroup* dcGroup = dataCentersGroups[numGroup];
        if (dcGroup->DataCenterSize()) {
            for (TDataCenterId dc : dcGroup->GetDataCenter()) {
                indexDC2Group[dc] = candidateGroups.data() + numGroup;
            }
        } else {
            for (const ui64 dcId : dcGroup->GetDataCenterNum()) {
                indexDC2Group[DataCenterToString(dcId)] = candidateGroups.data() + numGroup;
            }
        }
    }
    for (auto it = Nodes.begin(); it != Nodes.end(); ++it) {
        TNodeInfo* nodeInfo = &it->second;
        if (nodeInfo->IsAlive()) {
            TDataCenterId dataCenterId = nodeInfo->GetDataCenter();
            auto itDataCenter = indexDC2Group.find(dataCenterId);
            if (itDataCenter != indexDC2Group.end()) {
                itDataCenter->second->push_back(nodeInfo);
            } else {
                candidateGroups.back().push_back(nodeInfo);
            }
        } else {
            BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " node " << nodeInfo->Id << " is not alive");
            debugState.NodesDead++;
        }
    }

    TVector<TSelectedNode> selectedNodes;

    for (auto itCandidateNodes = candidateGroups.begin(); itCandidateNodes != candidateGroups.end(); ++itCandidateNodes) {
        const std::vector<TNodeInfo*>& candidateNodes(*itCandidateNodes);
        if (candidateGroups.size() > 1) {
            BLOG_TRACE("[FBN] Tablet " << tablet.ToString()
                       << " checking candidates group " << (itCandidateNodes - candidateGroups.begin() + 1)
                       << " of " << candidateGroups.size());
        }

        selectedNodes.clear();
        selectedNodes.reserve(candidateNodes.size());

        for (auto it = candidateNodes.begin(); it != candidateNodes.end(); ++it) {
            TNodeInfo& nodeInfo = *(*it);
            if (nodeInfo.IsAllowedToRunTablet(tablet, &debugState)) {
                if (nodeInfo.IsAbleToScheduleTablet()) {
                    if (nodeInfo.IsAbleToRunTablet(tablet, &debugState)) {
                        double usage = nodeInfo.GetNodeUsageForTablet(tablet);
                        selectedNodes.emplace_back(usage, &nodeInfo);
                        BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " selected usage " << Sprintf("%.9f", usage) << " of node " << nodeInfo.Id);
                    } else {
                        BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " node " << nodeInfo.Id << " is not able to run the tablet");
                    }
                } else {
                    BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " node " << nodeInfo.Id << " is not able to schedule the tablet");
                    tablet.BootState = BootStateTooManyStarting;
                    return TBestNodeResult(false);
                }
            } else {
                BLOG_TRACE("[FBN] Node " << nodeInfo.Id << " is not allowed"
                            << " to run the tablet " << tablet.ToString()
                            << " node domains " << nodeInfo.ServicedDomains
                            << " tablet object domain " << tablet.GetLeader().ObjectDomain
                            << " tablet allowed domains " << tablet.GetLeader().EffectiveAllowedDomains);
            }
        }
        if (!selectedNodes.empty()) {
            break;
        }
    }
    TNodeInfo* selectedNode = nullptr;
    if (!selectedNodes.empty()) {
        switch (GetNodeSelectStrategy()) {
            case NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_WEIGHTED_RANDOM:
                selectedNode = SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_WEIGHTED_RANDOM>(selectedNodes);
                break;
            case NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_EXACT_MIN:
                selectedNode = SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_EXACT_MIN>(selectedNodes);
                break;
            case NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM_MIN_7P:
                selectedNode = SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM_MIN_7P>(selectedNodes);
                break;
            case NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM:
            default:
                selectedNode = SelectNode<NKikimrConfig::THiveConfig::HIVE_NODE_SELECT_STRATEGY_RANDOM>(selectedNodes);
                break;
        }
    }
    if (selectedNode != nullptr) {
        BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " selected node " << selectedNode->Id);
        tablet.BootState = BootStateStarting;
        return TBestNodeResult(*selectedNode);
    } else {
        BLOG_TRACE("[FBN] Tablet " << tablet.ToString() << " no node was selected");

        ui32 nodesLeft = Nodes.size();

        if (tablet.IsFollower() && debugState.LeaderNotRunning) {
            tablet.BootState = BootStateLeaderNotRunning;
            return TBestNodeResult(true);
        }
        if (debugState.NodesDead == nodesLeft) {
            tablet.BootState = BootStateAllNodesAreDead;
            return TBestNodeResult(true);
        }
        nodesLeft -= debugState.NodesDead;
        if (debugState.NodesDown == nodesLeft) {
            tablet.BootState = BootStateAllNodesAreDeadOrDown;
            return TBestNodeResult(true);
        }
        nodesLeft -= debugState.NodesDown;
        if (debugState.NodesNotAllowed + debugState.NodesInDatacentersNotAllowed == nodesLeft) {
            tablet.BootState = BootStateNoNodesAllowedToRun;
            return TBestNodeResult(true);
        }
        nodesLeft -= debugState.NodesNotAllowed;
        nodesLeft -= debugState.NodesInDatacentersNotAllowed;
        if (debugState.NodesWithSomeoneFromOurFamily == nodesLeft) {
            tablet.BootState = BootStateWeFilledAllAvailableNodes;
            return TBestNodeResult(true);
        }
        nodesLeft -= debugState.NodesWithSomeoneFromOurFamily;
        if (debugState.NodesWithoutDomain == nodesLeft) {
            tablet.BootState = TStringBuilder() << "Can't find domain " << tablet.GetLeader().EffectiveAllowedDomains;
            return TBestNodeResult(true);
        }
        nodesLeft -= debugState.NodesWithoutDomain;
        if (tablet.IsFollower() && debugState.NodesFilledWithDatacenterFollowers == nodesLeft) {
            tablet.BootState = BootStateNotEnoughDatacenters;
            return TBestNodeResult(true);
        }
        if (debugState.NodesWithoutResources == nodesLeft) {
            tablet.BootState = BootStateNotEnoughResources;
            return TBestNodeResult(true);
        }
        if (debugState.NodesWithoutLocation == nodesLeft) {
            tablet.BootState = BootStateNodesLocationUnknown;
            return TBestNodeResult(true);
        }

        TStringBuilder state;

        if (debugState.LeaderNotRunning) {
            state << "LeaderNotRunning;";
        }
        if (debugState.NodesDead) {
            state << "NodesDead:" << debugState.NodesDead << ";";
        }
        if (debugState.NodesDown) {
            state << "NodesDown:" << debugState.NodesDown << ";";
        }
        if (debugState.NodesNotAllowed) {
            state << "NodesNotAllowed:" << debugState.NodesNotAllowed << ";";
        }
        if (debugState.NodesInDatacentersNotAllowed) {
            state << "NodesInDatacentersNotAllowed:" << debugState.NodesInDatacentersNotAllowed << ";";
        }
        if (debugState.NodesWithLeaderNotLocal) {
            state << "NodesWithLeaderNotLocal:" << debugState.NodesWithLeaderNotLocal << ";";
        }
        if (debugState.NodesWithoutDomain) {
            state << "NodesWithoutDomain:" << debugState.NodesWithoutDomain << ";";
        }
        if (debugState.NodesFilledWithDatacenterFollowers) {
            state << "NodesFilledWithDatacenterFollowers:" << debugState.NodesFilledWithDatacenterFollowers << ";";
        }
        if (debugState.NodesWithoutResources) {
            state << "NodesWithoutResources:" << debugState.NodesWithoutResources << ";";
        }
        if (debugState.NodesWithSomeoneFromOurFamily) {
            state << "NodesWithSomeoneFromOurFamily:" << debugState.NodesWithSomeoneFromOurFamily << ";";
        }
        if (debugState.NodesWithoutLocation) {
            state << "NodesWithoutLocation:" << debugState.NodesWithoutLocation << ";";
        }
        tablet.BootState = state;

        return TBestNodeResult(true);
    }
}

const TNodeLocation& THive::GetNodeLocation(TNodeId nodeId) const {
    auto it = NodesInfo.find(nodeId);
    if (it != NodesInfo.end())
        return it->second.Location;
    static TNodeLocation defaultLocation;
    return defaultLocation;
}

TNodeInfo& THive::GetNode(TNodeId nodeId) {
    auto it = Nodes.find(nodeId);
    if (it == Nodes.end()) {
        it = Nodes.emplace(std::piecewise_construct, std::tuple<TNodeId>(nodeId), std::tuple<TNodeId, THive&>(nodeId, *this)).first;
    }
    return it->second;
}

TNodeInfo* THive::FindNode(TNodeId nodeId) {
    auto it = Nodes.find(nodeId);
    if (it == Nodes.end())
        return nullptr;
    return &it->second;
}

TLeaderTabletInfo& THive::GetTablet(TTabletId tabletId) {
    auto it = Tablets.find(tabletId);
    if (it == Tablets.end()) {
        it = Tablets.emplace(std::piecewise_construct, std::tuple<TTabletId>(tabletId), std::tuple<TTabletId, THive&>(tabletId, *this)).first;
        UpdateCounterTabletsTotal(+1);
    }
    return it->second;
}

TLeaderTabletInfo* THive::FindTablet(TTabletId tabletId) {
    auto it = Tablets.find(tabletId);
    if (it == Tablets.end() || it->second.IsDeleting())
        return nullptr;
    return &it->second;
}

TLeaderTabletInfo* THive::FindTabletEvenInDeleting(TTabletId tabletId) {
    auto it = Tablets.find(tabletId);
    if (it == Tablets.end())
        return nullptr;
    return &it->second;
}

TTabletInfo& THive::GetTablet(TTabletId tabletId, TFollowerId followerId) {
    TLeaderTabletInfo& leader = GetTablet(tabletId);
    return leader.GetTablet(followerId);
}

TTabletInfo* THive::FindTablet(TTabletId tabletId, TFollowerId followerId) {
    TLeaderTabletInfo* leader = FindTablet(tabletId);
    if (leader == nullptr) {
        return nullptr;
    }
    return leader->FindTablet(followerId);
}

TTabletInfo* THive::FindTabletEvenInDeleting(TTabletId tabletId, TFollowerId followerId) {
    TLeaderTabletInfo* leader = FindTabletEvenInDeleting(tabletId);
    if (leader == nullptr) {
        return nullptr;
    }
    return leader->FindTablet(followerId);
}

TStoragePoolInfo& THive::GetStoragePool(const TString& name) {
    auto it = StoragePools.find(name);
    if (it == StoragePools.end()) {
        it = StoragePools.emplace(std::piecewise_construct, std::tuple<TString>(name), std::tuple<TString, THive*>(name, this)).first;
    }
    return it->second;
}

TStoragePoolInfo* THive::FindStoragePool(const TString& name) {
    auto it = StoragePools.find(name);
    if (it == StoragePools.end()) {
        return nullptr;
    }
    return &it->second;
}

TDomainInfo* THive::FindDomain(TSubDomainKey key) {
    auto it = Domains.find(key);
    if (it == Domains.end()) {
        return nullptr;
    }
    return &it->second;
}

void THive::DeleteTablet(TTabletId tabletId) {
    auto it = Tablets.find(tabletId);
    if (it != Tablets.end()) {
        TLeaderTabletInfo& tablet(it->second);
        tablet.BecomeStopped();
        for (TFollowerTabletInfo& follower : tablet.Followers) {
            follower.BecomeStopped();
        }
        ReportDeletedToWhiteboard(tablet);
        tablet.ReleaseAllocationUnits();
        OwnerToTablet.erase(tablet.Owner);
        if (tablet.Category) {
            tablet.Category->Tablets.erase(&tablet);
        }
        auto itObj = ObjectToTabletMetrics.find(tablet.ObjectId);
        if (itObj != ObjectToTabletMetrics.end()) {
            itObj->second.DecreaseCount();
            if (itObj->second.Counter == 0) {
                ObjectToTabletMetrics.erase(itObj);
            }
        }
        auto itType = TabletTypeToTabletMetrics.find(tablet.Type);
        if (itType != TabletTypeToTabletMetrics.end()) {
            itType->second.DecreaseCount();
            if (itType->second.Counter == 0) {
                TabletTypeToTabletMetrics.erase(itType);
            }
        }
        for (auto nt = Nodes.begin(); nt != Nodes.end(); ++nt) {
            for (auto st = nt->second.Tablets.begin(); st != nt->second.Tablets.end(); ++st) {
                Y_ENSURE_LOG(st->second.count(&tablet) == 0, " Deleting tablet found on node " << nt->first << " in state " << TTabletInfo::EVolatileStateName(st->first));
            }
            Y_ENSURE_LOG(nt->second.LockedTablets.count(&tablet) == 0, " Deleting tablet found on node " << nt->first << " in locked set");
        }
        UpdateCounterTabletsTotal(-1 - (tablet.Followers.size()));
        Tablets.erase(it);
    }
}

void THive::DeleteNode(TNodeId nodeId) {
    Nodes.erase(nodeId);
}

TTabletCategoryInfo& THive::GetTabletCategory(TTabletCategoryId tabletCategoryId) {
    auto it = TabletCategories.find(tabletCategoryId);
    if (it == TabletCategories.end())
        it = TabletCategories.emplace(tabletCategoryId, tabletCategoryId).first; // emplace()
    return it->second;
}

void THive::KillNode(TNodeId nodeId, const TActorId& local) {
    TNodeInfo* node = FindNode(nodeId);
    if (node != nullptr) {
        TVector<TTabletInfo*> tabletsToKill;
        for (const auto& t : node->Tablets) {
            for (TTabletInfo* tablet : t.second) {
                tabletsToKill.push_back(tablet);
            }
        }
        for (TTabletInfo* tablet : tabletsToKill) {
            Execute(CreateRestartTablet(tablet->GetFullTabletId()));
        }
    }
    Execute(CreateKillNode(nodeId, local));
}

void THive::SetCounterTabletsTotal(ui64 tabletsTotal) {
    if (TabletCounters != nullptr) {
        auto& counter = TabletCounters->Simple()[NHive::COUNTER_TABLETS_TOTAL];
        TabletsTotal = tabletsTotal;
        counter.Set(TabletsTotal);
        TabletCounters->Simple()[NHive::COUNTER_STATE_DONE].Set(TabletsTotal == TabletsAlive ? 1 : 0);
    }
}

void THive::UpdateCounterTabletsTotal(i64 tabletsTotalDiff) {
    if (TabletCounters != nullptr) {
        auto& counter = TabletCounters->Simple()[NHive::COUNTER_TABLETS_TOTAL];
        TabletsTotal = counter.Get() + tabletsTotalDiff;
        counter.Set(TabletsTotal);
        TabletCounters->Simple()[NHive::COUNTER_STATE_DONE].Set(TabletsTotal == TabletsAlive ? 1 : 0);
    }
}

void THive::UpdateCounterTabletsAlive(i64 tabletsAliveDiff) {
    if (TabletCounters != nullptr) {
        auto& counter = TabletCounters->Simple()[NHive::COUNTER_TABLETS_ALIVE];
        TabletsAlive = counter.Get() + tabletsAliveDiff;
        counter.Set(TabletsAlive);
        TabletCounters->Simple()[NHive::COUNTER_STATE_DONE].Set(TabletsTotal == TabletsAlive ? 1 : 0);
    }
}

void THive::UpdateCounterBootQueueSize(ui64 bootQueueSize) {
    if (TabletCounters != nullptr) {
        auto& counter = TabletCounters->Simple()[NHive::COUNTER_BOOTQUEUE_SIZE];
        counter.Set(bootQueueSize);
    }
}

bool THive::DomainHasNodes(const TSubDomainKey &domainKey) const {
    return !DomainsView.IsEmpty(domainKey);
}

TResourceNormalizedValues THive::GetStDevResourceValues() const {
    TVector<TResourceNormalizedValues> values;
    values.reserve(Nodes.size());
    for (const auto& ni : Nodes) {
        if (ni.second.IsAlive() && !ni.second.Down) {
            values.push_back(NormalizeRawValues(ni.second.GetResourceCurrentValues(), ni.second.GetResourceMaximumValues()));
        }
    }
    return GetStDev(values);
}

bool THive::IsTabletMoveExpedient(const TTabletInfo& tablet, const TNodeInfo& node) const {
    if (!tablet.IsAlive()) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " to " << node.Id
                   << " is expedient because the tablet is not alive");
        return true;
    }
    if (tablet.Node->Freeze) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is not expedient because the source node is freezed");
        return false;
    }
    if (node.Freeze) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is not expedient because the target node is freezed");
        return false;
    }
    if (tablet.Node->Down) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is expedient because the node is down");
        return true;
    }
    if (!tablet.Node->IsAllowedToRunTablet(tablet)) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is expedient because the current node is unappropriate target for the tablet");
        return true;
    }
    if (tablet.Node->Id == node.Id) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is not expedient because node is the same");
        return false;
    }
    if (tablet.Node->IsOverloaded() && !node.IsOverloaded()) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is forcefully expedient because source node is overloaded");
        return true;
    }

    if (!GetCheckMoveExpediency()) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is forcefully expedient because of the setting");
        return true;
    }

    TVector<TResourceNormalizedValues> values;
    std::size_t oldNode = std::numeric_limits<std::size_t>::max();
    std::size_t newNode = std::numeric_limits<std::size_t>::max();
    values.reserve(Nodes.size());
    for (const auto& ni : Nodes) {
        if (ni.second.IsAlive() && !ni.second.Down) {
            if (ni.first == node.Id)
                newNode = values.size();
            if (ni.first == tablet.Node->Id)
                oldNode = values.size();
            values.push_back(NormalizeRawValues(ni.second.GetResourceCurrentValues(), ni.second.GetResourceMaximumValues()));
        }
    }

    if (oldNode == std::numeric_limits<std::size_t>::max()
            || newNode == std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    auto tabletResources = tablet.GetResourceCurrentValues();
//    NMetrics::TResourceMetrics::TResourceNormalizedValues oldValues = values[oldNode];
//    NMetrics::TResourceMetrics::TResourceNormalizedValues newValues = values[newNode] + NMetrics::TResourceMetrics::Normalize(tabletResources, node.GetResourceMaximumValues());
//    return sum(newValues) < sum(oldValues);

    TResourceNormalizedValues beforeStDev = GetStDev(values);
    values[oldNode] -= NormalizeRawValues(tabletResources, tablet.Node->GetResourceMaximumValues());
    values[newNode] += NormalizeRawValues(tabletResources, node.GetResourceMaximumValues());
    TResourceNormalizedValues afterStDev = GetStDev(values);
    tablet.FilterRawValues(beforeStDev);
    tablet.FilterRawValues(afterStDev);
    double before = max(beforeStDev);
    double after = max(afterStDev);
    bool result = after < before;
    if (result) {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is expedient, beforeStDev " << beforeStDev << " afterStDev " << afterStDev);
    } else {
        BLOG_TRACE("[TME] Move of tablet " << tablet.ToString() << " from " << tablet.NodeId << " to " << node.Id
                   << " is not expedient, beforeStDev " << beforeStDev << " afterStDev " << afterStDev);
    }
    return result;
}

void THive::FillTabletInfo(NKikimrHive::TEvResponseHiveInfo& response, ui64 tabletId, const TLeaderTabletInfo *info, const NKikimrHive::TEvRequestHiveInfo &req) {
    if (info) {
        auto& tabletInfo = *response.AddTablets();
        tabletInfo.SetTabletID(tabletId);
        tabletInfo.SetTabletType(info->Type);
        tabletInfo.SetState(static_cast<ui32>(info->State));
        tabletInfo.SetTabletBootMode(info->BootMode);
        tabletInfo.SetVolatileState(info->GetVolatileState());
        tabletInfo.SetNodeID(info->NodeId);
        tabletInfo.MutableTabletOwner()->SetOwner(info->Owner.first);
        tabletInfo.MutableTabletOwner()->SetOwnerIdx(info->Owner.second);
        tabletInfo.SetGeneration(info->KnownGeneration);
        tabletInfo.MutableObjectDomain()->CopyFrom(info->ObjectDomain);
        if (!info->IsRunning()) {
            tabletInfo.SetLastAliveTimestamp(info->Statistics.GetLastAliveTimestamp());
        }
        tabletInfo.SetRestartsPerPeriod(info->Statistics.RestartTimestampSize());
        if (req.GetReturnMetrics()) {
            tabletInfo.MutableMetrics()->CopyFrom(info->GetResourceValues());
        }
        if (req.GetReturnChannelHistory()) {
            for (const auto& channel : info->TabletStorageInfo->Channels) {
                auto& tabletChannel = *tabletInfo.AddTabletChannels();
                for (const auto& history : channel.History) {
                    auto& tabletHistory = *tabletChannel.AddHistory();
                    tabletHistory.SetGroup(history.GroupID);
                    tabletHistory.SetGeneration(history.FromGeneration);
                    tabletHistory.SetTimestamp(history.Timestamp.MilliSeconds());
                }
            }
        }
        if (req.GetReturnFollowers()) {
            for (const auto& follower : info->Followers) {
                if (req.HasFollowerID() && req.GetFollowerID() != follower.Id)
                    continue;
                NKikimrHive::TTabletInfo& tabletInfo = *response.AddTablets();
                tabletInfo.SetTabletID(tabletId);
                tabletInfo.SetTabletType(info->Type);
                tabletInfo.SetFollowerID(follower.Id);
                tabletInfo.SetVolatileState(follower.GetVolatileState());
                tabletInfo.SetNodeID(follower.NodeId);
                tabletInfo.MutableTabletOwner()->SetOwner(info->Owner.first);
                tabletInfo.MutableTabletOwner()->SetOwnerIdx(info->Owner.second);
                tabletInfo.MutableObjectDomain()->CopyFrom(info->ObjectDomain);
                if (!follower.IsRunning()) {
                    tabletInfo.SetLastAliveTimestamp(follower.Statistics.GetLastAliveTimestamp());
                }
                tabletInfo.SetRestartsPerPeriod(follower.Statistics.RestartTimestampSize());
                if (req.GetReturnMetrics()) {
                    tabletInfo.MutableMetrics()->CopyFrom(follower.GetResourceValues());
                }
            }
        }
    }
}

void THive::Handle(TEvHive::TEvRequestHiveInfo::TPtr& ev) {
    const auto& record = ev->Get()->Record;
    TAutoPtr<TEvHive::TEvResponseHiveInfo> response = new TEvHive::TEvResponseHiveInfo();
    TInstant now = TlsActivationContext->Now();
    if (record.HasTabletID()) {
        TTabletId tabletId = record.GetTabletID();
        NKikimrHive::TForwardRequest forwardRequest;
        if (CheckForForwardTabletRequest(tabletId, forwardRequest)) {
            response->Record.MutableForwardRequest()->CopyFrom(forwardRequest);
        }
        TLeaderTabletInfo* tablet = FindTablet(tabletId);
        if (tablet) {
            tablet->ActualizeTabletStatistics(now);
            FillTabletInfo(response->Record, record.GetTabletID(), tablet, record);
        } else {
            BLOG_W("Can't find the tablet from RequestHiveInfo(TabletID=" << tabletId << ")");
        }
    } else {
        response->Record.MutableTablets()->Reserve(Tablets.size());
        for (auto it = Tablets.begin(); it != Tablets.end(); ++it) {
            if (record.HasTabletType() && record.GetTabletType() != it->second.Type) {
                continue;
            }
            if (it->second.IsDeleting()) {
                continue;
            }
            it->second.ActualizeTabletStatistics(now);
            FillTabletInfo(response->Record, it->first, &it->second, record);
        }
    }

    Send(ev->Sender, response.Release(), 0, ev->Cookie);
}

NKikimrTabletBase::TMetrics& operator +=(NKikimrTabletBase::TMetrics& metrics, const NKikimrTabletBase::TMetrics& toAdd) {
    if (toAdd.HasCPU()) {
        metrics.SetCPU(metrics.GetCPU() + toAdd.GetCPU());
    }
    if (toAdd.HasMemory()) {
        metrics.SetMemory(metrics.GetMemory() + toAdd.GetMemory());
    }
    if (toAdd.HasNetwork()) {
        metrics.SetNetwork(metrics.GetNetwork() + toAdd.GetNetwork());
    }
    if (toAdd.HasStorage()) {
        metrics.SetStorage(metrics.GetStorage() + toAdd.GetStorage());
    }
    if (toAdd.HasReadThroughput()) {
        metrics.SetReadThroughput(metrics.GetReadThroughput() + toAdd.GetReadThroughput());
    }
    if (toAdd.HasWriteThroughput()) {
        metrics.SetWriteThroughput(metrics.GetWriteThroughput() + toAdd.GetWriteThroughput());
    }
    return metrics;
}

void THive::Handle(TEvHive::TEvRequestHiveDomainStats::TPtr& ev) {
    struct TSubDomainStats {
        THashMap<TTabletInfo::EVolatileState, ui32> StateCounter;
        THashSet<TNodeId> NodeIds;
        ui32 AliveNodes = 0;
        NKikimrTabletBase::TMetrics Metrics;
    };

    THashMap<TSubDomainKey, TSubDomainStats> subDomainStats;

    for (auto it = Tablets.begin(); it != Tablets.end(); ++it) {
        const TLeaderTabletInfo& tablet = it->second;
        TSubDomainKey domain = tablet.ObjectDomain;
        TSubDomainStats& stats = subDomainStats[domain];
        stats.StateCounter[tablet.GetVolatileState()]++;
        if (ev->Get()->Record.GetReturnMetrics()) {
            stats.Metrics += tablet.GetResourceValues();
        }
    }

    for (auto it = Nodes.begin(); it != Nodes.end(); ++it) {
        const TNodeInfo& node = it->second;
        for (const TSubDomainKey& domain : node.ServicedDomains) {
            TSubDomainStats& stats = subDomainStats[domain];
            if (node.IsAlive()) {
                stats.AliveNodes++;
                stats.NodeIds.emplace(node.Id);
            }
        }
    }

    THolder<TEvHive::TEvResponseHiveDomainStats> response = MakeHolder<TEvHive::TEvResponseHiveDomainStats>();
    auto& record = response->Record;

    for (const auto& pr1 : subDomainStats) {
        auto& domainStats = *record.AddDomainStats();
        domainStats.SetShardId(pr1.first.first);
        domainStats.SetPathId(pr1.first.second);
        if (ev->Get()->Record.GetReturnMetrics()) {
            domainStats.MutableMetrics()->CopyFrom(pr1.second.Metrics);
        }
        for (const auto& pr2 : pr1.second.StateCounter) {
            auto& stateStats = *domainStats.AddStateStats();
            stateStats.SetVolatileState(pr2.first);
            stateStats.SetCount(pr2.second);
        }
        for (const TNodeId nodeId : pr1.second.NodeIds) {
            domainStats.AddNodeIds(nodeId);
        }
        domainStats.SetAliveNodes(pr1.second.AliveNodes);
    }

    Send(ev->Sender, response.Release(), 0, ev->Cookie);
}

void THive::Handle(TEvHive::TEvRequestHiveNodeStats::TPtr& ev) {
    THolder<TEvHive::TEvResponseHiveNodeStats> response = MakeHolder<TEvHive::TEvResponseHiveNodeStats>();
    auto& record = response->Record;
    for (auto it = Nodes.begin(); it != Nodes.end(); ++it) {
        auto& nodeStats = *record.AddNodeStats();
        const TNodeInfo& node = it->second;
        nodeStats.SetNodeId(node.Id);
        if (!node.ServicedDomains.empty()) {
            nodeStats.MutableNodeDomain()->CopyFrom(node.ServicedDomains.front());
        }
        for (const auto& [state, set] : node.Tablets) {
            if (!set.empty()) {
                auto* stateStats = nodeStats.AddStateStats();
                stateStats->SetVolatileState(state);
                stateStats->SetCount(set.size());
            }
        }
        if (ev->Get()->Record.GetReturnMetrics()) {
            nodeStats.MutableMetrics()->CopyFrom(MetricsFromResourceRawValues(node.GetResourceCurrentValues()));
        }
        if (!node.IsAlive()) {
            nodeStats.SetLastAliveTimestamp(node.Statistics.GetLastAliveTimestamp());
        }
        nodeStats.SetRestartsPerPeriod(node.Statistics.RestartTimestampSize());
    }
    Send(ev->Sender, response.Release(), 0, ev->Cookie);
}

void THive::Handle(TEvHive::TEvRequestHiveStorageStats::TPtr& ev) {
    THolder<TEvHive::TEvResponseHiveStorageStats> response = MakeHolder<TEvHive::TEvResponseHiveStorageStats>();
    auto& record = response->Record;
    for (const auto& [name, pool] : StoragePools) {
        auto& pbPool = *record.AddPools();
        pbPool.SetName(name);
        for (const auto& [id, group] : pool.Groups) {
            auto& pbGroup = *pbPool.AddGroups();
            pbGroup.SetGroupID(id);
            pbGroup.SetAcquiredUnits(group.Units.size());
            pbGroup.SetAcquiredIOPS(group.AcquiredIOPS);
            pbGroup.SetAcquiredThroughput(group.AcquiredThroughput);
            pbGroup.SetAcquiredSize(group.AcquiredSize);
            pbGroup.SetMaximumIOPS(group.MaximumIOPS);
            pbGroup.SetMaximumThroughput(group.MaximumThroughput);
            pbGroup.SetMaximumSize(group.MaximumSize);
            pbGroup.SetAllocatedSize(group.GroupParameters.GetAllocatedSize());
            pbGroup.SetAvailableSize(group.GroupParameters.GetAvailableSize());
        }
    }
    Send(ev->Sender, response.Release(), 0, ev->Cookie);
}

void THive::Handle(TEvHive::TEvLookupTablet::TPtr& ev) {
    const auto& request(ev->Get()->Record);
    TOwnerIdxType::TValueType ownerIdx(request.GetOwner(), request.GetOwnerIdx());
    auto itOwner = OwnerToTablet.find(ownerIdx);
    if (itOwner == OwnerToTablet.end()) {
        Send(ev->Sender, new TEvHive::TEvCreateTabletReply(NKikimrProto::NODATA, ownerIdx.first, ownerIdx.second), 0, ev->Cookie);
    } else {
        Send(ev->Sender, new TEvHive::TEvCreateTabletReply(NKikimrProto::OK, ownerIdx.first, ownerIdx.second, itOwner->second), 0, ev->Cookie);
    }
}

void THive::Handle(TEvHive::TEvLookupChannelInfo::TPtr& ev) {
    const auto& request(ev->Get()->Record);
    const TLeaderTabletInfo* tablet = FindTablet(request.GetTabletID());
    if (tablet == nullptr) {
        Send(ev->Sender, new TEvHive::TEvChannelInfo(NKikimrProto::ERROR, request.GetTabletID()));
        return;
    }
    TAutoPtr<TEvHive::TEvChannelInfo> response = new TEvHive::TEvChannelInfo(NKikimrProto::OK, tablet->Id);
    for (const TTabletChannelInfo& channelInfo : tablet->TabletStorageInfo->Channels) {
        if (request.ChannelsSize() > 0 ) {
            const auto& channels(request.GetChannels());
            if (std::find(channels.begin(), channels.end(), channelInfo.Channel) == channels.end())
                continue;
        }
        NKikimrHive::TChannelInfo* channel = response->Record.AddChannelInfo();
        channel->SetType(channelInfo.Type.GetErasure());
        for (const TTabletChannelInfo::THistoryEntry& historyInfo : channelInfo.History) {
            // TODO
            //if (request.HasForGeneration() && request.GetForGeneration() ? historyInfo.FromGeneration)
            //    continue;
            NKikimrHive::TChannelInfo_THistorySlot* history = channel->AddHistory();
            history->SetGroupID(historyInfo.GroupID);
            history->SetFromGeneration(historyInfo.FromGeneration);
            history->SetTimestamp(historyInfo.Timestamp.MilliSeconds());
        }
    }
    Send(ev->Sender, response.Release());
}

void THive::Handle(TEvHive::TEvCutTabletHistory::TPtr& ev) {
    Execute(CreateCutTabletHistory(ev));
}

void THive::Handle(TEvHive::TEvDrainNode::TPtr& ev) {
    Execute(CreateSwitchDrainOn(ev->Get()->Record.GetNodeID(),
    {
        .Persist = ev->Get()->Record.GetPersist(),
        .KeepDown = ev->Get()->Record.GetKeepDown(),
        .DrainInFlight = ev->Get()->Record.GetDrainInFlight(),
    }, ev->Sender));
}

void THive::Handle(TEvHive::TEvFillNode::TPtr& ev) {
    StartHiveFill(ev->Get()->Record.GetNodeID(), ev->Sender);
}

void THive::Handle(TEvHive::TEvInitiateTabletExternalBoot::TPtr& ev) {
    TTabletId tabletId = ev->Get()->Record.GetTabletID();
    TLeaderTabletInfo* tablet = FindTablet(tabletId);

    if (!tablet) {
        Send(ev->Sender, new TEvHive::TEvBootTabletReply(NKikimrProto::EReplyStatus::ERROR), 0, ev->Cookie);
        BLOG_ERROR("Tablet not found " << tabletId);
        return;
    }

    if (tablet->State == ETabletState::GroupAssignment ||
        tablet->State == ETabletState::BlockStorage)
    {
        Send(ev->Sender, new TEvHive::TEvBootTabletReply(NKikimrProto::EReplyStatus::TRYLATER), 0, ev->Cookie);
        BLOG_W("Tablet waiting for group assignment " << tabletId);
        return;
    }

    if (!tablet->IsBootingSuppressed()) {
        Send(ev->Sender, new TEvHive::TEvBootTabletReply(NKikimrProto::EReplyStatus::ERROR), 0, ev->Cookie);
        BLOG_ERROR("Tablet " << tabletId << " is not expected to boot externally");
        return;
    }

    Execute(CreateStartTablet(TFullTabletId(tabletId, 0), ev->Sender, ev->Cookie, /* external */ true));
}

void THive::Handle(NConsole::TEvConsole::TEvConfigNotificationRequest::TPtr& ev) {
    const NKikimrConsole::TConfigNotificationRequest& record = ev->Get()->Record;
    ClusterConfig = record.GetConfig().GetHiveConfig();
    BLOG_D("Received TEvConsole::TEvConfigNotificationRequest with update of cluster config: " << ClusterConfig.ShortDebugString());
    BuildCurrentConfig();
    Send(ev->Sender, new NConsole::TEvConsole::TEvConfigNotificationResponse(record), 0, ev->Cookie);
}

void THive::Handle(NConsole::TEvConfigsDispatcher::TEvSetConfigSubscriptionResponse::TPtr&) {
    // dummy
}

TResourceRawValues THive::GetDefaultResourceInitialMaximumValues() {
    TResourceRawValues values = {};
    std::get<NMetrics::EResource::Counter>(values) = 100000000; // MaximumTablets?
    std::get<NMetrics::EResource::CPU>(values) = 1000000 * 10; // 1 sec (per second) * 10 threads
    std::get<NMetrics::EResource::Memory>(values) = (ui64)512 << 30; // 512 GB
    std::get<NMetrics::EResource::Network>(values) = 1 << 30; // 10gbit network ~ 1gb/sec
    return values;
}

void THive::ProcessTabletBalancer() {
    if (!ProcessTabletBalancerScheduled && BootQueue.BootQueue.empty()) {
        Schedule(GetMinPeriodBetweenBalance(), new TEvPrivate::TEvProcessTabletBalancer());
        ProcessTabletBalancerScheduled = true;
    }
}

THive::THiveStats THive::GetStats() const {
    struct TNodeStat {
        TNodeId NodeId;
        double Usage;
    };

    THiveStats stats = {};
    TVector<TNodeStat> values;
    values.reserve(Nodes.size());
    for (const auto& ni : Nodes) {
        if (ni.second.IsAlive() && !ni.second.Down) {
            values.push_back({ni.first, ni.second.GetNodeUsage()});
        }
    }
    if (values.empty()) {
        return stats;
    }
    auto it = std::minmax_element(values.begin(), values.end(), [](const TNodeStat& a, const TNodeStat& b) -> bool {
        return a.Usage < b.Usage;
    });
    stats.MaxUsage = it.second->Usage;
    stats.MaxUsageNodeId = it.second->NodeId;
    stats.MinUsage = it.first->Usage;
    stats.MinUsageNodeId = it.first->NodeId;
    if (stats.MaxUsage > 0) {
        double minUsageToBalance = GetMinNodeUsageToBalance();
        double minUsage = std::max(stats.MinUsage, minUsageToBalance);
        double maxUsage = std::max(stats.MaxUsage, minUsageToBalance);
        stats.Scatter = (maxUsage - minUsage) / maxUsage;
    }
    return stats;
}

double THive::GetScatter() const {
    THiveStats stats = GetStats();
    return stats.Scatter;
}

double THive::GetUsage() const {
    THiveStats stats = GetStats();
    return stats.MaxUsage;
}

void THive::Handle(TEvPrivate::TEvProcessTabletBalancer::TPtr&) {
    ProcessTabletBalancerScheduled = false;
    if (!SubActors.empty()) {
        BLOG_D("Balancer has been postponed because of sub activity");
        ProcessTabletBalancer();
        return;
    }

    THiveStats stats = GetStats();
    BLOG_D("ProcessTabletBalancer"
           << " MaxUsage=" << Sprintf("%.9f", stats.MaxUsage) << " on #" << stats.MaxUsageNodeId
           << " MinUsage=" << Sprintf("%.9f", stats.MinUsage) << " on #" << stats.MinUsageNodeId
           << " Scatter=" << Sprintf("%.9f", stats.Scatter));

    TabletCounters->Simple()[NHive::COUNTER_BALANCE_SCATTER].Set(stats.Scatter * 100);
    TabletCounters->Simple()[NHive::COUNTER_BALANCE_USAGE_MIN].Set(stats.MinUsage * 100);
    TabletCounters->Simple()[NHive::COUNTER_BALANCE_USAGE_MAX].Set(stats.MaxUsage * 100);

    if (stats.MaxUsage >= GetMaxNodeUsageToKick()) {
        std::vector<TNodeId> overloadedNodes;
        for (const auto& [nodeId, nodeInfo] : Nodes) {
            if (nodeInfo.IsAlive() && !nodeInfo.Down && nodeInfo.IsOverloaded()) {
                overloadedNodes.emplace_back(nodeId);
            }
        }

        if (!overloadedNodes.empty()) {
            BLOG_D("Nodes " << overloadedNodes << " with usage over limit " << GetMaxNodeUsageToKick() << " - starting balancer");
            StartHiveBalancer(CurrentConfig.GetMaxMovementsOnAutoBalancer(), CurrentConfig.GetContinueAutoBalancer(), overloadedNodes);
            return;
        }
    }

    if (stats.MaxUsage < GetMinNodeUsageToBalance()) {
        TabletCounters->Cumulative()[NHive::COUNTER_SUGGESTED_SCALE_DOWN].Increment(1);
    }

    if (stats.Scatter >= GetMinScatterToBalance()) {
        BLOG_TRACE("Scatter " << stats.Scatter << " over limit "
                   << GetMinScatterToBalance() << " - starting balancer");
        StartHiveBalancer(CurrentConfig.GetMaxMovementsOnAutoBalancer(), CurrentConfig.GetContinueAutoBalancer());
    }
}

void THive::UpdateTotalResourceValues(
        const TNodeInfo* node,
        const TTabletInfo* tablet,
        const NKikimrTabletBase::TMetrics& before,
        const NKikimrTabletBase::TMetrics& after,
        TResourceRawValues deltaRaw,
        TResourceNormalizedValues deltaNormalized) {
    TotalRawResourceValues = TotalRawResourceValues + deltaRaw;
    TotalNormalizedResourceValues = TotalNormalizedResourceValues + deltaNormalized;
    TInstant now = TInstant::Now();

    if (LastResourceChangeReaction + GetResourceChangeReactionPeriod() < now) {
        // in case we had overloaded nodes
        if (!BootQueue.WaitQueue.empty()) {
            ProcessWaitQueue();
        } else if (!BootQueue.BootQueue.empty()) {
            ProcessBootQueue();
        }
        ProcessTabletBalancer();
        LastResourceChangeReaction = now;
    }

    Y_UNUSED(node);

    if (tablet != nullptr) {
        auto& objectMetrics = ObjectToTabletMetrics[tablet->GetObjectId()];
        auto beforeMetrics = objectMetrics.Metrics;
        objectMetrics.AggregateDiff(before, after, tablet);
        BLOG_TRACE("UpdateTotalResources: ObjectId " << tablet->GetObjectId() <<
                   ": {" << beforeMetrics.ShortDebugString() <<
                   "} -> {" << objectMetrics.Metrics.ShortDebugString() << "}");
        auto& typeMetrics = TabletTypeToTabletMetrics[tablet->GetTabletType()];
        beforeMetrics = typeMetrics.Metrics;
        typeMetrics.AggregateDiff(before, after, tablet);
        BLOG_TRACE("UpdateTotalResources: Type " << tablet->GetTabletType() <<
                   ": {" << beforeMetrics.ShortDebugString() <<
                   "} -> {" << typeMetrics.Metrics.ShortDebugString() << "}");
    }
    TabletCounters->Simple()[NHive::COUNTER_METRICS_COUNTER].Set(std::get<NMetrics::EResource::Counter>(TotalRawResourceValues));
    TabletCounters->Simple()[NHive::COUNTER_METRICS_CPU].Set(std::get<NMetrics::EResource::CPU>(TotalRawResourceValues));
    TabletCounters->Simple()[NHive::COUNTER_METRICS_MEMORY].Set(std::get<NMetrics::EResource::Memory>(TotalRawResourceValues));
    TabletCounters->Simple()[NHive::COUNTER_METRICS_NETWORK].Set(std::get<NMetrics::EResource::Network>(TotalRawResourceValues));
}

void THive::RemoveSubActor(ISubActor* subActor) {
    auto it = std::find(SubActors.begin(), SubActors.end(), subActor);
    if (it != SubActors.end()) {
        SubActors.erase(it);
    }
}

bool THive::IsValidMetrics(const NKikimrTabletBase::TMetrics& metrics) {
    return IsValidMetricsCPU(metrics) || IsValidMetricsMemory(metrics) || IsValidMetricsNetwork(metrics);
}

bool THive::IsValidMetricsCPU(const NKikimrTabletBase::TMetrics& metrics) {
    return metrics.GetCPU() > 1000/*1ms*/;
}

bool THive::IsValidMetricsMemory(const NKikimrTabletBase::TMetrics& metrics) {
    return metrics.GetMemory() > 1024/*1KB*/;
}

bool THive::IsValidMetricsNetwork(const NKikimrTabletBase::TMetrics& metrics) {
    return metrics.GetNetwork() > 1024/*1KBps*/;
}

TString THive::DebugDomainsActiveNodes() const {
    return DomainsView.AsString();
}

void THive::AggregateMetricsMax(NKikimrTabletBase::TMetrics& aggregate, const NKikimrTabletBase::TMetrics& value) {
    aggregate.SetCPU(std::max(aggregate.GetCPU(), value.GetCPU()));
    aggregate.SetMemory(std::max(aggregate.GetMemory(), value.GetMemory()));
    aggregate.SetNetwork(std::max(aggregate.GetNetwork(), value.GetNetwork()));
    aggregate.SetCounter(std::max(aggregate.GetCounter(), value.GetCounter()));
    aggregate.SetStorage(std::max(aggregate.GetStorage(), value.GetStorage()));
    aggregate.SetReadThroughput(std::max(aggregate.GetReadThroughput(), value.GetReadThroughput()));
    aggregate.SetWriteThroughput(std::max(aggregate.GetWriteThroughput(), value.GetWriteThroughput()));
}

template <void (NKikimrTabletBase::TMetrics::* setter)(ui64), ui64 (NKikimrTabletBase::TMetrics::* getter)() const, void (NKikimrTabletBase::TMetrics::* clear)()>
static void AggregateDiff(NKikimrTabletBase::TMetrics& aggregate, const NKikimrTabletBase::TMetrics& before, const NKikimrTabletBase::TMetrics& after, TTabletId tabletId, const TString& name) {
    i64 oldValue = (aggregate.*getter)();
    i64 delta = (after.*getter)() - (before.*getter)();
    i64 newValue = oldValue + delta;
    Y_ENSURE_LOG(newValue >= 0, "tablet " << tabletId << " name=" << name << " oldValue=" << oldValue << " delta=" << delta << " newValue=" << newValue);
    newValue = Max(newValue, (i64)0);
    if (newValue != 0) {
        (aggregate.*setter)(newValue);
    } else {
        (aggregate.*clear)();
    }
}

void THive::AggregateMetricsDiff(NKikimrTabletBase::TMetrics& aggregate, const NKikimrTabletBase::TMetrics& before, const NKikimrTabletBase::TMetrics& after, const TTabletInfo* tablet) {
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetCPU, &NKikimrTabletBase::TMetrics::GetCPU, &NKikimrTabletBase::TMetrics::ClearCPU>(aggregate, before, after, tablet->GetLeader().Id, "cpu");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetMemory, &NKikimrTabletBase::TMetrics::GetMemory, &NKikimrTabletBase::TMetrics::ClearMemory>(aggregate, before, after, tablet->GetLeader().Id, "memory");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetNetwork, &NKikimrTabletBase::TMetrics::GetNetwork, &NKikimrTabletBase::TMetrics::ClearNetwork>(aggregate, before, after, tablet->GetLeader().Id, "network");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetCounter, &NKikimrTabletBase::TMetrics::GetCounter, &NKikimrTabletBase::TMetrics::ClearCounter>(aggregate, before, after, tablet->GetLeader().Id, "counter");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetStorage, &NKikimrTabletBase::TMetrics::GetStorage, &NKikimrTabletBase::TMetrics::ClearStorage>(aggregate, before, after, tablet->GetLeader().Id, "storage");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetReadThroughput, &NKikimrTabletBase::TMetrics::GetReadThroughput, &NKikimrTabletBase::TMetrics::ClearReadThroughput>(aggregate, before, after, tablet->GetLeader().Id, "read");
    AggregateDiff<&NKikimrTabletBase::TMetrics::SetWriteThroughput, &NKikimrTabletBase::TMetrics::GetWriteThroughput, &NKikimrTabletBase::TMetrics::ClearWriteThroughput>(aggregate, before, after, tablet->GetLeader().Id, "write");
}

void THive::DivideMetrics(NKikimrTabletBase::TMetrics& metrics, ui64 divider) {
    metrics.SetCPU(metrics.GetCPU() / divider);
    metrics.SetMemory(metrics.GetMemory() / divider);
    metrics.SetNetwork(metrics.GetNetwork() / divider);
    metrics.SetCounter(metrics.GetCounter() / divider);
    metrics.SetStorage(metrics.GetStorage() / divider);
    metrics.SetReadThroughput(metrics.GetReadThroughput() / divider);
    metrics.SetWriteThroughput(metrics.GetWriteThroughput() / divider);
}

NKikimrTabletBase::TMetrics THive::GetDefaultResourceValuesForObject(TObjectId objectId) {
    NKikimrTabletBase::TMetrics metrics;
    auto itTablets = ObjectToTabletMetrics.find(objectId);
    if (itTablets != ObjectToTabletMetrics.end()) {
        metrics = itTablets->second.GetAverage();
        metrics.ClearCounter();
    }
    return metrics;
}

NKikimrTabletBase::TMetrics THive::GetDefaultResourceValuesForTabletType(TTabletTypes::EType type) {
    NKikimrTabletBase::TMetrics metrics;
    auto it = TabletTypeToTabletMetrics.find(type);
    if (it != TabletTypeToTabletMetrics.end()) {
        metrics = it->second.GetAverage();
        metrics.ClearCounter();
    }
    return metrics;
}

NKikimrTabletBase::TMetrics THive::GetDefaultResourceValuesForProfile(TTabletTypes::EType type, const TString& resourceProfile) {
    NKikimrTabletBase::TMetrics resourceValues;
    // copy default resource usage from resource profile
    if (ResourceProfiles) {
        // TODO: provide Hive with resource profile used by the tablet instead of default one.
        auto profile = ResourceProfiles->GetProfile(type, resourceProfile);
        resourceValues.SetMemory(profile->GetDefaultTabletMemoryUsage());
    }
    return resourceValues;
}

const TVector<i64>& THive::GetDefaultAllowedMetricIds() {
    static const TVector<i64> defaultAllowedMetricIds = {
        NKikimrTabletBase::TMetrics::kCounterFieldNumber,
        NKikimrTabletBase::TMetrics::kCPUFieldNumber,
        NKikimrTabletBase::TMetrics::kMemoryFieldNumber,
        NKikimrTabletBase::TMetrics::kNetworkFieldNumber,
        NKikimrTabletBase::TMetrics::kStorageFieldNumber,
        NKikimrTabletBase::TMetrics::kGroupReadThroughputFieldNumber,
        NKikimrTabletBase::TMetrics::kGroupWriteThroughputFieldNumber
    };
    return defaultAllowedMetricIds;
}

const TVector<i64>& THive::GetTabletTypeAllowedMetricIds(TTabletTypes::EType type) const {
    const TVector<i64>& defaultAllowedMetricIds = GetDefaultAllowedMetricIds();
    auto it = TabletTypeAllowedMetrics.find(type);
    if (it != TabletTypeAllowedMetrics.end()) {
        return it->second;
    }
    return defaultAllowedMetricIds;
}

THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters> THive::BuildGroupParametersForChannel(const TLeaderTabletInfo& tablet, ui32 channelId) {
    THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters> groupParameters = MakeHolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters>();
    Y_VERIFY(channelId < tablet.BoundChannels.size());
    const auto& binding = tablet.BoundChannels[channelId];
    groupParameters->MutableStoragePoolSpecifier()->SetName(binding.GetStoragePoolName());
    return groupParameters;
}

void THive::ExecuteStartTablet(TFullTabletId tabletId, const TActorId& local, ui64 cookie, bool external) {
    Execute(CreateStartTablet(tabletId, local, cookie, external));
}

void THive::SendPing(const TActorId& local, TNodeId id) {
    Send(local,
         new TEvLocal::TEvPing(HiveId,
                               HiveGeneration,
                               false,
                               GetLocalConfig()),
         IEventHandle::FlagTrackDelivery | IEventHandle::FlagSubscribeOnSession,
         id);
}

void THive::SendReconnect(const TActorId& local) {
    Send(local, new TEvLocal::TEvReconnect(HiveId, HiveGeneration));
}

ui32 THive::GetDataCenters() {
    return DataCenters ? DataCenters : 1;
}

ui32 THive::GetRegisteredDataCenters() {
    return RegisteredDataCenters ? RegisteredDataCenters : 1;
}

void THive::UpdateRegisteredDataCenters() {
    if (RegisteredDataCenters != RegisteredDataCenterNodes.size()) {
        BLOG_D("THive (UpdateRegisteredDC) DataCenters=" << DataCenters << " RegisteredDataCenters=" << RegisteredDataCenters << "->" << RegisteredDataCenterNodes.size());
        RegisteredDataCenters = RegisteredDataCenterNodes.size();
    }
}

void THive::AddRegisteredDataCentersNode(TDataCenterId dataCenterId, TNodeId nodeId) {
    if (dataCenterId != 0) { // ignore default data center id if exists
        if (RegisteredDataCenterNodes[dataCenterId].insert(nodeId).second) {
            if (RegisteredDataCenters != RegisteredDataCenterNodes.size()) {
                UpdateRegisteredDataCenters();
            }
        }
    }
}

void THive::RemoveRegisteredDataCentersNode(TDataCenterId dataCenterId, TNodeId nodeId) {
    if (dataCenterId != 0) { // ignore default data center id if exists
        RegisteredDataCenterNodes[dataCenterId].erase(nodeId);
        if (RegisteredDataCenterNodes[dataCenterId].size() == 0) {
            RegisteredDataCenterNodes.erase(dataCenterId);
        }
        if (RegisteredDataCenters != RegisteredDataCenterNodes.size()) {
            UpdateRegisteredDataCenters();
        }
    }
}

void THive::UpdateTabletFollowersNumber(TLeaderTabletInfo& tablet, NIceDb::TNiceDb& db, TSideEffects& sideEffects) {
    BLOG_D("UpdateTabletFollowersNumber Tablet " << tablet.ToString() << " RegisteredDataCenters=" << GetRegisteredDataCenters());
    for (TFollowerGroup& group : tablet.FollowerGroups) {
        ui32 followerCount = tablet.GetActualFollowerCount(group.Id);
        ui32 requiredFollowerCount = group.GetComputedFollowerCount(GetRegisteredDataCenters());

        while (followerCount < requiredFollowerCount) {
            BLOG_D("UpdateTabletFollowersNumber Tablet " << tablet.ToString() << " is increasing number of followers (" << followerCount << "<" << requiredFollowerCount << ")");

            TFollowerTabletInfo& follower = tablet.AddFollower(group);
            follower.Statistics.SetLastAliveTimestamp(TlsActivationContext->Now().MilliSeconds());
            db.Table<Schema::TabletFollowerTablet>().Key(tablet.Id, follower.Id).Update(
                        NIceDb::TUpdate<Schema::TabletFollowerTablet::GroupID>(follower.FollowerGroup.Id),
                        NIceDb::TUpdate<Schema::TabletFollowerTablet::FollowerNode>(0),
                        NIceDb::TUpdate<Schema::TabletFollowerTablet::Statistics>(follower.Statistics));
            follower.InitTabletMetrics();
            follower.BecomeStopped();
            ++followerCount;
        }

        while (followerCount > requiredFollowerCount) {
            BLOG_D("UpdateTabletFollowersNumber Tablet " << tablet.ToString() << " is decreasing number of followers (" << followerCount << ">" << requiredFollowerCount << ")");

            auto itFollower = tablet.Followers.rbegin();
            while (itFollower != tablet.Followers.rend() && itFollower->FollowerGroup.Id != group.Id) {
                ++itFollower;
            }
            if (itFollower == tablet.Followers.rend()) {
                break;
            }
            TFollowerTabletInfo& follower = *itFollower;
            db.Table<Schema::TabletFollowerTablet>().Key(tablet.Id, follower.Id).Delete();
            db.Table<Schema::Metrics>().Key(tablet.Id, follower.Id).Delete();
            follower.InitiateStop(sideEffects);
            tablet.Followers.erase(std::prev(itFollower.base()));
            --followerCount;
        }
    }
}

THive::THive(TTabletStorageInfo *info, const TActorId &tablet)
    : TActor(&TThis::StateInit)
    , TTabletExecutedFlat(info, tablet, new NMiniKQL::TMiniKQLFactory)
    , HiveUid(Max<ui32>())
    , HiveDomain(Max<ui32>())
    , RootHiveId()
    , HiveId(Max<ui64>())
    , HiveGeneration(0)
    , PipeClientCacheConfig(new NTabletPipe::TBoundedClientCacheConfig())
    , PipeClientCache(NTabletPipe::CreateBoundedClientCache(PipeClientCacheConfig))
    , PipeTracker(*PipeClientCache)
    , PipeRetryPolicy()
    , BalancerProgress(-1)
    , ResponsivenessPinger(nullptr)
{
    TabletCountersPtr.Reset(new TProtobufTabletCounters<
        ESimpleCounters_descriptor,
        ECumulativeCounters_descriptor,
        EPercentileCounters_descriptor,
        ETxTypes_descriptor
    >());
    TabletCounters = TabletCountersPtr.Get();
}

void THive::Handle(TEvHive::TEvInvalidateStoragePools::TPtr&) {
    for (auto& pr : StoragePools) {
        pr.second.Invalidate();
    }
}

void THive::InitDefaultChannelBind(TChannelBind& bind) {
    if (!bind.HasIOPS()) {
        bind.SetIOPS(GetDefaultUnitIOPS());
    }
    if (!bind.HasSize()) {
        bind.SetSize(GetDefaultUnitSize());
    }
    if (!bind.HasThroughput()) {
        bind.SetThroughput(GetDefaultUnitThroughput());
    }
}

void THive::RequestPoolsInformation() {
    BLOG_D("THive::RequestPoolsInformation()");
    TVector<THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters>> requests;

    for (const auto& [poolName, storagePool] : StoragePools) {
        THolder<NKikimrBlobStorage::TEvControllerSelectGroups::TGroupParameters> item = storagePool.BuildRefreshRequest();
        requests.emplace_back(std::move(item));
    }

    if (!requests.empty()) {
        THolder<TEvBlobStorage::TEvControllerSelectGroups> ev = MakeHolder<TEvBlobStorage::TEvControllerSelectGroups>();
        NKikimrBlobStorage::TEvControllerSelectGroups& record = ev->Record;
        record.SetReturnAllMatchingGroups(true);
        record.SetBlockUntilAllResourcesAreComplete(true);
        for (auto& request : requests) {
            record.MutableGroupParameters()->AddAllocated(std::move(request).Release());
        }
        SendToBSControllerPipe(ev.Release());
    }
}

STFUNC(THive::StateInit) {
    switch (ev->GetTypeRewrite()) {
        hFunc(TEvInterconnect::TEvNodesInfo, Handle);
    default:
        StateInitImpl(ev, ctx);
    }
}

STFUNC(THive::StateWork) {
    if (ResponsivenessPinger)
        ResponsivenessPinger->OnAnyEvent(ctx);

    switch (ev->GetTypeRewrite()) {
        hFunc(TEvHive::TEvCreateTablet, Handle);
        hFunc(TEvHive::TEvAdoptTablet, Handle);
        hFunc(TEvHive::TEvStopTablet, Handle);
        hFunc(TEvHive::TEvBootTablet, Handle);
        hFunc(TEvLocal::TEvStatus, Handle);
        hFunc(TEvLocal::TEvTabletStatus, Handle); // from bootqueue
        hFunc(TEvLocal::TEvRegisterNode, Handle); // from local
        hFunc(TEvBlobStorage::TEvControllerSelectGroupsResult, Handle);
        hFunc(TEvents::TEvPoisonPill, Handle);
        hFunc(TEvTabletPipe::TEvClientConnected, Handle);
        hFunc(TEvTabletPipe::TEvClientDestroyed, Handle);
        hFunc(TEvTabletPipe::TEvServerConnected, Handle);
        hFunc(TEvTabletPipe::TEvServerDisconnected, Handle);
        hFunc(TEvPrivate::TEvBootTablets, Handle);
        hFunc(TEvHive::TEvInitMigration, Handle);
        hFunc(TEvHive::TEvQueryMigration, Handle);
        hFunc(TEvInterconnect::TEvNodeConnected, Handle);
        hFunc(TEvInterconnect::TEvNodeDisconnected, Handle);
        hFunc(TEvInterconnect::TEvNodeInfo, Handle);
        hFunc(TEvInterconnect::TEvNodesInfo, Handle);
        hFunc(TEvents::TEvUndelivered, Handle);
        hFunc(TEvPrivate::TEvProcessBootQueue, Handle);
        hFunc(TEvPrivate::TEvPostponeProcessBootQueue, Handle);
        hFunc(TEvPrivate::TEvProcessPendingOperations, Handle);
        hFunc(TEvPrivate::TEvProcessDisconnectNode, Handle);
        hFunc(TEvLocal::TEvSyncTablets, Handle);
        hFunc(TEvPrivate::TEvKickTablet, Handle);
        hFunc(TEvHive::TEvTabletMetrics, Handle);
        hFunc(TEvTabletBase::TEvBlockBlobStorageResult, Handle);
        hFunc(TEvTabletBase::TEvDeleteTabletResult, Handle);
        hFunc(TEvHive::TEvReassignTablet, Handle);
        hFunc(TEvHive::TEvInitiateBlockStorage, Handle);
        hFunc(TEvHive::TEvDeleteTablet, Handle);
        hFunc(TEvHive::TEvDeleteOwnerTablets, Handle);
        hFunc(TEvHive::TEvRequestHiveInfo, Handle);
        hFunc(TEvHive::TEvLookupTablet, Handle);
        hFunc(TEvHive::TEvLookupChannelInfo, Handle);
        hFunc(TEvHive::TEvCutTabletHistory, Handle);
        hFunc(TEvHive::TEvDrainNode, Handle);
        hFunc(TEvHive::TEvFillNode, Handle);
        hFunc(TEvHive::TEvInitiateDeleteStorage, Handle);
        hFunc(TEvHive::TEvGetTabletStorageInfo, Handle);
        hFunc(TEvHive::TEvLockTabletExecution, Handle);
        hFunc(TEvHive::TEvUnlockTabletExecution, Handle);
        hFunc(TEvPrivate::TEvProcessTabletBalancer, Handle);
        hFunc(TEvPrivate::TEvUnlockTabletReconnectTimeout, Handle);
        hFunc(TEvHive::TEvInitiateTabletExternalBoot, Handle);
        hFunc(TEvHive::TEvRequestHiveDomainStats, Handle);
        hFunc(TEvHive::TEvRequestHiveNodeStats, Handle);
        hFunc(TEvHive::TEvRequestHiveStorageStats, Handle);
        hFunc(TEvHive::TEvInvalidateStoragePools, Handle);
        hFunc(TEvHive::TEvRequestTabletIdSequence, Handle);
        hFunc(TEvHive::TEvResponseTabletIdSequence, Handle);
        hFunc(TEvHive::TEvSeizeTablets, Handle);
        hFunc(TEvHive::TEvSeizeTabletsReply, Handle);
        hFunc(TEvHive::TEvReleaseTablets, Handle);
        hFunc(TEvHive::TEvReleaseTabletsReply, Handle);
        hFunc(TEvSubDomain::TEvConfigure, Handle);
        hFunc(TEvHive::TEvConfigureHive, Handle);
        hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, Handle);
        hFunc(NConsole::TEvConsole::TEvConfigNotificationRequest, Handle);
        hFunc(NConsole::TEvConfigsDispatcher::TEvSetConfigSubscriptionResponse, Handle);
        hFunc(NSysView::TEvSysView::TEvGetTabletIdsRequest, Handle);
        hFunc(NSysView::TEvSysView::TEvGetTabletsRequest, Handle);
        hFunc(TEvHive::TEvRequestTabletOwners, Handle);
        hFunc(TEvHive::TEvTabletOwnersReply, Handle);
    default:
        if (!HandleDefaultEvents(ev, ctx)) {
            BLOG_W("THive::StateWork unhandled event type: " << ev->GetTypeRewrite()
                   << " event: " << (ev->HasEvent() ? ev->GetBase()->ToString().data() : "serialized?"));
        }
        break;
    }
}

void THive::KickTablet(const TTabletInfo& tablet) {
    Send(SelfId(), new TEvPrivate::TEvKickTablet(tablet));
}

void THive::Handle(TEvHive::TEvRequestTabletIdSequence::TPtr& ev) {
    Execute(CreateRequestTabletSequence(std::move(ev)));
}

void THive::Handle(TEvHive::TEvResponseTabletIdSequence::TPtr& ev) {
    Execute(CreateResponseTabletSequence(std::move(ev)));
}

void THive::RequestFreeSequence() {
    TIntrusivePtr<TDomainsInfo> domains = AppData()->DomainsInfo;
    TIntrusivePtr<TDomainsInfo::TDomain> domain = domains->Domains.begin()->second;
    TTabletId rootHiveId = domains->GetHive(domain->DefaultHiveUid);
    if (rootHiveId != TabletID()) {
        size_t sequenceIndex = Sequencer.NextFreeSequenceIndex();
        size_t sequenceSize = GetRequestSequenceSize();

        if (PendingCreateTablets.size() > sequenceSize) {
            size_t newSequenceSize = ((PendingCreateTablets.size() / sequenceSize) + 1) * sequenceSize;
            BLOG_W("Increasing sequence size from " << sequenceSize << " to " << newSequenceSize << " due to PendingCreateTablets.size() == " << PendingCreateTablets.size());
            sequenceSize = newSequenceSize;
        }

        BLOG_D("Requesting free sequence #" << sequenceIndex << " of " << sequenceSize << " from root hive");
        SendToRootHivePipe(new TEvHive::TEvRequestTabletIdSequence(TabletID(), sequenceIndex, sequenceSize));
        RequestingSequenceNow = true;
        RequestingSequenceIndex = sequenceIndex;
    } else {
        BLOG_ERROR("We ran out of tablet ids");
    }
}

void THive::ProcessPendingOperations() {
    Execute(CreateProcessPendingOperations());
}

void THive::Handle(TEvSubDomain::TEvConfigure::TPtr& ev) {
    BLOG_D("Handle TEvSubDomain::TEvConfigure(" << ev->Get()->Record.ShortDebugString() << ")");
    Send(ev->Sender, new TEvSubDomain::TEvConfigureStatus(NKikimrTx::TEvSubDomainConfigurationAck::SUCCESS, TabletID()));
}

void THive::Handle(TEvHive::TEvConfigureHive::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvConfigureHive(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateConfigureSubdomain(std::move(ev)));
}


void THive::Handle(NSysView::TEvSysView::TEvGetTabletIdsRequest::TPtr& ev) {
    const auto& request = ev->Get()->Record;
    auto fromId = request.GetFrom();
    auto toId = request.GetTo();

    auto response = MakeHolder<NSysView::TEvSysView::TEvGetTabletIdsResponse>();
    auto& record = response->Record;

    for (const auto& [tabletId, _] : Tablets) {
        if (tabletId >= fromId && tabletId <= toId) {
            record.AddTabletIds(tabletId);
        }
    }

    Send(ev->Sender, response.Release());
}

void THive::Handle(NSysView::TEvSysView::TEvGetTabletsRequest::TPtr& ev) {
    const auto& request = ev->Get()->Record;

    auto response = MakeHolder<NSysView::TEvSysView::TEvGetTabletsResponse>();
    auto& record = response->Record;

    auto limit = request.GetBatchSizeLimit();
    size_t count = 0;

    for (size_t i = 0; i < request.TabletIdsSize(); ++i) {
        auto tabletId = request.GetTabletIds(i);
        auto lookup = Tablets.find(tabletId);
        if (lookup == Tablets.end()) {
            continue;
        }

        auto* entry = record.AddEntries();
        ++count;

        const auto& tablet = lookup->second;
        auto tabletTypeName = TTabletTypes::TypeToStr(tablet.Type);

        entry->SetTabletId(tabletId);
        entry->SetFollowerId(0);

        entry->SetType(tabletTypeName);
        entry->SetState(ETabletStateName(tablet.State));
        entry->SetVolatileState(TTabletInfo::EVolatileStateName(tablet.GetVolatileState()));
        entry->SetBootState(tablet.BootState);
        entry->SetGeneration(tablet.KnownGeneration);
        entry->SetNodeId(tablet.NodeId);

        const auto& resourceValues = tablet.GetResourceValues();
        entry->SetCPU(resourceValues.GetCPU());
        entry->SetMemory(resourceValues.GetMemory());
        entry->SetNetwork(resourceValues.GetNetwork());

        for (const auto& follower : tablet.Followers) {
            auto* entry = record.AddEntries();
            ++count;

            entry->SetTabletId(tabletId);
            entry->SetFollowerId(follower.Id);

            entry->SetType(tabletTypeName);
            // state is null
            entry->SetVolatileState(TTabletInfo::EVolatileStateName(follower.GetVolatileState()));
            entry->SetBootState(follower.BootState);
            // generation is null
            entry->SetNodeId(follower.NodeId);

            const auto& resourceValues = follower.GetResourceValues();
            entry->SetCPU(resourceValues.GetCPU());
            entry->SetMemory(resourceValues.GetMemory());
            entry->SetNetwork(resourceValues.GetNetwork());
        }

        if (count >= limit && i < request.TabletIdsSize() - 1) {
            record.SetNextTabletId(request.GetTabletIds(i + 1));
            break;
        }
    }

    Send(ev->Sender, response.Release());
}

const TTabletMetricsAggregates& THive::GetDefaultResourceMetricsAggregates() const {
    return DefaultResourceMetricsAggregates;
}

bool THive::CheckForForwardTabletRequest(TTabletId tabletId, NKikimrHive::TForwardRequest& forwardRequest) {
    const TLeaderTabletInfo* tablet = FindTablet(tabletId);
    if (tablet == nullptr) {
        TOwnershipKeeper::TOwnerType owner = Keeper.GetOwner(UniqPartFromTabletID(tabletId));
        if (owner == TSequencer::NO_OWNER && AreWeSubDomainHive()) {
            owner = RootHiveId;
        }
        if (owner != TSequencer::NO_OWNER && owner != TabletID()) {
            BLOG_NOTICE("Forwarding TabletRequest(TabletID " << tabletId << ") to Hive " << owner);
            forwardRequest.SetHiveTabletId(owner);
            return true;
        }
    }
    return false;
}

TSubDomainKey THive::GetRootDomainKey() const {
    return RootDomainKey;
}

TSubDomainKey THive::GetMySubDomainKey() const {
    if (AreWeRootHive()) {
        return GetRootDomainKey();
    }
    if (PrimaryDomainKey) {
        return PrimaryDomainKey;
    }
    if (Domains.size() == 1) {
        // not very straight way to get our domain key
        return Domains.begin()->first;
    }
    if (!Tablets.empty()) {
        std::unordered_set<TSubDomainKey> objectDomains;
        for (const auto& [id, tablet] : Tablets) {
            objectDomains.insert(tablet.ObjectDomain);
        }
        if (objectDomains.size() == 1) {
            BLOG_W("GetMySubDomainKey() - guessed PrimaryDomainKey to " << *objectDomains.begin());
            return *objectDomains.begin();
        } else {
            BLOG_W("GetMySubDomainKey() - couldn't guess PrimaryDomainKey: " << objectDomains.size() << " object domains found");
        }
    }
    return {};
}

void THive::Handle(TEvHive::TEvSeizeTablets::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvSeizeTablets(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateSeizeTablets(ev));
}

void THive::Handle(TEvHive::TEvSeizeTabletsReply::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvSeizeTabletsReply(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateSeizeTabletsReply(ev));
}

void THive::Handle(TEvHive::TEvReleaseTablets::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvReleaseTablets(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateReleaseTablets(ev));
}

void THive::Handle(TEvHive::TEvReleaseTabletsReply::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvReleaseTabletsReply(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateReleaseTabletsReply(ev));
}

void THive::Handle(TEvHive::TEvRequestTabletOwners::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvRequestTabletOwners(" << ev->Get()->Record.ShortDebugString() << ")");
    Execute(CreateRequestTabletOwners(std::move(ev)));
}

void THive::Handle(TEvHive::TEvTabletOwnersReply::TPtr& ev) {
    BLOG_D("Handle TEvHive::TEvTabletOwnersReply()");
    Execute(CreateTabletOwnersReply(std::move(ev)));
}

TVector<TNodeId> THive::GetNodesForWhiteboardBroadcast(size_t maxNodesToReturn) {
    TVector<TNodeId> nodes;
    TNodeId selfNodeId = SelfId().NodeId();
    nodes.emplace_back(selfNodeId);
    for (const auto& [nodeId, nodeInfo] : Nodes) {
        if (nodes.size() >= maxNodesToReturn) {
            break;
        }
        if (nodeId != selfNodeId && nodeInfo.IsAlive()) {
            nodes.emplace_back(nodeId);
        }
    }
    return nodes;
}

void THive::ReportStoppedToWhiteboard(const TLeaderTabletInfo& tablet) {
    ReportTabletStateToWhiteboard(tablet, NKikimrWhiteboard::TTabletStateInfo::Stopped);
}

void THive::ReportDeletedToWhiteboard(const TLeaderTabletInfo& tablet) {
    ReportTabletStateToWhiteboard(tablet, NKikimrWhiteboard::TTabletStateInfo::Deleted);
}

void THive::ReportTabletStateToWhiteboard(const TLeaderTabletInfo& tablet, NKikimrWhiteboard::TTabletStateInfo::ETabletState state) {
    ui32 generation = state == NKikimrWhiteboard::TTabletStateInfo::Deleted ? std::numeric_limits<ui32>::max() : tablet.KnownGeneration;
    TPathId pathId = tablet.GetTenant();
    TSubDomainKey tenantId(pathId.OwnerId, pathId.LocalPathId);
    for (TNodeId nodeId : GetNodesForWhiteboardBroadcast()) {
        TActorId whiteboardId = NNodeWhiteboard::MakeNodeWhiteboardServiceId(nodeId);
        THolder<NNodeWhiteboard::TEvWhiteboard::TEvTabletStateUpdate> event = MakeHolder<NNodeWhiteboard::TEvWhiteboard::TEvTabletStateUpdate>();
        event->Record.SetTabletId(tablet.Id);
        event->Record.SetType(tablet.Type);
        event->Record.SetLeader(true);
        event->Record.SetGeneration(generation);
        event->Record.SetState(state);
        event->Record.SetHiveId(TabletID());
        event->Record.MutableTenantId()->CopyFrom(tenantId);
        Send(whiteboardId, event.Release());
        for (const TFollowerTabletInfo& follower : tablet.Followers) {
            THolder<NNodeWhiteboard::TEvWhiteboard::TEvTabletStateUpdate> event = MakeHolder<NNodeWhiteboard::TEvWhiteboard::TEvTabletStateUpdate>();
            event->Record.SetTabletId(follower.LeaderTablet.Id);
            event->Record.SetFollowerId(follower.Id);
            event->Record.SetType(tablet.Type);
            event->Record.SetGeneration(generation);
            event->Record.SetState(state);
            event->Record.SetHiveId(TabletID());
            event->Record.MutableTenantId()->CopyFrom(tenantId);
            Send(whiteboardId, event.Release());
        }
    }
}

void THive::ActualizeRestartStatistics(google::protobuf::RepeatedField<google::protobuf::uint64>& array, ui64 barrier) {
    static constexpr decltype(array.size()) MAX_RESTARTS_PER_PERIOD = 128;
    auto begin = array.begin();
    auto end = array.end();
    auto it = begin;
    if (array.size() > MAX_RESTARTS_PER_PERIOD) {
        it = end - MAX_RESTARTS_PER_PERIOD;
    }
    while (it != end && *it < barrier) {
        ++it;
    }
    array.erase(begin, it);
}

bool THive::IsSystemTablet(TTabletTypes::EType type) {
    switch (type) {
        case TTabletTypes::Coordinator:
        case TTabletTypes::Mediator:
        case TTabletTypes::TxAllocator:
        //case TTabletTypes::SchemeShard:
            return true;
        default:
            return false;
    }
}

TString THive::GetLogPrefix() const {
    return TStringBuilder() << "HIVE#" << TabletID() << " ";
}

} // NHive

IActor* CreateDefaultHive(const TActorId &tablet, TTabletStorageInfo *info) {
    return new NHive::THive(info, tablet);
}

} // NKikimr
