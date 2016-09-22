//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/get.hpp>

#include <beast/unit_test/suite.hpp>
#include <boost/utility/string_ref.hpp>

#include <beast/http.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>
#include <functional>

namespace beast {
namespace http {

using resolver_type =
    boost::asio::ip::tcp::resolver;

using query_type =
    resolver_type::query;

using socket_type =
    boost::asio::ip::tcp::socket;

using endpoint_type = socket_type::endpoint_type;

class url
{
public:
    explicit
    url(std::string const& s)
    {
        parse(s);
    }

    std::string protocol;
    std::string host;
    std::string path;
    std::string query;

private:
    void
    parse(std::string const& s)
    {
        using namespace std;
        const string prot_end("://");
        string::const_iterator prot_i = search(
            s.begin(), s.end(), prot_end.begin(), prot_end.end());
        protocol.reserve(distance(s.begin(), prot_i));
        transform(s.begin(), prot_i,
                  back_inserter(protocol),
                  ptr_fun<int,int>(tolower)); // protocol is icase
        if(prot_i == s.end() )
            return;
        advance(prot_i, prot_end.length());
        string::const_iterator path_i = find(prot_i, s.end(), '/');
        host.reserve(distance(prot_i, path_i));
        transform(prot_i, path_i,
                  back_inserter(host),
                  ptr_fun<int,int>(tolower)); // host is icase
        string::const_iterator query_i = find(path_i, s.end(), '?');
        path.assign(path_i, query_i);
        if(query_i != s.end() )
            ++query_i;
        query.assign(query_i, s.end());        
    }
};

class basic_client
{
public:
    /** Fetch a resource using a HTTP/1 GET request.
    */
    void
    get(boost::string_ref url_str)
    {
        boost::asio::io_service ios;
        resolver_type r{ios};

        url u{url_str.to_string()};

        auto it = r.resolve(
            query_type{u.host, "http"});
        socket_type sock{ios};
        boost::asio::connect(sock, it);

        // Send HTTP request using beast
        beast::http::request_v1<
            beast::http::empty_body> req;
        req.method = "GET";
        req.url = "/";
        req.version = 11;
        req.headers.insert("Host", u.host);
        req.headers.insert("User-Agent", "Beast");
        beast::http::prepare(req);
        beast::http::write(sock, req);

        // Receive and print HTTP response using beast
        beast::streambuf sb;
        beast::http::response_v1<
            beast::http::streambuf_body> resp;
        beast::http::read(sock, sb, resp);
        std::cout << resp;
    }
};

using client = basic_client;

class get_test : public beast::unit_test::suite
{
public:
    void testGet()
    {
        client c;
        c.get("http://boost.org");

        pass();
    }
    void run() override
    {
        testGet();
    }
};

BEAST_DEFINE_TESTSUITE(get,http,beast);

} // http
} // beast
