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
#include <cctype>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "i18n.hpp"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "opengil/gil.hpp"
#include "opengil/semantic.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "imm32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

using opengil::gui::I18n;

constexpr const char* kChineseFontRelativePath = "assets/AlibabaPuHuiTi-3-55-Regular.ttf";

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_device_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
UINT g_resize_width = 0;
UINT g_resize_height = 0;
ID3D11RenderTargetView* g_main_render_target_view = nullptr;

enum class Workspace {
  Prefabs,
  Scene,
  Preview,
  NodeGraphs,
};

struct AppState {
  I18n i18n;
  std::optional<opengil::GilFile> file;
  std::filesystem::path path;
  std::string status = i18n.t("app.ready");
  std::string error;
  std::string font_warning;
  bool validation_ran = false;
  bool validation_ok = false;
  std::vector<std::string> validation_errors;
  std::vector<std::string> validation_warnings;
  std::vector<opengil::TabInfo> tabs;
  std::vector<opengil::PrefabInfo> prefabs;
  std::vector<opengil::SceneObjectInfo> scene_objects;
  std::vector<opengil::SceneObjectInfo> preview_objects;
  std::vector<opengil::NodeGraphInfo> nodegraphs;
  Workspace workspace = Workspace::Prefabs;
  std::array<char, 128> prefab_search{};
  std::array<char, 128> scene_search{};
  std::array<char, 128> preview_search{};
  std::array<char, 128> nodegraph_search{};
  int selected_tab = -1;
  int selected_role = -1;
  int selected_prefab = -1;
  int selected_scene_object = -1;
  int selected_preview_object = -1;
  int selected_nodegraph = -1;
};

std::string path_to_string(const std::filesystem::path& path) {
  return path.string();
}

std::string u64_to_string(uint64_t value) {
  return std::to_string(value);
}

std::string optional_u64_to_string(const std::optional<uint64_t>& value) {
  if (!value) return "-";
  return std::to_string(*value);
}

std::string optional_float_to_string(const std::optional<float>& value) {
  if (!value) return "-";
  char buffer[32]{};
  std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(*value));
  return buffer;
}

std::string lower_ascii(std::string_view input) {
  std::string out;
  out.reserve(input.size());
  for (const unsigned char ch : input) out.push_back(static_cast<char>(std::tolower(ch)));
  return out;
}

bool contains_case_insensitive(std::string_view text, std::string_view needle) {
  if (needle.empty()) return true;
  return lower_ascii(text).find(lower_ascii(needle)) != std::string::npos;
}

bool contains_any(std::string_view needle, const std::vector<std::string>& values) {
  if (needle.empty()) return true;
  for (const auto& value : values) {
    if (contains_case_insensitive(value, needle)) return true;
  }
  return false;
}

const char* display_text(const std::string& value, const I18n& i18n) {
  return value.empty() ? i18n.t("common.unnamed") : value.c_str();
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
  ofn.lpstrFilter = L"GIL 文件 (*.gil)\0*.gil\0所有文件 (*.*)\0*.*\0";
  ofn.nFilterIndex = 1;
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return std::nullopt;
  return std::filesystem::path(file_name.data());
}

std::filesystem::path executable_directory() {
  std::array<wchar_t, MAX_PATH> buffer{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length == buffer.size()) return std::filesystem::current_path();
  return std::filesystem::path(buffer.data()).parent_path();
}

std::optional<std::filesystem::path> find_asset_path(const std::filesystem::path& relative_path) {
  std::vector<std::filesystem::path> roots{
      executable_directory(),
      std::filesystem::current_path(),
      std::filesystem::current_path().parent_path(),
      std::filesystem::current_path().parent_path().parent_path(),
  };

  for (const auto& root : roots) {
    const auto candidate = root / relative_path;
    std::error_code error;
    if (std::filesystem::exists(candidate, error)) return candidate;
  }
  return std::nullopt;
}

bool load_chinese_font(AppState& app) {
  const auto font_path = find_asset_path(kChineseFontRelativePath);
  if (!font_path) {
    app.font_warning = app.i18n.t("app.font_missing");
    return false;
  }

  ImGuiIO& io = ImGui::GetIO();
  ImFontConfig config;
  config.OversampleH = 2;
  config.OversampleV = 2;
  if (!io.Fonts->AddFontFromFileTTF(path_to_string(*font_path).c_str(), 18.0f, &config, io.Fonts->GetGlyphRangesChineseFull())) {
    app.font_warning = app.i18n.t("app.font_missing");
    return false;
  }
  return true;
}

