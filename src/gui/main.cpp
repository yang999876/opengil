#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <tchar.h>

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "opengil/gil.hpp"
#include "opengil/semantic.hpp"
#include "opengil/version.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "imm32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_device_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
UINT g_resize_width = 0;
UINT g_resize_height = 0;
ID3D11RenderTargetView* g_main_render_target_view = nullptr;

struct AppState {
  std::optional<opengil::GilFile> file;
  std::filesystem::path path;
  std::string status = "Open a .gil file to inspect it.";
  std::string error;
  bool validation_ok = false;
  std::vector<std::string> validation_errors;
  std::vector<std::string> validation_warnings;
  std::vector<opengil::TabInfo> tabs;
  std::vector<opengil::PrefabInfo> prefabs;
  std::vector<opengil::SceneObjectInfo> scene_objects;
  std::vector<opengil::SceneObjectInfo> preview_objects;
  std::vector<opengil::NodeGraphInfo> nodegraphs;
  int selected_prefab = -1;
};

std::string path_to_string(const std::filesystem::path& path) {
  return path.string();
}

void create_render_target() {
  ID3D11Texture2D* back_buffer = nullptr;
  g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
  g_device->CreateRenderTargetView(back_buffer, nullptr, &g_main_render_target_view);
  back_buffer->Release();
}

void cleanup_render_target() {
  if (g_main_render_target_view) {
    g_main_render_target_view->Release();
    g_main_render_target_view = nullptr;
  }
}

bool create_device_d3d(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 2;
  desc.BufferDesc.Width = 0;
  desc.BufferDesc.Height = 0;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferDesc.RefreshRate.Numerator = 60;
  desc.BufferDesc.RefreshRate.Denominator = 1;
  desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = hwnd;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  constexpr std::array<D3D_FEATURE_LEVEL, 2> feature_levels{
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_0,
  };
  D3D_FEATURE_LEVEL feature_level{};
  const HRESULT result = D3D11CreateDeviceAndSwapChain(
      nullptr,
      D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      0,
      feature_levels.data(),
      static_cast<UINT>(feature_levels.size()),
      D3D11_SDK_VERSION,
      &desc,
      &g_swap_chain,
      &g_device,
      &feature_level,
      &g_device_context);
  if (result != S_OK) return false;

  create_render_target();
  return true;
}

void cleanup_device_d3d() {
  cleanup_render_target();
  if (g_swap_chain) {
    g_swap_chain->Release();
    g_swap_chain = nullptr;
  }
  if (g_device_context) {
    g_device_context->Release();
    g_device_context = nullptr;
  }
  if (g_device) {
    g_device->Release();
    g_device = nullptr;
  }
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) return true;

  switch (msg) {
    case WM_SIZE:
      if (wparam == SIZE_MINIMIZED) return 0;
      g_resize_width = static_cast<UINT>(LOWORD(lparam));
      g_resize_height = static_cast<UINT>(HIWORD(lparam));
      return 0;
    case WM_SYSCOMMAND:
      if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
      break;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProc(hwnd, msg, wparam, lparam);
}

std::optional<std::filesystem::path> open_gil_dialog(HWND owner) {
  std::array<wchar_t, MAX_PATH> file_name{};
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner;
  ofn.lpstrFile = file_name.data();
  ofn.nMaxFile = static_cast<DWORD>(file_name.size());
  ofn.lpstrFilter = L"GIL files (*.gil)\0*.gil\0All files (*.*)\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return std::nullopt;
  return std::filesystem::path(file_name.data());
}

void refresh_views(AppState& app) {
  app.validation_errors.clear();
  app.validation_warnings.clear();
  app.tabs.clear();
  app.prefabs.clear();
  app.scene_objects.clear();
  app.preview_objects.clear();
  app.nodegraphs.clear();
  app.selected_prefab = -1;

  if (!app.file) return;

  const auto validation = opengil::validate_gil(*app.file);
  app.validation_ok = validation.ok;
  app.validation_errors = validation.errors;
  app.validation_warnings = validation.warnings;

  app.tabs = opengil::list_tabs(*app.file);
  app.prefabs = opengil::list_prefabs(*app.file);
  app.scene_objects = opengil::list_scene_objects(*app.file);
  app.preview_objects = opengil::list_preview_objects(*app.file);
  app.nodegraphs = opengil::list_nodegraphs(*app.file);
}

void load_file(AppState& app, const std::filesystem::path& path) {
  app.error.clear();
  try {
    app.file = opengil::load_gil_file(path);
    app.path = path;
    refresh_views(app);
    app.status = "Loaded " + path_to_string(path);
  } catch (const std::exception& error) {
    app.file.reset();
    app.status = "Failed to load file.";
    app.error = error.what();
  }
}

