input patches;

def emptypath =
  subpath (0,0) of (0,0) -- (0,0)
enddef;

vardef clearnode_(suffix $) =
  if $.isleaf:
    _n_ := str $;
    generic_redeclare(numeric) _n.prev, _n.previn, _n.next, _n.nextin, _n.nextpath, _n.prevpath,
      _n.nextbpath, _n.prevbpath;
    _n_ := str $ & ".value0";
    generic_redeclare(numeric) _n;
  else:
    _n_ := str $ & ".child0";
    generic_redeclare(numeric) _n;
  fi
  _n_ := str $;
  generic_redeclare(numeric) _n.linkwidth, _n.nkeys, _n.deleted, _n.isleaf, _n.locked, _n.keywidth, _n.connectin;
  _n_ := str $ & ".keypic0";
  generic_redeclare(numeric) _n;
enddef;

vardef drawnode_(suffix $) text rest =
  save _i_, _w_, _pos_, _ll_; numeric _i_, _w_; pair _pos_, _ll_;
  _ll_ = $.sw + ($.linkwidth,0); _w_ = $.keywidth;
  for _i_ = 0 upto $.nkeys-1:
    if xpart (llcorner $.keypic[_i_] - urcorner $.keypic[_i_]) = 0:
      fill ((0,0) -- (_w_,0) -- (_w_,$.height) -- (0,$.height) -- cycle) shifted (_ll_ + (_w_*_i_,0)) withcolor .9white;
    fi
  endfor
  if $.locked:
    begingroup interim linecap := butt;
      draw ((0,-3) {left} .. (-3,0) {up} .. (0,3) {right} .. {down} (3,0)) shifted $.nw withpen pencircle scaled 2 rest;
    endgroup;
  fi
  drawboxes($) rest;
  if $.isleaf:
    draw ($.nw + ($.linkwidth,0)) -- ($.sw + ($.linkwidth,0)) withpen connectionpen rest;
    if known $.nextpath:
      drawarrow $.nextpath dashed withdots scaled 0.5 rest;
    else:
      draw $.ne -- ($.se - ($.linkwidth,0)) withpen connectionpen scaled 0.2 rest;
    fi
    draw ($.ne - ($.linkwidth,0)) -- ($.se - ($.linkwidth,0)) withpen connectionpen rest;
    if known $.prevpath:
      drawarrow $.prevpath dashed withdots scaled 0.5 rest;
    else:
      draw ($.nw + ($.linkwidth,0)) -- $.sw withpen connectionpen scaled 0.5 rest;
    fi
  fi
  for _i_ = 0 upto $.nkeys-1:
    _pos_ := .5[llcorner $.keypic[_i_],urcorner $.keypic[_i_]];
    draw $.keypic[_i_] shifted (_ll_ + (_w_*(_i_+0.5),.5*$.height) - _pos_) rest;
    if _i_ > 0:
      draw (_ll_ + (_w_*_i_,0)) -- (_ll_ + (_w_*_i_,.15*$.height)) withpen connectionpen scaled 2 rest;
      draw (_ll_ + (_w_*_i_,$.height)) -- (_ll_ + (_w_*_i_,.85*$.height)) withpen connectionpen scaled 2 rest;
    fi
  endfor
  if $.deleted:
    draw ($.nw + .1*($.se-$.nw)) -- ($.se - .1*($.se-$.nw))
      withpen penrazor scaled 5 rotated 60 withcolor white;
    draw ($.sw - .1*($.sw-$.ne)) -- ($.ne + .1*($.sw-$.ne))
      withpen penrazor scaled 5 rotated -60 withcolor white;
  fi
enddef;

vardef sizenode_(suffix $) =
  if unknown $.linkwidth: $.linkwidth = if $.isleaf: 5 else: 0 fi; fi
  if unknown $.deleted: $.deleted = false; fi
  if unknown $.locked: $.locked = false; fi
  if unknown $.dx or unknown $.dy:
    save _a_, _i_; pair _a_; _a_ := min_reasonable_cell;
    for _i_ = 0 upto $.nkeys-1:
      _a_ := (max(xpart _a_,xpart (urcorner $.keypic[_i_] - llcorner $.keypic[_i_])),
	  max(ypart _a_,ypart (urcorner $.keypic[_i_] - llcorner $.keypic[_i_])));
    endfor
    if unknown $.dx: $.dx = ($.linkwidth*2 + (2*xpart element_offset + xpart _a_)*$.nkeys)/2; fi
    if unknown $.dy: $.dy = (2*ypart element_offset + ypart _a_)/2; fi
  fi
  sizeelement_($)
enddef;

