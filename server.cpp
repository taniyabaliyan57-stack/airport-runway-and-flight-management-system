// Airport Runway & Flight Management System - GUI edition (Team: THE VISIONARIES)
// macOS Compatible Build: g++ -std=c+11 server.cpp -o server

#include <iostream>  //Standard Library headers
#include <string>
#include <queue>
#include <stack>
#include <vector>
#include <map>
#include <sstream>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <sys/socket.h>  //POSIX socket headers for the network server
#include <netinet/in.h>
#include <unistd.h>

typedef int SOCKET;     //Socket compatability: SOCKET is a plain int in macOS/Linux
#define closesocket close

using namespace std;

const int MAX_FLIGHTS = 40;
const int RUNWAYS     = 2;
const int PORT        = 8080;

enum Status { PENDING = 0, QUEUED, ON_RUNWAY, DONE };

struct Flight {
    int idx = -1; string number, name; char type = 'A'; string time;
    int fuel = 0, distance = 0, priority = 4, entryOrder = 0, runway = 0;
    Status status = PENDING;
};

struct ComparePriority {
    bool operator()(const Flight& a, const Flight& b) const {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.fuel     != b.fuel)     return a.fuel     > b.fuel;
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.entryOrder > b.entryOrder;
    }
};
//Global state: all flights,counter,and the simulated clock(start at 540 min = 9:00 AM)
Flight flights[MAX_FLIGHTS];
int flightCount = 0, entryCounter = 0, tickNo = 0, nextRunway = 0, clockMin = 540;
priority_queue<Flight, vector<Flight>, ComparePriority> pq;  //pq: waiting flights , most urgent first (uses ComparePriority)
queue<Flight> runwayQueue;
stack<Flight> completed;
bool runwayBusy[RUNWAYS] = { false, false };
Flight runwayFlight[RUNWAYS];
vector<string> logs;

void logMsg(const string& s) { logs.push_back(s); if (logs.size() > 40) logs.erase(logs.begin()); }

string priorityName(int p) {
    switch (p) { case 1: return "EMERGENCY"; case 2: return "LOW FUEL"; case 3: return "VIP"; default: return "NORMAL"; }
}
string statusText(const Flight& f) {
    switch (f.status) {
        case PENDING:   return "Waiting";
        case QUEUED:    return "In runway queue";
        case ON_RUNWAY: return f.type == 'A' ? "LANDING" : "TAKING OFF";
        default:        return f.type == 'A' ? "Landed" : "Departed";
    }
}
int findFlight(const string& n) { for (int i = 0; i < flightCount; i++) if (flights[i].number == n) return i; return -1; }
bool validTime(const string& t) {
    if (t.size() != 5 || t[2] != ':') return false;
    for (int i : {0, 1, 3, 4}) if (!isdigit((unsigned char)t[i])) return false;
    return stoi(t.substr(0, 2)) < 24 && stoi(t.substr(3, 2)) < 60;
}
bool validNumber(const string& n) {
    if (n.size() < 3 || n.size() > 7) return false;
    for (char c : n) if (!isalnum((unsigned char)c)) return false;
    return true;
}
bool addFlight(Flight f, int slot = -1) {
    if (slot < 0) { if (flightCount >= MAX_FLIGHTS) return false; slot = flightCount++; }
    if (f.fuel < 15 && f.priority > 2) f.priority = 2;
    f.idx = slot; f.entryOrder = entryCounter++; f.status = PENDING; f.runway = 0;
    flights[slot] = f; pq.push(f);
    return true;
}
void rebuildPQ() {
    pq = priority_queue<Flight, vector<Flight>, ComparePriority>();
    for (int i = 0; i < flightCount; i++) if (flights[i].status == PENDING) pq.push(flights[i]);
}

