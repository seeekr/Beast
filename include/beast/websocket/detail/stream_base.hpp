//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP
#define BEAST_WEBSOCKET_DETAIL_STREAM_BASE_HPP

#include <beast/websocket/error.hpp>
#include <beast/websocket/rfc6455.hpp>
#include <beast/websocket/detail/decorator.hpp>
#include <beast/websocket/detail/frame.hpp>
#include <beast/websocket/detail/invokable.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/http/empty_body.hpp>
#include <beast/http/message.hpp>
#include <beast/http/string_body.hpp>
#include <boost/asio/error.hpp>
#include <boost/assert.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>

namespace beast {
namespace websocket {
namespace detail {

template<class UInt>
static
std::size_t
clamp(UInt x)
{
    if(x >= std::numeric_limits<std::size_t>::max())
        return std::numeric_limits<std::size_t>::max();
    return static_cast<std::size_t>(x);
}

template<class UInt>
static
std::size_t
clamp(UInt x, std::size_t limit)
{
    if(x >= limit)
        return limit;
    return static_cast<std::size_t>(x);
}

using pong_cb = std::function<void(ping_data const&)>;

//------------------------------------------------------------------------------

struct stream_base
{
protected:
    struct op {};

    detail::maskgen maskgen_;           // source of mask keys
    decorator_type d_;                  // adorns http messages
    pong_cb pong_cb_;                   // pong callback
    detail::utf8_checker rd_utf8_check_;// for current text msg
    invokable rd_op_;                   // invoked after write completes
    invokable wr_op_;                   // invoked after read completes
    op* wr_block_;                      // op currenly writing
    ping_data* pong_data_;              // where to put pong payload

    std::uint64_t rd_size_;             // size of the current message so far
    std::uint64_t rd_need_ = 0;         // bytes left in msg frame payload
    std::size_t rd_msg_max_ =
        16 * 1024 * 1024;               // max message size
    std::size_t
        wr_frag_size_ = 16 * 1024;      // size of auto-fragments
    std::size_t mask_buf_size_ = 4096;  // mask buffer size

    detail::prepared_key_type rd_key_;  // prepared masking key

    detail::frame_header rd_fh_;        // current frame header
    close_reason cr_;                   // set from received close frame
    opcode rd_opcode_;                  // opcode of current msg
    opcode wr_opcode_ = opcode::text;   // outgoing message type
    role_type role_;                    // server or client

    bool failed_ : 1;                   // the connection failed
    bool rd_cont_ : 1;                  // expecting a continuation frame
    bool wr_close_ : 1;                 // sent close frame
    bool wr_cont_ : 1;                  // next write is continuation frame
    bool keep_alive_ : 1;               // close on failed upgrade

    stream_base(stream_base&&) = default;
    stream_base(stream_base const&) = delete;
    stream_base& operator=(stream_base&&) = default;
    stream_base& operator=(stream_base const&) = delete;

    stream_base()
        : d_(new decorator<default_decorator>{})
        , keep_alive_(false)
    {
    }

    template<class = void>
    void
    open(role_type role);

    template<class = void>
    void
    prepare_fh(close_code::value& code);

    template<class DynamicBuffer>
    void
    write_close(DynamicBuffer& db, close_reason const& rc);

    template<class DynamicBuffer>
    void
    write_ping(DynamicBuffer& db, opcode op, ping_data const& data);
};

} // detail
} // websocket
} // beast

#endif
