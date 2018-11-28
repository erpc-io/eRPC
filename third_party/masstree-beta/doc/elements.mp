% elements.mp -- MetaPost macros for drawing Click configuration graphs
% Eddie Kohler
%
% Copyright (c) 1999-2001 Massachusetts Institute of Technology
% Copyright (c) 2001-2003 International Computer Science Institute
% Copyright (c) 2006 Regents of the University of California
%
% Permission is hereby granted, free of charge, to any person obtaining a
% copy of this software and associated documentation files (the "Software"),
% to deal in the Software without restriction, subject to the conditions
% listed in the Click LICENSE file. These conditions include: you must
% preserve this copyright notice, and you cannot mention the copyright
% holders in advertising related to the Software without their permission.
% The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
% notice is a summary of the Click LICENSE file; the license in that file is
% legally binding.

input rboxes;
prologues := 1;
string defaultelementfont;
defaultscale := 1;
linejoin := mitered;

pair element_offset;
element_offset = (7.5, 4.5);
min_element_height = 19;
element_height_increment = 4;

defaultelementborderscale = 1;
defaultelementportscale = 1;

port_length = 6;
port_sep = 3;
port_offset = 4;
input_length = 7;
input_width = 4.5;
output_length = 6;
output_width = 3.8;
agnostic_sep = 1;

push = 0;
pull = 1;
agnostic = 2;
agnostic_push = 3;
agnostic_pull = 4;
push_to_pull = 5;
pull_to_push = 6;

pen elementpen.border, elementpen.port, connectionpen;
elementpen.border = pencircle scaled 0.9;
elementpen.port = pencircle scaled 0.35;
connectionpen = pencircle scaled 0.45;

color personalitycolor[], agnosticcolor[];
personalitycolor[push] = black;
personalitycolor[agnostic_push] = personalitycolor[agnostic] = white;
personalitycolor[pull] = personalitycolor[agnostic_pull] = white;
agnosticcolor[agnostic_push] = black;
agnosticcolor[agnostic_pull] = white;
agnosticcolor[agnostic] = 0.6white;

path _agnostic_output, _agnostic_input, _normal_output, _normal_input;
_agnostic_output := ((-.5,0.5output_length-agnostic_sep)
  -- (-output_width+agnostic_sep,0.5output_length-agnostic_sep)
  -- (-output_width+agnostic_sep,-0.5output_length+agnostic_sep)
  -- (-.5,-0.5output_length+agnostic_sep) -- cycle);
_agnostic_input := ((.5,0.5input_length-1.414agnostic_sep)
  -- (input_width-1.414agnostic_sep,0)
  -- (.5,-0.5input_length+1.414agnostic_sep) -- cycle);
_normal_input := ((.5,0.5input_length) -- (input_width,0)
    -- (.5,-0.5input_length) -- cycle);
_normal_output := ((-.5,0.5output_length) -- (-output_width,0.5output_length)
    -- (-output_width,-0.5output_length) -- (-.5,-0.5output_length) -- cycle);


%% redefine 'drawboxes' to allow extra text

vardef drawboxes(text t) text rest =         % Draw boundary path for each box
  forsuffixes s=t: draw bpath.s rest; endfor
enddef;


%%

vardef _make_element_ports(suffix $, port, side)(expr n, length, isout) =
  save _i_, _sc; pair _sc.adj;
  _sc.sep = (length - 2*port_offset - n*port_length + port_sep) / n;
  _sc.delta = port_length + _sc.sep;
  _sc = length/2 - port_offset - (_sc.sep - port_sep)/2 - 0.5port_length;
  _sc.adj = if isout: 1/2$.flowvector else: -1/2$.flowvector fi;
  for _i_ = 0 upto n-1:
    $.port[_i_] = $.side + $.sidevector * (_sc - _sc.delta*_i_) + _sc.adj;
  endfor;
enddef;

vardef make_element_inputs(suffix $)(expr xlen, ylen) =
  if $.down:
    _make_element_ports($, in, if $.rev: s else: n fi, $.nin, xlen-6, false);
  else:
    _make_element_ports($, in, if $.rev: e else: w fi, $.nin, ylen, false);
  fi;
enddef;

vardef make_element_outputs(suffix $)(expr xlen, ylen) =
  if $.down:
    _make_element_ports($, out, if $.rev: n else: s fi, $.nout, xlen-6, true);
  else:
    _make_element_ports($, out, if $.rev: w else: e fi, $.nout, ylen, true);
  fi;