void actAdd(map<string, string>& q) {
    Flight f; string n = q["n"];
    for (char& c : n) c = toupper((unsigned char)c);
    if (!validNumber(n))      { logMsg("ERROR: flight number must be 3-7 letters/digits."); return; }
    if (findFlight(n) >= 0)   { logMsg("ERROR: flight " + n + " already exists."); return; }
    if (q["name"].empty() || q["name"].size() > 19) { logMsg("ERROR: name must be 1-19 characters."); return; }
    if (!validTime(q["time"])) { logMsg("ERROR: time must be HH:MM."); return; }
    f.number = n; f.name = q["name"]; f.type = q["type"] == "D" ? 'D' : 'A'; f.time = q["time"];
    f.fuel = atoi(q["fuel"].c_str()); f.distance = atoi(q["dist"].c_str()); f.priority = atoi(q["pr"].c_str());
    if (f.fuel < 0 || f.fuel > 100 || f.distance < 0 || f.distance > 20000 || f.priority < 1 || f.priority > 4)
        { logMsg("ERROR: fuel 0-100, distance 0-20000, priority 1-4."); return; }
    if (!addFlight(f)) { logMsg("ERROR: flight list is full (40)."); return; }
    logMsg("Flight " + n + " added (" + priorityName(flights[flightCount - 1].priority) + ").");
}

void actSamples() {
    struct S { const char *no, *name; char type; const char* time; int fuel, dist, pr; };
    static const S d[20] = {
        {"AI101","Air India",'A',"09:10",42,820,4},   {"6E202","IndiGo",'D',"09:15",80,300,4},
        {"VT303","VIP Charter",'A',"09:20",55,1200,3},{"UK404","Vistara",'A',"09:25",12,950,2},
        {"SG505","SpiceJet",'D',"09:30",70,600,4},    {"AM606","Air Ambulance",'A',"09:35",30,400,1},
        {"EK510","Emirates",'A',"09:40",48,2200,4},   {"QP707","Akasa Air",'D',"09:45",90,450,4},
        {"AX808","Air India Express",'A',"09:50",35,700,4},{"LH761","Lufthansa",'A',"09:55",22,5900,4},
        {"6E909","IndiGo",'D',"10:00",65,520,4},      {"GA111","Garuda Indonesia",'A',"10:05",14,4200,2},
        {"VT212","Govt VIP Flight",'D',"10:10",88,350,3},{"BA256","British Airways",'A',"10:15",40,6700,4},
        {"SQ423","Singapore Airlines",'D',"10:20",75,4100,4},{"UK515","Vistara",'D',"10:25",60,880,4},
        {"MD909","Medevac Jet",'D',"10:30",50,150,1}, {"SG616","SpiceJet",'A',"10:35",28,640,4},
        {"QR577","Qatar Airways",'A',"10:40",18,3300,2},{"AI717","Air India",'D',"10:45",82,1100,4}
    };
    int added = 0;
    for (int i = 0; i < 20; i++) {
        if (findFlight(d[i].no) >= 0) continue;
        Flight f; f.number = d[i].no; f.name = d[i].name; f.type = d[i].type; f.time = d[i].time;
        f.fuel = d[i].fuel; f.distance = d[i].dist; f.priority = d[i].pr;
        if (!addFlight(f)) break;
        added++;
    }
    logMsg(to_string(added) + " sample flight(s) loaded.");
}

void actEmergency(map<string, string>& q) {
    string n = q["n"];
    for (char& c : n) c = toupper((unsigned char)c);
    int i = findFlight(n);
    if (i < 0) { logMsg("ERROR: no such flight."); return; }
    if (flights[i].status != PENDING) { logMsg("ERROR: " + n + " is already being processed."); return; }
    if (flights[i].priority == 1) { logMsg(n + " is already an emergency."); return; }
    flights[i].priority = 1; rebuildPQ();
    logMsg("EMERGENCY declared for " + n + " - moved to front of queue.");
}

bool workLeft() {
    if (!pq.empty() || !runwayQueue.empty()) return true;
    for (int r = 0; r < RUNWAYS; r++) if (runwayBusy[r]) return true;
    return false;
}

