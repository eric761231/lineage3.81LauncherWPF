// AutoPotionConfig.cpp: see AutoPotionConfig.h.
#include "stdafx.h"
#include "AutoPotionConfig.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cstdlib>

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
// 對齊伺服器 C_PlaySupport（opcode 75）。本地常數名可改，數值不能改。
constexpr BYTE kOpcodePlaySupport = 75; // 0x4B
constexpr BYTE kMagicUiVisible = 0x56;
constexpr BYTE kMagicStatusSupport = 0x57; // 吃肉／修武

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
 * @brief 校正並限制 AutoPotionSlot 的數值範圍。
 * @param s 欲限制的喝水/技能欄位參考
 */
void ClampAutoPotionSlot(AutoPotionSlot &s) {
  if (s.kind < AutoPotionSlot_None || s.kind > AutoPotionSlot_Skill) {
    s.kind = AutoPotionSlot_None;
  }
  if (s.kind == AutoPotionSlot_None) {
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
}

/**
 * @brief 校正並限制 AutoPotionSection 的數值範圍。
 * @param sec 欲限制的區塊參考（包含多個 Slots）
 */
void ClampAutoPotionSection(AutoPotionSection &sec) {
  for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
    ClampAutoPotionSlot(sec.slots[i]);
  }
}

/**
 * @brief 專用名稱，校正整個 AutoPotionConfig 結構之數值範圍，避免與其他標頭的 ClampConfig 衝突。
 * @param cfg 欲校正的設定檔結構參考
 */
void ClampAutoPotionConfig(AutoPotionConfig &cfg) {
  ClampAutoPotionSection(cfg.heal);
  ClampAutoPotionSection(cfg.mana);
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
bool AutoPotionConfig_HasAnySlot(const AutoPotionConfig &cfg) {
  for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
    if (cfg.heal.slots[i].kind != AutoPotionSlot_None && cfg.heal.slots[i].id > 0) {
      return true;
    }
    if (cfg.mana.slots[i].kind != AutoPotionSlot_None && cfg.mana.slots[i].id > 0) {
      return true;
    }
  }
  return false;
}

/**
 * @brief 自設定檔 (auto_potion.cfg) 載入自動喝水與輔助設定。
 * @return 載入並校正後的 AutoPotionConfig 結構
 */
AutoPotionConfig AutoPotionConfig_Load() {
  AutoPotionConfig cfg;
  const std::string path = ConfigFilePath();
  std::ifstream fin(path);
  if (!fin.is_open()) {
    ApCfgLog("[AutoPotion] load: file not found (%s), using defaults",
                         path.c_str());
    return cfg;
  }

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
    } else if (key.rfind("heal.slot", 0) == 0 || key.rfind("mana.slot", 0) == 0) {
      AutoPotionSection *sec = (key[0] == 'h') ? &cfg.heal : &cfg.mana;
      size_t slotPos = key.find(".slot");
      if (slotPos == std::string::npos) {
        continue;
      }
      int idx = key[slotPos + 5] - '0';
      if (idx < 0 || idx >= kAutoPotionSlotsPerSection) {
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
      } else if (field == "name") {
        Utf8ToWide(value, sec->slots[idx].name, _countof(sec->slots[idx].name));
      } else if (field == "count") {
        sec->slots[idx].count = atoi(value.c_str());
      }
    }
    // 舊版 heal.threshold / mana.threshold、status.eatMeatThreshold 忽略。
  }
  ClampAutoPotionConfig(cfg);
  ApCfgLog(
      "[AutoPotion] load: enabled=%d eatMeat=%d whetstone=%d showDamage=%d",
      (int)cfg.enabled, (int)cfg.eatMeat, (int)cfg.whetstone, (int)cfg.showDamage);
  return cfg;
}

/**
 * @brief 將自動喝水設定儲存至 auto_potion.cfg 檔案中。
 * @param cfgIn 欲儲存的設定結構
 * @return 成功寫入檔案回傳 true，失敗回傳 false
 */
