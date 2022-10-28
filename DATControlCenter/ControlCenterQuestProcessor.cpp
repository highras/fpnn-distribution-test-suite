#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include "FPLog.h"
#include "FileSystemUtil.h"
#include "NetworkUtility.h"
#include "Setting.h"
#include "StringUtil.h"
#include "ChainBuffer.h"
#include "../DATErrorInfo.h"
#include "ControlCenterQuestProcessor.h"

const std::string gc_defaultActorDescFileName = ".actorDesc.txt";
const size_t gc_maxTransportLength = 2 * 1024 * 1024;
std::atomic<int> globalTaskIdGen(0);

std::map<std::string, int> buildIdxMap(const std::set<std::string>& hopeFields, const std::vector<std::string>& fileds)
{
	std::set<std::string> remain = hopeFields;
	std::map<std::string, int> idxmap;
	for (size_t i = 0; i < fileds.size(); i++)
	{
		idxmap[fileds[i]] = i;
		remain.erase(fileds[i]);
	}

	for (auto& lost: remain)
		idxmap[lost] = -1;

	return idxmap;
}

UploadInfo::UploadInfo(const std::string& name_, const std::string& desc_, int sectionCount, ControlCenterQuestProcessorPtr ccqp):
	name(name_), desc(desc_), sectionLength(0), CCQP(ccqp), token(false)
{
	tmpFilePath = ccqp->tmpFileCachePath();
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

	taskId = globalTaskIdGen++;
}

UploadInfo::~UploadInfo()
{
	if (fd)
		close(fd);
	
	FPQWriter qw(3, "uploadFinish");
	qw.param("taskId", taskId);
	qw.param("actor", name);

	if (sections.empty())
	{
		CCQP->addNewActor(name, desc, tmpFilePath);
		qw.param("ok", true);
	}
	else
	{
		qw.param("ok", false);
		LOG_ERROR("uploadActor %s remain %d section(s) uncompleted.", name.c_str(), (int)sections.size());

		std::string cmd("rm -f ");
		cmd.append(tmpFilePath);

		system(cmd.c_str());
	}

	sender->sendQuest(qw.take(), [](FPAnswerPtr answer, int errorCode){
		if (errorCode != FPNN_EC_OK && errorCode != FPNN_EC_CORE_CONNECTION_CLOSED)
			LOG_ERROR("Send uploadFinish ontify failed. Error code %d", errorCode);
	}, 0);
	CCQP.reset();
}

void UploadInfo::fetchSectionToWrite(int& no, std::string& section, bool& allDone)
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

void ConnectionPrivateData::registerRole(ClientRole role, const std::string& region, const std::string& endpoint)
{
	std::unique_lock<std::mutex> lck(_mutex);
	_role = role;
	_deployHost.region = region;
	_deployHost.endpoint = endpoint;
}

void ConnectionPrivateData::wroteUploadSections()
{
	int sectionNo;
	std::string section;
	bool allDone;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		if (!_upload || _upload->token)
			return;

		_upload->fetchSectionToWrite(sectionNo, section, allDone);
		if (section.empty() && !allDone)
			return;

		_upload->token = true;
	}

	UploadInfoPtr outRelease;
	while (true)
	{
		if (lseek(_upload->fd, _upload->sectionLength * (sectionNo-1), SEEK_SET) == -1)
		{
			_upload.reset();		//-- auto clear.
			return;
		}

		size_t remain = section.length();
		size_t offset = 0;
		while (remain > 0)
		{
			ssize_t bytes = write(_upload->fd, section.data() + offset, remain);
			if (bytes < 0)
			{
				_upload.reset();		//-- auto clear.
				return;
			}
			remain -= (size_t)bytes;
			offset += (size_t)bytes;
		}


		std::unique_lock<std::mutex> lck(_mutex);
		_upload->sections.erase(sectionNo);
		section.clear();

		_upload->fetchSectionToWrite(sectionNo, section, allDone);
		if (allDone)
		{
			outRelease = _upload;
			_upload.reset();
			break;	//-- ensure outRelease/_upload release after unlocked _mutex.
		}
		if (section.empty())
		{
			_upload->token = false;
			return;
		}
	}
}

bool ConnectionPrivateData::fillUploadSection(const std::string& name, const std::string& desc,
	int sectionCount, int sectionNo, std::string& content, QuestSenderPtr sender, ControlCenterQuestProcessorPtr ccqp)
{
	std::unique_lock<std::mutex> lck(_mutex);
	if (_upload)
	{
		if (_upload->name == name)
		{
			if (sectionNo == 1)
				_upload->sectionLength = (int)content.length();

			_upload->sections[sectionNo].swap(content);
		}
		else
			return false;
	}
	else
	{
		_upload = std::make_shared<UploadInfo>(name, desc, sectionCount, ccqp);
		_upload->sender = sender;

		if (sectionNo == 1)
			_upload->sectionLength = (int)content.length();

		_upload->sections[sectionNo].swap(content);
	}
	return true;
}

void ControlCenterQuestProcessor::writeUploadActor(int socket, ConnectionPrivateData* idAddr)
{
	ConnectionPrivateDataPtr privData;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		privData = _connData[socket];
	}

	if (privData.get() == idAddr)
		privData->wroteUploadSections();
}

