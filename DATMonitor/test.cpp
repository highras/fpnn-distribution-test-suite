#include <unistd.h>
#include <sys/sysinfo.h>
#include <string.h>
#include <fstream>
#include <iostream>
#include "StringUtil.h"
#include "FormattedPrint.h"

using namespace fpnn;

int getConnNum()
{
	std::ifstream fin("/proc/net/sockstat");
	if (fin.is_open()) {
		char line[1024];
		while(fin.getline(line, sizeof(line))){
			if (strncmp("TCP:", line, 4))
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
			std::cout<<"failed to convert string to float, the string: "<<items[0].c_str()<<std::endl;
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

			if (strncmp("eth", items[0].c_str(), 3))
				continue;

			recvBytes += std::stoull(items[1]);
			sendBytes += std::stoull(items[9]);
		}
		fin.close();
	}
}

int main()
{
	using namespace std;

	int sleepSeconds = 2;
	uint64_t _RX = 0, _TX = 0;
	while (true)
	{
		struct sysinfo info;
		sysinfo(&info);

		uint64_t RX, TX;
		getNetworkStatus(RX, TX);

		uint64_t diffRX = (RX - _RX)/sleepSeconds;
		uint64_t diffTX = (TX - _TX)/sleepSeconds;

		_RX = RX;
		_TX = TX;

		cout<<"sys load: "<<getCPULoad()<<", conn: "<<getConnNum()<<", freeMemories: "<<fpnn::formatBytesQuantity(info.freeram);
		cout<<", RX: "<<fpnn::formatBytesQuantity(diffRX)<<"("<<diffRX<<")";
		cout<<", TX: "<<fpnn::formatBytesQuantity(diffTX)<<"("<<diffTX<<")"<<endl;

		sleep(sleepSeconds);
	}

	return 0;
}