#include <cpprest/http_listener.h>
#include <cpprest/filestream.h>
#include <cpprest/http_client.h>
#include <iostream>
#include <fstream>

using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::experimental::listener;
using namespace utility;
using namespace concurrency::streams;

const std::string local_root = "E:\\workspace\\http_proxy\\cache\\wsevlf001.eeo.im";

const utility::string_t static_remote_server = U("https://wsevlf001.eeo.im");
const utility::string_t dynamic_remote_server = U("https://wdevlf001.eeo.im");

bool local_file_exists(const utility::string_t& path) {
    std::string convertedPath = utility::conversions::to_utf8string(path);
    std::ifstream file(local_root + convertedPath);
    return file.good();
}

void serve_local_file(http_request request, const std::string& path) {
    auto file_path = utility::conversions::to_string_t(local_root + path);
    auto fileStream = std::make_shared<ostream>();
    
    pplx::task<void> requestTask = fstream::open_istream(file_path).then([=](istream fileStream) {
        request.reply(status_codes::OK, fileStream, U("text/html"))
        .then([=](pplx::task<void> t) {
            try {
                t.get();
            } catch (...) {
                request.reply(status_codes::InternalError, U("Internal Server Error"));
            }
        });
    });

    try {
        requestTask.wait();
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
        request.reply(status_codes::InternalError, U("Internal Server Error"));
    }
}

void forward_request_to_remote(http_request request) {
    http_client_config client_config;
    client_config.set_validate_certificates(false);

    http_client client(request.method() != methods::GET ? dynamic_remote_server : static_remote_server, client_config);
    uri_builder builder(request.relative_uri());

    http_request new_request(request.method());
    new_request.set_request_uri(builder.to_uri());

    for (const auto& header : request.headers()) {
        if (header.first != U("Host")) {
            new_request.headers().add(header.first, header.second);
        }
    }

    if (request.method() != methods::GET) {
        new_request.set_body(request.extract_string().get());
    }

    client.request(new_request).then([=](http_response response) {
        return request.reply(response);
    }).then([=](pplx::task<void> t) {
        try {
            t.get();
        }
        catch (const http_exception& e) {
            std::wcerr << U("HTTP Exception: ") << e.what() << std::endl;
            request.reply(status_codes::InternalError, U("Internal Server Error"));
        }
        catch (...) {
            request.reply(status_codes::InternalError, U("Internal Server Error"));
        }
    });
}

void handle_get(http_request request) {
    auto path = uri::decode(request.relative_uri().path());
    std::replace(path.begin(), path.end(), '/', '\\');

    if (local_file_exists(path)) {
        serve_local_file(request, utility::conversions::to_utf8string(path));
    } else {
        forward_request_to_remote(request);
    }
}

int main() {
    uri_builder uri(U("http://localhost:8080"));
    auto addr = uri.to_uri().to_string();
    http_listener listener(addr);

    listener.support(methods::GET, handle_get);
    listener.support(methods::POST, forward_request_to_remote);
    listener.support(methods::OPTIONS, forward_request_to_remote);

    try {
        listener
            .open()
            .then([&listener]() { std::cout << "Starting to listen at: " << utility::conversions::to_utf8string(listener.uri().to_string()) << std::endl; })
            .wait();

        std::string line;
        std::getline(std::cin, line);
    } catch (const std::exception& e) {
        std::cerr << "An error occurred: " << e.what() << std::endl;
    }

    return 0;
}