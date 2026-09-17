// PssConfig.cpp: see PssConfig.h.
#include "stdafx.h"
#include "PssConfig.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <vector>

namespace {

/**
 * @brief 自動喝水組態專用 Log 紀錄函式。
 * @param fmt 格式化字串
 * @param ... 可變參數列表
 */
static void ApCfgLog(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char body[512] = {0};
  vsprintf_s(body, fmt, args);
  va_end(args);
  launcherdll_hook_log("%s", body);
}
// PacketHandler：75→C_PlaySupport、128→C_SecurityStatus。數值對齊 OpcodesClient，不可改。
constexpr BYTE kOpcodePlaySupport = 75;  // 恢復（喝水／解析／面板）
constexpr BYTE kOpcodeItemFilter = 128;  // 借位名稱沿用；實際含名單＋其他頁 flags
constexpr BYTE kMagicUiVisible = 0x56;   // 僅 75：面板開／關
constexpr BYTE kMagicItemFilterSync = 0x58;    // 僅 128：名單第一段（覆寫）
constexpr BYTE kMagicItemFilterRequest = 0x59; // 僅 128：請 PacketBox 47 回推
constexpr BYTE kMagicMiscFlags = 0x5A;         // 僅 128：其他頁打勾
constexpr BYTE kMagicItemFilterAppend = 0x5B;  // 僅 128：名單續段（追加，勿清）
constexpr BYTE kMagicCraftFlags = 0x5C;        // 僅 128：提煉黑魔石四勾
constexpr BYTE kMagicFixedBuff = 0x5D;         // 僅 128：BUFF-固定九格 itemId
constexpr BYTE kMagicCustomBuff = 0x5E;        // 僅 128：BUFF-自訂九格 kind+id
constexpr BYTE kMagicEnabledFlag = 0x5F;       // 僅 128：啟動／停止旗標（取代 opcode 75 62-byte 包裡的 enabled）
constexpr BYTE kMagicHealSlots = 0x60;         // 僅 128：治療五槽（同上，取代 opcode 75 62-byte 包）
constexpr BYTE kMagicManaSlots = 0x61;         // 僅 128：補魔五槽（同上）
constexpr int kItemFilterChunk = 20;           // 每包最多 20 個 d；40 個分兩包，避免 164-byte 打亂加密

typedef void(__cdecl *SendPacketDataFn)(const char *format, ...);
const SendPacketDataFn SendPacketData = (SendPacketDataFn)0x580E50;

/**
 * @brief 將寬字元 (wchar_t) 字串轉換為 UTF-8 編碼的 std::string。
 * @param w 寬字元字串指標
 * @return UTF-8 編碼字串
 */
std::string WideToUtf8(const wchar_t *w) {
  if (!w || !w[0]) {
    return std::string();
  }
  int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  if (len <= 1) {
    return std::string();
  }
  std::string out(len - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], len, NULL, NULL);
  return out;
}

/**
 * @brief 將 UTF-8 編碼的 std::string 轉換為寬字元 (wchar_t) 緩衝區。
 * @param s UTF-8 字串
 * @param out 輸出之寬字元緩衝區指標
 * @param outCount 緩衝區容量
 */
void Utf8ToWide(const std::string &s, wchar_t *out, size_t outCount) {
  out[0] = 0;
  if (s.empty()) {
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out, (int)outCount);
}

/**
 * @brief 取得自動喝水設定檔 (auto_potion.cfg) 之完整檔案路徑。
 * @return 設定檔完整路徑
 */
std::string ConfigFilePath() {
  char exePath[MAX_PATH] = {0};
  if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
    for (int i = (int)strlen(exePath) - 1; i >= 0; i--) {
      if (exePath[i] == '\\' || exePath[i] == '/') {
        exePath[i] = '\0';
        break;
      }
    }
  }
  return std::string(exePath) + "\\Core\\auto_potion.cfg";
}

/**
 * @brief 校正並限制 PssSlot 的數值範圍。
 * @param s 欲限制的喝水/技能欄位參考
 */
