#include "app/dashboardApp.h"

#include <gweni/app/window.h>

int main()
{
    gweni::app::ShellOptions options;
    options.title="ArbiterAI";

    // In a page run() never returns and main's stack is gone before the first
    // frame, so everything the UI holds lives on the heap.
    auto *window=new gweni::app::Window(options);
    if(!window->ok())
        return 1;
    new arbiterAI::dashboard::DashboardApp(window->canvas());
    window->run();
    return 0;
}
