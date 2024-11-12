#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_tiemer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

using boost::asio::ip::tcp;
using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::resirect_error;
using boost::asio::use_awaitable;

namespace sas_boost
{
class Client
{
private :
  boost::asio::thread_pool ioc{1};

private:
  using Sock = boost::asio::deferred_t::as_default_on_t<boost::asio::ip::tcp::socket>;
  using Resolver = boost::asio::deferred_t::as_default_on_t<boost::asio::ip::tcp::resolver>;

  boost::asio::awaitable<Sock> Connect(std::string host, unsigned short port)
  {
    Sock s{ioc};
    co_await async_connect(s, co_await Resolver(ioc).async_resolve(host, port));
    co_return s;
  }

  boost::asio::awaitable<void> SendJson(Sock& s, const std::string filePath)
  {
  }
public: 
  void ConnectAndSend(const std::string & address, unsigned short port)
  {
    return co_spawn(ioc, [=, this]() mutable -> boost::asio::awaitable<void> {
      auto s = co_await Connect(address);
      co_await SendJson(s, jsonPath);
    }, boost::asio::use_future).get();
  }


};

}

int main (int argc; char* argv[])
{
  sas_boost::Client c;
  c.ConnectAndSend("127.0.0.1");
  return 0;
}



