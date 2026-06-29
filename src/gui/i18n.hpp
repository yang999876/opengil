#pragma once

#include <string_view>

namespace opengil::gui {

enum class Language {
  ZhCN,
  EnUS,
};

class I18n {
 public:
  explicit I18n(Language language = Language::ZhCN);

  const char* t(std::string_view key) const;
  void set_language(Language language);
  Language language() const;

 private:
  Language language_;
};

}  // namespace opengil::gui
