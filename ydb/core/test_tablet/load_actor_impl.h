#pragma once

#include "defs.h"

#include "test_shard_impl.h"
#include "time_series.h"
#include "state_server_interface.h"

namespace NKikimr::NTestShard {

    class TLoadActor : public TActorBootstrapped<TLoadActor> {
        const ui64 TabletId;
        const ui32 Generation;
        const TActorId Tablet;
        TActorId TabletActorId;
        const NKikimrClient::TTestShardControlRequest::TCmdInitialize Settings;

        ui64 ValidationRunningCount = 0;

        struct TKeyInfo {
            const ui32 Len = 0;
            ::NTestShard::TStateServer::EEntityState ConfirmedState = ::NTestShard::TStateServer::ABSENT;
            ::NTestShard::TStateServer::EEntityState PendingState = ::NTestShard::TStateServer::ABSENT;
            std::unique_ptr<TEvKeyValue::TEvRequest> Request;

            TKeyInfo(ui32 len)
                : Len(len)
            {}
        };

        enum {
            EvValidationFinished = EventSpaceBegin(TEvents::ES_PRIVATE),
        };

        struct TEvValidationFinished : TEventLocal<TEvValidationFinished, EvValidationFinished> {
            std::unordered_map<TString, TKeyInfo> Keys;
            bool InitialCheck;

            TEvValidationFinished(std::unordered_map<TString, TKeyInfo> keys, bool initialCheck)
                : Keys(std::move(keys))
                , InitialCheck(initialCheck)
            {}
        };

    public:
        TLoadActor(ui64 tabletId, ui32 generation, const TActorId tablet,
            const NKikimrClient::TTestShardControlRequest::TCmdInitialize& settings);
        void Bootstrap(const TActorId& parentId);
        void PassAway() override;
        void HandleWakeup();
        void Action();
        void Handle(TEvStateServerStatus::TPtr ev);

        STRICT_STFUNC(StateFunc,
            hFunc(TEvKeyValue::TEvResponse, Handle);
            hFunc(NMon::TEvRemoteHttpInfo, Handle);
            hFunc(TEvStateServerStatus, Handle);
            hFunc(TEvStateServerWriteResult, Handle);
            hFunc(TEvValidationFinished, Handle);
            cFunc(TEvents::TSystem::Poison, PassAway);
            cFunc(TEvents::TSystem::Wakeup, HandleWakeup);
        )

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Generic request/response management

        ui32 BytesProcessed = 0;
        ui32 StallCounter = 0;
        ui64 LastCookie = 0;

        std::unique_ptr<TEvKeyValue::TEvRequest> CreateRequest();
        void Handle(TEvKeyValue::TEvResponse::TPtr ev);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Key state

        std::unordered_map<TString, TKeyInfo> Keys;

        using TKey = std::unordered_map<TString, TKeyInfo>::value_type;

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // State validation actor

        class TValidationActor;
        friend class TValidationActor;

        TActorId ValidationActorId;
        void RunValidation(bool initialCheck);
        void Handle(TEvValidationFinished::TPtr ev);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // KV tablet write management code

        struct TWriteInfo {
            THPTimer Timer; // reset when write request is issued
            std::vector<TString> KeysInQuery;

            TWriteInfo(TString key)
                : KeysInQuery(1, std::move(key))
            {}
        };

        ui64 BytesOfData = 0;

        std::unordered_map<ui64, TWriteInfo> WritesInFlight; // cookie -> TWriteInfo
        ui32 KeysWritten = 0;
        static constexpr TDuration WriteSpeedWindow = TDuration::Seconds(10);
        TSpeedMeter WriteSpeed{WriteSpeedWindow};
        TTimeSeries StateServerWriteLatency;
        TTimeSeries WriteLatency;

        void GenerateKeyValue(TString *key, TString *value, bool *isInline);
        void IssueWrite();
        void ProcessWriteResult(ui64 cookie, const google::protobuf::RepeatedPtrField<NKikimrClient::TKeyValueResponse::TWriteResult>& results);
        void TrimBytesWritten(TInstant now);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // KV tablet delete management code

        struct TDeleteInfo {
            std::vector<TString> KeysInQuery; // keys being deleted

            TDeleteInfo(TString key)
                : KeysInQuery(1, std::move(key))
            {}
        };

        std::unordered_map<ui64, TDeleteInfo> DeletesInFlight; // cookie -> TDeleteInfo
        ui32 KeysDeleted = 0;

        std::optional<TString> FindKeyToDelete();
        void IssueDelete();
        void ProcessDeleteResult(ui64 cookie, const google::protobuf::RepeatedPtrField<NKikimrClient::TKeyValueResponse::TDeleteRangeResult>& results);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // State management

        std::deque<TKey*> TransitionInFlight;

        void RegisterTransition(TKey& key, ::NTestShard::TStateServer::EEntityState from,
            ::NTestShard::TStateServer::EEntityState to, std::unique_ptr<TEvKeyValue::TEvRequest> ev = nullptr);
        void Handle(TEvStateServerWriteResult::TPtr ev);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Tablet monitoring

        void Handle(NMon::TEvRemoteHttpInfo::TPtr ev);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Random generators

        template<typename T>
        size_t PickInterval(const google::protobuf::RepeatedPtrField<T>& intervals) {
            std::vector<ui64> cw;
            ui64 w = 0;
            cw.reserve(intervals.size());
            for (const auto& interval : intervals) {
                Y_VERIFY(interval.HasWeight());
                Y_VERIFY(interval.GetWeight());
                w += interval.GetWeight();
                cw.push_back(w);
            }
            const size_t num = std::upper_bound(cw.begin(), cw.end(), TAppData::RandomProvider->Uniform(w)) - cw.begin();
            Y_VERIFY(num < cw.size());
            return num;
        }

        TDuration GenerateRandomInterval(const NKikimrClient::TTestShardControlRequest::TTimeInterval& interval);
        TDuration GenerateRandomInterval(const google::protobuf::RepeatedPtrField<NKikimrClient::TTestShardControlRequest::TTimeInterval>& intervals);
        size_t GenerateRandomSize(const google::protobuf::RepeatedPtrField<NKikimrClient::TTestShardControlRequest::TSizeInterval>& intervals,
                bool *isInline);
    };

} // NKikimr::NTestShard