void draw_top_bar(AppState& app, HWND hwnd) {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open .gil...")) {
        if (const auto path = open_gil_dialog(hwnd)) load_file(app, *path);
      }
      if (ImGui::MenuItem("Exit")) PostQuitMessage(0);
      ImGui::EndMenu();
    }
    ImGui::TextUnformatted("openGil GUI");
    ImGui::EndMainMenuBar();
  }
}

void draw_summary_panel(const AppState& app) {
  ImGui::Begin("Summary");
  ImGui::Text("openGil %s", OPENGIL_VERSION);
  ImGui::Separator();
  if (!app.file) {
    ImGui::TextUnformatted("No file loaded.");
  } else {
    ImGui::Text("Path: %s", path_to_string(app.path).c_str());
    ImGui::Text("Size: %zu bytes", app.file->bytes.size());
    ImGui::Text("Schema: %u", app.file->header.schema);
    ImGui::Text("File type: %u", app.file->header.file_type);
    ImGui::Text("Payload size: %zu bytes", opengil::payload(*app.file).size());
    ImGui::Text("Validation: %s", app.validation_ok ? "ok" : "failed");
  }
  ImGui::Separator();
  ImGui::TextWrapped("%s", app.status.c_str());
  if (!app.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.25f, 1.0f));
    ImGui::TextWrapped("%s", app.error.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::End();
}

