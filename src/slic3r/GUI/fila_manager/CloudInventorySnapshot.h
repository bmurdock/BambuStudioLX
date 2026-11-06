#ifndef slic3r_CloudInventorySnapshot_h_
#define slic3r_CloudInventorySnapshot_h_

#include "nlohmann/json.hpp"
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace Slic3r { namespace GUI {

// Collect a complete, validated snapshot before changing the live inventory.
class CloudInventorySnapshot {
public:
    static constexpr int page_size = 20;

    bool append(const nlohmann::json& response)
    {
        const auto* envelope = &response;
        if (response.is_object() && response.contains("data")) envelope = &response.at("data");
        const nlohmann::json* page = nullptr;
        if (envelope->is_array()) page = envelope;
        else if (envelope->is_object()) {
            for (const auto* key : {"hits", "filaments"}) {
                if (envelope->contains(key) && envelope->at(key).is_array()) {
                    page = &envelope->at(key);
                    break;
                }
            }
        }
        if (!page) throw std::runtime_error("Invalid cloud inventory response");
        const auto* totals = envelope;
        if (!totals->is_object() || !totals->contains("total")) totals = &response;
        if (totals->is_object() && totals->contains("total")) {
            const auto& value = totals->at("total");
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0)
                throw std::runtime_error("Invalid cloud inventory total");
            const auto total = value.get<std::size_t>();
            if (m_total && *m_total != total) throw std::runtime_error("Cloud inventory changed while paging");
            m_total = total;
        }
        for (const auto& item : *page) {
            if (!item.is_object()) throw std::runtime_error("Invalid cloud spool");
            const auto it = item.contains("id") ? item.find("id") : item.find("spool_id");
            if (it == item.end()) throw std::runtime_error("Missing cloud spool id");
            const std::string id = it->is_string() ? it->get<std::string>() :
                (it->is_number_integer() ? it->dump() : std::string());
            if (id.empty() || !m_ids.insert(id).second)
                throw std::runtime_error("Missing or repeated cloud spool id");
            m_items.push_back(item);
        }
        if (m_total) {
            if (m_items.size() > *m_total || (page->empty() && m_items.size() < *m_total))
                throw std::runtime_error("Incomplete cloud inventory snapshot");
            return m_items.size() == *m_total;
        }
        return page->size() < page_size;
    }

    std::size_t offset() const { return m_items.size(); }
    const nlohmann::json& items() const { return m_items; }

private:
    nlohmann::json m_items = nlohmann::json::array();
    std::set<std::string> m_ids;
    std::optional<std::size_t> m_total;
};

}} // namespace Slic3r::GUI
#endif
