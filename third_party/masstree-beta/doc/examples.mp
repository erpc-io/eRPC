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
  label.lft(btex \small{(1)~~} etex, inj.w);
endfig;

end
