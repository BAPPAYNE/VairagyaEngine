#include "net/fetcher.h"

#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <boost/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/url.hpp>
#include <boost/url/authority_view.hpp>
#include <string>
#include <cstdlib>
#include <iostream>

using namespace std;
namespace beast = boost::beast;
namespace ssl = boost::asio::ssl;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace urls = boost::urls;

namespace net {
	FetchResult fetch(const string& uri_str, int timeout_ms) {
		// Placeholder implementation
		try {
			auto parsed = urls::parse_uri(uri_str);
			if (!parsed) {
				throw std::invalid_argument("Invalid URL");
			}

			urls::url_view url = *parsed;
			if (url.host().empty()) {
				throw std::invalid_argument("Invalid URL: Missing host");
			}

			bool is_https = (url.scheme() == "https");

			string host = string(url.host());
			string port = url.has_port() ? string(url.port()) : (is_https ? "443" : "80");
			string target = url.encoded_path().empty() ? "/" : string(url.encoded_path());
			
			if (url.has_query()) {
				target += "?" + string(url.encoded_query());
			}

			// I/O context
			asio::io_context ioc;
			tcp::resolver resolver(ioc);
			auto const results = resolver.resolve(host, port); // DNS resolution

			http::request<http::string_body> req{ http::verb::get, target, 11 };
			req.set(http::field::host, host);
			req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

			beast::flat_buffer buffer;
			http::response<http::string_body> res;

			// HTTP
			if (!is_https) {
				beast::tcp_stream stream(ioc);
				stream.connect(results);

				http::write(stream, req);
				http::read(stream, buffer, res);

				beast::error_code ec;
				stream.socket().shutdown(tcp::socket::shutdown_both, ec);
			}
		
		// HTTPS
			else {
				ssl::context ctx{ ssl::context::tlsv12_client };
				ctx.set_default_verify_paths();
				ctx.set_verify_mode(ssl::verify_none); // crawler-style

				beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

				// 🔥 REQUIRED: SNI
				if (!SSL_set_tlsext_host_name(
					stream.native_handle(),
					host.c_str())) {
					throw std::runtime_error("SNI failed");
				}

				beast::get_lowest_layer(stream).connect(results);
				stream.handshake(ssl::stream_base::client);

				http::write(stream, req);
				http::read(stream, buffer, res);

				beast::error_code ec;
				stream.shutdown(ec); // ignore EOF
			}

			return {
				FetchStatus::SUCCESS,
				res.body(),
				static_cast<uint16_t>(res.result_int())
			};			
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