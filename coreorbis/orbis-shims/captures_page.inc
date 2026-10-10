R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#0f1117">
<title>PS5 Captures</title>
<style>
:root{--bg:#0f1117;--card:#191c25;--line:#2a2f3c;--text:#eef0f5;--dim:#9aa1b2;--accent:#5b8cff;--accent2:#ffb02e}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 -apple-system,system-ui,Segoe UI,Roboto,sans-serif}
header{position:sticky;top:0;z-index:5;background:rgba(15,17,23,.94);backdrop-filter:blur(8px);padding:12px 16px 8px;border-bottom:1px solid var(--line)}
h1{margin:0;font-size:20px;letter-spacing:.2px;display:flex;align-items:center;gap:8px}
h1 small{font-size:13px;color:var(--dim);font-weight:400}
.bar{display:flex;gap:8px;margin-top:10px;overflow-x:auto;scrollbar-width:none}
.bar::-webkit-scrollbar{display:none}
.chip{flex:none;border:1px solid var(--line);background:var(--card);color:var(--text);border-radius:999px;padding:7px 13px;font-size:14px}
.chip.on{background:var(--accent);border-color:var(--accent);color:#fff}
#gamebtn{max-width:60vw;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#picker{position:fixed;inset:0;z-index:15;background:rgba(8,9,13,.97);display:none;flex-direction:column}
#picker.open{display:flex}
#picker .top{display:flex;align-items:center;padding:14px 16px;border-bottom:1px solid var(--line)}
#picker .top h2{flex:1;margin:0;font-size:18px}
#games{overflow-y:auto;padding:14px;display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
@media(min-width:700px){#games{grid-template-columns:repeat(6,1fr)}}
.gt{background:var(--card);border:2px solid transparent;border-radius:14px;padding:8px;text-align:center;color:var(--text)}
.gt.on{border-color:var(--accent)}
.gt img,.gt .all{width:100%;aspect-ratio:1;border-radius:10px;object-fit:cover;display:block;background:#232838}
.gt .all{display:flex;align-items:center;justify-content:center;font-size:30px}
.gt b{display:block;font-size:12px;font-weight:600;margin-top:6px;line-height:1.25;max-height:2.5em;overflow:hidden}
.gt i{display:block;font-style:normal;font-size:11px;color:var(--dim)}
select.chip{appearance:none;-webkit-appearance:none;padding-right:26px;background-image:linear-gradient(45deg,transparent 50%,var(--dim) 50%),linear-gradient(135deg,var(--dim) 50%,transparent 50%);background-position:calc(100% - 14px) 50%,calc(100% - 9px) 50%;background-size:5px 5px;background-repeat:no-repeat}
main{padding:6px 10px 110px}
.day{color:var(--dim);font-size:13px;margin:16px 6px 6px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:4px}
@media(min-width:700px){.grid{grid-template-columns:repeat(5,1fr)}}
.cell{position:relative;aspect-ratio:16/9;background:var(--card);border-radius:6px;overflow:hidden}
.cell img{width:100%;height:100%;object-fit:cover;display:block}
.cell .play{position:absolute;right:6px;bottom:6px;background:rgba(0,0,0,.6);border-radius:999px;padding:2px 7px;font-size:11px}
.cell .tick{position:absolute;left:6px;top:6px;width:22px;height:22px;border-radius:50%;border:2px solid #fff;background:rgba(0,0,0,.35);display:none}
.sel .cell .tick{display:block}
.cell.picked .tick{background:var(--accent);border-color:var(--accent)}
.cell.picked .tick::after{content:"";position:absolute;left:6px;top:2px;width:5px;height:10px;border:solid #fff;border-width:0 2px 2px 0;transform:rotate(45deg)}
.cell.picked img{opacity:.75}
.empty{color:var(--dim);text-align:center;margin-top:60px}
.foot{position:fixed;left:0;right:0;bottom:0;padding:10px 14px calc(10px + env(safe-area-inset-bottom));background:rgba(15,17,23,.96);border-top:1px solid var(--line);display:flex;gap:10px;align-items:center;z-index:6}
.foot .grow{flex:1;color:var(--dim);font-size:14px}
.btn{border:0;border-radius:10px;padding:11px 16px;font-size:15px;font-weight:600;background:var(--card);color:var(--text)}
.btn.main{background:var(--accent);color:#fff}
.btn:disabled{opacity:.4}
#view{position:fixed;inset:0;background:#000;z-index:20;display:none;flex-direction:column}
#view.open{display:flex}
#stage{flex:1;display:flex;align-items:center;justify-content:center;overflow:hidden}
#stage img,#stage video{max-width:100%;max-height:100%}
.vbar{display:flex;gap:8px;align-items:center;padding:10px 12px calc(10px + env(safe-area-inset-bottom));background:#0c0d12}
.vbar .grow{flex:1;color:var(--dim);font-size:13px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tip{color:var(--dim);font-size:12px;padding:0 12px 8px;background:#0c0d12;margin:0}
</style></head>
<body>
<header>
  <h1>PS5 Captures <small id="count"></small></h1>
  <div class="bar">
    <button class="chip on" data-k="all">All</button>
    <button class="chip" data-k="p">Screenshots</button>
    <button class="chip" data-k="v">Videos</button>
    <button class="chip" id="gamebtn">Every game &#9662;</button>
  </div>
</header>
<main id="list"><p class="empty">Loading...</p></main>
<div class="foot">
  <div class="grow" id="status">Tap a picture to open it</div>
  <button class="btn" id="selbtn">Select</button>
  <button class="btn main" id="dlbtn" style="display:none" disabled>Download</button>
</div>
<form id="zip" method="post" action="/zip" style="display:none"><input type="hidden" name="ids" id="zipids"></form>
<div id="picker"><div class="top"><h2>Pick a game</h2><button class="btn" id="pclose">&#10005;</button></div><div id="games"></div></div>
<div id="view">
  <div id="stage"></div>
  <p class="tip" id="tip"></p>
  <div class="vbar">
    <button class="btn" id="prev">&#8249;</button>
    <div class="grow" id="vname"></div>
    <a class="btn main" id="save" href="#">Save</a>
    <button class="btn" id="next">&#8250;</button>
    <button class="btn" id="close">&#10005;</button>
  </div>
</div>
<script>
let items=[],games={},kind="all",game="",shown=[],cur=-1,selecting=false,picked=new Set();
const $=s=>document.querySelector(s);
const ios=/iPhone|iPad|iPod/.test(navigator.userAgent);
function day(m){const d=new Date(m*1000);return d.toLocaleDateString(undefined,{weekday:"long",day:"numeric",month:"long",year:"numeric"})}
function time(m){return new Date(m*1000).toLocaleTimeString(undefined,{hour:"2-digit",minute:"2-digit"})}
function size(s){return s>1e9?(s/1e9).toFixed(1)+" GB":s>1e6?(s/1e6).toFixed(1)+" MB":Math.round(s/1e3)+" KB"}
function enc(id){return id.split("/").map(encodeURIComponent).join("/")}
async function load(){
  const r=await fetch("/api/list",{cache:"no-store"});const j=await r.json();items=j.items;games=j.games;
  draw();
}
function draw(){
  shown=items.filter(i=>(kind=="all"||i.t==kind)&&(!game||i.g==game));
  const p=items.filter(i=>i.t=="p").length,v=items.length-p;
  $("#count").textContent=p+" screenshots · "+v+" videos";
  const list=$("#list");list.innerHTML="";
  if(!shown.length){list.innerHTML='<p class="empty">Nothing here yet.</p>';return}
  let lastDay="",grid=null;
  shown.forEach((it,i)=>{
    const d=day(it.m);
    if(d!=lastDay){lastDay=d;const h=document.createElement("div");h.className="day";h.textContent=d;list.appendChild(h);grid=document.createElement("div");grid.className="grid";list.appendChild(grid)}
    const c=document.createElement("div");c.className="cell"+(picked.has(it.id)?" picked":"");
    c.innerHTML='<img loading="lazy" src="/t/'+enc(it.id)+'" alt="">'+(it.t=="v"?'<span class="play">&#9654; video</span>':"")+'<span class="tick"></span>';
    c.onclick=()=>selecting?toggle(it,c):open(i);
    grid.appendChild(c);
  });
}
function toggle(it,c){picked.has(it.id)?picked.delete(it.id):picked.add(it.id);c.classList.toggle("picked");upd()}
function upd(){
  let s=0;items.forEach(i=>{if(picked.has(i.id))s+=i.s});
  $("#dlbtn").disabled=!picked.size;$("#dlbtn").textContent=picked.size?"Download "+picked.size:"Download";
  $("#status").textContent=selecting?(picked.size?picked.size+" picked · "+size(s):"Tap pictures to pick them"):"Tap a picture to open it";
}
function open(i){
  cur=i;const it=shown[i];const st=$("#stage");st.innerHTML="";
  if(it.t=="v"){const v=document.createElement("video");v.src="/f/"+enc(it.id);v.controls=true;v.playsInline=true;v.autoplay=true;st.appendChild(v)}
  else{const im=document.createElement("img");im.src="/f/"+enc(it.id);st.appendChild(im)}
  $("#vname").textContent=(games[it.g]||it.g)+" · "+time(it.m)+" · "+size(it.s);
  $("#save").href="/f/"+enc(it.id)+"?dl=1";
  $("#tip").textContent=ios?(it.t=="v"?"Save, then in the download open it and tap Share → Save Video.":"Tip: press and hold the picture → Save to Photos."):"";
  $("#view").classList.add("open");
}
function closeView(){$("#view").classList.remove("open");$("#stage").innerHTML=""}
$("#close").onclick=closeView;
$("#prev").onclick=()=>{if(cur>0)open(cur-1)};
$("#next").onclick=()=>{if(cur<shown.length-1)open(cur+1)};
let sx=0;$("#stage").addEventListener("touchstart",e=>{sx=e.touches[0].clientX},{passive:true});
$("#stage").addEventListener("touchend",e=>{const dx=e.changedTouches[0].clientX-sx;if(Math.abs(dx)>60){dx<0?$("#next").onclick():$("#prev").onclick()}});
document.querySelectorAll(".chip[data-k]").forEach(b=>b.onclick=()=>{document.querySelectorAll(".chip[data-k]").forEach(x=>x.classList.remove("on"));b.classList.add("on");kind=b.dataset.k;draw()});
function esc(t){return String(t).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]))}
function pickGame(g){game=g;$("#gamebtn").innerHTML=esc(g?(games[g]||g):"Every game")+" &#9662;";$("#picker").classList.remove("open");draw()}
$("#gamebtn").onclick=()=>{
  // the games with captures, the one played last first, each with its icon and how many there are
  const last={},count={};items.forEach(i=>{count[i.g]=(count[i.g]||0)+1;if(!(i.g in last))last[i.g]=i.m});
  const order=Object.keys(count).sort((a,b)=>last[b]-last[a]);
  let h='<button class="gt'+(game?"":" on")+'" data-g=""><div class="all">&#9638;</div><b>Every game</b><i>'+items.length+'</i></button>';
  order.forEach(g=>{h+='<button class="gt'+(g==game?" on":"")+'" data-g="'+esc(g)+'"><img loading="lazy" src="/icon/'+encodeURIComponent(g)+'" onerror="this.outerHTML=\'<div class=all>&#127918;</div>\'"><b>'+esc(games[g]||g)+'</b><i>'+count[g]+'</i></button>'});
  $("#games").innerHTML=h;$("#games").querySelectorAll(".gt").forEach(b=>b.onclick=()=>pickGame(b.dataset.g));
  $("#picker").classList.add("open");
};
$("#pclose").onclick=()=>$("#picker").classList.remove("open");
$("#selbtn").onclick=()=>{selecting=!selecting;document.body.classList.toggle("sel",selecting);$("#selbtn").textContent=selecting?"Cancel":"Select";$("#dlbtn").style.display=selecting?"":"none";if(!selecting){picked.clear();draw()}upd()};
$("#dlbtn").onclick=()=>{if(picked.size==1){const id=[...picked][0];location.href="/f/"+enc(id)+"?dl=1";return}$("#zipids").value=[...picked].join("\n");$("#zip").submit()};
load();setInterval(()=>{if(!$("#view").classList.contains("open")&&!selecting)load()},15000);
</script>
</body></html>
)HTML"
