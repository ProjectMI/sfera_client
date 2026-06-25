#include "Compression/ZlibInflate.h"

namespace
{
    class FBitReader
    {
    public:
        FBitReader(const uint8* data, size_t size) : Data(data), Size(size) {}
        bool ReadBits(uint32 count, uint32& out)
        {
            out = 0;

            for (uint32 i = 0; i < count; ++i)
            {
                if (ByteOffset >= Size) { return false; }

                uint32 bit = (Data[ByteOffset] >> BitOffset) & 1u;
                out |= bit << i;
                ++BitOffset;

                if (BitOffset == 8)
                {
                    BitOffset = 0;
                    ++ByteOffset;
                }
            }

            return true;
        }
        bool ReadByte(uint8& out)
        {
            uint32 v = 0;

            if (!ReadBits(8, v)) { return false; }

            out = static_cast<uint8>(v);
            return true;
        }
        void AlignByte()
        {
            if (BitOffset != 0)
            {
                BitOffset = 0;
                ++ByteOffset;
            }
        }
        size_t Offset() const { return ByteOffset; }
    private:
        const uint8* Data = nullptr;
        size_t Size = 0;
        size_t ByteOffset = 0;
        uint32 BitOffset = 0;
    };

    uint32 ReverseBits(uint32 code, uint32 len)
    {
        uint32 out = 0;

        for (uint32 i = 0; i < len; ++i)
        {
            out = (out << 1) | (code & 1u);
            code >>= 1;
        }

        return out;
    }

    struct FHuffmanEntry
    {
        uint16 Symbol = 0;
        uint16 Code = 0;
        uint8 Length = 0;
    };

    class FHuffmanTree
    {
    public:
        FStatus Build(const std::vector<uint8>& lengths)
        {
            Entries.clear();
            std::array<uint16, 16> count{};
            std::array<uint16, 16> nextCode{};

            for (uint8 len : lengths)
            {
                if (len > 15) { return FStatus::Error(EStatusCode::InvalidData, "invalid deflate huffman code length"); }

                if (len)
                {
                    ++count[len];
                }
            }

            uint16 code = 0;

            for (uint32 bits = 1; bits <= 15; ++bits)
            {
                code = static_cast<uint16>((code + count[bits - 1]) << 1);
                nextCode[bits] = code;
            }

            for (uint32 symbol = 0; symbol < lengths.size(); ++symbol)
            {
                uint8 len = lengths[symbol];

                if (!len) { continue; }

                uint16 canonical = nextCode[len]++;
                Entries.push_back(FHuffmanEntry{static_cast<uint16>(symbol), static_cast<uint16>(ReverseBits(canonical, len)), len});
            }

            std::sort(Entries.begin(), Entries.end(), [](const FHuffmanEntry& a, const FHuffmanEntry& b)
            {
                if (a.Length != b.Length) { return a.Length < b.Length; }

                if (a.Code != b.Code) { return a.Code < b.Code; }

                return a.Symbol < b.Symbol;
            });
            return FStatus::Ok();
        }

        bool Decode(FBitReader& br, uint16& symbol) const
        {
            uint32 code = 0;

            for (uint32 len = 1; len <= 15; ++len)
            {
                uint32 bit = 0;

                if (!br.ReadBits(1, bit)) { return false; }

                code |= bit << (len - 1);

                for (const auto& e : Entries)
                {
                    if (e.Length == len && e.Code == code) { symbol = e.Symbol; return true; }

                    if (e.Length > len) { break; }
                }
            }

            return false;
        }
    private:
        std::vector<FHuffmanEntry> Entries;
    };