void reset_selection(AppState& app) {
  app.selected_prefab = -1;
  app.selected_scene_object = -1;
  app.selected_preview_object = -1;
  app.selected_nodegraph = -1;
}

void refresh_views(AppState& app) {
  app.validation_errors.clear();
  app.validation_warnings.clear();
  app.tabs.clear();
  app.prefabs.clear();
  app.scene_objects.clear();
  app.preview_objects.clear();
  app.nodegraphs.clear();
  reset_selection(app);

  if (!app.file) {
    app.validation_ran = false;
    app.validation_ok = false;
    return;
  }

  const auto validation = opengil::validate_gil(*app.file);
  app.validation_ran = true;
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
    app.status = std::string(app.i18n.t("app.loaded")) + ": " + path.filename().string();
  } catch (const std::exception& error) {
    app.file.reset();
    app.path.clear();
    refresh_views(app);
    app.status = app.i18n.t("app.load_failed");
    app.error = error.what();
  }
}

void refresh_current_file(AppState& app) {
  if (app.path.empty()) return;
  load_file(app, app.path);
}

bool prefab_in_selected_tab(const AppState& app, const opengil::PrefabInfo& prefab) {
  if (app.selected_tab < 0 || app.selected_tab >= static_cast<int>(app.tabs.size())) return true;
  const auto& ids = app.tabs[static_cast<size_t>(app.selected_tab)].prefab_ids;
  return std::find(ids.begin(), ids.end(), prefab.prefab_id) != ids.end();
}

int prefab_tab_count(const AppState& app, uint64_t prefab_id) {
  int count = 0;
  for (const auto& tab : app.tabs) {
    if (std::find(tab.prefab_ids.begin(), tab.prefab_ids.end(), prefab_id) != tab.prefab_ids.end()) ++count;
  }
  return count;
}

std::vector<const opengil::TabInfo*> prefab_tabs(const AppState& app, uint64_t prefab_id) {
  std::vector<const opengil::TabInfo*> out;
  for (const auto& tab : app.tabs) {
    if (std::find(tab.prefab_ids.begin(), tab.prefab_ids.end(), prefab_id) != tab.prefab_ids.end()) out.push_back(&tab);
  }
  return out;
}

int count_object_refs(const std::vector<opengil::SceneObjectInfo>& objects, uint64_t prefab_id) {
  int count = 0;
  for (const auto& object : objects) {
    if (object.ref_id == prefab_id) ++count;
  }
  return count;
}

bool prefab_matches_filter(const AppState& app, const opengil::PrefabInfo& prefab) {
  if (!prefab_in_selected_tab(app, prefab)) return false;
  return contains_any(app.prefab_search.data(), {
      prefab.name,
      u64_to_string(prefab.prefab_id),
      optional_u64_to_string(prefab.model_asset_id),
  });
}

bool object_matches_filter(std::string_view search, const opengil::SceneObjectInfo& object) {
  return contains_any(search, {
      object.name,
      u64_to_string(object.object_id),
      optional_u64_to_string(object.ref_id),
      optional_u64_to_string(object.asset_id),
      object.prefab_name,
      optional_u64_to_string(object.prefab_model_asset_id),
  });
}

bool nodegraph_matches_role(const AppState& app, const opengil::NodeGraphInfo& graph) {
  if (app.selected_role < 0) return true;
  std::set<std::string> roles;
  for (const auto& item : app.nodegraphs) roles.insert(item.role);
  if (app.selected_role >= static_cast<int>(roles.size())) return true;
  auto it = roles.begin();
  std::advance(it, app.selected_role);
  return graph.role == *it;
}

bool nodegraph_matches_filter(const AppState& app, const opengil::NodeGraphInfo& graph) {
  if (!nodegraph_matches_role(app, graph)) return false;
  return contains_any(app.nodegraph_search.data(), {
      graph.name,
      optional_u64_to_string(graph.id),
      graph.role,
      graph.path,
      std::to_string(graph.node_count),
  });
}

void text_value(const I18n& i18n, const char* label_key, const std::string& value) {
  ImGui::TextUnformatted(i18n.t(label_key));
  ImGui::SameLine(150.0f);
  ImGui::TextWrapped("%s", value.c_str());
}

void text_value(const I18n& i18n, const char* label_key, const char* value) {
  text_value(i18n, label_key, std::string(value));
}

