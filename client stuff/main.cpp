#ifndef _WIN32
    #error "This project can only be compiled for Windows OS."
#endif

#include <algorithm>
#include <windows.h>
#include <iterator>
#include <filesystem>
#include <shobjidl.h>
#include <fstream>
#define INITGUID

using namespace std;
namespace fs = std::filesystem;

#include "clihead.hpp"
#include "PluginAPI.hpp"
using CoreAPI = ::femto::CoreAPI;
using CommandQueue = ::femto::CommandQueue;
using APIBindings = ::femto::APIBindings;
using PluginUpdateFunc = ::femto::PluginUpdateFunc;
using plugin_info = ::femto::plugin_info;
using vector_of_window_groups = ::femto::vector_of_window_groups;


enum class KeyState { None, Pressed, Held, Released };

class UniversalKeyTracker {
    std::array<bool, 256> current{};
    std::array<bool, 256> previous{};

public:
    void Poll() {
        previous = current;
        for (int vk = 0; vk < 256; ++vk) {
            current[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
        }
    }

    KeyState GetState(int vk) const {
        bool curr = current[vk];
        bool prev = previous[vk];
        if (curr && !prev) return KeyState::Pressed;
        if (curr && prev)  return KeyState::Held;
        if (!curr && prev) return KeyState::Released;
        return KeyState::None;
    }

    bool IsDown(int vk) const {
        return current[vk];
    }
};

// Converts Win32 Virtual Key codes to human-readable strings
std::string GetKeyName(int vk) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "Ctrl";
        case VK_SHIFT:   case VK_LSHIFT:   case VK_RSHIFT:   return "Shift";
        case VK_MENU:    case VK_LMENU:    case VK_RMENU:    return "Alt";
        case VK_SPACE:  return "Space";
        case VK_RETURN: return "Enter";
        case VK_ESCAPE: return "Esc";
        case VK_TAB:    return "Tab";
        case VK_LEFT: return "Left Arrow";
        case VK_RIGHT: return "Right Arrow";
        case VK_UP: return "Up Arrow";
        case VK_DOWN: return "Down Arrow";
        case VK_BACK: return "Back Space";
        case VK_OEM_3: return "`";
        default: break;
    }
    UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    char name[32] = {0};
    if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) > 0) {
        return std::string(name);
    }
    return std::to_string(vk);
}

std::vector<std::string> GetFrameInputEvents(const UniversalKeyTracker& tracker) {
    std::vector<std::string> events;

    std::string modPrefix = "";
    KeyState remember_state;
    if (tracker.IsDown(VK_CONTROL)) {modPrefix += "Ctrl+"; remember_state = tracker.GetState(VK_CONTROL);}
    if (tracker.IsDown(VK_SHIFT))   {modPrefix += "Shift+"; remember_state = tracker.GetState(VK_SHIFT);}
    if (tracker.IsDown(VK_MENU))    {modPrefix += "Alt+"; remember_state = tracker.GetState(VK_MENU);}

    for (int vk = 1; vk < 255; ++vk) {
        if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LSHIFT   || vk == VK_RSHIFT   || vk == VK_LMENU    || vk == VK_RMENU) {
            continue;
        }
        KeyState state = tracker.GetState(vk);
        if (vk == VK_CONTROL || vk == VK_SHIFT   || vk == VK_MENU) {
            if (modPrefix.empty() && state == KeyState::Released) events.push_back(GetKeyName(vk) + " is released");
            continue;
        }

        if (state == KeyState::None) continue;

        std::string keyName = GetKeyName(vk);
        std::string fullComboName = modPrefix + keyName;

        if (modPrefix == "") {
            if (state == KeyState::Pressed)  events.push_back(fullComboName + " is pressed");
            if (state == KeyState::Held)     events.push_back(fullComboName + " is held");
            if (state == KeyState::Released) events.push_back(fullComboName + " is released");
        } else {
            if (state == KeyState::Pressed)  {
                // events.push_back(fullComboName + " is pressed");
                modPrefix = fullComboName + "+";
                remember_state = state;
            }
            if (state == KeyState::Held) {
                // events.push_back(fullComboName + " is held");
                modPrefix = fullComboName + "+";
                remember_state = state;
            }
            if (state == KeyState::Released) {
                events.push_back(fullComboName + " is released");
                modPrefix = "";
            }
        }

    }

    if (modPrefix != "") {
        std::string statement;
        if (remember_state == KeyState::Pressed) {
            statement = " is pressed";
        }
        if (remember_state == KeyState::Held) {
            statement = " is held";
        }
        if (remember_state == KeyState::Released) {
            statement = " is released";
        }
        events.push_back(modPrefix.substr(0,modPrefix.size()-1) + statement); // how do i get action
        modPrefix = "";
    }

    return events;
}

class ConsoleRenderer {
private:
    int width;                                // Holds the fixed character width of the rendering screen
    int height;                               // Holds the fixed character height of the rendering screen
    char* currentBufferPtr;   // Stores the new frame state built during the current tick
    char* previousBufferPtr;  // Stores the frame state that is currently visible on screen
    string outputBuffer;                 // Accumulates ANSI sequences and text for a single stream write

public:
    // Constructor: initializes dimensions and sizes the buffers to blank spaces
    ConsoleRenderer(char* incurrbfrptr, char* inprvsbfrptr, int w, int h) : currentBufferPtr(incurrbfrptr), previousBufferPtr(inprvsbfrptr), width(w), height(h) {
        memset(currentBufferPtr,' ', width * height * sizeof(char));   // Fills current frame with blank rows
        memset(previousBufferPtr,' ', width * height * sizeof(char));  // Fills previous frame with blank rows
        outputBuffer.reserve(width * height * 2);                // Pre-allocates memory to avoid reallocation overhead
    }

    void enterScreenMode(bool* remem_ptr) {
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        if (hInput != INVALID_HANDLE_VALUE) {
            FlushConsoleInputBuffer(hInput);
        }

        // \x1b[?25l : Hide cursor
        // \x1b[2J   : Clear screen
        // \x1b[3J   : Clear scrollback buffer (Fixes VS Code duplication!)
        // \x1b[H    : Move cursor to (1,1)
        std::cout << "\x1b[?25l\x1b[2J\x1b[3J\x1b[H" << std::flush;
        string blank_framed = " " + string(width,'_') + " \n";
        for (int i = 0; i < height; i++) {
            blank_framed += "|" + string(width,' ') + "|\n";
        }
        blank_framed += " " + string(width,'_') + " \x1b[H";
        cout << blank_framed;

        clearBufferPrvs();

        if (remem_ptr != nullptr) {
            *remem_ptr = true;
        }
    }

    void exitScreenMode() {
        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        if (hInput != INVALID_HANDLE_VALUE) {
            FlushConsoleInputBuffer(hInput);
        }

        // 1. Get Windows console output handle
        HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOutput != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOutput, &mode)) {
                // Restore default Windows output processing and auto line-wrapping
                SetConsoleMode(hOutput, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
            }
        }

        // 2. Output ANSI restoration sequence:
        // \x1b[0m  : Reset all text formatting (colors, bold, underline)
        // \x1b[?25h : Unhide/show terminal cursor
        // \x1b[2J  : Clear visible screen
        // \x1b[3J  : Clear scrollback buffer (prevents VS Code terminal duplicating lines)
        // \x1b[H   : Move cursor to home position (1,1)
        std::cout << "\x1b[0m\x1b[?25h\x1b[2J\x1b[3J\x1b[H" << std::flush;
    }

    // Clears the working buffer to prepare for rendering the next frame
    void clearBuffer() {
        memset(currentBufferPtr,' ', width * height * sizeof(char));   // Fills current frame with blank rows
    }

    // 4. Compare buffers, batch draw calls, and render efficiently
    void present() {
        outputBuffer.clear(); // Clears string stream accumulator from the prior frame tick

        for (int y = 0; y < height; ++y) {                   // Loop through each row (Y coordinate)
            for (int x = 0; x < width; ++x) {               // Loop through each column (X coordinate)
                
                // Checks if the new frame character differs from the visible frame character
                if (currentBufferPtr[y*width + x] != previousBufferPtr[y*width + x]) {
                    
                    int startX = x;       // Remembers the starting column index for this batch of changes
                    std::string segment;  // Temporary string to aggregate contiguous changed characters

                    // Collects adjacent changed characters on the same line into a single string batch
                    while (x < width && currentBufferPtr[y*width + x] != previousBufferPtr[y*width + x]) {
                        segment += currentBufferPtr[y*width + x];                        // Appends new character to segment
                        previousBufferPtr[y*width + x] = currentBufferPtr[y*width + x];           // Updates previous buffer state in lockstep
                        x++;                                                  // Advances column counter
                    }

                    // Appends ANSI jump sequence (\x1b[Row;ColumnH) using 1-based indexing
                    // i plus oned both cuz of frame
                    outputBuffer += "\x1b[" + std::to_string(y + 2) + ";" + std::to_string(startX + 2) + "H";
                    outputBuffer += segment; // Appends batched character text directly after cursor command

                    x--; // Adjusts outer loop index to offset extra increment from internal while loop
                }
            }
        }

        // Executes single continuous stream send to prevent expensive multi-step console hardware writes
        if (!outputBuffer.empty()) {
            std::cout << outputBuffer << std::flush; // Flushes output stream immediately to console screen
        }
    }

