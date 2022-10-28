#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include "StringUtil.h"
#include "ClientEngine.h"
#include "FileSystemUtil.h"
#include "DeployQuestProcessor.h"

using namespace std;

UploadStatus::UploadStatus(const std::string& name_, int sectionCount, const std::string& cachePath):
	sectionLength(0), name(name_), token(false)
{
	tmpFilePath = cachePath;
	tmpFilePath.append("/_").append(std::to_string((uint64_t)this)).append("_").append(name);

	std::string emptyContent;
	for (int i = 0; i < sectionCount; i++)
		sections[i+1] = emptyContent;

	fd = open(tmpFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IXGRP | S_IROTH | S_IXOTH);
	if (fd == -1)
	{
		fd = 0;
		LOG_ERROR("Prepare to receive new actor %s at %s failed.", name_.c_str(), tmpFilePath.c_str());
	}

	activeSecs = slack_real_sec();
}

UploadStatus::~UploadStatus()
{
	if (fd)
		close(fd);

	if (sections.empty())
		updateActorInfos(name, tmpFilePath);
	else
	{
		cout<<"[Error] UploadActor "<<name<<" remain "<<sections.size()<<" section(s) uncompleted."<<endl;

		std::string cmd("rm -f ");
		cmd.append(tmpFilePath);

		system(cmd.c_str());
	}
}

void UploadStatus::fetchSectionToWrite(int& no, std::string& section, bool& allDone)
{
	no = -1;
	allDone = false;

	if (!sectionLength)
		return;

	if (sections.empty())
	{
		allDone = true;
		return;
	}

	for (auto& sp: sections)
	{
		if (sp.second.length())
		{
			section.swap(sp.second);
			no = sp.first;
			return;
		}
	}
}

void UploadInfo::wroteUploadSections(int taskId)
{
	int sectionNo;
	std::string section;
	bool allDone;
	UploadStatusPtr upload;

	{
		std::unique_lock<std::mutex> lck(_mutex);
		auto iter = _status.find(taskId);
			if (iter == _status.end())
				return;

		upload = _status[taskId];

		if (upload->token)
			return;

		upload->fetchSectionToWrite(sectionNo, section, allDone);
		if (section.empty() && !allDone)
			return;

		upload->token = true;
		upload->activeSecs = slack_real_sec();
	}

	while (true)
	{
		if (lseek(upload->fd, upload->sectionLength * (sectionNo-1), SEEK_SET) == -1)
		{
			std::unique_lock<std::mutex> lck(_mutex);
			_status.erase(taskId);
			return;
		}

		size_t remain = section.length();
		size_t offset = 0;
		while (remain > 0)
		{
			ssize_t bytes = write(upload->fd, section.data() + offset, remain);
			if (bytes < 0)
			{
				std::unique_lock<std::mutex> lck(_mutex);
				_status.erase(taskId);
				return;
			}
			remain -= (size_t)bytes;
			offset += (size_t)bytes;
		}


		std::unique_lock<std::mutex> lck(_mutex);
		upload->sections.erase(sectionNo);
		section.clear();

		upload->fetchSectionToWrite(sectionNo, section, allDone);
		if (allDone)
		{
			_status.erase(taskId);
			break;	//-- ensure outRelease/upload release after unlocked _mutex.
		}
	
		if (section.empty())
		{
			upload->token = false;
			return;
		}
	}
}

void UploadInfo::fillUploadSection(int taskId, const std::string& name, const std::string& cachePath, int sectionCount, int sectionNo, std::string& content)
{
	std::unique_lock<std::mutex> lck(_mutex);
	auto iter = _status.find(taskId);
	if (iter == _status.end())
		_status[taskId] = std::make_shared<UploadStatus>(name, sectionCount, cachePath);

	if (sectionNo == 1)
		_status[taskId]->sectionLength = (int)content.length();

	_status[taskId]->sections[sectionNo].swap(content);
	_status[taskId]->activeSecs = slack_real_sec();
}

void UploadInfo::checkUploadTimeout()
{
	const int64_t expiredSecond = 60;
	int64_t threshold = slack_real_sec() - expiredSecond;

	std::set<int> expireds;
	std::unique_lock<std::mutex> lck(_mutex);
	for (auto& pp: _status)
		if (pp.second->activeSecs <= threshold)
			expireds.insert(pp.first);
	
	for (int taskId: expireds)
		_status.erase(taskId);
}

void DeployQuestProcessor::prepareCachePath(const std::string& cachePath)
{
	_cachePath = cachePath.empty() ? "./cache" : cachePath;
	if (_cachePath[0] != '/')
	{
		std::string selfPath = FileSystemUtil::getSelfExectuedFilePath();
		selfPath.append("/").append(_cachePath);
		_cachePath.swap(selfPath);
	}

	if (FileSystemUtil::createDirectories(_cachePath.c_str()) == false)
		cout<<"[FATAL] Prepare actors cache folder "<<_cachePath<<" failed."<<endl;


	_tmpFileCachePath = _cachePath;
	_tmpFileCachePath.append("/tmp");

	if (FileSystemUtil::createDirectories(_tmpFileCachePath.c_str()) == false)
	{
		cout<<"[FATAL] Prepare actors temporary cache folder "<<_tmpFileCachePath<<" failed."<<endl;
		_tmpFileCachePath = "/tmp";
	}
}

