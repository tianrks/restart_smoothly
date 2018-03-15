#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include "pthread.h"

#ifdef _WIN32
#	include <winsock.h>
#	include <direct.h>
#	pragma comment(lib, "ws2_32.lib")
#	pragma comment(lib, "pthreadVC2.lib")
#else
#   include <sys/socket.h>
#   include <netinet/in.h>
#   include <netinet/ip.h>
#   include <arpa/inet.h>
#   include <sys/mman.h>
#   include <unistd.h>
#   include <fcntl.h>
#   include <sys/stat.h>
#endif // _WIN32

#define LISTEN_NUM  5
#define LOCAL_PORT 9999
#define LOCAL_IP "127.0.0.1"

#ifdef _WIN32
#	define sleep_ms(ms) Sleep(ms)
#	define close_socket(sck) closesocket(sck)
#else
#	define sleep_ms(ms) usleep((ms) * 1000)
#	define close_socket(sck) close(sck)
#endif // _WIN32

bool g_worker_run		= true;		//工作线程继续执行标志
int g_listen_socket_fd	= 0;		//监听SOCKET
int g_accept_count		= 0;		//监听SOCKET当前处于accpet的数量
pthread_mutex_t worker_mutex;		//工作线程锁

//工作线程入口函数
void* worker_main(void* param)
{
	printf("start worker thread\n");
	
#ifdef _WIN32
	timeval select_time_val = { 0, 50 };
#else
	timeval select_time_val = { 0, 50000 };
#endif // _WIN32

	char recv_buffer[1024] = { 0 };

	fd_set listen_fd_set;
	
	//循环处理
	while (g_worker_run)
	{
		int connect_fd = 0;

		//检测监听socket的请求，防止阻塞在accept
		pthread_mutex_lock(&worker_mutex);
		while (g_worker_run)
		{
			FD_ZERO(&listen_fd_set);
			FD_SET(g_listen_socket_fd, &listen_fd_set);
			if (0 < select(g_listen_socket_fd + 1, &listen_fd_set, NULL, NULL, &select_time_val))
			{
				//接受连接
				g_accept_count++;
				connect_fd = accept(g_listen_socket_fd, NULL, NULL);
				g_accept_count--;
				break;
			}
		}
		pthread_mutex_unlock(&worker_mutex);

		//设置连接
		if (0 < connect_fd)
		{
			//设置超时时长
			timeval recv_time_val = { 1, 0 };
			setsockopt(connect_fd, SOL_SOCKET, SO_RCVTIMEO, (char*)&recv_time_val, sizeof(recv_time_val));
		}

		//处理连接
		while (0 < connect_fd)
		{
			int read_len = recv(connect_fd, recv_buffer, sizeof(recv_buffer) - 1, 0);
			if (0 < read_len)
			{
				recv_buffer[read_len + 1] = 0;
				printf("%d \t %s\n", connect_fd, recv_buffer);
			}
			else if (0 == read_len)
			{
				close_socket(connect_fd);
				connect_fd = 0;
			}
			else
			{
				printf("socket %d recv error\n", connect_fd);
			}
		}
	}//循环处理

	printf("quit worker thread\n");

	return NULL;
}//工作线程入口函数

