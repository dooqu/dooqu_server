#include <cstdio>
#include <iostream>
#include <map>
#include <cstring>
#include <dlfcn.h>
#include "tinyxml.h"
#include "dooqu_service.h"

using namespace std;


std::map<std::thread::id, char*>* lock_status;
//
///*
//1、main thread: 1
//
//2、io_service 线程: cpu + 1
//
//3、io_service's timer: 1
//
//4、game_zone's io_service thread; zone num * 1;
//5、game_zone's io_service timer zone_num * 1;
//
//6、http_request 会动态启动线程，无法控制 += 2
//*/
//
//
using namespace dooqu_service::net;
using namespace dooqu_service::service;
using namespace dooqu_service::util;
using namespace dooqu_service::basic;

inline void enable_mem_leak_check()
{
#ifdef WIN32
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#endif
}

typedef std::shared_ptr<std::map<const char*, const char*, char_key_op>> data_config_ptr;

class assembly_loader
{
    typedef game_plugin* (*create_plugin_func)(ws_service* service,
            char* game_id,
            char* name,
            int capacity);
    typedef void (*destroy_plugin_func)(game_plugin*);
    typedef void (*set_thread_status_instance)(service_status* instance);

protected:
    void* handle;
    char* assempath;
    create_plugin_func create;
    destroy_plugin_func destroy;
    std::vector<game_plugin*> plugins;
    std::map<game_plugin*, data_config_ptr> plugin_data_configs;
public:
    assembly_loader(const char* path) : handle(NULL), assempath(NULL), create(NULL), destroy(NULL)
    {
        assempath = new char[strlen(path) + 1];
        strcpy(assempath, path);
        assempath[strlen(path)] = 0;
    }

    const std::vector<game_plugin*>* get_plugins() const
    {
        return &this->plugins;
    }

    char* get_assemply_path()
    {
        return assempath;
    }

    template<typename FUNC_TYPE>
    char* find(const char* func_name, FUNC_TYPE* func)
    {
        char* result = NULL;
        if(handle == NULL)
        {
            handle = dlopen(assempath, RTLD_NOW);
            if ((result = dlerror()) != NULL)
            {
                return result;
            }
        }

        void* vf = dlsym(handle, func_name);
        if ((result = dlerror()) != NULL)
        {
            return result;
        }
        *func = (FUNC_TYPE)vf;

        return NULL;
    }

    void update_thread_status_instance()
    {
        set_thread_status_instance set_instance = NULL;
        char* result = this->find<set_thread_status_instance>("set_thread_status_instance", &set_instance);
        if(result != NULL)
        {
            return;
        }

        if(set_instance != NULL)
        {
            set_instance(service_status::instance());
        }
    }

//    std::map<std::thread::id, char*>* get_lock_status()
//    {
//
//        get_status get_status_func = NULL;
//            char* result = this->find<get_status>("get_lock_status", &get_status_func);
//            if(result != NULL)
//            {
//                return NULL;
//            }
//
//        if(get_status_func != NULL)
//        {
//            return get_status_func(thread_status::create_new());
//        }
//    }


    const char* create_plugin(ws_service* service,
                              const char* id,
                              const char* name,
                              int capacity,
                              data_config_ptr configs)
    {
        if(this->create == NULL)
        {
            char* result = this->find<create_plugin_func>("create_plugin", &create);
            if(result != NULL)
            {
                return result;
            }
        }

        if(this->create == NULL)
        {
            return "can not find create_plugin function.";
        }

        char* nid = const_cast<char*>(id);
        char* nname = const_cast<char*>(name);

        game_plugin* plugin = this->create(service, nid, nname, capacity);
        plugin->config(*configs.get());

        if(plugin != NULL)
        {
            this->plugins.push_back(plugin);
        }

        this->plugin_data_configs[plugin] = configs;

        return NULL;
    }


