#pragma once
#include "nlohmann/json.hpp"
using json = nlohmann::json;

using namespace std;

class PluginStoreClient {
private:
    std::string serverBaseUrl = "http://localhost:3000";

public:
    PluginStoreClient() {};

    // Register a Developer Account and Get API Key, returns empty string on failure
    std::string registerDeveloper(const std::string& username);

    bool verifyDeveloperCredentials(const std::string& username, const std::string& apiKey);

    // Upload Plugin Source Code (.zip) using API Key
    bool uploadPlugin(const std::string& apiKey, const std::string& pluginName, const std::string& description, const std::string& zipFilePath);

    // Search Store for Approved Plugins
    // return empty string on failure
    string searchStore(const std::string& searchQuery);

    // Downloads a plugin DLL by ID and saves it into a target folder path
    bool downloadPluginDll(const std::string& pluginId, const std::string& destinationFolderPath, const std::string& outputFilename);
};