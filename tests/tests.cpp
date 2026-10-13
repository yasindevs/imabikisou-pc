// Self-contained tests: every input is synthesized here, no game data is needed.

#include "core/afs.hpp"
#include "core/crc32.hpp"
#include "core/sfo.hpp"
#include "core/sjis.hpp"
#include "core/sn4.hpp"
#include "core/sns.hpp"

#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>

using namespace imbk;

namespace {

int failures = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

void put16(Bytes& b, std::size_t o, std::uint16_t v) {
    if (b.size() < o + 2) b.resize(o + 2);
    b[o] = v & 0xFF;
    b[o + 1] = v >> 8;
}

void put32(Bytes& b, std::size_t o, std::uint32_t v) {
    if (b.size() < o + 4) b.resize(o + 4);
    for (int i = 0; i < 4; ++i) b[o + i] = (v >> (8 * i)) & 0xFF;
}

void putStr(Bytes& b, std::size_t o, std::string_view s) {
    if (b.size() < o + s.size()) b.resize(o + s.size());
    for (std::size_t i = 0; i < s.size(); ++i) b[o + i] = static_cast<std::uint8_t>(s[i]);
}

void append(Bytes& b, std::initializer_list<int> bytes) {
    for (int x : bytes) b.push_back(static_cast<std::uint8_t>(x));
}

void appendStr(Bytes& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
}

void testCrc32() { CHECK(crc32(ByteSpan(reinterpret_cast<const std::uint8_t*>("123456789"), 9)) == 0xCBF43926); }

void testSjis() {
    const Bytes a = {0x82, 0xA0, 0x81, 0x42}; // "あ。"
    CHECK(sjisToUtf8(a) == "\xE3\x81\x82\xE3\x80\x82");
}

void testSfo() {
    Bytes b;
    putStr(b, 0, std::string_view("\0PSF", 4));
    put32(b, 4, 0x101);
    put32(b, 8, 20 + 2 * 16);       // key table
    put32(b, 12, 20 + 2 * 16 + 16); // data table
    put32(b, 16, 2);
    // entry 0: TITLE_ID (utf8), entry 1: PARENTAL_LEVEL (int)
    put16(b, 20, 0); put16(b, 22, 0x0204); put32(b, 24, 10); put32(b, 28, 16); put32(b, 32, 0);
    put16(b, 36, 9); put16(b, 38, 0x0404); put32(b, 40, 4); put32(b, 44, 4); put32(b, 48, 16);
    putStr(b, 52, std::string_view("TITLE_ID\0LEVEL\0\0", 16));
    putStr(b, 68, std::string_view("ABCD12345\0\0\0\0\0\0\0", 16));
    put32(b, 84, 5);
    const auto m = parseSfo(b);
    CHECK(m.at("TITLE_ID") == "ABCD12345");
    CHECK(m.at("LEVEL") == "5");
}

void testAfs() {
    Bytes b;
    putStr(b, 0, std::string_view("AFS\0", 4));
    put32(b, 4, 2);
    put32(b, 8, 0x800); put32(b, 12, 3);
    put32(b, 16, 0x1000); put32(b, 20, 2);
    put32(b, 24, 0x1800); put32(b, 28, 96); // name table
    putStr(b, 0x800, "abc");
    putStr(b, 0x1000, "xy");
    putStr(b, 0x1800, "first.dds");
    putStr(b, 0x1800 + 48, "second.sfd");
    b.resize(0x1800 + 96);

    const auto path = std::filesystem::temp_directory_path() / "imbk_test.afs";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    {
        const auto afs = Afs::parse(Slice::wholeFile(path));
        CHECK(afs.entries().size() == 2);
        CHECK(afs.entries()[0].name == "first.dds");
        CHECK(afs.entries()[1].name == "second.sfd");
        const auto* e = afs.find("second.sfd");
        CHECK(e && afs.data(*e).readAll() == Bytes({'x', 'y'}));
    }
    std::filesystem::remove(path);
}

// Builds a minimal ScenarioLink file with one scene label and a few instructions.
Bytes makeScript(bool corruptTarget) {
    Bytes b;
    putStr(b, 0, "ScenarioLink");
    putStr(b, 0x10, ":Ver:3.25.2.157");
    put32(b, 0xA0, 1);
    put32(b, 0xA4, 0xAC);
    putStr(b, 0xAC, "SCR-file:Ver:3.25.2.157");
    const std::size_t table = 0xAC + 0x30;
    const std::size_t entry = table + 257 * 4;
    const std::size_t code = entry + 24;
    put32(b, table + 7 * 4, static_cast<std::uint32_t>(entry - 0xAC)); // bucket 7
    put32(b, table + 256 * 4, static_cast<std::uint32_t>(code - 0xAC)); // code offset
    put16(b, entry, 0);
    put16(b, entry + 2, sn4::kSceneLabelTag);
    putStr(b, entry + 8, "AMI_0000_0100");
    b.resize(code);

    append(b, {0x2F, 0x30, 0x31, 0x32});                // two u8 ops
    append(b, {0x85, 0x0F, 0x00});                      // label
    appendStr(b, "AMI_0000_0100");
    append(b, {0x00});
    const auto labelEnd = static_cast<std::uint32_t>(b.size() - code);
    append(b, {0x22, 0x50, 0x00, 0x00, 0x00, 0x3C});    // begin, delay 60
    append(b, {0x02, 0x82, 0xA0, 0x03, 0x09});          // text "あ", wait
    append(b, {0x02, 0x82, 0xA2, 0x03, 0x0B});          // text "い", page
    put32(b, entry + 4, corruptTarget ? labelEnd + 1 : labelEnd);
    put32(b, 0xA8, static_cast<std::uint32_t>(b.size() - 0xAC));
    return b;
}

void testSn4() {
    const auto s = sn4::Script::parse(makeScript(false));
    CHECK(s.version == "3.25.2.157");
    CHECK(s.labels.size() == 1);
    CHECK(s.code.size() == 9);
    CHECK(s.validate().empty());
    const auto lines = s.lines();
    CHECK(lines.size() == 2);
    CHECK(lines[0].id == "AMI_0000_0100#0");
    CHECK(lines[1].id == "AMI_0000_0100#1");
    CHECK(sjisToUtf8(s.args(*lines[1].instr)) == "\xE3\x81\x84");

    CHECK(!sn4::Script::parse(makeScript(true)).validate().empty());

    auto bad = makeScript(false);
    bad[bad.size() - 1] = 0xF0; // unknown opcode
    bool threw = false;
    try {
        sn4::Script::parse(bad);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

void testSns() {
    Bytes plain;
    appendStr(plain, std::string_view("CSD-CSCNV:01.68\0", 16));
    append(plain, {0x00, 0x00, 0x55, 0xFF, 0xE3, 0x80, 0x8C});
    for (int i = 0; i < 40; ++i) plain.push_back(static_cast<std::uint8_t>(i * 7));
    CHECK(sns::version(plain) == "01.68");

    auto enc = plain;
    sns::encrypt(enc);
    CHECK(enc != plain);
    CHECK(std::equal(enc.begin(), enc.begin() + 16, plain.begin())); // header/key is stored in clear
    CHECK(enc[16] == 'C' && enc[17] == 'S');                          // zeros encrypt to the key itself
    CHECK(static_cast<std::uint8_t>(enc[18] - 'D') == 0x55); // byte 18 uses key[18 % 16] = 'D'
    sns::decrypt(enc);
    CHECK(enc == plain);
}

} // namespace

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"crc32", testCrc32}, {"sjis", testSjis}, {"sfo", testSfo}, {"afs", testAfs}, {"sn4", testSn4}, {"sns", testSns},
    };
    for (const auto& [name, fn] : tests) {
        try {
            fn();
        } catch (const std::exception& e) {
            std::cerr << name << ": exception: " << e.what() << "\n";
            ++failures;
        }
    }
    std::cout << (failures ? "FAILED" : "all tests passed") << " (" << failures << " failures)\n";
    return failures ? 1 : 0;
}
