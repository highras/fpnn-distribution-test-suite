#include <iostream>
#include <unistd.h>
#include <sys/sysinfo.h>
#include "ServerInfo.h"
#include "ignoreSignals.h"
#include "FileSystemUtil.h"
#include "CommandLineUtil.h"
#include "TCPClient.h"
#include "DeployQuestProcessor.h"

using namespace std;
using namespace fpnn;

/*
	Usage:
		DATDeployer -h endpoint [-d cache_folder]
*/

class Deployer
{
	std::mutex _mutex;
	TCPClientPtr _client;
	std::string _region;
	std::string _cachePath;

	std::shared_ptr<DeployQuestProcessor> _processor;

	void loadActorCache(std::vector<std::vector<std::string>>& rows);

public:
	bool init(const std::string& endpoint, const std::string& cachePath)
	{
		_region = ServerInfo::getServerRegionName();
		_client = TCPClient::createClient(endpoint);
		if (!_client)
			return false;

		_processor = std::make_shared<DeployQuestProcessor>(cachePath);
		_cachePath = _processor->cachePath();
		_client->setQuestProcessor(_processor);

		return true;
	}

	void check()
	{
		_processor->checkUploadTimeout();

		if (!_client->connected())
		{
			_client->connect();
			registerDeployer();
		}
	}
	void addNewActor(const std::string& name, const std::string& tmpPath);
	void registerDeployer();
};

const std::vector<std::string> RegisterFields{"actor", "size", "md5", "mtime"};

void Deployer::loadActorCache(std::vector<std::vector<std::string>>& rows)
{
	std::unique_lock<std::mutex> lck(_mutex);
	std::vector<std::string> cachedFiles = FileSystemUtil::getFilesInDirectory(_cachePath.c_str());
	for (auto& filename: cachedFiles)
	{
		if (filename[0] == '.')
			continue;

		FileSystemUtil::FileAttrs attrs;
		std::string fullname = _cachePath + "/" + filename;
		if (FileSystemUtil::readFileAndAttrs(fullname, attrs) == false)
		{
			cout<<"[Error] Load actor "<<filename<<" failed."<<endl;
			continue;
		}

		rows.push_back(std::vector<std::string>());
		size_t idx = rows.size() - 1;

		rows[idx].push_back(filename);
		rows[idx].push_back(std::to_string(attrs.size));
		rows[idx].push_back(attrs.sign);
		rows[idx].push_back(std::to_string(attrs.mtime));
	}
}

void Deployer::addNewActor(const std::string& name, const std::string& tmpPath)
{
	std::string fullname = _cachePath + "/" + name;

	std::string systemCmd("mv -f ");
	systemCmd.append(tmpPath).append(" ").append(fullname);

	{
		std::unique_lock<std::mutex> lck(_mutex);
		int rc = system(systemCmd.c_str());
		if (rc != 0)
			cout<<"[Error] Move new actor "<<name<<" from "<<tmpPath<<" into "<<fullname<<" failed. System returns code: "<<rc<<endl;
	}
}

void Deployer::registerDeployer()
{
	std::vector<std::vector<std::string>> rows;
	loadActorCache(rows);

	struct sysinfo info;
	sysinfo(&info);

	FPQWriter qw(5, "registerDeployer");
	qw.param("region", _region);
	qw.param("fields", RegisterFields);
	qw.param("rows", rows);
	qw.param("cpus", get_nprocs());
	qw.param("totalMemories", info.totalram);

	TCPClientPtr client = _client;
	_client->sendQuest(qw.take(), [client](FPAnswerPtr answer, int errorCode){
		if (errorCode != FPNN_EC_OK)
		{
			cout<<"[Error] Register deployer self exception. error code: "<<errorCode<<endl;
			client->close();
		}
	});
}

Deployer gc_Deployer;

void updateActorInfos(const std::string& name, const std::string& tmpPath)
{
	gc_Deployer.addNewActor(name, tmpPath);
	gc_Deployer.registerDeployer();
}

int main(int argc, const char* argv[])
{
	ignoreSignals();
	ClientEngine::configAnswerCallbackThreadPool(2, 1, 2, 4);
	ClientEngine::configQuestProcessThreadPool(0, 1, 2, 10, 0);

	CommandLineParser::init(argc, argv);
	std::string endpoint = CommandLineParser::getString("h");
	std::string cachePath = CommandLineParser::getString("d");

	std::string lockFile("/tmp/DATDeployer-");
	lockFile.append(endpoint);

	FileLocker locker(lockFile.c_str());
	if (!locker.locked())
	{
		cout<<"Other instance is running!"<<endl;
		return 0;
	}

	if (!gc_Deployer.init(endpoint, cachePath))
	{
		cout<<"Usage: ./DATDeployer -h endpoint [-d cache_folder]"<<endl;
		return -1;
	}

	while (true)
	{
		gc_Deployer.check();
		sleep(2);
	}

	return 0;
}