void ClampPssSlot(PssSlot &s) {
  if (s.kind < PssSlot_None || s.kind > PssSlot_Skill) {
    s.kind = PssSlot_None;
  }
  if (s.kind == PssSlot_None) {
    s.id = 0;
    s.name[0] = 0;
    s.count = 0;
  }
  if (s.id < 0) {
    s.id = 0;
  }
  if (s.thresholdPercent < 0) {
    s.thresholdPercent = 0;
  }
  if (s.thresholdPercent > 100) {
    s.thresholdPercent = 100;
  }
  if (s.thresholdPercent2 < 0) {
    s.thresholdPercent2 = 0;
  }
  if (s.thresholdPercent2 > 100) {
    s.thresholdPercent2 = 100;
  }
}

/**
 * @brief 校正並限制 PssSection 的數值範圍。
 * @param sec 欲限制的區塊參考（包含多個 Slots）
 */
void ClampPssSection(PssSection &sec) {
  for (int i = 0; i < kPssSlotsPerSection; i++) {
    ClampPssSlot(sec.slots[i]);
  }
}

/**
 * 刪除／溶解名單：去掉 itemId<=0、重複，count 夾在 0~kItemFilterMax，尾端清成空 entry。
 */
void ClampItemFilterList(ItemFilterList &list) {
  if (list.count < 0) {
    list.count = 0;
  }
  if (list.count > kItemFilterMax) {
    list.count = kItemFilterMax;
  }
  int w = 0;
  for (int i = 0; i < list.count; i++) {
    if (list.items[i].itemId <= 0) {
      continue;
    }
    bool dup = false;
    for (int j = 0; j < w; j++) {
      if (list.items[j].itemId == list.items[i].itemId) {
        dup = true;
        break;
      }
    }
    if (dup) {
      continue;
    }
    if (w != i) {
      list.items[w] = list.items[i];
    }
    w++;
  }
  list.count = w;
  for (int i = w; i < kItemFilterMax; i++) {
    list.items[i] = ItemFilterEntry();
  }
}

/** 用 sep 切開；連續分隔會產生空字串。 */
std::vector<std::string> SplitCsv(const std::string &s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == sep) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(s[i]);
    }
  }
  out.push_back(cur);
  return out;
}

/**
 * 從 cfg 三行還原名單：ids 逗號、gfx 逗號、names 用 |（名稱可含逗號）。
 * 對齊 Save 的 JoinFilter*。
 */
void ParseItemFilterList(ItemFilterList &list, const std::string &ids,
                         const std::string &gfx, const std::string &names) {
  std::vector<std::string> idv = SplitCsv(ids, ',');
  std::vector<std::string> gxv = SplitCsv(gfx, ',');
  std::vector<std::string> nv = SplitCsv(names, '|');
  list.count = 0;
  size_t n = idv.size();
  if (n > (size_t)kItemFilterMax) {
    n = kItemFilterMax;
  }
  for (size_t i = 0; i < n; i++) {
    int id = atoi(idv[i].c_str());
    if (id <= 0) {
      continue;
    }
    ItemFilterEntry &e = list.items[list.count];
    e.itemId = id;
    e.gfxid = (i < gxv.size()) ? atoi(gxv[i].c_str()) : 0;
    e.name[0] = 0;
    if (i < nv.size()) {
      Utf8ToWide(nv[i], e.name, _countof(e.name));
    }
    list.count++;
    if (list.count >= kItemFilterMax) {
      break;
    }
  }
}

/** 寫 delete_ids／dissolve_ids：逗號分隔 itemId。 */
std::string JoinFilterIds(const ItemFilterList &list) {
  std::ostringstream os;
  for (int i = 0; i < list.count; i++) {
    if (i) {
      os << ',';
    }
    os << list.items[i].itemId;
  }
  return os.str();
}

/** 寫 delete_gfx／dissolve_gfx。 */
std::string JoinFilterGfx(const ItemFilterList &list) {
  std::ostringstream os;
  for (int i = 0; i < list.count; i++) {
    if (i) {
      os << ',';
    }
    os << list.items[i].gfxid;
  }
  return os.str();
}

/** 寫 delete_names／dissolve_names：UTF-8，以 | 分隔。 */
std::string JoinFilterNames(const ItemFilterList &list) {
  std::ostringstream os;
  for (int i = 0; i < list.count; i++) {
    if (i) {
      os << '|';
    }
    os << WideToUtf8(list.items[i].name);
  }
  return os.str();
}

/**
 * @brief 專用名稱，校正整個 PssConfig 結構之數值範圍，避免與其他標頭的 ClampConfig 衝突。
 * @param cfg 欲校正的設定檔結構參考
 */
