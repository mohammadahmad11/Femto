#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>
#include <array>
#include <random>
#include <algorithm>

using namespace std;

random_device rd;
mt19937 gen(rd());
uniform_int_distribution<int> distrib(1, 2000000000);

// Define platform-specific DLL import/export macros
#ifdef BUILDING_PLUGIN_DLL
    #define PLUGIN_API __declspec(dllexport)
#else
    #define PLUGIN_API
#endif
#define NO_KEY_RETURN_VOID() if (main_key != in_main_key) return;
#define max_window_width 100
#define max_window_height 80
#define min_window_width 11
#define min_window_height 11

class femto {
    public:
        class CoreAPI;
        typedef void (*PluginUpdateFunc)(CoreAPI* api, int Windowgrouphandle);

    private:
        class Window {
            public:
                string name = "";
                char screen[max_window_height][max_window_width];
                int min_dim[2] = {min_window_width, min_window_height};
                int max_dim[2] = {max_window_width, max_window_height};;
                atomic<int> curr_dim[2];
                int pos[2];

                Window() {}

                Window(string in_Window_name, int in_min_dim[2], int in_max_dim[2]) {
                    if (in_min_dim[0] >= min_window_width && in_max_dim[0] <= max_window_width && in_min_dim[0] <= in_max_dim[0] &&
                        in_min_dim[1] >= min_window_height && in_max_dim[1] <= max_window_height && in_min_dim[1] <= in_max_dim[1]) {
                        memcpy(min_dim, in_min_dim,  sizeof(min_dim));
                        memcpy(max_dim, in_max_dim,  sizeof(max_dim));
                    }
                    if (in_Window_name.find('\n') == string::npos && in_Window_name.find('\t') == string::npos) {
                        name = in_Window_name;
                    }
                }

                Window(const Window& other) {
                    name = other.name;
                    memcpy(screen,other.screen, max_window_height*max_window_width*sizeof(char));
                    memcpy(min_dim,other.min_dim,2*sizeof(int));
                    memcpy(max_dim,other.max_dim,2*sizeof(int));
                    curr_dim[0].store(other.curr_dim[0].load());
                    curr_dim[1].store(other.curr_dim[1].load());
                    memcpy(pos,other.pos,2*sizeof(int));
                }

                // 2. Custom Copy Assignment Operator
                Window& operator=(const Window& other) {
                    if (this != &other) {
                        // Atomically load from 'other' and store into 'this'
                        name = other.name;
                        memcpy(screen,other.screen, max_window_height*max_window_width*sizeof(char));
                        memcpy(min_dim,other.min_dim,2*sizeof(int));
                        memcpy(max_dim,other.max_dim,2*sizeof(int));
                        curr_dim[0].store(other.curr_dim[0].load());
                        curr_dim[1].store(other.curr_dim[1].load());
                        memcpy(pos,other.pos,2*sizeof(int));
                    }
                    return *this;
                }
        };

        class window_group {
            public:
                map<int,Window> the_windows;
                int win_handle_counter = 0;
        };

