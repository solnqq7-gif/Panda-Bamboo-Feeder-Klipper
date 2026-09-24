# 退料／進料 G-code 範例（通用版）

對象：任何 Klipper 印表機 + Panda Bamboo Feeder 自製韌體 v0.10.0 以上。
Creality K1 Max 的實測版本在 [`tip-forming.md`](tip-forming.md)。

**這份是示範 `M117 PBF:FREE`／`M117 PBF:FOLLOW` 的用法**，讓印表機退料、進料時跟喂竹器交握。
裡面的尖端成形動作只是範例，不是最佳參數，**請換成你印表機原本就能用的退料動作**
（原廠退料巨集、切片軟體的換料參數都可以），只要照下面的結構把 `M117` 放進去就好。

---

## 1. 結構

| 段落 | 放什麼 | 喂竹器在做什麼 |
|---|---|---|
| 開頭 | `M117 PBF:FREE` + `G4 P400` | 放開齒輪（FREE），讓印表機自己拉料，不跟尖端成形的來回擺動互相拉扯 |
| 尖端成形 | 你原本的成形動作 | 齒輪空轉 |
| 成形結束 | `M400` + `M117 PBF:FOLLOW` + `G4 P400` | 夾回齒輪，改成同步收料（FOLLOW） |
| 長回抽 | `G1 E-<距離>` | 跟著印表機同速倒轉，把料頭拉離擠出機齒輪 |
| 結尾 | `M400` + `M117` | 清掉訊息，回到一般狀態 |

幾個不能省的地方：

- **`G4 P400`**：喂竹器每 0.2 秒讀一次印表機，等兩個週期讓它看到訊息。
- **`PBF:FOLLOW` 前面的 `M400`**：Klipper 會先執行 `M117`，前面排隊的移動還沒做完。
  少了 `M400`，成形還在擺動，喂竹器就先夾回來了。
- **結尾的 `M117`**：`M117` 的訊息會一直留在畫面上，不清掉就一直是 FREE／FOLLOW。
  （喂竹器有 60 秒逾時保護，但不要依賴它。）
- **長回抽不要放在 FREE 裡**：FREE 時齒輪空轉，退回來的料在入料端沒有東西收，幾百 mm 會塞成一團。
  要退遠就先切 FOLLOW。

## 2. 退料範本（在控制台、Mobileraker、Mainsail／Fluidd 的巨集按鈕執行）

```gcode
; ==== 依你的機器修改 ====
M109 S230          ; 加熱到退料溫度並等待（PLA 約 215、PETG 約 230）。已經熱了可以刪掉
M83                ; 相對擠出

M117 PBF:FREE      ; 喂竹器放開齒輪
G4 P400            ; 等喂竹器看到訊息

; ==== 尖端成形（範例：和 K1 實測版相同的動作，換成你自己的也可以）====
G1 E10 F300        ; 先擠一點，讓料頭乾淨
G1 E-15 F3000      ; 快速回抽離開熔區
G1 E-22.47 F2400
G1 E-6.42 F1200
G1 E-3.21 F720
G1 E5 F356         ; 冷卻擺動 4 組：讓料頭在冷區邊動邊變硬
G1 E-5 F384
G1 E5 F412
G1 E-5 F440
G1 E5 F467
G1 E-5 F495
G1 E5 F523
G1 E-5 F3000
G1 E-15 F3000
; ==== 尖端成形結束 ====

M400               ; 必要：等成形真的做完
M117 PBF:FOLLOW    ; 喂竹器夾回來，改成同步收料
G4 P400
G1 E-60 F2000      ; 把料頭拉離擠出機齒輪，距離見第 4 節
M400
M117               ; 清除訊號
```

退完之後按喂竹器的**退料鍵**，把耗材拉回料盒。

## 3. 進料範本

料已經送到擠出機入口（喂竹器按上鍵送料、停在感測器之後），再讓擠出機把料吃進熱端：

