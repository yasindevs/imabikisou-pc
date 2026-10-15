// Imabikiso PC launcher: first-run setup from the user's own disc image,
// language choice, a few settings, and Play. Dear ImGui on Direct3D 11.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shobjidl.h>

#include <cstdio>

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include "installer.hpp"

using namespace launcher;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resizeW = 0, g_resizeH = 0;

void createRtv() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

bool createDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                           &sd, &g_swap, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;
    createRtv();
    return true;
}

LRESULT WINAPI wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(h, msg, w, l)) return true;
    switch (msg) {
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) { g_resizeW = LOWORD(l); g_resizeH = HIWORD(l); }
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

// Native "open file" dialog; returns an empty path when cancelled.
fs::path pickFile(HWND owner, const wchar_t* title, const wchar_t* filterName, const wchar_t* filterSpec) {
    fs::path result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    const COMDLG_FILTERSPEC spec[] = {{filterName, filterSpec}, {L"All files", L"*.*"}};
    dlg->SetFileTypes(2, spec);
    dlg->SetTitle(title);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = p; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

fs::path pickFolder(HWND owner, const wchar_t* title) {
    fs::path result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS);
    dlg->SetTitle(title);
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { result = p; CoTaskMemFree(p); }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void pathRow(const char* label, const fs::path& value, const char* id, bool* clicked) {
    const float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(10 * em);
    *clicked = ImGui::Button(id, ImVec2(6 * em, 0));
    ImGui::SameLine();
    const std::string v = value.empty() ? std::string("(not set)") : toUtf8(value.wstring());
    if (value.empty()) ImGui::TextDisabled("%s", v.c_str());
    else ImGui::TextWrapped("%s", v.c_str());
}

void statusLine(const char* label, bool ok) {
    ImGui::TextColored(ok ? ImVec4(0.35f, 0.85f, 0.6f, 1) : ImVec4(0.9f, 0.7f, 0.3f, 1), ok ? "[ok]" : "[ - ]");
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
}

void style() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowRounding = 0;
    st.FrameRounding = 6;
    st.GrabRounding = 6;
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.07f, 0.09f, 1);
    c[ImGuiCol_Button] = ImVec4(0.16f, 0.21f, 0.27f, 1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.22f, 0.45f, 0.42f, 1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.60f, 0.52f, 1);
    c[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.16f, 0.21f, 1);
    c[ImGuiCol_CheckMark] = ImVec4(0.34f, 0.84f, 0.75f, 1);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.34f, 0.84f, 0.75f, 1);
    c[ImGuiCol_Header] = ImVec4(0.16f, 0.21f, 0.27f, 1);
}

} // namespace

