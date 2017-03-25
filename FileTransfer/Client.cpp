#include <string>
#include <thread>
#include <iostream>
#include <list>
#include <mutex>
#include <cstdio>
#include <fstream>
#include <memory>
#include <functional>
#include <array>

#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>

/*
TODO: 
 - make connect and transfer commands, instead of entering <ip> natively
*/

using namespace boost::asio::ip;
using namespace boost::asio;

//static boost::asio::io_service service_ptr;
static std::unique_ptr<io_service> service_ptr;
static std::unique_ptr<tcp::socket> sock_ptr;

//static tcp::endpoint ep(address::from_string("127.0.0.1"), 8001);
//static tcp::endpoint ep(address::from_string("192.168.1.7"), 8001);
static tcp::endpoint ep;

static std::list<std::string> filepaths;

static std::mutex filepaths_lock;
static std::mutex wait_transfer;
static volatile bool quit = false;
static volatile bool connected = false;

void connect_timeout(boost::system::error_code &connect_ec, const boost::system::error_code &ec);
void async_connect_done(boost::system::error_code &timout_ec, const boost::system::error_code &ec);
//void connect_timeout(const boost::system::error_code &ec);
//void async_connect_done(const boost::system::error_code &ec);
void connect_server();
void console_listener();
void send_listener();
void incoming_listener();

int handle_commands(std::string cmd);

static std::string cwd = boost::filesystem::current_path().string();

enum commands_enum
{
	EXIT,
	CWD,
	DIR,
	HELP,
	EMPTY
};

std::array<std::string, 4> commands = { "exit", "cwd", "dir", "help"};

template<typename T>
void print_vec(T vec)
{
	using namespace std;
	for (auto& val : vec)
	{
		cout << val << " ";
	}
	cout << endl;
}

int main()
{

	using namespace std;

	cwd += '\\';

	service_ptr = make_unique<io_service>();

	cout << "Enter <Host ip address> <Port>. Type 'exit' to quit" << endl;

	while (!quit)
	{
		string ip_addr;
		string port_num_str;
		string server_info;

		int port_num;

		cout << "> ";
		std::getline(std::cin, server_info);

		int command_result = handle_commands(server_info);

		if (command_result != -1)
			continue;

		string::iterator str_iter = server_info.begin();
		while (str_iter != server_info.end() && std::isgraph(*str_iter))
		{
			ip_addr += *str_iter;
			advance(str_iter, 1);
		}

		if (str_iter != server_info.end() && !isgraph(*str_iter))
			advance(str_iter, 1);

		while (str_iter != server_info.end())
		{
			if (!isgraph(*str_iter))
			{
				continue;
			}
			else if (isalpha(*str_iter))
			{
				cout << "<Port> parameter must be a number" << endl;
				continue;
			}
			port_num_str += *str_iter;
			advance(str_iter, 1);
		}

		port_num = atoi(port_num_str.c_str());


		if (port_num < 0)
		{
			cout << "Invalid port number" << endl;
			continue;
		}

		ep.address(address::from_string(ip_addr));
		ep.port(port_num);

		try
		{
			boost::system::error_code timeout_ec, connect_ec;
			timeout_ec = connect_ec = boost::asio::error::would_block;

			sock_ptr = make_unique<tcp::socket>(*service_ptr);

			deadline_timer timer(*service_ptr);
			timer.expires_from_now(boost::posix_time::seconds(3));

			auto bind_timer = bind(connect_timeout, std::ref(timeout_ec), std::placeholders::_1);
			auto bind_connect = bind(async_connect_done, std::ref(connect_ec), std::placeholders::_1);

			timer.async_wait(bind_timer);
			sock_ptr->async_connect(ep, bind_connect);

			while ((timeout_ec == error::would_block) && (connect_ec == error::would_block))
			{
				service_ptr->run_one();
			}

			cout << "current seconds: " << boost::posix_time::second_clock::local_time().time_of_day().seconds() << endl;
			cout << "timer cancels at:  " << timer.expires_at().time_of_day().seconds() << endl;
			timer.cancel();

			if (timeout_ec != error::would_block)
			{
				sock_ptr->close();
				sock_ptr.reset();
				service_ptr->stop();
				service_ptr.reset();
				service_ptr = make_unique<io_service>();
				cout << "Connection to server timed out" << endl;
				continue;
			}
			if (connect_ec)
			{
				sock_ptr->close();
				sock_ptr.reset();
				service_ptr->stop();
				service_ptr.reset();
				service_ptr = make_unique<io_service>();
				cout << "Error connecting to server" << endl;
				continue;
			}

			cout << "Connected to server." << endl;

			socket_base::keep_alive option(true);
			sock_ptr->set_option(option);

			std::thread console_thread(console_listener);
			std::thread send_thread(send_listener);
			std::thread incoming_thread(incoming_listener);

			console_thread.join();
			send_thread.join();
			incoming_thread.join();
			break;
		}
		catch (boost::system::system_error e)
		{
			std::cout << e.what() << std::endl;
			std::cout << "Press any key to continue..." << std::endl;
			getc(stdin);
		}
	}


    return 0;
}