ControlCenterQuestProcessor::ControlCenterQuestProcessor(): _monitorMachineStatus(0)
{
	registerMethod("ping", &ControlCenterQuestProcessor::ping);
	registerMethod("deploy", &ControlCenterQuestProcessor::deploy);
	registerMethod("uploadActor", &ControlCenterQuestProcessor::uploadActor);
	registerMethod("machineStatus", &ControlCenterQuestProcessor::machineStatus);
	registerMethod("reloadActorInfo", &ControlCenterQuestProcessor::reloadActorInfo);
	registerMethod("availableActors", &ControlCenterQuestProcessor::availableActors);
	registerMethod("actorTaskStatus", &ControlCenterQuestProcessor::actorTaskStatus);
	registerMethod("actorAction", &ControlCenterQuestProcessor::actorAction);
	registerMethod("systemCmd", &ControlCenterQuestProcessor::systemCmd);
	registerMethod("launchActor", &ControlCenterQuestProcessor::launchActor);
	registerMethod("monitorTasks", &ControlCenterQuestProcessor::monitorTasks);
	registerMethod("monitorMachineStatus", &ControlCenterQuestProcessor::monitorMachineStatus);

	registerMethod("registerDeployer", &ControlCenterQuestProcessor::registerDeployer);
	registerMethod("registerMonitor", &ControlCenterQuestProcessor::registerMonitor);

	registerMethod("registerActor", &ControlCenterQuestProcessor::registerActor);
	registerMethod("actorStatus", &ControlCenterQuestProcessor::actorStatus);
	registerMethod("actorResult", &ControlCenterQuestProcessor::actorResult);

	prepareActorCache();
	loadActorCache();

	globalTaskIdGen = (int)time(NULL) & 0xFFFF;
	_taskPool.init(0, 1, 0, 20);

	_running = true;
	_deployerMonitorThread = std::thread(&ControlCenterQuestProcessor::deployerMontiorCycle, this);
}

ControlCenterQuestProcessor::~ControlCenterQuestProcessor()
{
	_running = false;
	_deployerMonitorThread.join();

	_taskPool.release();
}

void ControlCenterQuestProcessor::prepareActorCache()
{
	_cachePath = Setting::getString("DATControlCenter.cachePath", "./cache");
	if (_cachePath[0] != '/')
	{
		std::string selfPath = FileSystemUtil::getSelfExectuedFilePath();
		selfPath.append("/").append(_cachePath);

		_cachePath.swap(selfPath);
	}

	if (FileSystemUtil::createDirectories(_cachePath.c_str()) == false)
	{
		/*
			Just record fatal log.
			DO NOT exit since we MUST directly notify user when he/she performences actions.
			If server exit, it maybe auto relaunched in many times and nobody can be notified to stop and fix this mistake. 
		*/
		LOG_FATAL("Prepare actors cache folder %s failed.", _cachePath.c_str());
	}

	_descFilePath = _cachePath;
	_descFilePath.append("/").append(gc_defaultActorDescFileName);

	_tmpFileCachePath = _cachePath;
	_tmpFileCachePath.append("/tmp");

	if (FileSystemUtil::createDirectories(_tmpFileCachePath.c_str()) == false)
	{
		LOG_FATAL("Prepare actors temporary cache folder %s failed.", _tmpFileCachePath.c_str());
		_tmpFileCachePath = "/tmp";
	}
}

void ControlCenterQuestProcessor::loadActorCache()
{
	std::map<std::string, struct ActorInfo> actorInfos;
	{
		std::unique_lock<std::mutex> lck(_fileMutex);
		std::vector<std::string> cachedFiles = FileSystemUtil::getFilesInDirectory(_cachePath.c_str());
		for (auto& filename: cachedFiles)
		{
			if (filename == gc_defaultActorDescFileName || filename[0] == '.')
				continue;

			FileSystemUtil::FileAttrs attrs;
			std::string fullname = _cachePath + "/" + filename;
			if (FileSystemUtil::readFileAndAttrs(fullname, attrs) == false)
			{
				LOG_ERROR("Load actor %s failed.", filename.c_str());
				continue;
			}

			actorInfos[filename].mtime = attrs.mtime;
			actorInfos[filename].fileSize = (size_t)attrs.size;
			actorInfos[filename].fileMd5 = attrs.sign;
		}

		std::vector<std::string> actorDesc;
		if (FileSystemUtil::fetchFileContentInLines(_descFilePath, actorDesc) == false)
		{
			LOG_ERROR("Load actor desc record file %s failed.", _descFilePath.c_str());
		}

		for (auto& descLine: actorDesc)
		{
			std::string::size_type pos = descLine.find_first_of(':');
			if (pos == std::string::npos || (size_t)pos  + 1 == descLine.length())
				continue;

			std::string name = descLine.substr(0, pos);
			std::string desc = descLine.substr(pos + 1);

			std::string actorName = StringUtil::trim(name);
			std::string descContent = StringUtil::trim(desc);

			if (desc.empty())
				continue;

			auto it = actorInfos.find(actorName);
			if (it != actorInfos.end())
				it->second.desc = descContent;
		}
	}

	{
		std::unique_lock<std::mutex> lck(_mutex);
		_actorInfos.swap(actorInfos);
	}
}

void ControlCenterQuestProcessor::persistentActorDesc()
{
	ChainBuffer content;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (auto& cp: _actorInfos)
		{
			content.append(cp.first.data(), cp.first.length());
			content.append(":", 1);
			content.append(cp.second.desc.data(), cp.second.desc.length());
			content.append("\r\n", 2);
		}
	}

	{
		std::unique_lock<std::mutex> lck(_fileMutex);
		int fd = open(_descFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IXGRP | S_IROTH | S_IXOTH);
		if (fd == -1)
		{
			LOG_ERROR("Persistent actor desc into record file %s failed.", _descFilePath.c_str());
		}
		else
		{
			content.writefd(fd, content.length(), 0);
			close(fd);
		}
	}
}