void ClampPssFixedBuff(PssFixedBuff &sec) {
  for (int i = 0; i < kPssFixedBuffSlots; i++) {
    ClampPssSlot(sec.slots[i]);
  }
}

void ClampPssCustomBuff(PssCustomBuff &sec) {
  for (int i = 0; i < kPssCustomBuffSlots; i++) {
    ClampPssSlot(sec.slots[i]);
  }
}

void ClampPssConfig(PssConfig &cfg) {
  ClampPssSection(cfg.heal);
  ClampPssSection(cfg.mana);
  ClampPssFixedBuff(cfg.fixedBuff);
  ClampPssCustomBuff(cfg.customBuff);
  ClampItemFilterList(cfg.autoDelete);
  ClampItemFilterList(cfg.autoDissolve);
  if (cfg.eatMeatItemId < 0) {
    cfg.eatMeatItemId = 0;
  }
  if (cfg.whetstoneItemId < 0) {
    cfg.whetstoneItemId = 0;
  }
}

} // namespace

/**
 * @brief 檢查設定檔中是否有任何已啟用的喝水/技能欄位。
 * @param cfg 自動喝水設定檔參考
 * @return true 代表至少有一個有效欄位，否則為 false
 */
bool PssConfig_HasAnySlot(const PssConfig &cfg) {
  for (int i = 0; i < kPssSlotsPerSection; i++) {
    if (cfg.heal.slots[i].kind != PssSlot_None && cfg.heal.slots[i].id > 0) {
      return true;
    }
    if (cfg.mana.slots[i].kind != PssSlot_None && cfg.mana.slots[i].id > 0) {
      return true;
    }
  }
  for (int i = 0; i < kPssFixedBuffSlots; i++) {
    if (cfg.fixedBuff.slots[i].kind != PssSlot_None &&
        cfg.fixedBuff.slots[i].id > 0) {
      return true;
    }
  }
  for (int i = 0; i < kPssCustomBuffSlots; i++) {
    if (cfg.customBuff.slots[i].kind != PssSlot_None &&
        cfg.customBuff.slots[i].id > 0) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 自設定檔 (auto_potion.cfg) 載入自動喝水與輔助設定。
 * @return 載入並校正後的 PssConfig 結構
 */
PssConfig PssConfig_Load() {
  PssConfig cfg;
  const std::string path = ConfigFilePath();
  std::ifstream fin(path);
  if (!fin.is_open()) {
    ApCfgLog("[Pss] load: file not found (%s), using defaults",
                         path.c_str());
    return cfg;
  }

  std::string deleteIds, deleteGfx, deleteNames;
  std::string dissolveIds, dissolveGfx, dissolveNames;

  std::string line;
  while (std::getline(fin, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    if (key == "enabled") {
      cfg.enabled = (atoi(value.c_str()) != 0);
    } else if (key == "status.eatMeat") {
      cfg.eatMeat = (atoi(value.c_str()) != 0);
    } else if (key == "status.whetstone") {
      cfg.whetstone = (atoi(value.c_str()) != 0);
    } else if (key == "status.eatMeatItemId") {
      cfg.eatMeatItemId = atoi(value.c_str());
    } else if (key == "status.whetstoneItemId") {
      cfg.whetstoneItemId = atoi(value.c_str());
    } else if (key == "client.showDamage") {
      cfg.showDamage = (atoi(value.c_str()) != 0);
    } else if (key == "client.underwaterPump") {
      cfg.underwaterPump = (atoi(value.c_str()) != 0);
    } else if (key == "client.allDay") {
      cfg.allDay = (atoi(value.c_str()) != 0);
    } else if (key.rfind("craft.darkStone", 0) == 0 && key.size() == 16) {
      int idx = key[15] - '0';
      if (idx >= 0 && idx < 4) {
        cfg.darkStone[idx] = (atoi(value.c_str()) != 0);
      }
    } else if (key == "delete_ids") {
      deleteIds = value;
    } else if (key == "delete_gfx") {
      deleteGfx = value;
    } else if (key == "delete_names") {
      deleteNames = value;
    } else if (key == "dissolve_ids") {
      dissolveIds = value;
    } else if (key == "dissolve_gfx") {
      dissolveGfx = value;
    } else if (key == "dissolve_names") {
      dissolveNames = value;
    } else if (key.rfind("heal.slot", 0) == 0 || key.rfind("mana.slot", 0) == 0) {
      PssSection *sec = (key[0] == 'h') ? &cfg.heal : &cfg.mana;
      size_t slotPos = key.find(".slot");
      if (slotPos == std::string::npos) {
        continue;
      }
      int idx = key[slotPos + 5] - '0';
      if (idx < 0 || idx >= kPssSlotsPerSection) {
        continue;
      }
      size_t fieldDot = key.find('.', slotPos + 6);
      if (fieldDot == std::string::npos) {
        continue;
      }
      std::string field = key.substr(fieldDot + 1);
      if (field == "kind") {
        sec->slots[idx].kind = atoi(value.c_str());
      } else if (field == "id") {
        sec->slots[idx].id = atoi(value.c_str());
      } else if (field == "gfxid") {
        sec->slots[idx].gfxid = atoi(value.c_str());
      } else if (field == "threshold") {
        sec->slots[idx].thresholdPercent = atoi(value.c_str());
      } else if (field == "threshold2") {
        sec->slots[idx].thresholdPercent2 = atoi(value.c_str());
      } else if (field == "name") {
        Utf8ToWide(value, sec->slots[idx].name, _countof(sec->slots[idx].name));
      } else if (field == "count") {
        sec->slots[idx].count = atoi(value.c_str());
      }
    } else if (key.rfind("buff.slot", 0) == 0) {
      size_t slotPos = key.find(".slot");
      if (slotPos == std::string::npos) {
        continue;
      }
      int idx = key[slotPos + 5] - '0';
      if (idx < 0 || idx >= kPssFixedBuffSlots) {
        continue;
      }
      size_t fieldDot = key.find('.', slotPos + 6);
      if (fieldDot == std::string::npos) {
        continue;
      }
      std::string field = key.substr(fieldDot + 1);
      if (field == "kind") {
        cfg.fixedBuff.slots[idx].kind = atoi(value.c_str());
      } else if (field == "id") {
        cfg.fixedBuff.slots[idx].id = atoi(value.c_str());
      } else if (field == "gfxid") {
        cfg.fixedBuff.slots[idx].gfxid = atoi(value.c_str());
      } else if (field == "name") {
        Utf8ToWide(value, cfg.fixedBuff.slots[idx].name,
                   _countof(cfg.fixedBuff.slots[idx].name));
      } else if (field == "count") {
        cfg.fixedBuff.slots[idx].count = atoi(value.c_str());
      }
    } else if (key.rfind("buffCustom.slot", 0) == 0) {
      size_t slotPos = key.find(".slot");
      if (slotPos == std::string::npos) {
        continue;
      }
      int idx = key[slotPos + 5] - '0';
      if (idx < 0 || idx >= kPssCustomBuffSlots) {
        continue;
      }
      size_t fieldDot = key.find('.', slotPos + 6);
      if (fieldDot == std::string::npos) {
        continue;
      }
      std::string field = key.substr(fieldDot + 1);
      if (field == "kind") {
        cfg.customBuff.slots[idx].kind = atoi(value.c_str());
      } else if (field == "id") {
        cfg.customBuff.slots[idx].id = atoi(value.c_str());
      } else if (field == "gfxid") {
        cfg.customBuff.slots[idx].gfxid = atoi(value.c_str());
      } else if (field == "name") {
        Utf8ToWide(value, cfg.customBuff.slots[idx].name,
                   _countof(cfg.customBuff.slots[idx].name));
      } else if (field == "count") {
        cfg.customBuff.slots[idx].count = atoi(value.c_str());
      }
    }
    // 舊版 heal.threshold / mana.threshold、status.eatMeatThreshold 忽略。
  }
  ParseItemFilterList(cfg.autoDelete, deleteIds, deleteGfx, deleteNames);
  ParseItemFilterList(cfg.autoDissolve, dissolveIds, dissolveGfx, dissolveNames);
  ClampPssConfig(cfg);
  ApCfgLog(
      "[Pss] load: enabled=%d eatMeat=%d whetstone=%d showDamage=%d pump=%d",
      (int)cfg.enabled, (int)cfg.eatMeat, (int)cfg.whetstone, (int)cfg.showDamage,
      (int)cfg.underwaterPump);
  return cfg;
}

/**
 * @brief 將自動喝水設定儲存至 auto_potion.cfg 檔案中。
 * @param cfgIn 欲儲存的設定結構
 * @return 成功寫入檔案回傳 true，失敗回傳 false
 */
bool PssConfig_Save(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);
  const std::string path = ConfigFilePath();
  std::ofstream fout(path, std::ios::trunc);
  if (!fout.is_open()) {
    ApCfgLog("[Pss] save: FAILED to open %s", path.c_str());
    return false;
  }

  fout << "enabled=" << (cfg.enabled ? 1 : 0) << "\n";
  for (int i = 0; i < kPssSlotsPerSection; i++) {
    fout << "heal.slot" << i << ".kind=" << cfg.heal.slots[i].kind << "\n";
    fout << "heal.slot" << i << ".id=" << cfg.heal.slots[i].id << "\n";
    fout << "heal.slot" << i << ".gfxid=" << cfg.heal.slots[i].gfxid << "\n";
    fout << "heal.slot" << i << ".threshold=" << cfg.heal.slots[i].thresholdPercent
         << "\n";
    fout << "heal.slot" << i << ".threshold2=" << cfg.heal.slots[i].thresholdPercent2
         << "\n";
    fout << "heal.slot" << i << ".name=" << WideToUtf8(cfg.heal.slots[i].name)
         << "\n";
    fout << "heal.slot" << i << ".count=" << cfg.heal.slots[i].count << "\n";
    fout << "mana.slot" << i << ".kind=" << cfg.mana.slots[i].kind << "\n";
    fout << "mana.slot" << i << ".id=" << cfg.mana.slots[i].id << "\n";
    fout << "mana.slot" << i << ".gfxid=" << cfg.mana.slots[i].gfxid << "\n";
    fout << "mana.slot" << i << ".threshold=" << cfg.mana.slots[i].thresholdPercent
         << "\n";
    fout << "mana.slot" << i << ".threshold2=" << cfg.mana.slots[i].thresholdPercent2
         << "\n";
    fout << "mana.slot" << i << ".name=" << WideToUtf8(cfg.mana.slots[i].name)
         << "\n";
    fout << "mana.slot" << i << ".count=" << cfg.mana.slots[i].count << "\n";
  }
  for (int i = 0; i < kPssFixedBuffSlots; i++) {
    fout << "buff.slot" << i << ".kind=" << cfg.fixedBuff.slots[i].kind << "\n";
    fout << "buff.slot" << i << ".id=" << cfg.fixedBuff.slots[i].id << "\n";
    fout << "buff.slot" << i << ".gfxid=" << cfg.fixedBuff.slots[i].gfxid << "\n";
    fout << "buff.slot" << i << ".name="
         << WideToUtf8(cfg.fixedBuff.slots[i].name) << "\n";
    fout << "buff.slot" << i << ".count=" << cfg.fixedBuff.slots[i].count
         << "\n";
  }
  for (int i = 0; i < kPssCustomBuffSlots; i++) {
    fout << "buffCustom.slot" << i
         << ".kind=" << cfg.customBuff.slots[i].kind << "\n";
    fout << "buffCustom.slot" << i << ".id=" << cfg.customBuff.slots[i].id
         << "\n";
    fout << "buffCustom.slot" << i
         << ".gfxid=" << cfg.customBuff.slots[i].gfxid << "\n";
    fout << "buffCustom.slot" << i << ".name="
         << WideToUtf8(cfg.customBuff.slots[i].name) << "\n";
    fout << "buffCustom.slot" << i
         << ".count=" << cfg.customBuff.slots[i].count << "\n";
  }
  // status：一律 0/1，跟 enabled= 風格一致。
  fout << "status.eatMeat=" << (cfg.eatMeat ? 1 : 0) << "\n";
  fout << "status.whetstone=" << (cfg.whetstone ? 1 : 0) << "\n";
  fout << "status.eatMeatItemId=" << cfg.eatMeatItemId << "\n";
  fout << "status.whetstoneItemId=" << cfg.whetstoneItemId << "\n";
  fout << "client.showDamage=" << (cfg.showDamage ? 1 : 0) << "\n";
  fout << "client.underwaterPump=" << (cfg.underwaterPump ? 1 : 0) << "\n";
  fout << "client.allDay=" << (cfg.allDay ? 1 : 0) << "\n";
  for (int i = 0; i < 4; i++) {
    fout << "craft.darkStone" << i << "=" << (cfg.darkStone[i] ? 1 : 0) << "\n";
  }
  fout << "delete_ids=" << JoinFilterIds(cfg.autoDelete) << "\n";
  fout << "delete_gfx=" << JoinFilterGfx(cfg.autoDelete) << "\n";
  fout << "delete_names=" << JoinFilterNames(cfg.autoDelete) << "\n";
  fout << "dissolve_ids=" << JoinFilterIds(cfg.autoDissolve) << "\n";
  fout << "dissolve_gfx=" << JoinFilterGfx(cfg.autoDissolve) << "\n";
  fout << "dissolve_names=" << JoinFilterNames(cfg.autoDissolve) << "\n";
  ApCfgLog("[Pss] save: wrote %s enabled=%d eat=%d whet=%d dmg=%d pump=%d",
                       path.c_str(), (int)cfg.enabled, (int)cfg.eatMeat,
                       (int)cfg.whetstone, (int)cfg.showDamage,
                       (int)cfg.underwaterPump);
  return true;
}

/** opcode 128 magic 0x5F：啟動／停止旗標。cccc = opcode+magic+enabled+pad，4 bytes。 */
void PssConfig_SendEnabledToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);
  SendPacketData("cccc", (int)kOpcodeItemFilter, (int)kMagicEnabledFlag,
                 (int)(cfg.enabled ? 1 : 0), 0);
  ApCfgLog("[Pss] send enabled=%d (128/0x5F 4-byte)", (int)cfg.enabled);
}

