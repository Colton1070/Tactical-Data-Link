//------------------------------------------------------------------------------------------------
// AG0_TDLGzip.c
//
// Pure-script inflate / gunzip, ported from puff.c (zlib's public-domain
// reference inflate, Mark Adler). Implements the full RFC 1951 deflate
// decompressor plus the RFC 1952 gzip wrapper.
//
// Why this exists:
//   * Reforger's per-request 1 MB ceiling is an engine-wide limit (affects
//     save/load, lockers, and any REST transfer), so compressing payloads
//     on the wire is the only general-purpose headroom available.
//   * RestContext.GET returns a script string which is NOT binary-safe —
//     a null byte in the body can truncate the response. So the server
//     gzips THEN base64-encodes. The mod base64-decodes first (producing
//     an array<int> of bytes) and hands those bytes to this class.
//
// Typical usage (paired with AG0_Base64 in AG0_TDLPhotoData.c):
//
//     array<int> bytes     = AG0_Base64.Decode(jsonField_rgz);
//     array<int> inflated  = AG0_TDLGzip.Gunzip(bytes);
//     // ...parse your binary records out of `inflated`...
//
// Notes:
//   * Binary-safe: consumes and produces array<int> (each element = one byte,
//     values 0..255). Never touches strings after the caller's base64 step.
//   * No CRC32 verification on gzip (HTTPS already guarantees integrity and
//     adding a CRC32 table costs another ~1KB of table data for negligible
//     benefit here). Easy to add if a use case appears.
//   * Returns an empty array on any failure and Prints a LogLevel.ERROR line.
//     Callers should check Count() before use.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Canonical Huffman decode table. Built once per block by ConstructHuffman()
// from a symbol-to-code-length mapping, then used by DecodeSymbol() to pull
// the next symbol from the bit stream.
//
//   count[len]  = number of symbols assigned code length `len`
//   symbol[k]   = symbols in order of ascending (length, value),
//                 so DecodeSymbol can walk the canonical tree without
//                 materialising it.
//------------------------------------------------------------------------------------------------
class AG0_TDLHuffman
{
    ref array<int> count;   // size = MAXBITS + 1
    ref array<int> symbol;  // size = number of symbols

    void AG0_TDLHuffman()
    {
        count  = {};
        symbol = {};
    }
}

//------------------------------------------------------------------------------------------------
class AG0_TDLGzip
{
    protected const int MAXBITS = 15;    // max Huffman code length in deflate
    protected const int MAXLCODES = 286; // max literal/length codes
    protected const int MAXDCODES = 30;  // max distance codes
    protected const int MAXCODES = 316;  // MAXLCODES + MAXDCODES
    protected const int FIXLCODES = 288; // fixed-literal code count

    // --- decoder state (recreated per Inflate call) ---
    protected ref array<int> m_Input;    // bytes we're decoding
    protected int m_iPos;                // read head into m_Input
    protected int m_iBitBuf;             // LSB-first bit accumulator
    protected int m_iBitCnt;             // # bits currently buffered
    protected ref array<int> m_Output;   // decompressed bytes (pre-Resized, indexed-write)
    protected int m_iOutPos;             // write head into m_Output
    protected bool m_bError;

    //------------------------------------------------------------------------------------------------
    //! Write one byte to the output buffer at m_iOutPos. Pre-Resize from
    //! ISIZE means almost all writes hit existing storage; the grow path
    //! is just defensive (e.g. ISIZE missing or wrong).
    protected void EmitByte(int b)
    {
        if (m_iOutPos >= m_Output.Count())
            m_Output.Resize(m_iOutPos + 4096);
        m_Output[m_iOutPos] = b & 0xFF;
        m_iOutPos = m_iOutPos + 1;
    }

    //------------------------------------------------------------------------------------------------
    //! Inflate a gzip-wrapped payload.
    //! @param gzipBytes  raw gzip stream (1F 8B 08 ...) as an array of bytes 0..255.
    //! @return decompressed bytes, or an empty array on failure.
    static array<int> Gunzip(array<int> gzipBytes)
    {
        AG0_TDLGzip d = new AG0_TDLGzip();
        array<int> empty = new array<int>();

        if (!d.SkipGzipHeader(gzipBytes))
        {
            Print("[TDLGzip] bad gzip header", LogLevel.ERROR);
            return empty;
        }

        if (!d.InflateDeflateStream())
            return empty;

        // Trim any over-allocation from the ISIZE-based pre-Resize.
        d.m_Output.Resize(d.m_iOutPos);
        return d.m_Output;
    }