class AsyncRecordActor: public ITaskThreadPool::ITask
{
	int _taskId;
	UploadInfoPtr _uploadInfos;

public:
	AsyncRecordActor(int taskId, UploadInfoPtr ui): _taskId(taskId), _uploadInfos(ui) {}
	virtual ~AsyncRecordActor()
	{
		_uploadInfos->wroteUploadSections(_taskId);
	}

	virtual void run() {}
};

FPAnswerPtr DeployQuestProcessor::deployActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string name = args->wantString("name");
	std::string section = args->wantString("section");
	int count = args->wantInt("count");
	int no = args->wantInt("no");
	int taskId = args->wantInt("taskId");

	_uploadInfos->fillUploadSection(taskId, name, _cachePath, count, no, section);
	ClientEngine::wakeUpQuestProcessThreadPool(std::make_shared<AsyncRecordActor>(taskId, _uploadInfos));

	return FPAWriter::emptyAnswer(quest);
}

class SystemCmds: public ITaskThreadPool::ITask
{
	size_t _idx;
	IAsyncAnswerPtr _async;
	std::vector<std::string> _cmds;

public:
	SystemCmds(IAsyncAnswerPtr async, std::vector<std::string>& cmds): _idx(0), _async(async)
	{
		_cmds.swap(cmds);
	}
	virtual ~SystemCmds()
	{
		if (_idx == _cmds.size())
			_async->sendAnswer(FPAWriter(1, _async->getQuest())("ok", true));
		else
			_async->sendAnswer(FPAWriter(2, _async->getQuest())("ok", false)("failedLine", _idx));
	}

	virtual void run()
	{
		for (; _idx < _cmds.size(); _idx++)
			if (system(_cmds[_idx].c_str()) == -1)
				return;
	}
};

FPAnswerPtr DeployQuestProcessor::systemCmd(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<std::string> cmdLines = args->want("cmdLines", std::vector<std::string>());
	ClientEngine::wakeUpQuestProcessThreadPool(std::make_shared<SystemCmds>(genAsyncAnswer(quest), cmdLines));
	return nullptr;
}
FPAnswerPtr DeployQuestProcessor::launchActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string actor = args->wantString("actor");
	std::string cmdLine = args->getString("cmdLine");

	std::string path = _cachePath + "/" + actor;

	if (access(path.c_str(), X_OK) != 0)
		return FPAWriter(1, quest)("ok", false);

	if (cmdLine.size())
		path.append(" ").append(cmdLine);

	path.append(" &");
	system(path.c_str());

	return FPAWriter(1, quest)("ok", true);
}


int getConnNum(const char *protoLetter)
{
	std::ifstream fin("/proc/net/sockstat");
	if (fin.is_open()) {
		char line[1024];
		while(fin.getline(line, sizeof(line))){
			if (strncmp(protoLetter, line, 4))
				continue;

			std::string sLine(line);
			std::vector<std::string> items;
			StringUtil::split(sLine, " ", items);
			if(items.size() > 2)
			{
				fin.close();
				return stoi(items[2]);			// number of inused TCP connections
			}
		}
		fin.close();
	}
	return -1;
}

int getTCPConnNum()
{
	return getConnNum("TCP:");
}

int getUDPConnNum()
{
	return getConnNum("UDP:");
}

float getCPULoad()
{
	std::stringstream msgstr;
	std::ifstream fin("/proc/loadavg");
	if (fin.is_open()) {
		char line[1024];
		fin.getline(line, sizeof(line));
		std::string sLine(line);
		std::vector<std::string> items;
		StringUtil::split(sLine, " ", items);
		fin.close();
		try {
			float res = stof(items[0]);		// load average within 1 miniute
			return res;
		} catch(std::exception& e) {
			LOG_ERROR("failed to convert string to float, the string: %s", items[0].c_str());
			return -1;
		}
	}
	return -1;			// cannot open the file
}

void getNetworkStatus(uint64_t& recvBytes, uint64_t& sendBytes)
{
	recvBytes = 0;
	sendBytes = 0;

	std::ifstream fin("/proc/net/dev");
	if (fin.is_open())
	{
		char line[1024];
		while(fin.getline(line, sizeof(line)))
		{
			std::string sLine(line);
			std::vector<std::string> items;
			StringUtil::split(sLine, " ", items);

			if (items.empty())
				continue;

			if (strncmp("eth", items[0].c_str(), 3) == 0
				|| strncmp("ens", items[0].c_str(), 3) == 0
				|| strncmp("eno", items[0].c_str(), 3) == 0
				|| strncmp("enp", items[0].c_str(), 3) == 0)
			{
				recvBytes += std::stoull(items[1]);
				sendBytes += std::stoull(items[9]);
			}
		}
		fin.close();
	}
}

FPAnswerPtr DeployQuestProcessor::machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	struct sysinfo info;
	sysinfo(&info);

	uint64_t recvBytes, sendBytes;
	getNetworkStatus(recvBytes, sendBytes);

	return FPAWriter(6, quest)("sysLoad", getCPULoad())("tcpConn", getTCPConnNum())("udpConn", getUDPConnNum())("freeMemories", info.freeram)("RX", recvBytes)("TX", sendBytes);
}