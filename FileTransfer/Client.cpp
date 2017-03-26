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
fine-tune general interface
*/

using namespace boost::asio::ip;
using namespace boost::asio;

static std::unique_ptr<io_service> service_ptr;
static std::unique_ptr<tcp::socket> sock_ptr;

static tcp::endpoint ep;

static std::list<std::string> filepaths;

static std::mutex filepaths_lock;
static std::mutex wait_transfer;
static std::mutex socket_lock;
static volatile bool quit = false;
static volatile bool connected = false;

void connect_timeout(boost::system::error_code &connect_ec, const boost::system::error_code &ec);
void async_connect_done(boost::system::error_code &timout_ec, const boost::system::error_code &ec);
void connect_server();
void console_listener();
void send_listener();
void incoming_listener();
void add_filepath(const std::string &filepath);

int handle_commands(std::string cmd);
bool connect_to_server(std::string ip_str, std::string port_str);

static std::string cwd = boost::filesystem::current_path().string();

enum commands_enum
{
	EXIT,
	CWD,
	DIR,
	HELP,
	CONNECT,
	TRANSFER,
	EMPTY
};

std::array<std::string, 6> commands = { "exit", "cwd", "dir", "help", "connect", "transfer"};

template<typename T>
void print_vec(T vec, bool new_line = false)
{
	using namespace std;
	for (auto iter = vec.begin(); iter != vec.end(); advance(iter, 1))
	{
		cout << *iter;
		if (next(iter, 1) != vec.end())
			cout << ", ";
		if (new_line)
			cout << endl;
	}
	cout << endl;
}

int main()
{

	using namespace std;

	cwd += '\\';

	service_ptr = make_unique<io_service>();

	while (!quit)
	{
		std::thread console_thread(console_listener);
		std::thread send_thread(send_listener);
		std::thread incoming_thread(incoming_listener);

		console_thread.join();
		send_thread.join();
		incoming_thread.join();

		/*
		catch (boost::system::system_error e)
		{
			std::cout << e.what() << std::endl;
			std::cout << "Press any key to continue..." << std::endl;
			getc(stdin);
		}
		*/
	}

    return 0;
}

bool ip_valid(std::string ip_str)
{
	using namespace std;

	stringstream stream;

	vector<string> ip_split;

	boost::split(ip_split, ip_str, [](const char c) {return c == '.'; });

	if (ip_split.size() != 4)
	{
		cout << "Invalid format for ip address" << endl;
	}

	int ip_num;
	for (auto& num_str : ip_split)
	{
		stream.str("");
		stream.clear();
		stream << num_str;
		stream >> ip_num;
		if (!stream.eof())
		{
			cout << "Invalid format for IP address" << endl;
			return false;
		}
		if (ip_num < 0 && ip_num > 255)
		{
			cout << "Numbers in IP address must be between 0 and 255, inclusive" << endl;
			return false;
		}
	}
	return true;
}

bool port_valid(std::string port_str)
{
	using namespace std;

	stringstream stream;

	vector<string> ip_split;

	int port_val;

	stream << port_str;
	stream >> port_val;

	if (!stream.eof() || port_val < 0)
	{
		cout << "Invalid format for port number" << endl;
		return false;
	}
	else
	{
		return true;
	}
}

bool connect_to_server(std::string ip_str, std::string port_str)
{
	using namespace std;

	if (!ip_valid(ip_str))
		return false;
	if (!port_valid(port_str))
		return false;

	unsigned port_num = atoi(port_str.c_str());


	ep.address(address::from_string(ip_str));
	ep.port(port_num);

	try
	{
		boost::system::error_code timeout_ec, connect_ec;
		timeout_ec = connect_ec = boost::asio::error::would_block;

		socket_lock.lock();
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
			socket_lock.unlock();
			cout << "Connection to server timed out" << endl;
			return false;
		}
		if (connect_ec)
		{
			sock_ptr->close();
			sock_ptr.reset();
			service_ptr->stop();
			service_ptr.reset();
			service_ptr = make_unique<io_service>();
			socket_lock.unlock();
			cout << "Error connecting to server" << endl;
			return false;
		}

		socket_base::keep_alive option(true);
		sock_ptr->set_option(option);

	}
	catch (boost::system::system_error e)
	{
		std::cout << e.what() << std::endl;
		std::cout << "Press any key to continue..." << std::endl;
		getc(stdin);
	}

	socket_lock.unlock();
	return true;
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

std::vector<std::string> parse_command_no_quote(std::string command)
{
	using namespace std;

	vector<string> words;
	
	boost::trim_left(command);
	boost::trim_right(command);

	if (command.size() == 0)
	{
		return{};
	}

	boost::split(words, command, [](const char &c) {return c == ' '; }, boost::token_compress_on);

	return words;
}

