#include "wgtFilaManagerCloudClient.h"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"

#include <boost/log/trivial.hpp>

#include <thread>
#include "libslic3r/Log.hpp"

namespace Slic3r { namespace GUI {

namespace {

void emit_http_debug(const std::string& level,
                     const std::string& title,
                     const std::string& summary,
                     const nlohmann::json& detail = nlohmann::json::object())
{
    wxGetApp().emit_fila_debug_log("http", level, title, summary, detail);
}

nlohmann::json parse_json_response(const std::string& resp_body)
{
    try {
        return nlohmann::json::parse(resp_body);
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return nlohmann::json{{"raw", resp_body}};
    }
}

int parse_int(const std::map<std::string, std::string>& query, const std::string& key, int default_value)
{
    auto it = query.find(key);
    if (it == query.end() || it->second.empty())
        return default_value;

    try {
        return std::stoi(it->second);
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        return default_value;
    }
}

NetworkAgent* get_agent()
{
    return wxGetApp().getAgent();
}

} // namespace

wgtFilaManagerCloudClient::~wgtFilaManagerCloudClient() { shutdown(); }

void wgtFilaManagerCloudClient::reset_session()
{
    ++*m_generation;
    m_worker.cancel_pending_and_wait();
}

void wgtFilaManagerCloudClient::shutdown()
{
    ++*m_generation;
    m_worker.shutdown();
}

void wgtFilaManagerCloudClient::dispatch_agent_request(
    const std::string& action, const nlohmann::json& detail,
    RequestFn request, SuccessFn on_ok, ErrorFn on_err)
{
    // Capture application services on the UI thread. Shutdown joins the worker
    // before the application destroys the agent or any request consumers.
    auto* app = wxTheApp;
    NetworkAgent* agent = get_agent();
    const auto generation = m_generation;
    const auto expected = generation->load();
    const std::string user_id = agent ? agent->get_user_id() : std::string();
    emit_http_debug("info", "NetworkAgent " + action,
                    "Dispatching filament cloud request through NetworkAgent", detail);
    m_worker.submit([app, agent, generation, expected, user_id, action,
                     request = std::move(request), on_ok = std::move(on_ok), on_err = std::move(on_err)]() mutable {
        int ret = BAMBU_NETWORK_ERR_INVALID_HANDLE;
        std::string body;
        nlohmann::json parsed;
        try {
            if (generation->load() != expected || !agent ||
                !agent->is_user_login() || agent->get_user_id() != user_id) return;
            ret = request(agent, body);
            if (ret == BAMBU_NETWORK_SUCCESS) parsed = nlohmann::json::parse(body);
        } catch (const std::exception& ex) {
            ret = BAMBU_NETWORK_ERR_INVALID_HANDLE;
            body = ex.what();
        }
        if (generation->load() != expected) return;
        app->CallAfter([generation, expected, user_id, action, ret, body = std::move(body),
                        parsed = std::move(parsed), on_ok = std::move(on_ok), on_err = std::move(on_err)]() mutable {
            // This guard is evaluated before touching owners captured by the
            // callbacks, including after shutdown with a queued UI reply.
            if (generation->load() != expected) return;
            auto* current_agent = get_agent();
            if (!current_agent || !current_agent->is_user_login() || current_agent->get_user_id() != user_id) return;
            if (ret == BAMBU_NETWORK_SUCCESS) {
                emit_http_debug("info", "NetworkAgent " + action + " success",
                                "Filament cloud request completed", {{"action", action}, {"response", parsed}});
                if (on_ok) on_ok(parsed);
            } else {
                emit_http_debug("error", "NetworkAgent " + action + " failed",
                                "Filament cloud request failed", {{"action", action}, {"ret_code", ret}});
                if (on_err) on_err(ret, body.empty() ? "network request failed" : body);
            }
        });
    });
}

bool wgtFilaManagerCloudClient::check_login(ErrorFn& on_err) const
{
    NetworkAgent* agent = get_agent();
    if (!agent || !agent->is_user_login()) {
        if (on_err)
            on_err(0, "user not logged in");
        return false;
    }
    return true;
}

void wgtFilaManagerCloudClient::create_spool(
    const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    dispatch_agent_request("create_spool",
                           {{"method", "POST"}, {"body", body}},
                           [payload = body.dump()](NetworkAgent* agent, std::string& response_body) {
                               return agent->create_filament_spool(payload, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::update_spool(
    const std::string& id, const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    // Swagger UpdateFilamentV2Req：`id` 为 int64 且 required，路由是
    // `PUT /my/filament/v2`（id 在 body，不在 path）。这里把字符串形式的本地
    // spool_id 转成 int64 写回 body；其余字段由 `spool_to_cloud_update_patch`
    // 过滤过 swagger 白名单，调用方直接传入即可。
    nlohmann::json request_body = body.is_object() ? body : nlohmann::json::object();
    try {
        request_body["id"] = std::stoll(id);
    } catch (const std::exception&) {
        BOOST_LOG_TRIVIAL(warning) << current_exception_message();
        // spool_id 不可解析为 int64 时保持字符串形式交给 server，让服务端回
        // 具体的 400 校验错误（而不是本地吞掉），便于排查。
        request_body["id"] = id;
    }

    dispatch_agent_request("update_spool",
                           {{"method", "PUT"}, {"spool_id", id}, {"body", request_body}},
                           [id, payload = request_body.dump()](NetworkAgent* agent, std::string& response_body) {
                               return agent->update_filament_spool(id, payload, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::batch_delete(
    const nlohmann::json& body, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    // Tolerate both int / string arrays for "ids" (cloud schema uses int64),
    // and both "RFIDs" (cloud key) / "rfids" (legacy local key).
    auto collect_as_strings = [](const nlohmann::json& arr, std::vector<std::string>& out) {
        if (!arr.is_array()) return;
        out.reserve(out.size() + arr.size());
        for (const auto& item : arr) {
            if (item.is_string())          out.push_back(item.get<std::string>());
            else if (item.is_number_integer())  out.push_back(std::to_string(item.get<int64_t>()));
            else if (item.is_number_unsigned()) out.push_back(std::to_string(item.get<uint64_t>()));
        }
    };

    FilamentDeleteParams params;
    if (body.contains("ids"))
        collect_as_strings(body["ids"], params.ids);
    if (body.contains("RFIDs"))
        collect_as_strings(body["RFIDs"], params.rfids);
    else if (body.contains("rfids"))
        collect_as_strings(body["rfids"], params.rfids);

    if (params.ids.empty() && params.rfids.empty()) {
        if (on_err)
            on_err(0, "batch_delete requires 'ids' or 'rfids'");
        return;
    }

    dispatch_agent_request("batch_delete",
                           {{"method", "DELETE"}, {"body", body}},
                           [params](NetworkAgent* agent, std::string& response_body) mutable {
                               return agent->delete_filament_spools(params, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::list_spools(
    const std::map<std::string, std::string>& query, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    FilamentQueryParams params;
    if (auto it = query.find("category"); it != query.end())
        params.category = it->second;
    if (auto it = query.find("status"); it != query.end())
        params.status = it->second;
    if (auto it = query.find("ids"); it != query.end())
        params.spool_id = it->second;
    else if (auto it = query.find("spoolId"); it != query.end())
        params.spool_id = it->second;
    else if (auto it2 = query.find("spool_id"); it2 != query.end())
        params.spool_id = it2->second;
    if (auto it = query.find("RFIDs"); it != query.end())
        params.rfid = it->second;
    else if (auto it = query.find("rfid"); it != query.end())
        params.rfid = it->second;
    params.offset = parse_int(query, "offset", 0);
    params.limit  = parse_int(query, "limit", 20);

    dispatch_agent_request("list_spools",
                           {{"method", "GET"}, {"query", query}},
                           [params](NetworkAgent* agent, std::string& response_body) mutable {
                               return agent->get_filament_spools(params, &response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::get_filament_config(SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err))
        return;

    dispatch_agent_request("get_filament_config",
                           {{"method", "GET"}},
                           [](NetworkAgent* agent, std::string& response_body) {
                               return agent->get_filament_config(&response_body);
                           },
                           std::move(on_ok),
                           std::move(on_err));
}

void wgtFilaManagerCloudClient::sync_ams(
    BBL::AmsSyncParams params, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err)) return;

    dispatch_agent_request("sync_ams",
                           {{"method", "POST"}, {"dev_id", params.devId},
                            {"item_count", (int) params.items.size()}},
                           [params = std::move(params)](NetworkAgent* agent,
                                                        std::string& response_body) mutable {
                               return agent->sync_ams_filaments(params, &response_body);
                           },
                           std::move(on_ok), std::move(on_err));
}

void wgtFilaManagerCloudClient::sync_slot_mappings(
    BBL::SlotMappingsSyncParams params, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err)) return;

    dispatch_agent_request("sync_slot_mappings",
                           {{"method", "POST"}, {"dev_id", params.devId},
                            {"mapping_count", (int) params.mappings.size()}},
                           [params = std::move(params)](NetworkAgent* agent,
                                                        std::string& response_body) mutable {
                               return agent->sync_slot_mappings(params, &response_body);
                           },
                           std::move(on_ok), std::move(on_err));
}

void wgtFilaManagerCloudClient::get_soft_match_pending(
    BBL::SoftMatchPendingParams params, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err)) return;

    dispatch_agent_request("get_soft_match_pending",
                           {{"method", "GET"}, {"devId", params.devId}, {"amsSn", params.amsSn}},
                           [params = std::move(params)](NetworkAgent* agent,
                                                        std::string& response_body) mutable {
                               return agent->get_soft_match_pending(params, &response_body);
                           },
                           std::move(on_ok), std::move(on_err));
}

void wgtFilaManagerCloudClient::post_soft_match_pending(
    BBL::SoftMatchPendingActionParams params, SuccessFn on_ok, ErrorFn on_err)
{
    if (!check_login(on_err)) return;

    dispatch_agent_request("post_soft_match_pending",
                           {{"method", "POST"}, {"action", params.action},
                            {"spoolId", params.spoolId},
                            {"targetSpoolId", params.targetSpoolId}},
                           [params = std::move(params)](NetworkAgent* agent,
                                                        std::string& response_body) mutable {
                               return agent->post_soft_match_pending(params, &response_body);
                           },
                           std::move(on_ok), std::move(on_err));
}

}} // namespace Slic3r::GUI
