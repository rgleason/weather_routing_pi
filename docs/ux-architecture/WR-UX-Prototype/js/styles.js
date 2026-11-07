body{
font-family:Segoe UI,sans-serif;
margin:0;
background:#f4f7fa;
}

header{
padding:20px;
background:#0c4a6e;
color:white;
}

.tabs{
display:flex;
gap:10px;
padding:10px;
background:white;
}

.tab{
flex:1;
padding:14px;
border:none;
border-radius:8px;
cursor:pointer;
}

.tab.active{
background:#0c4a6e;
color:white;
}

.tab-content{
display:none;
padding:20px;
}

.tab-content.active{
display:block;
}

.layout{
display:grid;
grid-template-columns:240px 1fr;
gap:20px;
}

.sidebar,
.card{
background:white;
padding:20px;
border-radius:12px;
box-shadow:0 2px 8px rgba(0,0,0,.08);
}

.step{
padding:10px;
margin-bottom:10px;
border-radius:8px;
background:#f0f3f6;
}

.current{
background:#dbeafe;
}

.complete{
background:#dcfce7;
}

.wizard-screen{
display:none;
}

.wizard-screen.active{
display:block;
}

.button-row{
display:flex;
gap:10px;
margin-top:20px;
}

button{
padding:10px 18px;
cursor:pointer;
}

.progress-bar{
height:20px;
background:#e5e7eb;
border-radius:20px;
overflow:hidden;
}

.progress-fill{
width:0;
height:100%;
background:#22c55e;
transition:width .3s;
}

.chart{
width:100%;
height:420px;
border-radius:12px;
}

.wind-light{
fill:#60a5fa22;
stroke:#60a5fa;
}

.wind-moderate{
fill:#22c55e22;
stroke:#22c55e;
}

.wind-fresh{
fill:#facc1522;
stroke:#facc15;
}

.wind-strong{
fill:#f9731622;
stroke:#f97316;
}

.wind-heavy{
fill:#dc262622;
stroke:#dc2626;
}

.streamline{
fill:none;
stroke:#60a5fa;
stroke-width:2;
stroke-dasharray:8 8;
animation:streamflow 1.5s linear infinite;
}

@keyframes streamflow{
to{
stroke-dashoffset:-20;
}
}

.route{
stroke:#ef4444;
stroke-width:4;
stroke-dasharray:10 6;
animation:routeflow 2s linear infinite;
}

@keyframes routeflow{
to{
stroke-dashoffset:-30;
}
}

.wind-legend{
margin-top:15px;
display:flex;
gap:8px;
flex-wrap:wrap;
}

.legend-item.inactive{
opacity:.35;
}

.route-card{
display:none;
margin-top:20px;
background:white;
padding:20px;
border-radius:12px;
}

.metrics{
display:grid;
grid-template-columns:repeat(4,1fr);
gap:20px;
}

.value{
font-size:24px;
font-weight:bold;
color:#0c4a6e;
}

@media(max-width:900px){

.layout{
grid-template-columns:1fr;
}

}