# Panda Bamboo Feeder – Klipper 緩衝韌體

BIQU **Panda Bamboo Feeder**（喂竹器）的非官方替換韌體，把它變成**單色 Klipper 印表機用的耗材緩衝器**，
加上進料、退料時的輔助推送。**不用原廠 5通**：喂竹器出料口直接用鐵氟龍管接到印表機。

English: [`README.md`](README.md)

> **本專案和 BIQU／BigTreeTech 無關。** 燒錄會把原廠韌體整個換掉，請先照安裝說明備份整顆 flash，風險自負。

## 功能

- **緩衝**：印表機把活動件拉回原位時自動輔助進料，並多送一點讓緩衝停在中間。螢幕上 `READY` 旁邊的箭頭會隨著輔助進料填滿。
- **進料**：把料送到印表機，Klipper 的 `filament_switch_sensor` 偵測到料就停。感測器可以從印表機的物件清單自動挑，也可以手動選。
  從齒輪開始的第一次進料會順便量出路徑長度。
- **退料輔助**：退料 G-code 可以用 `M117 PBF:FREE`（尖端成形時放開齒輪）和 `M117 PBF:FOLLOW`（跟著擠出機雙向同步）。
  沒有訊號時，非列印中的長回抽會被當成退料。
- **斷料**：料尾離開喂竹器就停手，交給印表機處理。
- **手工續料**：列印中斷料後插入新料，喂竹器會自動咬料、預載，再把新料推下管子接上舊料尾巴。
  預載完 3 分鐘內，卡料判定會放寬到能走完整段路徑。接頭快到擠出機時會持續施壓，直到擠出機把新料吃進去。
- **卡料偵測**：輔助進料後緩衝一直沒有回來，可以送 `PAUSE` 並用 Mobileraker 推播。
- **沒有輔助進料警報**：列印中印表機擠出 100mm（可設定）緩衝都沒空過，代表耗材不是從喂竹器來的
  （擠出機打滑或堵住、續料的新料沒被吃進去），會暫停列印並推播。用擠出量判斷，不是時間。
- **省電待機**：印表機閒置、噴頭冷掉時放開齒輪，印表機一加熱就夾回來。
- **安裝測試**：量緩衝行程，找出最低夠用的推料電流和夾持電流。
- **網頁**（繁體中文）：即時狀態、記錄、設定、韌體上傳。另外有 JSON API、OTA 更新，喂竹器小螢幕顯示狀態（英文）。
- 透過 WiFi 連 **Moonraker**。Moonraker 不信任喂竹器 IP 的話，可以另外填 API key。

## 目前狀態

v0.11.2。在 **Creality K1 Max** 上開發和使用。其他 Klipper 印表機應該可以用，但還沒有人實測。
燒錄用的 bin 檔會在目前版本實機驗證後放到 **Releases**。

## 需要

- BIQU Panda Bamboo Feeder 和原廠 24V 變壓器
- 跑 Klipper + Moonraker 的印表機，和喂竹器在同一個 2.4GHz 網路
- 建議：擠出機附近一顆 `filament_switch_sensor`、`[pause_resume]`、`[display_status]`（Mainsail、Fluidd 的設定檔都內建後兩個）

完整清單、需要的工具和燒錄步驟在 [`docs/安裝說明.md`](docs/安裝說明.md)。

## 文件

| 檔案 | 內容 |
|---|---|
| [`docs/安裝說明.md`](docs/安裝說明.md) | 安裝：工具、備份、燒錄、印表機端檢查清單、疑難排解 |
| [`docs/使用說明.md`](docs/使用說明.md) | 使用：按鍵、螢幕、狀態、網頁、設定 |
| [`docs/tip-forming-generic.md`](docs/tip-forming-generic.md) | 通用版退料／進料 G-code 範例（示範 `M117 PBF:…` 的用法） |
| [`docs/tip-forming.md`](docs/tip-forming.md) | Creality K1 Max 實測版 |

## 自己編譯

需要 [PlatformIO](https://platformio.org/)：

```bash
cd firmware/pbf-custom
pio run -e pbf_custom
```

輸出在 `.pio/build/pbf_custom/`（`bootloader.bin`、`partitions.bin`、`firmware.bin`），
`boot_app0.bin` 在 Arduino-ESP32 框架套件裡。USB 燒錄用 `pio run -e pbf_custom -t upload`；
已經在跑這套韌體的喂竹器也可以從網頁更新。

## 授權

MIT，見 [`LICENSE`](LICENSE)。第三方元件見 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
