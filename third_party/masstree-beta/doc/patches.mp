input elements;

def thediamond_(suffix $) =
  $.e -- $.n -- $.w -- $s -- cycle
enddef;

vardef sizediamond_(suffix $) =
  save a_, b_;
  (a_,b_) = .5*(urcorner pic_$ - llcorner pic_$);
  if (unknown $dy or unknown $dx) and unknown $squatness: $squatness = 1; fi
  if unknown $dy: $dy = $squatness * ($dx + a_) - b_; fi
  if unknown $dy:
    $dy = b_ + circmargin * sqrt($squatness+1) + $squatness * a_;
  fi
  sizeelement_($);
enddef;

vardef cleardiamond_(suffix $) =
  _n_ := str $;
  generic_redeclare(numeric) _n.squatness;
enddef;

vardef patchit@#(expr s, type) =
  _elementit.@#(s, 0, 0, push, false, false);
  if type = 0:
    pproc_.@# := "thecirc_";
    % sproc_.@# := "sizecirc_";
  elseif type = 2:
    _n_ := str @#;
    generic_declare(numeric) _n.squatness, _n.dx, _n.dy;
    pproc_.@# := "thediamond_";
    sproc_.@# := "sizediamond_";
    expandafter def expandafter clearboxes expandafter =
      clearboxes cleardiamond_(@#);
    enddef
  elseif type = 3:
    pproc_.@# := "rboxpath_";
  fi;
enddef;

vardef drawconnpatch(suffix $,$$)(text conn) text rest =
  interim linejoin := mitered;
  drawarrow ($.c conn $$.c) cutbefore bpath.$ cutafter bpath.$$ withpen connectionpen rest
enddef;
