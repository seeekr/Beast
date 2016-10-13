//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// This is a derivative work based on Zlib, copyright below:
/*
    Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Jean-loup Gailly        Mark Adler
    jloup@gzip.org          madler@alumni.caltech.edu

    The data format used by the zlib library is described by RFCs (Request for
    Comments) 1950 to 1952 in the files http://tools.ietf.org/html/rfc1950
    (zlib format), rfc1951 (deflate format) and rfc1952 (gzip format).
*/

#ifndef BEAST_ZLIB_IMPL_BASIC_INFLATE_STREAM_IPP
#define BEAST_ZLIB_IMPL_BASIC_INFLATE_STREAM_IPP

#include <cassert>
#include <cstring>

namespace beast {
namespace zlib {

/* Macros for inflate(): */

/* Clear the input bit accumulator */
#define INITBITS() \
    do { \
        hold_ = 0; \
        bits_ = 0; \
    } while(0)

/* Get a byte of input into the bit accumulator, or return from inflate()
   if there is no input available. */
#define PULLBYTE() \
    do { \
        if(zs.avail_in == 0) goto inf_leave; \
        zs.avail_in--; \
        auto nn = reinterpret_cast<std::uint8_t const*>(zs.next_in); \
        hold_ += (unsigned long)(*nn++) << bits_; \
        next_in = nn; \
        bits_ += 8; \
    } while(0)

/* Assure that there are at least n bits in the bit accumulator.  If there is
   not enough available input to do that, then return from inflate(). */
#define NEEDBITS(n) \
    do { \
        while(bits_ < (unsigned)(n)) \
            PULLBYTE(); \
    } while(0)

/* Return the low n bits of the bit accumulator (n < 16) */
#define BITS(n) \
    ((unsigned)hold_ & ((1U << (n)) - 1))

/* Remove n bits from the bit accumulator */
#define DROPBITS(n) \
    do { \
        hold_ >>= (n); \
        bits_ -= (unsigned)(n); \
    } while(0)

/* Remove zero to seven bits as needed to go to a byte boundary */
#define BYTEBITS() \
    do { \
        hold_ >>= bits_ & 7; \
        bits_ -= bits_ & 7; \
    } while(0)

template<class Allocator>
basic_inflate_stream<Allocator>::
basic_inflate_stream()
{
    reset(15);
}

template<class Allocator>
basic_inflate_stream<Allocator>::
~basic_inflate_stream()
{
    std::free(this->window_);
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
reset(std::uint8_t windowBits)
{
    if(windowBits < 8 || windowBits > 15)
        throw std::domain_error("windowBits out of range");
    if(window_ && wbits_ != windowBits)
    {
        std::free(window_);
        window_ = nullptr;
    }

    // update state and reset the rest of it
    wbits_ = (unsigned)windowBits;
    wsize_ = 0;
    whave_ = 0;
    wnext_ = 0;

    w_.reset(windowBits);

    resetKeep(*this);
}

template<class Allocator>
int
basic_inflate_stream<Allocator>::
write(int flush)
{
    return write(*this, flush);
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
resetKeep(z_stream& zs)
{
    zs.total_in = 0;
    zs.total_out = 0;
    zs.msg = 0;
    total_ = 0;
    mode_ = HEAD;
    last_ = 0;
    dmax_ = 32768U;
    hold_ = 0;
    bits_ = 0;
    lencode_ = codes_;
    distcode_ = codes_;
    next_ = codes_;
    sane_ = 1;
    back_ = -1;
    
    bi_.flush();
}

template<class Allocator>
void
basic_inflate_stream<Allocator>::
fixedTables()
{
auto strm = this;
    auto const fc = detail::get_fixed_tables();
    strm->lencode_ = fc.lencode;
    strm->lenbits_ = fc.lenbits;
    strm->distcode_ = fc.distcode;
    strm->distbits_ = fc.distbits;
}

//------------------------------------------------------------------------------

/*
   Update the window with the last wsize (normally 32K) bytes written before
   returning.  If window does not exist yet, create it.  This is only called
   when a window is already in use, or when output has been written during this
   inflate call, but the end of the deflate stream has not been reached yet.
   It is also called to create a window for dictionary data when a dictionary
   is loaded.

   Providing output buffers larger than 32K to inflate() should provide a speed
   advantage, since only the last 32K of output is copied to the sliding window
   upon return from inflate(), and since all distances after the first 32K of
   output will fall in the output data, making match copies simpler and faster.
   The advantage may be dependent on the size of the processor's data caches.
 */
template<class Allocator>
int
basic_inflate_stream<Allocator>::
updatewindow(const Byte *end, unsigned copy)
{
    auto strm = this;
    unsigned dist;

    // if it hasn't been done already, allocate space for the window
    if(strm->window_ == 0)
    {
        strm->window_ = (unsigned char *) std::malloc(1U << strm->wbits_);
        if(strm->window_ == 0)
            return 1;
    }

    // if window not in use yet, initialize
    if(strm->wsize_ == 0)
    {
        strm->wsize_ = 1U << strm->wbits_;
        strm->wnext_ = 0;
        strm->whave_ = 0;
    }

    // copy wsize or less output bytes into the circular window
    if(copy >= strm->wsize_)
    {
        std::memcpy(strm->window_, end - strm->wsize_, strm->wsize_);
        strm->wnext_ = 0;
        strm->whave_ = strm->wsize_;
    }
    else
    {
        dist = std::min(strm->wsize_ - strm->wnext_, copy);
        std::memcpy(strm->window_ + strm->wnext_, end - copy, dist);
        copy -= dist;
        if(copy)
        {
            std::memcpy(strm->window_, end - copy, copy);
            strm->wnext_ = copy;
            strm->whave_ = strm->wsize_;
        }
        else
        {
            strm->wnext_ += dist;
            if(strm->wnext_ == strm->wsize_)
                strm->wnext_ = 0;
            if(strm->whave_ < strm->wsize_)
                strm->whave_ += dist;
        }
    }
    return 0;
}

template<class Allocator>
int
basic_inflate_stream<Allocator>::
write(z_stream& zs, int flush)
{
    auto put = zs.next_out;
    auto next = zs.next_in;
    auto const outend = put + zs.avail_out;
    auto const end = next + zs.avail_in;
    auto const more =
        [&]
        {
            auto const nwritten = put - zs.next_out;
            // VFALCO TODO Don't update the window unless necessary
            {
                if(nwritten >= 32768)
                    w_.write(put - 32768, 32768);
                else
                    w_.write(zs.next_out,
                        static_cast<std::uint16_t>(nwritten));
            }
            zs.total_in += next - zs.next_in;
            zs.total_out += nwritten;
            zs.next_out = put;
            zs.avail_out = outend - put;
            zs.next_in = next;
            zs.avail_in = end - next;
        };

    unsigned in, out;       // save starting available input and output
    unsigned copy;          // number of stored or match bytes to copy
    unsigned char *from;    // where to copy match bytes from
    detail::code here;      // current decoding table entry
    detail::code last;      // parent table entry
    unsigned len;           // length to copy for repeats, bits to drop
    int ret;                // return code
    static unsigned short constexpr order[19] = // permutation of code lengths
        {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    if(zs.next_out == 0 ||
            (zs.next_in == 0 && zs.avail_in != 0))
        return Z_STREAM_ERROR;

    if(mode_ == TYPE)
        mode_ = TYPEDO;
    in = zs.avail_in;
    out = zs.avail_out;
    ret = Z_OK;
    for(;;)
    {
        switch(mode_)
        {
        case HEAD:
            mode_ = TYPEDO;
            break;

        case TYPE:
            if(flush == Z_BLOCK || flush == Z_TREES)
                goto inf_leave;
            // fall through

        case TYPEDO:
            if(last_)
            {
                BYTEBITS();
                mode_ = CHECK;
                break;
            }
            NEEDBITS(3);
            last_ = BITS(1);
            DROPBITS(1);
            switch(BITS(2))
            {
            case 0:                             /* stored block */
                mode_ = STORED;
                break;
            case 1:                             /* fixed block */
                fixedTables();
                mode_ = LEN_;             /* decode codes */
                if(flush == Z_TREES) {
                    DROPBITS(2);
                    goto inf_leave;
                }
                break;
            case 2:                             /* dynamic block */
                mode_ = TABLE;
                break;
            case 3:
                msg = (char *)"invalid block type";
                mode_ = BAD;
            }
            DROPBITS(2);
            break;

        case STORED:
            BYTEBITS();                         /* go to byte boundary */
            NEEDBITS(32);
            if((hold_ & 0xffff) != ((hold_ >> 16) ^ 0xffff))
            {
                msg = (char *)"invalid stored block lengths";
                mode_ = BAD;
                break;
            }
            length_ = (unsigned)hold_ & 0xffff;
            INITBITS();
            mode_ = COPY_;
            if(flush == Z_TREES)
                goto inf_leave;
            // fall through

        case COPY_:
            mode_ = COPY;
            // fall through

        case COPY:
            copy = length_;
            if(copy)
            {
                if(copy > avail_in)
                    copy = avail_in;
                if(copy > avail_out)
                    copy = avail_out;
                if(copy == 0)
                    goto inf_leave;
                std::memcpy(next_out, next_in, copy);
                avail_in -= copy;
                next_in += copy;
                avail_out -= copy;
                next_out += copy;
                length_ -= copy;
                break;
            }
            mode_ = TYPE;
            break;

        case TABLE:
            NEEDBITS(14);
            nlen_ = BITS(5) + 257;
            DROPBITS(5);
            ndist_ = BITS(5) + 1;
            DROPBITS(5);
            ncode_ = BITS(4) + 4;
            DROPBITS(4);
            if(nlen_ > 286 || ndist_ > 30)
            {
                msg = (char *)"too many length or distance symbols";
                mode_ = BAD;
                break;
            }
            have_ = 0;
            mode_ = LENLENS;
            // fall through

        case LENLENS:
            while(have_ < ncode_)
            {
                NEEDBITS(3);
                lens_[order[have_++]] = (unsigned short)BITS(3);
                DROPBITS(3);
            }
            while(have_ < 19)
                lens_[order[have_++]] = 0;
            next_ = codes_;
            lencode_ = (detail::code const*)(next_);
            lenbits_ = 7;
            ret = inflate_table(detail::CODES, lens_, 19, &(next_),
                                &(lenbits_), work_);
            if(ret)
            {
                msg = (char *)"invalid code lengths set";
                mode_ = BAD;
                break;
            }
            have_ = 0;
            mode_ = CODELENS;
            // fall through

        case CODELENS:
            while(have_ < nlen_ + ndist_)
            {
                for(;;)
                {
                    here = lencode_[BITS(lenbits_)];
                    if((unsigned)(here.bits) <= bits_) break;
                    PULLBYTE();
                }
                if(here.val < 16)
                {
                    DROPBITS(here.bits);
                    lens_[have_++] = here.val;
                }
                else
                {
                    if(here.val == 16)
                    {
                        NEEDBITS(here.bits + 2);
                        DROPBITS(here.bits);
                        if(have_ == 0)
                        {
                            msg = (char *)"invalid bit length repeat";
                            mode_ = BAD;
                            break;
                        }
                        len = lens_[have_ - 1];
                        copy = 3 + BITS(2);
                        DROPBITS(2);
                    }
                    else if(here.val == 17)
                    {
                        NEEDBITS(here.bits + 3);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 3 + BITS(3);
                        DROPBITS(3);
                    }
                    else
                    {
                        NEEDBITS(here.bits + 7);
                        DROPBITS(here.bits);
                        len = 0;
                        copy = 11 + BITS(7);
                        DROPBITS(7);
                    }
                    if(have_ + copy > nlen_ + ndist_)
                    {
                        msg = (char *)"invalid bit length repeat";
                        mode_ = BAD;
                        break;
                    }
                    while(copy--)
                        lens_[have_++] = (unsigned short)len;
                }
            }

            // handle error breaks in while
            if(mode_ == BAD)
                break;

            // check for end-of-block code (better have one)
            if(lens_[256] == 0)
            {
                msg = (char *)"invalid code -- missing end-of-block";
                mode_ = BAD;
                break;
            }

            /* build code tables -- note: do not change the lenbits or distbits
               values here (9 and 6) without reading the comments in inftrees.hpp
               concerning the ENOUGH constants, which depend on those values */
            next_ = codes_;
            lencode_ = (detail::code const*)(next_);
            lenbits_ = 9;
            ret = inflate_table(detail::LENS, lens_, nlen_, &(next_),
                                &(lenbits_), work_);
            if(ret)
            {
                msg = (char *)"invalid literal/lengths set";
                mode_ = BAD;
                break;
            }
            distcode_ = (detail::code const *)(next_);
            distbits_ = 6;
            ret = inflate_table(detail::DISTS, lens_ + nlen_, ndist_,
                            &(next_), &(distbits_), work_);
            if(ret)
            {
                msg = (char *)"invalid distances set";
                mode_ = BAD;
                break;
            }
            mode_ = LEN_;
            if(flush == Z_TREES)
                goto inf_leave;
            // fall through

        case LEN_:
            mode_ = LEN;
            // fall through

        case LEN:
            if(avail_in >= 6 && avail_out >= 258)
            {
                inflate_fast(zs, out);
                if(mode_ == TYPE)
                    back_ = -1;
                break;
            }
            back_ = 0;
            for(;;)
            {
                here = lencode_[BITS(lenbits_)];
                if((unsigned)(here.bits) <= bits_) break;
                PULLBYTE();
            }
            if(here.op && (here.op & 0xf0) == 0)
            {
                last = here;
                for(;;)
                {
                    here = lencode_[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if((unsigned)(last.bits + here.bits) <= bits_)
                        break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                back_ += last.bits;
            }
            DROPBITS(here.bits);
            back_ += here.bits;
            length_ = (unsigned)here.val;
            if((int)(here.op) == 0)
            {
                mode_ = LIT;
                break;
            }
            if(here.op & 32)
            {
                back_ = -1;
                mode_ = TYPE;
                break;
            }
            if(here.op & 64)
            {
                msg = (char *)"invalid literal/length code";
                mode_ = BAD;
                break;
            }
            extra_ = (unsigned)(here.op) & 15;
            mode_ = LENEXT;
            // fall through

        case LENEXT:
            if(extra_)
            {
                NEEDBITS(extra_);
                length_ += BITS(extra_);
                DROPBITS(extra_);
                back_ += extra_;
            }
            was_ = length_;
            mode_ = DIST;
            // fall through

        case DIST:
            for(;;) {
                here = distcode_[BITS(distbits_)];
                if((unsigned)(here.bits) <= bits_)
                    break;
                PULLBYTE();
            }
            if((here.op & 0xf0) == 0)
            {
                last = here;
                for(;;)
                {
                    here = distcode_[last.val +
                            (BITS(last.bits + last.op) >> last.bits)];
                    if((unsigned)(last.bits + here.bits) <= bits_)
                        break;
                    PULLBYTE();
                }
                DROPBITS(last.bits);
                back_ += last.bits;
            }
            DROPBITS(here.bits);
            back_ += here.bits;
            if(here.op & 64) {
                msg = (char *)"invalid distance code";
                mode_ = BAD;
                break;
            }
            offset_ = (unsigned)here.val;
            extra_ = (unsigned)(here.op) & 15;
            mode_ = DISTEXT;
            // fall through

        case DISTEXT:
            if(extra_)
            {
                NEEDBITS(extra_);
                offset_ += BITS(extra_);
                DROPBITS(extra_);
                back_ += extra_;
            }
#ifdef INFLATE_STRICT
            if(offset_ > dmax_)
            {
                msg = (char *)"invalid distance too far back";
                mode_ = BAD;
                break;
            }
#endif
            mode_ = MATCH;
            // fall through

        case MATCH:
            if(avail_out == 0)
                goto inf_leave;
            copy = out - avail_out;
            if(offset_ > copy)
            {
                // copy from window
                copy = offset_ - copy;
                if(copy > whave_)
                {
                    if(sane_)
                    {
                        msg = (char *)"invalid distance too far back";
                        mode_ = BAD;
                        break;
                    }
                }
                if(copy > wnext_)
                {
                    copy -= wnext_;
                    from = window_ + (wsize_ - copy);
                }
                else
                    from = window_ + (wnext_ - copy);
                if(copy > length_)
                    copy = length_;
            }
            else
            {
                // copy from output
                from = next_out - offset_;
                copy = length_;
            }
            if(copy > avail_out)
                copy = avail_out;
            avail_out -= copy;
            length_ -= copy;
            do
            {
                *next_out++ = *from++;
            }
            while(--copy);
            if(length_ == 0)
                mode_ = LEN;
            break;

        case LIT:
            if(avail_out == 0)
                goto inf_leave;
            *next_out++ = (unsigned char)(length_);
            avail_out--;
            mode_ = LEN;
            break;

        case CHECK:
            mode_ = DONE;
            // fall through

        case DONE:
            ret = Z_STREAM_END;
            goto inf_leave;

        case BAD:
            ret = Z_DATA_ERROR;
            goto inf_leave;

        case MEM:
            return Z_MEM_ERROR;

        case SYNC:
        default:
            return Z_STREAM_ERROR;
        }
    }
    /*
       Return from inflate(), updating the total counts and the check value.
       If there was no progress during the inflate() call, return a buffer
       error.  Call updatewindow() to create and/or update the window state.
       Note: a memory error from inflate() is non-recoverable.
     */
  inf_leave:
    if(wsize_ || (out != avail_out && mode_ < BAD &&
            (mode_ < CHECK || flush != Z_FINISH)))
    {
        if(updatewindow(next_out, out - avail_out))
        {
            mode_ = MEM;
            return Z_MEM_ERROR;
        }
    }
    in -= zs.avail_in;
    out -= zs.avail_out;
    zs.total_in += in;
    zs.total_out += out;
    total_ += out;
    zs.data_type = bits_ + (last_ ? 64 : 0) +
                      (mode_ == TYPE ? 128 : 0) +
                      (mode_ == LEN_ || mode_ == COPY_ ? 256 : 0);
    if(((in == 0 && out == 0) || flush == Z_FINISH) && ret == Z_OK)
        ret = Z_BUF_ERROR;
    return ret;
}

/*
   Decode literal, length, and distance codes and write out the resulting
   literal and match bytes until either not enough input or output is
   available, an end-of-block is encountered, or a data error is encountered.
   When large enough input and output buffers are supplied to inflate(), for
   example, a 16K input buffer and a 64K output buffer, more than 95% of the
   inflate execution time is spent in this routine.

   Entry assumptions:

        state->mode_ == LEN
        strm->avail_in >= 6
        strm->avail_out >= 258
        start >= strm->avail_out
        state->bits_ < 8

   On return, state->mode_ is one of:

        LEN -- ran out of enough output space or enough available input
        TYPE -- reached end of block code, inflate() to interpret next block
        BAD -- error in block data

   Notes:

    - The maximum input bits used by a length/distance pair is 15 bits for the
      length code, 5 bits for the length extra, 15 bits for the distance code,
      and 13 bits for the distance extra.  This totals 48 bits, or six bytes.
      Therefore if strm->avail_in >= 6, then there is enough input to avoid
      checking for available input while decoding.

    - The maximum bytes that a single length/distance pair can output is 258
      bytes, which is the maximum length that can be coded.  inflate_fast()
      requires strm->avail_out >= 258 for each loop to avoid checking for
      output space.
 */
template<class Allocator>
void
basic_inflate_stream<Allocator>::
inflate_fast(
    z_stream& zs,
    unsigned start)             // inflate()'s starting value for strm->avail_out
{
    const unsigned char *in;    // local strm->next_in
    const unsigned char *last;  // have enough input while in < last
    unsigned char *out;         // local strm->next_out
    unsigned char *beg;         // inflate()'s initial strm->next_out
    unsigned char *end;         // while out < end, enough space available
    detail::code here;          // retrieved table entry
    unsigned op;                // code bits, operation, extra bits, or window position, window bytes to copy
    unsigned len;               // match length, unused bytes
    unsigned dist;              // match distance
    unsigned const lmask =
        (1U << lenbits_) - 1;   // mask for first level of length codes
    unsigned dmask =
        (1U << distbits_) - 1;  // mask for first level of distance codes

    /* copy state to local variables */
    in = zs.next_in;
    last = in + (zs.avail_in - 5);
    out = zs.next_out;
    beg = out - (start - zs.avail_out);
    end = out + (zs.avail_out - 257);

    /* decode literals and length/distances until end-of-block or not enough
       input data or output space */
    do
    {
        if(bits_ < 15)
        {
            hold_ += (unsigned long)(*in++) << bits_;
            bits_ += 8;
            hold_ += (unsigned long)(*in++) << bits_;
            bits_ += 8;
        }
        here = lencode_[hold_ & lmask];
      dolen:
        op = (unsigned)(here.bits);
        hold_ >>= op;
        bits_ -= op;
        op = (unsigned)(here.op);
        if(op == 0)
        {
            // literal
            *out++ = (unsigned char)(here.val);
        }
        else if(op & 16)
        {
            // length base
            len = (unsigned)(here.val);
            op &= 15; // number of extra bits
            if(op)
            {
                if(bits_ < op)
                {
                    hold_ += (unsigned long)(*in++) << bits_;
                    bits_ += 8;
                }
                len += (unsigned)hold_ & ((1U << op) - 1);
                hold_ >>= op;
                bits_ -= op;
            }
            if(bits_ < 15)
            {
                hold_ += (unsigned long)(*in++) << bits_;
                bits_ += 8;
                hold_ += (unsigned long)(*in++) << bits_;
                bits_ += 8;
            }
            here = distcode_[hold_ & dmask];
          dodist:
            op = (unsigned)(here.bits);
            hold_ >>= op;
            bits_ -= op;
            op = (unsigned)(here.op);
            if(op & 16)
            {
                // distance base
                dist = (unsigned)(here.val);
                op &= 15; // number of extra bits
                if(bits_ < op)
                {
                    hold_ += (unsigned long)(*in++) << bits_;
                    bits_ += 8;
                    if(bits_ < op)
                    {
                        hold_ += (unsigned long)(*in++) << bits_;
                        bits_ += 8;
                    }
                }
                dist += (unsigned)hold_ & ((1U << op) - 1);
#ifdef INFLATE_STRICT
                if(dist > dmax_)
                {
                    zs.msg = (char *)"invalid distance too far back";
                    mode_ = BAD;
                    break;
                }
#endif
                hold_ >>= op;
                bits_ -= op;
                op = (unsigned)(out - beg); // max distance in output
                if(dist > op)
                {
                    // see if copy from window
                    op = dist - op; // distance back in window
                    if(op > whave_)
                    {
                        if(sane_)
                        {
                            zs.msg =
                                (char *)"invalid distance too far back";
                            mode_ = BAD;
                            break;
                        }
                    }
                    auto from = window_;
                    if(wnext_ == 0)
                    {
                        // very common case
                        from += wsize_ - op;
                        if(op < len)
                        {
                            // some from window
                            len -= op;
                            do
                            {
                                *out++ = *from++;
                            }
                            while(--op);
                            from = out - dist;  // rest from output */
                        }
                    }
                    else if(wnext_ < op)
                    {
                        // wrap around window
                        from += wsize_ + wnext_ - op;
                        op -= wnext_;
                        if(op < len)
                        {
                            // some from end of window
                            len -= op;
                            do
                            {
                                *out++ = *from++;
                            } while(--op);
                            from = window_;
                            if(wnext_ < len)
                            {
                                // some from start of window
                                op = wnext_;
                                len -= op;
                                do
                                {
                                    *out++ = *from++;
                                }
                                while(--op);
                                from = out - dist; // rest from output
                            }
                        }
                    }
                    else
                    {
                        // contiguous in window
                        from += wnext_ - op;
                        if(op < len)
                        {
                            // some from window
                            len -= op;
                            do
                            {
                                *out++ = *from++;
                            }
                            while(--op);
                            from = out - dist; // rest from output
                        }
                    }
                    while(len > 2)
                    {
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    }
                    if(len)
                    {
                        *out++ = *from++;
                        if(len > 1)
                            *out++ = *from++;
                    }
                }
                else
                {
                    // copy direct from output
                    auto from = out - dist;          
                    do
                    {
                        // minimum length is three
                        *out++ = *from++;
                        *out++ = *from++;
                        *out++ = *from++;
                        len -= 3;
                    }
                    while(len > 2);
                    if(len)
                    {
                        *out++ = *from++;
                        if(len > 1)
                            *out++ = *from++;
                    }
                }
            }
            else if((op & 64) == 0)
            {
                // 2nd level distance code
                here = distcode_[here.val + (hold_ & ((1U << op) - 1))];
                goto dodist;
            }
            else
            {
                zs.msg = (char *)"invalid distance code";
                mode_ = BAD;
                break;
            }
        }
        else if((op & 64) == 0)
        {
            // 2nd level length code
            here = lencode_[here.val + (hold_ & ((1U << op) - 1))];
            goto dolen;
        }
        else if(op & 32)
        {
            // end-of-block
            mode_ = TYPE;
            break;
        }
        else
        {
            zs.msg = (char *)"invalid literal/length code";
            mode_ = BAD;
            break;
        }
    }
    while(in < last && out < end);

    // return unused bytes (on entry, bits < 8, so in won't go too far back)
    len = bits_ >> 3;
    in -= len;
    bits_ -= len << 3;
    hold_ &= (1U << bits_) - 1;

    // update state and return
    zs.next_in = in;
    zs.next_out = out;
    zs.avail_in = (unsigned)(in < last ? 5 + (last - in) : 5 - (in - last));
    zs.avail_out = (unsigned)(out < end ?
                                 257 + (end - out) : 257 - (out - end));
}

/*
   inflate_fast() speedups that turned out slower (on a PowerPC G3 750CXe):
   - Using bit fields for code structure
   - Different op definition to avoid & for extra bits (do & for table bits)
   - Three separate decoding do-loops for direct, window, and wnext == 0
   - Special case for distance > 1 copies to do overlapped load and store copy
   - Explicit branch predictions (based on measured branch probabilities)
   - Deferring match copy and interspersed it with decoding subsequent codes
   - Swapping literal/length else
   - Swapping window/direct else
   - Larger unrolled copy loops (three is about right)
   - Moving len -= 3 statement into middle of loop
 */

} // zlib
} // beast

#endif
