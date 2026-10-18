#pragma once
#include <nlohmann/json.hpp>
#include <string_view>
#include <filesystem>

class SaveManager {
private:
  nlohmann::json values_;
  std::filesystem::path file_ = "Content/Save.json";

public:
  SaveManager();
  ~SaveManager();

  void Set(std::string_view key, auto value) { values_[key] = value; }
  auto Get(std::string_view key, auto defatultValue = {}) const {
    return values_.value(key, defatultValue);
  }

  bool Has(std::string_view key) const;
  void Erase(std::string_view key);
  void Clear();
};
