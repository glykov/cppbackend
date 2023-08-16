#ifdef WIN32
#include <sdkddkver.h>
#endif
// boost.beast будет использовать std::string_view вместо boost::string_view
#define BOOST_BEAST_USE_STD_STRING_VIEW

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <thread>
#include <optional>

namespace net = boost::asio;
using tcp = net::ip::tcp;
using namespace std::literals;
namespace beast = boost::beast;
namespace http = beast::http;
using string_request = http::request<http::string_body>;
using string_response = http::response<http::string_body>;

std::optional<string_request> read_request(tcp::socket& socket, beast::flat_buffer& buffer) {
    beast::error_code ec;
    string_request req;

    http::read(socket, buffer, req, ec);

    if (ec == http::error::end_of_stream) {
        return std::nullopt;
    }
    if (ec) {
        throw std::runtime_error("Failed to read request: "s.append(ec.message()));
    }

    return req;
}

string_response make_string_response(http::status status, std::string_view body,
                                     unsigned http_version, bool keep_alive,
                                     std::string_view content_type = "text/html"sv) {
    string_response response(status, http_version);
    response.set(http::field::content_type, content_type);
    response.body() = body;
    response.content_length(body.size());
    response.keep_alive(keep_alive);

    return response;
}

string_response handle_request(string_request&& request) {
    string_response response;
    
    if (request.method() != http::verb::get && request.method() != http::verb::head) {
        response = make_string_response(http::status::method_not_allowed, "Invalid method"sv, request.version(),
                                        request.keep_alive());
        response.set(http::field::allow, "GET, HEAD"sv);
    }
    else {
        response = make_string_response(http::status::ok, "Hello, "s.append(request.target().substr(1)),
                                        request.version(), request.keep_alive());
    };

    return response;
}

void handle_connection(tcp::socket& socket) {
    try {
        beast::flat_buffer buffer;

        while (auto request = read_request(socket, buffer)) {
            string_response response = handle_request(*std::move(request));
            response.set(http::field::content_type, "text/html"sv);

            http::write(socket, response);

            if (response.need_eof()) {
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    beast::error_code  ec;
    socket.shutdown(tcp::socket::shutdown_send, ec);
}

int main() {
    // Выведите строчку "Server has started...", когда сервер будет готов принимать подключения
    net::io_context io_context;
    const auto address = net::ip::make_address("0.0.0.0");
    const unsigned short port = 8080;

    tcp::acceptor acceptor(io_context, tcp::endpoint{address, port});
    std::cout << "Server has started..."sv << std::endl;

    while (true) {
        tcp::socket socket(io_context);
        acceptor.accept(socket);
        std::thread t(
                [] (tcp::socket socket) {
                    handle_connection(socket);
                },
                std::move(socket));
        t.detach();
    }
}
