#pragma once
// FieldMesh repeater: the WiFi dashboard page (P5e), served as one string from flash.
// Plain HTML/CSS/JS, no libraries (works without internet). The page never reloads itself:
// it polls the JSON snapshots and only replaces the parts whose data changed, so open rows,
// the radar zoom and half-typed text stay as they are.
//
// APIs (FieldMeshWeb.cpp): /api/status, /api/board, /api/nodes, /api/network (GET),
// /api/post, /api/time, /api/login, /api/logout, /api/admin (POST), /api/admin?id= (GET).

static const char FM_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>FieldMesh</title>
<style>
:root{--bg:#0F1412;--surface:#171E1A;--border:#243029;--border2:#33413A;--text:#E6EBE6;--muted:#9AA59C;--text2:#C9D2CB;--accent:#F2B544;--accent-text:#F2B544;--on-accent:#1B1405;--nav:#141B17;--radar:#121A15;--ring:#2C3A32;--line:#22302A;--bar-rx:#5E7D6B;--bar:#8FB3A0;--live:#6FD3A0;--stale:#7E8A82;--star-off:#5C6A62;--sos-banner:#3A1714;--sos-strong:#C8382D;--on-sos:#FFFFFF;--sos-text:#FFE3DF;--sos-card:#2A1412;--sos-border:#7A2A22;--sos-muted:#F0B3AB;--sos-author:#FF8A7F;--sos-dot:#FF6B5E;--warn-bg:#2A2312;--warn-border:#6B5521;--warn-title:#F2D08A;--warn-text:#E9DDBE;color-scheme:dark}
@media (prefers-color-scheme:light){:root:not([data-theme=dark]){--bg:#F3F5F2;--surface:#FFFFFF;--border:#D9E0DA;--border2:#BCC6BF;--text:#16201A;--muted:#56625A;--text2:#34413A;--accent-text:#8A5A00;--nav:#FFFFFF;--radar:#EAF0EB;--ring:#BCC6BF;--line:#D3DBD5;--bar-rx:#7FA08C;--bar:#5F8F76;--live:#1F9D60;--stale:#8A968E;--star-off:#AEB9B1;--sos-banner:#FBE1DD;--sos-text:#4A0F0B;--sos-card:#FDEDEA;--sos-border:#E0796E;--sos-muted:#8A3A32;--sos-author:#B3261E;--sos-dot:#D93A2B;--warn-bg:#FFF4D6;--warn-border:#D9AE45;--warn-title:#6B4A00;--warn-text:#4A3A10;color-scheme:light}}
:root[data-theme=light]{--bg:#F3F5F2;--surface:#FFFFFF;--border:#D9E0DA;--border2:#BCC6BF;--text:#16201A;--muted:#56625A;--text2:#34413A;--accent-text:#8A5A00;--nav:#FFFFFF;--radar:#EAF0EB;--ring:#BCC6BF;--line:#D3DBD5;--bar-rx:#7FA08C;--bar:#5F8F76;--live:#1F9D60;--stale:#8A968E;--star-off:#AEB9B1;--sos-banner:#FBE1DD;--sos-text:#4A0F0B;--sos-card:#FDEDEA;--sos-border:#E0796E;--sos-muted:#8A3A32;--sos-author:#B3261E;--sos-dot:#D93A2B;--warn-bg:#FFF4D6;--warn-border:#D9AE45;--warn-title:#6B4A00;--warn-text:#4A3A10;color-scheme:light}
*{box-sizing:border-box}
html,body{margin:0;background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;-webkit-text-size-adjust:100%}
[hidden]{display:none!important}
.app{max-width:560px;margin:0 auto;min-height:100vh;display:flex;flex-direction:column}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 16px 12px;border-bottom:1px solid var(--border)}
.hl{display:flex;flex-direction:column;gap:2px;min-width:0}
.hr{display:flex;flex-direction:column;align-items:flex-end;gap:4px;flex-shrink:0}
.kick{font-size:12px;letter-spacing:.08em;text-transform:uppercase;color:var(--muted)}
h1{margin:0;font-size:22px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
h2{margin:0;font-size:16px}
.badge{font-size:12px;font-weight:800;letter-spacing:.06em;padding:4px 8px;border-radius:6px;background:var(--border);color:var(--text)}
.badge.og{background:var(--accent);color:var(--on-accent)}
.mono{font-family:ui-monospace,Menlo,monospace}
.small{font-size:12px;color:var(--muted)}
.hint{margin:0;font-size:13px;color:var(--muted)}
.live{display:flex;align-items:center;gap:5px;font-size:12px;color:var(--muted)}
.live i{width:8px;height:8px;border-radius:4px;background:var(--live)}
.live.off{color:var(--sos-author)}.live.off i{background:var(--sos-dot)}
.offline{margin:12px 16px 0;padding:10px 12px;border-radius:10px;background:var(--sos-card);border:1px solid var(--sos-border);color:var(--sos-text);font-size:14px}
main{flex-grow:1;padding:16px 16px 24px}
.tab{display:flex;flex-direction:column;gap:16px}
.card{padding:12px;border-radius:12px;background:var(--surface);border:1px solid var(--border)}
.col{display:flex;flex-direction:column;gap:8px}
.list{display:flex;flex-direction:column;gap:8px}
label{font-size:13px;color:var(--muted)}
input{width:100%;min-width:0;height:44px;padding:0 12px;border-radius:8px;border:1px solid var(--border2);background:var(--bg);color:var(--text);font-size:16px;font-family:inherit}
input::placeholder{color:var(--stale)}
.btn{height:44px;padding:0 18px;border-radius:8px;border:0;background:var(--accent);color:var(--on-accent);font-size:16px;font-weight:700;cursor:pointer;font-family:inherit}
.btn.ghost{background:transparent;border:1px solid var(--accent);color:var(--accent-text)}
.btn.plain{background:transparent;border:1px solid var(--border2);color:var(--text);font-weight:400}
.btn.grey{background:var(--border);color:var(--text);font-weight:400}
.btn.armed{background:var(--sos-strong);border:0;color:var(--on-sos);font-weight:700}
.btn:disabled{opacity:.5;cursor:default}
.tgl{min-width:72px;height:44px;padding:0 14px;border-radius:22px;cursor:pointer;font-size:15px;font-weight:700;background:transparent;color:var(--text);border:1px solid var(--border2);font-family:inherit}
.tgl[aria-pressed=true]{background:var(--accent);color:var(--on-accent);border:0}
.setrow{display:flex;align-items:center;justify-content:space-between;gap:12px}
.setrow>span{display:flex;flex-direction:column}.setrow small{font-size:13px;color:var(--muted)}
.msg{margin:0;font-size:13px;color:var(--text2);overflow-wrap:anywhere}.msg.err{color:var(--sos-author)}
.sosban{display:flex;gap:12px;align-items:flex-start;padding:14px;border-radius:12px;background:var(--sos-banner);border:1px solid var(--sos-strong);color:var(--sos-text);text-decoration:none}
.sosban svg{flex-shrink:0;margin-top:1px;stroke:var(--sos-dot)}
.sosban span{display:flex;flex-direction:column;gap:2px}.sosban b{font-weight:700}.sosban small{font-size:14px}
.post .ph{display:flex;justify-content:space-between;gap:8px;font-size:13px}
.post .pa{font-weight:700;color:var(--accent-text);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.post .pt{color:var(--muted);flex-shrink:0}
.post p{margin:4px 0 0;font-size:16px;line-height:1.4;overflow-wrap:anywhere}
.post.sos{background:var(--sos-card);border-color:var(--sos-border)}.post.sos .pa{color:var(--sos-author)}
.post.sys .pa{color:var(--muted)}
.soscard{padding:14px;border-radius:12px;background:var(--sos-card);border:1px solid var(--sos-border);display:flex;flex-direction:column;gap:8px}
.soscard .sh{display:flex;justify-content:space-between;gap:8px}
.soscard .sh b{font-size:17px;color:var(--sos-text);overflow-wrap:anywhere}.soscard .sh span{font-size:13px;color:var(--sos-muted);flex-shrink:0}
.soscard .kv{color:var(--sos-muted);font-size:14px}.soscard .kv b{color:var(--sos-text)}
.sosmap{display:flex;align-items:center;justify-content:center;height:44px;border-radius:8px;background:var(--sos-strong);color:var(--on-sos);font-weight:700;text-decoration:none}
.grid2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}
.stat .l{font-size:12px;color:var(--muted)}.stat .v{font-size:22px;font-weight:700;overflow-wrap:anywhere}
.meter{height:6px;border-radius:3px;background:var(--border);margin-top:6px;overflow:hidden}.meter i{display:block;height:6px;border-radius:3px;background:var(--accent)}
.kv{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px 12px;font-size:13px;color:var(--muted)}
.kv b{display:block;font-size:16px;color:var(--text);overflow-wrap:anywhere}
.seg{display:flex;background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:3px}
.seg.g2{display:grid;grid-template-columns:repeat(2,minmax(0,1fr))}.seg.g3{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));background:var(--bg)}
.seg button{height:40px;padding:0 14px;border-radius:8px;border:0;cursor:pointer;font-size:15px;font-weight:700;background:transparent;color:var(--text2);font-family:inherit}
.seg button[aria-pressed=true]{background:var(--accent);color:var(--on-accent)}
.filters{display:flex;gap:8px;flex-wrap:wrap}
.node{padding:0;overflow:hidden}.node.stale{border:1px dashed var(--warn-border);opacity:.75}
.nrow{display:flex;align-items:center;gap:4px}
.nbtn{flex-grow:1;min-width:0;display:flex;align-items:center;gap:12px;padding:12px;background:transparent;border:0;color:var(--text);text-align:left;cursor:pointer;font:inherit}
.dot{width:10px;height:10px;border-radius:5px;flex-shrink:0;background:var(--live)}.dot.old{background:transparent;border:2px solid var(--stale)}
.nmid{display:flex;flex-direction:column;gap:2px;min-width:0;flex-grow:1}
.nname{font-weight:700;font-size:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.nsub{font-size:13px;color:var(--muted)}.nsub.warn{color:var(--warn-title)}
.nr{display:flex;flex-direction:column;align-items:flex-end;gap:2px;flex-shrink:0}.nr b{font-size:15px}.nr small{font-size:12px;color:var(--muted)}
.star{width:48px;height:48px;flex-shrink:0;display:flex;align-items:center;justify-content:center;background:transparent;border:0;cursor:default;padding:0}
.star svg{fill:none;stroke:var(--star-off)}.star.adm{cursor:pointer}.star.adm svg{stroke:var(--text2)}.star.on svg{fill:var(--accent);stroke:var(--accent)}
.ndet{padding:0 12px 12px 34px;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;font-size:13px;color:var(--muted)}
.ndet b{display:block;font-weight:400;color:var(--text);overflow-wrap:anywhere}
.maplink{grid-column:1/-1;display:flex;align-items:center;justify-content:center;height:40px;border-radius:8px;border:1px solid var(--accent);text-decoration:none;font-weight:700;color:var(--accent-text)}
.radarw{display:flex;flex-direction:column;align-items:center;gap:10px}
.radar{position:relative;width:300px;height:300px;border-radius:50%;background:var(--radar);border:1px solid var(--border2);overflow:hidden}
.radar .rg{position:absolute;border-radius:50%;border:1px dashed var(--ring)}
.radar .ln{position:absolute;background:var(--line)}
.radar .lb{position:absolute;font-size:11px;color:var(--muted)}
.radar .me{position:absolute;left:143px;top:143px;width:14px;height:14px;border-radius:3px;background:var(--accent)}
.radar .rn{position:absolute;display:flex;align-items:center;gap:4px;font-size:11px;white-space:nowrap}
.radar .rn i{width:10px;height:10px;border-radius:5px;background:var(--muted);flex-shrink:0}.radar .rn.tm i{background:var(--accent)}
.zoom{display:flex;align-items:center;gap:12px}
.zoom button{width:48px;height:48px;border-radius:24px;border:1px solid var(--border2);background:var(--surface);color:var(--text);font-size:24px;line-height:1;cursor:pointer}
.zoom span{min-width:110px;text-align:center;font-size:14px}
.bars{height:90px;display:flex;align-items:flex-end;gap:2px}
.bars div{flex:1 1 0;display:flex;flex-direction:column;justify-content:flex-end;height:90px}
.bars .t{background:var(--accent);border-radius:2px 2px 0 0}.bars .r{background:var(--bar-rx)}
.legend{display:flex;justify-content:space-between;font-size:12px;color:var(--muted)}
.legend span{display:flex;align-items:center;gap:4px}.legend i{width:10px;height:10px}
.ptrow{display:grid;grid-template-columns:96px minmax(0,1fr) 44px;gap:8px;align-items:center;font-size:13px}
.ptrow span:first-child{color:var(--text2)}
.hbar{height:10px;border-radius:5px;background:var(--border);overflow:hidden}.hbar i{display:block;height:10px;border-radius:5px;background:var(--bar)}
.nbrow{display:grid;grid-template-columns:minmax(0,1fr) 90px 64px;gap:8px;align-items:center;font-size:14px}
.nbrow span:first-child{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.nbrow .hbar i{background:var(--accent)}
.warnbox{padding:12px;border-radius:12px;background:var(--warn-bg);border:1px solid var(--warn-border);display:flex;flex-direction:column;gap:4px;color:var(--warn-text);font-size:14px}
.warnbox h2{color:var(--warn-title)}
.feed{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:8px 0;border-top:1px solid var(--border)}
.feed>span:first-child{display:flex;flex-direction:column;gap:2px;min-width:0}
.feed b{font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.feed small{font-size:12px;color:var(--muted)}
.feed>span:last-child{flex-shrink:0;font-size:13px;color:var(--text2)}
.chrow{display:flex;align-items:center;justify-content:space-between;gap:8px;padding:8px 0;border-bottom:1px solid var(--border)}
.chrow span{display:flex;flex-direction:column;min-width:0}.chrow b{overflow-wrap:anywhere}
.chrow button{height:40px;padding:0 12px;border-radius:8px;border:1px solid var(--border2);background:transparent;color:var(--text);cursor:pointer;font-family:inherit;font-size:14px}
nav{position:sticky;bottom:0;display:grid;grid-template-columns:repeat(4,minmax(0,1fr));background:var(--nav);border-top:1px solid var(--border);padding-bottom:env(safe-area-inset-bottom)}
nav button{position:relative;height:60px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;background:transparent;border:0;cursor:pointer;color:var(--muted);font-family:inherit;font-size:12px}
nav button[aria-current=page]{color:var(--accent-text);font-weight:700}
nav .bdg{position:absolute;top:6px;right:calc(50% - 20px);width:8px;height:8px;border-radius:4px;background:var(--sos-dot)}
.toast{position:fixed;left:50%;bottom:84px;transform:translateX(-50%);max-width:calc(100% - 32px);padding:10px 14px;border-radius:10px;background:var(--text);color:var(--bg);font-size:14px;z-index:5}
</style></head><body><div class="app">
<header><div class="hl"><span class="kick">FieldMesh</span><h1 id="hname">FieldMesh</h1></div>
<div class="hr"><span id="mode" class="badge">-</span><span id="freq" class="mono small">-</span>
<span id="live" class="live"><i aria-hidden="true"></i><span id="livet">connecting</span></span></div></header>
<div id="offline" class="offline" role="status" hidden></div>
<main>

<section id="t-board" class="tab">
<a id="sosban" class="sosban" href="#" data-go="sos" hidden><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4"/><path d="M12 17h.01"/></svg><span><b id="sosbt"></b><small id="sosbs"></small></span></a>
<p id="roomoff" class="hint card" hidden>The board is off. The admin can turn the room server on under Setup.</p>
<form id="pform" class="card col" autocomplete="off">
<label for="pname">Your name</label><input id="pname" maxlength="24" placeholder="e.g. Anna">
<label for="ptext">Message to the board</label>
<div style="display:flex;gap:8px"><input id="ptext" maxlength="120" placeholder="Meeting at the tent at 8"><button class="btn" type="submit">Post</button></div>
<p id="pmsg" class="msg" hidden></p></form>
<div id="posts" class="list"></div>
<p id="noposts" class="hint" hidden>No posts yet.</p>
</section>

<section id="t-sos" class="tab" hidden>
<h2 style="font-size:18px">SOS heard by the box</h2>
<p class="hint">From the SOS channels the box listens to. Distance and direction from the box.</p>
<div id="soslist" class="list"></div>
<p id="nosos" class="hint card" hidden>No SOS heard since the box started.</p>
</section>

<section id="t-mesh" class="tab" hidden>
<div class="grid2">
<div class="card stat"><div class="l">Team heard</div><div class="v" id="m-team">-</div><div class="small">last 15 min</div></div>
<div class="card stat"><div class="l">Nodes heard</div><div class="v" id="m-nodes">-</div><div class="small">last 24 h</div></div>
<div class="card stat"><div class="l">Airtime (duty cycle)</div><div class="v" id="m-duty">-</div><div class="meter"><i id="m-dutyb" style="width:0"></i></div><div class="small" id="m-dutyl" style="margin-top:4px">last hour</div></div>
<div class="card stat"><div class="l">Noise · Box</div><div class="v" id="m-noise">-</div><div class="small" id="m-env">&nbsp;</div></div>
</div>
<div class="seg g2" data-seg="sub" role="group" aria-label="Mesh view"><button type="button" data-v="nodes">Nodes</button><button type="button" data-v="network">Network</button></div>

<div id="v-nodes" class="tab" style="gap:14px">
<div class="filters">
<div class="seg" data-seg="filter" role="group" aria-label="Which nodes"><button type="button" data-v="team">Team</button><button type="button" data-v="all">Show all</button></div>
<div class="seg" data-seg="sort" role="group" aria-label="Sort"><button type="button" data-v="heard">Recent</button><button type="button" data-v="dist">Nearest</button></div>
<div class="seg" data-seg="view" role="group" aria-label="View"><button type="button" data-v="list">List</button><button type="button" data-v="radar">Radar</button></div>
</div>
<div id="v-list" class="list"><div id="nlist" class="list"></div><p id="nnote" class="hint" hidden></p>
<p id="starhint" class="hint">Team members are set by the admin: log in under Setup, then tap the star.</p></div>
<div id="v-radar" class="radarw" hidden>
<div class="radar" role="img" aria-label="Radar: nodes around the box by distance and direction">
<div class="rg" style="left:12px;top:12px;width:276px;height:276px"></div><div class="rg" style="left:81px;top:81px;width:138px;height:138px"></div>
<div class="ln" style="left:149px;top:0;width:1px;height:300px"></div><div class="ln" style="left:0;top:149px;width:300px;height:1px"></div>
<span class="lb" style="left:146px;top:0">N</span><span class="lb" id="r-out" style="left:252px;top:134px"></span><span class="lb" id="r-half" style="left:186px;top:134px"></span>
<div class="me"></div><div id="rnodes"></div></div>
<div class="zoom"><button type="button" data-act="zin" aria-label="Zoom in">+</button><span id="r-range"></span><button type="button" data-act="zout" aria-label="Zoom out">&minus;</button></div>
<p class="hint" style="text-align:center" id="r-hint"></p></div>
</div>

<div id="v-net" class="tab" style="gap:12px" hidden>
<section class="card col"><h2>Box</h2><div class="kv" id="n-box"></div></section>
<section class="card col"><div style="display:flex;justify-content:space-between;gap:8px;align-items:baseline"><h2>Activity, last 60 min</h2><span class="small">packets / 2 min</span></div>
<div class="bars" id="n-bars" role="img" aria-label="Received and sent packets per 2 minutes over the last hour"></div>
<div class="legend"><span>-60 min</span><span style="gap:12px"><span><i style="background:var(--bar-rx)"></i>received</span><span><i style="background:var(--accent)"></i>sent</span></span><span>now</span></div></section>
<section class="card col"><h2 id="n-radioh">Radio since start</h2><div class="kv" id="n-radio"></div></section>
<section class="card col"><h2>Packet types</h2><div id="n-types" class="col"></div></section>
<section class="card col"><h2>Neighbour repeaters (direct)</h2><div id="n-nb" class="col"></div><span class="small">SNR of their last advert heard by the box.</span></section>
<div id="n-col" class="col" hidden></div>
<section class="card" style="display:flex;flex-direction:column;gap:6px"><h2>Live packets</h2><div id="n-feed"></div></section>
</div>
</section>

<section id="t-setup" class="tab" style="gap:14px" hidden>
<section class="card col" style="padding:14px"><h2>Appearance</h2>
<div class="seg g3" data-seg="theme" role="group" aria-label="Appearance"><button type="button" data-v="auto">Auto</button><button type="button" data-v="light">Light</button><button type="button" data-v="dark">Dark</button></div>
<span class="small">Auto follows the phone (day/night). Saved on this phone only.</span></section>

<form id="lform" class="card col" style="padding:14px" autocomplete="off">
<h2 style="font-size:18px">Admin login</h2>
<label for="lpw">Same password as the repeater admin</label><input id="lpw" type="password" autocomplete="current-password">
<button class="btn" type="submit">Log in</button><p id="lmsg" class="msg err" hidden></p></form>

<div id="adm" class="tab" style="gap:14px" hidden>
<section class="card col" style="padding:14px;gap:10px"><h2>Radio</h2>
<div class="setrow"><span><b>Off-Grid</b><small id="s-ogd">-</small></span><button type="button" class="tgl" id="s-og" data-act="og">-</button></div>
<p id="s-ogm" class="msg" hidden></p></section>

<section class="card col" style="padding:14px;gap:10px"><h2>Room / board</h2>
<div class="setrow"><span><b>Room server</b><small id="s-rd">-</small></span><button type="button" class="tgl" id="s-room" data-act="room">-</button></div>
<label for="s-rn">Name</label><input id="s-rn" maxlength="31">
<label for="s-rp">Room password (empty = open)</label><input id="s-rp" maxlength="15" placeholder="open" autocomplete="off">
<button type="button" class="btn ghost" data-act="rsave">Save room settings</button>
<button type="button" class="btn plain" id="s-reset" data-act="rreset">Reset room…</button>
<span class="small">Reset deletes posts, users and password. Name stays. Changing the password logs everyone out. Turning the room on or off and renaming it changes the WiFi name: connect again afterwards.</span>
<p id="s-rm" class="msg" hidden></p></section>

<section class="card col" style="padding:14px;gap:10px"><h2>SOS channels</h2>
<div id="s-ch"></div>
<label for="s-chn">Channel name (#name for hashtag channels)</label><input id="s-chn" maxlength="31" placeholder="#sos" autocapitalize="none">
<label for="s-chk">Key (private channels only, 32 hex)</label><input id="s-chk" class="mono" maxlength="32" placeholder="not shown again after saving" autocapitalize="none" autocomplete="off">
<button type="button" class="btn ghost" data-act="chadd">Add channel</button>
<span class="small">A channel whose name contains "sos" is an SOS channel: "!SOS" messages there show up under SOS and on the board.</span>
<p id="s-chm" class="msg" hidden></p></section>

<section class="card col" style="padding:14px;gap:10px"><h2>Team</h2><span class="hint" style="font-size:14px" id="s-team"></span></section>

<section class="card col" style="padding:14px;gap:10px"><h2>Box</h2><span class="hint" style="font-size:14px" id="s-clock"></span>
<button type="button" class="btn plain" id="s-wifi" data-act="wifi">Turn WiFi off…</button>
<button type="button" class="btn grey" data-act="logout">Log out</button>
<p id="s-bm" class="msg" hidden></p></section>
</div>
</section>
</main>
<nav aria-label="Sections">
<button type="button" data-tab="board"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/></svg>Board</button>
<button type="button" data-tab="sos"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4"/><path d="M12 17h.01"/></svg>SOS<span class="bdg" id="sosbdg" hidden></span></button>
<button type="button" data-tab="mesh"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="2"/><path d="M16.2 7.8a6 6 0 0 1 0 8.4"/><path d="M7.8 16.2a6 6 0 0 1 0-8.4"/><path d="M19.1 4.9a10 10 0 0 1 0 14.2"/><path d="M4.9 19.1a10 10 0 0 1 0-14.2"/></svg>Mesh</button>
<button type="button" data-tab="setup"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/></svg>Setup</button>
</nav>
<div id="toast" class="toast" role="status" hidden></div>
</div>
<script>
'use strict';
const $=id=>document.getElementById(id);
const RANGES=[250,500,1000,2000,5000,10000];
const TYPES={1:'Companion',2:'Repeater',3:'Room',4:'Sensor'};
const st={tab:'board',filter:'team',sort:'heard',view:'list',sub:'nodes',zoom:2,theme:'auto'};
const open={},pend={};          // open node rows; stars waiting for the box
let S=null,B=null,N=null,W=null,SET=null,tS=0,tB=0,tN=0,tW=0,tok=null,busyAdm=false;

function ls(k,v){try{if(v===undefined)return localStorage.getItem(k);localStorage.setItem(k,v);}catch(e){}return null;}
function ss(k,v){try{if(v===undefined)return sessionStorage.getItem(k);if(v===null)sessionStorage.removeItem(k);else sessionStorage.setItem(k,v);}catch(e){}return null;}
['filter','sort','view','sub','theme'].forEach(k=>{const v=ls('fm-'+k);if(v)st[k]=v;});
{const z=parseInt(ls('fm-zoom'));if(z>=0&&z<RANGES.length)st.zoom=z;}
tok=ss('fm-tok');
$('pname').value=ls('fm-name')||'';

function esc(s){return String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function setT(el,v){if(typeof el==='string')el=$(el);if(el.textContent!==v)el.textContent=v;}
function setH(el,h){if(typeof el==='string')el=$(el);if(el._h!==h){el.innerHTML=h;el._h=h;}}
function show(el,on){if(typeof el==='string')el=$(el);if(el.hidden===!!on)el.hidden=!on;}
function p2(n){return('0'+n).slice(-2);}
function hm(t){const d=new Date(t*1000);return p2(d.getHours())+':'+p2(d.getMinutes());}
function hms(t){const d=new Date(t*1000);return hm(t)+':'+p2(d.getSeconds());}
function dur(s){s=Math.max(0,Math.floor(s));const d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;
 if(d)return d+' d '+h+' h';if(h)return h+' h '+m+' min';if(m)return m+' min '+(s%60)+' s';return(s%60)+' s';}
function ago(s){s=Math.max(0,s);if(s<45)return'just now';if(s<3600)return Math.max(1,Math.round(s/60))+' min ago';
 if(s<86400)return Math.round(s/3600)+' h ago';return Math.round(s/86400)+' d ago';}
function since(s){return ago(s).replace(' ago','');}
function okClock(t){return t>1704067200;}
function boxNow(){return S?S.time+(Date.now()-tS)/1000:Date.now()/1000;}
function nowOf(J,t){return J.now+(Date.now()-t)/1000;}
function distTxt(m){return m<1000?(Math.round(m/10)*10)+' m':(m/1000).toFixed(1)+' km';}
function compass(d){return['N','NE','E','SE','S','SW','W','NW'][Math.round(d/45)%8];}
function rangeTxt(m){return m>=1000?(m/1000)+' km':m+' m';}
function geo(la,lo){ // distance (m) and bearing (deg) from the box, null without positions
 if(!S||S.lat==null||la==null)return null;
 const R=6371000,r=Math.PI/180,p1=S.lat*r,p2=la*r,dp=(la-S.lat)*r,dl=(lo-S.lon)*r;
 const a=Math.sin(dp/2)**2+Math.cos(p1)*Math.cos(p2)*Math.sin(dl/2)**2;
 const b=Math.atan2(Math.sin(dl)*Math.cos(p2),Math.cos(p1)*Math.sin(p2)-Math.sin(p1)*Math.cos(p2)*Math.cos(dl));
 return{d:2*R*Math.asin(Math.min(1,Math.sqrt(a))),b:(b/r+360)%360};}
function mapUrl(la,lo,name){
 const ios=/iPhone|iPad|iPod/.test(navigator.userAgent)||(/Macintosh/.test(navigator.userAgent)&&'ontouchend' in document);
 return ios?'https://maps.apple.com/?ll='+la+','+lo+'&q='+encodeURIComponent(name):'geo:'+la+','+lo+'?q='+la+','+lo+'('+encodeURIComponent(name)+')';}
function snrBar(s){return Math.max(4,Math.min(100,Math.round((s+20)/32*100)));}
// keyed list: only items whose HTML changed are touched, open rows stay open
function sync(box,items){
 const old={};for(let c=box.firstElementChild;c;c=c.nextElementSibling)old[c._k]=c;
 let prev=null;
 for(const it of items){
  let el=old[it.k];
  if(el)delete old[it.k];else{el=document.createElement(it.tag||'div');el._k=it.k;}
  if(el._h!==it.h){el.innerHTML=it.h;el._h=it.h;}
  if(it.c!=null&&el.className!==it.c)el.className=it.c;
  const want=prev?prev.nextElementSibling:box.firstElementChild;
  if(want!==el)box.insertBefore(el,want);
  prev=el;}
 for(const k in old)box.removeChild(old[k]);}
let toastT=0;
function toast(m){setT('toast',m);show('toast',true);clearTimeout(toastT);toastT=setTimeout(()=>show('toast',false),3500);}
function msg(id,m,err){const e=$(id);setT(e,m||'');e.className='msg'+(err?' err':'');show(e,!!m);}

// ---- theme ----
function applyTheme(){if(st.theme==='auto')document.documentElement.removeAttribute('data-theme');else document.documentElement.setAttribute('data-theme',st.theme);}
applyTheme();

// ---- header ----
function renderHead(){
 const age=tS?(Date.now()-tS)/1000:0,live=tS&&age<10;
 const e=$('live');e.className='live'+(live||!tS?'':' off');
 setT('livet',!tS?'connecting':live?'Live · updated '+Math.round(age)+' s ago':'Offline · last data '+since(age)+' ago');
 show('offline',tS&&!live);
 if(tS&&!live)setT('offline','No connection to the box. Showing data from '+hm(tS/1000)+'. Move closer or check the WiFi.');
 if(!S)return;
 setT('hname',S.name);if(document.title!==S.name)document.title=S.name;
 const m=$('mode');setT(m,S.offgrid?'OFF-GRID':'NORMAL');m.className='badge'+(S.offgrid?' og':'');
 setT('freq',S.freq.toFixed(3)+' MHz'+(okClock(S.time)?' · '+hm(boxNow()):''));}

// ---- board ----
function sosRecent(){if(!B||!B.sos)return[];const n=nowOf(B,tB);return B.sos.filter(s=>n-s.t<86400);}
function sosPos(i){const m=/(-?\d+\.\d+)\s*,\s*(-?\d+\.\d+)/.exec(i||'');return m?{la:parseFloat(m[1]),lo:parseFloat(m[2])}:null;}
function sosBat(i){const m=/bat\s*(\d+)\s*%/i.exec(i||'');return m?m[1]+' %':'-';}
function renderBoard(){
 if(!B)return;
 const n=nowOf(B,tB),sos=sosRecent();
 show('sosban',sos.length>0);show('sosbdg',sos.length>0);
 if(sos.length){const s=sos[0],p=sosPos(s.i),g=p&&geo(p.la,p.lo);
  setT('sosbt','SOS from '+s.s);setT('sosbs',(g?distTxt(g.d)+' '+compass(g.b)+' · ':'')+ago(n-s.t)+' · tap for details');}
 show('pform',!!B.room);show('roomoff',!B.room);
 const posts=(B.posts||[]).map(p=>{
  const c='card post'+(p.k==='s'?' sos':p.k==='y'?' sys':'');
  return{k:p.t+'|'+p.a+'|'+p.x,tag:'article',c:c,h:'<div class="ph"><span class="pa">'+esc(p.a)+'</span><span class="pt">'+ago(n-p.t)+'</span></div><p>'+esc(p.x)+'</p>'};});
 sync($('posts'),posts);show('noposts',posts.length===0);
 // SOS tab
 const list=(B.sos||[]).map(s=>{
  const p=sosPos(s.i),g=p&&geo(p.la,p.lo);
  return{k:s.s+'|'+s.t,tag:'article',c:'soscard',h:'<div class="sh"><b>'+esc(s.s)+'</b><span>'+ago(n-s.t)+'</span></div>'
   +'<div class="kv"><span>Position<b class="mono">'+(p?p.la.toFixed(5)+', '+p.lo.toFixed(5):'not sent')+'</b></span>'
   +'<span>From box<b>'+(g?distTxt(g.d)+' '+compass(g.b):'-')+'</b></span>'
   +'<span>Battery<b>'+esc(sosBat(s.i))+'</b></span><span>Heard<b>'+(okClock(s.t)?hm(s.t):'-')+'</b></span></div>'
   +(p?'<a class="sosmap" href="'+esc(mapUrl(p.la,p.lo,'SOS '+s.s))+'">Open in map app</a>':'')};});
 sync($('soslist'),list);show('nosos',list.length===0);}

// ---- mesh: nodes ----
function nodeTeam(x){return x.k in pend?pend[x.k]:!!x.tm;}
function renderNodes(){
 if(!S)return;
 // stat cards (status data)
 const duty=S.duty_ms/36000,eu=S.freq>=869.4&&S.freq<=869.65;
 setT('m-duty',duty.toFixed(1)+' %');$('m-dutyb').style.width=Math.min(100,eu?duty*10:duty)+'%';
 setT('m-dutyl',eu?'last hour · limit 10 %':'last hour');
 setT('m-noise',S.noise+' dBm');
 setT('m-env',S.temp!=null?S.temp+' °C'+(S.hum!=null?' · '+S.hum+' %':''):S.mcu!=null?'chip '+S.mcu+' °C':' ');
 if(!N)return;
 const n=nowOf(N,tN),nodes=N.nodes||[];
 const team=nodes.filter(nodeTeam);
 setT('m-team',team.filter(x=>n-x.a<900).length+' of '+team.length);setT('m-nodes',String(nodes.length));
 const rows=nodes.map(x=>{const g=x.la!=null?geo(x.la,x.lo):null;return{x:x,g:g,tm:nodeTeam(x)};});
 const ord=(a,b)=>st.sort==='dist'?((a.g?a.g.d:1e12)-(b.g?b.g.d:1e12))||(b.x.a-a.x.a):(b.x.a-a.x.a);
 const tm=rows.filter(r=>r.tm).sort(ord);
 const shown=st.filter==='team'?tm:tm.concat(rows.filter(r=>!r.tm).sort(ord));
 if(st.view==='list'){
  const max=120,items=shown.slice(0,max).map(r=>nodeRow(r,n));
  sync($('nlist'),items);
  let note='';
  if(st.filter==='team'&&tm.length===0)note='No team members yet. Tap "Show all" to see every node the box heard.';
  else if(shown.length>max)note=(shown.length-max)+' more not shown.';
  else if(!shown.length)note='No nodes heard in the last 24 h.';
  setT('nnote',note);show('nnote',!!note);show('starhint',!tok);
 }else renderRadar(shown);}
function nodeRow(r,n){
 const x=r.x,age=n-x.a,stale=r.tm&&age>1800,fixed=x.y===2||x.y===3,adm=!!tok,isOpen=!!open[x.k];
 const sub=stale?'not heard for '+since(age):(TYPES[x.y]||'Node')+' · heard '+ago(age);
 const dist=x.la==null?'no position':r.g?distTxt(r.g.d)+' '+compass(r.g.b):'-';
 const sig=x.h===0?'SNR '+x.s.toFixed(1)+' dB':x.h+(x.h===1?' hop':' hops');
 let h='<div class="nrow"><button type="button" class="nbtn" data-act="open" data-k="'+x.k+'" aria-expanded="'+isOpen+'">'
  +'<span class="dot'+(age<900?'':' old')+'" aria-hidden="true"></span>'
  +'<span class="nmid"><span class="nname">'+esc(x.n)+'</span><span class="nsub'+(stale?' warn':'')+'">'+esc(sub)+'</span></span>'
  +'<span class="nr"><b>'+dist+'</b><small>'+sig+'</small></span></button>'
  +'<button type="button" class="star'+(r.tm?' on':'')+(adm?' adm':'')+'" data-act="star" data-k="'+x.k+'" aria-pressed="'+r.tm+'" aria-label="'+(r.tm?'Remove '+esc(x.n)+' from team':'Add '+esc(x.n)+' to team')+'">'
  +'<svg width="22" height="22" viewBox="0 0 24 24" stroke-width="2" stroke-linejoin="round" aria-hidden="true"><path d="M12 2.8l2.8 5.8 6.4.9-4.6 4.5 1.1 6.4L12 17.4l-5.7 3 1.1-6.4-4.6-4.5 6.4-.9z"/></svg></button></div>';
 if(isOpen){
  h+='<div class="ndet"><span>Last position<b>'+(x.la==null?'never sent':fixed?'fixed location':ago(n-x.pa))+'</b></span>'
   +'<span>Coordinates<b class="mono">'+(x.la==null?'-':x.la.toFixed(4)+', '+x.lo.toFixed(4))+'</b></span>'
   +'<span>Signal (SNR)<b>'+x.s.toFixed(1)+' dB</b></span>'
   +'<span>Route<b>'+(x.h===0?'direct (0 hops)':x.h+(x.h===1?' hop':' hops'))+'</b></span>'
   +'<span>Adverts heard<b>'+x.c+'</b></span><span>ID<b class="mono">'+x.k.slice(0,2).toUpperCase()+' · '+x.k.toUpperCase()+'</b></span>'
   +(x.la!=null?'<a class="maplink" href="'+esc(mapUrl(x.la,x.lo,x.n))+'">Open in map app</a>':'')+'</div>';}
 return{k:x.k,tag:'article',c:'card node'+(stale?' stale':''),h:h};}
function renderRadar(shown){
 const R=150,max=RANGES[st.zoom];
 setT('r-out',rangeTxt(max));setT('r-half',rangeTxt(max/2));setT('r-range','Range '+rangeTxt(max));
 let out=0,nopos=0;const items=[];
 for(const r of shown){
  if(!r.g){nopos++;continue;}
  if(r.g.d>max*1.08){out++;continue;}
  const rad=Math.min(r.g.d/max,1)*(R-12),a=r.g.b*Math.PI/180;
  const x=Math.round(R+rad*Math.sin(a)-5),y=Math.round(R-rad*Math.cos(a)-5);
  items.push({k:r.x.k,tag:'span',c:'rn'+(r.tm?' tm':''),h:'<i></i>'+esc(r.x.n),st:'left:'+x+'px;top:'+y+'px'});}
 const box=$('rnodes');sync(box,items);
 for(const it of items){for(let c=box.firstElementChild;c;c=c.nextElementSibling)if(c._k===it.k&&c.getAttribute('style')!==it.st)c.setAttribute('style',it.st);}
 let hint=S&&S.lat!=null?'Box in the middle. Only nodes that send their position.':'The box has no position (set lat / set lon), so no radar.';
 if(out)hint+=' '+out+' further away.';if(nopos)hint+=' '+nopos+' without position.';
 setT('r-hint',hint);}

// ---- mesh: network ----
function renderNet(){
 if(!S)return;
 const reason=S.boot?', '+S.boot:'';
 setH('n-box','<span>Running for<b>'+dur(S.uptime)+'</b></span>'
  +'<span>Last start<b>'+(okClock(S.time)?hm(S.time-S.uptime):'-')+esc(reason)+'</b></span>'
  +'<span>Clock<b>'+(okClock(S.time)?hm(boxNow()):'not set')+'</b></span>'
  +'<span>Board<b>'+(S.room?S.posts+' posts · '+S.users+' users':'room off')+'</b></span>');
 setT('n-radioh','Radio since start ('+dur(S.uptime)+')');
 if(!W)return;
 const act=W.act||[],peak=Math.max(8,...act.map(b=>b[0]+b[1]));
 setH('n-bars',act.map(b=>'<div><span class="t" style="height:'+Math.round(b[1]/peak*88)+'px"></span><span class="r" style="height:'+Math.round(b[0]/peak*88)+'px"></span></div>').join(''));
 const t=W.st||{},sec=v=>dur(v);
 setH('n-radio','<span>Received<b>'+t.rx+'</b></span><span>Sent<b>'+t.tx+'</b></span>'
  +'<span>Flood / direct in<b>'+t.rf+' / '+t.rd+'</b></span><span>Duplicates dropped<b>'+((t.df||0)+(t.dd||0))+'</b></span>'
  +'<span>TX airtime<b>'+sec(t.txair||0)+'</b></span><span>RX airtime<b>'+sec(t.rxair||0)+'</b></span>'
  +'<span>Last packet<b>SNR '+(t.snr!=null?t.snr.toFixed(1):'-')+' · '+t.rssi+' dBm</b></span><span>Receive errors<b>'+t.err+'</b></span>');
 const c=W.types||[],sum=c.reduce((a,b)=>a+b,0)||1,grp=[['Adverts',[4]],['Messages',[2]],['Channel msgs',[5,6]],['ACKs',[3]],['Paths',[8]],['Requests',[0,1,7]],['Other',[9,10,11,12,13,14,15]]];
 setH('n-types',grp.map(g=>{const p=Math.round(g[1].reduce((a,i)=>a+(c[i]||0),0)/sum*100);
  return'<div class="ptrow"><span>'+g[0]+'</span><div class="hbar"><i style="width:'+p+'%"></i></div><span class="mono" style="text-align:right">'+p+' %</span></div>';}).join(''));
 const nb=(W.nb||[]).slice().sort((a,b)=>b.s-a.s);
 setH('n-nb',nb.length?nb.map(x=>'<div class="nbrow"><span>'+esc(x.n)+'</span><div class="hbar"><i style="width:'+snrBar(x.s)+'%"></i></div><span class="mono" style="text-align:right;font-size:13px">'+x.s.toFixed(1)+' dB</span></div>').join(''):'<span class="hint">No neighbour repeater heard yet.</span>');
 setH('n-col',(W.col||[]).map(g=>'<section class="warnbox"><h2>Hash collision</h2><span>'+g.n.map(esc).join(' and ')+' share the short ID '+esc(g.id)+'. Apps may show the wrong node in message paths.</span></section>').join(''));
 show('n-col',(W.col||[]).length>0);
 const TN=['Request','Response','Message','ACK','Advert','Channel msg','Channel data','Anon request','Path','Trace','Multipart','Control','Type 12','Type 13','Type 14','Custom'];
 sync($('n-feed'),(W.feed||[]).map((f,i)=>({k:f.t+'|'+f.y+'|'+f.n+'|'+f.s+'|'+f.h,c:'feed',
  h:'<span><b>'+(TN[f.y]||'?')+' · '+esc(f.n||'-')+'</b><small>'+(okClock(f.t)?hms(f.t):'-')+' · '+(f.f?'flood '+f.h:'direct')+'</small></span><span class="mono">SNR '+f.s.toFixed(1)+'</span>'})));}

// ---- setup ----
function fill(id,v){const e=$(id);if(document.activeElement!==e&&!e._dirty&&e.value!==v)e.value=v;}
['s-rn','s-rp','s-chn','s-chk'].forEach(id=>$(id).addEventListener('input',e=>{e.target._dirty=true;}));
function renderSetup(){
 show('lform',!tok);show('adm',!!tok);
 if(!tok)return;
 if(S){setT('s-team',S.team?S.team+(S.team===1?' team member.':' team members.')+' Tap the star next to a node in Mesh to add or remove it.':'No team members yet. Tap the star next to a node in Mesh to add it.');
  setT('s-clock',okClock(S.time)?'Box clock: '+new Date(boxNow()*1000).toLocaleString('en-GB',{day:'numeric',month:'short',year:'numeric',hour:'2-digit',minute:'2-digit'})+'. Phones that open this page set it (forward only).':'Box clock not set yet.');}
 if(!SET)return;
 const og=$('s-og');setT(og,SET.og?'On':'Off');og.setAttribute('aria-pressed',!!SET.og);
 setT('s-ogd',SET.og?'Active on '+SET.of.toFixed(3)+' MHz, normal '+SET.nf.toFixed(3)+' MHz':'Normal '+SET.nf.toFixed(3)+' MHz, Off-Grid '+SET.of.toFixed(3)+' MHz');
 const rb=$('s-room');setT(rb,SET.room?'On':'Off');rb.setAttribute('aria-pressed',!!SET.room);
 setT('s-rd',SET.room?(SET.rp?'password set':'open, no password'):'off · repeater only');
 fill('s-rn',SET.rn);fill('s-rp',SET.rp);
 sync($('s-ch'),(SET.ch||[]).map((c,i)=>({k:c+'|'+i,c:'chrow',h:'<span><b>'+esc(c)+'</b><span class="small">'+(c[0]==='#'?'Hashtag channel':'Private channel')+(/sos/i.test(c)?' · SOS':' · not SOS (no "sos" in the name)')+'</span></span><button type="button" data-act="chdel" data-i="'+i+'">Remove</button>'})));
 if(!(SET.ch||[]).length)setH('s-ch','<span class="hint">No channel yet. Add the channel your team uses for SOS, e.g. #sos.</span>');}

// ---- render ----
function render(){
 renderHead();
 for(const b of document.querySelectorAll('nav button')){const on=b.dataset.tab===st.tab;if(b.getAttribute('aria-current')!==(on?'page':'false'))b.setAttribute('aria-current',on?'page':'false');}
 for(const t of['board','sos','mesh','setup'])show('t-'+t,st.tab===t);
 for(const g of document.querySelectorAll('[data-seg]'))for(const b of g.querySelectorAll('button')){const on=String(st[g.dataset.seg])===b.dataset.v;if(b.getAttribute('aria-pressed')!==String(on))b.setAttribute('aria-pressed',on);}
 show('v-nodes',st.sub==='nodes');show('v-net',st.sub==='network');show('v-list',st.view==='list');show('v-radar',st.view==='radar');
 renderBoard();
 if(st.tab==='mesh'){if(st.sub==='nodes')renderNodes();else renderNet();}
 if(st.tab==='setup')renderSetup();}

// ---- data ----
const inflight={};
function get(url,cb){if(inflight[url])return;inflight[url]=1;
 fetch(url,{cache:'no-store'}).then(r=>r.ok?r.json():Promise.reject(r.status)).then(j=>{inflight[url]=0;cb(j);render();}).catch(()=>{inflight[url]=0;});}
function post(url,data){return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data).toString()});}
function getStatus(){get('/api/status',j=>{if(j.name==null)return;S=j;tS=Date.now();toLocal(j.host);});}
function getBoard(){get('/api/board',j=>{if(j.now==null)return;B=j;tB=Date.now();});}
function getNodes(){get('/api/nodes',j=>{if(j.now==null)return;N=j;tN=Date.now();
 for(const x of j.nodes||[])if(x.k in pend&&!!x.tm===pend[x.k])delete pend[x.k];});}
function getNet(){get('/api/network',j=>{if(j.now==null)return;W=j;tW=Date.now();});}
let tick=0;
function poll(){
 tick++;
 if(document.hidden)return;
 if(tick%3===1){getStatus();getBoard();}
 if(st.tab==='mesh'&&tick%5===1){getNodes();if(st.sub==='network')getNet();}
 render();}
function sendTime(){post('/api/time',{t:Math.floor(Date.now()/1000)}).catch(()=>{});}
let moved=false;
function toLocal(host){
 // The fixed address (QR code: 192.168.4.1) moves on to http://<name>.local, but only when this
 // phone can reach that name (iPhone: yes; Android before 12 often not: stay on the IP then).
 if(moved||!host||location.hostname===host)return;moved=true;
 const probe=fetch('http://'+host+'/api/ping',{mode:'no-cors',cache:'no-store'});
 const timeout=new Promise((_,rej)=>setTimeout(rej,2500));
 Promise.race([probe,timeout]).then(()=>location.replace('http://'+host+'/')).catch(()=>{});}

// ---- admin ----
function logoutLocal(){tok=null;ss('fm-tok',null);SET=null;render();}
function sleep(ms){return new Promise(r=>setTimeout(r,ms));}
async function adm(c){ // runs one CLI command on the box, returns its reply text
 if(!tok)throw new Error('Log in first.');
 let id=0;
 for(let i=0;i<8&&!id;i++){
  const r=await post('/api/admin',{k:tok,c:c});
  if(r.status===403){logoutLocal();throw new Error('Logged out (session expired). Log in again.');}
  if(r.status===429){await sleep(500);continue;}
  if(!r.ok)throw new Error('The box did not accept the command.');
  id=(await r.json()).id;}
 if(!id)throw new Error('The box is busy, try again.');
 for(let i=0;i<25;i++){
  await sleep(i?400:150);
  const r=await fetch('/api/admin?k='+tok+'&id='+id,{cache:'no-store'});
  if(r.status===403){logoutLocal();throw new Error('Logged out. Log in again.');}
  const j=await r.json();
  if(j.done)return j.reply;}
 throw new Error('No answer from the box.');}
async function loadSetup(){try{SET=JSON.parse(await adm('fm.setup'));render();}catch(e){}}
async function run(msgId,cmds,okText){ // several commands in a row, shows the last reply
 if(busyAdm)return;busyAdm=true;msg(msgId,'Working…');
 let last='';
 try{for(const c of cmds){last=await adm(c);if(/^Err/.test(last))break;}
  msg(msgId,okText&&!/^Err/.test(last)?okText:last.replace(/^OK - /,''),/^Err/.test(last));}
 catch(e){msg(msgId,e.message,true);}
 busyAdm=false;inflight['/api/status']=0;getStatus();await loadSetup();return last;}
const armed={};
function arm(id,text,orig,fn){const b=$(id);
 if(armed[id]){clearTimeout(armed[id]);armed[id]=0;setT(b,orig);b.classList.remove('armed');fn();return;}
 setT(b,text);b.classList.add('armed');
 armed[id]=setTimeout(()=>{armed[id]=0;setT(b,orig);b.classList.remove('armed');},5000);}

// ---- events ----
document.addEventListener('click',e=>{
 const tabB=e.target.closest('[data-tab]');
 if(tabB){st.tab=tabB.dataset.tab;if(st.tab==='mesh'){getNodes();getNet();}if(st.tab==='setup'&&tok)loadSetup();render();window.scrollTo(0,0);return;}
 const go=e.target.closest('[data-go]');
 if(go){e.preventDefault();st.tab=go.dataset.go;render();window.scrollTo(0,0);return;}
 const segB=e.target.closest('[data-seg] button');
 if(segB){const k=segB.closest('[data-seg]').dataset.seg;st[k]=segB.dataset.v;ls('fm-'+k,st[k]);
  if(k==='theme')applyTheme();if(k==='sub'&&st.sub==='network')getNet();render();return;}
 const a=e.target.closest('[data-act]');if(!a)return;
 const act=a.dataset.act;
 if(act==='open'){open[a.dataset.k]=!open[a.dataset.k];render();}
 else if(act==='zin'||act==='zout'){st.zoom=Math.max(0,Math.min(RANGES.length-1,st.zoom+(act==='zin'?-1:1)));ls('fm-zoom',st.zoom);render();}
 else if(act==='star'){
  if(!tok){toast('Log in under Setup to change the team.');return;}
  const k=a.dataset.k,x=(N&&N.nodes||[]).find(n=>n.k===k);if(!x)return;
  const on=!nodeTeam(x);pend[k]=on;render();
  adm('team.key '+k+' '+(on?'on':'off')).then(r=>{if(/^Err/.test(r)){delete pend[k];toast(r.replace(/^Err - /,''));render();}
   setTimeout(()=>{inflight['/api/nodes']=0;getNodes();getStatus();},600);})
   .catch(err=>{delete pend[k];toast(err.message);render();});}
 else if(act==='og'&&SET)run('s-ogm',['set offgrid '+(SET.og?'off':'on')],SET.og?'Back to normal in 2 s.':'Off-Grid in 2 s.');
 else if(act==='room'&&SET){
  const name=$('s-rn').value.trim();
  run('s-rm',[SET.room?'set roomserver off':(name&&name!==SET.rn?'set roomserver '+name:'set roomserver on')]).then(()=>{$('s-rn')._dirty=false;});}
 else if(act==='rsave'&&SET){
  const name=$('s-rn').value.trim(),pw=$('s-rp').value,cmds=[];
  if(pw!==SET.rp)cmds.push('set room.pass '+pw);
  if(name&&name!==SET.rn){if(SET.room)cmds.push('set roomserver '+name);else{msg('s-rm','The room is off: the name is used when you turn it on.');return;}}
  if(!cmds.length){msg('s-rm','Nothing changed.');return;}
  run('s-rm',cmds).then(r=>{if(!/^Err/.test(r||'')){$('s-rn')._dirty=false;$('s-rp')._dirty=false;}});}
 else if(act==='rreset')arm('s-reset','Tap again to reset','Reset room…',()=>run('s-rm',['room.reset']).then(()=>{$('s-rp')._dirty=false;}));
 else if(act==='chadd'){
  const n=$('s-chn').value.trim(),k=$('s-chk').value.trim();
  if(!n){msg('s-chm','Enter a channel name.',true);return;}
  if(/\s/.test(n)){msg('s-chm','No spaces in the channel name.',true);return;}
  if(n[0]!=='#'&&!/^[0-9a-fA-F]{32}$/.test(k)){msg('s-chm','A private channel needs its key (32 hex characters).',true);return;}
  run('s-chm',['channel.add '+n+(n[0]==='#'?'':' '+k)]).then(r=>{if(!/^Err/.test(r||'')){const a1=$('s-chn'),a2=$('s-chk');a1.value=a2.value='';a1._dirty=a2._dirty=false;}});}
 else if(act==='chdel'&&SET){const c=SET.ch[parseInt(a.dataset.i)];if(c!=null)run('s-chm',['channel.del '+c]);}
 else if(act==='wifi')arm('s-wifi','Tap again: WiFi off','Turn WiFi off…',()=>{msg('s-bm','WiFi turns off. Turn it on again in the box menu or with "set wifi on".');adm('set wifi off').catch(()=>{});});
 else if(act==='logout'){post('/api/logout',{k:tok}).catch(()=>{});logoutLocal();}});

$('pform').addEventListener('submit',e=>{
 e.preventDefault();
 const name=$('pname').value.trim(),text=$('ptext').value.trim();
 if(!text){msg('pmsg','Write a message first.',true);return;}
 ls('fm-name',name);
 post('/api/post',{n:name,t:text}).then(r=>{
  if(r.status===202){$('ptext').value='';msg('pmsg','Posted.');setTimeout(()=>{msg('pmsg','');inflight['/api/board']=0;getBoard();},700);}
  else if(r.status===429)msg('pmsg','Busy, try again in a moment.',true);
  else msg('pmsg','Not posted (is the room server on?).',true);})
 .catch(()=>msg('pmsg','Not sent - no connection to the box.',true));});

$('lform').addEventListener('submit',e=>{
 e.preventDefault();
 const p=$('lpw').value;if(!p)return;
 post('/api/login',{p:p}).then(r=>{
  if(r.status===200)return r.json().then(j=>{tok=j.token;ss('fm-tok',tok);$('lpw').value='';msg('lmsg','');render();loadSetup();});
  msg('lmsg',r.status===429?'Wait 2 seconds and try again.':'Wrong password.',true);})
 .catch(()=>msg('lmsg','No connection to the box.',true));});

document.addEventListener('visibilitychange',()=>{if(!document.hidden){sendTime();tick=0;poll();}});
sendTime();poll();setInterval(poll,1000);
</script></body></html>)HTML";