enddef;

vardef clearelement_(suffix $) =
  _n_ := str $;
  generic_redeclare(numeric) _n.down, _n.rev, _n.sidevector, _n.flowvector, _n.width, _n.height, _n.nin, _n.nout, _n.borderscale, _n.portscale, _n.drawports;
  _n_ := str $ & ".in0";
  generic_redeclare(numeric) _n;
  _n_ := str $ & ".out0";
  generic_redeclare(numeric) _n;
  _n_ := str $ & ".inpers0";
  generic_redeclare(numeric) _n;
  _n_ := str $ & ".outpers0";
  generic_redeclare(numeric) _n;
  _n_ := "elemdraw_." & str $;
  generic_redeclare(numeric) _n;
enddef;

vardef _elementit@#(expr label_str, ninputs, noutputs, personality, down_var, rev_var) =
  picture _label_; numeric _x_, _y_;
  
  if picture label_str: _label_ = label_str
  elseif label_str = "": _label_ = nullpicture
  else: _label_ = label_str infont defaultelementfont scaled defaultscale
  fi;
  
  boxit.@#(_label_);
  _n_ := str @#;
  generic_declare(boolean) _n.down, _n.rev, _n.drawports;
  generic_declare(pair) _n.sidevector, _n.flowvector;
  generic_declare(numeric) _n.width, _n.height, _n.nin, _n.nout, _n.borderscale, _n.portscale;
  _n_ := str @# & ".in0";
  generic_declare(pair) _n;
  _n_ := str @# & ".out0";
  generic_declare(pair) _n;
  _n_ := "elemdraw_." & str @#;
  generic_declare(string) _n;
  
  @#.down = down_var;
  if down_var: @#.sidevector = (-1, 0); else: @#.sidevector = (0, 1); fi;
  if down_var: @#.flowvector = (0, -1); else: @#.flowvector = (1, 0); fi;
  @#.rev = rev_var;
  if rev_var: @#.flowvector := -@#.flowvector; @#.sidevector := -@#.sidevector; fi;
  @#.drawports = true;
  
  @#.width = xpart(@#.e - @#.w);
  @#.height = ypart(@#.n - @#.s);
  
  @#.nin = ninputs;
  @#.nout = noutputs;
  if ninputs > 0: make_element_inputs(@#, @#.width, @#.height); fi;
  if noutputs > 0: make_element_outputs(@#, @#.width, @#.height); fi;
  
  _x_ := personality;
  if _x_ = push_to_pull: _x_ := push;
  elseif _x_ = pull_to_push: _x_ := pull; fi;
  for _y_ = 0 upto ninputs-1: @#.inpers[_y_] = _x_; endfor;
  
  _x_ := personality;
  if _x_ = push_to_pull: _x_ := pull;
  elseif _x_ = pull_to_push: _x_ := push; fi;
  for _y_ = 0 upto noutputs-1: @#.outpers[_y_] = _x_; endfor;
  
  elemdraw_@# = "drawboxes";
  sproc_@# := "sizeelement_";
  
  expandafter def expandafter clearboxes expandafter =
    clearboxes clearelement_(@#);
  enddef
enddef;

vardef elementit@#(expr s, ninputs, noutputs, personality_var) =
  _elementit.@#(s, ninputs, noutputs, personality_var, false, false);
enddef;
vardef relementit@#(expr s, ninputs, noutputs, personality_var) =
  _elementit.@#(s, ninputs, noutputs, personality_var, false, true);
enddef;
vardef velementit@#(expr s, ninputs, noutputs, personality_var) =
  _elementit.@#(s, ninputs, noutputs, personality_var, true, false);
enddef;
vardef rvelementit@#(expr s, ninputs, noutputs, personality_var) =
  _elementit.@#(s, ninputs, noutputs, personality_var, true, true);
enddef;


%% change

vardef killinput(suffix $)(expr p) =
  if (p >= 0) and (p < $.nin): save _i_;
    for _i_ = p upto $.nin-2:
      $.in[_i_] := $.in[_i_+1];
      $.inpers[_i_] := $.inpers[_i_+1];
    endfor;
    $.nin := $.nin - 1
  fi
enddef;

vardef killoutput(suffix $)(expr p) =
  if (p >= 0) and (p < $.nout): save _i_;
    for _i_ = p upto $.nout-2:
      $.out[_i_] := $.out[_i_+1];
      $.outpers[_i_] := $.outpers[_i_+1];
    endfor;
    $.nout := $.nout - 1
  fi
enddef;

vardef portinteriorin(suffix $)(expr i) =
  path _p_;
  _p_ := if $.inpers[i] >= agnostic: _agnostic_input else: _normal_input fi
    scaled $.portscale;
  if $.down: _p_ := _p_ rotated -90 fi;
  if $.rev: _p_ := _p_ rotated 180 fi;
  _p_ := _p_ shifted $.in[i];
  if $.down and $.rev: .5[ulcorner _p_,urcorner _p_]
  elseif $.down: .5[llcorner _p_,lrcorner _p_]
  elseif $.rev: .5[ulcorner _p_,llcorner _p_]
  else: .5[urcorner _p_,lrcorner _p_] fi
enddef;

vardef portinteriorout(suffix $)(expr i) =
  path _p_;
  _p_ := if $.outpers[i] >= agnostic: _agnostic_output else: _normal_output fi
    scaled $.portscale;
  if $.down: _p_ := _p_ rotated -90 fi;
  if $.rev: _p_ := _p_ rotated 180 fi;
  _p_ := _p_ shifted $.out[i];
  if $.down and $.rev: .5[llcorner _p_,lrcorner _p_]
  elseif $.down: .5[ulcorner _p_,urcorner _p_]
  elseif $.rev: .5[urcorner _p_,lrcorner _p_]
  else: .5[ulcorner _p_,llcorner _p_] fi
enddef;


%% fix

vardef set_element_dx(suffix $) =
  if $.down: save x;
    x.maxport = max($.nin, $.nout);
    x.len = x.maxport*port_length + (x.maxport-1)*port_sep + 2port_offset;
    x.w = xpart(urcorner pic_$ - llcorner pic_$);
    x.ww = x.w + 2xpart(element_offset);
    if x.len > x.ww: $.dx = (x.len - x.w) / 2;
    else: $.dx = xpart element_offset; fi;
  else:
    $.dx = xpart element_offset;
  fi;
enddef;

vardef set_element_dy(suffix $) =
  save y;
  y.h = ypart(urcorner pic_$ - llcorner pic_$);
  y.hh = y.h + 2ypart(element_offset);
  if $.down: y := y.hh;
  else:
    y.maxport = max($.nin, $.nout);
    y.len = y.maxport*port_length + (y.maxport-1)*port_sep + 2port_offset;
    y := max(y.hh, y.len);
  fi;
  
  y'' := min_element_height;
  forever:
    exitif y'' >= y;
    y'' := y'' + element_height_increment;
  endfor;
  
  $.dy = (y'' - y.h)/2;
enddef;

def sizeelement_(suffix $) =
  if unknown $.dx: set_element_dx($); fi
  if unknown $.dy: set_element_dy($); fi
  if unknown $.borderscale: $.borderscale = defaultelementborderscale; fi
  if unknown $.portscale: $.portscale = defaultelementportscale; fi
enddef;

vardef fixelementsizeleft(text t) =
  forsuffixes $=t:
    fixsize($);
    if $.dx > xpart element_offset: $.off := $.off - ($.dx - xpart element_offset, 0); fi;
  endfor;
enddef;

vardef fixelement(text elements) =
  fixsize(elements);
  fixpos(elements);
enddef;

vardef elementleftjustify(text elements) =
  fixsize(elements);
  forsuffixes $=elements:
    if $.dx > xpart element_offset: $.off := $.off - ($.dx - xpart element_offset, 0); fi;
  endfor;
enddef;

vardef fixrelations(suffix efirst)(text elements) =
  forsuffixes $=elements:
    if unknown xpart(efirst.off - $.off): xpart $.off = xpart efirst.off fi;
    if unknown ypart(efirst.off - $.off): ypart $.off = ypart efirst.off fi;
  endfor;
enddef;

vardef elementbbox(suffix efirst)(text elements) =
  fixsize(efirst,elements);
  fixrelations(efirst,elements);
  save __t,__l,__r,__b,__p; picture __p;
  __t = ypart(efirst.n - efirst.off);
  __l = xpart(efirst.w - efirst.off);
  __r = xpart(efirst.e - efirst.off);
  __b = ypart(efirst.s - efirst.off);
  forsuffixes $=elements:
    if ypart($.n - efirst.off) > __t: __t := ypart($.n - efirst.off) fi;
    if xpart($.w - efirst.off) < __l: __l := xpart($.w - efirst.off) fi;
    if xpart($.e - efirst.off) > __r: __r := xpart($.e - efirst.off) fi;
    if ypart($.s - efirst.off) < __b: __b := ypart($.s - efirst.off) fi;
  endfor;
  __p = nullpicture;
  setbounds __p to ((__l,__t) -- (__r,__t) -- (__r,__b) -- (__l,__b) -- cycle)
     if known efirst.off: shifted efirst.off fi;
  __p
enddef;

vardef compoundelementlink@#(suffix efirst)(text elements) =
  fixsize(efirst,elements);
  fixrelations(efirst,elements);
  save __t,__l,__r,__b,__p; picture __p;
  __t = ypart(efirst.n - efirst.off);
  __l = xpart(efirst.w - efirst.off);
  __r = xpart(efirst.e - efirst.off);
  __b = ypart(efirst.s - efirst.off);
  forsuffixes $=elements:
    if ypart($.n - efirst.off) > __t: __t := ypart($.n - efirst.off) fi;
    if xpart($.w - efirst.off) < __l: __l := xpart($.w - efirst.off) fi;
    if xpart($.e - efirst.off) > __r: __r := xpart($.e - efirst.off) fi;
    if ypart($.s - efirst.off) < __b: __b := ypart($.s - efirst.off) fi;
  endfor;
  @#.c = efirst.off + .5[(__l,__t), (__r,__b)]
enddef;


%% draw

vardef draw_element_inputs(suffix $) =
  path _p_, _ag_;
  _p_ := _normal_input scaled $.portscale;
  _ag_ := _agnostic_input scaled $.portscale;
  if $.down: _p_ := _p_ rotated -90; _ag_ := _ag_ rotated -90; fi;
  if $.rev: _p_ := _p_ rotated 180; _ag_ := _ag_ rotated 180; fi;
  for _i_ = 0 upto $.nin - 1:
    if $.inpers[_i_] >= 0:
      fill _p_ shifted $.in[_i_] withcolor personalitycolor[$.inpers[_i_]];
      draw _p_ shifted $.in[_i_];
      if $.inpers[_i_] >= agnostic:
	fill _ag_ shifted $.in[_i_] withcolor agnosticcolor[$.inpers[_i_]];
	draw _ag_ shifted $.in[_i_]; fi
    fi;
  endfor
enddef;

vardef draw_element_outputs(suffix $) =
  path _p_, _ag_;
  _p_ := _normal_output scaled $.portscale;
  _ag_ := _agnostic_output scaled $.portscale;
  if $.down: _p_ := _p_ rotated -90; _ag_ := _ag_ rotated -90; fi;
  if $.rev: _p_ := _p_ rotated 180; _ag_ := _ag_ rotated 180; fi;
  for _i_ = 0 upto $.nout - 1:
    if $.outpers[_i_] >= 0:
      fill _p_ shifted $.out[_i_] withcolor personalitycolor[$.outpers[_i_]];
      draw _p_ shifted $.out[_i_];
      if $.outpers[_i_] >= agnostic:
	fill _ag_ shifted $.out[_i_] withcolor agnosticcolor[$.outpers[_i_]];
	draw _ag_ shifted $.out[_i_]; fi
    fi;
  endfor
enddef;

vardef drawelement(text elements) text rest =
  drawelementbox(elements) rest;
  drawunboxed(elements);
enddef;

vardef drawelementbox(text elements) text rest =
  save $, oldpen; oldpen := savepen;
  interim linejoin := mitered;
  fixsize(elements);
  fixpos(elements);
  forsuffixes $ = elements:
    if $.drawports:
      pickup elementpen.port scaled $.portscale;
      if $.nin > 0: draw_element_inputs($); fi;
      if $.nout > 0: draw_element_outputs($); fi;
    fi;
    if $.borderscale > 0:
      pickup elementpen.border scaled $.borderscale;
      scantokens elemdraw_$($) rest;
    fi;
  endfor;
  pickup oldpen;
enddef;

vardef fillelement(text elements)(text color) =
  fixsize(elements);
  fixpos(elements);
  forsuffixes $=elements:
    fill bpath.$ withcolor color;
  endfor;
enddef;


%% queues

vardef _drawqueued(expr p,delta,rot,lim,pp) text rest =
  save i; interim linecap := squared; i := delta;
  forever:
    draw (p) shifted ((i,0) rotated rot) withpen currentpen scaled 0.25 rest;
    i := i + delta; exitunless i < lim;
  endfor;
  draw (pp) rest;
enddef;
def drawqueued(suffix $) =
  _drawqueued($.ne -- $.se, 6, 180, .9*$.width, $.nw -- $.ne -- $.se -- $.sw)
enddef;
def drawrqueued(suffix $) =
  _drawqueued($.nw -- $.sw, 6, 0, .9*$.width, $.ne -- $.nw -- $.sw -- $.se)
enddef;
def drawvqueued(suffix $) =
  _drawqueued($.se -- $.sw, 5, 90, .9*$.height, $.nw -- $.sw -- $.se -- $.ne)
enddef;
def drawrvqueued(suffix $) =
  _drawqueued($.ne -- $.nw, 5, 270, .9*$.height, $.sw -- $.nw -- $.ne -- $.se)
enddef;

vardef queueit@#(expr s) =
  _elementit.@#(s, 1, 1, push_to_pull, false, false);
  elemdraw_@# := "drawqueued";
enddef;
vardef rqueueit@#(expr s) =
  _elementit.@#(s, 1, 1, push_to_pull, false, true);
  elemdraw_@# := "drawrqueued";
enddef;
vardef vqueueit@#(expr s) =
  _elementit.@#(s, 1, 1, push_to_pull, true, false);
  elemdraw_@# := "drawvqueued";
enddef;
vardef rvqueueit@#(expr s) =
  _elementit.@#(s, 1, 1, push_to_pull, true, true);
  elemdraw_@# := "drawrvqueued";
enddef;


%% connections

picture _cutarrpic;

vardef arrowhead expr p =
  save q,h,e,f; path q,h; pair e,f;
  e = point length p of p;
  q = gobble(p shifted -e cutafter makepath(pencircle scaled 2ahlength))
    cuttings;
  h = gobble(p shifted -e cutafter makepath(pencircle scaled 1.5ahlength))
    cuttings;
  f = point 0 of h;
  (q rotated .5ahangle & reverse q rotated -.5ahangle -- f -- cycle)  shifted e
enddef;
def _cutarr(expr b,e) text t =
  _cutarrpic := image(draw (0,0) -- (1,0) -- cycle t);
  _cutarramt := (2ypart urcorner _cutarrpic / sind .5ahangle) - 0.75;
  if _cutarramt > 0:
    _apth := subpath (xpart(_apth intersectiontimes makepath(pencircle
	  scaled (b*_cutarramt)) shifted (point 0 of _apth)),
      xpart(_apth intersectiontimes makepath(pencircle scaled (e*_cutarramt))
  shifted (point length _apth of _apth))) of _apth;
  fi
enddef;
def _finarr text t =
  _cutarr(0,1) t;
  draw (subpath (0, xpart(_apth intersectiontimes makepath(pencircle scaled 1.2ahlength) shifted (point length _apth of _apth))) of _apth) t;
  fill arrowhead _apth  t
enddef;
def _findarr text t =
  _cutarr(1,1) t;
  draw (subpath
    (xpart(_apth intersectiontimes makepath(pencircle scaled 1.2ahlength) shifted (point 0 of _apth)),
     xpart(_apth intersectiontimes makepath(pencircle scaled 1.2ahlength) shifted (point length _apth of _apth))) of _apth) t;
  fill arrowhead _apth  t;
  fill arrowhead reverse _apth  t
enddef;

def connectpath(suffix $,#,##,$$) =
  $.out[#]{$.flowvector} .. {$$.flowvector}$$.in[##]
enddef;
vardef drawconnectj(suffix $,#,##,$$)(text t) text rest =
  interim linejoin := mitered;
  drawarrow $.out[#]{$.flowvector} t {$$.flowvector}$$.in[##] withpen connectionpen rest
enddef;
def drawconnect(suffix $,#,##,$$) =
  drawconnectj($,#,##,$$)(..)
enddef;
vardef drawconnectna(suffix $,#,##,$$) text rest =
  interim linejoin := mitered;
  draw $.out[#]{$.flowvector} .. {$$.flowvector}$$.in[##] withpen connectionpen rest
enddef;

def drawconnarrow expr p =
  _apth:=p; _finarr withpen connectionpen
enddef;
def drawconnarrowna expr p =
  draw p withpen connectionpen
enddef;
def drawdblconnarrow expr p =
  _apth:=p; _findarr withpen connectionpen
enddef;