void print_curr_dir()
{
	using namespace std;
	cout << "Current working directory: ";
	cout << cwd << endl;
}

std::string proc_change_wd(std::string candidate)
{
	boost::trim_left(candidate);
	boost::trim_right(candidate);

	while (candidate.front() == '\\' || candidate.front() == '/')
	{
		candidate.erase(candidate.begin());
	}
	
	if (candidate.size() > 1 && candidate.at(1) == ':')
	{
		return candidate;
	}
	else
	{
		return cwd + candidate;
	}
}

void change_cwd(std::string candidate_dir)
{
	using namespace std;
	if (boost::filesystem::is_directory(candidate_dir))
	{
		cwd = candidate_dir;
		cwd = boost::filesystem::canonical(cwd).string();
		cwd += '\\';
	}
	else
	{
		cout << "Invalid path specified for current working directory" << endl;
	}
}

void list_subdirectories()
{
	using namespace boost::filesystem;

	path p(cwd);

	for (auto &dir : boost::make_iterator_range(directory_iterator(p)))
	{
		if (!is_regular_file(dir))
			std::cout << dir.path().string() << std::endl;
	}
}

void list_files()
{
	using namespace boost::filesystem;

	path p(cwd);

	for (auto &file : boost::make_iterator_range(directory_iterator(p)))
	{
		if (is_regular_file(file))
			std::cout << file.path().string() << std::endl;
	}
}

int handle_commands(std::string command)
{
	/*
	TODO: provide for directory names that have spaces in 
	*/

	using namespace std;

	vector<string> words;
	
	boost::trim_left(command);
	boost::trim_right(command);

	boost::split(words, command, [](const char &c) {return c == ' '; }, boost::token_compress_on);
	if ((words.front() == "cwd" || words.front() == "ccwd") && words.size() > 1)
	{
		vector<string> new_vec(next(words.begin(), 1), words.end());
		string candidate_dir = boost::algorithm::join(new_vec, " ");
		words.erase(next(words.begin(), 1), words.end());
		words.push_back(candidate_dir);
	}

	if (words.size() == 1 && words.front().size() == 0)
	{
		return EMPTY;
	}

	print_vec(words);

	auto found_iter = find(commands.begin(), commands.end(), words.front());
	if (found_iter == commands.end())
		return -1;
	unsigned idx = distance(commands.begin(), found_iter);
	switch (idx)
	{
		case (EXIT):
		{
			if (words.size() > 1)
				cout << "exit command does not take any arguments" << endl;
			else
				quit = true;
		}
		break;
		case (CWD):
		{
			if (words.size() == 1)
			{
				print_curr_dir();
			}
			else
			{
				change_cwd(proc_change_wd(words.back()));
			}
		}
		break;
		case (DIR):
		{
			if (words.size() > 1)
			{
				cout << "dir does not take any arguments" << endl;
			}
			else
			{
				list_subdirectories();
				list_files();
			}
		}
		break;
		case (HELP):
		{
			cout << "Enter <Host ip address> <Port>. Type 'exit' to quit" << endl;
		}
		break;
	}

	return idx;
}