std::vector<std::string> combine_quotes(std::string command)
{
	using namespace std;

	std::vector<std::string> words;

	string curr_word = "";
	bool in_quotes = false;

	for (auto iter = command.begin(); iter != command.end(); advance(iter, 1))
	{
		if (*iter == '"')
		{
			if (in_quotes)
			{
				curr_word += *iter;
				if (curr_word.size() > 2)
					words.push_back(curr_word);
				curr_word = "";
			}
			else
			{
				if (curr_word.size() > 0)
				{
					words.push_back(curr_word);
				}
				curr_word = "\"";
			}
			in_quotes = !in_quotes;
		}
		else
			curr_word += *iter;
	}
	if (curr_word.size() > 0 && (curr_word.size() != 1 || curr_word.at(0) != '"'))
		words.push_back(curr_word);

	return words;
}

std::vector<std::string> parse_command(std::string command)
{
	using namespace std;

	vector<string> words = combine_quotes(command);

	vector<unsigned> idxes_replace;
	for (unsigned i = 0; i < words.size(); i++)
	{
		if (words.at(i).size() > 0 && words.at(i).at(0) != '"')
		{
			idxes_replace.push_back(i);
		}
	}

	reverse(idxes_replace.begin(), idxes_replace.end());

	for (auto i : idxes_replace)
	{
		auto new_words_i = parse_command_no_quote(words.at(i));
		words.erase(next(words.begin(), i));
		for (auto r_iter = new_words_i.rbegin(); r_iter != new_words_i.rend(); advance(r_iter, 1))
			words.insert(next(words.begin(), i), *r_iter);
	}

	for (auto &str : words)
	{
		if (str.front() == '"')
		{
			str.erase(str.begin());
		}
		if (str.back() == '"')
		{
			str.erase(next(str.end(), -1));
		}
	}

	return words;
}



/*
filepaths_lock must be held when entering this function
*/
int handle_commands(std::string command)
{

	using namespace std;

	vector<string> words = parse_command(command);
	
	if (words.size() == 1 && words.front().size() == 0)
	{
		return EMPTY;
	}

	auto found_iter = find(commands.begin(), commands.end(), words.front());
	if (found_iter == commands.end())
	{
		cout << "Unkown command: " << words.front() << endl;
		return -1;
	}
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
		case (CONNECT):
		{
			if (words.size() != 3)
			{
				cout << "connect command takes two parameters: <ip> <port>" << endl;
				break;
			}
			if (!connect_to_server(words.at(1), words.at(2)))
			{
				cout << "Failed to connect to server" << endl;
			}
			else
			{
				cout << "Successfully connected to server" << endl;
			}
		}
		break;
		case (TRANSFER):
		{
			if (words.size() < 2)
			{
				cout << "transfer command needs at least one argument" << endl;
				break;
			}

			for (auto iter = next(words.begin(), 1); iter != words.end(); advance(iter, 1))
			{
				filepaths.push_back(cwd + *iter);
			}
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
		filepaths_lock.lock();
		if (filepaths.size() > 0)
		{
			filepaths_lock.unlock();
			continue;
		}
		std::string filepath = "";
		std::cout << "> ";
		std::getline(std::cin, filepath);
		//std::cin.ignore(std::numeric_limits<unsigned>().max(), '\n');
		if (filepath.size() > 0)
		{
			int command_result = handle_commands(filepath);
			/*
			if (command_result != -1)
			{
				continue;
			}
			filepaths_lock.lock();
			filepaths.push_back(cwd + filepath);
			filepaths_lock.unlock();
			*/
		}
		filepaths_lock.unlock();
	}
}

void send_listener()
{
	while (!quit)
	{
		socket_lock.lock();
		if (!sock_ptr)
		{
			socket_lock.unlock();
			continue;
		}

		filepaths_lock.lock();
		while (filepaths.size() > 0)
		{

			std::ifstream ifs(filepaths.front(), std::ios::binary);

			if (!ifs.good())
			{
				std::cout << "Invalid filepath specified: file not found" << std::endl;
				std::cout << "You entered: |" << filepaths.front() << "|" << std::endl;
				filepaths.pop_front();
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
		socket_lock.unlock();
		filepaths_lock.unlock();
	}
}

void incoming_listener()
{
	while (!quit)
	{
		socket_lock.lock();
		if (!sock_ptr)
		{
			socket_lock.unlock();
			continue;
		}
		if (sock_ptr->available())
		{
			char read_buf[512];
			sock_ptr->read_some(buffer(read_buf, 512));
			std::cout << read_buf << std::endl;
		}
		socket_lock.unlock();
	}
}

void add_filepath(const std::string &filepath)
{
	filepaths_lock.lock();
	filepaths.push_back(filepath);
	filepaths_lock.lock();
}