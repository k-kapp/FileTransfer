#include <iostream>
#include <memory>
#include <list>
#include <thread>
#include <fstream>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;

static io_service service;

static tcp::acceptor acceptor(service, tcp::endpoint(tcp::v4(), 8001));

static std::list<std::shared_ptr<tcp::socket> > sock_ptr_list;

static const std::string write_dir = "repl_files/";

static const char ok_char = 'K';

void accept_connects(int n);
void accept_incoming(int n);

int main()
{
	std::thread connects_thread(accept_connects, 0);
	std::thread incoming_thread(accept_incoming, 0);

	connects_thread.join();
	incoming_thread.join();


	std::cout << "Press any key to continue" << std::endl;
	getc(stdin);

    return 0;
}

void accept_connects(int n)
{
	while (true)
	{
		auto socket_ptr = std::make_shared<tcp::socket>(service);

		acceptor.accept(*socket_ptr);

		sock_ptr_list.push_back(socket_ptr);

		std::cout << "socket accepted" << std::endl;
	}
}

void accept_incoming(int n)
{
	while (true)
	{
		for (auto sock_ptr : sock_ptr_list)
		{
			unsigned available_num;
			if (available_num = sock_ptr->available())
			{
				char buf[512];
				char num_char_buf[sizeof(unsigned) / sizeof(char)];
				char filename_buf[256];
				char char_buf = ' ';
				unsigned pos_count = 0;
				while (true)
				{
					sock_ptr->read_some(buffer(&char_buf, sizeof(char)));

					// if there is only one byte sent, and this byte is a null character, then
					// we assume that the client is pinging us => send the ok_char message back
					if (available_num == 1 && char_buf == 0)
					{
						while (!sock_ptr->write_some(buffer(&ok_char, sizeof(char)))) {}
					}
					if (char_buf == '&')
						break;
					num_char_buf[pos_count] = char_buf;
					pos_count++;
				}
				unsigned file_buf_size;
				memcpy(&file_buf_size, num_char_buf, sizeof(unsigned));

				pos_count = 0;
				while (true)
				{
					sock_ptr->read_some(buffer(&char_buf, sizeof(char)));
					if (char_buf == '&')
						break;
					filename_buf[pos_count] = char_buf;
					pos_count++;
				}
				filename_buf[pos_count] = '\0';
				pos_count = 0;

				char * file_buf = new char[file_buf_size / sizeof(char)];
				unsigned bytes_read = 0;
				
				while (bytes_read < file_buf_size)
				{
					bytes_read += sock_ptr->read_some(buffer(file_buf + bytes_read, file_buf_size - bytes_read));
				}

				if (!filesystem::exists(write_dir))
				{
					filesystem::create_directory(write_dir);
				}

				std::ofstream ofs(write_dir + std::string(filename_buf), std::ios::binary);

				unsigned idx = 0;
				unsigned fifth = (file_buf_size / sizeof(char)) / 5;

				while (idx < file_buf_size / sizeof(char))
				{
					if ((idx + 1) % fifth == 0)
					{
						std::cout << "Creating new file: " << (float)(idx + 1) / (file_buf_size / sizeof(char)) * 100 << "% done" << std::endl;
					}
					ofs.put(file_buf[idx]);
					idx++;
				}
				//std::cout << "Creating new file: 100% done" << std::endl;

				delete file_buf;
			}
		}
	}
}