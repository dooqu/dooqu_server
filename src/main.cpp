#include <cstdio>
#include <iostream>
#include <map>
#include <cstring>
#include <dlfcn.h>
#include "tinyxml.h"
#include "dooqu_service.h"

using namespace std;

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

using namespace dooqu_service::service;
using namespace dooqu_service::util;

inline void enable_mem_leak_check()
{
#ifdef WIN32
    _CrtSetDbgFlag(_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) | _CRTDBG_LEAK_CHECK_DF);
#endif
}
typedef dooqu_service::service::game_plugin* (*create_plugin_func)(game_service* service,
                                      char* game_id,
                                      char* name,
                                      int frequence,
                                      int capacity,
                                      std::map<const char*, const char*, char_key_op>* configs);


class assembly_loader
{
protected:
    void* handle;
    char* assempath;

public:
    assembly_loader(const char* path)
    {
        assempath = new char[strlen(path) + 1];
        strcpy(assempath, path);
        assempath[strlen(path)] = 0;

        handle = NULL;
    }

    char* find(const char* func_name, void** func)
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

        *func = dlsym(handle, func_name);

        if ((result = dlerror()) != NULL)
        {
            return result;
        }

        return NULL;
    }


    const char* create_plugin(game_service* service,
                                 const char* id,
                                 const char* name,
                                 double frequence,
                                 int capacity,
                                 std::map<const char*, const char*, char_key_op>* configs, game_plugin** plugin)
    {
        void* func = NULL;

        char* result = this->find("create_plugin", &func);

        if(result != NULL)
        {
            return result;
        }

        create_plugin_func create_func = (create_plugin_func)func;

        if(create_func == NULL)
        {
            return "create plugin type error.";
        }


        char* nid = const_cast<char*>(id);
        char* nname = const_cast<char*>(name);

        *plugin = create_func(service, nid, nname, frequence, capacity, configs);

        return NULL;
    }



    virtual ~assembly_loader()
    {
        if(assempath != NULL)
        {
            delete [] assempath;
        }

        if(handle != NULL)
        {
            dlclose(handle);
            handle = NULL;
        }
    }
};


void load_plugins_from_configs(game_service* service, std::vector<game_plugin*>& plugins)
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
    TiXmlElement *currPluginEle = rootElement->FirstChildElement("plugin");


    while (currPluginEle != NULL)
    {
        const char* assemplaypath = currPluginEle->Attribute("assemblypath");

        assembly_loader loader(assemplaypath);        //

        TiXmlElement* currInstanceEle = currPluginEle->FirstChildElement("instance");

        while (currInstanceEle != NULL)
        {
            std::map<const char*, const char*, char_key_op> configs;
            const char* plugin_id = NULL;
            const char* plugin_name = NULL;
            int  frequence = 2000;
            int capacity = 300;
            TiXmlElement* currAttrEle = currInstanceEle->FirstChildElement();

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
                else if(strcmp(currAttrEle->Value(), "frequence") == 0)
                {
                    frequence = atoi(currAttrEle->GetText());
                }
                else if(strcmp(currAttrEle->Value(), "capacity") == 0)
                {
                    capacity = atoi(currAttrEle->GetText());
                }
                else
                {
                    configs[currAttrEle->Value()] = currAttrEle->GetText();
                }
                currAttrEle = currAttrEle->NextSiblingElement();
            }

            game_plugin* plugin = NULL;
            loader.create_plugin(service, plugin_id, plugin_name, frequence, capacity, &configs, &plugin);

            //delete plugin;
            //service->load_plugin(plugin, "zone0");
            if(plugin != NULL)
            plugins.push_back(plugin);
            currInstanceEle = currInstanceEle->NextSiblingElement("instance");
        }

        currPluginEle = currPluginEle->NextSiblingElement("plugin");
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

    {
        game_service service(8000);

        std::vector<game_plugin*> plugins;
        load_plugins_from_configs(&service, plugins);

        for(int i = 0;i < plugins.size(); i++)
        {
            service.load_plugin(plugins.at(i), "ddz_zone_0");
        }
        service.start();
        service.stop();
        service.start();


        std::cout << "please input the service command:" << endl;
        string read_line;
        std::getline(std::cin, read_line);


        while (std::strcmp(read_line.c_str(), "exit") != 0)
        {
            if (std::strcmp(read_line.c_str(), "onlines") == 0)
            {
                printf("current onlines is:\n");
                game_service::game_plugin_list* plugins = service.get_plugins();

                game_service::game_plugin_list::const_iterator curr_plugin = plugins->begin();

                int online_total = 0;
                while (curr_plugin != plugins->end())
                {
                    online_total += (*curr_plugin)->clients()->size();
                    printf("current game {%s}, onlines=%d\n", (*curr_plugin)->game_id(), (*curr_plugin)->clients()->size());
                    ++curr_plugin;
                }
                printf("all plugins online total=%d\n", online_total);
            }
            else if (std::strcmp(read_line.c_str(), "io") == 0)
            {
                printf("press any key to exit i/o monitor mode:\n");
                tcp_client::LOG_IO_DATA = true;
                getline(std::cin, read_line);
                std::cout << tcp_client::CURR_RECE_TOTAL << std::endl;
                tcp_client::LOG_IO_DATA = false;
                printf("i/o monitor is stoped.\n");
            }
            else if (std::strcmp(read_line.c_str(), "timers") == 0)
            {
                printf("press any key to exit timers monitor mode:\n");
                game_zone::LOG_TIMERS_INFO = true;
                getline(std::cin, read_line);
                game_zone::LOG_TIMERS_INFO = false;
                printf("timers monitor is stoped.\n");
            }
            else if (std::strcmp(read_line.c_str(), "thread") == 0)
            {
                for (thread_status_map::iterator curr_thread_pair = service.threads_status()->begin();
                        curr_thread_pair != service.threads_status()->end();
                        ++curr_thread_pair)
                {
                    std::cout << "thread id: "<< (*curr_thread_pair).first << " " <<   (*curr_thread_pair).second->elapsed() << std::endl;
                }
            }
            else if (std::strcmp(read_line.c_str(), "stop") == 0)
            {
                service.stop();
            }
            else if (std::strcmp(read_line.c_str(), "unload") == 0)
            {
                service.stop();
            }
            else if (std::strcmp(read_line.c_str(), "update") == 0)
            {
                using namespace dooqu_service::service;
//
                async_task task(service.get_io_service());
                tick_count* tick = new tick_count();
                test_time test;

                task.queue_task(std::bind(&test_time::test, &test, tick), 3000);
                getline(std::cin, read_line);

//                service.tick_count_threads();
//
//                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                //thread_lock_status thread_mutex_message;
                for (thread_lock_status::iterator e = thread_status::instance()->status()->begin();
                        e != thread_status::instance()->status()->end();
                        ++e)
                {
                    std::cout << "thread id " << (*e).first << " " << (*e).second<< std::endl;
                }
            }
            cin.clear();
            printf("please input the service command:\n");
            std::getline(std::cin, read_line);
        }

        if(service.is_running())
        {
            service.stop();
        }

        for(int i = 0; i < plugins.size(); i++)
        {
            delete plugins.at(i);
        }
    }

    delete thread_status::instance();
    printf("server object is recyled, press any key to exit.");
    getchar();

    return 0;
}