void ControlCenterQuestProcessor::addNewActor(const std::string& name, const std::string& desc, const std::string& tmpPath)
{
	FileSystemUtil::FileAttrs attrs;
	std::string fullname = _cachePath + "/" + name;

	std::string systemCmd("mv -f ");
	systemCmd.append(tmpPath).append(" ").append(fullname);

	{
		std::unique_lock<std::mutex> lck(_fileMutex);

		int rc = system(systemCmd.c_str());
		if (rc != 0)
		{
			LOG_ERROR("Move new actor %s from %s into %s failed. System returns code: %d",
				name.c_str(), tmpPath.c_str(), fullname.c_str(), rc);
		}

		if (FileSystemUtil::readFileAndAttrs(fullname, attrs) == false)
		{
			LOG_ERROR("Add new actor %s into cache failed.", name.c_str());
			return;
		}
	}

	{
		std::unique_lock<std::mutex> lck(_mutex);

		_actorInfos[name].mtime = attrs.mtime;
		_actorInfos[name].fileSize = (size_t)attrs.size;
		_actorInfos[name].fileMd5 = attrs.sign;
		_actorInfos[name].desc = desc;
	}

	persistentActorDesc();
}

void ControlCenterQuestProcessor::connected(const ConnectionInfo& ci)
{
	std::unique_lock<std::mutex> lck(_mutex);
	_connData[ci.socket] = std::make_shared<ConnectionPrivateData>();
}

void ControlCenterQuestProcessor::connectionWillClose(const ConnectionInfo& connInfo, bool closeByError)
{
	std::unique_lock<std::mutex> lck(_mutex);

	ConnectionPrivateDataPtr cpd = _connData[connInfo.socket];
	if (cpd->_role == ClientRole::Deployer)
	{
		_deployerInfos.erase(cpd->_deployHost);
	}
	else if (cpd->_role == ClientRole::Actor)
	{
		_runningActorInfos[cpd->_deployHost][cpd->_actorName].erase(cpd->_actorPid);
		if (_runningActorInfos[cpd->_deployHost][cpd->_actorName].empty())
		{
			_runningActorInfos[cpd->_deployHost].erase(cpd->_actorName);
			if (_runningActorInfos[cpd->_deployHost].empty())
				_runningActorInfos.erase(cpd->_deployHost);
		}
	}
	else if (cpd->_role == ClientRole::Monitor)
	{
		_monitorInfos.erase(cpd->_deployHost);
	} 

	for (auto& pp: _monitorMap)
		pp.second.erase(connInfo.socket);

	if (cpd->_monitoringMachineStatus)
		_monitorMachineStatus--;

	_connData.erase(connInfo.socket);
}

void ControlCenterQuestProcessor::adjustMachineDelay(bool deployerRole, struct DeployHost host, int64_t cost)
{
	std::unique_lock<std::mutex> lck(_mutex);
	if (deployerRole)
	{
		auto iter = _deployerInfos.find(host);
		if (iter != _deployerInfos.end())
			iter->second.delayInMsec = cost/2;
	}
	else
	{
		auto iter = _monitorInfos.find(host);
		if (iter != _monitorInfos.end())
			iter->second.delayInMsec = cost/2;
	}
}

void ControlCenterQuestProcessor::adjustMachineStatus(bool deployerRole, struct DeployHost host, int intervalSec, FPAReader& ar)
{
	float load = ar.wantDouble("sysLoad");
	int tcpCount = ar.wantInt("tcpConn");
	int udpCount = ar.wantInt("udpConn");
	int64_t freeMemories = ar.wantInt("freeMemories");
	uint64_t recvBytes = ar.wantInt("RX");
	uint64_t sendBytes = ar.wantInt("TX");

	std::unique_lock<std::mutex> lck(_mutex);
	if (deployerRole)
	{
		auto iter = _deployerInfos.find(host);
		if (iter != _deployerInfos.end())
		{
			iter->second.tcpCount = tcpCount;
			iter->second.udpCount = udpCount;
			iter->second.systemLoad = load;
			iter->second.freeMemories = freeMemories;

			if (iter->second.recvBytes)
				iter->second.recvBytesDiff = (recvBytes - iter->second.recvBytes)/intervalSec;
			if (iter->second.sendBytes)
				iter->second.sendBytesDiff = (sendBytes - iter->second.sendBytes)/intervalSec;

			iter->second.recvBytes = recvBytes;
			iter->second.sendBytes = sendBytes;
		}
	}
	else
	{
		auto iter = _monitorInfos.find(host);
		if (iter != _monitorInfos.end())
		{
			iter->second.tcpCount = tcpCount;
			iter->second.udpCount = udpCount;
			iter->second.systemLoad = load;
			iter->second.freeMemories = freeMemories;

			if (iter->second.recvBytes)
				iter->second.recvBytesDiff = (recvBytes - iter->second.recvBytes)/intervalSec;
			if (iter->second.sendBytes)
				iter->second.sendBytesDiff = (sendBytes - iter->second.sendBytes)/intervalSec;
			
			iter->second.recvBytes = recvBytes;
			iter->second.sendBytes = sendBytes;
		}
	}
}