/**
 * opcode 128 magic 0x60/0x61：治療／補魔六槽（kPssSlotsPerSection）。
 * 2026-09-17：每格多加一個 threshold2（目前只有補魔第 6 格的「HP% 安全下限」
 * 在用，其餘一律 0）。header(cccc=opcode+magic+pad+n) +
 * 6×(threshold(c)+threshold2(c)+kind(c)+id(d)) = 4+24=28 chars,
 * bytes = 22c + 6d = 22+24 = 46。
 */
static void SendSectionSlots(BYTE magic, const PssSection &sec) {
  SendPacketData(
      "cccc" "cccdcccdcccdcccdcccdcccd", (int)kOpcodeItemFilter, (int)magic,
      0, kPssSlotsPerSection,
      (int)sec.slots[0].thresholdPercent, (int)sec.slots[0].thresholdPercent2,
      (int)sec.slots[0].kind, (int)sec.slots[0].id,
      (int)sec.slots[1].thresholdPercent, (int)sec.slots[1].thresholdPercent2,
      (int)sec.slots[1].kind, (int)sec.slots[1].id,
      (int)sec.slots[2].thresholdPercent, (int)sec.slots[2].thresholdPercent2,
      (int)sec.slots[2].kind, (int)sec.slots[2].id,
      (int)sec.slots[3].thresholdPercent, (int)sec.slots[3].thresholdPercent2,
      (int)sec.slots[3].kind, (int)sec.slots[3].id,
      (int)sec.slots[4].thresholdPercent, (int)sec.slots[4].thresholdPercent2,
      (int)sec.slots[4].kind, (int)sec.slots[4].id,
      (int)sec.slots[5].thresholdPercent, (int)sec.slots[5].thresholdPercent2,
      (int)sec.slots[5].kind, (int)sec.slots[5].id);
}

