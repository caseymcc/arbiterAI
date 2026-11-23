#ifndef _ARBITERAI_DASHBOARD_STORAGEPANEL_H_
#define _ARBITERAI_DASHBOARD_STORAGEPANEL_H_

#include "data/dataSource.h"

#include <gweni/controls.h>
#include <gweni/controls/dataTable.h>
#include <gweni/controls/dialogs/query.h>
#include <gweni/controls/progressBar.h>

#include <nlohmann/json.hpp>

#include <ctime>
#include <functional>
#include <string>

namespace arbiterAI
{
namespace dashboard
{

/// How long ago a model was last used, as its row's colour shows it.
enum class Age {
    Fresh,  //!< 14 days or less
    Stale,  //!< 15 to 30 days
    Old     //!< more than 30 days, or never
};

/// \p iso "YYYY-MM-DDTHH:MM:SSZ" against \p now; an empty or unreadable time is Old.
Age ageOf(const std::string &iso, std::time_t now);
/// "YYYY-MM-DD" of \p iso, "-" when empty.
std::string dateOf(const std::string &iso);

/// The Storage tab (the old Downloaded Models page): the storage bar, the
/// active downloads, the cleanup preview and the model files with their
/// hot-ready and protected flags and a guarded delete.
class StoragePanel {
public:
    StoragePanel(gweni::controls::Base *page, DataSource &data, std::function<void()> refresh,
        std::function<std::time_t()> now=nullptr);

    /// /api/storage
    void updateStorage(const nlohmann::json &storage);
    /// /api/storage/models
    void updateModels(const nlohmann::json &models);
    /// /api/downloads
    void updateDownloads(const nlohmann::json &downloads);
    /// /api/storage/cleanup/preview
    void updateCleanup(const nlohmann::json &preview);

    /// POST (on) or DELETE (off) /api/models/<name>/variants/<variant>/<flag>
    void setFlag(const std::string &name, const std::string &variant, const std::string &flag, bool on);
    /// Asks, then DELETE /api/models/<name>/files?variant=<variant>.
    void confirmDelete(const std::string &name, const std::string &variant);
    void deleteFiles(const std::string &name, const std::string &variant);

    gweni::controls::ProgressBar *storageBar() const { return m_bar; }
    const std::string &usedText() const { return m_used->getText(); }
    const std::string &limitText() const { return m_limit->getText(); }
    const std::string &cleanupText() const { return m_cleanup->getText(); }
    const std::string &candidatesText() const { return m_candidates->getText(); }
    gweni::controls::DataTable *modelsTable() const { return m_models; }
    gweni::controls::Base *downloadsCard() const { return m_downloadsCard; }
    gweni::controls::Base *downloads() const { return m_downloads; }
    /// the delete confirmation while it is open
    gweni::dialogs::Dialog *deleteConfirm() const { return m_confirm; }

private:
    DataSource &m_data;
    std::function<void()> m_refresh;
    std::function<std::time_t()> m_now;
    gweni::controls::Base *m_page;

    gweni::controls::Label *m_used;
    gweni::controls::Label *m_limit;
    gweni::controls::ProgressBar *m_bar;
    gweni::controls::Label *m_cleanup;
    gweni::controls::Label *m_candidates;
    gweni::controls::Base *m_downloadsCard;
    gweni::controls::Base *m_downloads;
    gweni::controls::DataTable *m_models;
    gweni::dialogs::Dialog *m_confirm=nullptr;

    std::string m_shownModels;
    std::string m_shownDownloads;
};

}//namespace dashboard
}//namespace arbiterAI

#endif//_ARBITERAI_DASHBOARD_STORAGEPANEL_H_
