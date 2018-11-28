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
  % tree with JKL removed
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.locked = j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(j,0,2) withcolor deletedelement;
  fill leafnextbpath(j) withcolor black;
  g.nextpath = g.next .. (j.nw + (0,3)) .. tension 1.5 .. (j.ne + (0,3)) .. (m.nextin + (0,1));
  j.prevpath = j.prev -- g.previn;
  j.nextpath = j.next -- m.nextin;
  m.prevpath = m.prev .. (j.se - (0,3)) .. tension 1.5 .. (j.sw - (0,3)) .. (g.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(2);
  % bad idea: tree with M in internal node removed
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(nullpicture, nullpicture, nullpicture);
  inj.locked = true;
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(j,0,2) withcolor deletedelement;
  fill leafnextbpath(j) withcolor black;
  g.nextpath = g.next .. (j.nw + (0,3)) .. tension 1.5 .. (j.ne + (0,3)) .. (m.nextin + (0,1));
  j.prevpath = j.prev -- g.previn;
  j.nextpath = j.next -- m.nextin;
  m.prevpath = m.prev .. (j.se - (0,3)) .. tension 1.5 .. (j.sw - (0,3)) .. (g.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,m);
  internalnodeconnect(root,ina,inj);
  label.rt(btex \textsf{\textbf{~~XXX}} etex, inj.e);
endfig;

beginfig(3);
  % tree with M stubbed out
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  inj.locked = true;
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(j,0,2) withcolor deletedelement;
  fill leafnextbpath(j) withcolor black;
  g.nextpath = g.next .. (j.nw + (0,3)) .. tension 1.5 .. (j.ne + (0,3)) .. (m.nextin + (0,1));
  j.prevpath = j.prev -- g.previn;
  j.nextpath = j.next -- m.nextin;
  m.prevpath = m.prev .. (j.se - (0,3)) .. tension 1.5 .. (j.sw - (0,3)) .. (g.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnectone(inj,m,1);
  drawterminationarrow(inj.child[0] -- (inj.child[0] - (0,5)));
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(4);
  % tree with M replacing J at the root
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[12], nullpicture, nullpicture);
  root.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(j,0,2) withcolor deletedelement;
  fill leafnextbpath(j) withcolor black;
  g.nextpath = g.next .. (j.nw + (0,3)) .. tension 1.5 .. (j.ne + (0,3)) .. (m.nextin + (0,1));
  j.prevpath = j.prev -- g.previn;
  j.nextpath = j.next -- m.nextin;
  m.prevpath = m.prev .. (j.se - (0,3)) .. tension 1.5 .. (j.sw - (0,3)) .. (g.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnectone(inj,m,1);
  drawterminationarrow(inj.child[0] -- (inj.child[0] - (0,5)));
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(5);
  % tree with MNO removed
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  m.locked = m.deleted = true;
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[12], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(m,0,2) withcolor deletedelement;
  fill leafnextbpath(m) withcolor black;
  m.prevpath = m.prev -- g.previn;
  drawelement(a,d,g,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnectone(inj,m,1);
  drawterminationarrow(inj.child[0] -- (inj.child[0] - (0,5)));
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(6);
  % tree with MNO removed and internal node removed
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  m.deleted = true;
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(nullpicture, nullpicture, nullpicture);
  inj.locked = inj.deleted = true;
  internalnodeit.root(data[12], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(m,0,2) withcolor deletedelement;
  fill leafnextbpath(m) withcolor black;
  m.prevpath = m.prev -- g.previn;
  drawelement(a,d,g,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  drawterminationarrow(inj.child[0] -- (inj.child[0] - (0,5)));
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(7);
  % tree with MNO removed and internal node removed
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  j.deleted = true;
  leafit.m(data[12], data[13], data[14]);
  m.deleted = true;
  boxjoin();
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.inj(nullpicture, nullpicture, nullpicture);
  inj.deleted = true;
  internalnodeit.root(nullpicture, nullpicture, nullpicture);
  root.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  leafconnect(a,d,g);
  fill nodecellsbpath(m,0,2) withcolor deletedelement;
  fill leafnextbpath(m) withcolor black;
  m.prevpath = m.prev -- g.previn;
  drawelement(a,d,g,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  drawterminationarrow(inj.child[0] -- (inj.child[0] - (0,5)));
  internalnodeconnect(root,ina);
endfig;

beginfig(8);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.root(nullpicture, nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  root.sw = ina.n + (0,24);
  .8[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g);
  fixelement(root,ina,x);
  leafconnect(a,d,g);
  drawelement(a,d,g,ina,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} root.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(root,ina);
endfig;

beginfig(9);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.root(nullpicture, nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  root.sw = ina.n + (0,24);
  .8[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g);
  fixelement(root,ina,x);
  leafconnect(a,d,g);
  drawelement(a,d,g,ina,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} root.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(root,ina);
endfig;

beginfig(11);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.root(nullpicture, nullpicture, nullpicture);
  root.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  root.sw = ina.n + (0,24);
  .8[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g);
  fixelement(root,ina,x);
  leafconnect(a,d,g);
  drawelement(a,d,g,ina,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} root.n withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(root,ina);
endfig;

beginfig(12);
  % tree with MNO removed, gclayer begin
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  internalnodeit.ina(data[3], data[6], nullpicture);
  internalnodeit.root(nullpicture, nullpicture, nullpicture);
  root.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  root.sw = ina.n + (0,24);
  .8[x.sw,x.se] = root.n + (0,24);
  fixelement(a,d,g);
  fixelement(root,ina,x);
  leafconnect(a,d,g);
  drawelement(a,d,g,ina,root);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} (ina.n - (4,0)) withpen connectionpen scaled 2 withcolor upperlayer;
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(root,ina);
endfig;

beginfig(15);
  % tree with MNO removed, gclayer begin
  leafit.a(nullpicture, nullpicture, nullpicture);
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  .5[x.sw,x.se] = a.n + (0,24);
  fixelement(a,x);
  leafconnect(a);
  drawelement(a,x);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} a.n withpen connectionpen scaled 2 withcolor upperlayer;
endfig;

beginfig(16);
  % tree with MNO removed, gclayer begin
  leafit.a(nullpicture, nullpicture, nullpicture);
  a.locked = true;
  boxjoin();
  leafit.x(dotdata, dotdata, dotdata, dotdata);
  x.locked = true;
  x.nextpath = x.prevpath = emptypath;
  .5[x.sw,x.se] = a.n + (0,24);
  fixelement(a,x);
  leafconnect(a);
  drawelement(a,x);
  fillelement(x)(upperlayerfill); drawelement(x) withcolor upperlayer;
  begingroup
    interim linecap := butt;
    draw (x.sw - (5,10)) -- (x.se + (40,-10)) withpen pencircle scaled 5 dashed evenly scaled 3 withcolor upperlayerfill;
  endgroup;
  drawconnarrow x.value[1] {down} .. tension 2 .. {down} a.n withpen connectionpen scaled 2 withcolor upperlayer;
endfig;

end
