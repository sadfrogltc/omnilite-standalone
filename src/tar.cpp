// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tar.h>

#include <crypto/sha256.h>
#include <util/strencodings.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <vector>

namespace {

const size_t BLOCK = 512;

/** An unsigned number as zero-padded octal in a field of width bytes (with the terminating NUL). */
void Octal(char* field, size_t width, uint64_t value)
{
    std::string s(width - 1, '0');
    for (size_t i = width - 1; i-- > 0 && value > 0; value >>= 3) s[i] = static_cast<char>('0' + (value & 7));
    if (value > 0) throw TarError("value too large for a tar header");
    std::memcpy(field, s.data(), width - 1);
    field[width - 1] = '\0';
}

uint64_t ParseOctal(const char* field, size_t width)
{
    uint64_t v = 0;
    size_t i = 0;
    while (i < width && field[i] == ' ') ++i;
    for (; i < width && field[i] >= '0' && field[i] <= '7'; ++i) v = (v << 3) + static_cast<uint64_t>(field[i] - '0');
    if (i < width && field[i] != '\0' && field[i] != ' ') throw TarError("bad number in tar header");
    return v;
}

unsigned int Checksum(const char* header)
{
    unsigned int sum = 0;
    for (size_t i = 0; i < BLOCK; ++i) {
        sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(header[i]);
    }
    return sum;
}

std::string Field(const char* field, size_t width)
{
    return std::string(field, strnlen(field, width));
}

/** Relative, without "." or ".." parts and without empty parts. */
bool SafeName(const std::string& name)
{
    if (name.empty() || name.front() == '/' || name.back() == '/') return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        std::string part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string Hex(CSHA256& hasher)
{
    unsigned char hash[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(hash);
    return HexStr(hash, hash + sizeof(hash));
}

} // namespace

TarWriter::TarWriter(const std::string& path) : m_out(path, std::ios::binary | std::ios::trunc), m_path(path)
{
    if (!m_out) throw TarError("cannot create " + path);
}

void TarWriter::Header(const std::string& name, uint64_t size)
{
    if (!SafeName(name)) throw TarError("bad name for a tar entry: " + name);
    char h[BLOCK] = {};
    // Names up to 255 bytes: the directory part goes into the prefix field.
    std::string prefix, base = name;
    if (name.size() > 100) {
        size_t split = name.rfind('/', 155);
        if (split == std::string::npos || name.size() - split - 1 > 100) throw TarError("name too long for tar: " + name);
        prefix = name.substr(0, split);
        base = name.substr(split + 1);
    }
    std::memcpy(h, base.data(), base.size());
    Octal(h + 100, 8, 0644);
    Octal(h + 108, 8, 0);
    Octal(h + 116, 8, 0);
    Octal(h + 124, 12, size);
    Octal(h + 136, 12, static_cast<uint64_t>(std::time(nullptr)));
    h[156] = '0';
    std::memcpy(h + 257, "ustar", 6);
    std::memcpy(h + 263, "00", 2);
    std::memcpy(h + 345, prefix.data(), prefix.size());
    char sum[8];
    Octal(sum, 7, Checksum(h));
    std::memcpy(h + 148, sum, 7);
    h[155] = ' ';
    m_out.write(h, BLOCK);
}

void TarWriter::Pad(uint64_t size)
{
    static const char zeros[BLOCK] = {};
    if (size % BLOCK) m_out.write(zeros, BLOCK - size % BLOCK);
}

void TarWriter::Add(const std::string& name, const std::string& data)
{
    Header(name, data.size());
    m_out.write(data.data(), data.size());
    Pad(data.size());
    if (!m_out) throw TarError("cannot write " + m_path);
}

std::string TarWriter::AddFile(const std::string& name, const std::string& source)
{
    std::ifstream in(source, std::ios::binary | std::ios::ate);
    if (!in) throw TarError("cannot read " + source);
    const uint64_t size = static_cast<uint64_t>(in.tellg());
    in.seekg(0);
    Header(name, size);
    CSHA256 hasher;
    std::vector<char> buf(1 << 16);
    uint64_t left = size;
    while (left > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(left, buf.size()));
        if (!in.read(buf.data(), n)) throw TarError("cannot read " + source);
        hasher.Write(reinterpret_cast<const unsigned char*>(buf.data()), n);
        m_out.write(buf.data(), n);
        left -= n;
    }
    Pad(size);
    if (!m_out) throw TarError("cannot write " + m_path);
    return Hex(hasher);
}

void TarWriter::Finish()
{
    static const char zeros[2 * BLOCK] = {};
    m_out.write(zeros, sizeof(zeros));
    m_out.close();
    if (!m_out) throw TarError("cannot write " + m_path);
}

TarReader::TarReader(const std::string& path) : m_in(path, std::ios::binary)
{
    if (!m_in) throw TarError("cannot read " + path);
}

bool TarReader::Next(std::string& name, uint64_t& size)
{
    if (m_pending) throw TarError("tar entry not read");
    char h[BLOCK];
    if (!m_in.read(h, BLOCK)) throw TarError("archive ends without its end marker");
    bool zero = true;
    for (char c : h) zero = zero && c == 0;
    if (zero) return false;
    if (ParseOctal(h + 148, 8) != Checksum(h)) throw TarError("tar header checksum mismatch");
    if (std::memcmp(h + 257, "ustar", 5) != 0) throw TarError("not a ustar archive");
    const char type = h[156];
    const std::string prefix = Field(h + 345, 155);
    name = (prefix.empty() ? "" : prefix + "/") + Field(h, 100);
    size = ParseOctal(h + 124, 12);
    if (type != '0' && type != '\0') throw TarError("only regular files are allowed in a snapshot: " + name);
    if (!SafeName(name)) throw TarError("unsafe name in archive: " + name);
    m_left = size;
    m_pending = true;
    return true;
}

void TarReader::Read(const std::function<void(const char*, size_t)>& sink)
{
    if (!m_pending) throw TarError("no tar entry to read");
    std::vector<char> buf(1 << 16);
    const uint64_t size = m_left;
    while (m_left > 0) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(m_left, buf.size()));
        if (!m_in.read(buf.data(), n)) throw TarError("archive is truncated");
        sink(buf.data(), n);
        m_left -= n;
    }
    if (size % BLOCK) m_in.ignore(static_cast<std::streamsize>(BLOCK - size % BLOCK));
    m_pending = false;
}

std::string FileSHA256(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw TarError("cannot read " + path);
    CSHA256 hasher;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), buf.size());
        hasher.Write(reinterpret_cast<const unsigned char*>(buf.data()), static_cast<size_t>(in.gcount()));
    }
    return Hex(hasher);
}