    const std::array<uint16, 29> LengthBase =
    {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    const std::array<uint8, 29> LengthExtra =
    {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    const std::array<uint16, 30> DistBase =
    {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    const std::array<uint8, 30> DistExtra =
    {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    const std::array<uint8, 19> CodeLengthOrder =
    {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    FStatus BuildFixedTrees(FHuffmanTree& litLen, FHuffmanTree& dist)
    {
        std::vector<uint8> ll(288);

        for (uint32 i = 0; i <= 143; ++i)
        {
            ll[i] = 8;
        }

        for (uint32 i = 144; i <= 255; ++i)
        {
            ll[i] = 9;
        }

        for (uint32 i = 256; i <= 279; ++i)
        {
            ll[i] = 7;
        }

        for (uint32 i = 280; i <= 287; ++i)
        {
            ll[i] = 8;
        }

        std::vector<uint8> dl(32, 5);
        FStatus s = litLen.Build(ll);

        if (!s.IsOk()) { return s; }

        return dist.Build(dl);
    }

    FStatus BuildDynamicTrees(FBitReader& br, FHuffmanTree& litLen, FHuffmanTree& dist)
    {
        uint32 hlit = 0, hdist = 0, hclen = 0;

        if (!br.ReadBits(5, hlit) || !br.ReadBits(5, hdist) || !br.ReadBits(4, hclen)) { return FStatus::Error(EStatusCode::InvalidData, "truncated dynamic deflate header"); }

        hlit += 257;
        hdist += 1;
        hclen += 4;
        std::vector<uint8> codeLenLengths(19, 0);

        for (uint32 i = 0; i < hclen; ++i)
        {
            uint32 v = 0;

            if (!br.ReadBits(3, v)) { return FStatus::Error(EStatusCode::InvalidData, "truncated dynamic code-length table"); }

            codeLenLengths[CodeLengthOrder[i]] = static_cast<uint8>(v);
        }

        FHuffmanTree codeLenTree;
        FStatus s = codeLenTree.Build(codeLenLengths);

        if (!s.IsOk()) { return s; }

        std::vector<uint8> lengths;
        lengths.reserve(hlit + hdist);

        while (lengths.size() < hlit + hdist)
        {
            uint16 sym = 0;

            if (!codeLenTree.Decode(br, sym)) { return FStatus::Error(EStatusCode::InvalidData, "bad dynamic code-length symbol"); }

            if (sym <= 15) { lengths.push_back(static_cast<uint8>(sym)); continue; }

            if (sym == 16)
            {
                if (lengths.empty()) { return FStatus::Error(EStatusCode::InvalidData, "deflate repeat with no previous length"); }

                uint32 extra = 0; if (!br.ReadBits(2, extra)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate repeat length"); }
                uint8 prev = lengths.back();

                for (uint32 i = 0; i < 3 + extra; ++i)
                {
                    lengths.push_back(prev);
                }
            }
            else if (sym == 17)
            {
                uint32 extra = 0; if (!br.ReadBits(3, extra)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate zero repeat"); }

                for (uint32 i = 0; i < 3 + extra; ++i)
                {
                    lengths.push_back(0);
                }
            }
            else if (sym == 18)
            {
                uint32 extra = 0; if (!br.ReadBits(7, extra)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate long zero repeat"); }

                for (uint32 i = 0; i < 11 + extra; ++i)
                {
                    lengths.push_back(0);
                }
            } else { return FStatus::Error(EStatusCode::InvalidData, "invalid dynamic code-length symbol"); }

            if (lengths.size() > hlit + hdist) { return FStatus::Error(EStatusCode::InvalidData, "dynamic code-length table overflow"); }
        }

        std::vector<uint8> ll(lengths.begin(), lengths.begin() + hlit);
        std::vector<uint8> dl(lengths.begin() + hlit, lengths.end());

        if (dl.empty())
        {
            dl.push_back(0);
        }

        s = litLen.Build(ll);

        if (!s.IsOk()) { return s; }

        return dist.Build(dl);
    }

    FStatus InflateCodes(FBitReader& br, const FHuffmanTree& litLen, const FHuffmanTree& dist, FByteArray& out, size_t expectedSize)
    {
        for (;;)
        {
            uint16 sym = 0;

            if (!litLen.Decode(br, sym)) { return FStatus::Error(EStatusCode::InvalidData, "bad deflate literal/length code"); }

            if (sym < 256)
            {
                out.push_back(static_cast<uint8>(sym));
            }
            else if (sym == 256) { return FStatus::Ok(); }
            else if (sym <= 285)
            {
                uint32 idx = sym - 257;

                if (idx >= LengthBase.size()) { return FStatus::Error(EStatusCode::InvalidData, "invalid deflate length code"); }

                uint32 extra = 0;

                if (LengthExtra[idx] && !br.ReadBits(LengthExtra[idx], extra)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate length extra"); }

                uint32 length = LengthBase[idx] + extra;
                uint16 dsym = 0;

                if (!dist.Decode(br, dsym) || dsym >= DistBase.size()) { return FStatus::Error(EStatusCode::InvalidData, "bad deflate distance code"); }

                uint32 dext = 0;

                if (DistExtra[dsym] && !br.ReadBits(DistExtra[dsym], dext)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate distance extra"); }

                uint32 distance = DistBase[dsym] + dext;

                if (distance == 0 || distance > out.size()) { return FStatus::Error(EStatusCode::InvalidData, "invalid deflate back-reference distance"); }

                for (uint32 i = 0; i < length; ++i)
                {
                    out.push_back(out[out.size() - distance]);
                }
            } else { return FStatus::Error(EStatusCode::InvalidData, "invalid deflate symbol"); }

            if (expectedSize && out.size() > expectedSize) { return FStatus::Error(EStatusCode::InvalidData, "deflate output overflow"); }
        }
    }

    uint32 Adler32(const FByteArray& data)
    {
        uint32 a = 1, b = 0;

        for (uint8 byte : data)
        {
            a = (a + byte) % 65521u;
            b = (b + a) % 65521u;
        }

        return (b << 16) | a;
    }
}

TResult<FByteArray> FZlibInflate::DecodeRawDeflate(const uint8* data, size_t size, size_t expectedSize)
{
    FBitReader br(data, size);
    FByteArray out;

    if (expectedSize)
    {
        out.reserve(expectedSize);
    }

    bool finalBlock = false;

    while (!finalBlock)
    {
        uint32 bfinal = 0, btype = 0;

        if (!br.ReadBits(1, bfinal) || !br.ReadBits(2, btype)) { return FStatus::Error(EStatusCode::InvalidData, "truncated deflate block header"); }

        finalBlock = bfinal != 0;

        if (btype == 0)
        {
            br.AlignByte();
            uint8 a=0,b=0,c=0,d=0;

            if (!br.ReadByte(a) || !br.ReadByte(b) || !br.ReadByte(c) || !br.ReadByte(d)) { return FStatus::Error(EStatusCode::InvalidData, "truncated stored deflate block"); }

            uint16 len = static_cast<uint16>(a | (b << 8));
            uint16 nlen = static_cast<uint16>(c | (d << 8));

            if (static_cast<uint16>(len ^ 0xFFFFu) != nlen) { return FStatus::Error(EStatusCode::InvalidData, "stored deflate length check failed"); }

            for (uint32 i = 0; i < len; ++i)
            {
                uint8 ch=0;

                if (!br.ReadByte(ch))
                {
                    return FStatus::Error(EStatusCode::InvalidData, "truncated stored deflate payload");
                }

                out.push_back(ch);
            }
        }
        else if (btype == 1 || btype == 2)
        {
            FHuffmanTree ll, dd;
            FStatus s = btype == 1 ? BuildFixedTrees(ll, dd) : BuildDynamicTrees(br, ll, dd);

            if (!s.IsOk()) { return s; }

            s = InflateCodes(br, ll, dd, out, expectedSize);

            if (!s.IsOk()) { return s; }
        } else { return FStatus::Error(EStatusCode::InvalidData, "reserved deflate block type"); }
    }

    if (expectedSize && out.size() != expectedSize) { return FStatus::Error(EStatusCode::InvalidData, "deflate output size mismatch"); }

    return out;
}

TResult<FByteArray> FZlibInflate::DecodeZlib(const uint8* data, size_t size, size_t expectedSize)
{
    if (size < 6) { return FStatus::Error(EStatusCode::InvalidData, "zlib stream too small"); }

    uint8 cmf = data[0];
    uint8 flg = data[1];

    if ((cmf & 0x0F) != 8) { return FStatus::Error(EStatusCode::Unsupported, "zlib stream is not deflate"); }

    if ((((uint32)cmf << 8) + flg) % 31u != 0) { return FStatus::Error(EStatusCode::InvalidData, "bad zlib header check bits"); }

    if (flg & 0x20) { return FStatus::Error(EStatusCode::Unsupported, "zlib preset dictionary is not supported"); }

    auto raw = DecodeRawDeflate(data + 2, size - 6, expectedSize);

    if (!raw.IsOk()) { return raw.Status(); }

    uint32 storedAdler = (uint32(data[size - 4]) << 24) | (uint32(data[size - 3]) << 16) | (uint32(data[size - 2]) << 8) | uint32(data[size - 1]);
    uint32 actual = Adler32(raw.Value());

    if (storedAdler != actual) { return FStatus::Error(EStatusCode::InvalidData, "zlib Adler32 mismatch"); }

    return raw.Value();
}