bool AutoPotionConfig_Save(const AutoPotionConfig &cfgIn) {
  AutoPotionConfig cfg = cfgIn;
  ClampAutoPotionConfig(cfg);
  const std::string path = ConfigFilePath();
  std::ofstream fout(path, std::ios::trunc);
  if (!fout.is_open()) {
    ApCfgLog("[AutoPotion] save: FAILED to open %s", path.c_str());
    return false;
  }

  fout << "enabled=" << (cfg.enabled ? 1 : 0) << "\n";
  for (int i = 0; i < kAutoPotionSlotsPerSection; i++) {
    fout << "heal.slot" << i << ".kind=" << cfg.heal.slots[i].kind << "\n";
    fout << "heal.slot" << i << ".id=" << cfg.heal.slots[i].id << "\n";
    fout << "heal.slot" << i << ".gfxid=" << cfg.heal.slots[i].gfxid << "\n";
    fout << "heal.slot" << i << ".threshold=" << cfg.heal.slots[i].thresholdPercent
         << "\n";
    fout << "heal.slot" << i << ".name=" << WideToUtf8(cfg.heal.slots[i].name)
         << "\n";
    fout << "heal.slot" << i << ".count=" << cfg.heal.slots[i].count << "\n";
    fout << "mana.slot" << i << ".kind=" << cfg.mana.slots[i].kind << "\n";
    fout << "mana.slot" << i << ".id=" << cfg.mana.slots[i].id << "\n";
    fout << "mana.slot" << i << ".gfxid=" << cfg.mana.slots[i].gfxid << "\n";
    fout << "mana.slot" << i << ".threshold=" << cfg.mana.slots[i].thresholdPercent
         << "\n";
    fout << "mana.slot" << i << ".name=" << WideToUtf8(cfg.mana.slots[i].name)
         << "\n";
    fout << "mana.slot" << i << ".count=" << cfg.mana.slots[i].count << "\n";
  }
  // status：一律 0/1，跟 enabled= 風格一致。
  fout << "status.eatMeat=" << (cfg.eatMeat ? 1 : 0) << "\n";
  fout << "status.whetstone=" << (cfg.whetstone ? 1 : 0) << "\n";
  fout << "status.eatMeatItemId=" << cfg.eatMeatItemId << "\n";
  fout << "status.whetstoneItemId=" << cfg.whetstoneItemId << "\n";
  fout << "client.showDamage=" << (cfg.showDamage ? 1 : 0) << "\n";
  ApCfgLog("[AutoPotion] save: wrote %s enabled=%d eat=%d whet=%d dmg=%d",
                       path.c_str(), (int)cfg.enabled, (int)cfg.eatMeat,
                       (int)cfg.whetstone, (int)cfg.showDamage);
  return true;
}

/**
 * @brief 發送自動喝水/技能設定至伺服器。
 * @param cfgIn 自動喝水設定
 */
