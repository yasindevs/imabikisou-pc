// imbk_player: first playable prototype (Windows, GDI). Plays the main script from the user's disc:
// pictures, transitions and text. Sound and movies are not implemented yet.
//
//   imbk_player <game> [label]                       window; click / Enter / Space = next, Ctrl = fast forward
//   imbk_player <game> [label] --shot out.bmp [--frames N]   no window: run N frames (auto-click), save a frame

#include "core/afs.hpp"
#include "core/game.hpp"
#include "core/sns.hpp"
#include "engine/player.hpp"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <iostream>
#include <unordered_map>

using namespace imbk;
using engine::Image;

namespace {

constexpr int kWidth = 1280, kHeight = 720;
// Text area of the normal (ADV) text, measured on the PS3 version.
constexpr int kTextLeft = 131, kTextTop = 72, kTextWidth = 1015, kLineHeight = 66, kFontSize = 44;

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Pictures by script name ("x.psd" -> scriptdatachunk.afs entry "x.dds"), decoded once and scaled to 1280x720.
class PictureCache {
public:
    explicit PictureCache(const Game& game) : afs_(Afs::parse(game.chunk("scriptdatachunk.afs"))) {
        for (const auto& e : afs_.entries()) byName_[e.name] = &e;
    }

    std::shared_ptr<const Image> get(const std::string& psdName) {
        if (const auto it = cache_.find(psdName); it != cache_.end()) return it->second;
        std::string dds = psdName;
        if (dds.ends_with(".psd")) dds.replace(dds.size() - 4, 4, ".dds");
        std::shared_ptr<const Image> img;
        if (const auto it = byName_.find(dds); it != byName_.end()) {
            try {
                auto decoded = engine::decodeDds(afs_.data(*it->second).readAll());
                img = std::make_shared<Image>(engine::resize(decoded, kWidth, kHeight));
            } catch (const std::exception& e) {
                std::cerr << "picture " << dds << ": " << e.what() << "\n";
            }
        } else {
            std::cerr << "picture not found: " << dds << "\n";
        }
        cache_[psdName] = img;
        return img;
    }

private:
    Afs afs_;
    std::unordered_map<std::string, const Afs::Entry*> byName_;
    std::unordered_map<std::string, std::shared_ptr<const Image>> cache_;
};

class Renderer {
public:
    explicit Renderer(const Game& game) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = kWidth;
        bi.bmiHeader.biHeight = -kHeight; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        dc_ = CreateCompatibleDC(nullptr);
        bitmap_ = CreateDIBSection(dc_, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
        SelectObject(dc_, bitmap_);

        // The game's text font, loaded from the user's disc (never shipped).
        const auto sys = Afs::parse(game.chunk("systemchunk.afs"));
        if (const auto* e = sys.find("sn4_p03.TTF")) {
            fontData_ = sys.data(*e).readAll();
            DWORD count = 0;
            AddFontMemResourceEx(fontData_.data(), static_cast<DWORD>(fontData_.size()), nullptr, &count);
        }
        font_ = makeFont(kFontSize, L"sn4_p03");
        small_ = makeFont(20, L"sn4_p03");
        SetBkMode(dc_, TRANSPARENT);
    }

    ~Renderer() {
        DeleteObject(font_);
        DeleteObject(small_);
        DeleteObject(bitmap_);
        DeleteDC(dc_);
    }

    HDC dc() const { return dc_; }
    const std::uint32_t* pixels() const { return pixels_; }

    void draw(const engine::Player& p) {
        std::fill(pixels_, pixels_ + kWidth * kHeight, 0xFF000000u);
        for (const auto& [_, layer] : p.layers()) {
            if (layer.previous) blit(*layer.previous, 255);
            if (layer.image) blit(*layer.image, static_cast<int>(layer.progress() * 255));
        }
        GdiFlush();

        SelectObject(dc_, small_);
        int capY = 12;
        for (const auto& [_, layer] : p.layers())
            if (!layer.caption.empty()) {
                drawText(widen(layer.caption), 16, capY, RGB(220, 220, 220));
                capY += 26;
            }

        SelectObject(dc_, font_);
        POINT end{kTextLeft, kTextTop};
        for (const auto& b : p.page()) {
            const int x = b.positioned ? b.x : kTextLeft, y = b.positioned ? b.y : kTextTop;
            const int width = b.positioned ? kWidth - b.x - 40 : kTextWidth;
            end = drawWrapped(widen(b.text), x, y, width);
        }
        if (p.waitingForClick()) drawText(L"\x25B6", end.x + 4, end.y, RGB(230, 230, 230)); // ▶

        SelectObject(dc_, small_);
        const auto status = "label " + p.label() + (p.bgm().empty() ? "" : "   BGM " + p.bgm());
        drawText(widen(status), 12, kHeight - 30, RGB(150, 150, 150));
        GdiFlush();
    }

    void saveBmp(const std::string& path) const {
        BITMAPFILEHEADER fh{};
        BITMAPINFOHEADER ih{};
        ih.biSize = sizeof ih;
        ih.biWidth = kWidth;
        ih.biHeight = -kHeight;
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof fh + sizeof ih;
        fh.bfSize = fh.bfOffBits + kWidth * kHeight * 4;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&fh), sizeof fh);
        out.write(reinterpret_cast<const char*>(&ih), sizeof ih);
        out.write(reinterpret_cast<const char*>(pixels_), kWidth * kHeight * 4);
    }

