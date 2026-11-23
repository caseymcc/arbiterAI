#include "app/dashboardApp.h"
#include "web/fetchDataSource.h"

#include <gweni/app/window.h>
#include <gweni/platforms/platform.h>

int main()
{
    gweni::app::ShellOptions options;
    options.title="ArbiterAI";

    // In a page run() never returns and main's stack is gone before the first
    // frame, so everything the UI holds lives on the heap.
    auto *window=new gweni::app::Window(options);
    if(!window->ok())
        return 1;

    // the log's Copy and Ctrl+C reach the page's clipboard
    gweni::platform::useBrowserClipboard(true);

    auto *data=new arbiterAI::dashboard::FetchDataSource();
    auto *app=new arbiterAI::dashboard::DashboardApp(window->canvas(), *data, gweni::platform::currentUrl(),
        [](const std::string &path) { gweni::platform::pushUrl(path); });
    gweni::platform::setUrlChangedHandler([app](const gweni::String &url) { app->navigate(url); });

    window->run();
    return 0;
}