void tick() {
    ++tickNo; clockMin += 5;
    logMsg("--- Tick " + to_string(tickNo) + " ---");
    for (int r = 0; r < RUNWAYS; r++) {
        if (!runwayBusy[r]) continue;
        Flight& f = flights[runwayFlight[r].idx];
        f.status = DONE; completed.push(f);
        logMsg("Runway " + to_string(r + 1) + ": " + f.number + (f.type == 'A' ? " landed" : " departed"));
        runwayBusy[r] = false;
    }
    while ((int)runwayQueue.size() < RUNWAYS && !pq.empty()) {
        Flight f = pq.top(); pq.pop();
        flights[f.idx].status = QUEUED; runwayQueue.push(f);
    }
    for (int k = 0; k < RUNWAYS && !runwayQueue.empty(); k++) {
        int r = (nextRunway + k) % RUNWAYS;
        if (runwayBusy[r]) continue;
        Flight f = runwayQueue.front(); runwayQueue.pop();
        flights[f.idx].status = ON_RUNWAY; flights[f.idx].runway = r + 1;
        runwayFlight[r] = flights[f.idx]; runwayBusy[r] = true;
        logMsg("Runway " + to_string(r + 1) + ": " + f.number + (f.type == 'A' ? " is LANDING [" : " is TAKING OFF [") + priorityName(f.priority) + "]");
    }
    nextRunway = (nextRunway + 1) % RUNWAYS;
}

void actTick(bool silent) { if (workLeft()) tick(); else if (!silent) logMsg("Nothing to process."); }

void actRandom() {
    static const char* AL[9][2] = {{"AI","Air India"},{"6E","IndiGo"},{"UK","Vistara"},{"EK","Emirates"},{"QR","Qatar Airways"},
                                   {"SG","SpiceJet"},{"BA","British Airways"},{"SQ","Singapore Air"},{"LH","Lufthansa"}};
    int slot = -1;
    if (flightCount >= MAX_FLIGHTS) {
        for (int i = 0; i < flightCount; i++) if (flights[i].status == DONE) { slot = i; break; }
        if (slot < 0) return;
        vector<Flight> keep;
        while (!completed.empty()) { if (completed.top().number != flights[slot].number) keep.push_back(completed.top()); completed.pop(); }
        for (int i = (int)keep.size() - 1; i >= 0; i--) completed.push(keep[i]);
    }
    int a = rand() % 9; string n;
    do n = string(AL[a][0]) + to_string(100 + rand() % 899); while (findFlight(n) >= 0);
    Flight f; f.number = n; f.name = AL[a][1]; f.type = rand() % 2 ? 'A' : 'D';
    int m = (clockMin + rand() % 40) % 1440; char b[8]; snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60); f.time = b;
    f.fuel = 8 + rand() % 88; f.distance = 100 + rand() % 6000;
    int r = rand() % 100; f.priority = r < 5 ? 1 : r < 15 ? 3 : 4;
    addFlight(f, slot);
    logMsg("New flight " + n + " (" + f.name + ") entered the system.");
}

void actReset() {
    flightCount = entryCounter = tickNo = nextRunway = 0; clockMin = 540;
    pq = priority_queue<Flight, vector<Flight>, ComparePriority>();
    runwayQueue = queue<Flight>(); completed = stack<Flight>();
    for (int r = 0; r < RUNWAYS; r++) runwayBusy[r] = false;
    for (int i = 0; i < MAX_FLIGHTS; i++) flights[i] = Flight();
    logs.clear(); logMsg("System reset.");
}

string esc(const string& s) {
    string o;
    for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 32) o += ' '; else o += c; }
    return o;
}
string q(const string& s) { return "\"" + esc(s) + "\""; }

string stateJson() {
    ostringstream o;
    o << "{\"tick\":" << tickNo << ",\"clock\":" << clockMin << ",\"max\":" << MAX_FLIGHTS << ",\"flights\":[";
    for (int i = 0; i < flightCount; i++) {
        const Flight& f = flights[i];
        o << (i ? "," : "") << "{\"n\":" << q(f.number) << ",\"name\":" << q(f.name) << ",\"type\":" << q(string(1, f.type))
          << ",\"time\":" << q(f.time) << ",\"fuel\":" << f.fuel << ",\"dist\":" << f.distance << ",\"p\":" << f.priority
          << ",\"r\":" << f.runway << ",\"st\":" << f.status << ",\"s\":" << q(statusText(f)) << "}";
    }
    o << "],\"pq\":[";
    auto c = pq; bool first = true;
    while (!c.empty()) { o << (first ? "" : ",") << q(c.top().number); first = false; c.pop(); }
    o << "],\"rq\":[";
    auto rq = runwayQueue; first = true;
    while (!rq.empty()) { o << (first ? "" : ",") << q(rq.front().number); first = false; rq.pop(); }
    o << "],\"runways\":[";
    for (int r = 0; r < RUNWAYS; r++) o << (r ? "," : "") << q(runwayBusy[r] ? runwayFlight[r].number : "");
    o << "],\"done\":[";
    auto cp = completed; first = true;
    while (!cp.empty()) { o << (first ? "" : ",") << q(cp.top().number); first = false; cp.pop(); }
    o << "],\"log\":[";
    for (size_t i = 0; i < logs.size(); i++) o << (i ? "," : "") << q(logs[i]);
    o << "]}";
    return o.str();
}