vardef leafit@#(text components) =
  _elementit.@#(nullpicture, 0, 0, push, false, false);
  _n_ := str @#;
  generic_declare(numeric) _n.linkwidth, _n.nkeys, _n.keywidth;
  generic_declare(boolean) _n.deleted, _n.isleaf, _n.locked;
  generic_declare(pair) _n.prev, _n.previn, _n.next, _n.nextin, _n.connectin;
  generic_declare(path) _n.prevpath, _n.nextpath, _n.nextbpath, _n.prevbpath;
  _n_ := str @# & ".keypic0";
  generic_declare(picture) _n;
  _n_ := str @# & ".value0";
  generic_declare(pair) _n;
  @#.isleaf = true;
  save _i_, _nk_; picture _i_; _nk_ := 0;
  for _i_ = components:
    @#.keypic[_nk_] = _i_;
    @#.value[_nk_] = @#.sw + (@#.linkwidth + @#.keywidth*(_nk_+0.5),.15*@#.height);
    _nk_ := _nk_ + 1;
  endfor
  elemdraw_@# := "drawnode_";
  sproc_@# := "sizenode_";
  @#.prev = @#.w + (@#.linkwidth/2,-@#.height/10);
  @#.previn = @#.e + (0,-@#.height/10);
  @#.next = @#.e + (-@#.linkwidth/2,@#.height/10);
  @#.nextin = @#.w + (0,@#.height/10);
  @#.nkeys = _nk_;
  @#.keywidth = (@#.width - 2 * @#.linkwidth) / @#.nkeys;
  expandafter def expandafter clearboxes expandafter =
    clearboxes clearnode_(@#);
  enddef
enddef;

vardef leafconnectnext(suffix $,$$)(text connection) =
  $.nextpath = $.next connection $$.nextin;
enddef;
vardef leafconnectprev(suffix $,$$)(text connection) =
  $.prevpath = $.prev connection $$.previn;
enddef;
vardef leafconnect(text suffixes) =
  save _fart_; string _fart_;
  forsuffixes $=suffixes:
    if known _fart_:
      leafconnectnext(scantokens _fart_,$)(--);
      leafconnectprev($,scantokens _fart_)(--);
    fi
    _fart_ := str $;
  endfor
enddef;
def leafnextbpath(suffix $) =
  $.ne -- ($.ne - ($.linkwidth,0)) -- ($.se - ($.linkwidth,0)) -- $.se -- cycle
enddef;
def leafprevbpath(suffix $) =
  $.nw -- ($.nw + ($.linkwidth,0)) -- ($.sw + ($.linkwidth,0)) -- $.sw -- cycle
enddef;
def nodecellsbpath(suffix $)(expr i,j) =
  (((0,0) -- ((j-i+1)*$.keywidth,0) -- ((j-i+1)*$.keywidth,$.height) -- (0,$.height) -- cycle)
      shifted ($.sw + ($.linkwidth + i*$.keywidth,0)))
enddef;
def nodecellbpath(suffix $)(expr i) =
  nodecellsbpath($,i,i)
enddef;

def internalnodeconnectone(suffix $,$$)(expr i) =
  if known $$.connectin:
    drawconnarrow $.child[i] {down} .. tension 4 .. {down} $$.connectin
  else:
    drawconnarrow $.child[i] {down} .. tension 4 .. {down} $$.n
  fi
enddef;
vardef internalnodeconnect(text suffixes) =
  save _fart_, _i_; string _fart_; _i_ = 0;
  forsuffixes $=suffixes:
    if unknown _fart_:
      _fart_ := str $;
    elseif _i_ <= $.nkeys:
      forsuffixes $$=scantokens _fart_:
	internalnodeconnectone($$,$,_i_);
      endfor
      _i_ := _i_ + 1;
    fi
  endfor
enddef;

vardef internalnodeit@#(text components) =
  _elementit.@#(nullpicture, 0, 0, push, false, false);
  _n_ := str @#;
  generic_declare(numeric) _n.linkwidth, _n.nkeys;
  generic_declare(boolean) _n.deleted, _n.isleaf, _n.locked;
  generic_declare(pair) _n.connectin;
  _n_ := str @# & ".keypic0";
  generic_declare(picture) _n;
  _n_ := str @# & ".child0";
  generic_declare(pair) _n;
  @#.isleaf = false;
  save _i_, _nk_; picture _i_; _nk_ := 0;
  for _i_ = components:
    @#.keypic[_nk_] = _i_;
    @#.child[_nk_] = @#.sw + (@#.linkwidth + _nk_*@#.keywidth,0);
    _nk_ := _nk_ + 1;
  endfor
  @#.child[_nk_] = @#.sw + (@#.linkwidth + _nk_*@#.keywidth,0);
  elemdraw_@# := "drawnode_";
  sproc_@# := "sizenode_";
  @#.nkeys = _nk_;
  @#.keywidth = (@#.width - 2*@#.linkwidth) / @#.nkeys;
  expandafter def expandafter clearboxes expandafter =
    clearboxes clearnode_(@#);
  enddef
enddef;

vardef drawterminationarrow(expr p) text rest =
  draw p withpen connectionpen rest;
  save endpt, enddir; pair endpt;
  endpt = point length p of p; enddir = angle(direction length p of p);
  draw ((-4,0) -- (4,0)) shifted endpt rotated (enddir + 90) withpen connectionpen rest;
  draw ((-2.25,-1.5) -- (2.25,-1.5)) shifted endpt rotated (enddir + 90) withpen connectionpen rest;
  draw ((-.5,-3) -- (.5,-3)) shifted endpt rotated (enddir + 90) withpen connectionpen rest;
enddef;
