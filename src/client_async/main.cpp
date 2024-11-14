#include <iostream>
#include <string>
#include <string_view>
#include <boost/asio.hpp>
#include <boost/endian/arithmetic.hpp>
#include <array>
#include "transport.h"

#include "request_generated.h"
#include <flatbuffers/util.h>
#include <flatbuffers/idl.h>

namespace Homework
{

class BoostClient
{
    using NetSize  = boost::endian::big_uint64_t;

private:
    boost::asio::thread_pool ioc{1};

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
        std::size_t network_data_size = sizeof(char) * fb_data_size  + sizeof(int);
        char * buff = new char[network_data_size];
        std::memcpy(buff, &fb_data_size, sizeof(fb_data_size));
        std::memcpy(buff + sizeof(fb_data_size), parser.builder_.GetBufferPointer(), fb_data_size);
        return {buff, fb_data_size};
    }

    boost::asio::awaitable<bool> SendJsonAsync(Sock& socket, const std::string & json)
    {
        auto [content_data_ptr, content_size] = MakeFbCommand(json);
        if (content_data_ptr == nullptr) {
            co_return false;
        }
        co_await async_write(socket, boost::asio::buffer(content_data_ptr, sizeof(content_size)), boost::asio::use_awaitable);
        co_return true;
    }

public: 

    bool ConnectAndSendAsync(std::string_view address, std::string_view port)
    {
        std::promise<bool> p;
        std::future<bool> f = p.get_future();

        co_spawn(ioc, [=, this]() mutable -> boost::asio::awaitable<bool> {
            auto s = co_await ConnectAsync(address, port);
            bool r = co_await SendJsonAsync(s, R"({"operation":"set","member":{"key":"k3","value":"v3"}})");
            co_return r;
        },
        [p = std::move(p)] (std::exception_ptr e , bool r) mutable {
            if(e) p.set_exception(e); else p.set_value(r);
        });
        return f.get();
    }
};

}

int main (int argc, char* argv[]) {
    Homework::BoostClient c;
    bool res = c.ConnectAndSendAsync("127.0.0.1", "7777");
    std::cout << "Data sent : " << std::boolalpha << res << std::endl; 
    return 0;
}



