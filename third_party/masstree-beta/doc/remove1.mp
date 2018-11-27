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

pair min_reasonable_cell; min_reasonable_cell := (0,0);
for _i_ = 0 upto 14:
  min_reasonable_cell :=
    (max(xpart min_reasonable_cell, xpart (urcorner data[_i_] - llcorner data[_i_])),
      max(ypart min_reasonable_cell, ypart (urcorner data[_i_] - llcorner data[_i_])));
endfor;
color deletedelement; deletedelement = .5white;

picture emptyqueue; emptyqueue := btex \phantom{\elementlabel{Queue}} etex;
picture vemptyqueue; vemptyqueue := emptyqueue rotated 90 xscaled .4;
hardborderscale = 3;

beginfig(1);
  % initial tree
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(2);
  % delete element E
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.locked = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  fill nodecellbpath(d,1) withcolor deletedelement;
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(3);
  % delete node DEF: poison next pointer
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.locked = d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  fill nodecellsbpath(d,0,2) withcolor deletedelement;
  fill leafnextbpath(d) withcolor black;
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(31);
  % delete node DEF: poison ABC's next pointer
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.locked = d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  fill nodecellsbpath(d,0,2) withcolor deletedelement;
  fill leafnextbpath(d) withcolor black;
  fill leafnextbpath(a) withcolor black;
  leafconnect(a,d,g,j,m);
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(4);
  % delete node DEF: adjust GHI.prev
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.locked = d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  fill nodecellsbpath(d,0,2) withcolor deletedelement;
  fill leafnextbpath(d) withcolor black;
  fill leafnextbpath(a) withcolor black;
  leafconnect(a,d);
  leafconnect(g,j,m);
  g.prevpath = g.prev .. (d.se - (0,3)) .. tension 1.5 .. (d.sw - (0,3)) .. (a.previn - (0,1));
  d.nextpath = d.next -- g.nextin;
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(5);
  % delete node DEF: adjust ABC.next
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.locked = d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
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
  fill nodecellsbpath(d,0,2) withcolor deletedelement;
  fill leafnextbpath(d) withcolor black;
  leafconnect(g,j,m);
  a.nextpath = a.next .. (d.nw + (0,3)) .. tension 1.5 .. (d.ne + (0,3)) .. (g.nextin + (0,1));
  d.prevpath = d.prev -- a.previn;
  d.nextpath = d.next -- g.nextin;
  g.prevpath = g.prev .. (d.se - (0,3)) .. tension 1.5 .. (d.sw - (0,3)) .. (a.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,d,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(6);
  % delete node DEF: adjust internal node
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[6], nullpicture, nullpicture);
  ina.locked = true;
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  fill nodecellsbpath(d,0,2) withcolor deletedelement;
  fill leafnextbpath(d) withcolor black;
  leafconnect(g,j,m);
  a.nextpath = a.next .. (d.nw + (0,3)) .. tension 1.5 .. (d.ne + (0,3)) .. (g.nextin + (0,1));
  d.prevpath = d.prev -- a.previn;
  d.nextpath = d.next -- g.nextin;
  g.prevpath = g.prev .. (d.se - (0,3)) .. tension 1.5 .. (d.sw - (0,3)) .. (a.previn - (0,1));
  drawelement(a,d,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(7);
  % delete node GHI: after leaf unlink
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  g.locked = g.deleted = true;
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(data[6], nullpicture, nullpicture);
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  fill nodecellsbpath(g,0,2) withcolor deletedelement;
  fill leafnextbpath(g) withcolor black;
  leafconnect(j,m);
  a.nextpath = a.next .. (g.nw + (0,3)) .. tension 1.5 .. (g.ne + (0,3)) .. (j.nextin + (0,1));
  g.prevpath = g.prev -- a.previn;
  g.nextpath = g.next -- j.nextin;
  j.prevpath = j.prev .. (g.se - (0,3)) .. tension 1.5 .. (g.sw - (0,3)) .. (a.previn - (0,2));
  drawelement(a,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a,g);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(8);
  % delete node GHI: after internal node
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  g.deleted = true;
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(nullpicture, nullpicture, nullpicture);
  ina.locked = true;
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  fill nodecellsbpath(g,0,2) withcolor deletedelement;
  fill leafnextbpath(g) withcolor black;
  leafconnect(j,m);
  a.nextpath = a.next .. (g.nw + (0,3)) .. tension 1.5 .. (g.ne + (0,3)) .. (j.nextin + (0,1));
  g.prevpath = g.prev -- a.previn;
  g.nextpath = g.next -- j.nextin;
  j.prevpath = j.prev .. (g.se - (0,3)) .. tension 1.5 .. (g.sw - (0,3)) .. (a.previn - (0,2));
  drawelement(a,g,j,m,ina,inj,root);
  internalnodeconnect(ina,a);
  internalnodeconnect(inj,j,m);
  internalnodeconnect(root,ina,inj);
endfig;

beginfig(9);
  % delete node GHI: after collapse
  boxjoin(b.w - a.e = (20,0));
  leafit.a(data[0], data[1], data[2]);
  leafit.d(data[3], data[4], data[5]);
  d.deleted = true;
  leafit.g(data[6], data[7], data[8]);
  g.deleted = true;
  leafit.j(data[9], data[10], data[11]);
  leafit.m(data[12], data[13], data[14]);
  boxjoin();
  internalnodeit.ina(nullpicture, nullpicture, nullpicture);
  ina.deleted = true;
  internalnodeit.inj(data[12], nullpicture, nullpicture);
  internalnodeit.root(data[9], nullpicture, nullpicture);
  root.locked = true;
  ina.s = .5[a.nw,g.ne] + (0,24);
  inj.s = .5[j.nw,m.ne] + (0,24);
  root.s = .5[ina.nw,inj.ne] + (0,24);
  fixelement(a,d,g,j,m);
  fixelement(root,ina,inj);
  fill nodecellsbpath(g,0,2) withcolor deletedelement;
  fill leafnextbpath(g) withcolor black;
  leafconnect(j,m);
  a.nextpath = a.next .. (g.nw + (0,3)) .. tension 1.5 .. (g.ne + (0,3)) .. (j.nextin + (0,1));
  g.prevpath = g.prev -- a.previn;
  g.nextpath = g.next -- j.nextin;
  j.prevpath = j.prev .. (g.se - (0,3)) .. tension 1.5 .. (g.sw - (0,3)) .. (a.previn - (0,2));
  drawelement(a,g,j,m,ina,inj,root);
  internalnodeconnect(inj,j,m);
  drawconnarrow ina.child[0] {down} .. tension 4 .. {down} (a.n + (3,0));
  drawconnarrow root.child[0] {down} .. tension 2 and 1 .. (ina.nw + (0,4)) .. {down} a.n;
  internalnodeconnectone(root,inj,1);
endfig;

end
