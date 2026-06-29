#include "i18n.hpp"

namespace opengil::gui {
namespace {

struct Entry {
  std::string_view key;
  const char* value;
};

const char* lookup_zh_cn(std::string_view key) {
  static constexpr Entry entries[] = {
      {"app.title", "openGil 只读浏览器"},
      {"app.open_gil", "打开 GIL"},
      {"app.refresh", "刷新"},
      {"app.no_file", "未打开文件"},
      {"app.loaded", "已加载"},
      {"app.load_failed", "加载失败"},
      {"app.ready", "请选择一个 .gil 文件"},
      {"app.validation_ok", "校验通过"},
      {"app.validation_failed", "校验失败"},
      {"app.validation_not_run", "未校验"},
      {"app.current_file", "当前文件"},
      {"app.open_first", "先打开一个 .gil 文件"},
      {"app.font_missing", "中文字体未找到，已使用默认字体"},

      {"nav.prefabs", "元件"},
      {"nav.scene", "场景"},
      {"nav.preview", "元件预览空间"},
      {"nav.nodegraphs", "节点图"},

      {"filter.title", "筛选"},
      {"filter.search", "搜索"},
      {"filter.search_prefabs", "搜索名称、ID 或 Asset"},
      {"filter.search_objects", "搜索名称、ID、元件或 Asset"},
      {"filter.search_nodegraphs", "搜索名称、ID、Role 或 Path"},
      {"filter.tabs", "Tab"},
      {"filter.all_tabs", "全部 Tab"},
      {"filter.role", "Role"},
      {"filter.all_roles", "全部 Role"},

      {"common.id", "ID"},
      {"common.name", "名称"},
      {"common.none", "无"},
      {"common.unnamed", "未命名"},
      {"common.not_available", "-"},
      {"common.count", "数量"},
      {"common.details", "详情"},
      {"common.no_selection", "未选择项目"},
      {"common.no_data", "没有可展示的数据"},
      {"common.index", "索引"},

      {"prefabs.title", "元件"},
      {"prefabs.model_asset", "模型 Asset"},
      {"prefabs.tab_count", "Tab 数"},
      {"prefabs.tabs", "所属 Tab"},
      {"prefabs.scene_refs", "场景引用"},
      {"prefabs.preview_refs", "元件预览空间引用"},

      {"objects.scene_title", "场景对象"},
      {"objects.preview_title", "元件预览空间对象"},
      {"objects.ref_id", "Ref ID"},
      {"objects.asset_id", "Asset ID"},
      {"objects.prefab", "元件"},
      {"objects.prefab_model_asset", "元件模型 Asset"},
      {"objects.transform", "Transform"},
      {"objects.position", "位置"},
      {"objects.rotation", "旋转"},
      {"objects.scale", "缩放"},

      {"nodegraphs.title", "节点图"},
      {"nodegraphs.role", "Role"},
      {"nodegraphs.path", "Path"},
      {"nodegraphs.nodes", "节点"},
      {"nodegraphs.composite_pins", "复合 Pin"},
      {"nodegraphs.comments", "注释"},
      {"nodegraphs.graph_values", "图变量"},
      {"nodegraphs.affiliations", "归属"},
      {"nodegraphs.role_definition", "定义"},
      {"nodegraphs.role_reference", "引用"},
  };

  for (const auto& entry : entries) {
    if (entry.key == key) return entry.value;
  }
  return nullptr;
}

const char* lookup_en_us(std::string_view) {
  return nullptr;
}

}  // namespace

I18n::I18n(Language language) : language_(language) {}

const char* I18n::t(std::string_view key) const {
  if (language_ == Language::EnUS) {
    if (const char* value = lookup_en_us(key)) return value;
  }
  if (const char* value = lookup_zh_cn(key)) return value;
  return "<?>";
}

void I18n::set_language(Language language) {
  language_ = language;
}

Language I18n::language() const {
  return language_;
}

}  // namespace opengil::gui