/**
 * 送啟動／停止＋治療／補魔槽設定。
 *
 * 2026-09-16：原本用單一 opcode 75、62-byte（33 個參數）的 SendPacketData 送出，
 * 實測伺服器每次都收成 64 bytes（多 2 bytes），導致 enabled 旗標／恢復槽設定
 * 完全送不到、BUFF-固定跟著判斷不到啟動狀態——已用 client 端 launcher.log 核對過
 * client 這邊確實組出剛好 62 bytes 送出，問題發生在送出之後、原生封包層看不到的
 * 地方，原因不明。改拆成三個各自獨立、跟 BUFF-固定／自訂九格同一套已驗證可靠寫法
 * 的小封包（opcode 128 / magic 0x5F、0x60、0x61）繞過去。
 */
void PssConfig_SendToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);
  PssConfig_SendEnabledToServer(cfg);
  SendSectionSlots(kMagicHealSlots, cfg.heal);
  SendSectionSlots(kMagicManaSlots, cfg.mana);
  ApCfgLog("[Pss] send potion: done (128/0x5F+0x60+0x61, enabled=%d)",
          (int)cfg.enabled);
}

/**
 * @brief 送其他頁打勾（吃肉／修武）。
 * opcode 128 + magic 0x5A + flags + pad = 4 bytes。
 * 原生 C_SecurityStatus 城堡治安是 5 bytes，長度不同才不會開 CastleS。
 * 不傳 itemId（舊 75 的 12-byte／magic 0x57 已廢）。
 */