    //------------------------------------------------------------------------------------------------
    //! Inflate a raw deflate stream (no gzip/zlib wrapper).
    static array<int> Inflate(array<int> deflateBytes)
    {
        AG0_TDLGzip d = new AG0_TDLGzip();
        array<int> empty = new array<int>();

        d.m_Input    = deflateBytes;
        d.m_iPos     = 0;
        d.m_iBitBuf  = 0;
        d.m_iBitCnt  = 0;
        d.m_Output   = new array<int>();
        d.m_iOutPos  = 0;
        d.m_bError   = false;

        if (!d.InflateDeflateStream())
            return empty;

        d.m_Output.Resize(d.m_iOutPos);
        return d.m_Output;
    }

    //------------------------------------------------------------------------------------------------
    //! Parse and skip the gzip header, leaving m_iPos at the start of the
    //! embedded deflate stream. Initialises decoder state.
    //!
    //! RFC 1952 header layout:
    //!   [0..1]   magic 1F 8B
    //!   [2]      method (must be 8 = deflate)
    //!   [3]      flags — FTEXT(0), FHCRC(1), FEXTRA(2), FNAME(3), FCOMMENT(4)
    //!   [4..7]   mtime (unused here)
    //!   [8]      xfl
    //!   [9]      os
    //!   + optional FEXTRA / FNAME / FCOMMENT / FHCRC per flags
    //------------------------------------------------------------------------------------------------
    protected bool SkipGzipHeader(array<int> src)
    {
        m_Input    = src;
        m_iPos     = 0;
        m_iBitBuf  = 0;
        m_iBitCnt  = 0;
        m_Output   = new array<int>();
        m_iOutPos  = 0;
        m_bError   = false;

        if (src.Count() < 10)                                return false;
        if ((src[0] & 0xFF) != 0x1F)                         return false;
        if ((src[1] & 0xFF) != 0x8B)                         return false;
        if ((src[2] & 0xFF) != 0x08)                         return false;  // deflate only

        int flags = src[3] & 0xFF;
        m_iPos = 10;

        if ((flags & 0x04) != 0)  // FEXTRA
        {
            if (m_iPos + 2 > src.Count()) return false;
            int xlen = (src[m_iPos] & 0xFF) | ((src[m_iPos + 1] & 0xFF) << 8);
            m_iPos += 2 + xlen;
        }
        if ((flags & 0x08) != 0)  // FNAME  — null-terminated
        {
            while (m_iPos < src.Count() && (src[m_iPos] & 0xFF) != 0) m_iPos++;
            m_iPos++;  // skip terminator
        }
        if ((flags & 0x10) != 0)  // FCOMMENT — null-terminated
        {
            while (m_iPos < src.Count() && (src[m_iPos] & 0xFF) != 0) m_iPos++;
            m_iPos++;
        }
        if ((flags & 0x02) != 0)  // FHCRC — 2 bytes
            m_iPos += 2;

        // Pre-Reserve output to the uncompressed size from ISIZE (last 4
        // bytes, little-endian, modulo 2^32). For a 446 KB output this
        // skips ~19 array growth reallocations during DecodeSymbols / DecodeStored
        // and noticeably reduces gunzip time on large payloads.
        if (src.Count() >= 18)  // 10-byte header + 8-byte trailer minimum
        {
            int n = src.Count();
            int isize = (src[n - 4] & 0xFF)
                      | ((src[n - 3] & 0xFF) << 8)
                      | ((src[n - 2] & 0xFF) << 16)
                      | ((src[n - 1] & 0xFF) << 24);
            // Sanity-clamp: refuse absurd values (corrupt header) and
            // negative-as-unsigned (>2GB). We Resize (not Reserve) so the
            // indexed-write fast path in EmitByte hits existing storage.
            if (isize > 0 && isize < 32 * 1024 * 1024)
                m_Output.Resize(isize);
        }

        return m_iPos <= src.Count();
    }