void ControlCenterQuestProcessor::deployerMontiorCycle()
{
	const int sleepIntervalSec = 2;
	int pingTicket = 0;

	while (_running)
	{
		sleep(sleepIntervalSec);
		pingTicket++;

		bool ping = false;
		if (pingTicket == 10)
		{
			ping = true;
			pingTicket = 0;
		}

		if (_monitorMachineStatus == 0)
			continue;

		ControlCenterQuestProcessorPtr CCQP = shared_from_this();

		std::unique_lock<std::mutex> lck(_mutex);

		//-- Deployers
		for (auto& pp: _deployerInfos)
		{
			struct DeployHost host = pp.first;

			pp.second.sender->sendQuest(FPQWriter::emptyQuest("machineStatus"), [CCQP, host](FPAnswerPtr answer, int errorCode){
				if (errorCode == FPNN_EC_OK)
				{
					FPAReader ar(answer);
					CCQP->adjustMachineStatus(true, host, sleepIntervalSec, ar);
				}
				else
				{
					LOG_ERROR("errorcode = %d",errorCode);
				}
				
				// if (answer)
				// 	LOG_DEBUG("answer = %s",answer->json().c_str());					
			});

			if (ping)
			{
				int64_t msec = slack_mono_msec();
				pp.second.sender->sendQuest(FPQWriter::emptyQuest("ping"), [CCQP, host, msec](FPAnswerPtr answer, int errorCode){
					if (errorCode == FPNN_EC_OK)
						CCQP->adjustMachineDelay(true, host, slack_mono_msec() - msec);
				});
			}
		}

		//-- Monitors
		for (auto& pp: _monitorInfos)
		{
			struct DeployHost host = pp.first;

			pp.second.sender->sendQuest(FPQWriter::emptyQuest("machineStatus"), [CCQP, host](FPAnswerPtr answer, int errorCode){
				if (errorCode == FPNN_EC_OK)
				{
					FPAReader ar(answer);
					CCQP->adjustMachineStatus(false, host, sleepIntervalSec, ar);
				}
			});

			if (ping)
			{
				int64_t msec = slack_mono_msec();
				pp.second.sender->sendQuest(FPQWriter::emptyQuest("ping"), [CCQP, host, msec](FPAnswerPtr answer, int errorCode){
					if (errorCode == FPNN_EC_OK)
						CCQP->adjustMachineDelay(false, host, slack_mono_msec() - msec);
				});
			}
		}
	}
}

std::map<struct DeployHost, QuestSenderPtr> ControlCenterQuestProcessor::fetchDeployerSenders(const std::string& region, std::set<std::string>& ips)
{
	std::map<struct DeployHost, QuestSenderPtr> rev;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (auto& pp: _deployerInfos)
		{
			if (pp.first.region == region || ips.find(pp.first.endpoint) != ips.end())
			{
				ips.erase(pp.first.endpoint);
				rev[pp.first] = pp.second.sender;
			}
		}
	}

	return rev;
}

class DeployCallback
{
	std::mutex _mutex;
	int _taskId;
	QuestSenderPtr _sender;
	std::set<std::string> _failedEndpoints;
public:
	DeployCallback(int taskId, std::set<std::string> invalidEndpoints, QuestSenderPtr sender):
		_taskId(taskId), _sender(sender)
	{
		_failedEndpoints = invalidEndpoints;
	}
	~DeployCallback()
	{
		FPQWriter qw(2, "deployFinish");
		qw.param("taskId", _taskId);
		qw.param("failedEndpoints", _failedEndpoints);

		_sender->sendQuest(qw.take(), [](FPAnswerPtr answer, int errorCode){
			if (errorCode != FPNN_EC_OK && errorCode != FPNN_EC_CORE_CONNECTION_CLOSED)
				LOG_ERROR("Send deployFinish notify failed. Erro code %d", errorCode);
		}, 0);
	}

	void addFailedEndpoint(const std::string& endpoint)
	{
		std::unique_lock<std::mutex> lck(_mutex);
		_failedEndpoints.insert(endpoint);
	}
};

FPAnswerPtr ControlCenterQuestProcessor::deploy(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	const std::string def("<no-region>");

	std::string region = args->getString("region", def);
	std::string actor = args->wantString("actor");
	std::set<std::string> endpoints = args->get("endpoints", std::set<std::string>());

	//-- fetch file content --//
	std::string actorFile(_cachePath);
	actorFile.append("/").append(actor);

	std::string content;
	if (FileSystemUtil::readFileContent(actorFile, content) == false || content.empty())
		return FPAWriter::errorAnswer(quest, ErrorInfo::ActorIsNotExistCode, "Actor is not exist or cannot be loaded or actor invalid.", "DATControlCenter");

	//-- calculate sections --//
	size_t parts = content.length() / gc_maxTransportLength;
	if (content.length() % gc_maxTransportLength)
		parts += 1;

	//-- fetch target endpoints, prepare all callback --//
	int taskId = globalTaskIdGen++;
	std::map<struct DeployHost, QuestSenderPtr> ipmap = fetchDeployerSenders(region, endpoints);
	std::shared_ptr<DeployCallback> allCB(new DeployCallback(taskId, endpoints, genQuestSender(ci)));
	
	//-- send deploy quests --//
	size_t remain = content.length();
	size_t offset = 0;
	for (size_t i = 0; i < parts; i++)
	{
		FPQWriter qw(5, "deployActor");
		qw.param("name", actor);
		qw.paramBinary("section", content.data() + offset, (remain > gc_maxTransportLength) ? gc_maxTransportLength : remain);
		qw.param("count", parts);
		qw.param("no", i + 1);
		qw.param("taskId", taskId);

		FPQuestPtr orgQuest = qw.take();

		for (auto& pp: ipmap)
		{
			std::string endpoint = pp.first.endpoint;
			bool status = pp.second->sendQuest(orgQuest, [endpoint, allCB](FPAnswerPtr answer, int errorCode){
				if (errorCode != FPNN_EC_OK)
					allCB->addFailedEndpoint(endpoint);
			}, 30);
			if (!status)
				allCB->addFailedEndpoint(endpoint);
		}

		remain -= gc_maxTransportLength;
		offset += gc_maxTransportLength;
	}

	FPAWriter aw(1, quest);
	aw.param("taskId", taskId);
	return aw.take();
}

