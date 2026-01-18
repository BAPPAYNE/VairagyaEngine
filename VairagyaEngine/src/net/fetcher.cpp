#include "net/fetcher.h"

#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/url.hpp>
#include <boost/url/authority_view.hpp>
#include <string>
#include <cstdlib>
#include <iostream>

using namespace std;
namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace urls = boost::urls;

namespace net {
	FetchResult fetch(const string& uri_str, int timeout_ms) {
		// Placeholder implementation
		try {
			urls::url_view url(uri_str);
			if (url.host().empty()) {
				throw std::invalid_argument("Invalid URL: Missing host");
			}

			string host = string(url.authority().host());
			string port = url.has_port() ? string(url.port()) : "80";
			string target = url.encoded_path().empty() ? "/" : string(url.encoded_path());
			if (url.has_query()) {
				target += "?" + string(url.encoded_query());
			}
			// I/O context
			asio::io_context ioc;
			tcp::resolver resolver(ioc);
			auto const results = resolver.resolve(host, port); // DNS resolution

			beast::tcp_stream stream(ioc);
			stream.connect(results); // Connect to server

			http::request<http::string_body> req{ http::verb::get, target, 11 };
			req.set(http::field::host, host);
			req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

			http::write(stream, req); // Send HTTP request

			beast::flat_buffer buffer; // Buffer for reading

			http::response<http::string_body> res; // Response object

			http::read(stream, buffer, res); //Receive the response\

			stream.socket().shutdown(tcp::socket::shutdown_both); // Gracefully close the socket
			FetchResult result;
			result.status = FetchStatus::SUCCESS;
			result.content = res.body();
			result.http_code = res.result_int();
			return result;
			
		}
		catch (const std::exception& e) {
			FetchResult result;
			result.status = FetchStatus::FAILED;
			result.content = "";
			result.http_code = 0;
			return result;
		}
	}
};