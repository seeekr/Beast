//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_DETAIL_MASK_HPP
#define BEAST_WEBSOCKET_DETAIL_MASK_HPP

#include <boost/asio/buffer.hpp>
#include <array>
#include <climits>
#include <cstdint>
#include <random>
#include <type_traits>

namespace beast {
namespace websocket {
namespace detail {

/** XOR-shift Generator.

    Meets the requirements of UniformRandomNumberGenerator.
    Simple and fast RNG based on:
    http://xorshift.di.unimi.it/xorshift128plus.c
    does not accept seed==0
*/
class xor_shift_engine
{
public:
    using result_type = std::uint64_t;

    xor_shift_engine(xor_shift_engine const&) = default;
    xor_shift_engine& operator=(xor_shift_engine const&) = default;

    explicit
    xor_shift_engine(result_type val = 1977u)
    {
        seed(val);
    }

    void
    seed(result_type seed);

    template<class Sseq>
    typename std::enable_if<
        ! std::is_integral<Sseq>::value>::type
    seed(Sseq& ss)
    {
        std::array<result_type, 2> v;
        ss.generate(v.begin(), v.end());
        seed((v[0] << 32) + v[1]);
    }

    result_type
    operator()();

    static
    result_type constexpr
    min()
    {
        return std::numeric_limits<result_type>::min();
    }

    static
    result_type constexpr
    max()
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    result_type s_[2];

    static
    result_type
    murmurhash3(result_type x);
};

inline
void
xor_shift_engine::seed(result_type seed)
{
    if(seed == 0)
        throw std::domain_error("invalid seed");
    s_[0] = murmurhash3(seed);
    s_[1] = murmurhash3(s_[0]);
}

inline
auto
xor_shift_engine::operator()() ->
    result_type
{
    result_type s1 = s_[0];
    result_type const s0 = s_[1];
    s_[0] = s0;
    s1 ^= s1<< 23;
    return(s_[1] =(s1 ^ s0 ^(s1 >> 17) ^(s0 >> 26))) + s0;
}

inline
auto
xor_shift_engine::murmurhash3(result_type x)
    -> result_type
{
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    return x ^= x >> 33;
}

// Pseudo-random source of mask keys
//
template<class Generator>
class maskgen_t
{
    Generator g_;

public:
    using result_type = std::uint32_t;

    maskgen_t();

    result_type
    operator()() noexcept;

    void
    rekey();
};

template<class Generator>
maskgen_t<Generator>::maskgen_t()
{
    rekey();
}

template<class Generator>
auto
maskgen_t<Generator>::operator()() noexcept ->
    result_type
{
    for(;;)
        if(auto key = static_cast<result_type>(g_()))
            return key;
}

template<class _>
void
maskgen_t<_>::rekey()
{
    for(;;)
    {
        std::random_device rng;
        std::array<std::uint32_t, 32> e;
        for(auto& i : e)
            i = rng();
        std::seed_seq ss(e.begin(), e.end());
        g_.seed(ss);
        if(g_())
            break;
    }
}

using maskgen = maskgen_t<xor_shift_engine>;

//------------------------------------------------------------------------------

using prepared_key_type =
    std::conditional<sizeof(void*) == 8,
        std::uint64_t, std::uint32_t>::type;

inline
void
prepare_key(std::uint32_t& prepared, std::uint32_t key)
{
    prepared = key;
}

inline
void
prepare_key(std::uint64_t& prepared, std::uint32_t key)
{
    prepared =
        (static_cast<std::uint64_t>(key) << 32) | key;
}

template<class T>
inline
typename std::enable_if<std::is_integral<T>::value, T>::type
ror(T t, unsigned n = 1)
{
    auto constexpr bits =
        static_cast<unsigned>(
            sizeof(T) * CHAR_BIT);
    n &= bits-1;
    return static_cast<T>((t << (bits - n)) | (
        static_cast<typename std::make_unsigned<T>::type>(t) >> n));
}

// 32-bit Unoptimized
//
template<class = void>
void
mask_inplace_general(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto const n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *p ^=  key      ; ++p;
        *p ^= (key >> 8); ++p;
        *p ^= (key >>16); ++p;
        *p ^= (key >>24); ++p;
    }
    auto const m =
        static_cast<std::uint8_t>(n % sizeof(key));
    switch(m)
    {
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, m*8);
    default:
        break;
    }
}

// 64-bit unoptimized
//
template<class = void>
void
mask_inplace_general(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    using boost::asio::buffer_cast;
    using boost::asio::buffer_size;
    auto const n = buffer_size(b);
    auto p = buffer_cast<std::uint8_t*>(b);
    for(auto i = n / sizeof(key); i; --i)
    {
        *p ^=  key      ; ++p;
        *p ^= (key >> 8); ++p;
        *p ^= (key >>16); ++p;
        *p ^= (key >>24); ++p;
        *p ^= (key >>32); ++p;
        *p ^= (key >>40); ++p;
        *p ^= (key >>48); ++p;
        *p ^= (key >>56); ++p;
    }
    auto const m =
        static_cast<std::uint8_t>(n % sizeof(key));
    switch(m)
    {
    case 7: p[6] ^= (key >>16);
    case 6: p[5] ^= (key >> 8);
    case 5: p[4] ^=  key;
    case 4: p[3] ^= (key >>24);
    case 3: p[2] ^= (key >>16);
    case 2: p[1] ^= (key >> 8);
    case 1: p[0] ^=  key;
        key = ror(key, m*8);
    default:
        break;
    }
}

inline
void
mask_inplace(
    boost::asio::mutable_buffer const& b,
        std::uint32_t& key)
{
    mask_inplace_general(b, key);
}

inline
void
mask_inplace(
    boost::asio::mutable_buffer const& b,
        std::uint64_t& key)
{
    mask_inplace_general(b, key);
}

// Apply mask in place
//
template<class MutableBuffers, class KeyType>
void
mask_inplace(
    MutableBuffers const& bs, KeyType& key)
{
    for(auto const& b : bs)
        mask_inplace(b, key);
}

} // detail
} // websocket
} // beast

#endif
