#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cpr/cpr.h>
#include "clihead.hpp"
namespace fs = std::filesystem;

using namespace std;

std::string PluginStoreClient::registerDeveloper(const std::string& username) {
    std::string url = serverBaseUrl + "/api/developers/register";
    json body = {{"username", username}};

    cpr::Response response = cpr::Post(
        cpr::Url{url},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{body.dump()}
    );

    if (response.status_code == 200) {
        json resJson = json::parse(response.text);
        std::string apiKey = resJson["apiKey"].get<std::string>();
        return apiKey;
    } else {
        return "";
    }
}

bool PluginStoreClient::verifyDeveloperCredentials(const std::string& username, const std::string& apiKey) {        
    std::string requestUrl = serverBaseUrl + "/api/developers/verify";

    // Pass credentials as URL parameters
    cpr::Response response = cpr::Get(
        cpr::Url{requestUrl},
        cpr::Parameters{
            {"username", username},
            {"api_key", apiKey}
        }
    );

    if (response.status_code == 200 && response.text == "Yes") {
        return true;
    } else {
        return false;
    }
}

bool PluginStoreClient::uploadPlugin(const std::string& apiKey, const std::string& pluginName, const std::string& description, const std::string& zipFilePath) {

    std::string uploadUrl = serverBaseUrl + "/api/plugins/upload";

    cpr::Multipart multipartData{
        {"apiKey", apiKey},
        {"name", pluginName},
        {"description", description},
        {"source", cpr::File{zipFilePath}}
    };

    cpr::Response response = cpr::Post(cpr::Url{uploadUrl}, multipartData);

    if (response.status_code == 200) {
        return true;
    } else {
        return false;
    }
}

string PluginStoreClient::searchStore(const std::string& searchQuery) {
    std::string url = serverBaseUrl + "/api/store/search?q=" + searchQuery;
    cpr::Response response = cpr::Get(cpr::Url{url});

    if (response.status_code == 200) {
        return response.text;
        //json plugins = json::parse(response.text);
        //
        // for (const auto& item : plugins) {
        //     std::cout << "ID:          " << item["id"].get<std::string>() << "\n";
        //     std::cout << "Name:        " << item["name"].get<std::string>() << "\n";
        //     std::cout << "Creator:     " << item["creator"].get<std::string>() << "\n";
        //     std::cout << "Description: " << item["description"].get<std::string>() << "\n";
        //     std::cout << "DLL URL:     " << item["dll_url"].get<std::string>() << "\n";
        //     std::cout << "------------------------------------------------" << std::endl;
        // }
    } else {
        return "";
    }
}

bool PluginStoreClient::downloadPluginDll(const std::string& pluginId, const std::string& destinationFolderPath, const std::string& outputFilename) {
    
    // Ensure the destination folder exists locally before writing
    if (!fs::exists(destinationFolderPath)) {
        fs::create_directories(destinationFolderPath);
    }

    // Construct full local destination file path (e.g., "C:/MyPlugins/StereoDelay.dll")
    fs::path fullPath = fs::path(destinationFolderPath) / outputFilename;

    // Build API endpoint URL
    std::string requestUrl = serverBaseUrl + "/api/plugins/download-dll?id=" + pluginId;

    // Perform HTTP GET request using CPR
    cpr::Response response = cpr::Get(cpr::Url{requestUrl});

    // Verify successful HTTP response
    if (response.status_code == 200) {
        // Open file stream in binary output mode
        std::ofstream outFile(fullPath, std::ios::binary);

        if (!outFile.is_open()) {
            return false;
        }

        // Write raw response string bytes directly into file
        outFile.write(response.text.data(), response.text.size());
        outFile.close();

        return true;
    } else {
        return false;
    }
}