    //------------------------------------------------------------------------------------------------
    //! Main deflate block loop. Each block:
    //!   BFINAL : 1 bit   — set on the last block
    //!   BTYPE  : 2 bits  — 00 stored, 01 fixed huffman, 10 dynamic, 11 reserved/error
    protected bool InflateDeflateStream()
    {
        while (!m_bError)
        {
            int bfinal = ReadBits(1); if (m_bError) break;
            int btype  = ReadBits(2); if (m_bError) break;

            bool ok;
            switch (btype)
            {
                case 0:  ok = DecodeStored();  break;
                case 1:  ok = DecodeFixed();   break;
                case 2:  ok = DecodeDynamic(); break;
                default: ok = false;           break;  // btype == 3 is reserved
            }
            if (!ok)
            {
                Print(string.Format("[TDLGzip] block type %1 failed", btype), LogLevel.ERROR);
                return false;
            }
            if (bfinal == 1)
                return true;
        }
        return false;
    }

    //------------------------------------------------------------------------------------------------
    //! Read `need` bits LSB-first from the input stream.
    //!
    //! Deflate never asks for more than 16 bits at a time (longest Huffman code
    //! is 15; longest extra-bits field is 13), and we only fill the buffer
    //! enough to satisfy `need`. So `val` tops out around 23 meaningful bits —
    //! bit 31 is never touched and sign-extension on the right shift is moot.
    //!
    //! Sets m_bError and returns 0 on input underrun.
    protected int ReadBits(int need)
    {
        int val = m_iBitBuf;
        while (m_iBitCnt < need)
        {
            if (m_iPos >= m_Input.Count())
            {
                Print("[TDLGzip] bit stream underrun", LogLevel.ERROR);
                m_bError = true;
                return 0;
            }
            val = val | ((m_Input[m_iPos] & 0xFF) << m_iBitCnt);
            m_iPos++;
            m_iBitCnt += 8;
        }

        int result;
        if (need <= 0)
            result = 0;
        else
            result = val & ((1 << need) - 1);

        m_iBitBuf = val >> need;
        m_iBitCnt -= need;
        return result;
    }