void PssConfig_SendStatusToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);

  // bit0=吃肉 bit1=修武。opcode 128、長度 4（≠ 原生城堡 5）。
  BYTE flags = 0;
  if (cfg.eatMeat) {
    flags |= 0x01; // 對齊 L1PlaySupportSystem.applyMiscFlags
  }
  if (cfg.whetstone) {
    flags |= 0x02;
  }

  // cccc：opcode、0x5A、flags、pad=0。不要再塞 dd itemId。
  SendPacketData("cccc", (int)kOpcodeItemFilter, (int)kMagicMiscFlags, (int)flags,
                 0);
  ApCfgLog("[Pss] send misc flags=0x%02X (128/0x5A 4-byte)", (unsigned)flags);
}

/**
 * @brief 送提煉黑魔石四勾。
 * opcode 128 + magic 0x5C + flags + pad = 4 bytes（≠ 城堡 5，也不加長 0x5A）。
 * bit0～3＝一級～四級；沒勾的級跳過，不要隱含從一級一路煉上來。
 */
void PssConfig_SendCraftToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);

  BYTE flags = 0;
  for (int i = 0; i < 4; i++) {
    if (cfg.darkStone[i]) {
      flags |= (BYTE)(1 << i);
    }
  }

  SendPacketData("cccc", (int)kOpcodeItemFilter, (int)kMagicCraftFlags, (int)flags,
                 0);
  ApCfgLog("[Pss] send craft flags=0x%02X (128/0x5C 4-byte)", (unsigned)flags);
}

