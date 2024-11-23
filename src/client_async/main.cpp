#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <chrono>
#include <boost/asio.hpp>
#include <boost/endian/arithmetic.hpp>
#include "transport.h"

#include "request_generated.h"
#include <flatbuffers/util.h>
#include <flatbuffers/idl.h>

namespace Homework
{

using namespace std::chrono_literals;

class BoostClient
{
    using NetSize  = boost::endian::big_uint64_t;

private:
    boost::asio::thread_pool ioc;

private:
    using Sock = boost::asio::deferred_t::as_default_on_t<boost::asio::ip::tcp::socket>;
    using Resolver = boost::asio::deferred_t::as_default_on_t<boost::asio::ip::tcp::resolver>;

    boost::asio::awaitable<Sock> ConnectAsync(std::string_view host, std::string_view port)
    {
        Sock s{ioc};
        co_await async_connect(s, co_await Resolver(ioc).async_resolve(host, port));
        co_return s;
    }

    std::pair<char*, std::size_t> MakeFbCommand(const std::string & json)
    {
        flatbuffers::Parser request_parsr;
        std::string schemafile;
        if (!flatbuffers::LoadFile("./resources/request.fbs", false, &schemafile)) {
            std::cerr << "Loading fbs file failed!" << std::endl;
            return {nullptr, 0};
        }

        flatbuffers::Parser parser;
        parser.Parse(schemafile.c_str());
        if (!parser.Parse(json.c_str())) { 
            std::cerr << "Parsing json string failed!" << std::endl;
            return {nullptr, 0};
        }

        int fb_data_size = (int)parser.builder_.GetSize();
        std::size_t network_data_size = fb_data_size + sizeof(int);
        char * network_buff = new char[network_data_size];
        std::memcpy(network_buff, &fb_data_size, sizeof(fb_data_size));
        std::memcpy(network_buff + sizeof(fb_data_size), parser.builder_.GetBufferPointer(), fb_data_size);
        return {network_buff, network_data_size};
    }


public: 

    boost::asio::awaitable<void> ConnectAndSendAsync(std::string_view address, std::string_view port) {
        auto socket = co_await ConnectAsync(address, port);
        
        std::string json = R"({"operation":"set","member":{"key":"k3","value":"v3"}})";
        auto [content_data_ptr, content_size] = MakeFbCommand(json);
        if (content_data_ptr != nullptr)  {

            auto [ec, n] = co_await boost::asio::async_write(socket, 
                                                            boost::asio::buffer(content_data_ptr, content_size), 
                                                            boost::asio::as_tuple(boost::asio::use_awaitable));
            if (ec) {
                std::cerr << "Can't send flat buffer with encoded json [" << json << "\n";
                std::cerr << "\t" << ec.message() << "\n";
            }

            delete [] content_data_ptr;
        }
        std::this_thread::sleep_for(1000ms);
    }
};

}

int main (int argc, char* argv[]) {

    Homework::BoostClient c;
    //boost::asio::io_context io_context;
    //boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    //signals.async_wait([&] (auto, auto) { std::cerr << "Stopped"; io_context.stop(); });

    boost::asio::thread_pool ioc;
    boost::asio::co_spawn(ioc, c.ConnectAndSendAsync("127.0.0.1", "7000"), boost::asio::detached);
    ioc.join();

    return 0;
}