void AutoPotionConfig_SendToServer(const AutoPotionConfig &cfgIn) {
  AutoPotionConfig cfg = cfgIn;
  ClampAutoPotionConfig(cfg);
  ApCfgLog("[AutoPotion] send potion: opcode=%u enabled=%d",
                       (unsigned)kOpcodePlaySupport, (int)cfg.enabled);

  // 62 bytes：opcode+enabled + 5×heal(ccd) + 5×mana(ccd)
  SendPacketData(
      "ccccdccdccdccdccdccdccdccdccdccd", (int)kOpcodePlaySupport,
      (int)(cfg.enabled ? 1 : 0),
      (int)cfg.heal.slots[0].thresholdPercent, (int)cfg.heal.slots[0].kind,
      (int)cfg.heal.slots[0].id, (int)cfg.heal.slots[1].thresholdPercent,
      (int)cfg.heal.slots[1].kind, (int)cfg.heal.slots[1].id,
      (int)cfg.heal.slots[2].thresholdPercent, (int)cfg.heal.slots[2].kind,
      (int)cfg.heal.slots[2].id, (int)cfg.heal.slots[3].thresholdPercent,
      (int)cfg.heal.slots[3].kind, (int)cfg.heal.slots[3].id,
      (int)cfg.heal.slots[4].thresholdPercent, (int)cfg.heal.slots[4].kind,
      (int)cfg.heal.slots[4].id, (int)cfg.mana.slots[0].thresholdPercent,
      (int)cfg.mana.slots[0].kind, (int)cfg.mana.slots[0].id,
      (int)cfg.mana.slots[1].thresholdPercent, (int)cfg.mana.slots[1].kind,
      (int)cfg.mana.slots[1].id, (int)cfg.mana.slots[2].thresholdPercent,
      (int)cfg.mana.slots[2].kind, (int)cfg.mana.slots[2].id,
      (int)cfg.mana.slots[3].thresholdPercent, (int)cfg.mana.slots[3].kind,
      (int)cfg.mana.slots[3].id, (int)cfg.mana.slots[4].thresholdPercent,
      (int)cfg.mana.slots[4].kind, (int)cfg.mana.slots[4].id);
  ApCfgLog("[AutoPotion] send potion: done (62-byte)");
}

/**
 * @brief 發送輔助狀態（吃肉／修武）至伺服器。
 * @param cfgIn 自動喝水設定
 */
void AutoPotionConfig_SendStatusToServer(const AutoPotionConfig &cfgIn) {
  AutoPotionConfig cfg = cfgIn;
  ClampAutoPotionConfig(cfg);

  // bit0=吃肉 bit1=修武；後端用 (flags&1)、(flags&2) 拆。
  BYTE flags = 0;
  if (cfg.eatMeat) {
    flags |= 0x01;
  }
  if (cfg.whetstone) {
    flags |= 0x02;
  }

  // 12 bytes：opcode + magic 0x57 + flags + pad + meatId + whetId
  // pad 固定 0（不再傳 foodThreshold；後端用 get_food()<225）
  SendPacketData("ccccdd", (int)kOpcodePlaySupport, (int)kMagicStatusSupport,
                 (int)flags, 0, cfg.eatMeatItemId, cfg.whetstoneItemId);
  ApCfgLog(
      "[AutoPotion] send status: flags=0x%02X meatId=%d whetId=%d (12-byte)",
      (unsigned)flags, cfg.eatMeatItemId, cfg.whetstoneItemId);
}

/**
 * @brief 發送道具解析請求至伺服器。
 * @param section 區塊 (0: heal, 1: mana)
 * @param slotIndex 欄位索引 (0~4)
 * @param objId 道具之 Object ID
 */
void AutoPotionConfig_SendResolveItemRequest(int section, int slotIndex,
                                             DWORD objId) {
  ApCfgLog(
      "[AutoPotion] resolve request: section=%d slotIndex=%d objId=%u(0x%X)",
      section, slotIndex, (unsigned)objId, (unsigned)objId);
  // opcode+section+slotIndex+objId+pad = 8 bytes，對齊伺服器
  // RESOLVE_ITEM_PAYLOAD_LEN（2026-09-10 伺服器那邊加了 pad 從 7 改成 8，
  // 這裡原本沒跟著改，導致點道具請求全部被靜默吃掉——已修正）。
  SendPacketData("cccdc", (int)kOpcodePlaySupport, section, slotIndex,
                 (int)objId, 0);
}

/**
 * @brief 發送 UI 可見性狀態通知至伺服器。
 * @param visible UI 是否顯示
 */
void AutoPotionConfig_SendUiVisible(bool visible) {
  ApCfgLog("[AutoPotion] ui visible=%d (4-byte notify)", (int)visible);
  SendPacketData("cccc", (int)kOpcodePlaySupport, (int)kMagicUiVisible,
                 (int)(visible ? 1 : 0), 0);
}