void PssConfig_SendFixedBuffToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);
  int kind[kPssFixedBuffSlots] = {};
  int id[kPssFixedBuffSlots] = {};
  for (int i = 0; i < kPssFixedBuffSlots; i++) {
    const PssSlot &s = cfg.fixedBuff.slots[i];
    // 2026-09-17：第 7 格（解毒）以外一律只能是道具；第 7 格才可能是技能
    // （解毒術/聖潔之光），跟伺服器 FIXED_BUFF_SLOT_DETOX 對齊。
    if (s.kind == PssSlot_Item && s.id > 0) {
      kind[i] = PssSlot_Item;
      id[i] = s.id;
    } else if (i == kFixedBuffDetoxSlotIndex && s.kind == PssSlot_Skill &&
               s.id > 0) {
      kind[i] = PssSlot_Skill;
      id[i] = s.id;
    }
  }
  // opcode + 0x5D + pad + n=10 (4 c's) + 10×kind (10 c's) + 10×itemId/skillId (10 d's) = 24 chars
  SendPacketData("cccc" "cccccccccc" "dddddddddd", (int)kOpcodeItemFilter,
                 (int)kMagicFixedBuff, 0, kPssFixedBuffSlots, kind[0], kind[1],
                 kind[2], kind[3], kind[4], kind[5], kind[6], kind[7],
                 kind[8], kind[9], id[0], id[1], id[2], id[3], id[4], id[5],
                 id[6], id[7], id[8], id[9]);
  ApCfgLog("[Pss] send fixed buff 128/0x5D");
}

void PssConfig_SendCustomBuffToServer(const PssConfig &cfgIn) {
  PssConfig cfg = cfgIn;
  ClampPssConfig(cfg);
  int kind[kPssCustomBuffSlots] = {};
  int id[kPssCustomBuffSlots] = {};
  for (int i = 0; i < kPssCustomBuffSlots; i++) {
    const PssSlot &s = cfg.customBuff.slots[i];
    if ((s.kind == PssSlot_Item || s.kind == PssSlot_Skill) && s.id > 0) {
      kind[i] = s.kind;
      id[i] = s.id;
    }
  }
  // opcode + 0x5E + pad + n=9 (4 c's) + 9×kind (9 c's) + 9×itemId/skillId (9 d's) = 22 chars
  SendPacketData("cccc" "ccccccccc" "ddddddddd", (int)kOpcodeItemFilter,
                 (int)kMagicCustomBuff, 0, kPssCustomBuffSlots, kind[0],
                 kind[1], kind[2], kind[3], kind[4], kind[5], kind[6],
                 kind[7], kind[8], id[0], id[1], id[2], id[3], id[4], id[5],
                 id[6], id[7], id[8]);
  ApCfgLog("[Pss] send custom buff 128/0x5E");
}