FPAnswerPtr ControlCenterQuestProcessor::uploadActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string name = args->wantString("name");
	std::string desc = args->getString("desc");

	int count = 1;
	int no = 0;

	std::string section = args->getString("actor");
	if (section.empty())
	{
		section = args->wantString("section");
		count = args->wantInt("count");
		no = args->wantInt("no");
	}

	bool rev;
	int taskId = 0;
	int socket = ci.socket;
	ConnectionPrivateData* addr;
	QuestSenderPtr sender = genQuestSender(ci);
	ControlCenterQuestProcessorPtr CCQP = shared_from_this();
	{
		std::unique_lock<std::mutex> lck(_mutex);
		rev = _connData[socket]->fillUploadSection(name, desc, count, no, section, sender, CCQP);
		addr = _connData[socket].get();
		taskId = addr->_upload->taskId;
	}

	if (rev)
	{
		_taskPool.wakeUp([CCQP, socket, addr](){
			CCQP->writeUploadActor(socket, addr);
		});

		FPAWriter aw(1, quest);
		aw.param("taskId", taskId);
		return aw.take();
	}
	else
		return FPAWriter::errorAnswer(quest, ErrorInfo::FileUploadTaskExistCode, "Another file upload task is executing.", "DATControlCenter");
}

const std::vector<std::string> availableActorsFields{"name", "size", "mtime", "md5", "desc"};
const std::vector<std::string> deployedActorFields{"region", "endpoint", "actorName", "size", "mtime", "md5"};
const std::vector<std::string> actorTaskStatusFields{"region", "endpoint", "actorName", "pid", "taskId", "method", "desc"};

FPAnswerPtr ControlCenterQuestProcessor::returnActorInfos(const FPQuestPtr quest)
{
	std::vector<std::vector<std::string>> availableActors, deployedActors;
	{
		std::unique_lock<std::mutex> lck(_mutex);

		for (auto& pp: _actorInfos)
		{
			availableActors.push_back(std::vector<std::string>());
			size_t idx = availableActors.size() - 1;

			availableActors[idx].push_back(pp.first);
			availableActors[idx].push_back(std::to_string(pp.second.fileSize));
			availableActors[idx].push_back(std::to_string(pp.second.mtime));
			availableActors[idx].push_back(pp.second.fileMd5);
			availableActors[idx].push_back(pp.second.desc);
		}

		for (auto& pp: _deployerInfos)
		{
			for (auto& pp2: pp.second.actorInfos)
			{
				deployedActors.push_back(std::vector<std::string>());
				size_t idx = deployedActors.size() - 1;

				deployedActors[idx].push_back(pp.first.region);
				deployedActors[idx].push_back(pp.first.endpoint);
				deployedActors[idx].push_back(pp2.first);
				deployedActors[idx].push_back(std::to_string(pp2.second.fileSize));
				deployedActors[idx].push_back(std::to_string(pp2.second.mtime));
				deployedActors[idx].push_back(pp2.second.fileMd5);
			}

			if (pp.second.actorInfos.empty())
			{
				deployedActors.push_back(std::vector<std::string>());
				size_t idx = deployedActors.size() - 1;

				deployedActors[idx].push_back(pp.first.region);
				deployedActors[idx].push_back(pp.first.endpoint);
				deployedActors[idx].push_back("");
				deployedActors[idx].push_back("");
				deployedActors[idx].push_back("");
				deployedActors[idx].push_back("");
			}
		}
	}

	FPAWriter aw(2, quest);
	aw.paramMap("availableActors", 2);
	aw.param("fields", availableActorsFields);
	aw.param("rows", availableActors);
	aw.paramMap("deployedActors", 2);
	aw.param("fields", deployedActorFields);
	aw.param("rows", deployedActors);

	return aw.take();
}