    const char* destroy_plugin(game_plugin* plugin)
    {
        if(this->destroy == NULL)
        {
            char* result = this->find<destroy_plugin_func>("destroy_plugin", &this->destroy);
            if(result != NULL)
            {
                return result;
            }
        }

        if(this->destroy == NULL)
        {
            return "destroy plugin type error.";
        }

        this->plugin_data_configs.erase(plugin);
        (this->destroy)(plugin);

        return NULL;
    }

    virtual ~assembly_loader()
    {
        if(assempath != NULL)
        {
            delete [] assempath;
        }

        for(int i = 0; i < this->plugins.size(); i++)
        {
            this->plugin_data_configs.erase(this->plugins[i]);
            this->destroy_plugin(this->plugins[i]);
        }

        if(handle != NULL)
        {
            dlclose(handle);
            handle = NULL;
        }
    }
};


template<typename SOCK_TYPE>
void load_plugins_from_configs(game_service<SOCK_TYPE>* service, std::vector<assembly_loader*>& loaders)
{
    TiXmlDocument xmlDoc("plugins.config");

    if(xmlDoc.LoadFile() == false)
    {
        std::cout << "加载配置文件失败" << std::endl;
        return;
    }

    //获取跟目录
    TiXmlElement *rootElement = xmlDoc.RootElement();
    //获取第一个
    TiXmlElement *currZoneEle = rootElement->FirstChildElement("zone");

    while (currZoneEle != NULL)
    {
        const char* assemplaypath = currZoneEle->Attribute("assembly_path");

        assembly_loader* loader = new assembly_loader(assemplaypath);
        loaders.push_back(loader);
        loader->update_thread_status_instance();
        TiXmlElement* currPluginEle = currZoneEle->FirstChildElement("plugin");

        while (currPluginEle != NULL)
        {
            std::shared_ptr<std::map<const char*, const char*, char_key_op>> data_configs_ptr(new std::map<const char*, const char*, char_key_op>());
            const char* plugin_id = NULL;
            const char* plugin_name = NULL;
            std::map<const char*, const char*, char_key_op>* configs = data_configs_ptr.get();

            int capacity = 300;
            TiXmlElement* currAttrEle = currPluginEle->FirstChildElement();

            while (currAttrEle != NULL)
            {
                if(strcmp(currAttrEle->Value(), "id") == 0)
                {
                    plugin_id = currAttrEle->GetText();
                }
                else if(strcmp(currAttrEle->Value(), "name") == 0)
                {
                    plugin_name = currAttrEle->GetText();
                }
                else if(strcmp(currAttrEle->Value(), "capacity") == 0)
                {
                    capacity = atoi(currAttrEle->GetText());
                }
                else
                {
                    (*configs)[currAttrEle->Value()] = currAttrEle->GetText();
                }
                currAttrEle = currAttrEle->NextSiblingElement();
            }

            char* result = loader->create_plugin(service, plugin_id, plugin_name, capacity, data_configs_ptr);

            if(result != NULL)
            {
                std::cout << result << std::endl;
            }
            //lock_status = loader->get_lock_status();
            currPluginEle = currPluginEle->NextSiblingElement("plugin");
        }
        currZoneEle = currZoneEle->NextSiblingElement("zone");
    }
}

class test_time
{
public:
    void test(dooqu_service::util::tick_count* tick_cout)
    {
        std::cout << tick_cout->elapsed() << std::endl;
    }
};