// --install / --play: the same steps without a window, using launcher.ini,
// with progress written to the console (for scripted setups and testing).
static int headless(const std::wstring& mode) {
    // Keep a redirected stdout (file or pipe); otherwise write to the console
    // the launcher was started from.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD type = out && out != INVALID_HANDLE_VALUE ? GetFileType(out) : FILE_TYPE_UNKNOWN;
    if (type != FILE_TYPE_DISK && type != FILE_TYPE_PIPE && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
    }
    const Paths paths = Paths::fromExe();
    Config cfg = Config::load(paths.ini);
    if (cfg.dataDir.empty()) cfg.dataDir = paths.base / "data";
    State& state = *new State;  // shared with detached workers: lives until exit
    if (mode == L"--install") startInstall(paths, cfg, state);
    else startPlay(paths, cfg, state);
    size_t shown = 0;
    float lastP = -2.f;
    for (;;) {
        Sleep(200);
        const bool running = mode == L"--install" ? state.busy.load() : state.gameRunning.load();
        {
            std::lock_guard<std::mutex> lk(state.mu);
            // The deque is bounded; print what is new since last time.
            // `shown` counts every line ever logged; the deque drops old ones.
            if (shown < state.dropped) shown = state.dropped;
            for (; shown < state.dropped + state.log.size(); ++shown)
                std::printf("%s\n", state.log[shown - state.dropped].c_str());
            if (state.progress >= 0 && state.progress - lastP >= 0.05f) {
                std::printf("   %3.0f%%\n", state.progress * 100);
                lastP = state.progress;
            }
        }
        std::fflush(stdout);
        if (!running) break;
    }
    std::lock_guard<std::mutex> lk(state.mu);
    return state.error.empty() ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    if (args.find(L"--install") != std::wstring::npos) return headless(L"--install");
    if (args.find(L"--play") != std::wstring::npos) return headless(L"--play");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ImGui_ImplWin32_EnableDpiAwareness();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc{sizeof wc, CS_CLASSDC, wndProc, 0, 0, inst, nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr,
                   nullptr, L"ImabikisoLauncher", nullptr};
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Imabikiso PC", WS_OVERLAPPEDWINDOW, 100, 100, (int)(900 * dpi),
                              (int)(760 * dpi), nullptr, nullptr, inst, nullptr);
    if (!createDevice(hwnd)) {
        MessageBoxW(nullptr, L"Direct3D 11 is not available.", L"Imabikiso PC", MB_ICONERROR);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    style();
    ImGui::GetStyle().ScaleAllSizes(dpi);
    ImFontConfig fc;
    fc.SizePixels = 15.f * dpi;
    {
        // Segoe UI when present; Japanese glyphs are not needed in the UI.
        wchar_t windir[MAX_PATH];
        GetWindowsDirectoryW(windir, MAX_PATH);
        const fs::path seg = fs::path(windir) / "Fonts" / "segoeui.ttf";
        if (fs::exists(seg)) io.Fonts->AddFontFromFileTTF(toUtf8(seg.wstring()).c_str(), 17.f * dpi);
        else io.Fonts->AddFontDefault(&fc);
    }
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    const Paths paths = Paths::fromExe();
    Config cfg = Config::load(paths.ini);
    if (cfg.dataDir.empty()) cfg.dataDir = paths.base / "data";
    State& state = *new State;  // shared with detached workers: lives until exit
    Status status = queryStatus(paths, cfg);
    bool wasBusy = false;
    int frame = 0;

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_resizeW) {
            if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
            g_swap->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            createRtv();
        }
        const bool busy = state.busy;
        if ((wasBusy && !busy) || ++frame % 120 == 0) status = queryStatus(paths, cfg);
        wasBusy = busy;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextColored(ImVec4(0.34f, 0.84f, 0.75f, 1), "IMABIKISO / PC PORT");
        ImGui::SetWindowFontScale(1.6f);
        ImGui::TextUnformatted("Imabikiso PC");
        ImGui::SetWindowFontScale(1.f);
        ImGui::TextDisabled("Unofficial native port. Bring your own copy of the game (PS3, BLJM60042).");
        ImGui::Separator();

        bool changed = false, click = false;
        ImGui::BeginDisabled(busy || state.gameRunning);
        ImGui::SeparatorText("1. Your game");
        pathRow("Disc image (.iso)", cfg.iso, "Browse##iso", &click);
        if (click) if (auto f = pickFile(hwnd, L"Choose your Imabikiso disc image", L"Disc images", L"*.iso"); !f.empty()) { cfg.iso = f; changed = true; }
        pathRow("RPCS3 (rpcs3.exe)", cfg.rpcs3, "Browse##rpcs3", &click);
        if (click) if (auto f = pickFile(hwnd, L"Choose rpcs3.exe (used once to decrypt EBOOT.BIN)", L"rpcs3.exe", L"rpcs3.exe"); !f.empty()) { cfg.rpcs3 = f; changed = true; }
        ImGui::TextDisabled("RPCS3 is only used once, to decrypt EBOOT.BIN. You can give a decrypted EBOOT.ELF instead:");
        pathRow("or EBOOT.ELF", cfg.eboot, "Browse##elf", &click);
        if (!cfg.eboot.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##elf")) { cfg.eboot.clear(); changed = true; }
        }
        if (click) if (auto f = pickFile(hwnd, L"Choose a decrypted EBOOT.ELF", L"ELF", L"*.elf"); !f.empty()) { cfg.eboot = f; changed = true; }
        pathRow("Data folder", cfg.dataDir, "Change##data", &click);
        if (click) if (auto f = pickFolder(hwnd, L"Where to extract the game (needs about 10 GB)"); !f.empty()) { cfg.dataDir = f; changed = true; }

        ImGui::SeparatorText("2. Language");
        int lang = cfg.language == "ja" ? 0 : 1;
        if (ImGui::RadioButton("Japanese (original)", lang == 0)) { cfg.language = "ja"; changed = true; }
        ImGui::SameLine(0, 30);
        if (ImGui::RadioButton("English (machine-translation draft, first scenes only)", lang == 1)) { cfg.language = "en"; changed = true; }

        ImGui::SeparatorText("3. Settings");
        changed |= ImGui::Checkbox("VSync", &cfg.vsync);
        ImGui::SameLine(0, 30);
        changed |= ImGui::Checkbox("Mute sound", &cfg.mute);
        ImGui::EndDisabled();
        if (changed) {
            cfg.save(paths.ini);
            status = queryStatus(paths, cfg);
        }

        ImGui::SeparatorText("Status");
        statusLine("Game files extracted", status.discReady);
        statusLine("Game executable decrypted and verified", status.elfReady);
        statusLine(cfg.language == "ja" ? "Japanese version" : "English version prepared", status.langReady);
        statusLine("Port executable (game\\ImabikisoPC.exe)", status.gameExe);

        const bool ready = status.discReady && status.elfReady && status.langReady && status.gameExe;
        ImGui::Spacing();
        if (busy) {
            if (ImGui::Button("Cancel", ImVec2(140 * dpi, 40 * dpi))) state.cancel = true;
        } else if (!ready) {
            ImGui::BeginDisabled(state.gameRunning);
            if (ImGui::Button("Install", ImVec2(140 * dpi, 40 * dpi))) startInstall(paths, cfg, state);
            ImGui::EndDisabled();
        } else {
            ImGui::BeginDisabled(state.gameRunning);
            if (ImGui::Button(state.gameRunning ? "Running..." : "Play", ImVec2(140 * dpi, 40 * dpi)))
                startPlay(paths, cfg, state);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Saves folder", ImVec2(0, 40 * dpi))) openFolder(paths.profiles / cfg.language / "gamedata");
        ImGui::SameLine();
        if (ImGui::Button("Logs", ImVec2(0, 40 * dpi))) openFolder(paths.logs);

        {
            std::lock_guard<std::mutex> lk(state.mu);
            if (busy || !state.step.empty()) {
                ImGui::TextUnformatted(state.step.c_str());
                if (state.progress < 0) {
                    const float t = (float)ImGui::GetTime();
                    ImGui::ProgressBar(-1.f * t, ImVec2(-1, 0), "working...");
                } else {
                    ImGui::ProgressBar(state.progress, ImVec2(-1, 0));
                }
            }
            if (!state.error.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", state.error.c_str());
            ImGui::TextDisabled("Controls: X = confirm, Z = back, arrows = move, Enter = Start (Xbox: B = confirm).");
            ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (const auto& l : state.log) ImGui::TextUnformatted(l.c_str());
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
        ImGui::End();

        ImGui::Render();
        const float clear[4] = {0.05f, 0.07f, 0.09f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    state.cancel = true;
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv) g_rtv->Release();
    g_swap->Release();
    g_ctx->Release();
    g_dev->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    CoUninitialize();
    return 0;
}