/**
 * @brief 發送道具解析請求至伺服器。
 * @param section 0 治療／1 補魔／2 固定／3 自訂道具／4 自訂技能
 * @param slotIndex 欄位索引
 * @param objId section 0–3：背包實例 id；section 4：伺服器 skillId（packed+1）
 */
void PssConfig_SendResolveItemRequest(int section, int slotIndex,
                                             DWORD objId) {
  ApCfgLog(
      "[Pss] resolve request: section=%d slotIndex=%d objId=%u(0x%X)",
      section, slotIndex, (unsigned)objId, (unsigned)objId);
  // opcode+section+slotIndex+objId+pad = 8 bytes，對齊伺服器
  // RESOLVE_ITEM_PAYLOAD_LEN（2026-09-10 伺服器那邊加了 pad 從 7 改成 8，
  // 這裡原本沒跟著改，導致點道具請求全部被靜默吃掉——已修正）。
  SendPacketData("cccdc", (int)kOpcodePlaySupport, section, slotIndex,
                 (int)objId, 0); // 最後 0=pad
}

/**
 * @brief 發送 UI 可見性狀態通知至伺服器。
 * @param visible UI 是否顯示
 */
void PssConfig_SendUiVisible(bool visible) {
  ApCfgLog("[Pss] ui visible=%d (4-byte notify)", (int)visible);
  // cccc：75、0x56、0/1、pad。長度與 128 flags 同為 4，靠 opcode 分開。
  SendPacketData("cccc", (int)kOpcodePlaySupport, (int)kMagicUiVisible,
                 (int)(visible ? 1 : 0), 0);
}

/**
 * opcode 128：刪除／溶解名單。n=0 只送 4 bytes；n>0 只帶 n 個 d（n=1 為 8 bytes）。
 * 不可固定 20 個 d：native SendPacketData 緩衝不夠時會打亂後續加密。
 * 第一包 magic 0x58 覆寫，超過 20 筆第二包 0x5B 追加。
 */
static void SendItemFilterChunk(BYTE magic, int listType, int n, const int *ids) {
  if (n <= 0) {
    SendPacketData("cccc", (int)kOpcodeItemFilter, (int)magic, listType, 0);
    return;
  }
  int a[kItemFilterChunk] = {};
  if (n > kItemFilterChunk) {
    n = kItemFilterChunk;
  }
  memcpy(a, ids, (size_t)n * sizeof(int));
  char fmt[8 + kItemFilterChunk] = "cccc";
  for (int i = 0; i < n; i++) {
    fmt[4 + i] = 'd';
  }
  fmt[4 + n] = 0;
  // 多餘的 a[] 引數在 fmt 只有 n 個 d 時不會被寫進封包。
  SendPacketData(fmt, (int)kOpcodeItemFilter, (int)magic, listType, n, a[0],
                 a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10],
                 a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], a[19]);
}

void PssConfig_SendItemFilterList(int listType, const ItemFilterList &listIn) {
  int ids[kItemFilterMax] = {};
  int n = 0;
  const int cap = listIn.count < kItemFilterMax ? listIn.count : kItemFilterMax;
  for (int i = 0; i < cap; i++) {
    if (listIn.items[i].itemId > 0) {
      ids[n++] = listIn.items[i].itemId;
    }
  }
  SendItemFilterChunk(kMagicItemFilterSync, listType,
                      n < kItemFilterChunk ? n : kItemFilterChunk, ids);
  int off = n < kItemFilterChunk ? n : kItemFilterChunk;
  while (off < n) {
    int chunk = n - off;
    if (chunk > kItemFilterChunk) {
      chunk = kItemFilterChunk;
    }
    SendItemFilterChunk(kMagicItemFilterAppend, listType, chunk, ids + off);
    off += chunk;
  }
  ApCfgLog("[Pss] send item-filter type=%d n=%d", listType, n);
}

/** opcode 128 magic 0x59：請伺服器推 PacketBox 47。cccc＝opcode、magic、listType、pad。 */
void PssConfig_RequestItemFilterList(int listType) {
  SendPacketData("cccc", (int)kOpcodeItemFilter, (int)kMagicItemFilterRequest,
                 listType, 0);
}