FPAnswerPtr ControlCenterQuestProcessor::reloadActorInfo(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	loadActorCache();
	return returnActorInfos(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::availableActors(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	return returnActorInfos(quest);
}

const std::vector<std::string> MachineStatusFields{"source", "region", "host", "ping/2 (msec)", "cpus", "load", "memories", "freeMemories", "tcpCount", "udpCount", "RX", "TX"};

FPAnswerPtr ControlCenterQuestProcessor::machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<std::vector<std::string>> rows;
	{
		std::unique_lock<std::mutex> lck(_mutex);

		for (auto& pp: _deployerInfos)
		{
			std::string host;
			int port;

			if (!parseAddress(pp.first.endpoint, host, port))
				host = pp.first.endpoint;
		
			rows.push_back(std::vector<std::string>());
			size_t idx = rows.size() - 1;

			rows[idx].push_back("Deployer");
			rows[idx].push_back(pp.first.region);
			rows[idx].push_back(host);
			
			rows[idx].push_back(std::to_string(pp.second.delayInMsec));
			rows[idx].push_back(std::to_string(pp.second.cpuCount));
			rows[idx].push_back(std::to_string(pp.second.systemLoad));

			rows[idx].push_back(std::to_string(pp.second.memoryCount));
			rows[idx].push_back(std::to_string(pp.second.freeMemories));
			rows[idx].push_back(std::to_string(pp.second.tcpCount));
			rows[idx].push_back(std::to_string(pp.second.udpCount));

			rows[idx].push_back(std::to_string(pp.second.recvBytesDiff));
			rows[idx].push_back(std::to_string(pp.second.sendBytesDiff));
		}

		for (auto& pp: _monitorInfos)
		{
			std::string host;
			int port;

			if (!parseAddress(pp.first.endpoint, host, port))
				host = pp.first.endpoint;
		
			rows.push_back(std::vector<std::string>());
			size_t idx = rows.size() - 1;

			rows[idx].push_back("Monitor");
			rows[idx].push_back(pp.first.region);
			rows[idx].push_back(host);
			
			rows[idx].push_back(std::to_string(pp.second.delayInMsec));
			rows[idx].push_back(std::to_string(pp.second.cpuCount));
			rows[idx].push_back(std::to_string(pp.second.systemLoad));

			rows[idx].push_back(std::to_string(pp.second.memoryCount));
			rows[idx].push_back(std::to_string(pp.second.freeMemories));
			rows[idx].push_back(std::to_string(pp.second.tcpCount));
			rows[idx].push_back(std::to_string(pp.second.udpCount));

			rows[idx].push_back(std::to_string(pp.second.recvBytesDiff));
			rows[idx].push_back(std::to_string(pp.second.sendBytesDiff));
		}
	}
	
	FPAWriter aw(2, quest);
	aw.param("fields", MachineStatusFields);
	aw.param("rows", rows);

	return aw.take();
}

FPAnswerPtr ControlCenterQuestProcessor::actorTaskStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::vector<std::vector<std::string>> rows;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (auto& pp: _runningActorInfos)
		{
			for (auto& pp2: pp.second)
			{
				for (auto& pp3: pp2.second)
				{
					for (auto& pp4: pp3.second.taskMap)
					{
						rows.push_back(std::vector<std::string>());
						size_t idx = rows.size() - 1;

						rows[idx].push_back(pp.first.region);
						rows[idx].push_back(pp.first.endpoint);
						rows[idx].push_back(pp2.first);
						rows[idx].push_back(std::to_string(pp3.first));
						rows[idx].push_back(std::to_string(pp4.first));
						rows[idx].push_back(pp4.second[0]);
						rows[idx].push_back(pp4.second[1]);
					}

					if (pp3.second.taskMap.empty())
					{
						rows.push_back(std::vector<std::string>());
						size_t idx = rows.size() - 1;

						rows[idx].push_back(pp.first.region);
						rows[idx].push_back(pp.first.endpoint);
						rows[idx].push_back(pp2.first);
						rows[idx].push_back(std::to_string(pp3.first));
						rows[idx].push_back("");
						rows[idx].push_back("");
						rows[idx].push_back("");
					}
				}
			}
		}
	}
	
	FPAWriter aw(2, quest);
	aw.param("fields", actorTaskStatusFields);
	aw.param("rows", rows);

	return aw.take();
}

void ControlCenterQuestProcessor::actorTaskFinish(const std::string& endpoint, const std::string& actor, int pid, int taskId)
{
	std::unique_lock<std::mutex> lck(_mutex);
	for (auto& pp: _runningActorInfos)
	{
		if (pp.first.endpoint == endpoint)
		{
			auto actorIter = pp.second.find(actor);
			if (actorIter != pp.second.end())
			{
				auto pidIter = actorIter->second.find(pid);
				if (pidIter != actorIter->second.end())
					pidIter->second.taskMap.erase(taskId);
			}
			break;
		}
	}

	_monitorMap.erase(taskId);
}

FPAnswerPtr ControlCenterQuestProcessor::actorAction(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string actor = args->wantString("actor");
	std::string endpoint = args->wantString("endpoint");
	int pid = args->wantInt("pid");
	std::string method = args->wantString("method");
	std::string payload = args->wantString("payload");
	std::string taskDesc = args->getString("taskDesc");

	int taskId = 0;
	QuestSenderPtr sender;
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (auto& pp: _runningActorInfos)
		{
			if (pp.first.endpoint == endpoint)
			{
				auto actorIter = pp.second.find(actor);
				if (actorIter != pp.second.end())
				{
					auto pidIter = actorIter->second.find(pid);
					if (pidIter != actorIter->second.end())
					{
						sender = pidIter->second.sender;

						taskId = globalTaskIdGen++;
						pidIter->second.taskMap[taskId].push_back(method);
						pidIter->second.taskMap[taskId].push_back(taskDesc);
					}
				}
				break;
			}
		}

		_monitorMap[taskId][ci.socket] = genQuestSender(ci);
	}

	if (sender)
	{
		FPQWriter qw(3, "action");
		qw.param("taskId", taskId);
		qw.param("method", method);
		qw.param("payload", payload);

		IAsyncAnswerPtr async = genAsyncAnswer(quest);
		ControlCenterQuestProcessorPtr CCQP = shared_from_this();
		bool status = sender->sendQuest(qw.take(), [async, taskId, endpoint, actor, pid, CCQP](FPAnswerPtr answer, int errorCode){
			if (errorCode == FPNN_EC_OK)
			{
				FPAWriter aw(1, async->getQuest());
				aw.param("taskId", taskId);

				async->sendAnswer(aw.take());
			}
			else
			{
				if (answer)
				{
					FPAReader ar(answer);
					async->sendErrorAnswer(errorCode, ar.wantString("ex"));
				}
				else
					async->sendErrorAnswer(errorCode, "");

				CCQP->actorTaskFinish(endpoint, actor, pid, taskId);
			}
		});
		if (!status)
		{
			actorTaskFinish(endpoint, actor, pid, taskId);
			async->sendErrorAnswer(FPNN_EC_CORE_SEND_ERROR, "Transport action failed.");
		}
		
		return nullptr;
	}
	else
		return FPAWriter::errorAnswer(quest, ErrorInfo::ActorIsNotExistCode, "Actor is not exist or cannot be loaded or actor invalid.", "DATControlCenter");
}

