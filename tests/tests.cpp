// Self-contained tests: every input is synthesized here, no game data is needed.

#include "core/afs.hpp"
#include "core/crc32.hpp"
#include "core/sfo.hpp"
#include "core/sjis.hpp"
#include "core/sn4.hpp"
#include "core/sns.hpp"
#include "core/snspatch.hpp"

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

// Builds a decrypted st.sns container with one script file, one label and resource tables.
Bytes makeSnsContainer(bool badTarget) {
    Bytes b;
    appendStr(b, std::string_view("CSD-CSCNV:01.68\0", 16));
    putStr(b, 0xE0, "L1");
    const std::size_t file = 0x110;
    put32(b, 0x100, 1);
    put32(b, 0x104, file);

    putStr(b, file, "test.xml");
    const std::size_t table = file + 0x30;
    const std::size_t entry = table + 257 * 4;
    const std::size_t code = entry + 12; // 8-byte entry header + "L1\0" padded to 4
    put32(b, table + 3 * 4, static_cast<std::uint32_t>(entry - file));
    put32(b, table + 256 * 4, static_cast<std::uint32_t>(code - file));
    put16(b, entry, 0);
    put16(b, entry + 2, scr::kSceneLabelTag);
    put32(b, entry + 4, badTarget ? 5 : 6); // just after the label instruction below
    putStr(b, entry + 8, std::string_view("L1\0\0", 4));
    b.resize(code);

    append(b, {0x4D, 0x04, 0x01, 'L', '1', 0x00});         // label L1
    append(b, {0x7E, 24});                                 // message: 18 bytes of params, u16, u8, id "m1"
    b.insert(b.end(), 20, 0);
    append(b, {0x01, 'm', '1', 0x00});
    append(b, {0x01, 0xE3, 0x81, 0x82, 0x02});             // text "あ"
    append(b, {0x37, 0x01, 0x00});                         // name 0
    append(b, {0x17, 0x00, 0x7F, 0x00});                   // wait, message end
    append(b, {0x01, 'h', 'i', 0x02, 0x17, 0x00, 0x1A, 0x00}); // narration "hi", wait, newline
    append(b, {0x18, 0x01, 'y', 0x02, 0x19, 0x01, 'Y', 0x02, 0x1B, 0x00}); // ruby y over Y, page
    append(b, {0x9C, 0x00});                               // return
    while (b.size() % 4) b.push_back(0);
    put32(b, 0x108, static_cast<std::uint32_t>(b.size() - file));

    put32(b, 0x20, static_cast<std::uint32_t>(b.size())); // parameter table (unknown format), first after the files
    append(b, {0, 0, 0, 0});
    // String tables: aisac, then pictures / movies / bgm / sounds; each holds one name.
    const char* names[] = {"aisac", "pic.psd", "mov.sfd", "bgm.adx", "snd"};
    for (int i = 0; i < 5; ++i) {
        const auto t = static_cast<std::uint32_t>(b.size());
        put32(b, t, 0);
        const std::size_t ptr = i == 0 ? 0x28 : 0x60 + (i - 1) * 8;
        put32(b, ptr, t);
        put32(b, ptr + 4, t + 4);
        appendStr(b, names[i]);
        b.push_back(0);
    }
    return b;
}

void testSnsContainer() {
    auto data = makeSnsContainer(false);
    sns::encrypt(data);
    sns::decrypt(data);
    const auto s = sns::Script::parse(data);
    CHECK(s.version == "01.68");
    CHECK(s.startLabel == "L1");
    CHECK(s.files.size() == 1 && s.files[0].name == "test.xml");
    CHECK(s.resources.pictures == std::vector<std::string>{"pic.psd"});
    CHECK(s.resources.sounds == std::vector<std::string>{"snd"});
    CHECK(s.resources.aisac == std::vector<std::string>{"aisac"});
    CHECK(s.files[0].code.back().opcode == sns::op::Return);
    CHECK(s.validate().empty());

    const auto lines = s.lines();
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(lines[0].id == "m1" && lines[0].text == "\xE3\x81\x82<name:00>");
        CHECK(lines[1].id == "L1#0" && lines[1].text == "hi");
        CHECK(lines[2].id == "L1#1" && lines[2].text == "\n<ruby:y>Y");
    }
    CHECK(!sns::Script::parse(makeSnsContainer(true)).validate().empty());
}