int main(int argc, char* argv[])
{
    enable_mem_leak_check();
    service_status::create_new();
    service_status::instance()->init(std::this_thread::get_id());

    {
        game_service<dooqu_service::net::tcp_stream> service(8000);
        
        std::vector<assembly_loader*> loaders;
        load_plugins_from_configs(&service, loaders);

        for(int i = 0; i < loaders.size(); i++)
        {
            std::cout << i << std::endl;
            assembly_loader* loader = loaders[i];
            const std::vector<game_plugin*>* plugins_in_loader = loader->get_plugins();

            for(int n = 0; n < plugins_in_loader->size(); n++)
            {
                std::cout << "load plugin" << std::endl;
                service.load_plugin(plugins_in_loader->at(n), "ddz_zone_0");
            }
        }
        
        service.start();

        std::cout << "please input the service command:" << endl;
        string read_line;
        std::cout << "dooqu_server>";
        std::getline(std::cin, read_line);

        
        while (std::strcmp(read_line.c_str(), "exit") != 0)
        {
            if (std::strcmp(read_line.c_str(), "onlines") == 0)
            {
                game_service<ssl_stream>::game_plugin_list* plugins = service.get_plugins();
                 game_service<ssl_stream>::game_plugin_list::const_iterator curr_plugin = plugins->begin();

                int online_total = 0;
                while (curr_plugin != plugins->end())
                {
                    online_total += (*curr_plugin)->clients()->size();
                    printf("game_plugin: {%s}, onlines = %d\n", (*curr_plugin)->game_id(), (*curr_plugin)->clients()->size());
                    ++curr_plugin;
                }
                printf("total online = %d\n", online_total);
            }
            else if (std::strcmp(read_line.c_str(), "io") == 0)
            {
                    
            }
            else if (std::strcmp(read_line.c_str(), "timers") == 0)
            {
                printf("press any key to exit timers monitor mode:\n");
                service_status::instance()->echo_timers_status = true;
                getline(std::cin, read_line);
                service_status::instance()->echo_timers_status = false;
                printf("timers monitor is stoped.\n");
            }
            else if (std::strcmp(read_line.c_str(), "thread") == 0)
            {
                for (thread_status_map::iterator curr_thread_pair = service.threads_status()->begin();
                        curr_thread_pair != service.threads_status()->end();
                        ++curr_thread_pair)
                {
                    std::cout << "thread id: 0x"<< std::hex << (*curr_thread_pair).first << " " << std::dec << (*curr_thread_pair).second->elapsed() << std::endl;
                }
            }
            else if (std::strcmp(read_line.c_str(), "stop") == 0)
            {
                service.stop();
            }
            else if (std::strcmp(read_line.c_str(), "help") == 0)
            {
                std::cout << "[start] : start service." << std::endl;
                std::cout << "[stop] : stop service." << std::endl;
                std::cout << "[thread] : show all worker thread status." << std::endl;
                std::cout << "[onlines] : show every plugin's online status." << std::endl;
                std::cout << "[exit] : stop service and exit." << std::endl;
            }
            else if (std::strcmp(read_line.c_str(), "update") == 0)
            {
                using namespace dooqu_service::service;

                service.tick_count_threads();
                getline(std::cin, read_line);
                continue;

                thread_lock_stack::iterator e = service_status::instance()->lock_stack.begin();
                for(; e != service_status::instance()->lock_stack.end(); ++e)
                {
                    if((*e).first == std::this_thread::get_id())
                        continue;

                    std::deque<char*>* curr_thread_lock_stack = (*e).second;
                    std::cout << "thread id: " << (*e).first << "stack info:" << std::endl;
                    std::deque<char*>::iterator e2 = curr_thread_lock_stack->begin();
                    for(; e2 !=  curr_thread_lock_stack->end(); ++e2)
                    {
                        std::cout << " " << " " << "->" << (*e2) << std::endl;
                    }
                }
            }
            else  if (std::strcmp(read_line.c_str(), "http") == 0)
            {
                std::cout << "malloc:" << service.memory_malloc << ", free:" << service.memory_free << std::endl;
            }
            else
            {
                std::cout << "unkown command, \"help\" to see all available commands. " << std::endl;
            }
            cin.clear();
            printf("please input the service command:\n");
            std::cout << "dooqu_server>";
            std::getline(std::cin, read_line);
        }

        if(service.is_running())
        {
            service.stop();
        }

        for(int i = 0; i < loaders.size(); i++)
        {
            assembly_loader* loader = loaders[i];
            const std::vector<game_plugin*>* plugins_in_loader = loader->get_plugins();

            for(int n = 0; n < plugins_in_loader->size(); n++)
            {
                service.unload_plugin(plugins_in_loader->at(n));
            }
        }

        for(int i = 0, j = loaders.size(); i < j; i++)
        {
            delete loaders[i];
        }
    }

    service_status::destroy();
    return 0;
}