class SystemCmdCallback
{
	std::mutex _mutex;
	IAsyncAnswerPtr _async;
	std::map<std::string, int> _failedEndpoints;
public:
	SystemCmdCallback(std::set<std::string> invalidEndpoints, IAsyncAnswerPtr async): _async(async)
	{
		for (auto& endpoint: invalidEndpoints)
			_failedEndpoints[endpoint] = 0;
	}
	~SystemCmdCallback()
	{
		if (_failedEndpoints.empty())
		{
			FPAWriter aw(1, _async->getQuest());
			aw.param("ok", true);
			_async->sendAnswer(aw.take());
		}
		else
		{
			FPAWriter aw(2, _async->getQuest());
			aw.param("ok", false);
			aw.param("failedEndpoints", _failedEndpoints);
			_async->sendAnswer(aw.take());
		}
	}

	void addFailedCase(const std::string& endpoint, int line)
	{
		std::unique_lock<std::mutex> lck(_mutex);
		_failedEndpoints[endpoint] = line;
	}
};

FPAnswerPtr ControlCenterQuestProcessor::systemCmd(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	const std::string def("<no-region>");

	std::string region = args->getString("region", def);
	std::set<std::string> endpoints = args->get("endpoints", std::set<std::string>());
	std::vector<std::string> cmdLines = args->want("cmdLines", std::vector<std::string>());

	std::map<struct DeployHost, QuestSenderPtr> ipmap = fetchDeployerSenders(region, endpoints);
	std::shared_ptr<SystemCmdCallback> allCB(new SystemCmdCallback(endpoints, genAsyncAnswer(quest)));

	FPQWriter qw(1, "systemCmd");
	qw.param("cmdLines", cmdLines);
	FPQuestPtr orgQuest = qw.take();

	for (auto& pp: ipmap)
	{
		std::string endpoint = pp.first.endpoint;
		bool status = pp.second->sendQuest(orgQuest, [endpoint, allCB](FPAnswerPtr answer, int errorCode){
			if (errorCode != FPNN_EC_OK)
				allCB->addFailedCase(endpoint, 0);
			else
			{
				FPAReader ar(answer);
				if (!ar.wantBool("ok"))
					allCB->addFailedCase(endpoint, ar.wantInt("failedLine"));
			}
		});
		if (!status)
			allCB->addFailedCase(endpoint, 0);
	}

	return nullptr;
}

class LaunchActorCallback
{
	std::mutex _mutex;
	IAsyncAnswerPtr _async;
	std::set<std::string> _failedEndpoints;
public:
	LaunchActorCallback(std::set<std::string> invalidEndpoints, IAsyncAnswerPtr async): _async(async)
	{
		_failedEndpoints = invalidEndpoints;
	}
	~LaunchActorCallback()
	{
		if (_failedEndpoints.empty())
		{
			FPAWriter aw(1, _async->getQuest());
			aw.param("ok", true);
			_async->sendAnswer(aw.take());
		}
		else
		{
			FPAWriter aw(2, _async->getQuest());
			aw.param("ok", false);
			aw.param("failedEndpoints", _failedEndpoints);
			_async->sendAnswer(aw.take());
		}
	}

	void addFailedEndpoint(const std::string& endpoint)
	{
		std::unique_lock<std::mutex> lck(_mutex);
		_failedEndpoints.insert(endpoint);
	}
};

FPAnswerPtr ControlCenterQuestProcessor::launchActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	const std::string def("<no-region>");

	std::string region = args->getString("region", def);
	std::string actor = args->wantString("actor");
	std::set<std::string> endpoints = args->get("endpoints", std::set<std::string>());
	std::string cmdLine = args->getString("cmdLine");

	std::map<struct DeployHost, QuestSenderPtr> ipmap = fetchDeployerSenders(region, endpoints);
	std::shared_ptr<LaunchActorCallback> allCB(new LaunchActorCallback(endpoints, genAsyncAnswer(quest)));

	FPQWriter qw(2, "launchActor");
	qw.param("actor", actor);
	qw.param("cmdLine", cmdLine);
	FPQuestPtr orgQuest = qw.take();

	for (auto& pp: ipmap)
	{
		std::string endpoint = pp.first.endpoint;
		bool status = pp.second->sendQuest(orgQuest, [endpoint, allCB](FPAnswerPtr answer, int errorCode){
			if (errorCode != FPNN_EC_OK)
				allCB->addFailedEndpoint(endpoint);
			else
			{
				FPAReader ar(answer);
				if (!ar.wantBool("ok"))
					allCB->addFailedEndpoint(endpoint);
			}
		});
		if (!status)
			allCB->addFailedEndpoint(endpoint);
	}

	return nullptr;
}

