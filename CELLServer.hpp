#ifndef _CELL_SERVER_HPP_
#define _CELL_SERVER_HPP_

#include"CELL.hpp"
#include"INetEvent.hpp"
#include"CELLClient.hpp"

#include<vector>
#include<map>

//������Ϣ��������
class CellSendMsg2ClientTask :public CellTask
{
	CellClient* _pClient;
	netmsg_DataHeader* _pHeader;
public:
	CellSendMsg2ClientTask(CellClient* pClient, netmsg_DataHeader* header)
	{
		_pClient = pClient;
		_pHeader = header;
	}

	//ִ������ //���ÿͻ������Դ���sendData()����
	void doTask()
	{
		_pClient->SendData(_pHeader);
		delete _pHeader;
	}
};

//������Ϣ���մ��������
class CellServer
{
public:
	CellServer(SOCKET sock = INVALID_SOCKET)
	{
		_sock = sock;
		_pNetEvent = nullptr;
	}

	~CellServer()
	{
		Close();
		_sock = INVALID_SOCKET;
	}

	void setEventObj(INetEvent* event)
	{
		_pNetEvent = event;
	}

	//�ر�Socket
	void Close()
	{
		if (_sock != INVALID_SOCKET)
		{
#ifdef _WIN32
			for (auto iter : _clients)
			{
				closesocket(iter.second->sockfd());
				delete iter.second;
			}
			//�ر��׽���closesocket
			closesocket(_sock);
#else
			for (auto iter : _clients)
			{
				close(iter.second->sockfd());
				delete iter.second;
			}
			//�ر��׽���closesocket
			close(_sock);
#endif
			_clients.clear();
		}
	}

	//�Ƿ�����
	bool isRun()
	{
		return _sock != INVALID_SOCKET;
	}