const char *PAGE = R"HTML(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>Airport Control · THE VISIONARIES</title>
<style>
:root{--bg:#060b16;--pn:rgba(17,28,50,.78);--ln:#22344f;--tx:#e4edf9;--mu:#8097b6;--ac:#ffb81f;--red:#ff4d5e;--amb:#ff9d1a;--vio:#a98bff;--blu:#5db7ff;--grn:#2ee59d;color-scheme:dark;box-sizing:border-box}
*{box-sizing:border-box}
body{margin:0;background:radial-gradient(1200px 600px at 20% -10%,#12264a 0,transparent 60%),radial-gradient(900px 500px at 100% 0,#2a1a45 0,transparent 55%),var(--bg);color:var(--tx);font:14px/1.4 system-ui,"Segoe UI",sans-serif;min-height:100vh}
header{display:flex;flex-wrap:wrap;gap:10px;justify-content:space-between;align-items:center;padding:14px 20px}
h1{margin:0;font-size:19px;letter-spacing:2px;font-weight:700}h1 b{color:var(--ac)}h1 small{display:block;font-size:11px;letter-spacing:3px;color:var(--mu);font-weight:400}
.clock{font:600 26px ui-monospace,Consolas,monospace;color:var(--ac);text-shadow:0 0 18px rgba(255,184,31,.5)}
.live{display:inline-flex;align-items:center;gap:6px;color:var(--grn);font-size:12px;letter-spacing:2px}.live i{width:8px;height:8px;border-radius:50%;background:var(--grn);box-shadow:0 0 10px var(--grn);animation:pu 1.2s infinite}
@keyframes pu{50%{opacity:.25}}
.stats{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;padding:0 20px}
.st{background:var(--pn);border:1px solid var(--ln);border-radius:12px;padding:10px 14px;backdrop-filter:blur(8px)}
.st span{display:block;color:var(--mu);font-size:11px;letter-spacing:1.5px;text-transform:uppercase}.st b{font-size:26px}
.wrap{display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:14px;padding:14px 20px 24px}
.card{background:var(--pn);border:1px solid var(--ln);border-radius:14px;padding:12px;margin-bottom:14px;backdrop-filter:blur(8px)}
.card h2{margin:0 0 10px;font-size:12px;letter-spacing:2px;color:var(--ac);text-transform:uppercase}
canvas{width:100%;display:block;border-radius:10px;border:1px solid var(--ln)}
.two{display:grid;grid-template-columns:1fr 1fr;gap:14px}.two .card{margin:0}
.sc{overflow-x:auto}
table{width:100%;border-collapse:collapse;font:12.5px ui-monospace,Consolas,monospace;min-width:430px}
th{text-align:left;color:var(--mu);font-weight:400;font-size:11px;padding:4px 6px;border-bottom:1px solid var(--ln)}
td{padding:5px 6px;border-bottom:1px solid #172540;color:#ffd666;white-space:nowrap}
tr.go td{background:rgba(46,229,157,.1);animation:pu 1s infinite}tr.dn td{opacity:.45}
.b{padding:1px 7px;border-radius:99px;font:600 10px system-ui;color:#fff}.p1{background:var(--red)}.p2{background:#d97a00}.p3{background:#7b4ee8}.p4{background:#35597f}
label{display:block;margin:7px 0 3px;color:var(--mu);font-size:11px;letter-spacing:1px}
input,select{width:100%;padding:8px;background:#0a1222;color:var(--tx);border:1px solid #2a3f60;border-radius:8px;font:inherit}
button{padding:8px 12px;border:0;border-radius:8px;background:linear-gradient(135deg,#ffc93c,#ff9d1a);color:#201300;font-weight:700;cursor:pointer}
button:hover{filter:brightness(1.1)}button.s{background:#27395a;color:var(--tx)}button.r{background:var(--red);color:#fff}button.on{background:var(--grn);color:#02281b}
.row{display:flex;gap:8px;margin-top:8px}.row>*{flex:1}.g2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.chips span{display:inline-block;margin:2px;padding:3px 8px;background:#15233d;border:1px solid var(--ln);border-radius:6px;font:12px ui-monospace,monospace}
#log{height:160px;overflow:auto;font:12px ui-monospace,Consolas,monospace;background:#070e1c;padding:8px;border-radius:8px;color:#9fe8c8}
@media(max-width:1050px){.wrap{grid-template-columns:1fr}.two{grid-template-columns:1fr}.stats{grid-template-columns:repeat(2,1fr)}}
</style></head><body>
<header><h1><b>AIRPORT</b> RUNWAY &amp; FLIGHT MANAGEMENT<small>TEAM · THE VISIONARIES</small></h1><div><span class="live"><i></i>LIVE</span></div><div class="clock" id="clk">09:00</div></header>
<div class="stats"><div class="st"><span>Total flights</span><b id="s0">0</b></div><div class="st"><span>Waiting</span><b id="s1">0</b></div><div class="st"><span>On runway</span><b id="s2">0</b></div><div class="st"><span>Completed</span><b id="s3">0</b></div><div class="st"><span>Emergencies</span><b id="s4" style="color:var(--red)">0</b></div></div>
<div class="wrap"><div>
<div class="card"><h2>Live runway view</h2><canvas id="cv" height="200"></canvas></div>
<div class="two" style="margin-bottom:14px"><div class="card"><h2>✈ Arrivals</h2><div class="sc" id="arr"></div></div><div class="card"><h2>✈ Departures</h2><div class="sc" id="dep"></div></div></div>
<div class="two"><div class="card"><h2>Priority queue (next first)</h2><div class="chips" id="pqv"></div><h2 style="margin-top:12px">Runway queue</h2><div class="chips" id="rqv"></div></div><div class="card"><h2>Completed stack (latest on top)</h2><div class="chips" id="dnv"></div></div></div>
</div><div>
<div class="card"><h2>Control panel</h2><div class="row"><button onclick="tick()">Next tick</button><button id="ab" class="s" onclick="tAuto()">Auto run: OFF</button></div>
<div class="row"><button id="lb" class="s" onclick="tLive()">Live traffic: OFF</button></div>
<label>SPEED · <span id="spv">4.0</span> s per tick</label><input type="range" id="sp" min="1.5" max="8" step=".5" value="4" oninput="spv.textContent=(+this.value).toFixed(1)">
<div class="row"><button class="s" onclick="samples()">Load 20 samples</button><button class="r" onclick="reset()">Reset</button></div></div>
<div class="card"><h2>Add flight</h2><label>FLIGHT NUMBER</label><input id="fn" placeholder="AI101"><label>AIRLINE</label><input id="fa" placeholder="Air India">
<div class="g2"><div><label>TYPE</label><select id="ft"><option value="A">Arrival</option><option value="D">Departure</option></select></div><div><label>TIME</label><input id="fti" type="time" value="09:00"></div><div><label>FUEL %</label><input id="ff" type="number" value="50" min="0" max="100"></div><div><label>DISTANCE KM</label><input id="fd" type="number" value="500" min="0" max="20000"></div></div>
<label>PRIORITY</label><select id="fp"><option value="4">Normal</option><option value="3">VIP</option><option value="2">Low fuel</option><option value="1">Medical emergency</option></select><div class="row"><button onclick="addUI()">Add flight</button></div></div>
<div class="card"><h2>Declare emergency</h2><div class="row"><select id="em"></select><button class="r" onclick="emerg(em.value)">🚨 Declare</button></div></div>
<div class="card"><h2>System log</h2><div id="log"></div></div></div></div>
<script>
const PN=['','EMERGENCY','LOW FUEL','VIP','NORMAL'],COL=['','#ff4d5e','#ff9d1a','#a98bff','#5db7ff'],$=id=>document.getElementById(id);
let S={flights:[],pq:[],rq:[],runways:['',''],done:[],log:[],tick:0,max:40,clock:540},fl=[],pqL=[],rq=[],done=[],run=[null,null],runAt=[0,0],runT=[4000,4000],ghosts=[],auto=null,liveT=null,prev=[null,null];
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const hm=m=>{m%=1440;return String(Math.floor(m/60)).padStart(2,'0')+':'+String(m%60).padStart(2,'0')};
const speed=()=>+$('sp').value*1000;

async function api(p,qs){
    try{
        const r=await fetch('/api/'+p+(qs?'?'+new URLSearchParams(qs):''));
        apply(await r.json());
    }catch(e){
        $('log').textContent='Cannot reach backend. Is server running?';
    }
}

function apply(d){
    const now=performance.now(),F=n=>d.flights.find(f=>f.n===n),rs=d.tick<S.tick,nr=d.runways.map(n=>n?F(n):null);
    for(let r=0;r<2;r++){
        const o=prev[r],n=nr[r];
        if(o&&(!n||n.n!==o.n)&&!rs)ghosts.push({f:o,t0:now,T:speed()});
        if(n&&(!o||o.n!==n.n)){runAt[r]=now;runT[r]=speed();}
    }
    if(rs)ghosts=[];
    prev=nr;S=d;fl=d.flights;pqL=d.pq.map(F).filter(Boolean);rq=d.rq.map(F).filter(Boolean);done=d.done.map(F).filter(Boolean);run=nr;
    render();
}

function tick(){api('tick');}
function samples(){api('samples');}
function reset(){api('reset');}
function emerg(n){if(n)api('emergency',{n});}
function addUI(){
    const n=$('fn').value.trim(),name=$('fa').value.trim(),type=$('ft').value,time=$('fti').value,fuel=$('ff').value,dist=$('fd').value,pr=$('fp').value;
    if(!n||!name){alert('Flight number & Airline required');return;}
    api('add',{n,name,type,time,fuel,dist,pr});
    $('fn').value='';$('fa').value='';
}
function tAuto(){
    if(auto){clearInterval(auto);auto=null;$('ab').textContent='Auto run: OFF';$('ab').classList.remove('on');}
    else{auto=setInterval(tick,speed());$('ab').textContent='Auto run: ON';$('ab').classList.add('on');}
}
function tLive(){
    if(liveT){clearInterval(liveT);liveT=null;$('lb').textContent='Live traffic: OFF';$('lb').classList.remove('on');}
    else{liveT=setInterval(()=>api('random'),speed()*1.5);$('lb').textContent='Live traffic: ON';$('lb').classList.add('on');}
}

function table(type){
    const items=fl.filter(f=>f.type===type);
    let h='<table><thead><tr><th>FLIGHT</th><th>AIRLINE</th><th>TIME</th><th>FUEL</th><th>DIST</th><th>PRIORITY</th><th>STATUS</th></tr></thead><tbody>';
    for(const f of items){
        const cls=f.st===2?'go':(f.st===3?'dn':'');
        h+=`<tr class="${cls}"><td><b>${esc(f.n)}</b></td><td>${esc(f.name)}</td><td>${esc(f.time)}</td><td>${f.fuel}%</td><td>${f.dist}km</td><td><span class="b p${f.p}">${PN[f.p]}</span></td><td>${esc(f.s)}</td></tr>`;
    }
    return h+'</tbody></table>';
}

function render(){
    $('clk').textContent=hm(S.clock);$('s0').textContent=fl.length;
    $('s1').textContent=fl.filter(f=>f.st===0||f.st===1).length;
    $('s2').textContent=run.filter(Boolean).length;
    $('s3').textContent=done.length;
    $('s4').textContent=fl.filter(f=>f.p===1&&f.st<3).length;

    $('arr').innerHTML=table('A');$('dep').innerHTML=table('D');

    $('pqv').innerHTML=pqL.map(f=>`<span style="color:${COL[f.p]}">${esc(f.n)}</span>`).join('')||'<i>None</i>';
    $('rqv').innerHTML=rq.map(f=>`<span>${esc(f.n)}</span>`).join('')||'<i>None</i>';
    $('dnv').innerHTML=done.map(f=>`<span>${esc(f.n)}</span>`).join('')||'<i>None</i>';

    const emOpts=fl.filter(f=>f.st===0&&f.p>1).map(f=>`<option value="${esc(f.n)}">${esc(f.n)} (${esc(f.name)})</option>`);
    $('em').innerHTML=emOpts.join('')||'<option value="">None available</option>';

    $('log').innerHTML=S.log.slice().reverse().map(l=>`<div>${esc(l)}</div>`).join('');
}

const cv=$('cv'),ctx=cv.getContext('2d');
function draw(){
    cv.width=cv.clientWidth;cv.height=200;
    ctx.fillStyle='#08101e';ctx.fillRect(0,0,cv.width,cv.height);

    for(let r=0;r<2;r++){
        const y=50+r*90;
        ctx.fillStyle='#14233a';ctx.fillRect(20,y,cv.width-40,40);
        ctx.strokeStyle='#304768';ctx.setLineDash([12,12]);ctx.strokeRect(20,y+19,cv.width-40,2);ctx.setLineDash([]);
        ctx.fillStyle='#5b7da8';ctx.font='11px monospace';ctx.fillText('RUNWAY 0'+(r+1),26,y+24);

        const f=run[r];
        if(f){
            const p=Math.min(1,(performance.now()-runAt[r])/runT[r]);
            const x=f.type==='A'?(cv.width-60)-(cv.width-100)*p:40+(cv.width-100)*p;
            ctx.fillStyle=COL[f.p];ctx.font='bold 13px system-ui';
            ctx.fillText((f.type==='A'?'🛬 ':'🛫 ')+f.n,x,y+25);
        }
    }
    requestAnimationFrame(draw);
}
requestAnimationFrame(draw);
api('state');
</script></body></html>
)HTML";

void sendResponse(SOCKET client, const string& contentType, const string& body) {
    ostringstream resp;
    resp << "HTTP/1.1 200 OK\r\n"
         << "Content-Type: " << contentType << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n\r\n"
         << body;
    string s = resp.str();
    send(client, s.c_str(), (int)s.size(), 0);
}

map<string, string> parseQuery(const string& qs) {
    map<string, string> q;
    stringstream ss(qs);
    string item;
    while (getline(ss, item, '&')) {
        auto pos = item.find('=');
        if (pos != string::npos) q[item.substr(0, pos)] = item.substr(pos + 1);
    }
    return q;
}

int main() {
    srand((unsigned)time(NULL));
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("Socket failed"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (::bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        return 1;
    }

    cout << "========================================" << endl;
    cout << " Server running on http://localhost:" << PORT << endl;
    cout << " Opening dashboard in your browser..." << endl;
    cout << "========================================" << endl;

    system("open http://localhost:8080");

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        SOCKET client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client < 0) continue;

        char buf[2048] = {0};
        recv(client, buf, sizeof(buf) - 1, 0);
        string req(buf);

        if (req.find("GET /api/") == 0) {
            size_t p1 = req.find("GET /api/") + 9;
            size_t p2 = req.find(" ", p1);
            string path = req.substr(p1, p2 - p1);

            string action = path;
            string qs = "";
            size_t qmark = path.find('?');
            if (qmark != string::npos) {
                action = path.substr(0, qmark);
                qs = path.substr(qmark + 1);
            }
            map<string, string> query = parseQuery(qs);

            if (action == "tick") actTick(false);
            else if (action == "samples") actSamples();
            else if (action == "reset") actReset();
            else if (action == "random") actRandom();
            else if (action == "add") actAdd(query);
            else if (action == "emergency") actEmergency(query);

            sendResponse(client, "application/json", stateJson());
        } else {
            sendResponse(client, "text/html", PAGE);
        }

        closesocket(client);
    }
    closesocket(server_fd);
    return 0;
}