FPAnswerPtr ControlCenterQuestProcessor::monitorTasks(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::set<int> taskIds = args->want("taskIds", std::set<int>());
	QuestSenderPtr sender = genQuestSender(ci);
	{
		std::unique_lock<std::mutex> lck(_mutex);
		for (int taskId: taskIds)
			_monitorMap[taskId][ci.socket] = sender;
	}

	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::registerDeployer(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string region = args->wantString("region");
	std::string endpoint = ci.endpoint();
	std::vector<std::string> fields = args->want("fields", std::vector<std::string>());
	std::vector<std::vector<std::string>> rows = args->want("rows", std::vector<std::vector<std::string>>());
	int cpus = args->wantInt("cpus");
	int64_t memories = args->wantInt("totalMemories");

	std::map<std::string, int> idxmap = buildIdxMap(std::set<std::string>{"actor", "size", "md5", "mtime"}, fields);
	QuestSenderPtr sender = genQuestSender(ci);

	{
		std::unique_lock<std::mutex> lck(_mutex);
		_connData[ci.socket]->registerRole(ClientRole::Deployer, region, endpoint);

		const struct DeployHost& host = _connData[ci.socket]->_deployHost;
		_deployerInfos[host].sender = sender;
		_deployerInfos[host].cpuCount = cpus;
		_deployerInfos[host].memoryCount = memories;

		std::map<std::string, struct ActorInfo>& deployStatus = _deployerInfos[host].actorInfos;
		deployStatus.clear();
		for (auto& row: rows)
		{
			int idx = idxmap["actor"];
			if (idx < 0)
				continue;

			struct ActorInfo& ai = deployStatus[row[idx]];

			idx = idxmap["mtime"];
			if (idx > -1)
				ai.mtime = atoll(row[idx].c_str());

			idx = idxmap["size"];
			if (idx > -1)
				ai.fileSize = atoll(row[idx].c_str());

			idx = idxmap["md5"];
			if (idx > -1)
				ai.fileMd5 = row[idx];
		}
	}
	
	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::registerMonitor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string region = args->wantString("region");
	std::string endpoint = ci.endpoint();

	int cpus = args->wantInt("cpus");
	int64_t memories = args->wantInt("totalMemories");

	QuestSenderPtr sender = genQuestSender(ci);

	{
		std::unique_lock<std::mutex> lck(_mutex);
		_connData[ci.socket]->registerRole(ClientRole::Monitor, region, endpoint);

		const struct DeployHost& host = _connData[ci.socket]->_deployHost;
		_monitorInfos[host].sender = sender;
		_monitorInfos[host].cpuCount = cpus;
		_monitorInfos[host].memoryCount = memories;
	}
	
	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::registerActor(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	std::string region = args->wantString("region");
	std::string endpoint = ci.endpoint();
	std::string name = args->wantString("name");
	int pid = args->wantInt("pid");
	std::map<int, std::vector<std::string>> executingTasks = args->get("executingTasks", std::map<int, std::vector<std::string>>());
	QuestSenderPtr sender = genQuestSender(ci);

	{
		std::unique_lock<std::mutex> lck(_mutex);
		_connData[ci.socket]->registerRole(ClientRole::Actor, region, endpoint);
		_connData[ci.socket]->_actorName = name;
		_connData[ci.socket]->_actorPid = pid;

		const struct DeployHost& host = _connData[ci.socket]->_deployHost;
		_runningActorInfos[host][name][pid].sender = sender;

		_runningActorInfos[host][name][pid].taskMap.clear();
		
		for (auto& pp: executingTasks)
			_runningActorInfos[host][name][pid].taskMap[pp.first] = pp.second;
	}
	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::forwardActorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	int taskId = args->wantInt("taskId");
	std::string region = args->wantString("region");
	std::string payload = args->wantString("payload");

	FPQWriter qw(4, quest->method());
	qw.param("taskId", taskId);
	qw.param("region", region);
	qw.param("endpoint", ci.endpoint());
	qw.param("payload", payload);

	{
		std::unique_lock<std::mutex> lck(_mutex);
		auto iter = _monitorMap.find(taskId);
		if (iter != _monitorMap.end())
		{
			for (auto& pp: iter->second)
				pp.second->sendQuest(qw.take(), [](FPAnswerPtr answer, int errorCode){
					if (errorCode != FPNN_EC_OK)
						LOG_ERROR("Forward 'actorStatus' or 'actorResult' error. Code: %d", errorCode);
				}, 0);
		}
	}

	return FPAWriter::emptyAnswer(quest);
}
FPAnswerPtr ControlCenterQuestProcessor::actorStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	return forwardActorStatus(args, quest, ci);
}

FPAnswerPtr ControlCenterQuestProcessor::actorResult(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	return forwardActorStatus(args, quest, ci);
}

FPAnswerPtr ControlCenterQuestProcessor::ping(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	return FPAWriter::emptyAnswer(quest);
}

FPAnswerPtr ControlCenterQuestProcessor::monitorMachineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	bool monitor = args->wantBool("monitor");

	{
		std::unique_lock<std::mutex> lck(_mutex);
		ConnectionPrivateDataPtr cpd = _connData[ci.socket];

		if (monitor != cpd->_monitoringMachineStatus)
		{
			cpd->_monitoringMachineStatus = monitor;
			if (monitor)
				_monitorMachineStatus++;
			else
				_monitorMachineStatus--;
		}			
	}
	return FPAWriter::emptyAnswer(quest);
}