private:
    // Helper function to reset both buffers to a blank space grid state
    void clearBufferPrvs() {
        memset(previousBufferPtr,' ', width * height * sizeof(char));  // Fills previous frame with blank rows
        
    }
};

// Helper to dynamically load a DLL and extract the plugin entry function
PluginUpdateFunc LoadPluginSymbol(const char* path, void** outHandle) {
    HMODULE handle = LoadLibraryA(path);
    if (!handle) {
        cerr << "Failed to load DLL: " << path << "\n";
        return nullptr;
    }
    *outHandle = handle;
    return (PluginUpdateFunc)GetProcAddress(handle, "RunPluginThread");
}

void UnloadPlugin(void* handle) {
    if (!handle) return;
    FreeLibrary((HMODULE)handle);
}

void make_active(string in_plugin_name, CoreAPI& in_api, int in_key, map<int,thread>& all_threads, int& thread_counter, APIBindings& globalBindings) {
    void* pluginHandle = nullptr;
    PluginUpdateFunc pluginThreadFunc = LoadPluginSymbol((*in_api.all_plugins_to_path_get_ptr(in_key))[in_plugin_name].c_str(), &pluginHandle);
    if (!pluginThreadFunc || pluginThreadFunc == nullptr) {
        cout << "couldnt get dll dawg" << endl;
    } else {
        auto gotten_window_group = in_api.create_window_group(in_key);
        globalBindings.create_bind_group(gotten_window_group, in_key);
        thread_counter++;
        in_api.active_plugins_to_info_assign(in_plugin_name, pluginHandle, pluginThreadFunc, gotten_window_group, thread_counter, in_key);
        in_api.set_is_running(gotten_window_group, true, in_key);
        // 2. Launch the plugin thread using the function extracted from DLL
        thread pluginThread(pluginThreadFunc, &in_api, gotten_window_group);
        all_threads[thread_counter] = move(pluginThread);
    
    }
}

void make_unactive(CoreAPI& api, string plugin_name, plugin_info the_plugin_info, map<int,thread>& all_threads, int key, APIBindings& globalBindings, vector_of_window_groups& all_window_groups) {
    api.set_is_running(the_plugin_info.win_grp_handle,false,key);
    if (all_threads[the_plugin_info.thread_index].joinable()) {
        all_threads[the_plugin_info.thread_index].join();
    }
    globalBindings.remove_bind_group(the_plugin_info.win_grp_handle, key);
    (*api.get_window_num_ptr(key)) -= (*all_window_groups.get_ptr(key))[the_plugin_info.win_grp_handle].the_windows.size();
    api.remove_window_group(the_plugin_info.win_grp_handle,key);
    UnloadPlugin(the_plugin_info.handle);
    api.active_plugins_to_info_remove(plugin_name, key);
}

vector<array<int,4>> unhiding_options(bool* occupance_screen_ptr, int screen_width, int screen_height, int min_dim[2]) {
    min_dim[0] += 2;
    min_dim[1] += 2;
    // x y width height
    vector<array<int,4>> successes = {};
    //  x        y width score
    map<int,array<int,3>> live_checking = {};
    for (int y = 0; y < screen_height; y++) {
        int x = 0;
        while (x < screen_width) {
            if (!occupance_screen_ptr[y*screen_width + x]) {
                if (live_checking.find(x) != live_checking.end()) {
                    int inner_x = x;
                    bool condition = inner_x < screen_width && inner_x < x + live_checking[x][1];
                    if (condition) {
                        condition = !occupance_screen_ptr[y*screen_width + inner_x];
                    }
                    while (condition) {
                        inner_x++;
                        condition = inner_x < screen_width && inner_x < x + live_checking[x][1];
                        if (condition) {
                            condition = !occupance_screen_ptr[y*screen_width + inner_x];
                        }
                    }
                    if (inner_x == x + live_checking[x][1]) {
                        live_checking[x][2]++;
                    } else {
                        //cout << x << " " << live_checking[x][0] << " " << live_checking[x][1] << " " << live_checking[x][2]-1 << "removing" << endl;
                        if (live_checking[x][2] >= min_dim[1]) {
                            //cout << "yay" << endl;
                            successes.push_back({x,live_checking[x][0],live_checking[x][1],live_checking[x][2]-1});
                        }
                        live_checking.erase(x);
                    }
                    x += inner_x - x - 1;
                }
            }
            x++;
        }

        x = 0;
        while (x < screen_width) {
            if (!occupance_screen_ptr[y*screen_width + x]) {
                if (live_checking.find(x) == live_checking.end()) {
                    int inner_x = x;
                    bool condition = inner_x < screen_width;
                    if (condition) {
                        condition = !occupance_screen_ptr[y*screen_width + inner_x];
                    }
                    while (condition) {
                        inner_x++;
                        condition = inner_x < screen_width;
                        if (condition) {
                            condition = !occupance_screen_ptr[y*screen_width + inner_x];
                        }
                    }
                    if (inner_x - x >= min_dim[0]) {
                        live_checking[x] = {y,inner_x - x,1};
                        //cout << x << " " << y << " " << inner_x - x << " " << 1 << endl;
                    }
                    x += inner_x - x - 1;
                } else {
                    x += live_checking[x][1] - 1;
                }
            }
            x++;
        }
    }

    for (auto a_live_check : live_checking) {
        auto x = a_live_check.first;
        //cout << x << " " << live_checking[x][0] << " " << live_checking[x][1] << " " << live_checking[x][2]-1 << "removing" << endl;                        
        if (a_live_check.second[2] >= min_dim[1]) {
            //cout << "yay" << endl;
            successes.push_back({a_live_check.first, a_live_check.second[0], a_live_check.second[1], a_live_check.second[2]-1});
        }
    }




    // for (auto a_success : successes) {
    //     cout << a_success[0] << " " << a_success[1] << " " << a_success[2] << " " << a_success[3] << endl;
    // }
    //cout << endl;
    return successes;
}

std::string getUserInput(const std::string promptText) {
    // 1. Flush OS-level input buffer (drops keys pressed during rendering)
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput != INVALID_HANDLE_VALUE) {
        FlushConsoleInputBuffer(hInput);
    }

    // 2. Reset error flags on std::cin
    std::cin.clear();

    // 3. Only ignore leftover characters if there are actually any waiting in the C++ stream buffer
    if (std::cin.rdbuf()->in_avail() > 0) {
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }

    // 4. Print prompt FIRST before reading input
    std::cout << promptText << std::flush;

    // 5. Capture the actual user input
    std::string userInput;
    std::getline(std::cin, userInput);

    return userInput;
}

std::string ConvertWStringToString(const std::wstring wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

string canon(string in_file_path) {
    if (in_file_path == "") {
        return "";
    } else {
        return fs::canonical(in_file_path).string();
    }
}

std::string SelectFile(wstring file_type, wstring file_again) {
    std::wstring filePath = L"";

    // Initialize COM library
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        return "";
    }

    IFileOpenDialog* pFileOpen = nullptr;

    // Create the FileOpenDialog object
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, 
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

    if (SUCCEEDED(hr)) {
        // Define the file type filter for .zip files
        COMDLG_FILTERSPEC fileTypes[] = {
            { file_type.c_str(), file_again.c_str() }
        };

        // Apply the filter to the dialog
        pFileOpen->SetFileTypes(ARRAYSIZE(fileTypes), fileTypes);
        pFileOpen->SetFileTypeIndex(1);

        // Optional: Ensure the user can only select files that exist
        DWORD dwFlags;
        pFileOpen->GetOptions(&dwFlags);
        pFileOpen->SetOptions(dwFlags | FOS_FILEMUSTEXIST);

        // Show the Open dialog box
        hr = pFileOpen->Show(NULL);

        // Retrieve the file name if the user clicked "Open"
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                if (SUCCEEDED(hr)) {
                    filePath = pszFilePath;
                    CoTaskMemFree(pszFilePath); // Free memory allocated by Windows
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }

    CoUninitialize();
    return ConvertWStringToString(filePath);
}

vector<char> alphabets = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};