    //------------------------------------------------------------------------------------------------
    //! BTYPE = 00: uncompressed block. Discards bit-buffer, reads LEN/NLEN
    //! (complement check), then copies LEN bytes verbatim.
    protected bool DecodeStored()
    {
        // Align to byte boundary — discard any bits still in the buffer.
        m_iBitBuf = 0;
        m_iBitCnt = 0;

        if (m_iPos + 4 > m_Input.Count())
            return false;

        int len  = (m_Input[m_iPos]     & 0xFF) | ((m_Input[m_iPos + 1] & 0xFF) << 8);
        int nlen = (m_Input[m_iPos + 2] & 0xFF) | ((m_Input[m_iPos + 3] & 0xFF) << 8);
        m_iPos += 4;

        if (len != ((~nlen) & 0xFFFF))
            return false;

        if (m_iPos + len > m_Input.Count())
            return false;

        for (int i = 0; i < len; i++)
        {
            EmitByte(m_Input[m_iPos + i]);
        }
        m_iPos += len;
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! BTYPE = 01: fixed huffman codes. Uses the precomputed tables defined
    //! in RFC 1951 §3.2.6 — literal/length lengths are hardcoded, distances
    //! are all 5 bits.
    protected bool DecodeFixed()
    {
        array<int> lens = {};
        lens.Resize(FIXLCODES);
        int i = 0;
        while (i < 144) { lens[i] = 8; i++; }   //   0..143 → 8
        while (i < 256) { lens[i] = 9; i++; }   // 144..255 → 9
        while (i < 280) { lens[i] = 7; i++; }   // 256..279 → 7
        while (i < 288) { lens[i] = 8; i++; }   // 280..287 → 8

        AG0_TDLHuffman lencode = new AG0_TDLHuffman();
        if (!ConstructHuffman(lencode, lens, FIXLCODES))
            return false;

        array<int> dlens = {};
        dlens.Resize(MAXDCODES);
        for (int d = 0; d < MAXDCODES; d++) dlens[d] = 5;

        AG0_TDLHuffman distcode = new AG0_TDLHuffman();
        if (!ConstructHuffman(distcode, dlens, MAXDCODES))
            return false;

        return DecodeSymbols(lencode, distcode);
    }

    //------------------------------------------------------------------------------------------------
    //! BTYPE = 10: dynamic huffman. Reads HLIT/HDIST/HCLEN, builds the
    //! code-length huffman, then uses it to RLE-decode the literal/length
    //! and distance code lengths, builds those tables, and finally runs
    //! DecodeSymbols on the block.
    protected bool DecodeDynamic()
    {
        // The HCLEN code lengths arrive in this permuted order (RFC 1951 §3.2.7).
        const int order[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

        int hlit  = ReadBits(5) + 257;
        int hdist = ReadBits(5) + 1;
        int hclen = ReadBits(4) + 4;
        if (m_bError) return false;
        if (hlit > MAXLCODES || hdist > MAXDCODES) return false;

        array<int> clens = {};
        clens.Resize(19);
        for (int z = 0; z < 19; z++) clens[z] = 0;
        for (int i = 0; i < hclen; i++)
        {
            int v = ReadBits(3);
            if (m_bError) return false;
            clens[order[i]] = v;
        }

        AG0_TDLHuffman codecode = new AG0_TDLHuffman();
        if (!ConstructHuffman(codecode, clens, 19))
            return false;

        // Decode literal/length + distance code lengths using codecode.
        array<int> lens = {};
        lens.Resize(hlit + hdist);

        int idx = 0;
        while (idx < hlit + hdist)
        {
            int sym = DecodeSymbol(codecode);
            if (sym < 0) return false;

            if (sym < 16)
            {
                lens[idx] = sym;
                idx++;
            }
            else if (sym == 16)
            {
                if (idx == 0) return false;
                int rep = 3 + ReadBits(2);
                if (m_bError) return false;
                int prev = lens[idx - 1];
                for (int r = 0; r < rep; r++)
                {
                    if (idx >= hlit + hdist) return false;
                    lens[idx] = prev;
                    idx++;
                }
            }
            else if (sym == 17)
            {
                int rep = 3 + ReadBits(3);
                if (m_bError) return false;
                for (int r = 0; r < rep; r++)
                {
                    if (idx >= hlit + hdist) return false;
                    lens[idx] = 0;
                    idx++;
                }
            }
            else if (sym == 18)
            {
                int rep = 11 + ReadBits(7);
                if (m_bError) return false;
                for (int r = 0; r < rep; r++)
                {
                    if (idx >= hlit + hdist) return false;
                    lens[idx] = 0;
                    idx++;
                }
            }
            else
            {
                return false;
            }
        }

        // Split into literal/length and distance length tables.
        array<int> llens = {};
        llens.Resize(hlit);
        for (int i = 0; i < hlit; i++) llens[i] = lens[i];

        array<int> dlens = {};
        dlens.Resize(hdist);
        for (int i = 0; i < hdist; i++) dlens[i] = lens[hlit + i];

        AG0_TDLHuffman lencode  = new AG0_TDLHuffman();
        AG0_TDLHuffman distcode = new AG0_TDLHuffman();
        if (!ConstructHuffman(lencode, llens, hlit))   return false;
        if (!ConstructHuffman(distcode, dlens, hdist)) return false;

        return DecodeSymbols(lencode, distcode);
    }

    //------------------------------------------------------------------------------------------------
    //! Decode the body of a block: literal/length codes. Values < 256 emit
    //! bytes, 256 terminates the block, 257..285 trigger an LZ77 back-reference
    //! whose length comes from the length table + `extra` bits, and whose
    //! distance is decoded via distcode + distance table + `extra` bits.
    protected bool DecodeSymbols(AG0_TDLHuffman lencode, AG0_TDLHuffman distcode)
    {
        // Length base & extra bits for codes 257..285 (RFC 1951 §3.2.5).
        const int lens[] = {
              3,   4,   5,   6,   7,   8,   9,  10,  11,  13,  15,  17,  19,  23,  27,
             31,  35,  43,  51,  59,  67,  83,  99, 115, 131, 163, 195, 227, 258
        };
        const int lext[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
        };
        // Distance base & extra bits for codes 0..29.
        const int dists[] = {
               1,    2,    3,    4,    5,    7,    9,   13,   17,   25,
              33,   49,   65,   97,  129,  193,  257,  385,  513,  769,
            1025, 1537, 2049, 3073, 4097, 6145, 8193,12289,16385,24577
        };
        const int dext[] = {
            0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,10,10,11,11,12,12,13,13
        };

        while (true)
        {
            int sym = DecodeSymbol(lencode);
            if (sym < 0) return false;

            if (sym < 256)
            {
                EmitByte(sym);
            }
            else if (sym == 256)
            {
                return true;
            }
            else
            {
                int li = sym - 257;
                if (li < 0 || li >= 29) return false;

                int length = lens[li] + ReadBits(lext[li]);
                if (m_bError) return false;

                int dsym = DecodeSymbol(distcode);
                if (dsym < 0 || dsym >= 30) return false;

                int dist = dists[dsym] + ReadBits(dext[dsym]);
                if (m_bError) return false;

                // m_iOutPos is the logical end of decoded data; m_Output
                // may extend further (pre-Resized to ISIZE) but those bytes
                // are uninitialised. Use m_iOutPos for distance bounds.
                if (dist > m_iOutPos) return false;

                // LZ77 copy. Overlapping copies (dist < length) are legal and
                // idiomatic in deflate — the classic "run-length" compression
                // of repeating bytes — so we read from m_Output as we append.
                int start = m_iOutPos - dist;
                for (int k = 0; k < length; k++)
                {
                    EmitByte(m_Output[start + k]);
                }
            }
        }
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Build a canonical-Huffman decode table from a list of code lengths.
    //! Returns false on malformed tables (over- or under-subscribed) except
    //! for the standard edge case of a single-symbol tree (one 1-bit code),
    //! which is legal.
    protected bool ConstructHuffman(AG0_TDLHuffman h, array<int> lengths, int n)
    {
        // count[L] = number of symbols with code length L.
        h.count.Resize(MAXBITS + 1);
        for (int L = 0; L <= MAXBITS; L++) h.count[L] = 0;
        for (int s = 0; s < n; s++)
        {
            int len = lengths[s];
            if (len < 0 || len > MAXBITS) return false;
            h.count[len] = h.count[len] + 1;  // Enfusion doesn't allow `array[i]++`
        }

        // All zero-length? Table has no codes — legal, used for empty distances.
        if (h.count[0] == n)
        {
            h.symbol.Resize(n);
            return true;
        }

        // Kraft check: sum of 2^-len over all assigned codes must be <= 1.
        // We track `left` = 2^L - cumulative codes; negative means over-subscribed.
        int left = 1;
        for (int L = 1; L <= MAXBITS; L++)
        {
            left <<= 1;
            left -= h.count[L];
            if (left < 0) return false;
        }

        // Compute first-symbol index for each length bucket in the sorted
        // `symbol` array.
        array<int> offs = {};
        offs.Resize(MAXBITS + 1);
        offs[1] = 0;
        for (int L = 1; L < MAXBITS; L++) offs[L + 1] = offs[L] + h.count[L];

        // Place each symbol at its (length, value)-sorted slot.
        h.symbol.Resize(n);
        for (int s2 = 0; s2 < n; s2++)
        {
            int L = lengths[s2];
            if (L != 0)
            {
                h.symbol[offs[L]] = s2;
                offs[L] = offs[L] + 1;  // Enfusion doesn't allow `array[i]++`
            }
        }

        // left > 0 => under-subscribed tree. Only a single-symbol tree of
        // length 1 is a legal under-subscription in deflate.
        if (left > 0)
        {
            bool singleton = (h.count[1] == 1);
            for (int L = 2; L <= MAXBITS && singleton; L++)
                if (h.count[L] != 0) singleton = false;
            if (!singleton) return false;
        }
        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! Decode one symbol by walking the canonical tree bit-by-bit. Returns
    //! the symbol value, or -1 on malformed stream.
    protected int DecodeSymbol(AG0_TDLHuffman h)
    {
        int code  = 0;   // bits accumulated so far, MSB-first in-code
        int first = 0;   // first code value of current length
        int index = 0;   // index into h.symbol for current length bucket

        for (int len = 1; len <= MAXBITS; len++)
        {
            int bit = ReadBits(1);
            if (m_bError) return -1;
            code = (code << 1) | bit;

            int count = h.count[len];
            if (code - count < first)
            {
                int sym = h.symbol[index + (code - first)];
                return sym;
            }
            index += count;
            first = (first + count) << 1;
        }
        return -1;  // ran off the end — invalid code
    }
}