int main(int argc, const char* argv[])
{
#ifdef _WIN32
	WSADATA  Ws;
	if (WSAStartup(MAKEWORD(2, 2), &Ws) == 0)
	{
		printf("WSAStartup ok\n");
	}
	else
	{
		printf("WSAStartup error\n");
		exit(-1);
	}
#endif // _WIN32

	//创建socket
	g_listen_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (0 < g_listen_socket_fd)
	{
		printf("socket ok\n");
	}
	else
	{
		printf("socket error\n");
		exit(-1);
	}

	//设置socket参数
	if (0 < g_listen_socket_fd)
	{
		const int switch_on = 1;

		//设置其他IP可使用该端口
		if (setsockopt(g_listen_socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&switch_on, sizeof(switch_on)) == -1)
		{
			printf("SO_REUSEADDR error\n");
		}

		//设置其他进程可使用该IP和端口.
#ifdef SO_REUSEPORT
		if (setsockopt(g_listen_socket_fd, SOL_SOCKET, SO_REUSEPORT, (char*)&switch_on, sizeof(switch_on)) == -1)
		{
			printf("SO_REUSEPORT error\n");
		}
#else
		printf("unsupport SO_REUSEPORT\n");
#endif
	}

	//绑定IP、PORT
	sockaddr_in saddr = { 0 };
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(LOCAL_PORT);
	saddr.sin_addr.s_addr = inet_addr(LOCAL_IP);
	if (bind(g_listen_socket_fd, (sockaddr*)&saddr, sizeof(saddr)) == 0)
	{
		printf("bind ok\n");
	}
	else
	{
		close_socket(g_listen_socket_fd);
		g_listen_socket_fd = 0;
		printf("bind error\n");
		return -1;
	}

	//开始监听.
	if (listen(g_listen_socket_fd, LISTEN_NUM) == 0)
	{
		printf("listen ok\n");
	}
	else
	{
		close_socket(g_listen_socket_fd);
		g_listen_socket_fd = 0;
		printf("listen error\n");
		return -1;
	}

	//启动工作线程
	pthread_mutex_init(&worker_mutex, NULL);
	pthread_t worker_info[LISTEN_NUM] = { 0 };
	for (int create_index = 0; create_index < LISTEN_NUM; ++create_index)
	{
		pthread_create(&worker_info[create_index], NULL, worker_main, NULL);
	}

	//拼接共享文件
	char mmap_file_path[512] = { 0 };
#ifdef _WIN32
	_getcwd(mmap_file_path, sizeof(mmap_file_path));
#else
	getcwd(mmap_file_path, sizeof(mmap_file_path));
#endif // _WIN32
	sprintf(mmap_file_path + strlen(mmap_file_path), "/process_%d", LOCAL_PORT);

#ifdef _WIN32
	//创建文件.
	DWORD access_flags = GENERIC_READ | GENERIC_WRITE;
	DWORD share_flags = FILE_SHARE_WRITE | FILE_SHARE_WRITE;
	HANDLE file_fd = CreateFile(mmap_file_path, access_flags, share_flags, NULL, OPEN_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (file_fd == NULL)
	{
		printf("create file error: %d\n", GetLastError());
	}

	//创建映射
	HANDLE map_fd = NULL;
	if (NULL != file_fd)
	{
		map_fd = CreateFileMapping(file_fd, NULL, PAGE_READWRITE, 0, 10, TEXT("reuseport"));
		if (NULL == map_fd)
		{
			printf("mapping error: %d\n", GetLastError());
		}
	}

	//映射到内存.
	short* notice_value = NULL;
	if (NULL != map_fd)
	{
		notice_value = (short*)MapViewOfFile(map_fd, FILE_MAP_ALL_ACCESS, 0, 0, 10);
		if (NULL == notice_value)
		{
			printf("map view error: %d\n", GetLastError());
		}
	}

	//进程通信
	if (NULL != notice_value)
	{
		//通知其他退出
		printf("notic others quit\n");
		const short self_value = (*notice_value = (*notice_value + 1) % 10000);

		//等待通知
		printf("wait notice quit\n");
		while (self_value == *notice_value)
		{
			sleep_ms(50);
		}
	}

	//关闭句柄.
	if (NULL != notice_value)
	{
		UnmapViewOfFile((LPCVOID)notice_value);
		notice_value = NULL;
	}
	if (NULL != map_fd)
	{
		CloseHandle(map_fd);
		map_fd = NULL;
	}
	if (NULL != file_fd)
	{
		CloseHandle(file_fd);
		file_fd = NULL;
	}
#else
	//打开文件.
	int file_fd = open(mmap_file_path, O_RDWR | O_CREAT, 0744);
	if (-1 == file_fd)
	{
		printf("open error\n");
	}

	//获取文件信息.
	struct stat file_sb;
	if (-1 != file_fd)
	{
		fstat(file_fd, &file_sb);
	}

	//文件大小不足时，让它足
	if (-1 != file_fd && file_sb.st_size < sizeof(short))
	{
		printf("init file\n");

		short init_value = 0;
		write(file_fd, (char*)&init_value, sizeof(init_value));

		fstat(file_fd, &file_sb);
	}

	//映射至内存
	short* notice_value = NULL;
	if (-1 != file_fd)
	{
		notice_value = (short*)mmap(NULL, file_sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
	}

	//关闭文件
	if (-1 != file_fd)
	{
		close(file_fd);
	}

	//进程通信
	if (NULL != notice_value)
	{
		//通知其他进程退出
		const short self_value = (*notice_value = (*notice_value + 1) % 10000);

		//等待通知
		printf("wait notice\n");
		while (self_value == *notice_value)
		{
			sleep_ms(50);
		}
	}

	//关闭内存映射.
	if (NULL != notice_value)
	{
		munmap((void*)notice_value, sizeof(*notice_value));
	}
#endif // _WIN32

    //等待一会再退出，验证新、老进程谁能收到连接.
	printf("check new connect\n");
	sleep_ms(5000);

	//关闭工作线程
	g_worker_run = false;
	while (g_accept_count != 0)
	{
		sleep_ms(50);
	}

	//关闭监听socket
	close_socket(g_listen_socket_fd);
	g_listen_socket_fd = 0;

	//等待线程完成
	for (int join_index = 0; join_index < LISTEN_NUM; ++join_index)
	{
		pthread_join(worker_info[join_index], NULL);
	}
	pthread_mutex_destroy(&worker_mutex);

	//看一下线程退出情况
	printf("check thread quit\n");
	sleep_ms(5000);

	printf("quit...\n");

	return 0;
}

/* vim: set expandtab ts=4 sw=4 sts=4 tw=100: */