        class ICommand {
            protected:
                void create_window_command_execute(int win_grp_handle, string name, int in_min_dim[2], int in_max_dim[2], int in_win_grps_key) {
                    if ((*api->all_window_groups.get_ptr(api->store_win_grps_key)).find(win_grp_handle) != (*api->all_window_groups.get_ptr(api->store_win_grps_key)).end()) {
                        if (api->window_num < 26) {
                            api->window_num++;
                            (*api->all_window_groups.get_ptr(api->store_win_grps_key))[win_grp_handle].win_handle_counter++;
                            (*api->all_window_groups.get_ptr(api->store_win_grps_key))[win_grp_handle].the_windows[(*api->all_window_groups.get_ptr(api->store_win_grps_key))[win_grp_handle].win_handle_counter] = Window(name, in_min_dim, in_max_dim);
                        } else {
                            api->gen_notification("too many windows");
                        }
                    }
                }
                void remove_window_command_execute(int in_win_grp_handle, int in_win_handle, int in_win_grps_key) {
                    if ((*api->all_window_groups.get_ptr(api->store_win_grps_key)).find(in_win_grp_handle) != (*api->all_window_groups.get_ptr(api->store_win_grps_key)).end()) {
                        if ((*api->all_window_groups.get_ptr(api->store_win_grps_key))[in_win_grp_handle].the_windows.find(in_win_handle) != (*api->all_window_groups.get_ptr(api->store_win_grps_key))[in_win_grp_handle].the_windows.end()) {
                            api->window_num--;
                            (*api->all_window_groups.get_ptr(api->store_win_grps_key))[in_win_grp_handle].the_windows.erase(in_win_handle);
                        }
                    }
                }
                void bind_ICommand(int in_win_grp_handle, string in_type, string in_message, void (*in_funcPtr)()) {
                    api->bindings.bind(in_win_grp_handle, in_type, in_message, in_funcPtr);
                }
                void free_bind_ICommand(int in_win_grp_handle, string in_type, string in_message) {
                    api->bindings.free_bind(in_win_grp_handle, in_type, in_message);
                }
                void clear_screen_command_execute(int in_win_grp_handle, int in_win_handle, int in_win_grps_key) {
                    if ((*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.find(in_win_handle) != (*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.end()) {
                        auto window_ptr = &((*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows[in_win_handle]);
                        for (int x = 0; x < max_window_width; x++) {
                            for (int y = 0; y < max_window_height; y++) {
                                window_ptr->screen[y][x] = ' ';
                            }
                        }
                    }
                }
                void draw_text_command_execute(int in_win_grp_handle, int in_win_handle, int in_win_grps_key, string in_text, int pos[2]) {
                    if (pos[0] < max_window_width && pos[1] < max_window_height && in_text.find('\t') == string::npos && 
                        (*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.find(in_win_handle) != (*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.end()) {
                        auto window_ptr = &((*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows[in_win_handle]);
                        int line_no = 0;
                        int col_no = 0;
                        for (char character : in_text) {
                            if (character == '\n') {
                                line_no++;
                                col_no = 0;
                            } else {
                                if (pos[1] + line_no < max_window_height && pos[0] + col_no < max_window_width) {
                                    window_ptr->screen[pos[1]+line_no][pos[0]+col_no] = character;
                                    col_no++;
                                }
                            }
                        }
                    }
                }
                void gen_notif_command_execute(string in_message) {
                    api->notifications.push_back(in_message);
                }
                void draw_object_command_execute(int in_win_grp_handle, int in_win_handle, int in_win_grps_key, int in_pos[2], char in_object[max_window_width][max_window_height], char in_clearing_symbol) {
                    if ((*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.find(in_win_handle) != (*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows.end()) {
                        auto window_ptr = &((*api->all_window_groups.get_ptr(in_win_grps_key))[in_win_grp_handle].the_windows[in_win_handle]);
                        for (int x = 0; x < max_window_width - in_pos[0]; x++) {
                            for (int y = 0; y < max_window_height - in_pos[1]; y++) {
                                if (in_object[x][y] != in_clearing_symbol && x+in_pos[0] < max_window_width && y+in_pos[1] < max_window_height) {
                                    window_ptr->screen[y+in_pos[1]][x+in_pos[0]] = in_object[x][y];
                                }
                            }
                        }
                    }
                }

            public:
                CoreAPI* api;
                string kind;
                virtual ~ICommand() = default;
                virtual void Execute() = 0;
                virtual string get_message() {return "";}
        };

    public:
        class CommandQueue {
            vector<unique_ptr<ICommand>> queue;
            mutex queueMutex;
            condition_variable cv;
            bool processingFinished = false;

            public:
                void PushCommand(std::unique_ptr<ICommand> commandId) {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    queue.push_back(std::move(commandId)); // Use std::move
                    processingFinished = false;
                }

                void WaitUntilFlushed() {
                    std::unique_lock<std::mutex> lock(queueMutex);
                    
                    // Sleep until the condition is met (Queue is empty AND main thread finished processing)
                    cv.wait(lock, [this]() { 
                        return queue.empty() && processingFinished; 
                    });
                }

                void FlushAndExecute(map<int,map<pair<string,string>,void (*)()>> gotten_bindings) {
                    std::vector<std::unique_ptr<ICommand>> localQueue;
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        if (queue.empty()) return;
                        localQueue = std::move(queue); // Move the whole vector
                    }
                    for (const auto& cmd : localQueue) { // Iterate by reference
                        if (cmd->kind == "system") {
                            cmd->Execute();
                        } else if (cmd->kind == "intercomm") {
                            pair<string,string> rep = {cmd->kind,cmd->get_message()};
                            for (auto [win_grp_handle,local_binds_map] : gotten_bindings) {
                                if (local_binds_map.find(rep) != local_binds_map.end()) {
                                    thread t(local_binds_map[rep]);
                                    t.detach();
                                }
                            }
                        }
                    }
                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        processingFinished = true;
                    }

                    cv.notify_all();
                }
            
        };

        class vector_of_window_groups {
            int win_grps_key;
            map<int,window_group> all_window_groups = {};
            public:            
                vector_of_window_groups(int in_win_grps_key) : win_grps_key(in_win_grps_key) {}
                map<int,window_group>* get_ptr(int in_win_grps_key) {
                    if (win_grps_key != in_win_grps_key) return nullptr;
                    return &all_window_groups;
                }

        };

        class APIBindings {
            int bindings_key;
            map<int,map<pair<string,string>,void (*)()>> the_bindings;

            public:
                APIBindings(int in_bindings_key) : bindings_key(in_bindings_key) {}

                void create_bind_group(int in_win_grp_handle, int in_bindings_key) {
                    if (bindings_key != in_bindings_key) return;
                    the_bindings[in_win_grp_handle] = {};
                }

                void remove_bind_group(int in_win_grp_handle, int in_bindings_key) {
                    if (bindings_key != in_bindings_key) return;
                    the_bindings.erase(in_win_grp_handle);
                }

                void bind(int in_win_grp_handle, string kind, string message, void (*funcPtr)()) {
                    if (kind != "system") {
                        the_bindings[in_win_grp_handle][{kind,message}] = funcPtr;
                    }
                }

                void free_bind(int in_win_grp_handle, string kind, string message) {
                    if (the_bindings[in_win_grp_handle].find({kind,message}) != the_bindings[in_win_grp_handle].end()) {
                        the_bindings[in_win_grp_handle].erase({kind,message});
                    }
                }              
                
                map<int,map<pair<string,string>,void (*)()>> get(int in_bindings_key) {
                    return the_bindings;
                }
        };

        class CreateBindingCommand : public ICommand {
            int win_grp_handle;
            string type;
            string message;
            void (*funcPtr)();
            public:
            CreateBindingCommand(int in_win_grp_handle, string in_type, string in_message, void (*in_funcPtr)(), CoreAPI* in_api) : win_grp_handle(in_win_grp_handle), type(in_type), message(in_message), funcPtr(in_funcPtr) {api = in_api; kind = "system";}
            void Execute() override {
                bind_ICommand(win_grp_handle,type,message,funcPtr);
            }
        };

        class FreeBindingCommand : public ICommand {
            int win_grp_handle;
            string type;
            string message;
            public:
            FreeBindingCommand(int in_win_grp_handle, string in_type, string in_message, CoreAPI* in_api) : win_grp_handle(in_win_grp_handle), type(in_type), message(in_message) {api = in_api; kind = "system";}
            void Execute() override {
                free_bind_ICommand(win_grp_handle,type,message);
            }
        };

        class CustomCommand : public ICommand {
            string message;
            public:
                CustomCommand(string in_message) : message(in_message) {kind = "intercomm";}
                void Execute() override {}
                string get_message() override {return message;}
        };

        class CreateWindowCommand : public ICommand {
            int win_grp_handle;
            string name;
            int min_dims[2];
            int max_dims[2];
            int win_grps_key;
            public:
                CreateWindowCommand(int in_win_grp_handle, string in_name, int in_min_dims[2], int in_max_dims[2], CoreAPI* in_api, int in_win_grps_key) : win_grp_handle(in_win_grp_handle), name(in_name), win_grps_key(in_win_grps_key) {api = in_api; kind = "system"; memcpy(min_dims,in_min_dims,sizeof(min_dims)); memcpy(max_dims,in_max_dims,sizeof(max_dims));}
                void Execute() override {
                    create_window_command_execute(win_grp_handle,name,min_dims,max_dims,win_grps_key);
                }
        };

        class Remove_WindowCommand : public ICommand {
            int win_grp_handle;
            int win_handle;
            int win_grps_key;
            public:
                Remove_WindowCommand(int in_win_grp_handle, int in_win_handle, CoreAPI* in_api, int in_win_grps_key) : win_grp_handle(in_win_grp_handle), win_handle(in_win_handle), win_grps_key(in_win_grps_key) {api = in_api; kind = "system";}
                void Execute() override {
                    remove_window_command_execute(win_grp_handle,win_handle,win_grps_key);
                }
        };

        class ClearScreenCommand : public ICommand {
            int win_grp_handle;
            int win_handle;
            int win_grps_key;
            public:
                ClearScreenCommand(int in_win_grp_handle, int in_win_handle, CoreAPI* in_api, int in_win_grps_key) : win_grp_handle(in_win_grp_handle), win_handle(in_win_handle), win_grps_key(in_win_grps_key) {api = in_api; kind = "system";}
                void Execute() override {
                    clear_screen_command_execute(win_grp_handle, win_handle, win_grps_key);
                }
        };

        class DrawTextCommand : public ICommand {
            int win_grp_handle;
            int win_handle;
            int win_grps_key;
            string text;
            int pos[2];
            public:
                DrawTextCommand(int in_win_grp_handle, int in_win_handle, CoreAPI* in_api, int in_win_grps_key, string in_text, int in_pos[2]) : win_grp_handle(in_win_grp_handle), win_handle(in_win_handle), win_grps_key(in_win_grps_key), text(in_text) {api = in_api; kind = "system"; pos[0] = in_pos[0]; pos[1] = in_pos[1];}
                void Execute() override {
                    draw_text_command_execute(win_grp_handle, win_handle, win_grps_key, text, pos);
                }
        };

        class DrawObjectCommand : public ICommand {
            int win_grp_handle;
            int win_handle;
            int win_grps_key;
            int pos[2];
            char object[max_window_width][max_window_height];
            char clearing_symbol;
            public:
                DrawObjectCommand(int in_win_grp_handle, int in_win_handle, CoreAPI* in_api, int in_win_grps_key, int in_pos[2], char in_object[max_window_width][max_window_height], char in_clearing_symbol) : win_grp_handle(in_win_grp_handle), win_handle(in_win_handle), win_grps_key(in_win_grps_key), clearing_symbol(in_clearing_symbol) {api = in_api; kind = "system";
                    pos[0] = in_pos[0];
                    pos[1] = in_pos[1];
                    memcpy(object,in_object,max_window_width * max_window_height * sizeof(char));
                }
                void Execute() override {
                    draw_object_command_execute(win_grp_handle, win_handle, win_grps_key, pos, object, clearing_symbol);
                }
        };

        class GenNotifCommand : public ICommand {
            string message;
            public:
                GenNotifCommand(string in_message, CoreAPI* in_api) : message(in_message) {kind = "system"; api = in_api;}
                void Execute() override {
                    gen_notif_command_execute(message);
                }
        };

        class plugin_info {
            public:
                void* handle;
                PluginUpdateFunc the_plugin_func;
                int win_grp_handle;
                int thread_index;
                plugin_info() {}
                plugin_info(void* in_handle, PluginUpdateFunc in_the_plugin_func, int in_win_grp_handle, int in_thread_index) {
                    handle = in_handle;
                    the_plugin_func = in_the_plugin_func;
                    win_grp_handle = in_win_grp_handle;
                    thread_index = in_thread_index;
                }
        };

        class CoreAPI {

            int main_key;
            CommandQueue& cmdQueue;
            APIBindings& bindings;
            vector_of_window_groups& all_window_groups;
            int window_num = 0;
            int store_win_grps_key;
            map<int,atomic<bool>> isRunning;
            vector<string>& notifications;
            pair<atomic<int>,atomic<int>>& window_focused;
            friend class ICommand;

            map<string,string> all_plugins_to_path;
            map<string,plugin_info> active_plugins_to_info;
            vector<int> win_grp_handles_so_far = {};

            public:
                CoreAPI(CommandQueue& q, APIBindings& in_bindings, vector_of_window_groups& in_all_window_groups, vector<string>& in_notifications, int in_win_grps_key, int in_main_key, pair<atomic<int>,atomic<int>>& in_window_focused) : cmdQueue(q), bindings(in_bindings), all_window_groups(in_all_window_groups), notifications(in_notifications), store_win_grps_key(in_win_grps_key), main_key(in_main_key), window_focused(in_window_focused) {}

                bool IsRunning(int plugin_id) const {
                    return isRunning.at(plugin_id).load(memory_order_relaxed);
                }
                void set_is_running(int in_win_grp_handle, bool value, int in_main_key) {
                    NO_KEY_RETURN_VOID();
                    isRunning[in_win_grp_handle].store(value, memory_order_relaxed);
                }
                
                map<string,string>* all_plugins_to_path_get_ptr(int in_main_key) {
                    if (main_key != in_main_key) return nullptr;
                    return &all_plugins_to_path;
                }

                void active_plugins_to_info_assign(string in_plugin_name, void* in_handle, PluginUpdateFunc in_the_plugin_func, int in_win_grp_handle, int thread_index, int in_main_key) {
                    NO_KEY_RETURN_VOID();
                    active_plugins_to_info[in_plugin_name] = plugin_info(in_handle, in_the_plugin_func, in_win_grp_handle, thread_index);
                }
                void active_plugins_to_info_remove(string in_plugin_name, int in_main_key) {
                    NO_KEY_RETURN_VOID();
                    active_plugins_to_info.erase(in_plugin_name);
                }
                bool active_plugins_to_info_is_in(string in_plugin_name, int in_main_key) {
                    if (main_key != in_main_key) return false;
                    return active_plugins_to_info.find(in_plugin_name) != active_plugins_to_info.end();
                }
                plugin_info active_plugins_to_info_get(string in_plugin_name, int in_main_key) {
                    if (main_key != in_main_key) return plugin_info(nullptr,nullptr,0,0);
                    return active_plugins_to_info[in_plugin_name];
                }
                map<string,plugin_info> active_plugins_to_info_all(int in_main_key) {
                    if (main_key != in_main_key) return {};
                    return active_plugins_to_info;
                }
                int* get_window_num_ptr(int in_main_key) {
                    if (main_key != in_main_key) {return nullptr;}
                    return &window_num;
                }

                int create_window_group(int in_main_key) {
                    if (main_key != in_main_key) return 0;
                    int chosen_win_grp_handle = distrib(gen);
                    while (find(win_grp_handles_so_far.begin(), win_grp_handles_so_far.end(), chosen_win_grp_handle) != win_grp_handles_so_far.end()) {
                        chosen_win_grp_handle = distrib(gen);
                    }
                    (*all_window_groups.get_ptr(store_win_grps_key))[chosen_win_grp_handle] = {};
                    return chosen_win_grp_handle;
                }
                void remove_window_group(int in_win_grp_handle, int in_main_key) {
                    NO_KEY_RETURN_VOID();
                    (*all_window_groups.get_ptr(store_win_grps_key)).erase(in_win_grp_handle);
                    win_grp_handles_so_far.erase(find(win_grp_handles_so_far.begin(), win_grp_handles_so_far.end(), in_win_grp_handle));
                }







                void gen_notification(string message) {
                    cmdQueue.PushCommand(make_unique<GenNotifCommand>(message, this));
                }

                void gen_intercomm(string message) {
                    cmdQueue.PushCommand(make_unique<CustomCommand>(message));
                }

                int get_latest_win_handle(int plugin_id) {
                    cmdQueue.WaitUntilFlushed();
                    return (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].win_handle_counter;
                }

                void bind(int plugin_id, string type, string message, void (*funcPtr)()) {
                    cmdQueue.PushCommand(make_unique<CreateBindingCommand>(plugin_id, type, message, funcPtr, this));
                }

                void free_bind(int plugin_id, string type, string message) {
                    cmdQueue.PushCommand(make_unique<FreeBindingCommand>(plugin_id, type, message, this));
                }

                void create_window(int plugin_id, string in_name, int in_min_dims[2], int in_max_dims[2]) {
                    cmdQueue.PushCommand(make_unique<CreateWindowCommand>(plugin_id, in_name, in_min_dims, in_max_dims, this, store_win_grps_key));
                }
                void create_window(int plugin_id, string in_name) {
                    int in_min_dims[2] = {min_window_width, min_window_height};
                    int in_max_dims[2] = {max_window_width, max_window_height};
                    cmdQueue.PushCommand(make_unique<CreateWindowCommand>(plugin_id, in_name, in_min_dims, in_max_dims, this, store_win_grps_key));
                }

                void remove_window(int plugin_id, int in_win_handle) {
                    cmdQueue.PushCommand(make_unique<Remove_WindowCommand>(plugin_id, in_win_handle, this, store_win_grps_key));
                }
        
                void clear_screen(int plugin_id, int in_win_handle) {
                    cmdQueue.PushCommand(make_unique<ClearScreenCommand>(plugin_id, in_win_handle, this, store_win_grps_key));
                }

                array<array<char,max_window_width>,max_window_height> get_screen(int plugin_id, int in_win_handle) {
                    cmdQueue.WaitUntilFlushed();
                    array<array<char,max_window_width>,max_window_height> returnable;
                    if ((*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.find(in_win_handle) != (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.end()) {
                        for (int y = 0; y < max_window_height; y++) {
                            copy(begin((*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows[in_win_handle].screen[y]),
                                   end((*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows[in_win_handle].screen[y]),
                                returnable[y].begin());
                        }
                    }
                    return returnable;
                }

                pair<int,int> get_curr_dim(int plugin_id, int in_win_handle) {
                    cmdQueue.WaitUntilFlushed();
                    Window window_data;
                    if ((*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.find(in_win_handle) != (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.end()) {
                        window_data = (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows[in_win_handle];
                        return make_pair(window_data.curr_dim[0].load(), window_data.curr_dim[1].load());
                    } else {
                        return make_pair(-1,-1);
                    }
                }

                void draw_text(int plugin_id, int in_win_handle, string text, int pos[2]) {
                    cmdQueue.PushCommand(make_unique<DrawTextCommand>(plugin_id, in_win_handle, this, store_win_grps_key, text, pos));
                }

                void draw_object(int plugin_id, int in_win_handle, int pos[2], char object[max_window_width][max_window_height], char in_clearing_symbol) {
                    cmdQueue.PushCommand(make_unique<DrawObjectCommand>(plugin_id, in_win_handle, this, store_win_grps_key, pos, object, in_clearing_symbol));
                }

                string get_name(int plugin_id, int in_win_handle) {
                    cmdQueue.WaitUntilFlushed();
                    Window window_data;
                    if ((*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.find(in_win_handle) != (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows.end()) {
                        window_data = (*all_window_groups.get_ptr(store_win_grps_key))[plugin_id].the_windows[in_win_handle];
                        return window_data.name;
                    } else {
                        return "";
                    }
                }

                pair<int,int> get_window_focused(int plugin_id) {
                    cmdQueue.WaitUntilFlushed();
                    if (window_focused.first == plugin_id) {
                        return make_pair(window_focused.first.load(), window_focused.second.load());
                    } else {
                        return make_pair(-1,-1);
                    }
                }

        };
};