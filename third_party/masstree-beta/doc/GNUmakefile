MAIN = spec.tex

BASE := $(basename $(MAIN))

BIBFILES := $(wildcard *.bib)
STYFILES := $(wildcard *.sty)

GS_FONTPATH := $(shell x=`kpsewhich cmr10.pfb`; echo $$GS_FONTPATH:`dirname $$x`)
export GS_FONTPATH

#LATEX = pdflatex \\nonstopmode\\input
USEPDFLATEX = 1
PDFLATEX = pdflatex
LATEX = latex
BIBTEX = bibtex -min-crossrefs=1000
METAPOST = mpost
DVIPS := dvips -P pdf -j0 -G0 -t letter
PS2PDF := ps2pdf
PS2PDF_LETTER := GS_OPTIONS=-sPAPERSIZE=letter $(PS2PDF) -sPAPERSIZE=letter
PDF2PS := $(shell (which acroread >/dev/null 2>&1 && echo acroread -toPostScript) || echo pdf2ps)

MPFIGBASE = examples_1 \
	remove1_1 remove1_2 remove1_3 remove1_31 remove1_4 remove1_5 \
	remove1_6 remove1_7 remove1_8 remove1_9 \
	remove2_1 remove2_2 remove2_3 remove2_4 remove2_5 \
	remove2_6 remove2_7 remove2_8 remove2_9 \
	remove2_11 remove2_12 remove2_15 remove2_16 \
	insert1_1 insert1_2 insert1_3 insert1_4 insert1_5
PDFFIGS += $(addsuffix .pdf,$(MPFIGBASE))
EPSFIGS += $(addsuffix .eps,$(MPFIGBASE))

RERUN = egrep -q '(^LaTeX Warning:|\(natbib\)).* Rerun'
UNDEFINED = egrep -q '^(LaTeX|Package natbib) Warning:.* undefined'

RERUNLATEX = if test ! -f $(2).bbl || $(RERUN) $(2).log || $(UNDEFINED) $(2).log; then \
		set -x; $(BIBTEX) $(2); $(1); \
		! $(RERUN) $(2).log || $(1); \
		! $(RERUN) $(2).log || $(1); \
	fi

%.dvi: %.ltx
	test ! -s $*.aux || $(BIBTEX) $* || rm -f $*.aux $*.bbl
	$(LATEX) $<
	@$(call RERUNLATEX,$(LATEX) $<,$*)

%.ps: %.dvi
	$(DVIPS) $< -o $@

%.pdf: %.ps
	$(PS2PDF_LETTER) $<

ifeq ($(USEPDFLATEX),1)
%.pdf: %.tex
	test ! -s $*.aux || $(BIBTEX) $* || rm -f $*.aux $*.bbl
	$(PDFLATEX) $<
	@$(call RERUNLATEX,$(PDFLATEX) $<,$*)
endif

all: $(BASE).pdf

$(BASE).dvi $(BASE).pdf: $(TEXFILES) $(STYFILES) $(BIBFILES) $(PDFFIGS) $(EPSFIGS)

bib:
	$(LATEX) $(MAIN)
	rm -f $(BASE).dvi
	$(MAKE) $(BASE).dvi

# after making the figure, run perl to fix Computer Modern font names to
# uppercase
%_1.eps %_2.eps %_3.eps %_4.eps %_5.eps %_6.eps %_7.eps %_8.eps %_9.eps \
%_10.eps %_11.eps %_12.eps %_13.eps %_14.eps %_15.eps %_16.eps %_17.eps %_18.eps %_19.eps \
%_20.eps %_21.eps %_22.eps %_23.eps %_24.eps %_25.eps %_26.eps %_27.eps %_28.eps %_29.eps \
%_30.eps %_31.eps %_32.eps %_33.eps %_34.eps %_35.eps %_36.eps %_37.eps %_38.eps %_39.eps: \
		%.mp elements.mp elemfig.sty patches.mp masstree.mp
	TEX=$(LATEX) $(METAPOST) $<
	perl -pi -e 'next if $$x; $$x = 1 if /%%EndProlog/; s=/([a-z][a-z0-9]+) def=/\U$$1\E def= if !/^%%/; s=\b([a-z][a-z0-9]+)\b=\U$$1\E=g if /^%%DocumentFonts/ || /^%%\+/;' $(subst .mp,,$<).[0-9]*
	for i in $(subst .mp,,$<).[0-9]*; do mv $$i `echo "$$i" | sed 's/\.\([0-9]*\)$$/_\1.eps/'`; done

%.pdf: %.eps
	epstopdf --outfile=$@ $<

clean:
	rm -f $(EPSFIGS) $(PDFFIGS) $(EPSGRAPHS) $(PDFGRAPHS)
	rm -f *.ps $(BASE).pdf *.aux
	rm -f *.dvi *.aux *.log *.bbl *.blg *.lof *.lot *.toc *.bak *~

.PHONY: clean bib all