bool getConsoleSizeWindows(int& width, int& height) {
    // 1. Create a direct file handle to the active console screen buffer
    HANDLE hConsole = CreateFileW(
        L"CONOUT$",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hConsole == INVALID_HANDLE_VALUE) {
        return false;
    }

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        // Calculate the visible window size in characters
        width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        
        CloseHandle(hConsole);
        return true;
    }

    CloseHandle(hConsole);
    return false;
}

int main() {
    PluginStoreClient client;

    try {
        fs::create_directories("plugins");
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 0;
    }

    auto plugins_folder_path = fs::canonical("plugins").string();

    UniversalKeyTracker tracker;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<int> distrib(1, 2000000000);
    int key = distrib(gen);
    
    CommandQueue globalCommandQueue;
    APIBindings globalBindings(key);
    vector_of_window_groups all_window_groups(key);
    vector<string> notifications;
    pair<atomic<int>,atomic<int>> window_focused = {-1,-1};
    CoreAPI api(globalCommandQueue,globalBindings,all_window_groups,notifications,key,key,window_focused);
    map<int,thread> all_threads = {};
    int thread_counter = 0;
    // yes 2 less both
    int screen_width;
    int screen_height;
    bool size_successful = getConsoleSizeWindows(screen_width, screen_height);
    if (!size_successful) {screen_width = 85; screen_height=25;}
    if (screen_width < 85 || screen_height < 25) {cout << "Terminal Window too small";return 0;}
    screen_width-=2;
    screen_height-=2;
    char global_screen[screen_height][screen_width];
    char local_screen[screen_height][screen_width];
    bool occupance_screen[screen_height][screen_width];
    memset(global_screen, ' ', screen_width * screen_height * sizeof(char));
    vector<pair<int,int>> shown_windows;
    vector<pair<int,int>> hidden_windows;
    bool remem_to_delete_key = false;
    string mode = "main";
    string username = "null";
    string password = "";
    bool exit = false;

    // vars for main, one is above
    pair<int,int> null_window = {-1,-1};
    pair<int,int> window_in_hand = {-1,-1};
    int normal_number = -1;
    int nwih = 0;
    vector<array<int,4>> held_options;
    int held_index = -1;
    int notif_time = 0;
    int notif_count = 0;
    string notif_message = "";
    // vars for notifications
    int nc = 0;
    int notif_index = -1;
    // vars for plugins
    int plugin_index = -1;
    // vars for shop
    int shop_index = -1;
    vector<string> search_results = {};
    vector<array<string,3>> search_data = {};

    if (fs::exists("cookies.txt")) {
        string cookie_line;
        ifstream cookies_read("cookies.txt");
        bool condition = true;
        string hold_prev;
        int frame = 0;
        while (getline (cookies_read, cookie_line)) {
            if (condition) {
                if (cookie_line != "\\e") {
                    if (frame % 2 == 0) {
                        hold_prev = cookie_line;
                    } else {
                        (*api.all_plugins_to_path_get_ptr(key))[hold_prev] = cookie_line;
                    }
                } else {
                    condition = false;
                }
                frame++;
            } else {
                make_active(cookie_line, api, key, all_threads, thread_counter, globalBindings);
            }
        }
        cookies_read.close();
    }


    ConsoleRenderer renderer((char*) &local_screen, (char*) &global_screen, screen_width, screen_height);
    renderer.enterScreenMode(nullptr);

    // 3. Main frame loop
    auto previousTime = chrono::high_resolution_clock::now();
    int frame_no = 0;
    char fps_char[7] = "      ";
    while (!exit) {
        tracker.Poll();
        auto frameInputs = GetFrameInputEvents(tracker);
        exit = GetAsyncKeyState(VK_ESCAPE) & 0x8000;
        if (remem_to_delete_key) {
            frameInputs.clear();
            remem_to_delete_key = false;
        }

        memset(local_screen, ' ', screen_width * screen_height * sizeof(char));
        memset(occupance_screen, false, screen_width * screen_height * sizeof(bool));

        notif_count = notifications.size();

        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 14; y++) {
                occupance_screen[y][x] = true;
                local_screen[y][x] = ' ';
            }
        }
        for (auto a_window : shown_windows) {
            if ((*all_window_groups.get_ptr(key))[a_window.first].the_windows.find(a_window.second) != (*all_window_groups.get_ptr(key))[a_window.first].the_windows.end()) {
                auto window_data = (*all_window_groups.get_ptr(key))[a_window.first].the_windows[a_window.second];
                for (int x = 0; x < window_data.curr_dim[0] + 2; x++) {
                    for (int y = 0; y < window_data.curr_dim[1] + 2; y++) {
                        occupance_screen[window_data.pos[1] + y][window_data.pos[0] + x] = true;
                    }
                }
            }
        }

        auto gotten_bindings = globalBindings.get(key);
        if (!frameInputs.empty()) {
            for (int i = 0; i < frameInputs.size(); i++) {
                pair<string,string> rep = {"keyboard",frameInputs[i]};
                for (auto [win_grp_handle,local_binds_map] : gotten_bindings) {
                    if (local_binds_map.find(rep) != local_binds_map.end()) {
                        thread t(local_binds_map[rep]);
                        t.detach();
                    }
                }
            }
            
        }

        globalCommandQueue.FlushAndExecute(gotten_bindings);

        for (auto [win_grp_handle,win_grp_in_all] : (*all_window_groups.get_ptr(key))) {
            for (auto [win_handle,win_in_all] : (*all_window_groups.get_ptr(key))[win_grp_handle].the_windows) {
                if (find(shown_windows.begin(),shown_windows.end(),make_pair(win_grp_handle,win_handle)) == shown_windows.end() &&
                    find(hidden_windows.begin(),hidden_windows.end(),make_pair(win_grp_handle,win_handle)) == hidden_windows.end()) {
                    auto the_options = unhiding_options((bool*) &occupance_screen, screen_width, screen_height, win_in_all.min_dim);
                    if (the_options.size() > 0) {
                        auto first_option = the_options[0];
                        first_option[2] -= 2;
                        first_option[3] -= 2;
                        win_in_all.pos[0] = first_option[0];
                        win_in_all.pos[1] = first_option[1];
                        if (win_in_all.max_dim[0] < first_option[2]) {
                            win_in_all.curr_dim[0] = win_in_all.max_dim[0];
                        } else {
                            win_in_all.curr_dim[0] = first_option[2];
                        }
                        if (win_in_all.max_dim[1] < first_option[3]) {
                            win_in_all.curr_dim[1] = win_in_all.max_dim[1];
                        } else {
                            win_in_all.curr_dim[1] = first_option[3];
                        }
                        (*all_window_groups.get_ptr(key))[win_grp_handle].the_windows[win_handle] = win_in_all;
                        shown_windows.insert(shown_windows.begin(), make_pair(win_grp_handle,win_handle));
                        for (int x = win_in_all.pos[0]; x < win_in_all.pos[0] + win_in_all.curr_dim[0] + 2; x++) {
                            for (int y = win_in_all.pos[1]; y < win_in_all.pos[1] + win_in_all.curr_dim[1] + 2; y++) {
                                occupance_screen[y][x] = true;
                            }
                        }
                    } else {
                        hidden_windows.insert(hidden_windows.begin(), make_pair(win_grp_handle,win_handle));
                        api.gen_notification("too cluttered");
                    }
                }
            }
        }
        int i = 0;
        while (i < hidden_windows.size()) {
            auto win_rep = hidden_windows[i];
            if ((*all_window_groups.get_ptr(key)).find(win_rep.first) != (*all_window_groups.get_ptr(key)).end()) {
                if ((*all_window_groups.get_ptr(key))[win_rep.first].the_windows.find(win_rep.second) == (*all_window_groups.get_ptr(key))[win_rep.first].the_windows.end()) {
                    hidden_windows.erase(hidden_windows.begin() + i);
                    continue;
                }
            } else {
                hidden_windows.erase(hidden_windows.begin() + i);
                continue;
            }
            i++;
        }
        i = 0;
        while (i < shown_windows.size()) {
            auto win_rep = shown_windows[i];
            if ((*all_window_groups.get_ptr(key)).find(win_rep.first) != (*all_window_groups.get_ptr(key)).end()) {
                if ((*all_window_groups.get_ptr(key))[win_rep.first].the_windows.find(win_rep.second) == (*all_window_groups.get_ptr(key))[win_rep.first].the_windows.end()) {
                    shown_windows.erase(shown_windows.begin() + i);
                    continue;
                }
            } else {
                shown_windows.erase(shown_windows.begin() + i);
                continue;
            }
            i++;
        }

        
        if (mode == "main") {
            for (auto a_window : shown_windows) {
                auto window_data = (*all_window_groups.get_ptr(key))[a_window.first].the_windows[a_window.second];
                for (int y = window_data.pos[1] + 1; y < window_data.pos[1] + window_data.curr_dim[1] + 2; y++) {
                    if (a_window == window_in_hand) {
                        local_screen[y][window_data.pos[0]] = '|';
                        local_screen[y][window_data.pos[0] + window_data.curr_dim[0] + 1] = '|';
                    } else {
                        local_screen[y][window_data.pos[0]] = '.';
                        local_screen[y][window_data.pos[0] + window_data.curr_dim[0] + 1] = '.';
                    }
                }
                for (int x = window_data.pos[0] + 1; x < window_data.pos[0] + window_data.curr_dim[0] + 1; x++) {
                    if (a_window == window_in_hand) {
                        local_screen[window_data.pos[1]][x] = '_';
                        local_screen[window_data.pos[1] + window_data.curr_dim[1] + 1][x] = '_';
                    } else {
                        local_screen[window_data.pos[1]][x] = '.';
                        local_screen[window_data.pos[1] + window_data.curr_dim[1] + 1][x] = '.';
                    }
                }
                string name = window_data.name;
                if (name.size() > window_data.curr_dim[0] - 5) {
                    name = name.substr(0, window_data.curr_dim[0] - 7) + "..";
                }
                auto name_c_str = name.c_str();
                memcpy(&local_screen[window_data.pos[1]][window_data.pos[0]+2],name_c_str,sizeof(char)*name.length());
                
                local_screen[window_data.pos[1]][window_data.pos[0] + window_data.curr_dim[0] - 2] = alphabets[distance(shown_windows.begin(),find(shown_windows.begin(), shown_windows.end(), a_window))];
                
                for (int x = 0; x < window_data.curr_dim[0]; x++) {
                    for (int y = 0; y < window_data.curr_dim[1]; y++) {
                        local_screen[window_data.pos[1] + 1 + y][window_data.pos[0] + 1 + x] = window_data.screen[y][x];
                    }
                }
            }

            if (nwih == 3 && held_index != -1) {
                auto held_info = held_options[held_index];
                for (int x = held_info[0] + 1; x < held_info[0] + held_info[2] - 1; x++) {
                    for (int y = held_info[1] + 1; y < held_info[1] + held_info[3] - 1; y++) {
                        local_screen[y][x] = 'H';
                    }
                }
            }
            
            for (int y = 0; y < 14; y++) {
                local_screen[y][14] = '|';
            }
            for (int x = 0; x < 14; x++) {
                local_screen[13][x] = '_';
            }
            if (nwih == 2) {
                char hidden_str[] = "- HIDDEN";
                memcpy(&local_screen[1][1],hidden_str,sizeof(char) * 8);
            } else {
                char hidden_str[] = "  HIDDEN";
                memcpy(&local_screen[1][1],hidden_str,sizeof(char) * 8);
            }
            if (hidden_windows.size() >= 0) {
                int frame_starter_index;
                if (window_in_hand != null_window && (nwih == 2 || nwih == 3)) {
                    frame_starter_index = (distance(hidden_windows.begin(), find(hidden_windows.begin(), hidden_windows.end(), window_in_hand)) / 5) * 5;
                } else {
                    frame_starter_index = 0;
                }
                int iterate_till;
                if (hidden_windows.size() > frame_starter_index + 4) {
                    iterate_till = 5;
                } else {
                    iterate_till = hidden_windows.size() - frame_starter_index;
                }
                for (int i = frame_starter_index; i < frame_starter_index + iterate_till; i++) {
                    auto window_data = (*all_window_groups.get_ptr(key))[hidden_windows[i].first].the_windows[hidden_windows[i].second];
                    int y = 3 + (i - frame_starter_index)*2;
                    if (hidden_windows[i] == window_in_hand) {
                        local_screen[y][1] = '-';
                    }
                    string name = window_data.name;
                    if (name.size() > 8) {
                        name = name.substr(0,6) + ".. ";
                    } else {
                        name = name + string(9 - name.size(),' ');
                    }
                    name += alphabets[i - frame_starter_index];
                    auto the_c_str = name.c_str();
                    memcpy(&local_screen[y][3],the_c_str,name.length());
                }
            }

        } else if (mode == "notifications") {
            char notifications_c_str[] = "NOTIFICATIONS";
            if (nc == 1) {
                local_screen[1][(screen_width - 13*sizeof(char))/2 - 2] = '-';
            }
            memcpy(&local_screen[1][(screen_width - 13*sizeof(char))/2],notifications_c_str,13*sizeof(char));
            int frame_starter_index;
            if (nc == 1) {
                frame_starter_index = (notif_index / 5) * 5;
            } else {
                frame_starter_index = 0;
            }
            int iterate_till;
            if (notifications.size() > frame_starter_index + 4) {
                iterate_till = 5;
            } else {
                iterate_till = notifications.size() - frame_starter_index;
            }
            int left_boundary = screen_width / 3;
            for (int i = frame_starter_index; i < frame_starter_index + iterate_till; i++) {
                string notif_message_preview;
                if (notifications[i].find('\n') != string::npos) {
                    notif_message_preview = notifications[i].substr(0,notifications[i].find('\n'));
                } else {
                    notif_message_preview = notifications[i];
                }
                int y = 3 + (i - frame_starter_index)*2;
                if (i == notif_index) {
                    local_screen[y][left_boundary - 2] = '-';
                }
                if (notif_message_preview.size() > left_boundary) {
                    notif_message_preview = notif_message_preview.substr(0,left_boundary - 2) + "..";
                } else {
                    notif_message_preview = notif_message_preview + string(left_boundary - notif_message_preview.size(),' ');
                }
                auto the_c_str = notif_message_preview.c_str();
                memcpy(&local_screen[y][left_boundary],the_c_str,sizeof(char)*notif_message_preview.length()-1);
            }

        } else if (mode == "plugins") {
            char plugins_c_str[] = "- PLUGINS";
            memcpy(&local_screen[1][(screen_width - 9*sizeof(char))/2 - 2],plugins_c_str,9*sizeof(char));
            if (plugin_index != -1) {
                int frame_starter_index = (plugin_index / 5) * 5;
                int iterate_till;
                vector<string> all_plugins_name_list;
                all_plugins_name_list.reserve(api.all_plugins_to_path_get_ptr(key)->size());
                for (auto a_pair : (*api.all_plugins_to_path_get_ptr(key))) {
                    all_plugins_name_list.push_back(a_pair.first);
                }

                if (all_plugins_name_list.size() > frame_starter_index + 4) {
                    iterate_till = 5;
                } else {
                    iterate_till = all_plugins_name_list.size() - frame_starter_index;
                }
                int left_boundary = screen_width / 3;
                for (int i = frame_starter_index; i < frame_starter_index + iterate_till; i++) {
                    string plugin_name_preview = all_plugins_name_list[i];
                    int y = 3 + (i - frame_starter_index)*2;
                    if (i == plugin_index) {
                        local_screen[y][left_boundary - 2] = '-';
                    }
                    if (plugin_name_preview.size() > left_boundary) {
                        plugin_name_preview = all_plugins_name_list[i].substr(0,left_boundary - 2) + ".. ";
                    } else {
                        plugin_name_preview = all_plugins_name_list[i] + string(left_boundary - plugin_name_preview.size() + 1,' ');
                    }
                    auto the_c_str = plugin_name_preview.c_str();
                    memcpy(&local_screen[y][left_boundary],the_c_str,sizeof(char)*plugin_name_preview.length()-1);
                    if (api.active_plugins_to_info_is_in(all_plugins_name_list[i],key)) {
                        char the_c_str[] = " ENABLED";
                        memcpy(&local_screen[y][left_boundary*2 + 2],the_c_str,sizeof(char)*8);
                    } else {
                        char the_c_str[] = "DISABLED";
                        memcpy(&local_screen[y][left_boundary*2 + 2],the_c_str,sizeof(char)*8);
                    }
                }
            }
        } else if (mode == "shop") {
            char search_c_str[] = "- SEARCH RESULTS";
            memcpy(&local_screen[1][(screen_width - 16*sizeof(char))/2 - 2],search_c_str,16*sizeof(char));
            int left_boundary = screen_width / 3;
            if (shop_index != -1) {
                int frame_starter_index = (shop_index / 5) * 5;
                int iterate_till;

                if (search_results.size() > frame_starter_index + 4) {
                    iterate_till = 5;
                } else {
                    iterate_till = search_results.size() - frame_starter_index;
                }
                for (int i = frame_starter_index; i < frame_starter_index + iterate_till; i++) {
                    string result_name_preview = search_results[i];
                    int y = 3 + (i - frame_starter_index)*2;
                    if (i == shop_index) {
                        local_screen[y][left_boundary - 2] = '-';
                    }
                    if (result_name_preview.size() > left_boundary) {
                        result_name_preview = search_results[i].substr(0,left_boundary - 2) + ".. ";
                    } else {
                        result_name_preview = search_results[i] + string(left_boundary - result_name_preview.size() + 1,' ');
                    }
                    auto the_c_str = result_name_preview.c_str();
                    memcpy(&local_screen[y][left_boundary],the_c_str,sizeof(char)*result_name_preview.length());
                }
            } else {
                string result_name_preview = "NO RESULTS";
                auto the_c_str = result_name_preview.c_str();
                memcpy(&local_screen[3][left_boundary],the_c_str,sizeof(char)*result_name_preview.length());
            }
        } else if (mode == "dev") {
            char dev_c_str[] = "- DEVELOPER";
            memcpy(&local_screen[1][(screen_width - 11*sizeof(char))/2 - 2],dev_c_str,11*sizeof(char));
            string temp_user = "username : " + username;
            auto user_c_str = temp_user.c_str();
            memcpy(&local_screen[3][(screen_width - temp_user.length()*sizeof(char))/2 - 2],user_c_str,temp_user.length()*sizeof(char));
            string ins_str_1 = "SIGNUP : 1";
            string ins_str_2 = "LOGIN : 2";
            string ins_str_3 = "LOGOUT : 3";
            string ins_str_4 = "TEST : 4";
            string ins_str_5 = "UPLOAD : 5";
            auto ins_char_1 = ins_str_1.c_str();
            auto ins_char_2 = ins_str_2.c_str();
            auto ins_char_3 = ins_str_3.c_str();
            auto ins_char_4 = ins_str_4.c_str();
            auto ins_char_5 = ins_str_5.c_str();
            memcpy(&local_screen[6][(screen_width - ins_str_1.length()*sizeof(char))/2],ins_char_1,ins_str_1.length()*sizeof(char));
            memcpy(&local_screen[8][(screen_width - ins_str_2.length()*sizeof(char))/2],ins_char_2,ins_str_2.length()*sizeof(char));
            memcpy(&local_screen[10][(screen_width - ins_str_3.length()*sizeof(char))/2],ins_char_3,ins_str_3.length()*sizeof(char));
            memcpy(&local_screen[12][(screen_width - ins_str_4.length()*sizeof(char))/2],ins_char_4,ins_str_4.length()*sizeof(char));
            memcpy(&local_screen[14][(screen_width - ins_str_5.length()*sizeof(char))/2],ins_char_5,ins_str_5.length()*sizeof(char));
        }
        for (auto an_input : frameInputs) {
            string alphabet = "";
            if (an_input.substr(1,11) == " is pressed" && isalpha(an_input[0])) {alphabet = an_input[0];}
            string digit = "";
            if (an_input.substr(1,11) == " is pressed" && isdigit(an_input[0])) {digit = an_input[0];}

            if (mode == "main") {

                if (an_input == "Tab is pressed" && (nwih == 0 || nwih == 2)) {
                    nwih = 1;
                } else if (an_input == "Tab is pressed" && nwih != 0) {
                    nwih = 0;
                    window_in_hand = null_window;
                    window_focused = null_window;
                    normal_number = -1;
                    held_index = -1;
                }
                
                if (an_input == "Back Space is pressed" && window_focused.first.load() == -1 && window_focused.second.load() == -1 && window_in_hand != null_window && nwih != 0) {
                    (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows.erase(window_in_hand.second);
                    (*api.get_window_num_ptr(key))--;
                    window_in_hand = null_window;
                    normal_number = -1;
                    held_index = -1;
                    nwih = 1;
                }
                
                if (alphabet != "" && nwih != 2 && nwih != 0 && window_focused.first.load() == -1 && window_focused.second.load() == -1) {
                    int alpha_index = distance(alphabets.begin(), find(alphabets.begin(), alphabets.end(), alphabet[0]));
                    if (alpha_index < shown_windows.size()) {
                        window_in_hand = shown_windows[alpha_index];
                        normal_number = -1;
                        nwih = 1;
                    }
                }
                
                if (digit != "" && nwih == 1 && window_focused.first.load() == -1 && window_focused.second.load() == -1 && window_in_hand != null_window) {
                    int number = stoi(digit);
                    if (number > -1 && number < 5) {
                        normal_number = number;
                    }
                }
                
                if (nwih == 1 && window_in_hand != null_window && window_focused.first.load() == -1 && window_focused.second.load() == -1) {
                    auto window_data = (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second];
                    if (normal_number == 0) {
                        if (an_input == "Up Arrow is pressed" || an_input == "Up Arrow is held") {
                            bool condition = window_data.pos[1] == 1;
                            if (!condition) {
                                condition = window_data.pos[1] > 0;
                                for (int cycle = 0; cycle < window_data.curr_dim[0] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1]-1][window_data.pos[0] + cycle];
                                    }
                                }
                            }
                            if (condition) {(*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[1]--;}
                        }
                        if (an_input == "Down Arrow is pressed" || an_input == "Down Arrow is held") {
                            bool condition = window_data.pos[1] + window_data.curr_dim[1] + 3 == screen_height;
                            if (!condition) {
                                condition = window_data.pos[1] + window_data.curr_dim[1] + 2 < screen_height;
                                for (int cycle = 0; cycle < window_data.curr_dim[0] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + window_data.curr_dim[1] + 2][window_data.pos[0] + cycle];
                                    }
                                }
                            }
                            if (condition) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[1]++;
                            }
                        }
                        if (an_input == "Left Arrow is pressed" || an_input == "Left Arrow is held") {
                            bool condition = window_data.pos[0] == 1;
                            if (!condition) {
                                condition = window_data.pos[0] > 0;
                                for (int cycle = 0; cycle < window_data.curr_dim[1] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + cycle][window_data.pos[0]-1];
                                    }
                                }
                            }
                            if (condition) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[0]--;
                            }
                        }
                        if (an_input == "Right Arrow is pressed" || an_input == "Right Arrow is held") {
                            bool condition = window_data.pos[0] + window_data.curr_dim[0] + 3 == screen_width;
                            if (!condition) {
                                condition = window_data.pos[0] + window_data.curr_dim[0] + 2 < screen_width;
                                for (int cycle = 0; cycle < window_data.curr_dim[1] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + cycle][window_data.pos[0] + window_data.curr_dim[0] + 2];
                                    }
                                }
                            }
                            if (condition) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[0]++;
                            }
                        }
                    } else if (normal_number == 1) {
                        if (an_input == "Up Arrow is pressed" || an_input == "Up Arrow is held") {
                            bool condition = window_data.pos[1] == 1;
                            if (!condition) {
                                condition = window_data.pos[1] > 0;
                                for (int cycle = 0; cycle < window_data.curr_dim[0] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1]-2][window_data.pos[0] + cycle];
                                    }
                                }
                            }
                            if (condition && window_data.curr_dim[1] != window_data.max_dim[1]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[1]--;
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1]++;
                            }
                        }
                        if (an_input == "Down Arrow is pressed" || an_input == "Down Arrow is held") {
                            if (window_data.curr_dim[1] != window_data.min_dim[1]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[1]++;
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1]--;
                            }
                        }
                    } else if (normal_number == 2) {
                        if (an_input == "Up Arrow is pressed" || an_input == "Up Arrow is held") {
                            if (window_data.curr_dim[1] != window_data.min_dim[1]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1]--;
                            }
                        }
                        if (an_input == "Down Arrow is pressed" || an_input == "Down Arrow is held") {
                            bool condition = window_data.pos[1] + window_data.curr_dim[1] + 3 == screen_height;
                            if (!condition) {
                                condition = window_data.pos[1] + window_data.curr_dim[1] + 2 < screen_height;
                                for (int cycle = 0; cycle < window_data.curr_dim[0] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + window_data.curr_dim[1] + 3][window_data.pos[0] + cycle];
                                    }
                                }
                            }
                            if (condition && window_data.curr_dim[1] != window_data.max_dim[1]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1]++;
                            }
                        }
                    } else if (normal_number == 3) {
                        if (an_input == "Left Arrow is pressed" || an_input == "Left Arrow is held") {
                            bool condition = window_data.pos[0] == 1;
                            if (!condition) {
                                condition = window_data.pos[0] > 0;
                                for (int cycle = 0; cycle < window_data.curr_dim[1] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + cycle][window_data.pos[0]-2];
                                    }
                                }
                            }
                            if (condition && window_data.curr_dim[0] != window_data.max_dim[0]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[0]--;
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0]++;
                            }
                        }
                        if (an_input == "Right Arrow is pressed" || an_input == "Right Arrow is held") {
                            if (window_data.curr_dim[0] != window_data.min_dim[0]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[0]++;
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0]--;
                            }
                        }
                    } else if (normal_number == 4) {
                        if (an_input == "Left Arrow is pressed" || an_input == "Left Arrow is held") {
                            if (window_data.curr_dim[0] != window_data.min_dim[0]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0]--;
                            }
                        }
                        if (an_input == "Right Arrow is pressed" || an_input == "Right Arrow is held") {
                            bool condition = window_data.pos[0] + window_data.curr_dim[0] + 3 == screen_width;
                            if (!condition) {
                                condition = window_data.pos[0] + window_data.curr_dim[0] + 2 < screen_width;
                                for (int cycle = 0; cycle < window_data.curr_dim[1] + 2; cycle++) {
                                    if (condition) {
                                        condition = !occupance_screen[window_data.pos[1] + cycle][window_data.pos[0] + window_data.curr_dim[0] + 3];
                                    }
                                }
                            }
                            if (condition && window_data.curr_dim[0] != window_data.max_dim[0]) {
                                (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0]++;
                            }
                        }
                    }
                }
                
                if (an_input == "Enter is pressed" && nwih == 1 && window_focused.first.load() == -1 && window_focused.second.load() == -1 && window_in_hand != null_window) {
                    window_focused = window_in_hand;
                }
                
                if (an_input == "` is pressed" && window_focused.first.load() == -1 && window_focused.second.load() == -1 && nwih == 1) {
                    nwih = 2;
                    normal_number = -1;
                    window_in_hand = null_window;
                    window_focused = null_window;
                    held_index = -1;
                } else if (an_input == "` is pressed" && window_focused.first.load() != -1 && window_focused.second.load() != -1) {
                    window_focused = null_window;
                    normal_number = -1;
                    nwih = 1;
                }
                
                if (an_input == "Shift is pressed" && window_focused.first.load() == -1 && window_focused.second.load() == -1 && window_in_hand != null_window && nwih == 1) {
                    shown_windows.erase(find(shown_windows.begin(), shown_windows.end(), window_in_hand));
                    hidden_windows.insert(hidden_windows.begin(), window_in_hand);
                    nwih = 2;
                    normal_number = -1;
                } else if (an_input == "Shift is pressed" && nwih == 2 && window_in_hand != null_window) {
                    int curr_dim_int[2] = {(*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0].load(),
                                            (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1].load()};
                    held_options = unhiding_options((bool*) &occupance_screen, screen_width, screen_height, curr_dim_int);
                    if (held_options.size() != 0) {
                        nwih = 3;
                        held_index = 0;
                    } else {
                        api.gen_notification("too cluttered");
                    }
                }
                
                if (alphabet != "" && nwih == 2) {
                    if (hidden_windows.size() != 0) {
                        int alpha_index = distance(alphabets.begin(), find(alphabets.begin(), alphabets.end(), alphabet[0]));
                        if (alpha_index < 5) {
                            int index_in_hand;
                            if (find(hidden_windows.begin(), hidden_windows.end(), window_in_hand) != hidden_windows.end()) {
                                index_in_hand = distance(hidden_windows.begin(), find(hidden_windows.begin(), hidden_windows.end(), window_in_hand));
                            } else {
                                index_in_hand = 0;
                            }
                            int frame_start = (index_in_hand / 5) * 5;
                            if (frame_start + alpha_index < hidden_windows.size()) {
                                window_in_hand = hidden_windows[frame_start + alpha_index];
                            }
                        }
                    }
                }
                
                if (nwih == 2 && window_in_hand != null_window) {
                    auto index_of_current = distance(hidden_windows.begin(), find(hidden_windows.begin(), hidden_windows.end(), window_in_hand));
                    if (an_input == "Up Arrow is pressed") {
                        if (index_of_current != 0) {
                            window_in_hand = hidden_windows[index_of_current - 1];
                        } else {
                            window_in_hand = hidden_windows[hidden_windows.size()-1];
                        }
                    }
                    if (an_input == "Down Arrow is pressed") {
                        if (index_of_current != hidden_windows.size()-1) {
                            window_in_hand = hidden_windows[index_of_current + 1];
                        } else {
                            window_in_hand = hidden_windows[0];
                        }
                    }
                }
                
                if (nwih == 3) {
                    if (an_input == "Up Arrow is pressed") {
                        if (held_index != 0) {
                            held_index--;
                        } else {
                            held_index = held_options.size()-1;
                        }
                    }
                    if (an_input == "Down Arrow is pressed") {
                        if (held_index != held_options.size()-1) {
                            held_index++;
                        } else {
                            held_index = 0;
                        }
                    }
                }
                
                if (an_input == "Enter is pressed" && nwih == 3) {
                    auto the_option = held_options[held_index];
                    the_option[2] -= 2;
                    the_option[3] -= 2;
                    (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[0] = the_option[0];
                    (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].pos[1] = the_option[1];
                    if ((*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].max_dim[0] < the_option[2]) {
                        (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0] = (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].max_dim[0];
                    } else {
                        (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[0] = the_option[2];
                    }
                    if ((*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].max_dim[1] < the_option[3]) {
                        (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1] = (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].max_dim[1];
                    } else {
                        (*all_window_groups.get_ptr(key))[window_in_hand.first].the_windows[window_in_hand.second].curr_dim[1] = the_option[3];
                    }

                    shown_windows.insert(shown_windows.begin(), window_in_hand);
                    hidden_windows.erase(find(hidden_windows.begin(), hidden_windows.end(), window_in_hand));
                    window_focused = null_window;
                    held_index = -1;
                    normal_number = -1;
                    nwih = 1;
                }

            } else if (mode == "notifications") {

                if (an_input == "Tab is pressed" && nc == 0 && notifications.size() > 0) {
                    nc = 1;
                    notif_index = notifications.size()-1;
                } else if (an_input == "Tab is pressed" && nc == 1) {
                    nc = 0;
                    if (notifications.size() > 0) {
                        notif_index = notifications.size()-1;
                    }
                } else if (nc == 1) {
                    if (an_input == "Up Arrow is pressed" && notif_index > 0) {
                        notif_index--;
                    }
                    if (an_input == "Down Arrow is pressed" && notif_index < notifications.size() - 1) {
                        notif_index++;
                    }
                    if (an_input == "Back Space is pressed") {
                        notifications.erase(notifications.begin() + notif_index);
                        if (notifications.size() == 0) {
                            nc = 0;
                            notif_index = notifications.size()-1;
                        }
                    }
                    if (an_input == "Enter is pressed") {
                        renderer.exitScreenMode();
                        cout << notifications[notif_index] + "\n\n Press Enter to Exit";
                        cin.clear();
                        cin.ignore(numeric_limits<streamsize>::max(), '\n');
                        cin.get();
                        renderer.enterScreenMode(nullptr);
                    }
                
                }

            } else if (mode == "plugins") {
                if (plugin_index != -1) {
                    if (an_input == "Up Arrow is pressed" && plugin_index > 0) {
                        plugin_index--;
                    }
                    if (an_input == "Down Arrow is pressed" && next(api.all_plugins_to_path_get_ptr(key)->begin(),plugin_index + 1) != api.all_plugins_to_path_get_ptr(key)->end()) {
                        plugin_index++;
                    }
                    if (an_input == "Enter is pressed") {
                        if (api.active_plugins_to_info_is_in(next(api.all_plugins_to_path_get_ptr(key)->begin(), plugin_index)->first, key)) {
                            make_unactive(api, next(api.all_plugins_to_path_get_ptr(key)->begin(), plugin_index)->first, 
                                api.active_plugins_to_info_get(next(api.all_plugins_to_path_get_ptr(key)->begin(), plugin_index)->first, key), all_threads, key, globalBindings, all_window_groups);
                        } else {
                            make_active(next(api.all_plugins_to_path_get_ptr(key)->begin(), plugin_index)->first, api, key, all_threads, thread_counter, globalBindings);
                        }
                    }
                    if (an_input == "Back Space is pressed") {
                        auto plugin_name = next(api.all_plugins_to_path_get_ptr(key)->begin(), plugin_index)->first;
                        if (api.active_plugins_to_info_is_in(plugin_name, key)) {
                            make_unactive(api, plugin_name, api.active_plugins_to_info_get(plugin_name, key), all_threads, key, globalBindings, all_window_groups);
                        }
                        auto plugin_path = (*api.all_plugins_to_path_get_ptr(key))[plugin_name];
                        bool is_from_cloud = plugin_path.length() > plugins_folder_path.length();
                        if (is_from_cloud) {
                            is_from_cloud = plugin_path.substr(0,plugins_folder_path.length()) == plugins_folder_path;
                        }
                        if (is_from_cloud) {
                            try {
                                fs::remove(plugin_path);
                            } catch (const fs::filesystem_error& err) {
                                api.gen_notification("Error deleting plugin file: \n");
                            }
                        }

                        api.all_plugins_to_path_get_ptr(key)->erase(plugin_name);
                        if (api.all_plugins_to_path_get_ptr(key)->size() != 0) {
                            if (plugin_index != 0) {
                                plugin_index--;
                            }
                        } else {
                            plugin_index = -1;
                        }
                    }
                }
            } else if (mode == "shop") {
                if (shop_index != -1) {
                    if (an_input == "Up Arrow is pressed" && shop_index > 0) {
                        shop_index--;
                    }
                    if (an_input == "Down Arrow is pressed" && shop_index < search_results.size()-1) {
                        shop_index++;
                    }
                    if (an_input == "Enter is pressed") {
                        renderer.exitScreenMode();
                        if (!fs::exists("plugins\\" + search_results[shop_index] + ".dll")) {
                            string command = getUserInput(search_results[shop_index] + "\n\nBy: " + search_data[shop_index][1] + "\n\n" + search_data[shop_index][2] + "\n\nEnter D/d to Download: ");
                            if (command == "d" || command == "D") {
                                cout << "\nDownloading...\n";
                                auto download_success = client.downloadPluginDll(search_data[shop_index][0], "plugins", search_results[shop_index] + ".dll");
                                if (download_success) {
                                    api.gen_notification("Plugin Downloaded Successfully");
                                    (*api.all_plugins_to_path_get_ptr(key))[search_results[shop_index]] = fs::canonical("plugins\\" + search_results[shop_index] + ".dll").string();
                                    cout << "Download was Successful";
                                } else {
                                    api.gen_notification("Plugin Download Failed");
                                    cout << "Download Failed";
                                }
                                cout << "\n\nPress Enter to Exit: ";
                                cin.clear();
                                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                                cin.get();
                            }
                        } else {
                            cout << search_results[shop_index] + "\n\nBy: " + search_data[shop_index][1] + "\n\n" + search_data[shop_index][2] + "\n\nAlready Downaloaded. Press Enter to Exit: ";
                            cin.clear();
                            cin.ignore(numeric_limits<streamsize>::max(), '\n');
                            cin.get();
                        }
                        renderer.enterScreenMode(&remem_to_delete_key);
                    }
                }
            } else if (mode == "dev") {
                if (digit != "") {
                    auto number = stoi(digit);
                    if (number > 0 && number < 6 && number != 3) {
                        renderer.exitScreenMode();
                        cout << "Enter \\e OR \\E at any moment to cancel\n\n";

                        if (digit == "1") { // signup
                            auto input_username = getUserInput("SIGNUP\n\nusername: ");
                            if (input_username != "\\e" && input_username != "\\E") {
                                if (input_username.size() <= 20 && input_username != "null" && input_username.find('\n') == string::npos && input_username.find('\t') == string::npos) {
                                    auto returned_pass = client.registerDeveloper(input_username);
                                    if (returned_pass != "") {
                                        cout << "\npassword: " + returned_pass;
                                        username = input_username;
                                        password = returned_pass;
                                    } else {
                                        cout << "\nSignUp Failed";
                                    }
                                } else {
                                    cout << "\nusername should be less than 21 characters and and cant be \"null\" and cant include new lines or tabs";
                                }
                                cout << "\n\nPress Enter to Exit: ";
                                cin.clear();
                                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                                cin.get();
                            }
                        } else if (digit == "2") { // login
                            auto input_username = getUserInput("LOGIN\n\nusername: ");
                            if (input_username != "\\e" && input_username != "\\E") {
                                auto input_password = getUserInput("\npassword: ");
                                if (input_password != "\\e" && input_password != "\\E") {
                                    auto creds_correct = client.verifyDeveloperCredentials(input_username,input_password);
                                    if (creds_correct) {
                                        cout << "\nLogin Successful";
                                    } else {
                                        cout << "\nLogin Failed";
                                    }
                                    cout << "\n\nPress Enter to Exit: ";
                                    cin.clear();
                                    cin.ignore(numeric_limits<streamsize>::max(), '\n');
                                    cin.get();
                                }
                            }
                        } else if (digit == "4") { // test
                            auto back_slash_e = false;
                            cout << username << endl;
                            cout << "Select a .dll file: ";
                            auto plugin_path = canon(SelectFile(L"Dynamic Link Libraries (*.dll)", L"*.dll"));
                            if (plugin_path != "") {
                                cout << plugin_path;
                                bool is_from_cloud = plugin_path.length() > plugins_folder_path.length();
                                if (is_from_cloud) {
                                    is_from_cloud = plugin_path.substr(0,plugins_folder_path.length()) == plugins_folder_path;
                                }
                                if (!is_from_cloud) {
                                    auto input_name = getUserInput("\n\nPlugin Name: test_");
                                    if (input_name != "\\e" && input_name != "\\E") {
                                        if (input_name.size() <= 35 && input_name.find('\n') == string::npos && input_name.find('\t') == string::npos) {
                                            input_name = "test_" + input_name;
                                            (*api.all_plugins_to_path_get_ptr(key))[input_name] = plugin_path;
                                        } else {
                                            cout << "\ncomplete name should be less than 40 characters and cant include new lines or tabs";;
                                        }
                                    } else {
                                        back_slash_e = true;
                                    }
                                } else {
                                    cout << "\n\nCant choose from the app's plugins folder";
                                }
                            } else {
                                cout << "Didnt Select a File";
                            }
                            
                            if (!back_slash_e) {
                                cout << "\n\nPress Enter to Exit: ";
                                cin.clear();
                                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                                cin.get();
                            }

                        } else if (digit == "5") { // upload
                             auto back_slash_e = false;
                            if (username != "null") {
                                cout << "Select a .zip file: ";
                                auto plugin_path = canon(SelectFile(L"ZIP Archives (*.zip)", L"*.zip"));
                                if (plugin_path != "") {
                                    cout << plugin_path;
                                    bool is_from_cloud = plugin_path.length() > plugins_folder_path.length();
                                    if (is_from_cloud) {
                                        is_from_cloud = plugin_path.substr(0,plugins_folder_path.length()) == plugins_folder_path;
                                    }
                                    if (!is_from_cloud) {
                                        auto input_name = getUserInput("\n\nPlugin Name: ");
                                        if (input_name != "\\e" && input_name != "\\E") {
                                            if (input_name.size() <= 40 && input_name.substr(0,5) != "test_" && input_name.find('\n') == string::npos && input_name.find('\t') == string::npos) {
                                                auto description = getUserInput("\nDescription: ");
                                                if (description != "\\e" && description != "\\E") {
                                                    bool uploaded_successfull = client.uploadPlugin(password, input_name, description, plugin_path);
                                                    if (uploaded_successfull) {
                                                        cout << "Upload Successful";
                                                    } else {
                                                        cout << "Upload Failed";
                                                    }
                                                } else {
                                                    back_slash_e = true;
                                                }
                                            } else {
                                                cout << "\ncomplete name should be less than 40 characters and and cant start with \"test_\" and cant include new lines or tabs";
                                            }
                                        } else {
                                            back_slash_e = true;
                                        }
                                    } else {
                                        cout << "\n\nCant choose from the app's plugins folder";
                                    }
                                } else {
                                    cout << "Didnt Select a File";
                                }
                            } else {
                                cout << "\nLogin First";    
                            }

                            if (!back_slash_e) {
                                cout << "\n\nPress Enter to Exit: ";
                                cin.clear();
                                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                                cin.get();
                            }
                        }

                        renderer.enterScreenMode(&remem_to_delete_key);
                    } else if (digit == "3") { // logout
                        username = "null";
                        password = "";
                        api.gen_notification("Logged Out");
                    }
                }
                
            }

            if (nwih == 0) {
                if (alphabet == "M") {
                    mode = "main";
                } else if (alphabet == "N") {
                    nc = 0;
                    notif_index = notifications.size()-1;
                    mode = "notifications";
                } else if (alphabet == "P") {
                    if (api.all_plugins_to_path_get_ptr(key)->empty()) {
                        plugin_index = -1;
                    } else {
                        plugin_index = 0;
                    }
                    mode = "plugins";
                } else if (alphabet == "S") {
                    renderer.exitScreenMode();
                    auto searched_string = getUserInput("Enter \\e OR \\E at any moment to cancel\n\nSearch Shop: ");
                    auto result_str = client.searchStore(searched_string);
                    search_results = {};
                    search_data = {};
                    if (result_str != "" && searched_string != "\\e" && searched_string != "\\E") {
                        json plugins = json::parse(result_str);
                        for (const auto& item : plugins) {
                            search_results.push_back(item["name"]);
                            search_data.push_back({item["id"], item["creator"], item["description"]});
                        }
                    }

                    renderer.enterScreenMode(&remem_to_delete_key);
                    if (search_results.size() > 0) {
                        shop_index = 0;
                    } else {
                        shop_index = -1;
                    }

                    if (searched_string != "\\e" && searched_string != "\\E") {
                        mode = "shop";
                    }
                } else if (alphabet == "D") {
                    mode = "dev";
                } else if (alphabet == "T") {
                    renderer.exitScreenMode();
                    cout << R"(TUTORIAL
                    
        The "-" before some texts, it specifies either that object being focused or being in a particular mode.
        The program starts on mode main_0, there are keys to switch between modes /  tabs
        Keys that switch between tab modes, wont work if in modes main_1-3

        Press M to switch to main_0
        Press Tab to switch from main_0 or main_2, to main_1
        Pressing Tab from any other main mode will take you to main_0
        Pressing Enter when having a window selected, focuses on it
        Pressing ` when focusing on a window exits the window
        Pressing ` when not focusing on a window switches to main_2
        Pressing Back Space having a window selected in any main mode, deletes it
        Pressing an alphabet on main_1 or main_2 helps navigate windows
        Pressing numbers 0-4 then using arrows allows moving and resizing windows
        Pressing Shift, having selected a window in main_0 hides it
        Use arrows too to navigate hidden windows
        Press Shift to show a window, use arrows to scroll through options and then press enter to show there

        Press N to switch to notif_0, you cannot interact with notifications in notif_0
        Press Tab to toggle between notif_0 and notif_1
        Use arrows to navigate notifications
        Press Enter to read full notification
        Press Back Space to delete notification

        Press P to switch to plugins mode
        Press Enter on a selected plugin to toggle it between enabled and disabled
        Press Back Space to delete it

        Press S to search in shop
        In the shop, use arrows to navigate
        Press Enter on a selected search result to interact and read about it

        Press D to switch to developers tab
        [Self Explanatory]

        -----------------

        For Devs:
        Plugins need to be downloaded and enabled in order to be used. Plugins need to be with the pluginAPI header file. Plugin Devs can test their plugins locally by compiling a dll. And they can upload their plugins to the cloud by compressing their plugin to a zip and uploading that. Uploading a plugin sends a gmail to the admin, to review and approve/reject plugins only after which it would be available to users.

        Press Esc to Exit Program
                    )";
                    cout << "\n\nPress Enter to Exit: ";
                    cin.clear();
                    cin.ignore(numeric_limits<streamsize>::max(), '\n');
                    cin.get();
                    renderer.enterScreenMode(&remem_to_delete_key);
                }
            }
        }

        notif_count = notifications.size() - notif_count;
        if (notif_count > 0) {
            notif_time = 30;
            notif_message = notifications[notifications.size()-1];
        }
        if (notif_time > 0) {
            notif_time--;
            local_screen[2][31] = '|';
            local_screen[3][31] = '|';
            local_screen[4][31] = '|';
            local_screen[2][screen_width-16] = '|';
            local_screen[3][screen_width-16] = '|';
            local_screen[4][screen_width-16] = '|';
            for (int x = 32; x < screen_width - 17; x++) {
                local_screen[1][x] = '_';
                local_screen[2][x] = ' ';
                local_screen[3][x] = ' ';
                local_screen[4][x] = '_';
            }
            string notif_message_preview;
            if (notif_message.find('\n') != string::npos) {
                notif_message_preview = notif_message.substr(0,notif_message.find('\n'));
            } else {
                notif_message_preview = notif_message;
            }
            if (notif_message_preview.size() > screen_width - 60) {
                notif_message_preview = notif_message_preview.substr(0,screen_width-62) + "..";
            }
            auto full_c_str = notif_message_preview.c_str();
            memcpy(&local_screen[3][33],full_c_str,notif_message_preview.length());
        }

        if (frame_no % 25 == 0) {
            // --- 1. Frame Setup & Time Calculation ---
            auto currentTime = std::chrono::high_resolution_clock::now();
            
            // Calculate the duration of the single frame in seconds (Delta Time)
            std::chrono::duration<double> frameDuration = currentTime - previousTime;
            previousTime = currentTime;

            double deltaTime = frameDuration.count();
            deltaTime /= 25.0;

            // Calculate instantaneous FPS for this specific loop iteration
            // Avoid division by zero on extremely fast loops
            int instantaneousFPS = (int) ((deltaTime > 0.0) ? (1.0 / deltaTime) : 0);
            string fps_str = to_string(instantaneousFPS);
            if (fps_str.length() == 1) {fps_str = "0" + fps_str;}
            fps_str = "fps: " + fps_str;
            if (frame_no != 0) {
                memcpy(fps_char,(const void*) fps_str.c_str(),7);
            }
        }
        if (mode == "main" && nwih == 0) {local_screen[screen_height-2][screen_width-10] = '-';}
        memcpy(&local_screen[screen_height-2][screen_width-8],fps_char,7*sizeof(char));

        renderer.present();

        memcpy(global_screen,local_screen,screen_width * screen_height * sizeof(char));

        this_thread::sleep_for(chrono::milliseconds(40));
        frame_no++;
        if (frame_no == 2000000000) {
            frame_no = 0;
        }
    }

    ofstream cookies_write("cookies.txt");
    string cookie_text;
    for (auto a_plugin : (*api.all_plugins_to_path_get_ptr(key))) {
        cookie_text += a_plugin.first + "\n" + a_plugin.second + "\n";
    }
    cookie_text += "\\e\n";
    for (auto an_active_plugin : api.active_plugins_to_info_all(key)) {
        cookie_text += an_active_plugin.first + "\n";
    }
    cookie_text = cookie_text.substr(0,cookie_text.length()-1);
    cookies_write << cookie_text;
    cookies_write.close();

    for (auto [plugin_name,plugin_info] : api.active_plugins_to_info_all(key)) {
        make_unactive(api, plugin_name, plugin_info, all_threads, key, globalBindings, all_window_groups);
    }
    
    renderer.exitScreenMode();
    return 0;
}