void testEncodeLine() {
    CHECK(sns::encodeLine("hi") == Bytes({0x01, 'h', 'i', 0x02}));
    CHECK(sns::encodeLine("a\nb") == Bytes({0x01, 'a', 0x02, 0x1A, 0x00, 0x01, 'b', 0x02}));
    CHECK(sns::encodeLine("<ruby:y>Y") == Bytes({0x18, 0x01, 'y', 0x02, 0x19, 0x01, 'Y', 0x02}));
    CHECK(sns::encodeLine("<name:05>!") == Bytes({0x37, 0x01, 0x05, 0x01, '!', 0x02}));
    CHECK(sns::encodeLine("x<3F:02FF>") == Bytes({0x01, 'x', 0x02, 0x3F, 0x02, 0x02, 0xFF}));
    CHECK(sns::encodeLine("1 < 2") == Bytes({0x01, '1', ' ', '<', ' ', '2', 0x02})); // not a tag
    bool threw = false;
    try {
        sns::encodeLine("<01:00>");
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

void testSnsPatch() {
    const auto original = sns::Script::parse(makeSnsContainer(false));
    sns::PatchReport report;
    const auto patched = sns::applyTranslations(
        original, {{"m1", "\xE3\x81\x82<name:00>"}, {"L1#0", "a much longer line"}, {"L1#1", "\n<ruby:y>Y"}}, {}, report);
    CHECK(report.errors.empty());
    CHECK(report.replaced == 1 && report.unchanged == 2);
    if (!report.errors.empty()) return;
    const auto s = sns::Script::parse(patched);
    CHECK(s.validate().empty());
    const auto lines = s.lines();
    CHECK(lines.size() == 3 && lines[1].text == "a much longer line");
    CHECK(s.resources.sounds == std::vector<std::string>{"snd"}); // tables moved with the size change

    sns::PatchReport bad;
    sns::applyTranslations(original, {{"nope", "x"}}, {}, bad);
    CHECK(bad.errors.size() == 1);
}

void testAfsRebuild() {
    Bytes b;
    putStr(b, 0, std::string_view("AFS\0", 4));
    put32(b, 4, 2);
    put32(b, 8, 0x800); put32(b, 12, 3);
    put32(b, 16, 0x1000); put32(b, 20, 2);
    put32(b, 24, 0x1800); put32(b, 28, 96);
    putStr(b, 0x800, "abc");
    putStr(b, 0x1000, "xy");
    putStr(b, 0x1800, "first.dds");
    put32(b, 0x1800 + 44, 3);
    putStr(b, 0x1800 + 48, "second.sfd");
    put32(b, 0x1800 + 48 + 44, 2);
    b.resize(0x2000);

    const auto path = std::filesystem::temp_directory_path() / "imbk_test_rebuild.afs";
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    {
        const auto afs = Afs::parse(Slice::wholeFile(path));
        CHECK(afs.rebuild({}) == b);
        const auto bigger = afs.rebuild({{"first.dds", Bytes(0x900, 'z')}});
        const auto out = std::filesystem::temp_directory_path() / "imbk_test_rebuild2.afs";
        std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(bigger.data()), static_cast<std::streamsize>(bigger.size()));
        {
            const auto again = Afs::parse(Slice::wholeFile(out));
            CHECK(again.entries()[0].size == 0x900);
            CHECK(again.entries()[1].offset == 0x1800 && again.data(again.entries()[1]).readAll() == Bytes({'x', 'y'}));
            CHECK(again.entries()[1].name == "second.sfd");
        }
        std::filesystem::remove(out);
    }
    std::filesystem::remove(path);
}

} // namespace

int main() {
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"crc32", testCrc32}, {"sjis", testSjis}, {"sfo", testSfo}, {"afs", testAfs}, {"sn4", testSn4}, {"sns", testSns}, {"sns container", testSnsContainer}, {"encode line", testEncodeLine}, {"sns patch", testSnsPatch}, {"afs rebuild", testAfsRebuild},
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
