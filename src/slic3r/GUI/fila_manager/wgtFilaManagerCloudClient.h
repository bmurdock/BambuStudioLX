#ifndef slic3r_wgtFilaManagerCloudClient_h_
#define slic3r_wgtFilaManagerCloudClient_h_

#include <functional>
#include <atomic>
#include <memory>
#include "CloudRequestWorker.h"
#include <map>
#include <string>
#include "nlohmann/json.hpp"
#include "slic3r/Utils/bambu_networking.hpp"

namespace Slic3r { class NetworkAgent; namespace GUI {

class wgtFilaManagerCloudClient {
public:
    using SuccessFn = std::function<void(const nlohmann::json& data)>;
    using ErrorFn   = std::function<void(int http_code, const std::string& error)>;

    wgtFilaManagerCloudClient() = default;
    ~wgtFilaManagerCloudClient();
    // UI thread: suppress queued replies, discard pending work and drain the
    // active plugin call before account changes or owner destruction.
    void reset_session();
    void shutdown();

    // POST /my/filament/v2
    void create_spool(const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // PUT /my/filament/v2/:id
    void update_spool(const std::string& id, const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // DELETE /my/filament/v2/batch
    void batch_delete(const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err);

    // GET /my/filament/v2
    void list_spools(const std::map<std::string, std::string>& query, SuccessFn on_ok, ErrorFn on_err);

    // GET /filament/config  (no auth required)
    void get_filament_config(SuccessFn on_ok, ErrorFn on_err);

    // POST /my/filament/v2/ams/sync
    void sync_ams(BBL::AmsSyncParams params, SuccessFn on_ok, ErrorFn on_err);

    // POST /my/filament/v2/slot-mappings/sync
    void sync_slot_mappings(BBL::SlotMappingsSyncParams params, SuccessFn on_ok, ErrorFn on_err);

    // GET /my/filament/v2/soft-match/pending
    void get_soft_match_pending(BBL::SoftMatchPendingParams params, SuccessFn on_ok, ErrorFn on_err);

    // POST /my/filament/v2/soft-match/pending
    void post_soft_match_pending(BBL::SoftMatchPendingActionParams params,
                                 SuccessFn on_ok, ErrorFn on_err);

private:
    using RequestFn = std::function<int(NetworkAgent*, std::string&)>;
    void dispatch_agent_request(const std::string& action, const nlohmann::json& detail,
                                RequestFn request, SuccessFn on_ok, ErrorFn on_err);
    std::shared_ptr<std::atomic<std::uint64_t>> m_generation =
        std::make_shared<std::atomic<std::uint64_t>>(0);
    CloudRequestWorker m_worker;
    bool        check_login(ErrorFn& on_err) const;
};

}} // namespace Slic3r::GUI

#endif // slic3r_wgtFilaManagerCloudClient_h_