void draw_no_file(const AppState& app) {
  ImGui::TextUnformatted(app.i18n.t("app.open_first"));
}

void draw_top_bar(AppState& app, HWND hwnd) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 46.0f));
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
  ImGui::Begin("top_bar", nullptr, flags);

  if (ImGui::Button(app.i18n.t("app.open_gil"), ImVec2(96.0f, 30.0f))) {
    if (const auto path = open_gil_dialog(hwnd)) load_file(app, *path);
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!app.file.has_value());
  if (ImGui::Button(app.i18n.t("app.refresh"), ImVec2(72.0f, 30.0f))) refresh_current_file(app);
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::TextUnformatted(app.i18n.t("app.title"));
  ImGui::SameLine();
  ImGui::TextDisabled("| %s", app.status.c_str());

  if (app.validation_ran) {
    ImGui::SameLine();
    const ImVec4 color = app.validation_ok ? ImVec4(0.40f, 0.86f, 0.52f, 1.0f) : ImVec4(1.0f, 0.38f, 0.28f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(app.validation_ok ? app.i18n.t("app.validation_ok") : app.i18n.t("app.validation_failed"));
    ImGui::PopStyleColor();
  }

  if (!app.error.empty()) {
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.38f, 0.28f, 1.0f));
    ImGui::TextWrapped("%s", app.error.c_str());
    ImGui::PopStyleColor();
  } else if (!app.font_warning.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", app.font_warning.c_str());
  }

  ImGui::End();
}

void draw_nav_button(AppState& app, Workspace workspace, const char* label_key) {
  const bool selected = app.workspace == workspace;
  if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
  if (ImGui::Button(app.i18n.t(label_key), ImVec2(-1.0f, 38.0f))) app.workspace = workspace;
  if (selected) ImGui::PopStyleColor();
}

void draw_sidebar(AppState& app, const ImVec2& pos, const ImVec2& size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("sidebar", nullptr, flags);
  draw_nav_button(app, Workspace::Prefabs, "nav.prefabs");
  draw_nav_button(app, Workspace::Scene, "nav.scene");
  draw_nav_button(app, Workspace::Preview, "nav.preview");
  draw_nav_button(app, Workspace::NodeGraphs, "nav.nodegraphs");
  ImGui::End();
}

void draw_prefab_filters(AppState& app) {
  ImGui::TextUnformatted(app.i18n.t("filter.title"));
  ImGui::Separator();
  ImGui::TextUnformatted(app.i18n.t("filter.search"));
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##prefab_search", app.i18n.t("filter.search_prefabs"), app.prefab_search.data(), app.prefab_search.size());
  ImGui::Spacing();
  ImGui::TextUnformatted(app.i18n.t("filter.tabs"));
  if (ImGui::Selectable(app.i18n.t("filter.all_tabs"), app.selected_tab == -1)) app.selected_tab = -1;
  for (int i = 0; i < static_cast<int>(app.tabs.size()); ++i) {
    const auto& tab = app.tabs[static_cast<size_t>(i)];
    const std::string label = (tab.name.empty() ? app.i18n.t("common.unnamed") : tab.name) + " (" + std::to_string(tab.prefab_ids.size()) + ")";
    if (ImGui::Selectable(label.c_str(), app.selected_tab == i)) app.selected_tab = i;
  }
}

void draw_object_filters(AppState& app, std::array<char, 128>& search) {
  ImGui::TextUnformatted(app.i18n.t("filter.title"));
  ImGui::Separator();
  ImGui::TextUnformatted(app.i18n.t("filter.search"));
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##object_search", app.i18n.t("filter.search_objects"), search.data(), search.size());
}

void draw_nodegraph_filters(AppState& app) {
  ImGui::TextUnformatted(app.i18n.t("filter.title"));
  ImGui::Separator();
  ImGui::TextUnformatted(app.i18n.t("filter.search"));
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##nodegraph_search", app.i18n.t("filter.search_nodegraphs"), app.nodegraph_search.data(), app.nodegraph_search.size());
  ImGui::Spacing();
  ImGui::TextUnformatted(app.i18n.t("filter.role"));
  if (ImGui::Selectable(app.i18n.t("filter.all_roles"), app.selected_role == -1)) app.selected_role = -1;
  std::set<std::string> roles;
  for (const auto& item : app.nodegraphs) roles.insert(item.role);
  int index = 0;
  for (const auto& role : roles) {
    const char* display = role == "definition" ? app.i18n.t("nodegraphs.role_definition") :
                          role == "reference"  ? app.i18n.t("nodegraphs.role_reference") :
                                                 role.c_str();
    if (ImGui::Selectable(display, app.selected_role == index)) app.selected_role = index;
    ++index;
  }
}