private:
    static HFONT makeFont(int size, const wchar_t* face) {
        return CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    }

    void blit(const Image& img, int alpha) {
        if (alpha <= 0 || img.width != kWidth || img.height != kHeight) return;
        for (std::size_t i = 0; i < img.pixels.size(); ++i) {
            const std::uint32_t s = img.pixels[i];
            const std::uint32_t a = (s >> 24) * static_cast<std::uint32_t>(alpha) / 255;
            if (a == 255) {
                pixels_[i] = s;
            } else if (a) {
                const std::uint32_t d = pixels_[i];
                const auto ch = [&](int sh) { return ((((s >> sh) & 0xFF) * a + ((d >> sh) & 0xFF) * (255 - a)) / 255) << sh; };
                pixels_[i] = 0xFF000000u | ch(16) | ch(8) | ch(0);
            }
        }
    }

    void drawText(const std::wstring& s, int x, int y, COLORREF color) {
        SetTextColor(dc_, RGB(0, 0, 0));
        for (const auto [dx, dy] : {std::pair{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {2, 2}})
            TextOutW(dc_, x + dx, y + dy, s.c_str(), static_cast<int>(s.size()));
        SetTextColor(dc_, color);
        TextOutW(dc_, x, y, s.c_str(), static_cast<int>(s.size()));
    }

    // Draws text with explicit '\n' breaks and wrapping at `width`; returns the pen position after it.
    POINT drawWrapped(const std::wstring& text, int x, int y, int width) {
        POINT pen{x, y};
        std::size_t start = 0;
        while (start <= text.size()) {
            const auto nl = text.find(L'\n', start);
            std::wstring line = text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
            do {
                int fit = 0;
                SIZE size{};
                GetTextExtentExPointW(dc_, line.c_str(), static_cast<int>(line.size()), width, &fit, nullptr, &size);
                if (fit <= 0) fit = line.empty() ? 0 : 1;
                const auto part = line.substr(0, static_cast<std::size_t>(fit));
                drawText(part, x, pen.y, RGB(255, 255, 255));
                GetTextExtentPoint32W(dc_, part.c_str(), static_cast<int>(part.size()), &size);
                pen.x = x + size.cx;
                line.erase(0, static_cast<std::size_t>(fit));
                if (!line.empty()) pen.y += kLineHeight;
            } while (!line.empty());
            if (nl == std::wstring::npos) break;
            pen.y += kLineHeight;
            pen.x = x;
            start = nl + 1;
        }
        return pen;
    }

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    std::uint32_t* pixels_ = nullptr;
    Bytes fontData_;
    HFONT font_ = nullptr, small_ = nullptr;
};

sns::Script loadScript(const Game& game) {
    const auto afs = Afs::parse(game.chunk("residentchunk.afs"));
    const auto* e = afs.find("st.sns");
    if (!e) throw std::runtime_error("st.sns not found");
    auto data = afs.data(*e).readAll();
    sns::decrypt(data);
    return sns::Script::parse(std::move(data));
}

engine::Player* g_player = nullptr;

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (g_player) g_player->advance();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        else if ((wp == VK_RETURN || wp == VK_SPACE) && g_player) g_player->advance();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int runWindow(engine::Player& player, Renderer& renderer) {
    g_player = &player;
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"imbk_player";
    RegisterClassW(&wc);
    RECT r{0, 0, kWidth, kHeight};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Imabikis\x014D", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, wc.hInstance, nullptr);

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    std::string lastTitle;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        player.update(GetKeyState(VK_CONTROL) < 0);
        renderer.draw(player);

        RECT client;
        GetClientRect(hwnd, &client);
        HDC wdc = GetDC(hwnd);
        SetStretchBltMode(wdc, HALFTONE);
        StretchBlt(wdc, 0, 0, client.right, client.bottom, renderer.dc(), 0, 0, kWidth, kHeight, SRCCOPY);
        ReleaseDC(hwnd, wdc);

        const auto title = "Imabikis\xC5\x8D  -  " + player.label() + (player.finished() ? "  (fin du script)" : "");
        if (title != lastTitle) {
            SetWindowTextW(hwnd, widen(title).c_str());
            lastTitle = title;
        }
        // ~60 frames per second
        for (;;) {
            QueryPerformanceCounter(&now);
            const double ms = (now.QuadPart - last.QuadPart) * 1000.0 / freq.QuadPart;
            if (ms >= 1000.0 / 60) break;
            if (ms < 14) Sleep(1);
        }
        last = now;
    }
}

} // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) {
        std::cout << "usage: imbk_player <game> [label] [--shot out.bmp [--frames N]]\n";
        return 2;
    }
    try {
        std::string label = "S00A_0000_0100", shot;
        int frames = 600;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--shot" && i + 1 < argc) shot = argv[++i];
            else if (arg == "--frames" && i + 1 < argc) frames = std::stoi(argv[++i]);
            else label = arg;
        }
        const auto game = Game::open(argv[1]);
        const auto script = loadScript(game);
        PictureCache pictures(game);
        engine::Player player(script, [&](const std::string& name) { return pictures.get(name); });
        player.start(label);
        Renderer renderer(game);

        int result = 0;
        if (!shot.empty()) {
            // Headless run: click automatically one second after each wait.
            int waited = 0;
            for (int f = 0; f < frames && !player.finished(); ++f) {
                player.update(false);
                if (player.waitingForClick() && ++waited >= 60) {
                    waited = 0;
                    if (f + 1 < frames) player.advance();
                }
            }
            renderer.draw(player);
            renderer.saveBmp(shot);
            std::cout << "frame saved to " << shot << " (label " << player.label() << ")\n";
        } else {
            result = runWindow(player, renderer);
        }
        std::cout << "not implemented yet: " << player.skippedSummary() << "\n";
        return result;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
