# Panda Bamboo Feeder – Klipper 緩衝韌體

BIQU **Panda Bamboo Feeder**（喂竹器）的非官方替換韌體，把它變成**單色 Klipper 印表機用的耗材緩衝器**，加上進料、退料時的輔助推送。**不用原廠 5通**：喂竹器出料口直接用鐵氟龍管接到印表機。

English: [`README.md`](README.md)

> [!WARNING]
> **本專案和 BIQU／BigTreeTech 無關。** 燒錄會把原廠韌體整個換掉，請先照安裝說明備份整顆 flash，風險自負。

## 主打功能

### 1. 緩衝

印表機把活動件拉回原位時自動輔助進料，並多送一點讓緩衝停在中間，<mark><b>印表機吃的是緩衝裡的存料，不用自己從料捲硬拉</b></mark>。螢幕上 `READY` 旁邊的箭頭會隨著輔助進料填滿。

### 2. 料捲纏住／卡料偵測

<mark><b>料捲纏住、料尾固定在料盤上拉不出來、管子卡住，緩衝一空，幾秒內就暫停列印</b></mark>，不會讓擠出機一直啃料、少料，直到整個列印失敗。

原理：印表機把緩衝裡的存料用完後，喂竹器會輔助進料。料如果拉不動，活動件就一直停在原位不會被撐開；送滿 80mm（設定「卡料判定長度」，預設速度約 3 秒）還是沒撐開，喂竹器就停下，送 `PAUSE` 給 Klipper，並用 Mobileraker Companion 推播「PBF: 卡料」。

> [!TIP]
> 真正的斷料不一樣：料尾離開喂竹器時，喂竹器會停手，交給印表機自己的斷料處理。

## 其他功能

- **進料**：把料送到印表機，Klipper 的 `filament_switch_sensor` 偵測到料就停。感測器可以從印表機的物件清單自動挑，也可以手動選。從齒輪開始的第一次進料會順便量出路徑長度。
- **退料輔助**：退料 G-code 可以用 `M117 PBF:FREE`（尖端成形時放開齒輪）和 `M117 PBF:FOLLOW`（跟著擠出機雙向同步）。沒有訊號時，非列印中的長回抽會被當成退料。
- **斷料**：料尾離開喂竹器就停手，交給印表機處理。
- **手工續料**：列印中斷料後插入新料，喂竹器會自動咬料、預載，再把新料推下管子接上舊料尾巴。預載完 3 分鐘內，卡料判定會放寬到能走完整段路徑。接頭快到擠出機時會持續施壓，直到擠出機把新料吃進去。
- **沒有輔助進料警報**：和卡料剛好相反。列印中印表機擠出 100mm（可設定）緩衝都沒空過，代表耗材不是從喂竹器來的（擠出機打滑或堵住、續料的新料沒被吃進去），會暫停列印並推播。用擠出量判斷，不是時間。
- **噴頭關加熱就放鬆緩衝**：印表機噴頭目標溫度變成 0 後，把停駐的那段退回來、活動件回到原位就停，閒置時擠出機的管子卡扣不再被拉著。
- **省電待機**：印表機閒置、噴頭冷掉時放開齒輪，印表機一加熱就夾回來。
- **安裝測試**：量緩衝行程，找出最低夠用的推料電流和夾持電流。
- **網頁**（中文／English，右上角切換，推播通知跟著同一個語言）：即時狀態、記錄、設定、韌體上傳；容易設錯的設定旁有 **?** 可以點開說明。另外有 JSON API、OTA 更新，喂竹器小螢幕顯示狀態（英文）。
- 透過 WiFi 連 **Moonraker**。Moonraker 不信任喂竹器 IP 的話，可以另外填 API key。

## 目前狀態

