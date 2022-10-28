#include <iostream>
#include "TCPEpollServer.h"
#include "ControlCenterQuestProcessor.h"
#include "Setting.h"

using namespace std;

int main(int argc, char* argv[])
{
    try{
        if (argc != 2){
            cout<<"Usage: "<<argv[0]<<" config"<<endl;
            return 0;
        }
        if(!Setting::load(argv[1])){
            cout<<"Config file error:"<< argv[1]<<endl;
            return 1;
        }

        ServerPtr server = TCPEpollServer::create();
        server->setQuestProcessor(std::make_shared<ControlCenterQuestProcessor>());
        server->startup();
        server->run();
    }
    catch(const exception& ex){
        cout<<"exception:"<<ex.what()<<endl;
    }
    catch(...){
        cout<<"Unknow exception."<<endl;
    }

    return 0;
}