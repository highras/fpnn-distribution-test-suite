#include <sys/sysinfo.h>
#include <string.h>
#include <fstream>
#include "StringUtil.h"
#include "MonitorQuestProcessor.h"

using namespace std;

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

FPAnswerPtr MonitorQuestProcessor::machineStatus(const FPReaderPtr args, const FPQuestPtr quest, const ConnectionInfo& ci)
{
	struct sysinfo info;
	sysinfo(&info);

	uint64_t recvBytes, sendBytes;
	getNetworkStatus(recvBytes, sendBytes);

	return FPAWriter(6, quest)("sysLoad", getCPULoad())("tcpConn", getTCPConnNum())("udpConn", getUDPConnNum())("freeMemories", info.freeram)("RX", recvBytes)("TX", sendBytes);
}