```gcode
M109 S230          ; 加熱並等待
M83
M117 PBF:FOLLOW    ; 喂竹器跟著擠出機同步推料
G4 P400
G1 E100 F300       ; 擠出機把料拉進熱端並擠出一段，距離依你的機器
M400
M117
```

## 4. 最後那段回抽要多長

目標是**料頭離開擠出機的齒輪**，喂竹器才拉得回來（喂竹器拉不動還被擠出機夾著的料）。

| 擠出機型式 | 回抽距離（從噴嘴算起的料頭位置） |
|---|---|
| 近程（direct drive） | 噴嘴到擠出機齒輪約 50～100mm，再加 10～20mm 餘量 |
| 遠程（Bowden） | 噴嘴到擠出機齒輪的整段管長，再加 10～20mm 餘量（常常要幾百 mm） |

範本裡的料頭位置 = 成形段最後的位置 + 最後這段 `G1 E-` 的距離。
範例的成形段結束在約 62mm，加 `E-60` 後料頭約在 122mm。

**驗收**：退料完按一下喂竹器退料鍵 —— 拉得動 = 料頭已經離開擠出機齒輪；打滑磨料 = 還被夾著，把距離加大。

## 5. 做成 `printer.cfg` 巨集（選配）

願意改印表機設定的話，可以包成一個巨集，之後只要下 `PBF_UNLOAD`：

```ini
[gcode_macro PBF_UNLOAD]
description: 退料並通知 Panda Bamboo Feeder
gcode:
  {% set temp = params.TEMP|default(230)|int %}
  {% set dist = params.DIST|default(60)|float %}
  M109 S{temp}
  SAVE_GCODE_STATE NAME=pbf_unload
  M83
  M117 PBF:FREE
  G4 P400
  G1 E10 F300
  G1 E-15 F3000
  G1 E-22.47 F2400
  G1 E-6.42 F1200
  G1 E-3.21 F720
  G1 E5 F356
  G1 E-5 F384
  G1 E5 F412
  G1 E-5 F440
  G1 E5 F467
  G1 E-5 F495
  G1 E5 F523
  G1 E-5 F3000
  G1 E-15 F3000
  M400
  M117 PBF:FOLLOW
  G4 P400
  G1 E-{dist} F2000
  M400
  M117
  RESTORE_GCODE_STATE NAME=pbf_unload
```

用法：`PBF_UNLOAD TEMP=215 DIST=80`。
**不要**把巨集取名成印表機已經有的 `UNLOAD_FILAMENT`、`LOAD_FILAMENT` 之類的名字，會互相覆蓋。

## 6. 印表機沒有 `[display_status]` 的時候

`M117` 要有 `[display_status]` 才能用（Mainsail、Fluidd 的設定檔都內建）。
沒有的話 `M117` 會報 `Unknown command`，改用備用管道：在 `printer.cfg` 加一個只存變數的巨集

```ini
[gcode_macro PBF]
variable_mode: 0
gcode:
  # 只用來存變數，不需要內容
```

然後把範本裡的 `M117 PBF:FREE`／`M117 PBF:FOLLOW`／`M117` 分別換成

```gcode
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=1   ; FREE
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=2   ; FOLLOW
SET_GCODE_VARIABLE MACRO=PBF VARIABLE=mode VALUE=0   ; 清除
```

喂竹器兩種管道都讀，哪一個有訊號就用哪一個。

## 7. 其他注意

- **正式列印中不會執行 FOLLOW**（避免訊號忘了清掉，整場列印一直送料）。在列印檔中間換料要先暫停，
  `M600` 通常本來就會先 `PAUSE`。
- 不用任何訊號也行：喂竹器看到非列印中的回抽累計超過 25mm（網頁可改），會自動改成放開齒輪。
  但有訊號比較準，建議加上。
- 這份範本的成形段只在 K1 上測過。料頭會不會卡，只能實際退料後用卡尺量
  （方法見 [`tip-forming.md`](tip-forming.md) 第 4 節）。