void draw_validation_panel(const AppState& app) {
  ImGui::Begin("Validate");
  if (!app.file) {
    ImGui::TextUnformatted("Open a file first.");
  } else {
    ImGui::Text("Result: %s", app.validation_ok ? "ok" : "failed");
    if (ImGui::CollapsingHeader("Errors", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (app.validation_errors.empty()) ImGui::TextUnformatted("None");
      for (const auto& error : app.validation_errors) ImGui::BulletText("%s", error.c_str());
    }
    if (ImGui::CollapsingHeader("Warnings", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (app.validation_warnings.empty()) ImGui::TextUnformatted("None");
      for (const auto& warning : app.validation_warnings) ImGui::BulletText("%s", warning.c_str());
    }
  }
  ImGui::End();
}

void draw_prefabs_panel(AppState& app) {
  ImGui::Begin("Prefabs");
  if (!app.file) {
    ImGui::TextUnformatted("Open a file first.");
  } else if (ImGui::BeginTable("prefabs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Model Asset");
    ImGui::TableHeadersRow();
    for (int i = 0; i < static_cast<int>(app.prefabs.size()); ++i) {
      const auto& prefab = app.prefabs[static_cast<size_t>(i)];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool selected = app.selected_prefab == i;
      if (ImGui::Selectable(std::to_string(prefab.prefab_id).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        app.selected_prefab = i;
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(prefab.name.c_str());
      ImGui::TableSetColumnIndex(2);
      if (prefab.model_asset_id) {
        ImGui::Text("%llu", static_cast<unsigned long long>(*prefab.model_asset_id));
      } else {
        ImGui::TextUnformatted("-");
      }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void draw_tabs_panel(const AppState& app) {
  ImGui::Begin("Tabs");
  if (!app.file) {
    ImGui::TextUnformatted("Open a file first.");
  } else if (ImGui::BeginTable("tabs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Prefabs");
    ImGui::TableHeadersRow();
    for (const auto& tab : app.tabs) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (tab.id) ImGui::Text("%llu", static_cast<unsigned long long>(*tab.id));
      else ImGui::TextUnformatted("-");
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(tab.name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%zu", tab.prefab_ids.size());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void draw_objects_panel(const char* title, const std::vector<opengil::SceneObjectInfo>& objects) {
  ImGui::Begin(title);
  if (objects.empty()) {
    ImGui::TextUnformatted("No objects, or no file loaded.");
  } else if (ImGui::BeginTable(title, 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Ref");
    ImGui::TableSetupColumn("Asset");
    ImGui::TableSetupColumn("Prefab");
    ImGui::TableHeadersRow();
    for (const auto& object : objects) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%llu", static_cast<unsigned long long>(object.object_id));
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(object.name.c_str());
      ImGui::TableSetColumnIndex(2);
      if (object.ref_id) ImGui::Text("%llu", static_cast<unsigned long long>(*object.ref_id));
      else ImGui::TextUnformatted("-");
      ImGui::TableSetColumnIndex(3);
      if (object.asset_id) ImGui::Text("%llu", static_cast<unsigned long long>(*object.asset_id));
      else ImGui::TextUnformatted("-");
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(object.prefab_name.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void draw_nodegraphs_panel(const AppState& app) {
  ImGui::Begin("Nodegraphs");
  if (!app.file) {
    ImGui::TextUnformatted("Open a file first.");
  } else if (ImGui::BeginTable("nodegraphs", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("ID");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Role");
    ImGui::TableSetupColumn("Path");
    ImGui::TableSetupColumn("Nodes");
    ImGui::TableHeadersRow();
    for (const auto& graph : app.nodegraphs) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if (graph.id) ImGui::Text("%llu", static_cast<unsigned long long>(*graph.id));
      else ImGui::TextUnformatted("-");
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(graph.name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(graph.role.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(graph.path.c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%zu", graph.node_count);
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

void draw_pixel_art_panel() {
  ImGui::Begin("Pixel Art Import");
  ImGui::TextWrapped("Planned: PNG-only import that maps pixels to decoration blocks.");
  ImGui::Separator();
  ImGui::BulletText("Open PNG and read RGBA pixels.");
  ImGui::BulletText("Create or target an empty prefab.");
  ImGui::BulletText("Add one decoration block per visible pixel.");
  ImGui::BulletText("Color write path is still under research.");
  ImGui::End();
}

void draw_app(AppState& app, HWND hwnd) {
  draw_top_bar(app, hwnd);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 origin = viewport->WorkPos;
  const ImVec2 size = viewport->WorkSize;
  const float left_width = std::max(300.0f, size.x * 0.24f);
  const float right_width = std::max(330.0f, size.x * 0.26f);
  const float center_width = std::max(420.0f, size.x - left_width - right_width - 24.0f);
  const float gap = 8.0f;

  ImGui::SetNextWindowPos(ImVec2(origin.x + gap, origin.y + gap), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(left_width, size.y * 0.46f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(origin.x + gap, origin.y + size.y * 0.50f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(left_width, size.y * 0.46f), ImGuiCond_FirstUseEver);

  ImGui::SetNextWindowPos(ImVec2(origin.x + left_width + gap * 2.0f, origin.y + gap), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(center_width, size.y * 0.46f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(origin.x + left_width + gap * 2.0f, origin.y + size.y * 0.50f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(center_width, size.y * 0.22f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(origin.x + left_width + gap * 2.0f, origin.y + size.y * 0.74f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(center_width, size.y * 0.22f), ImGuiCond_FirstUseEver);

  ImGui::SetNextWindowPos(ImVec2(origin.x + left_width + center_width + gap * 3.0f, origin.y + gap), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(right_width, size.y * 0.46f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(origin.x + left_width + center_width + gap * 3.0f, origin.y + size.y * 0.50f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(right_width, size.y * 0.46f), ImGuiCond_FirstUseEver);

  draw_summary_panel(app);
  draw_validation_panel(app);
  draw_prefabs_panel(app);
  draw_tabs_panel(app);
  draw_objects_panel("Scene Objects", app.scene_objects);
  draw_objects_panel("Preview Objects", app.preview_objects);
  draw_nodegraphs_panel(app);
  draw_pixel_art_panel();
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  ImGui_ImplWin32_EnableDpiAwareness();

  WNDCLASSEXW wc{
      sizeof(wc),
      CS_CLASSDC,
      wnd_proc,
      0L,
      0L,
      instance,
      nullptr,
      nullptr,
      nullptr,
      nullptr,
      L"openGilGuiGraphicApi",
      nullptr,
  };
  RegisterClassExW(&wc);
  HWND hwnd = CreateWindowW(
      wc.lpszClassName,
      L"openGil GUI - ImGui / Win32 / DirectX11",
      WS_OVERLAPPEDWINDOW,
      100,
      100,
      1400,
      850,
      nullptr,
      nullptr,
      wc.hInstance,
      nullptr);

  if (!create_device_d3d(hwnd)) {
    cleanup_device_d3d();
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_device_context);

  AppState app;
  bool done = false;
  while (!done) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;

    if (g_resize_width != 0 && g_resize_height != 0) {
      cleanup_render_target();
      g_swap_chain->ResizeBuffers(0, g_resize_width, g_resize_height, DXGI_FORMAT_UNKNOWN, 0);
      g_resize_width = 0;
      g_resize_height = 0;
      create_render_target();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    draw_app(app, hwnd);

    ImGui::Render();
    constexpr float clear_color[4] = {0.08f, 0.09f, 0.10f, 1.00f};
    g_device_context->OMSetRenderTargets(1, &g_main_render_target_view, nullptr);
    g_device_context->ClearRenderTargetView(g_main_render_target_view, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap_chain->Present(1, 0);
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  cleanup_device_d3d();
  DestroyWindow(hwnd);
  UnregisterClassW(wc.lpszClassName, wc.hInstance);
  return 0;
}