	//����������Ϣ
	//���ݿͻ�socket fd_set
	fd_set _fdRead_bak;
	//�ͻ��б��Ƿ��б仯
	bool _clients_change;
	SOCKET _maxSock;
	void OnRun()
	{
		_clients_change = true;
		while (isRun())
		{
			if (!_clientsBuff.empty())
			{//�ӻ��������ȡ���ͻ�����
				std::lock_guard<std::mutex> lock(_mutex);
				for (auto pClient : _clientsBuff)
				{
					_clients[pClient->sockfd()] = pClient;
				}
				_clientsBuff.clear();
				_clients_change = true;
			}

			//���û����Ҫ����Ŀͻ��ˣ�������
			if (_clients.empty())
			{
				std::chrono::milliseconds t(1);
				std::this_thread::sleep_for(t);
				continue;
			}
			/*
			����FD_READ���ϵ�Ŀ�ģ�
			1.���ǿͻ��˶����ޱ仯ʱ����Ҫ���¼���maxSock
			2.����ÿ�ζ�ѭ����_client�������sockȡ������fd_read��������ֱ�Ӹ�ֵ
			*/

			//�������׽��� BSD socket
			fd_set fdRead;//��������socket�� ����
						  //������
			FD_ZERO(&fdRead);
			if (_clients_change)
			{
				_clients_change = false;
				//����������socket�����뼯��
				_maxSock = _clients.begin()->second->sockfd();
				for (auto iter : _clients)
				{
					FD_SET(iter.second->sockfd(), &fdRead);
					if (_maxSock < iter.second->sockfd())
					{
						_maxSock = iter.second->sockfd();
					}
				}
				memcpy(&_fdRead_bak, &fdRead, sizeof(fd_set));
			}
			else {
				memcpy(&fdRead, &_fdRead_bak, sizeof(fd_set));
			}

			///nfds ��һ������ֵ ��ָfd_set����������������(socket)�ķ�Χ������������
			///���������ļ����������ֵ+1 ��Windows�������������д0
			/*
			select�����ķ���ֵ
			��ֵ��select����
			��ֵ����ʾĳЩ�ļ��ɶ����д
			0���ȴ���ʱ��û�пɶ�д�������ļ�
			*/
			int ret = select(_maxSock + 1, &fdRead, nullptr, nullptr, nullptr);
			if (ret < 0)
			{
				printf("select���������\n");
				Close();
				return;
			}
			else if (ret == 0)
			{
				continue;
			}

#ifdef _WIN32
			for (int n = 0; n < fdRead.fd_count; n++)
			{
				auto iter = _clients.find(fdRead.fd_array[n]);
				if (iter != _clients.end())
				{     //�ͻ����˳�����ͻ��˷������ݣ�recvData()����-1
					if (-1 == RecvData(iter->second))
					{
						if (_pNetEvent)
							_pNetEvent->OnNetLeave(iter->second);
						_clients_change = true;
						_clients.erase(iter->first);
					}
				}
				else {
					printf("error. if (iter != _clients.end())\n");
				}

			}
#else
			std::vector<CellClient*> temp;
			for (auto iter : _clients)
			{
				if (FD_ISSET(iter.second->sockfd(), &fdRead))
				{
					if (-1 == RecvData(iter.second))
					{
						if (_pNetEvent)
							_pNetEvent->OnNetLeave(iter.second);
						_clients_change = false;
						temp.push_back(iter.second);
					}
				}
			}
			for (auto pClient : temp)
			{
				_clients.erase(pClient->sockfd());
				delete pClient;
			}
#endif
		}
	}
	//�������� ����ճ�� ��ְ�   //���տͻ������������߳̽��У�ֱ��д��ÿ���ͻ�����Ľ��ջ�����
	int RecvData(CellClient* pClient)
	{
		//���տͻ�������
		char* szRecv = pClient->msgBuf() + pClient->getLastPos();
		//ֱ��д��ͻ�����Ľ��ջ�����
		int nLen = (int)recv(pClient->sockfd(), szRecv, (RECV_BUFF_SZIE)-pClient->getLastPos(), 0);
		//֪ͨ���߳̽�����һ����Ϣ
		_pNetEvent->OnNetRecv(pClient);
		//printf("nLen=%d\n", nLen);
		if (nLen <= 0)
		{
			//printf("�ͻ���<Socket=%d>���˳������������\n", pClient->sockfd());
			return -1;
		}
		//����ȡ�������ݿ�������Ϣ������
		//memcpy(pClient->msgBuf() + pClient->getLastPos(), _szRecv, nLen);
		//��Ϣ������������β��λ�ú���
		pClient->setLastPos(pClient->getLastPos() + nLen);

		//�ж���Ϣ�����������ݳ��ȴ�����Ϣͷnetmsg_DataHeader����
		//����ͻ��˽��ջ��������ݴ���������Ϣ��Ϣ�ĳ��ȣ��������Ϣ�����ѽ��ջ����������ݽ���ƫ��
		while (pClient->getLastPos() >= sizeof(netmsg_DataHeader))
		{
			//��ʱ�Ϳ���֪����ǰ��Ϣ�ĳ���
			netmsg_DataHeader* header = (netmsg_DataHeader*)pClient->msgBuf();
			//�ж���Ϣ�����������ݳ��ȴ�����Ϣ����
			if (pClient->getLastPos() >= header->dataLength)
			{
				//��Ϣ������ʣ��δ�������ݵĳ���
				int nSize = pClient->getLastPos() - header->dataLength;
				//����������Ϣ
				OnNetMsg(pClient, header);
				//����Ϣ������ʣ��δ��������ǰ��
				memcpy(pClient->msgBuf(), pClient->msgBuf() + header->dataLength, nSize);
				//��Ϣ������������β��λ��ǰ��
				pClient->setLastPos(nSize);
			}
			else {
				//��Ϣ������ʣ�����ݲ���һ��������Ϣ
				break;
			}
		}
		return 0;
	}

	//��Ӧ������Ϣ
	virtual void OnNetMsg(CellClient* pClient, netmsg_DataHeader* header)
	{
		_pNetEvent->OnNetMsg(this, pClient, header);
	}

	void addClient(CellClient* pClient)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		//_mutex.lock();
		_clientsBuff.push_back(pClient);
		//_mutex.unlock();
	}

	void Start()
	{
		_thread = std::thread(std::mem_fn(&CellServer::OnRun), this);
		_taskServer.Start();  //�¿�һ���߳�ִ�з����������
	}

	size_t getClientCount()
	{
		return _clients.size() + _clientsBuff.size();
	}

	//���ӷ������߳�����ӷ������񵽷������������������Ҫ����Ŀ���Լ�������Ϣ
	void addSendTask(CellClient* pClient, netmsg_DataHeader* header)
	{
		CellSendMsg2ClientTask* task = new CellSendMsg2ClientTask(pClient, header);
		_taskServer.addTask(task);
	}
private:
	SOCKET _sock;
	//��ʽ�ͻ�����
	std::map<SOCKET, CellClient*> _clients;
	//����ͻ�����
	std::vector<CellClient*> _clientsBuff;
	//������е���
	std::mutex _mutex;
	std::thread _thread;
	//�����¼�����
	INetEvent* _pNetEvent;
	//���ӷ������߳�����ӷ������񵽷������������
	CellTaskServer _taskServer;  //ÿ���ӷ������ְ���һ���������������
};

#endif // !_CELL_SERVER_HPP_