void draw_filters(AppState& app, const ImVec2& pos, const ImVec2& size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("filters", nullptr, flags);
  if (!app.file) {
    draw_no_file(app);
  } else if (app.workspace == Workspace::Prefabs) {
    draw_prefab_filters(app);
  } else if (app.workspace == Workspace::NodeGraphs) {
    draw_nodegraph_filters(app);
  } else if (app.workspace == Workspace::Scene) {
    draw_object_filters(app, app.scene_search);
  } else {
    draw_object_filters(app, app.preview_search);
  }
  ImGui::End();
}

void setup_common_table_flags(ImGuiTableFlags& flags) {
  flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
          ImGuiTableFlags_SizingStretchProp;
}

void draw_prefab_table(AppState& app) {
  ImGuiTableFlags flags;
  setup_common_table_flags(flags);
  if (!ImGui::BeginTable("prefabs", 4, flags)) return;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(app.i18n.t("common.id"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn(app.i18n.t("common.name"));
  ImGui::TableSetupColumn(app.i18n.t("prefabs.model_asset"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
  ImGui::TableSetupColumn(app.i18n.t("prefabs.tab_count"), ImGuiTableColumnFlags_WidthFixed, 86.0f);
  ImGui::TableHeadersRow();

  for (int i = 0; i < static_cast<int>(app.prefabs.size()); ++i) {
    const auto& prefab = app.prefabs[static_cast<size_t>(i)];
    if (!prefab_matches_filter(app, prefab)) continue;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool selected = app.selected_prefab == i;
    const auto id = u64_to_string(prefab.prefab_id);
    if (ImGui::Selectable(id.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) app.selected_prefab = i;
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(display_text(prefab.name, app.i18n));
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(optional_u64_to_string(prefab.model_asset_id).c_str());
    ImGui::TableSetColumnIndex(3);
    ImGui::Text("%d", prefab_tab_count(app, prefab.prefab_id));
  }
  ImGui::EndTable();
}

void draw_object_table(
    AppState& app,
    const std::vector<opengil::SceneObjectInfo>& objects,
    int& selected_index,
    const char* table_id,
    std::string_view search) {
  ImGuiTableFlags flags;
  setup_common_table_flags(flags);
  if (!ImGui::BeginTable(table_id, 5, flags)) return;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(app.i18n.t("common.id"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn(app.i18n.t("common.name"));
  ImGui::TableSetupColumn(app.i18n.t("objects.ref_id"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn(app.i18n.t("objects.asset_id"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn(app.i18n.t("objects.prefab"));
  ImGui::TableHeadersRow();

  for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
    const auto& object = objects[static_cast<size_t>(i)];
    if (!object_matches_filter(search, object)) continue;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool selected = selected_index == i;
    const auto id = u64_to_string(object.object_id);
    if (ImGui::Selectable(id.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) selected_index = i;
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(display_text(object.name, app.i18n));
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(optional_u64_to_string(object.ref_id).c_str());
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(optional_u64_to_string(object.asset_id).c_str());
    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(display_text(object.prefab_name, app.i18n));
  }
  ImGui::EndTable();
}

void draw_nodegraph_table(AppState& app) {
  ImGuiTableFlags flags;
  setup_common_table_flags(flags);
  if (!ImGui::BeginTable("nodegraphs", 5, flags)) return;

  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn(app.i18n.t("common.id"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
  ImGui::TableSetupColumn(app.i18n.t("common.name"));
  ImGui::TableSetupColumn(app.i18n.t("nodegraphs.role"), ImGuiTableColumnFlags_WidthFixed, 92.0f);
  ImGui::TableSetupColumn(app.i18n.t("nodegraphs.path"), ImGuiTableColumnFlags_WidthFixed, 180.0f);
  ImGui::TableSetupColumn(app.i18n.t("nodegraphs.nodes"), ImGuiTableColumnFlags_WidthFixed, 72.0f);
  ImGui::TableHeadersRow();

  for (int i = 0; i < static_cast<int>(app.nodegraphs.size()); ++i) {
    const auto& graph = app.nodegraphs[static_cast<size_t>(i)];
    if (!nodegraph_matches_filter(app, graph)) continue;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    const bool selected = app.selected_nodegraph == i;
    const auto id = optional_u64_to_string(graph.id);
    if (ImGui::Selectable(id.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) app.selected_nodegraph = i;
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(display_text(graph.name, app.i18n));
    ImGui::TableSetColumnIndex(2);
    const char* role = graph.role == "definition" ? app.i18n.t("nodegraphs.role_definition") :
                       graph.role == "reference"  ? app.i18n.t("nodegraphs.role_reference") :
                                                    graph.role.c_str();
    ImGui::TextUnformatted(role);
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(graph.path.c_str());
    ImGui::TableSetColumnIndex(4);
    ImGui::Text("%zu", graph.node_count);
  }
  ImGui::EndTable();
}

void draw_main_table(AppState& app, const ImVec2& pos, const ImVec2& size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("main_table", nullptr, flags);

  if (!app.file) {
    draw_no_file(app);
  } else if (app.workspace == Workspace::Prefabs) {
    draw_prefab_table(app);
  } else if (app.workspace == Workspace::Scene) {
    draw_object_table(app, app.scene_objects, app.selected_scene_object, "scene_objects", app.scene_search.data());
  } else if (app.workspace == Workspace::Preview) {
    draw_object_table(app, app.preview_objects, app.selected_preview_object, "preview_objects", app.preview_search.data());
  } else {
    draw_nodegraph_table(app);
  }

  ImGui::End();
}

void draw_prefab_details(AppState& app) {
  if (app.selected_prefab < 0 || app.selected_prefab >= static_cast<int>(app.prefabs.size())) {
    ImGui::TextUnformatted(app.i18n.t("common.no_selection"));
    return;
  }

  const auto& prefab = app.prefabs[static_cast<size_t>(app.selected_prefab)];
  text_value(app.i18n, "common.id", u64_to_string(prefab.prefab_id));
  text_value(app.i18n, "common.name", prefab.name.empty() ? app.i18n.t("common.unnamed") : prefab.name);
  text_value(app.i18n, "prefabs.model_asset", optional_u64_to_string(prefab.model_asset_id));
  text_value(app.i18n, "prefabs.scene_refs", std::to_string(count_object_refs(app.scene_objects, prefab.prefab_id)));
  text_value(app.i18n, "prefabs.preview_refs", std::to_string(count_object_refs(app.preview_objects, prefab.prefab_id)));

  ImGui::Spacing();
  ImGui::SeparatorText(app.i18n.t("prefabs.tabs"));
  const auto tabs = prefab_tabs(app, prefab.prefab_id);
  if (tabs.empty()) {
    ImGui::TextUnformatted(app.i18n.t("common.none"));
  } else {
    for (const auto* tab : tabs) {
      ImGui::BulletText("%s", display_text(tab->name, app.i18n));
    }
  }
}

void draw_vec3(const I18n& i18n, const char* label_key, const std::optional<float>& x, const std::optional<float>& y, const std::optional<float>& z) {
  const std::string value = optional_float_to_string(x) + ", " + optional_float_to_string(y) + ", " + optional_float_to_string(z);
  text_value(i18n, label_key, value);
}

void draw_object_details(AppState& app, const std::vector<opengil::SceneObjectInfo>& objects, int selected_index) {
  if (selected_index < 0 || selected_index >= static_cast<int>(objects.size())) {
    ImGui::TextUnformatted(app.i18n.t("common.no_selection"));
    return;
  }

  const auto& object = objects[static_cast<size_t>(selected_index)];
  text_value(app.i18n, "common.index", std::to_string(object.index));
  text_value(app.i18n, "common.id", u64_to_string(object.object_id));
  text_value(app.i18n, "common.name", object.name.empty() ? app.i18n.t("common.unnamed") : object.name);
  text_value(app.i18n, "objects.ref_id", optional_u64_to_string(object.ref_id));
  text_value(app.i18n, "objects.asset_id", optional_u64_to_string(object.asset_id));
  text_value(app.i18n, "objects.prefab", object.prefab_name.empty() ? app.i18n.t("common.not_available") : object.prefab_name);
  text_value(app.i18n, "objects.prefab_model_asset", optional_u64_to_string(object.prefab_model_asset_id));

  ImGui::Spacing();
  ImGui::SeparatorText(app.i18n.t("objects.transform"));
  draw_vec3(app.i18n, "objects.position", object.transform.position_x, object.transform.position_y, object.transform.position_z);
  draw_vec3(app.i18n, "objects.rotation", object.transform.rotation_x, object.transform.rotation_y, object.transform.rotation_z);
  draw_vec3(app.i18n, "objects.scale", object.transform.scale_x, object.transform.scale_y, object.transform.scale_z);
}

void draw_nodegraph_details(AppState& app) {
  if (app.selected_nodegraph < 0 || app.selected_nodegraph >= static_cast<int>(app.nodegraphs.size())) {
    ImGui::TextUnformatted(app.i18n.t("common.no_selection"));
    return;
  }

  const auto& graph = app.nodegraphs[static_cast<size_t>(app.selected_nodegraph)];
  const char* role = graph.role == "definition" ? app.i18n.t("nodegraphs.role_definition") :
                     graph.role == "reference"  ? app.i18n.t("nodegraphs.role_reference") :
                                                  graph.role.c_str();
  text_value(app.i18n, "common.id", optional_u64_to_string(graph.id));
  text_value(app.i18n, "common.name", graph.name.empty() ? app.i18n.t("common.unnamed") : graph.name);
  text_value(app.i18n, "nodegraphs.role", role);
  text_value(app.i18n, "nodegraphs.path", graph.path);
  text_value(app.i18n, "nodegraphs.nodes", std::to_string(graph.node_count));
  text_value(app.i18n, "nodegraphs.composite_pins", std::to_string(graph.composite_pin_count));
  text_value(app.i18n, "nodegraphs.comments", std::to_string(graph.comment_count));
  text_value(app.i18n, "nodegraphs.graph_values", std::to_string(graph.graph_value_count));
  text_value(app.i18n, "nodegraphs.affiliations", std::to_string(graph.affiliation_count));
}

void draw_details(AppState& app, const ImVec2& pos, const ImVec2& size) {
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(size);
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
  ImGui::Begin("details", nullptr, flags);
  ImGui::TextUnformatted(app.i18n.t("common.details"));
  ImGui::Separator();

  if (!app.file) {
    draw_no_file(app);
  } else if (app.workspace == Workspace::Prefabs) {
    draw_prefab_details(app);
  } else if (app.workspace == Workspace::Scene) {
    draw_object_details(app, app.scene_objects, app.selected_scene_object);
  } else if (app.workspace == Workspace::Preview) {
    draw_object_details(app, app.preview_objects, app.selected_preview_object);
  } else {
    draw_nodegraph_details(app);
  }

  ImGui::End();
}

void draw_app(AppState& app, HWND hwnd) {
  draw_top_bar(app, hwnd);

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 origin = viewport->WorkPos;
  const ImVec2 size = viewport->WorkSize;
  constexpr float top_height = 46.0f;
  constexpr float gap = 8.0f;
  const float content_y = origin.y + top_height + gap;
  const float content_height = std::max(100.0f, size.y - top_height - gap);
  const float sidebar_width = 150.0f;
  const float filter_width = std::max(220.0f, size.x * 0.18f);
  const float details_width = std::max(300.0f, size.x * 0.25f);
  const float main_width = std::max(320.0f, size.x - sidebar_width - filter_width - details_width - gap * 5.0f);

  ImVec2 nav_pos(origin.x + gap, content_y);
  ImVec2 filter_pos(nav_pos.x + sidebar_width + gap, content_y);
  ImVec2 table_pos(filter_pos.x + filter_width + gap, content_y);
  ImVec2 details_pos(table_pos.x + main_width + gap, content_y);
  ImVec2 panel_size_y(0.0f, content_height - gap);

  draw_sidebar(app, nav_pos, ImVec2(sidebar_width, panel_size_y.y));
  draw_filters(app, filter_pos, ImVec2(filter_width, panel_size_y.y));
  draw_main_table(app, table_pos, ImVec2(main_width, panel_size_y.y));
  draw_details(app, details_pos, ImVec2(details_width, panel_size_y.y));
}

void apply_style() {
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.GrabRounding = 4.0f;
  style.ScrollbarRounding = 4.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(8.0f, 7.0f);
  style.WindowPadding = ImVec2(12.0f, 12.0f);
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
      L"openGil 只读浏览器",
      WS_OVERLAPPEDWINDOW,
      100,
      100,
      1440,
      860,
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

  AppState app;
  load_chinese_font(app);
  apply_style();
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_device, g_device_context);

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
