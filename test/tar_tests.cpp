// Copyright (c) 2026 The OmniLite Standalone developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <tar.h>

#include <test/util.h>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

namespace {

std::map<std::string, std::string> ReadAll(const std::string& path)
{
    std::map<std::string, std::string> files;
    TarReader r(path);
    std::string name;
    uint64_t size = 0;
    while (r.Next(name, size)) {
        std::string data;
        r.Read([&](const char* d, size_t n) { data.append(d, n); });
        BOOST_CHECK_EQUAL(data.size(), size);
        files[name] = data;
    }
    return files;
}

/** Replaces the name of the first entry and fixes its checksum: an archive our writer would never make. */
void Rename(const std::string& path, const std::string& name, bool fixChecksum)
{
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    char h[512];
    f.read(h, 512);
    std::fill(h, h + 100, 0);
    std::copy(name.begin(), name.end(), h);
    if (fixChecksum) {
        unsigned int sum = 0;
        for (int i = 0; i < 512; ++i) sum += (i >= 148 && i < 156) ? ' ' : static_cast<unsigned char>(h[i]);
        std::snprintf(h + 148, 8, "%06o", sum);
        h[155] = ' ';
    }
    f.seekp(0);
    f.write(h, 512);
}

bool FailsWith(const std::string& path, const std::string& text)
{
    try {
        ReadAll(path);
    } catch (const TarError& e) {
        return std::string(e.what()).find(text) != std::string::npos;
    }
    return false;
}

} // namespace

BOOST_AUTO_TEST_SUITE(tar_tests)

BOOST_AUTO_TEST_CASE(round_trip)
{
    TempDir dir;
    const std::string longName = "main/omnicore/" + std::string(120, 'd') + "/file.ldb";
    {
        std::ofstream(dir.Sub("big")) << std::string(70000, 'x');
    }
    const std::string archive = dir.Sub("a.tar");
    TarWriter w(archive);
    w.Add("SNAPSHOT.json", "{}\n");
    w.Add("main/empty", "");
    BOOST_CHECK_EQUAL(w.AddFile(longName, dir.Sub("big")), FileSHA256(dir.Sub("big")));
    BOOST_CHECK_THROW(w.Add("../evil", "x"), TarError);
    w.Finish();

    std::map<std::string, std::string> files = ReadAll(archive);
    BOOST_REQUIRE_EQUAL(files.size(), 3u);
    BOOST_CHECK_EQUAL(files["SNAPSHOT.json"], "{}\n");
    BOOST_CHECK_EQUAL(files["main/empty"], "");
    BOOST_CHECK_EQUAL(files[longName], std::string(70000, 'x'));

    // A plain tar reads it too.
    BOOST_CHECK_EQUAL(std::system(("tar -tf " + archive + " > /dev/null").c_str()), 0);
}

BOOST_AUTO_TEST_CASE(reader_refuses_what_could_escape)
{
    TempDir dir;
    auto make = [&](const std::string& path) {
        TarWriter w(path);
        w.Add("main/file", "data");
        w.Finish();
    };
    for (const std::string bad : {"../etc/passwd", "/etc/passwd", "main/../../x", "main//x"}) {
        const std::string path = dir.Sub("bad.tar");
        make(path);
        Rename(path, bad, true);
        BOOST_CHECK_MESSAGE(FailsWith(path, "unsafe name"), bad);
    }
    // A damaged header.
    make(dir.Sub("damaged.tar"));
    Rename(dir.Sub("damaged.tar"), "main/fila", false);
    BOOST_CHECK(FailsWith(dir.Sub("damaged.tar"), "checksum"));
    // Links.
    const std::string links = dir.path + "/links";
    BOOST_REQUIRE_EQUAL(std::system(("mkdir " + links + " && ln -s /etc/passwd " + links + "/l && tar --format=ustar -C " + links +
                                     " -cf " + dir.Sub("link.tar") + " l").c_str()), 0);
    BOOST_CHECK(FailsWith(dir.Sub("link.tar"), "only regular files"));
    // Cut off.
    make(dir.Sub("cut.tar"));
    std::filesystem::resize_file(dir.Sub("cut.tar"), 512);
    BOOST_CHECK(FailsWith(dir.Sub("cut.tar"), "truncated"));
}

BOOST_AUTO_TEST_SUITE_END()
