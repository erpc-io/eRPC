input masstree;
verbatimtex %&latex
\documentclass[12pt]{article}
\usepackage{elemfig,amsmath}
\begin{document}
\newcommand{\xlab}[1]{\mlabel{\textsc{#1}}}
etex;

picture data[];
data[0] = btex \xlab{a} etex; data[1] = btex \xlab{b} etex; data[2] = btex \xlab{c} etex;
data[3] = btex \xlab{d} etex; data[4] = btex \xlab{e} etex; data[5] = btex \xlab{f} etex;
data[6] = btex \xlab{g} etex; data[7] = btex \xlab{h} etex; data[8] = btex \xlab{i} etex;
data[9] = btex \xlab{j} etex; data[10] = btex \xlab{k} etex; data[11] = btex \xlab{l} etex;
data[12] = btex \xlab{m} etex; data[13] = btex \xlab{n} etex; data[14] = btex \xlab{o} etex;
picture dotdata;
dotdata = btex \xlab{\dots\dots} etex;

pair min_reasonable_cell; min_reasonable_cell := (0,0);
for _i_ = 0 upto 14:
  min_reasonable_cell :=
    (max(xpart min_reasonable_cell, xpart (urcorner data[_i_] - llcorner data[_i_])),
      max(ypart min_reasonable_cell, ypart (urcorner data[_i_] - llcorner data[_i_])));
endfor;
color deletedelement; deletedelement = .5white;
color upperlayer, upperlayerfill; upperlayer = (0,0,.5); upperlayerfill = (.9,.9,1);

picture emptyqueue; emptyqueue := btex \phantom{\elementlabel{Queue}} etex;
picture vemptyqueue; vemptyqueue := emptyqueue rotated 90 xscaled .4;
hardborderscale = 3;

beginfig(1);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], data[9]);
  ina.s = .5[a.nw,j.ne] + (0,24);
  .5[x.sw,x.se] = ina.n + (0,24);
  fixelement(a,d,g,j,ina,x);
  leafconnect(a,d,g,j);
  drawelement(a,d,g,j,ina);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (40,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} ina.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g,j);
endfig;

beginfig(2);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], nullpicture, nullpicture);
  j.locked = m.locked = true;
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], data[9]);
  ina.s = .5[a.nw,j.ne] + (0,24);
  .5[x.sw,x.se] = ina.n + (0,24);
  fixelement(a,d,g,j,m,ina,x);
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (40,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} ina.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g,j);
endfig;

beginfig(3);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], nullpicture, nullpicture);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  ina.locked = inj.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  .5[x.sw,x.se] = ina.n + (0,24);
  fixelement(a,d,g,j,m,ina,inj,x);
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (40,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} ina.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
endfig;

beginfig(4);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], nullpicture, nullpicture);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.sw = .3[ina.n,inj.n] + (0,24);
  .8[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g,j,m,root,ina,inj,x);
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (40,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} ina.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  ina.connectin := .6[ina.nw,ina.ne];
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(5);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], nullpicture, nullpicture);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.sw = .3[ina.n,inj.n] + (0,24);
  .5[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g,j,m,root,ina,inj,x);
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (40,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} root.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

end