void connect_timeout(boost::system::error_code &timeout_ec, const boost::system::error_code &ec)
{
	using namespace std;

	cout << "Timeout function called" << endl;

	timeout_ec = ec;
	sock_ptr->close();
}

void async_connect_done(boost::system::error_code &connect_ec, const boost::system::error_code &ec)
{
	using namespace std;
	connect_ec = ec;
}

void connect_server()
{
	using namespace std;

	sock_ptr = make_unique<tcp::socket>(*service_ptr);

	sock_ptr->connect(ep);
}

void console_listener()
{
	//getc(stdin);
	while (!quit)
	{
		if (filepaths.size() > 0)
			continue;
		std::string filepath = "";
		std::getline(std::cin, filepath);
		//std::cin.ignore(std::numeric_limits<unsigned>().max(), '\n');
		if (filepath.size() > 0)
		{
			int command_result = handle_commands(filepath);
			if (command_result != -1)
			{
				continue;
			}

			filepaths_lock.lock();
			filepaths.push_back(cwd + filepath);
			filepaths_lock.unlock();
		}
	}
}

void send_listener()
{
	while (!quit)
	{
		filepaths_lock.lock();
		if (filepaths.size() > 0)
		{

			std::ifstream ifs(filepaths.front(), std::ios::binary);

			if (!ifs.good())
			{
				std::cout << "Invalid filepath specified: file not found" << std::endl;
				std::cout << "You entered: |" << filepaths.front() << "|" << std::endl;
				filepaths.pop_front();
				filepaths_lock.unlock();
				continue;
			}

			ifs.seekg(0, ifs.end);
			unsigned buf_size = ifs.tellg();
			char * buf = new char[buf_size];

			ifs.seekg(0, 0);

			unsigned idx = 0;
			unsigned fifth = buf_size / 5;

			std::cout << "Reading file " << filepaths.front() << "..." << std::endl;

			while (idx < buf_size)
			{
				ifs.get(buf[idx]);
				if ((idx + 1) % fifth == 0)
					std::cout << (float)(idx + 1) / buf_size * 100 << "% done" << std::endl;
				idx++;
			}

			unsigned write_buf_size = sizeof(unsigned) / sizeof(char) + 1 + filepaths.front().size() + 1 + buf_size;
			char * write_buf = new char[write_buf_size];

			std::cout << "file buffer size: " << buf_size << std::endl;

			char * bufsize_ptr = new char[sizeof(unsigned) / sizeof(char)];
			memcpy(bufsize_ptr, &buf_size, sizeof(unsigned));
			char and_sign = '&';

			unsigned pos = 0;
			
			memcpy(write_buf + pos, bufsize_ptr, sizeof(unsigned));
			pos = sizeof(unsigned) / sizeof(char);
			memcpy(write_buf + pos, &and_sign, sizeof(char));
			pos += 1;
			memcpy(write_buf + pos, filepaths.front().c_str(), filepaths.front().size() * sizeof(char));
			pos += filepaths.front().size();
			memcpy(write_buf + pos, &and_sign, sizeof(char));
			pos += 1;
			memcpy(write_buf + pos, buf, buf_size * sizeof(char));

			unsigned to_be_written = write_buf_size * sizeof(char);

			try
			{
				while (to_be_written)
					to_be_written -= sock_ptr->write_some(buffer(write_buf, to_be_written));
			}
			catch (boost::system::system_error e)
			{
				std::cout << e.what() << std::endl;
			}

			std::cout << std::endl;

			filepaths.pop_front();
		}
		filepaths_lock.unlock();
	}
}

void incoming_listener()
{
	while (!quit)
	{
		if (sock_ptr->available())
		{
			char read_buf[512];
			sock_ptr->read_some(buffer(read_buf, 512));
			std::cout << read_buf << std::endl;
		}
	}
}