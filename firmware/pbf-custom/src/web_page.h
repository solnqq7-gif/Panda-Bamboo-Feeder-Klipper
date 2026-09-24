// Web UI served at "/". Polls /api/status and /api/log.
#pragma once

#include <pgmspace.h>

const char kIndexHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-Hant"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PBF 喂竹器</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#667085;--acc:#2f7d4f;--warn:#b42318;--bd:#e3e6ea;--orange:#e07b12;
--danger:#b42318;--g-motor:#ecf6ef;--g-speed:#fcefed;--g-screen:#edf2fb;--g-prn:#fbf5e6}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1a1f26;--fg:#e8eaed;--mut:#98a2b3;--acc:#3fa86a;--warn:#f97066;--bd:#2a313b;
--danger:#c8322a;--g-motor:#17271e;--g-speed:#2c1b1c;--g-screen:#18212f;--g-prn:#2a2416}}
*{box-sizing:border-box}
body{margin:0;font:15px/1.45 system-ui,-apple-system,"Noto Sans TC","PingFang TC",sans-serif;background:var(--bg);color:var(--fg)}
main{max-width:720px;margin:0 auto;padding:12px 16px 40px}
h1{font-size:18px;margin:4px 0 12px}
h2{font-size:14px;margin:0 0 10px;color:var(--mut);font-weight:600}
.card{background:var(--card);border:1px solid var(--bd);border-radius:10px;padding:14px;margin-bottom:12px}
#state{font-size:26px;font-weight:700}
#msg{color:var(--mut);min-height:1.4em}
.sens{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.pill{border:1px solid var(--bd);border-radius:99px;padding:3px 10px;font-size:13px}
.pill.on{background:var(--acc);color:#fff;border-color:var(--acc)}
.btns{display:grid;grid-template-columns:repeat(auto-fill,minmax(110px,1fr));gap:8px}
button{font:inherit;padding:10px;border-radius:8px;border:1px solid var(--bd);background:var(--card);color:var(--fg);cursor:pointer}
button.pri{background:var(--acc);color:#fff;border-color:var(--acc)}
button.warn{border-color:var(--warn);color:var(--warn)}
button.orange{background:var(--orange);color:#fff;border-color:var(--orange)}
button.danger{background:var(--danger);color:#fff;border-color:var(--danger)}
.btns.two{grid-template-columns:1fr 1fr}
button.hold{padding:18px 10px;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;touch-action:none}
button.hold.active{outline:3px solid var(--fg)}
button:disabled{opacity:.4;cursor:not-allowed}
h3{font-size:13px;margin:14px 0 2px;color:var(--mut)}
#lockmsg{color:var(--warn);font-weight:600}
.dist{display:flex;gap:8px;align-items:center;margin-top:8px}
.dist button{flex:1}
.dist input{width:90px}
label{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:7px 0;border-bottom:1px solid var(--bd)}
input,select{font:inherit;padding:6px;border-radius:6px;border:1px solid var(--bd);background:var(--bg);color:var(--fg);width:130px}
input[type=checkbox]{width:auto;transform:scale(1.2)}
pre{background:var(--bg);border-radius:8px;padding:8px;height:260px;overflow:auto;font-size:12px;margin:8px 0 0;white-space:pre-wrap;word-break:break-all}
.grp{border-radius:8px;padding:2px 10px 4px;margin:0 0 10px}
.grp h3{margin:8px 0 0}
.grp label:last-child{border-bottom:0}
.grp input,.grp select{background:var(--card)}
.grp.motor{background:var(--g-motor)}.grp.speed{background:var(--g-speed)}
.grp.screen{background:var(--g-screen)}.grp.prn{background:var(--g-prn)}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.mut{color:var(--mut);font-size:13px}
.tests{display:grid;gap:8px}
button.test{text-align:left;display:flex;flex-direction:column;gap:2px}
button.test span{font-size:13px;color:var(--mut)}
button.test.run{border-color:var(--warn);color:var(--warn)}
#tprog{margin-top:8px;font-weight:600;min-height:1.4em}
#tres{margin-top:4px}
.res-ok{color:var(--acc);font-weight:700}
.res-bad{color:var(--warn);font-weight:700}
</style></head><body><main>
<h1>Panda Bamboo Feeder <span class="mut" id="fw"></span></h1>

<div class="card">
  <div id="state">連線中…</div>
  <div id="msg"></div>
  <div class="sens">
    <span class="pill" id="s_in1">入料口</span><span class="pill" id="s_in2">齒輪</span>
    <span class="pill" id="s_buf">緩衝</span><span class="pill" id="s_drv">馬達電源24V</span><span class="pill" id="s_k1">%P</span>
  </div>
  <div class="mut" id="info" style="margin-top:8px"></div>
</div>

<div class="card"><h2>統計（存在 flash）</h2>
  <div id="stats" class="mut">讀取中…</div>
  <div class="row" style="margin-top:8px">
    <button class="danger" data-cmd="stats_clear" data-confirm="清除所有統計？這個動作無法復原。">清除統計</button>
    <button id="logbtn" type="button" aria-controls="log" style="margin-left:auto"></button>
  </div>
  <pre id="log" hidden></pre>
</div>

<div class="card"><h2>操作</h2>
  <div id="lockmsg"></div>
  <div class="btns two">
    <button class="pri hold lockable" id="hold_feed">按住進料</button>
    <button class="orange hold lockable" id="hold_back">按住退料</button>
  </div>
  <div class="dist">
    <button class="pri lockable" id="dist_feed">距離進料</button>
    <input type="number" id="load_mm" min="1" max="10000"><span>mm</span>
  </div>
  <div class="dist">
    <button class="orange lockable" id="dist_back">距離退料</button>
    <input type="number" id="unload_mm" min="1" max="10000"><span>mm</span>
  </div>
  <div class="btns" style="margin-top:8px">
    <button class="warn" data-cmd="stop">停止／解除警報</button>
    <button id="freebtn" data-cmd="free">放開齒輪</button>
    <button data-cmd="beep">測試蜂鳴器</button>
  </div>
  <div id="actmsg" style="min-height:1.4em;margin-top:8px;font-weight:600"></div>
  <p class="mut">路徑長度校準預設是自動的；勾選「鎖定進退料距離」後，完整進退料都只用你設定的距離，不再自動改寫。<br>
  放開齒輪：馬達斷電讓齒輪空轉，印表機自己把料拉出來（尖端成形用）。逾時或按任一鍵就會夾回來。
  退料 G-code 裡放 <code>M117 PBF:FREE</code> 就會自動放開，結尾 <code>M117</code> 清掉就夾回來
  —— FREE 觸發後 60 秒內不會自動輔助進料；若 %P 開始正式列印，會立刻取消保護並恢復輔助進料。
  進料或長距離回抽前可放 <code>M117 PBF:FOLLOW</code>，讓兩邊雙向同步；
  大小寫皆可。沒有 FREE 訊號時，跟隨回抽累計超過設定長度也會改成放開。</p>
</div>

<div class="card"><h2>安裝測試</h2>
  <p class="mut">開始前：耗材已經送到 %P 擠出機並被夾住（有用耗材感測器的話要顯示有料）、%P 沒在列印、喂竹器在待命或就緒。
  結果會自動存進設定。測試中按喂竹器任一鍵、或再按一次同一個按鈕就會中止。</p>
  <div class="tests">
    <button class="test lockable" data-test="fulltest" data-kind="full"><b>綜合測試</b>
      <span>第一次安裝用這個：先跑緩衝量測試，再用新的停駐量跑壓力測試，最後給一份總結。約 4～8 分鐘，會加熱噴頭、用掉約 30cm 耗材</span></button>
    <button class="test lockable" data-test="btest" data-kind="buffer"><b>緩衝量測試</b>
      <span>把活動件推到底量行程，自動設定停駐量（外側留 5mm 吸收回抽）。約 20 秒，不花耗材、不用加熱</span></button>
    <button class="test lockable" data-test="ptest" data-kind="pressure"><b>壓力測試</b>
      <span>找出推得動活動件、待機夾得住耗材的最低電流（待機依 60、90、120、150、180、200、250mA… 往上找）。推料加 1 級、夾持加 2 格餘裕。約 4～8 分鐘，會加熱噴頭、用掉約 30cm 耗材</span></button>
  </div>
  <div id="tprog"></div>
  <div id="tres"></div>
</div>

<div class="card"><h2>設定</h2>
<form id="cfg">
  <p class="mut" style="margin:0 0 8px">兩個電流級別選單改了就立即套用；其他欄位要按最下面的「儲存設定」。</p>
  <div class="grp motor"><h3>馬達與緩衝</h3>
  <label>壓力級別<select name="pressure"></select></label>
  <label>進退料電流級別<select name="move_level"></select></label>
  <label>待機夾持電流 (mA)<input type="number" name="hold_ma" min="50" max="700"></label>
  <label>緩衝停駐量 (mm)<input type="number" name="overfeed_mm" min="0" max="25"></label>
  <label>卡料判定長度 (mm)<input type="number" name="stuck_mm" min="30" max="500"></label>
  <label>預載長度 (mm)<input type="number" name="preload_mm" min="0" max="300"></label>
  <label>空閒時放鬆馬達<input type="checkbox" name="idle_release"></label>
  <label>%P 閒置時省電待機（放開齒輪）<input type="checkbox" name="standby"></label>
  <label>閒置多久進入省電待機 (分鐘)<input type="number" name="standby_min" min="1" max="120"></label>
  <label>馬達方向反轉<input type="checkbox" name="invert_dir"></label>
  <label>每 mm 步數<input type="number" name="steps_mm" step="0.1" min="50" max="1000"></label>
  </div>
  <div class="grp speed"><h3>速度</h3>
  <label>輔助進料速度 (mm/s)<input type="number" name="buffer_speed" min="5" max="60"></label>
  <label>手動送料速度 (mm/s)<input type="number" name="load_speed" min="5" max="60"></label>
  <label>退料速度 (mm/s)<input type="number" name="unload_speed" min="5" max="60"></label>
  </div>
  <div class="grp screen"><h3>螢幕與蜂鳴器</h3>
  <label>螢幕亮度：一般 (%)<input type="number" name="brightness" min="5" max="100"></label>
  <label>螢幕亮度：待命 READY (%)<input type="number" name="brightness_ready" min="0" max="100"></label>
  <label>螢幕亮度：輔助進料時 (%)<input type="number" name="brightness_active" min="5" max="100"></label>
  <label>蜂鳴器<input type="checkbox" name="buzzer"></label>
  </div>
  <div class="grp prn"><h3>印表機連線（Klipper / Moonraker）</h3>
  <label>啟用<input type="checkbox" name="k1_enabled"></label>
  <label>印表機名稱（英數，最多 12 字）<input name="printer_name" maxlength="12" autocomplete="off"></label>
  <label>%P 位址（IP 或主機名稱）<input name="printer_host" autocomplete="off"></label>
  <label>連接埠（Moonraker，通常 7125）<input type="number" name="printer_port" min="1" max="65535"></label>
  <label>API key（Moonraker 拒絕連線時才需要）<input name="api_key" type="password" autocomplete="new-password"></label>
  <div class="row" id="keyrow" style="margin:6px 0" hidden><button type="button" id="keyclr">清除 API key</button><span class="mut">留空儲存＝不變</span></div>
  <label>耗材感測器（判斷料到擠出機）<select name="sensor"></select></label>
  <label>列印中鎖定按鍵與網頁進退料<input type="checkbox" name="lock_printing"></label>
  <label>卡料時暫停 %P 並推播<input type="checkbox" name="pause_on_stuck"></label>
  <label>進料到擠出機自動停（耗材感測器）<input type="checkbox" name="stop_at_extruder"></label>
  <label>感測器偵測到料後再送 (mm，最多 5000)<input type="number" name="extruder_extra_mm" min="0" max="5000"></label>
  <label>路徑校準餘裕 (mm)<input type="number" name="path_margin_mm" min="0" max="250"></label>
  <label>鎖定進退料距離（禁止自動校準）<input type="checkbox" name="lock_move_distance"></label>
  <label>壓力測試噴頭溫度 (C)<input type="number" name="ptest_temp" min="150" max="300"></label>
  <label>%P 回抽時同步倒轉（非列印中）<input type="checkbox" name="retract_sync"></label>
  <label>%P 退料時放開齒輪（M117 PBF:FREE）<input type="checkbox" name="free_on_unload"></label>
  <label>放開齒輪逾時 (秒)<input type="number" name="free_timeout_s" min="5" max="600"></label>
  <label>沒有訊號時靠回抽長度判斷<input type="checkbox" name="free_on_retract"></label>
  <label>判定為退料的回抽長度 (mm)<input type="number" name="free_retract_mm" min="10" max="200"></label>
  <label>耗材用完時推播<input type="checkbox" name="notify_runout"></label>
  <div class="row" style="margin:8px 0 6px"><button type="button" data-cmd="notify_test">測試推播</button><span class="mut">需要 Mobileraker Companion</span></div>
  </div>
  <div class="row" style="margin-top:10px"><button class="pri" type="submit">儲存設定</button><span class="mut" id="cfgmsg"></span></div>
</form></div>

<div class="card"><h2>WiFi</h2>
<form id="wifi">
  <label>名稱 (SSID)<input name="ssid" autocomplete="off"></label>
  <label>密碼<input name="pass" type="password" autocomplete="new-password"></label>
  <div class="row" style="margin-top:10px"><button type="submit">儲存並重新開機</button></div>
</form></div>

<div class="card"><h2>韌體更新</h2>
  <div class="row"><input type="file" id="fwfile" accept=".bin" style="width:auto"><button id="fwbtn">上傳</button></div>
  <div class="mut" id="fwmsg">選擇 firmware.bin 上傳，完成後會自動重新開機。</div>
</div>
</main>
<script>
const $=s=>document.querySelector(s);
// The printer's name from the settings replaces every %P on the page.
let PN='Printer';
const P=t=>t.replace(/%P/g,PN);
const tpl=[];{const w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let n;
  while(n=w.nextNode())if(n.nodeValue.includes('%P')&&n.parentNode.nodeName!=='SCRIPT')tpl.push([n,n.nodeValue])}
function applyName(){tpl.forEach(([n,t])=>n.nodeValue=P(t))}
applyName();
const NAMES={"NO POWER":"馬達沒有電源（24V？）","EMPTY":"無料","INSERTING":"咬料中","PUSH IN":"請把耗材再推進去",
"PRELOAD":"預載中","STAGED":"就緒（緩衝暫停，可換管）","LOADING":"送料中","READY":"待命（緩衝啟動）","FEEDING":"輔助進料中","STANDBY":"省電待機（齒輪放開）",
"RUNOUT":"斷料：交給 %P 處理","UNLOADING":"退料中","UNLOADED":"已退料","STUCK!":"卡料！","ERROR":"錯誤",
"FOLLOW":"同步 %P 進退料中","FREE":"齒輪放開（印表機自己拉）"};
const K1NAMES={"printing":"列印中","paused":"暫停","standby":"閒置","complete":"完成","cancelled":"已取消","error":"錯誤","off":"未啟用","not connected":"未連線","unauthorized":"被拒絕（要 API key）"};
async function get(p){const r=await fetch(p,{cache:'no-store'});return r.json()}
async function post(p,b){const r=await fetch(p,{method:'POST',body:new URLSearchParams(b)});return r.json()}
const mmss=t=>Math.floor(t/60)+':'+String(t%60).padStart(2,'0');
function pill(id,on,txt){const e=$(id);e.classList.toggle('on',!!on);if(txt)e.textContent=txt}
async function refresh(){
  try{
    const s=await get('/api/status');
    $('#state').textContent=P(NAMES[s.state]||s.state);
    $('#state').style.color=s.alarm?'var(--warn)':'';
    $('#msg').textContent=s.msg+(s.menu?'（喂竹器正在設定選單中）':'');
    $('#fw').textContent='v'+s.fw;
    pill('#s_in1',s.in1,s.in1?'入料口：有料':'入料口：無料');pill('#s_in2',s.in2,s.in2?'齒輪：已咬料':'齒輪：無料');
    pill('#s_buf',s.buf_full,'緩衝：'+(s.buf_rest?'原位':'撐開')+(s.buf_full?'・有存料':'・空'));
    pill('#s_drv',s.driver,s.driver?'馬達電源24V':'馬達電源24V：沒電');
    const k1=K1NAMES[s.k1]||s.k1;pill('#s_k1',s.k1!=='not connected'&&s.k1!=='off',PN+'：'+k1+(s.k1_toolhead?'・感測器有料':''));
    const sk=JSON.stringify(s.sensors||{});
    if(sk!==sensorsKey&&document.activeElement!==$('#cfg').sensor){sensorsKey=sk;sensors=s.sensors||{};fillSensors()}
    const fb=$('#freebtn');fb.textContent=s.free?'夾住齒輪':'放開齒輪';fb.dataset.cmd=s.free?'grip':'free';
    fb.className=s.free?'orange':'';fb.disabled=!s.free&&!s.can_free;
    document.querySelectorAll('.lockable').forEach(b=>b.disabled=!!s.locked);
    showTests(s);
    $('#lockmsg').textContent=P(s.lock_offline?'%P 連線中斷（最後狀態：列印中）：進退料維持鎖定，%P 回報沒在列印才解鎖（停止仍可使用）'
      :s.locked?'%P 列印中：進退料操作已鎖定（停止仍可使用）':'');
    $('#info').textContent=`${s.moving?'馬達運轉中・':''}本次移動 ${s.move_mm}mm・壓力 L${s.pressure}（${s.current_ma}mA）・位置 ${s.pos}mm`
      +`・退料訊號 ${s.free?({marker:'M117 放開',retract:'回抽判定 放開',manual:'手動 放開'})[s.free_src]:(s.k1_pbf_mode===2?'M117 同步':(s.k1_pbf_display||s.k1_pbf_macro?'待命':'無法讀取'))}`
      +(s.topup_block_s?`・輔助進料保護 ${s.topup_block_s}s`:'')
      +(s.state==='STANDBY'?`・${PN} 加熱、列印或按任一鍵就會喚醒`:s.standby_in_s>=0?`・${mmss(s.standby_in_s)} 後省電待機`:'')
      +`・WiFi ${s.wifi_mode} ${s.ip}${s.rssi?'（'+s.rssi+' dBm）':''}`;
    if(s.stats){const t=s.stats;$('#stats').innerHTML=
      `送出 <b>${t.fed_m}</b> m・退回 <b>${t.pulled_m}</b> m・輔助進料 <b>${t.top_ups}</b> 次<br>`+
      `卡料警報 <b>${t.stuck_alarms}</b>・退料卡住 <b>${t.unload_jams}</b>・UART 干擾 <b>${t.uart_hiccups}</b>・通電 <b>${t.powered_h}</b> 小時`+
      (t.stored?'':'<br><span style="color:var(--warn)">無法寫入 flash，只計算本次開機</span>')}
  }catch(e){$('#msg').textContent='連線中斷，重試中…'}
}
const TKIND={full:'綜合測試',buffer:'緩衝量測試',pressure:'壓力測試'};
const TCODE={cancelled:'已中止',no_power:'馬達沒有 24V 電源',no_filament:'喂竹器沒有耗材：先插料並送到 %P',
busy:'喂竹器正在動作，等它回到待命再按',k1_link:'連不到 %P：檢查設定裡的位址和連接埠',k1_printing:'%P 列印中不能測試',
k1_empty:'%P 的耗材感測器沒有料：先把耗材送到 %P 擠出機',stopped:'測試被中斷（錯誤或馬達斷電）',
drain_failed:'無法把緩衝退回原位：檢查活動件是否卡住',
never_left_home:'送了 30mm 活動件都沒被撐開：耗材可能沒被 %P 擠出機夾住，或管子沒接好',
no_end:'找不到行程終點：梳齒感測器可能沒有訊號',inconsistent:'兩次量測差太多：把管子固定好再測一次',
short_travel:'行程太短：檢查鐵氟龍管是否太短或彎得太急',
park_weak:'目前的壓力級別推不到新的停駐位置，請接著跑壓力測試（或直接用綜合測試）',
push_failed:'所有壓力級別都推不動活動件：檢查路徑是否卡住',
hold_failed:'待機電流擋不住印表機的拉力：檢查齒輪是否咬緊耗材',heat_failed:'噴頭沒有加熱到設定溫度'};
let resSeq=-1;
function showTests(s){
  document.querySelectorAll('button.test').forEach(b=>{
    const run=s.test===b.dataset.kind;b.classList.toggle('run',run);
    b.querySelector('b').textContent=(run?'中止':'')+TKIND[b.dataset.kind];
    if(s.test&&!run)b.disabled=true;
  });
  $('#tprog').textContent=s.test?TKIND[s.test]+'進行中：'+s.msg:'';
  const r=s.result;if(!r||!r.kind||!r.seq){$('#tres').innerHTML='';return}
  if(resSeq>=0&&r.seq!==resSeq)loadCfg();  // the test saved new settings
  resSeq=r.seq;
  const L=[];
  if(r.travel_mm>0)L.push(`活動件行程 <b>${r.travel_mm}</b>mm（離開原位前 ${r.home_mm}mm）→ 停駐量 ${r.overfeed_old} → <b>${r.overfeed}</b>mm，`+
    `外側保留 ${(r.travel_mm-r.overfeed).toFixed(1)}mm 吸收回抽，每次輔助進料約 ${(r.home_mm+r.overfeed).toFixed(0)}mm`);
  if(r.pressure)L.push(`壓力級別 <b>L${r.pressure}</b>（${r.pressure_ma}mA）`+(r.hold_ma?`・待機夾持 <b>${r.hold_ma}</b>mA`:''));
  if(r.idle_release_fixed)L.push('已關閉「空閒時放鬆馬達」：緩衝要齒輪夾住才會運作');
  const why=P(TCODE[r.code]||r.code);
  let head=r.ok?`<span class="res-ok">✓ ${TKIND[r.kind]}通過</span>`:`<span class="res-bad">✗ ${TKIND[r.kind]}未通過：${why}</span>`;
  if(r.ok&&r.code)L.push(`<span class="res-bad">注意：${why}</span>`);
  $('#tres').innerHTML=head+(L.length?'<br>'+L.join('<br>'):'')+`<div class="mut">${r.detail.replace(/</g,'&lt;')}</div>`;
}
document.querySelectorAll('button.test').forEach(b=>b.onclick=async()=>{
  if(!b.classList.contains('run')&&!confirm(P('開始'+TKIND[b.dataset.kind]+'？耗材要已經送到 %P 擠出機，%P 不能在列印。')))return;
  try{await post('/api/action',{cmd:b.dataset.test});}catch(e){actMsg('送出失敗：連線中斷')}
  refresh();
});
let seq=0;
async function pollLog(){
  try{
    const r=await get('/api/log?since='+seq);seq=r.next;
    if(!r.lines.length)return;
    const pre=$('#log');const atEnd=pre.scrollTop+pre.clientHeight>=pre.scrollHeight-20;
    let t=pre.textContent+r.lines.join('\n')+'\n';const L=t.split('\n');
    if(L.length>500)t=L.slice(-500).join('\n');
    pre.textContent=t;if(atEnd)pre.scrollTop=pre.scrollHeight;
  }catch(e){}
}
let actTimer;
function actMsg(t){$('#actmsg').textContent=t;clearTimeout(actTimer);actTimer=setTimeout(()=>$('#actmsg').textContent='',2500)}
document.querySelectorAll('[data-cmd]').forEach(b=>b.onclick=async()=>{
  if(b.dataset.confirm&&!confirm(b.dataset.confirm))return;
  try{const r=await post('/api/action',{cmd:b.dataset.cmd,mm:b.dataset.mm||0});actMsg((r.ok?'已送出：':'失敗：')+b.textContent)}
  catch(e){actMsg('送出失敗：連線中斷')}
  refresh();
});
const lvl=i=>`L${i}（${250+(i-1)*50}mA）`;
function fillLevels(sel,zero){
  sel.innerHTML='';if(zero)sel.add(new Option(zero,0));
  for(let i=1;i<=10;i++)sel.add(new Option(lvl(i),i));
}
// Sensor choices come from the printer (status "sensors"); "" = pick automatically.
let sensors={},sensorsKey='';
function fillSensors(cur){
  const sel=$('#cfg').sensor,found=sensors.found||[];let keep=cur!==undefined?cur:sel.value;
  if(keep.includes(' '))keep=keep.slice(keep.indexOf(' ')+1);  // "filament_switch_sensor x" -> "x"
  sel.innerHTML='';
  sel.add(new Option('自動'+(sensors.active?'（目前：'+sensors.active+'）':'（沒找到感測器）'),''));
  found.forEach(n=>sel.add(new Option(n,n)));
  if(keep&&keep!=='none'&&!found.includes(keep))sel.add(new Option(keep+'（印表機沒有這個）',keep));
  sel.add(new Option('不使用感測器','none'));
  sel.value=keep;
}
async function loadCfg(){
  const c=await get('/api/settings');const f=$('#cfg');
  fillLevels(f.pressure);fillLevels(f.move_level,'同壓力級別');fillSensors(c.sensor||'');
  PN=c.printer_name||'Printer';applyName();
  for(const el of f.elements){if(!el.name||el.name==='sensor'||el.name==='api_key')continue;if(el.type==='checkbox')el.checked=!!c[el.name];else el.value=c[el.name]}
  // The key is never sent back: show only whether one is stored.
  f.api_key.value='';f.api_key.placeholder=c.api_key_set?'已設定':'未設定';$('#keyrow').hidden=!c.api_key_set;
  $('#wifi').ssid.value=c.wifi_ssid||'';
  $('#load_mm').value=c.load_mm;$('#unload_mm').value=c.unload_mm;
}
// Hold buttons: the feeder stops by itself if the keep-alive stops arriving (e.g. WiFi drops).
function holdButton(id,dir){
  const b=$(id);let t=null;
  const send=()=>post('/api/action',{cmd:'hold',dir}).catch(()=>{});
  const start=e=>{e.preventDefault();if(t)return;if(b.setPointerCapture)b.setPointerCapture(e.pointerId);
    b.classList.add('active');send();t=setInterval(send,250);actMsg(b.textContent+'中…')};
  const stop=()=>{if(!t)return;clearInterval(t);t=null;b.classList.remove('active');
    post('/api/action',{cmd:'hold_stop'}).then(refresh).catch(()=>{});actMsg('已放開')};
  b.addEventListener('pointerdown',start);
  ['pointerup','pointercancel','lostpointercapture'].forEach(ev=>b.addEventListener(ev,stop));
  b.addEventListener('contextmenu',e=>e.preventDefault());
}
holdButton('#hold_feed','feed');holdButton('#hold_back','retract');
document.addEventListener('visibilitychange',()=>{if(document.hidden)post('/api/action',{cmd:'hold_stop'}).catch(()=>{})});
async function distAction(cmd,input,label){
  const mm=$(input).value;
  try{const r=await post('/api/action',{cmd,mm});actMsg((r.ok?'已送出：':'失敗：')+label+' '+mm+'mm')}
  catch(e){actMsg('送出失敗：連線中斷')}
  refresh();
}
$('#dist_feed').onclick=()=>distAction('feed_dist','#load_mm','距離進料');
$('#dist_back').onclick=()=>distAction('unload_dist','#unload_mm','距離退料');
$('#cfg').pressure.onchange=()=>post('/api/settings',{pressure:$('#cfg').pressure.value}).then(()=>{actMsg('壓力級別已套用 L'+$('#cfg').pressure.value);cfgMsg('壓力級別已套用');refresh()});
$('#cfg').move_level.onchange=()=>post('/api/settings',{move_level:$('#cfg').move_level.value})
  .then(()=>cfgMsg('進退料電流級別已套用'));
$('#keyclr').onclick=async()=>{if(!confirm('清除 API key？'))return;
  await post('/api/settings',{api_key:''});cfgMsg('API key 已清除');loadCfg()};
function cfgMsg(t){$('#cfgmsg').textContent=t;setTimeout(()=>$('#cfgmsg').textContent='',2000)}
function setLog(open){
  const p=$('#log'),b=$('#logbtn');p.hidden=!open;b.textContent=open?'摺疊記錄 ▴':'展開記錄 ▾';
  b.setAttribute('aria-expanded',open);if(open)p.scrollTop=p.scrollHeight;
  try{localStorage.setItem('pbf_log_open',open?'1':'0')}catch(e){}
}
$('#logbtn').onclick=()=>setLog($('#log').hidden);
{let open=false;try{open=localStorage.getItem('pbf_log_open')==='1'}catch(e){}setLog(open)}
$('#load_mm').onchange=()=>post('/api/settings',{load_mm:$('#load_mm').value}).then(()=>actMsg('進料距離已儲存'));
$('#unload_mm').onchange=()=>post('/api/settings',{unload_mm:$('#unload_mm').value}).then(()=>actMsg('退料距離已儲存'));
$('#cfg').onsubmit=async e=>{
  e.preventDefault();const b={};
  for(const el of e.target.elements){if(!el.name)continue;b[el.name]=el.type==='checkbox'?(el.checked?1:0):el.value}
  if(!b.api_key)delete b.api_key;  // empty = keep the stored key
  await post('/api/settings',b);cfgMsg('已儲存');loadCfg();refresh();
};
$('#wifi').onsubmit=async e=>{
  e.preventDefault();if(!confirm('儲存 WiFi 設定並重新開機？'))return;
  await post('/api/wifi',{ssid:e.target.ssid.value,pass:e.target.pass.value});$('#msg').textContent='重新開機中…';
};
$('#fwbtn').onclick=()=>{
  const f=$('#fwfile').files[0];if(!f)return;if(!confirm('上傳 '+f.name+' 並重新開機？'))return;
  const x=new XMLHttpRequest();
  x.upload.onprogress=e=>$('#fwmsg').textContent='上傳中 '+Math.round(e.loaded/e.total*100)+'%';
  x.onload=()=>$('#fwmsg').textContent=x.responseText==='OK'?'完成，重新開機中…':'失敗：'+x.responseText;
  x.onerror=()=>$('#fwmsg').textContent='上傳失敗';
  const d=new FormData();d.append('firmware',f);x.open('POST','/update');x.send(d);
};
loadCfg();refresh();pollLog();setInterval(refresh,700);setInterval(pollLog,1000);
</script></body></html>)HTML";