**v0.11.9**。在 **Creality K1 Max** 上開發和使用。其他 Klipper 印表機應該可以用，但還沒有人實測。燒錄用的四個檔案和 `SHA256SUMS.txt` 在 [**Releases**](https://github.com/solnqq7-gif/Panda-Bamboo-Feeder-Klipper/releases/latest)。各版本的變更見下方[版本紀錄](#版本紀錄)。

## 需要

- BIQU Panda Bamboo Feeder 和原廠 24V 變壓器
- 跑 Klipper + Moonraker 的印表機，和喂竹器在同一個 2.4GHz 網路
- 建議：擠出機附近一顆 `filament_switch_sensor`、`[pause_resume]`、`[display_status]`（Mainsail、Fluidd 的設定檔都內建後兩個）
- 要收推播：手機裝 [Mobileraker](https://github.com/Clon1998/mobileraker)，印表機裝它的 Companion

完整清單、需要的工具和燒錄步驟在 [`docs/安裝說明.md`](docs/安裝說明.md)。

## 文件

| 中文 | English | 內容 |
|---|---|---|
| [`docs/安裝說明.md`](docs/安裝說明.md) | [`docs/en/installation.md`](docs/en/installation.md) | 安裝：工具、備份、燒錄、印表機端檢查清單、疑難排解 |
| [`docs/使用說明.md`](docs/使用說明.md) | [`docs/en/user-guide.md`](docs/en/user-guide.md) | 使用：按鍵、螢幕、狀態、網頁、設定 |
| [`docs/tip-forming-generic.md`](docs/tip-forming-generic.md) | [`docs/en/tip-forming-generic.md`](docs/en/tip-forming-generic.md) | 通用版退料／進料 G-code 範例（示範 `M117 PBF:…` 的用法） |
| [`docs/tip-forming.md`](docs/tip-forming.md) | [`docs/en/tip-forming.md`](docs/en/tip-forming.md) | Creality K1 Max 實測版 |

## 自己編譯

需要 [PlatformIO](https://platformio.org/)：

```bash
cd firmware/pbf-custom
pio run -e pbf_custom
```

輸出在 `.pio/build/pbf_custom/`（`bootloader.bin`、`partitions.bin`、`firmware.bin`），`boot_app0.bin` 在 Arduino-ESP32 框架套件裡。USB 燒錄用 `pio run -e pbf_custom -t upload`；已經在跑這套韌體的喂竹器也可以從網頁更新。

桌面回歸檢查（需要 Python 3 與支援 C++17 的 `g++`，可用 `CXX` 指定編譯器），在 `firmware/pbf-custom` 執行：

```bash
python tests/run_regressions.py
```

## 版本紀錄

<details>
<summary>最近幾版（點開展開）</summary>

- **v0.11.9**：網頁新增英文介面，右上角 **English／中文** 切換整台喂竹器的語言：網頁、推播通知、設定錯誤訊息都跟著換。204 項桌面檢查，OTA 燒錄後設定全部保留，實機確認語言切換正常。
- **v0.11.8**：網頁設定裡比較容易設錯的 15 個欄位旁加上圓圈問號，點一下在該欄下方展開說明。
- **v0.11.7**：修正狀態訊息：印表機重新加熱或開始輔助進料時，換掉 `heater off` 等等候訊息（之前會整場列印都顯示舊訊息，送料不受影響）。v0.11.7、v0.11.8 都通過編譯及 202 項桌面檢查，OTA 燒錄後設定全部保留；訊息修正已在實機確認。
- **v0.11.6**：印表機噴頭目標溫度變成 0 後放鬆緩衝：把停駐的那段退回來，活動件一回到原位就停（最多退停駐量 + 6mm，不會拉擠出機裡的料），收尾和閒置時擠出機卡扣不再被緩衝拉著，下次加熱後的輔助進料會補回。193 項桌面檢查。實機確認：開機時（印表機閒置）退了 2.8mm；2026-10-21 列印收尾時退了 15.6mm，活動件回到原位；燒錄後 9 次列印都正常完成。
- **v0.11.5**：下鍵雙點「退到無料」的上限改成實測退料長度 + 20mm（原本 ×1.3），料頭退過齒輪後一定再退完 15mm 才停。170 項桌面檢查，OTA 燒錄後啟動正常、設定保留。
- **v0.11.4**：修正手動進退料、跟隨印表機進退料時雙點上鍵回 STAGED：第一下停止也算進雙擊，單按停止不會延遲觸發進料；跟隨動作須等印表機這一段進退料結束才重新允許。實體按鍵操作仍待實機確認。
- **v0.11.3**：修正測試結束時干擾列印、列印時進入選單、殘留 FREE 訊號及數字設定驗證問題。

</details>

## 支持

如果這個專案對你有幫助，歡迎請我喝杯咖啡 ☕

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/lornfall)

## 授權

MIT，見 [`LICENSE`](LICENSE)。第三方元件見 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
