#include "ui/app.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    consoleDebugInit(debugDevice_SVC);
    socketInitializeDefault();
    nxlinkStdio();

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        printf("curl_global_init failed\n");
        socketExit();
        return 1;
    }

    DiscordApp app;
    std::string error;
    if (!app.init(error)) {
        printf("Init failed: %s\n", error.c_str());
        curl_global_cleanup();
        socketExit();
        return 1;
    }

    app.run();
    app.shutdown();

    curl_global_cleanup();
    socketExit();
    return 0;
}
