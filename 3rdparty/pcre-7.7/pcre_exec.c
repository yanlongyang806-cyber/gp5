/*************************************************
*      Perl-Compatible Regular Expressions       *
*************************************************/

/* PCRE is a library of functions to support regular expressions whose syntax
and semantics are as close as possible to those of the Perl 5 language.

                       Written by Philip Hazel
           Copyright (c) 1997-2008 University of Cambridge

-----------------------------------------------------------------------------
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of the University of Cambridge nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------------
*/


/* This module contains pcre_exec(), the externally visible function that does
pattern matching using an NFA algorithm, trying to mimic Perl as closely as
possible. There are also some static supporting functions. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define NLBLOCK md             /* Block containing newline information */
#define PSSTART start_subject  /* Field containing processed string start */
#define PSEND   end_subject    /* Field containing processed string end */

#include "pcre_internal.h"

/* Undefine some potentially clashing cpp symbols */

#undef min
#undef max

/* Flag bits for the match() function */

#define match_condassert     0x01  /* Called to check a condition assertion */
#define match_cbegroup       0x02  /* Could-be-empty unlimited repeat group */

/* Non-error returns from the match() function. Error returns are externally
defined PCRE_ERROR_xxx codes, which are all negative. */

#define MATCH_MATCH        1
#define MATCH_NOMATCH      0

/* Special internal returns from the match() function. Make them sufficiently
negative to avoid the external error codes. */

#define MATCH_COMMIT       (-999)
#define MATCH_PRUNE        (-998)
#define MATCH_SKIP         (-997)
#define MATCH_THEN         (-996)

/* Maximum number of ints of offset to save on the stack for recursive calls.
If the offset vector is bigger, malloc is used. This should be a multiple of 3,
because the offset vector is always a multiple of 3 long. */

#define REC_STACK_SAVE_MAX 30

/* Min and max values for the common repeats; for the maxima, 0 => infinity */

static const char rep_min[] = { 0, 0, 1, 1, 0, 0 };
static const char rep_max[] = { 0, 0, 0, 0, 1, 1 };



#ifdef DEBUG
/*************************************************
*        Debugging function to print chars       *
*************************************************/

/* Print a sequence of chars in printable format, stopping at the end of the
subject if the requested.

Arguments:
  p           points to characters
  length      number to print
  is_subject  TRUE if printing from within md->start_subject
  md          pointer to matching data block, if is_subject is TRUE

Returns:     nothing
*/

static void
pchars(const uschar *p, int length, BOOL is_subject, match_data *md)
{
unsigned int c;
if (is_subject && length > md->end_subject - p) length = md->end_subject - p;
while (length-- > 0)
  if (isprint(c = *(p++))) printf("%c", c); else printf("\\x%02x", c);
}
#endif



/*************************************************
*          Match a back-reference                *
*************************************************/

/* If a back reference hasn't been set, the length that is passed is greater
than the number of characters left in the string, so the match fails.

Arguments:
  offset      index into the offset vector
  eptr        points into the subject
  length      length to be matched
  md          points to match data block
  ims         the ims flags

Returns:      TRUE if matched
*/

static BOOL
match_ref(int offset, register USPTR eptr, int length, match_data *md,
  unsigned long int ims)
{
USPTR p = md->start_subject + md->offset_vector[offset];

#ifdef DEBUG
if (eptr >= md->end_subject)
  printf("matching subject <null>");
else
  {
  printf("matching subject ");
  pchars(eptr, length, TRUE, md);
  }
printf(" against backref ");
pchars(p, length, FALSE, md);
printf("\n");
#endif

/* Always fail if not enough characters left */

if (length > md->end_subject - eptr) return FALSE;

/* Separate the caselesss case for speed */

if ((ims & PCRE_CASELESS) != 0)
  {
  while (length-- > 0)
    if (md->lcc[*p++] != md->lcc[*eptr++]) return FALSE;
  }
else
  { while (length-- > 0) if (*p++ != *eptr++) return FALSE; }

return TRUE;
}



/***************************************************************************
****************************************************************************
                   RECURSION IN THE match() FUNCTION

The match() function is highly recursive, though not every recursive call
increases the recursive depth. Nevertheless, some regular expressions can cause
it to recurse to a great depth. I was writing for Unix, so I just let it call
itself recursively. This uses the stack for saving everything that has to be
saved for a recursive call. On Unix, the stack can be large, and this works
fine.

It turns out that on some non-Unix-like systems there are problems with
programs that use a lot of stack. (This despite the fact that every last chip
has oodles of memory these days, and techniques for extending the stack have
been known for decades.) So....

There is a fudge, triggered by defining NO_RECURSE, which avoids recursive
calls by keeping local variables that need to be preserved in blocks of memory
obtained from malloc() instead instead of on the stack. Macros are used to
achieve this so that the actual code doesn't look very different to what it
always used to.

The original heap-recursive code used longjmp(). However, it seems that this
can be very slow on some operating systems. Following a suggestion from Stan
Switzer, the use of longjmp() has been abolished, at the cost of having to
provide a unique number for each call to RMATCH. There is no way of generating
a sequence of numbers at compile time in C. I have given them names, to make
them stand out more clearly.

Crude tests on x86 Linux show a small speedup of around 5-8%. However, on
FreeBSD, avoiding longjmp() more than halves the time taken to run the standard
tests. Furthermore, not using longjmp() means that local dynamic variables
don't have indeterminate values; this has meant that the frame size can be
reduced because the result can be "passed back" by straight setting of the
variable instead of being passed in the frame.
****************************************************************************
***************************************************************************/

/* Numbers for RMATCH calls. When this list is changed, the code at HEAP_RETURN
below must be updated in sync.  */

enum { RM1=1, RM2,  RM3,  RM4,  RM5,  RM6,  RM7,  RM8,  RM9,  RM10,
       RM11,  RM12, RM13, RM14, RM15, RM16, RM17, RM18, RM19, RM20,
       RM21,  RM22, RM23, RM24, RM25, RM26, RM27, RM28, RM29, RM30,
       RM31,  RM32, RM33, RM34, RM35, RM36, RM37, RM38, RM39, RM40,
       RM41,  RM42, RM43, RM44, RM45, RM46, RM47, RM48, RM49, RM50,
       RM51,  RM52, RM53, RM54 };

/* These versions of the macros use the stack, as normal. There are debugging
versions and production versions. Note that the "rw" argument of RMATCH isn't
actuall used in this definition. */

#ifndef NO_RECURSE
#define REGISTER register

#ifdef DEBUG
#define RMATCH(ra,rb,rc,rd,re,rf,rg,rw) \
  { \
  printf("match() called in line %d\n", __LINE__); \
  rrc = match(ra,rb,mstart,rc,rd,re,rf,rg,rdepth+1); \
  printf("to line %d\n", __LINE__); \
  }
#define RRETURN(ra) \
  { \
  printf("match() returned %d from line %d ", ra, __LINE__); \
  return ra; \
  }
#else
#define RMATCH(ra,rb,rc,rd,re,rf,rg,rw) \
  rrc = match(ra,rb,mstart,rc,rd,re,rf,rg,rdepth+1)
#define RRETURN(ra) return ra
#endif

#else


/* These versions of the macros manage a private stack on the heap. Note that
the "rd" argument of RMATCH isn't actually used in this definition. It's the md
argument of match(), which never changes. */

#define REGISTER

#define RMATCH(ra,rb,rc,rd,re,rf,rg,rw)\
  {\
  heapframe *newframe = (pcre_stack_malloc)(sizeof(heapframe));\
  frame->Xwhere = rw; \
  newframe->Xeptr = ra;\
  newframe->Xecode = rb;\
  newframe->Xmstart = mstart;\
  newframe->Xoffset_top = rc;\
  newframe->Xims = re;\
  newframe->Xeptrb = rf;\
  newframe->Xflags = rg;\
  newframe->Xrdepth = frame->Xrdepth + 1;\
  newframe->Xprevframe = frame;\
  frame = newframe;\
  DPRINTF(("restarting from line %d\n", __LINE__));\
  goto HEAP_RECURSE;\
  L_##rw:\
  DPRINTF(("jumped back to line %d\n", __LINE__));\
  }

#define RRETURN(ra)\
  {\
  heapframe *newframe = frame;\
  frame = newframe->Xprevframe;\
  (pcre_stack_free)(newframe);\
  if (frame != NULL)\
    {\
    rrc = ra;\
    goto HEAP_RETURN;\
    }\
  return ra;\
  }


/* Structure for remembering the local variables in a private frame */

typedef struct heapframe {
  struct heapframe *Xprevframe;

  /* Function arguments that may change */

  const uschar *Xeptr;
  const uschar *Xecode;
  const uschar *Xmstart;
  int Xoffset_top;
  long int Xims;
  eptrblock *Xeptrb;
  int Xflags;
  unsigned int Xrdepth;

  /* Function local variables */

  const uschar *Xcallpat;
  const uschar *Xcharptr;
  const uschar *Xdata;
  const uschar *Xnext;
  const uschar *Xpp;
  const uschar *Xprev;
  const uschar *Xsaved_eptr;

  recursion_info Xnew_recursive;

  BOOL Xcur_is_word;
  BOOL Xcondition;
  BOOL Xprev_is_word;

  unsigned long int Xoriginal_ims;

#ifdef SUPPORT_UCP
  int Xprop_type;
  int Xprop_value;
  int Xprop_fail_result;
  int Xprop_category;
  int Xprop_chartype;
  int Xprop_script;
  int Xoclength;
  uschar Xocchars[8];
#endif

  int Xctype;
  unsigned int Xfc;
  int Xfi;
  int Xlength;
  int Xmax;
  int Xmin;
  int Xnumber;
  int Xoffset;
  int Xop;
  int Xsave_capture_last;
  int Xsave_offset1, Xsave_offset2, Xsave_offset3;
  int Xstacksave[REC_STACK_SAVE_MAX];

  eptrblock Xnewptrb;

  /* Where to jump back to */

  int Xwhere;

} heapframe;

#endif


/***************************************************************************
***************************************************************************/



/*************************************************
*         Match from current position            *
*************************************************/

/* This function is called recursively in many circumstances. Whenever it
returns a negative (error) response, the outer incarnation must also return the
same response.

Performance note: It might be tempting to extract commonly used fields from the
md structure (e.g. utf8, end_subject) into individual variables to improve
performance. Tests using gcc on a SPARC disproved this; in the first case, it
made performance worse.

Arguments:
   eptr        pointer to current character in subject
   ecode       pointer to current position in compiled code
   mstart      pointer to the current match start position (can be modified
                 by encountering \K)
   offset_top  current top pointer
   md          pointer to "static" info for the match
   ims         current /i, /m, and /s options
   eptrb       pointer to chain of blocks containing eptr at start of
                 brackets - for testing for empty matches
   flags       can contain
                 match_condassert - this is an assertion condition
                 match_cbegroup - this is the start of an unlimited repeat
                   group that can match an empty string
   rdepth      the recursion depth

Returns:       MATCH_MATCH if matched            )  these values are >= 0
               MATCH_NOMATCH if failed to match  )
               a negative PCRE_ERROR_xxx value if aborted by an error condition
                 (e.g. stopped by repeated call or recursion limit)
*/

static int
match(REGISTER USPTR eptr, REGISTER const uschar *ecode, const uschar *mstart,
  int offset_top, match_data *md, unsigned long int ims, eptrblock *eptrb,
  int flags, unsigned int rdepth)
{
/* These variables do not need to be preserved over recursion in this function,
so they can be ordinary variables in all cases. Mark some of them with
"register" because they are used a lot in loops. */

register int  rrc;         /* Returns from recursive calls */
register int  i;           /* Used for loops not involving calls to RMATCH() */
register unsigned int c;   /* Character values not kept over RMATCH() calls */
register BOOL utf8;        /* Local copy of UTF-8 flag for speed */

BOOL minimize, possessive; /* Quantifier options */

/* When recursion is not being used, all "local" variables that have to be
preserved over calls to RMATCH() are part of a "frame" which is obtained from
heap storage. Set up the top-level frame here; others are obtained from the
heap whenever RMATCH() does a "recursion". See the macro definitions above. */

#ifdef NO_RECURSE
heapframe *frame = (pcre_stack_malloc)(sizeof(heapframe));
frame->Xprevframe = NULL;            /* Marks the top level */

/* Copy in the original argument variables */

frame->Xeptr = eptr;
frame->Xecode = ecode;
frame->Xmstart = mstart;
frame->Xoffset_top = offset_top;
frame->Xims = ims;
frame->Xeptrb = eptrb;
frame->Xflags = flags;
frame->Xrdepth = rdepth;

/* This is where control jumps back to to effect "recursion" */

HEAP_RECURSE:

/* Macros make the argument variables come from the current frame */

#define eptr               frame->Xeptr
#define ecode              frame->Xecode
#define mstart             frame->Xmstart
#define offset_top         frame->Xoffset_top
#define ims                frame->Xims
#define eptrb              frame->Xeptrb
#define flags              frame->Xflags
#define rdepth             frame->Xrdepth

/* Ditto for the local variables */

#ifdef SUPPORT_UTF8
#define charptr            frame->Xcharptr
#endif
#define callpat            frame->Xcallpat
#define data               frame->Xdata
#define next               frame->Xnext
#define pp                 frame->Xpp
#define prev               frame->Xprev
#define saved_eptr         frame->Xsaved_eptr

#define new_recursive      frame->Xnew_recursive

#define cur_is_word        frame->Xcur_is_word
#define condition          frame->Xcondition
#define prev_is_word       frame->Xprev_is_word

#define original_ims       frame->Xoriginal_ims

#ifdef SUPPORT_UCP
#define prop_type          frame->Xprop_type
#define prop_value         frame->Xprop_value
#define prop_fail_result   frame->Xprop_fail_result
#define prop_category      frame->Xprop_category
# Vq`‹Îè,÷ÿÿVÇ$,Iè& Y^ÃV‹ñ‹‹@W3ÿöD0u‹D08‹‹ÈÿR4ƒøÿuj_‹‹HÎ…ÿt‹AÇƒy8 uƒÈj Pèåÿÿ_‹Æ^ÃƒÁéàøÿÿVq ‹ÎèˆÿÿÿöD$tVè6’åÿY‹Æ^Â ¸1ãDè
ŸåÿQQV‹ñƒf j,èú™åÿY…Àt0‹Mô‰F‰ ‹F‰@‹F‰@‹FÆ@(‹FÆ@)‹Æ^d‰    ÉÂ ƒeð ÇEì5Gƒeü Mìè=7åÿÌ¸CãDè£žåÿQV‹uW‹ùV‰}ðèÚàÿÿ‹‹@ƒeü ƒ|0 u‹D0<…Àt‹Èèíþÿÿ‹‹@ƒ|0 ‹Mô”ÀˆG‹Ç_^d‰    ÉÂ ‹‹@öDtéºþÿÿÃÿt$‹L$èÆÿÿ3É…À˜ÀÂ U‹ìQÿu‹ƒeü ÿP‹EÉÂ U‹ìQÿu‹ƒeü ÿP‹EÉÂ ¸UãDèðåÿQVEðPèÎÚÿÿƒeü Pè°ûÿÿƒMüÿYMð‹ðè3Ùÿÿÿu‹‹ÎÿP‹Mô^d‰    ÉÂ U‹ìQÿu‹ƒeü ÿP‹EÉÂ ¸gãDè’åÿQQVWj Mìèõ ‹5\\ƒeü ¹h\‰uðèë×ÿÿ‹MPèãØÿÿ‹ø…ÿu9…öt‹þë1ÿuEðPè²ûÿÿYYƒøÿuè&Mæÿë‹}ð‹Ï‰=\\èß×ÿÿWè· YƒMüÿMìè² ‹Mô‹Ç_^d‰    ÉÃ¸yãDèÿœåÿQQVWj Mìèb ‹5`\ƒeü ¹p\‰uðèX×ÿÿ‹MPèPØÿÿ‹ø…ÿu9…öt‹þë1ÿuEðPè»ûÿÿYYƒøÿuè“Læÿë‹}ð‹Ï‰=`\èL×ÿÿWè$ YƒMüÿMìè ‹Mô‹Ç_^d‰    ÉÃ¸®ãDèlœåÿƒì(ƒeð S‹]…Ûtlƒ; ugVjèP—åÿ‹ðY‰uƒeü …öt:‹E‹ ‹H…ÉuHQMÌè‹ñÿÿƒf 3ÉA‰MðQP‹ÎÇEü   Çð-Iè«ðÿÿë3öƒMüÿöEð‰3^tMÌèÉñÿÿ‹MôjX[d‰    ÉÃVÿt$‹ñèÌþÿÿYP‹ÎèDëÿÿ^Â ¸ÀãDè¾›åÿV‹ñƒ~T uMÿuÿuÿuè¦% ƒÄ…Àt8jP‹ÎèÄêÿÿEP‹ÎèÝÿÿƒeü PèxþÿÿYP‹ÎèðêÿÿƒMüÿMèÓÖÿÿ‹Æë3À‹Mô^d‰    ÉÂ ¸ÒãDèM›åÿQV‹ñ‰uð‹‹‹@ƒeü öDtè©ûÿÿƒMüÿ‹ÎèÝÿÿ‹Mô^d‰    ÉÃVW‹|$‹ñƒÿÿu
hH5Gè˜ j Wè†9åÿ„Àt!ÿt$‹ÎWj è’¿ÿÿƒ~‰~r‹ë‹ÆÆ8 _‹Æ^Â V‹ñèXáÿÿ‹D$ƒf< j ‹Î‰F8è¶üÿÿƒ~8 ˆF@u‹FƒÈj P‹Îèmàÿÿ€|$ tVè¸  Y^Â ¸äãDè{šåÿƒì‹ESVW3ÿ‰}ðHŠ@„Òuù‹u+Á‹Ø‹‹@‹L0 ‹D0$…À|…Ét;Ç|;Ëv+ËÇ‰Mè‰Eìë!}è!}ìVMàèsûÿÿ3Ò‰Uü8UäuÇEð   éÁ   ‹‹@‹D0%À  ƒø@t?9Uì|:9Uèv3‹‹@ŠL0@ˆMÿu‹L08èºèÿÿƒøÿtmƒEèÿƒUìÿƒ}ì Õ|ƒ}è wÍ‹‹@‹L08‹WSÿuÿP$;Ãu@;×u<3ÿ9}ì|<~,‹‹@ŠL0@ˆMÿu‹L08ècèÿÿƒøÿtƒEèÿƒUìÿ9}ìÖ|9}èwÏëÇEð   ‹‹@Æƒ`  ƒ`$ 3Ò‹‹HÎ9Uðt‹AEð9Q8uƒÈRPèþÞÿÿƒMüÿMàèÅýÿÿ‹Mô_‹Æ^[d‰    ÉÃ¸öãDè™åÿƒìƒeð S‹]V‹u‹‹@‹L0 ‹D0$W‹{…À|…Ét‰Eì;Ïv
+Ï‰Eì‰Mëƒe VMèèúÿÿƒeü €}ì uÇEð   é¯   ‹‹@‹D0%À  ƒø@t(ƒ} v"‹‹@ŠL0@ˆMÿu‹L08èYçÿÿƒøÿtÿMuÞƒ{r5‹ë3ÇEð   ƒ} vN‹‹@ŠL0@ˆMÿu‹L08è çÿÿƒøÿt-ÿMuÞë*‹Ã‹‹I‹L18‹3ÛSWPÿR$;Çu;Ót¹ÇEð   ëƒMð‹‹@Æƒ`  ƒ`$ ‹‹H3ÒÎ9Uðt‹AEð9Q8uƒÈRPè§ÝÿÿƒMüÿMèènüÿÿ‹Mô_‹Æ^[d‰    ÉÃ¸äDèª—åÿQQVWj Mìè ‹5d\ƒeü ¹l\‰uðèÒÿÿ‹MPèûÒÿÿ‹ø…ÿu9…öt‹þë1ÿuEðPèìúÿÿYYƒøÿuè>Gæÿë‹}ð‹Ï‰=d\è÷ÑÿÿWèÏ YƒMüÿMìèÊ ‹Mô‹Ç_^d‰    ÉÃ¸"äDè—åÿƒìSV‹ñ3ÛVMà‰]ðèUøÿÿ‰]ü8]ätgWEìP‹‹HÎèÒÓÿÿPÆEüèCùÿÿYMì‹øˆ]üè8Òÿÿÿu‹‹A‹IŠT1@ÿu‹L18ˆUèÿuè‹ÆPQˆ]ØÿuØEP‹ÏÿR_8]tÇEð   ‹‹HÎ9]ðt‹AEð9Y8uƒÈSPèHÜÿÿƒMüÿMàèûÿÿ‹Mô‹Æ^[d‰    ÉÂ ‹D$Vÿt$ƒÈPÿt$‹ñNèqúÿÿ…À‹‹HuÎ‹AƒÈƒy8 uƒÈëÎ‹A8÷ØÀƒàüƒÀj PèÜÛÿÿ^Â ¸4äDèø•åÿƒì(SV‹ñ‹F ‹ 3Û;Ãt0‹F ‹N0‹ ‹	È;Ás ‹F0ÿ‹v ‹H‰¶ ‹Mô^[d‰    ÉÃ9^TuƒÈÿëè‹Îè&Öÿÿ9^DuÿvTEòPˆ]òè†ïÿÿYY„ÀtÚ¶EòëÁWÇEà   ‰]Üˆ]Ì‰]üé“   PjMÌè²Äÿÿƒ}à‹UÌ‹ÂsEÌ‹ÐEÜ‹ND‹9]èS]ôS]óS]ìSPRFLPÿW3Û;Ã|c3ÉA;Á~'ƒøuW9MÜr@ƒ}à‹EÌsEÌQPQEóPèC9 ƒÄëyEó9Eè‹EÌuDƒ}àsEÌ‹Mì+ÈQSMÌè,åÿÿvTèÑ2 Yƒøÿ…[ÿÿÿƒÎÿƒMüÿSjMÌèù4åÿ‹Æ_éìþÿÿƒ}àsEÌ+EìEÜ‹øë‹EìÿvTO¾Pèl7 YY;ûé¶uóë¹‹D$Vÿt$ƒÈPÿt$‹ñNèøÿÿ…À‹‹HuÎ‹AƒÈƒy8 uƒÈëÎ‹A8÷ØÀƒàüƒÀj PèÚÿÿ^Â Vÿt$‹ñƒf ÿt$ÇF   Æ èùøÿÿ‹Æ^Â ¸ZäDè ”åÿQQ3ÀV‹ñ‰uì‰Eð9EtÇX,IÇFT,I‰EüÇEð   ‹‹@ÿuÇH,I‹‹HÿuÎèíøÿÿ‹Mô‹Æ^d‰    ÉÂ ¸€äDèž“åÿQQ3ÀV‹ñ‰uì‰Eð9EtÇ.IÇFT,I‰EüÇEð   ‹‹IÿuÇL,Iÿu‰F‰F‹‹HÎè…øÿÿ‹Mô‹Æ^d‰    ÉÂ ¸±äDè6“åÿQQ3ÀV‹ñ‰uì‰Eð9EtÇ.IÇF.IÇFT,I‰EüÇEð   PPÿu‹ÎèRÿÿÿ‹F‹@‹MôÇDH,I‹‹@ÇP,I‹Æ^d‰    ÉÂ ¸ÃäDèÅ’åÿƒì SV‹ñ3Û9^D„Ý   8^I„Ô   ‹jÿÿPƒøÿu2ÀéÃ   SjMÔÇEè   ‰]äˆ]Ôèh÷ÿÿ‰]üWƒ}è‹EÔs?EÔ‹Ð‹ND‹9]ðS‹]äØSRFLPÿWƒè t#Ht$HHtp2ÛƒMüÿj jMÔèy2åÿŠÃ_ë_‹UÔëÁÆFI ƒ}è‹EÔsEÔ‹}ð+øtƒ}è‹EÔsEÔÿvTWjPè
8 ƒÄ;øu®€~I t…ÿ…oÿÿÿWjMÔè-Áÿÿé_ÿÿÿ³ëŽ°‹Mô^[d‰    ÉÃ¸åäDè¼‘åÿƒìXS‹]VWEäP‹Ëè‘Îÿÿƒeü PèêùÿÿƒMüÿY‹øMä‰}àèóÌÿÿEœP‹ÏèÎóÿÿ‹3öF‹Ï‰uüÿPƒeÈ ˆEèÇEÌ   ÆE¸ ‹} ŠÆEü<+t
<-tƒeì ë‰uìèU¡æÿ‹ ÿu0Š jeWˆEðfÇEñe è‡* ÿu0‹ð¾EðPWèw* ƒÄ‰E …Àu!E,ƒ}°‹EœsEœ€8„  ƒ}°‹EœsEœ€8 Ží   ÿu0M¸WèuÂÿÿ…öuj0ÿu,M¸èÀÿÿë'ƒ}  uj0ÿu$M¸è Àÿÿƒe$ j0ÿu,+÷VM¸èc¼ÿÿ‹u j0M¸…öu
ÿu$èØ¿ÿÿë ÿu(+÷FPè?¼ÿÿj0ÿu$M¸Vè1¼ÿÿƒe( ƒe$ ƒ}°‹}œs}œƒ}Ì‹E¸sE¸MðQPè~¥æÿYY‹ðë)„À~+‹Î+Mì¾À;Ásj +ðjVM¸èâ»ÿÿG€8 ~‹øŠ<uÑƒ}Ì‹}¸s}¸‹EÈƒe, ‰E0‹E$E(‹K$E,‹S E0…É|‹ò…öt‰MØ;Ðv
+Ð‰MØ‰U ëƒe  ‹C‹u%À  ƒø@tZ=   u+ƒ}ì v%jWÿuEÔÿuPVè¦ìÿÿ‹ƒÄ‰M‹@GÿM0‰Eÿu EÔÿuÿuÿuPVèAìÿÿ‹‰M‹@ƒÄƒe  ‰E¾Eðÿu0PWè¨( ƒÄ…À„Ÿ   ÿuè+ÇXCÿPWÿuEÔÿuPVèjìÿÿ‹ÿu$‰M‹Hj0‰Mÿpÿ0EÔPVèÚëÿÿ‹‰M‹@‹Mà‰E‹ƒÄ4ÿPˆE$‹Eÿu$‰EÜ‹EMÜ‰EàèÆâÿÿÿu(‹Mà‹EÜj0QP‰EEÔPV‰Mèëÿÿ‹‰M‹@ûƒÄ)]0‹]‰Eÿu0jeWèó' ƒÄ…À„…   ÿuè+Ç@‰E$HPWÿuEÔÿuPVè¶ëÿÿ‹ÿu,‰M‹Hj0‰Mÿpÿ0EÔPVè&ëÿÿ‹ƒe, ‰M‹HƒÄ4öC‰M¹$.Iu¹ .IjQÿpÿ0EÔPVè/ëÿÿ‹‰M‹@‰E‹E$ƒÄø)E0ÿuèEÔÿu0WÿuÿuPVè6ëÿÿ‹ÿu,‰M‹Hj0‰Mÿpÿ0EÔPVè¦êÿÿ‹ÿu ‰M‹@ÿu3ÿPQÿu‰EV‰{ ‰{$è‚êÿÿƒÄLWjM¸ÆEüèÜ-åÿƒMüÿWjMœèÍ-åÿ‹Mô‹E_^[d‰    ÉÃ¸ÿäDèpåÿƒì,S‹]VWEäP‹ËèEÊÿÿƒeü PèžõÿÿƒMüÿYMä‰Eèè©Èÿÿ‹MèEÈPèƒïÿÿ‹U Š3ÉA‰Mü<+t"<-t<0uŠB<xt<Xu	ÇE   ë	ƒe ë‰Mƒ}Ü‹MÈ‹ÁsEÈ€8trƒ}Ü‹ÁsEÈ€8 ~bƒ}Ür‰MðëEÈ‰Eð‹u$ëC„À~H‹Î+M¾À;Ás<+ð‹E$+Æ@P<WP‹E DPè#3 ‹EðƒÄÿE$@Æ €8 ~‰Eð‹U ‹EðŠ <u´‹C$‹{ ‹Ï…À|…Ét‰Eð;}$v+}$‰Eðë3ÿ‹C‹u%À  ƒø@th=   EìtWÿuÿuÿuPVèéÿÿƒÄë;ÿuRÿuÿuPVè*éÿÿ‹Wÿu‰M‹H‰Mÿp‹Mÿ0M )M$EìPVèÆèÿÿƒÄ0‹‰M‹@3ÿ‰E‹Mè‹ÿPˆEÿuEìÿu$ÿu ÿuÿuPVèéÿÿ‹Wÿu‰M‹@ƒc  ƒc$ PQÿu‰EVèpèÿÿƒMüÿƒÄ4j jMÈèÉ+åÿ‹Mô‹E_^[d‰    ÉÃ¸åDèl‹åÿQ‹M‰M‰Mðƒeü …ÉtÿuèÖúÿ‹Môd‰    ÉÃV‹t$ëjÿj ÿt$‹ÎèÝ(åÿƒÆ;t$uè^ÃU‹ìV‹u9utƒm‹Mjÿj ƒîVè²(åÿ;uuç‹E^]Ã¸HåDèðŠåÿQQSVW3ÿ3À‹ñG‰uì‰Eð9EtÇ0.IÇF(.IÇF`T,I‰Eü‰}ðP^S‹Îès÷ÿÿ‹‹@ÿu‹Ë‰}üÇ°-Iè
äÿÿ‹Mô_‹Æ^[d‰    ÉÂ ¸ZåDè}Šåÿƒì(S‹]V‹ñƒûÿu3Àë(‹F$‹W3ÿ;Ït,‹F4‹Ñ;Ês!ÿ‹v$‹H‰ˆ‹Ã_‹Mô^[d‰    ÉÂ 9~Tt‹Îè§Êÿÿ9~DuÿvTSè(äÿÿYY„ÀuÍƒÈÿëÊWjMÌˆ]ðÇEà   ‰}ÜÆEÌ èÚîÿÿ‰}üƒ}à‹EÌƒ‡   EÌ‹ÐEÜ‹ND‹9]ìSPREèPEñPEðPFLPÿW…Àx{ƒø`ƒ}à‹EÌsEÌ‹}ì+øtƒ}à‹EÌsEÌÿvTWjPè’/ ƒÄ;øuDEðÆFI9EèuR…ÿu‚ƒ}Ü s.WjMÌè­¸ÿÿélÿÿÿ‹UÌévÿÿÿƒøuÿvTÿuðèRãÿÿYY„ÀuƒÎÿƒMüÿj jMÌèj)åÿ‹Æéßþÿÿ‹uëäU‹ìQQV‹ñ‹F WNH3ÿ9uƒ}u9~DuƒEÿƒUÿ9~Ttf‹Îèöÿÿ„Àt[‹EEuƒ}tÿuÿuÿuÿvTè•0 ƒÄ…Àu5EøPÿvTè‹/ YY…Àu#‹Îè'Éÿÿ‹E‹Mø‰H‹Mü‰H‹NL‰8‰x‰Hë‹E‹XŠL‰‹\ŠL‰H‰x‰x‰x_^ÉÂ U‹ìQQ‹ES‹]V‹ñƒ~T ‰Eø‹EW‹}‰Eütnèqõÿÿ„ÀteEøPÿvTè~0 YY…ÀuS‹ÇÃtjSWÿvTèç/ ƒÄ…Àu:EøPÿvTèÝ. YY…Àu(‹E‹Î‰FLèsÈÿÿ‹E‹Møƒ  ƒ` ‰H‹Mü‰H‹NLë‹E‹XŠL‰‹\ŠL‰H3É‰H‰H_^‰H[ÉÂ  SV‹ñ3ÛW‹þ9^TtèÏôÿÿ„Àu3ÿÿvTèùæÿY…Àt3ÿ‹Îˆ^Pˆ^IèÉÿÿ‰^T¡T\‰FL‹Ç_‰^D^[Ã¸„åDè^‡åÿƒìLV‹u÷F @  ‰Mðu¶U‹RÿuVÿuÿuÿuÿPé%  SWEìP‹ÎèÄÿÿ3ÛP‰]üècïÿÿƒMüÿYMì‰EènÂÿÿÇEØ   ‰]Ôˆ]Ä‹M3ÿGE¨‰}üP8]tèÒèÿÿÆEüë	è°èÿÿÆEüPMÄèL(åÿSWM¨ÆEüèþ&åÿ‹F$‹N ‹UÔ;Ã|;Ëv‰Mè‰Eì;Êv	+Ê‰Eì‹ùë3ÿ‹F%À  ƒø@t&WÿuEèÿuÿuPÿuðèGãÿÿ‹‰M‹@ƒÄ‰E3ÿƒ}Ø‹EÄsEÄÿuÔPÿuEàÿuPÿuðèáâÿÿ‹Wÿu‰M‹@PQÿu‰Eÿuð‰^ ‰^$èóâÿÿƒMüÿƒÄ0SjMÄèM&åÿ_[‹Mô‹E^d‰    ÉÂ U‹ìƒìHV‹u‹FWÿu‹ùPh8.IEøPWèÙØÿÿƒÄPE¸j@PèMåÿPE¸PÿuVÿuÿuÿuWè4øÿÿ‹EƒÄ0_^ÉÂ U‹ìƒìHV‹u‹FWÿu‹ùPh<.IEøPWèƒØÿÿƒÄPE¸j@Pè÷åÿPE¸PÿuVÿuÿuÿuWèÞ÷ÿÿ‹EƒÄ0_^ÉÂ U‹ìƒì@V‹u‹FWÿu ‹ùÿuPh@.IEPWè*ØÿÿƒÄPEÀj@PèžåÿPEÀPÿuVÿuÿuÿuWè…÷ÿÿ‹EƒÄ4_^ÉÂ U‹ìƒì@V‹u‹FWÿu ‹ùÿuPhD.IEPWèÑ×ÿÿƒÄPEÀj@PèEåÿPEÀPÿuVÿuÿuÿuWè,÷ÿÿ‹EƒÄ4_^ÉÂ U‹ìƒì|SV‹u‹F‹V‰MðW¹    …À|…Òu
…NujZ3À‹Ø‹ú…Û|j$X;øv‰Eôë‰}ô‹EôÝE™+øÚ‹Vƒeø ƒeü ‹Â% 0  ;Á…³   Ý€tGØÉÙÁÚéßàöÄD‹œ   ÙîØÑßàöÄAu
ÙÉÆEÙàëÆE ÙÉÝ`.Ij
ØÑYßàöÄA{ÝØë!ÙÉ}øˆ  sñMøÙÉÜX.IØÑßàöÄtáÝÙØÑßàÝÙöÄAu7…Û|3~-ÝP.IØÙßàöÄu"}üˆ  sÜH.IƒÇöƒÓÿMü…ÛÕ|;ùsÏ€} tÙàQQÝ$ÿuôERj PÿuðèÖÿÿƒÄPE„jlPèÜåÿPWÿuüE„ÿuøPÿuVÿuÿuÿuÿuðènñÿÿ‹EƒÄD_^[ÉÂ U‹ìƒì|SV‹u‹F‹V‰MðW¹    …À|…Òu
…NujZ3À‹Ø‹ú…Û|j$X;øv‰Eôë‰}ô‹Eô™+øÚ‹Vƒeø ƒeü ‹Â% 0  ;Á…   ÙîÝEØÑßàöÄzÆEÙàÝ`.Ij
ØÑYßàöÄA{ÝØë'ÆE ëäÙÉ}øˆ  sëMøÙÉÜX.IØÑßàöÄtáÝÙØÑßàÝÙöÄAu7…Û|3~-ÝP.IØÙßàöÄu"}üˆ  sÜH.IƒÇöƒÓÿMü…ÛÕ|;ùsÏ€} tÙàëÝEQQÝ$ÿuôERjLPÿuðè§ÔÿÿƒÄPE„jlPèŒåÿPWÿuüE„ÿuøPÿuVÿuÿuÿuÿuðèðÿÿ‹EƒÄD_^[ÉÂ U‹ìƒì@VÿuEÀhh.Ij@P‹ñè5ŒåÿPEÀPÿuÿuÿuÿuÿuVèôÿÿ‹EƒÄ0^ÉÂ U‹ìQÿq‹Mƒeü è!åÿ‹EÉÂ U‹ìQÿq‹Mƒeü èø åÿ‹EÉÂ U‹ìQÿq‹Mƒeü èÞ åÿ‹EÉÂ ÿt$ÿt$è¸õÿÿYYÂ éÞõÿÿU‹ìQÿuüÿuÿuÿuèêõÿÿƒÄÉÃV‹ñNèNùÿÿ…Àu‹‹HÎ‹AƒÈƒy8 uƒÈj Pè³Æÿÿ^Ã¸–åDèÑ€åÿQV‹ñ‰uðÇ´-Iƒeü ƒ~T tè6Áÿÿ€~P t‹Îè÷øÿÿƒMüÿ‹ÎèÈÿÿ‹Mô^d‰    ÉÃ¸ÇåDè†€åÿQQSVW3ÿ3Û‹ñG‰uì‰]ð9]tÇp.IÇFhT,I‰]ü‰}ðSFSP‹Îè§ìÿÿ‹‹@SN‰}üÇl.IèÚÿÿ‹Mô_‹Æ^[d‰    ÉÂ ¸ÜåDè€åÿQ‹A˜‹@V‰MðÇD˜l.Iƒeü q¨‹Îèÿÿÿ‹Fð‹@‹MôÇD0ðL,I^d‰    ÉÃV‹ñèûþÿÿöD$tVèäråÿY‹Æ^Â ¸æDè¸åÿƒì<S3Û‰]ð‹Q@VWöÂuH‹A$9tA‹ ‹Q<;Ðr‹Â‹I‹	+ÁPQM¸èí°ÿÿ‹}3öE¸FÇG   ‰_P‹Ï‰uüˆèã åÿM¸ëföÂu2‹Q 9t+‹A0‹ ‹I‹	+ÁPQMÔè¤°ÿÿ‹}ÇEü   ÇG   ëjX‰Eè‰]äˆ]Ô‹}ÇEü   ‰GEÔ‰_P‹Ïˆè~ åÿ3öMÔFSV‰uðˆ]üè+åÿ‹Mô‹Ç_^[d‰    ÉÂ U‹ìQSVW‹}€) ‰Müt
hü7GèŸø Mè¦÷ôÿ‹€z) t‹_ë‹G€x) t‹Úë
‹E‹X;Çuv€{) ‹wu‰s‹Mü‹A9xu‰Xë9>u‰ë‰^‹I99u€{) t‹Öë‹‹Óë‹Ð‹€x) tö‰‹Eü‹H9yu{€{) t‹Öë‹C‹Óë‹Ð‹B€x) tõ‰QëZ‰B‹‰;Gu‹ðë€{) ‹pu‰s‰‹W‰P‹O‰A‹Mü‹I9yu‰Aë‹O99u‰ë‰A‹O‰HŠW(ŠH(ˆP(ˆO(€(…  é²   €{(…ð   ‹;Øul‹F€x( uÆ@(VÆF( è¾Îÿÿ‹F€x) u}‹€y(u	‹H€y(th‹H€y(u‹ÆA(‹MüPÆ@( èÊÎÿÿ‹FŠN(ˆH(‹MüÆF(‹@VÆ@(èkÎÿÿë~€x( uÆ@(VÆF( è—Îÿÿ‹€x) u‹H€y(u"‹€y(uÆ@( ‹Þ‹v‹Mü‹A;X…?ÿÿÿë7‹€y(u‹HÆA(‹MüPÆ@( èÎÿÿ‹ŠN(ˆH(‹MüÆF(‹ VÆ@(è-ÎÿÿÆC(j OjèæåÿWèºoåÿY‹Mü‹A_^[…ÀtH‰A‹E‹M‰ÉÂ U‹ìSVW‹ù‹G=H’$	r!‹uj Njè¡åÿVèuoåÿÇ$Ô7Gèæõ ‹]@‰G‹E‰C‹O;Áu‰Y‹G‰‹G‰Xë!€} t‰‹O;u‰ë‰X‹O;Au‰Y‹C‹óé“   ‹F‹H;u8‹I€y( t7;pu
‹ðV‹ÏèÍÿÿ‹FÆ@(‹F‹@Æ@( ‹Fÿp‹Ïè7ÍÿÿëN‹	€y( uÆ@(ÆA(‹F‹@Æ@( ‹F‹pë,;0u
‹ðV‹ÏèÍÿÿ‹FÆ@(‹F‹@Æ@( ‹Fÿp‹Ïè¥Ìÿÿ‹F€x( „cÿÿÿ‹G‹@_Æ@(‹E^‰[]Â SVW‹|$€) ‹Ù‹÷u'ÿv‹Ëèåÿÿÿ‹6j OjèdåÿWè8nåÿ€~) Y‹þtÙ_^[Â ÿt$ÿt$è‹ïÿÿYYÃU‹ìëÿuÿuèxïÿÿÿMƒEYYƒ} wæ]Ã¸zæDèÓzåÿì\  SV‹uW…˜þÿÿP3Ûh  ‰]ü‰]ðÿ .cj_…˜þÿÿ‰~‰^P‹ÎˆèXåÿjhˆ.IMÔ‰]üÇEð   ‰}è‰]äˆ]ÔènåÿEÔPEœVPÇEü   èwL  ƒÄP‹ÎÆEüèÏåÿSjMœÆEüè€åÿSjMÔˆ]üèråÿjh€.IMÔ‰}è‰]äˆ]ÔèåÿEÔPEœVPÇEü   èL  ƒÄP‹ÎÆEüèvåÿSjMœÆEüè'åÿˆ]üSjMÔèåÿh  …˜þÿÿPSÿè.c…˜þÿÿ‰}Ì‰]Èˆ]¸xŠ@:Ëuù+ÇP…˜þÿÿPM¸è–åÿƒÏÿWht)IM¸ÇEü   èoÐÿÿ;ÇtgW@PEœPM¸èˆúÿPM¸ÆEüèãåÿSjMœÆEüè”åÿShx.IM¸èGúÿ;Çt*PSEœPM¸èLúÿPM¸ÆEüè§åÿSjMœÆEüèXåÿE¸PEœVPè#K  ƒÄP‹ÎÆEüè{åÿSjMœÆEüè,åÿSjM¸ˆ]üèåÿ‹Mô_‹Æ^[d‰    ÉÃVqh‹ÎèøÿÿVÇ$,Iè8ÿ Y^ÃU‹ìQÿuƒeü ƒÁèÝøÿÿ‹EÉÂ ¸«æDèŽxåÿQQSVW3ÿ3Û‹ñG‰uì‰]ð9]tÇ”.IÇF`T,I‰]ü‰}ðSFSP‹ÎèMäÿÿ‹‹@SN‰}üÇ.IèÒÿÿ‹Mô_‹Æ^[d‰    ÉÂ ¸ÀæDè"xåÿQV‹ñ‹F ‹@‰uðÇD0 .Iƒeü N¤è&÷ÿÿ‹F ‹@‹MôÇD0 H,I^d‰    ÉÃVq˜‹ÎèÿÿÿöD$tVèéjåÿY‹Æ^Â VW‹ù‹Gÿpèlüÿÿ‹G‰@‹G‰ ‹w‰vƒg _^ÃVèVÉÿÿÿt$‹ðFPèìÿÿYY‹Æ^Â V‹t$W‹|$…ÿtÿt$VèõëÿÿYƒÆOYuî_^ÃVq`‹Îè)ÿÿÿVÇ$,IèÌý Y^Ã¸ÕæDè<wåÿìà   Vj,ÿÿÿè˜öÿÿj@j!ÿu3ö,ÿÿÿ‰uüè‘âÿÿ‹…,ÿÿÿ‹@‹„8ÿÿÿ;Æue¨ua9ut\VVV,ÿÿÿèÎÿÿEÜP,ÿÿÿè¾ÿÿjVV,ÿÿÿèóÍÿÿ…ÿÿÿP,ÿÿÿèî½ÿÿ‹p+uÜƒMüÿ+uä,ÿÿÿ0èÐýÿÿ‹ÆëƒMüÿ,ÿÿÿè½ýÿÿ3À‹Mô^d‰    ÉÃ‹D$ƒxr‹ Pè"ÿÿÿ™Y;T$rw;D$v3À@ë3ÀÂ Vq ‹ÎèãþÿÿöD$tVèQiåÿY‹Æ^Â U‹ìQV‹ñ‹F‹M;u9Eu‹ÎèKþÿÿ‹F‹ë%;Mt W‹ùMèïîôÿWEüP‹Îè÷ÿÿ‹M;Muâ_‹E‰^ÉÂ U‹ìVÿu‹ñè.þÿÿPÿu‹Îÿuÿuè<ùÿÿ‹E^]Â U‹ìƒÁQÿuÿuÿuèþÿÿ‹EkÀƒÄE]Â Uƒì8¸NçDè‚uåÿì  SVW3öFVjàýÿÿènêÿÿ3ÛS‰]üè YRPðýÿÿè.ÞÿÿEPèiúÿÿEPÆEüèÉ YYE PàýÿÿèvüÿÿPEP…0ÿÿÿPÆEüè-G  ƒÄPMÆEüè„åÿSV0ÿÿÿÆEüè3åÿSVM ÆEüè%åÿj¿¤/IWMèq¦ÿÿƒ}0‹EsESSjSSh   @Pÿ,/c‰E8;Ã„A  ÿuPÿuDPè²ÿÿƒÄÿu8ÿ(/ch  …ÜüÿÿPSÿè.cVjLÿÿÿèxéÿÿ…\ÿÿÿh”/IPÆEüèØÙÿÿ…\ÿÿÿh„/IPèÇÙÿÿ…ÜüÿÿP…\ÿÿÿPè´Ùÿÿ…\ÿÿÿht/IPè£Ùÿÿ…\ÿÿÿh`/IPè’Ùÿÿ‹ELƒÄ(;Ãu¸Ú>GP…\ÿÿÿPèvÙÿÿ…\ÿÿÿhH/IPèeÙÿÿ…\ÿÿÿh</IPèTÙÿÿƒÄE PàýÿÿèûÿÿP…\ÿÿÿPÆEüè«ÚÿÿYYSVM ÆEüèçåÿ…\ÿÿÿh,/IPèÙÿÿ…\ÿÿÿh/IPèÿØÿÿE ƒÄPè ‹Èèë P…\ÿÿÿPÆEüèUÚÿÿYYSVM ÆEüè‘åÿ…\ÿÿÿh/IPèºØÿÿ…\ÿÿÿhü.IPè©Øÿÿ‹EH‹@‹ƒÄ‰M8;ÈtK…\ÿÿÿhð.IPè†Øÿÿ‹E8ƒÀP…\ÿÿÿPèíÙÿÿ…\ÿÿÿhä.IPèbØÿÿƒÄM8èÐëôÿ‹EH‹@9E8uµ…\ÿÿÿhÔ.IPè;Øÿÿ…\ÿÿÿhÄ.IPè*Øÿÿ…\ÿÿÿWPèØÿÿ…\ÿÿÿh°.IPèØÿÿ…\ÿÿÿhœ.IPèû×ÿÿE Pèš÷ÿÿƒÄ,PMÆEüèèåÿSVM ÆEüèšåÿ…0ÿÿÿPàýÿÿè•ùÿÿPEPE PÆEüèOD  ƒÄPMÆEü	è¦åÿSVM ÆEüèXåÿSV0ÿÿÿÆEüèGåÿjh7GMè”£ÿÿVˆþÿÿèSùÿÿƒ}0‹EÆEü
sEj@jPˆþÿÿèwÛÿÿ9àþÿÿt;E PLÿÿÿèùÿÿP…ˆþÿÿPÆEüè˜ØÿÿYYSVM ÆEü
èÔåÿˆþÿÿè~ðÿÿˆþÿÿÆEüèúÿÿLÿÿÿÆEüèÊÑÿÿSVMˆ]üèžåÿƒMüÿàýÿÿè®Ñÿÿ‹Mô_^3À[d‰    ƒÅ<ÉÃU‹ìQSVW‹ù‹G‹p‹ØÆEüë:€} ‹ÞtÿuNèà˜ÿÿ…À™Eüë‹MFPèÌ˜ÿÿ…À˜Eü€}ü t‹6ë‹v€~) tÀ€}ü ‹ó‰ut9‹G;u'ÿu‹Ïè'ùÿÿPSjEP‹Ïè7ôÿÿ‹E‹M‰Æ@ë4MèvÁÿÿ‹uÿuNèd˜ÿÿ…Àyÿu‹ÏèæøÿÿPSÿuüë¼‹E‰0Æ@ _^[ÉÂ Q‹A‹PRD$Pè.úÿÿYÃ¸hçDèQpåÿƒì<VW‹}‹ñ…ÿ„»  ‹FS‹+Ãj™Y÷ù¹I’$	+È;Ïs
hØºGè´é 8‹F+Ãj™[÷û;Áƒ­   Q‹Îè¡ûùÿ‹Øj Sè¶òùÿ‰E‹E+YYjY™÷ùÿu‹ÎW‰EðkÀEPèúÿÿFPÿuÿuÿ6èäúÿFP‹EðÇkÀEPÿvÿuèÉúÿ‹!IS_NEWLINE(ptr)) &&
              ((ctypes[c] & toptable1[d]) ^ toptable2[d]) != 0))
          {
          if (codevalue == OP_TYPEPOSSTAR)
            {
            active_count--;            /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW(state_offset, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_TYPEEXACT:
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        if ((c >= 256 && d != OP_DIGIT && d != OP_WHITESPACE && d != OP_WORDCHAR) ||
            (c < 256 &&
              (d != OP_ANY || !IS_NEWLINE(ptr)) &&
              ((ctypes[c] & toptable1[d]) ^ toptable2[d]) != 0))
          {
          if (++count >= GET2(code, 1))
            { ADD_NEW(state_offset + 4, 0); }
          else
            { ADD_NEW(state_offset, count); }
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_TYPEUPTO:
      case OP_TYPEMINUPTO:
      case OP_TYPEPOSUPTO:
      ADD_ACTIVE(state_offset + 4, 0);
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        if ((c >= 256 && d != OP_DIGIT && d != OP_WHITESPACE && d != OP_WORDCHAR) ||
            (c < 256 &&
              (d != OP_ANY || !IS_NEWLINE(ptr)) &&
              ((ctypes[c] & toptable1[d]) ^ toptable2[d]) != 0))
          {
          if (codevalue == OP_TYPEPOSUPTO)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          if (++count >= GET2(code, 1))
            { ADD_NEW(state_offset + 4, 0); }
          else
            { ADD_NEW(state_offset, count); }
          }
        }
      break;

/* ========================================================================== */
      /* These are virtual opcodes that are used when something like
      OP_TYPEPLUS has OP_PROP, OP_NOTPROP, OP_ANYNL, or OP_EXTUNI as its
      argument. It keeps the code above fast for the other cases. The argument
      is in the d variable. */

#ifdef SUPPORT_UCP
      case OP_PROP_EXTRA + OP_TYPEPLUS:
      case OP_PROP_EXTRA + OP_TYPEMINPLUS:
      case OP_PROP_EXTRA + OP_TYPEPOSPLUS:
      count = current_state->count;           /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 4, 0); }
      if (clen > 0)
        {
        BOOL OK;
        int category = _pcre_ucp_findprop(c, &chartype, &script);
        switch(code[2])
          {
          case PT_ANY:
          OK = TRUE;
          break;

          case PT_LAMP:
          OK = chartype == ucp_Lu || chartype == ucp_Ll || chartype == ucp_Lt;
          break;

          case PT_GC:
          OK = category == code[3];
          break;

          case PT_PC:
          OK = chartype == code[3];
          break;

          case PT_SC:
          OK = script == code[3];
          break;

          /* Should never occur, but keep compilers from grumbling. */

          default:
          OK = codevalue != OP_PROP;
          break;
          }

        if (OK == (d == OP_PROP))
          {
          if (count > 0 && codevalue == OP_PROP_EXTRA + OP_TYPEPOSPLUS)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW(state_offset, count);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_EXTUNI_EXTRA + OP_TYPEPLUS:
      case OP_EXTUNI_EXTRA + OP_TYPEMINPLUS:
      case OP_EXTUNI_EXTRA + OP_TYPEPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 2, 0); }
      if (clen > 0 && _pcre_ucp_findprop(c, &chartype, &script) != ucp_M)
        {
        const uschar *nptr = ptr + clen;
   
            fprintf(mStream, _T("%s, "), floatVal(f));
    }
    fprintf(mStream, _T("]\n"));
        
    Indent(level+1);
    fprintf(mStream, _T("emissiveColor [ "));
    for(i = 0; i < num; i++) {
        sub = mtl->GetSubMtl(i);
        if (!sub)
            continue;
        c = sub->GetDiffuse(mStart);
        float si;
        if (sub->ClassID() == Class_ID(DMTL_CLASS_ID, 0)) {
            StdMat* stdMtl = (StdMat *) sub;
            si = stdMtl->GetSelfIllum(mStart);
        }
        else
            si = 0.0f;
        Point3 p = si * Point3(c.r, c.g, c.b);
        if (i == num - 1)
            fprintf(mStream, _T("%s "), color(p));
        else
            fprintf(mStream, _T("%s, "), color(p));
    }
    fprintf(mStream, _T("]\n"));
        
    Indent(level);
    fprintf(mStream, _T("}\n"));
}

void
VRBLExport::OutputNoTexture(int level)
{
    Indent(level);
    fprintf(mStream, _T("Texture2 {}\n"));
}

// Output the matrial definition for a node.
BOOL
VRBLExport::OutputMaterial(INode* node, BOOL& twoSided, int level)
{
    Mtl* mtl = node->GetMtl();
    twoSided = FALSE;

    // If no material is assigned, use the wire color
    if (!mtl || (mtl->ClassID() != Class_ID(DMTL_CLASS_ID, 0) &&
                 !mtl->IsMultiMtl())) {
        Color col(node->GetWireColor());
        Indent(level);
        fprintf(mStream, _T("Material {\n"));
        Indent(level+1);
        fprintf(mStream, _T("diffuseColor %s\n"), color(col));
        Indent(level+1);
        fprintf(mStream, _T("specularColor .9 .9 .9\n"));
        Indent(level);
        fprintf(mStream, _T("}\n"));
        OutputNoTexture(level);
        return FALSE;
    }

    if (mtl->IsMultiMtl()) {
        OutputMultiMtl(mtl, level);
        OutputNoTexture(level);
        return TRUE;
    }

    StdMat* sm = (StdMat*) mtl;
    twoSided = sm->GetTwoSided();
    Interval i = FOREVER;
    sm->Update(0, i);
    Indent(level);
    fprintf(mStream, _T("Material {\n"));
    Color c;

    Indent(level+1);
    c = sm->GetAmbient(mStart);
    fprintf(mStream, _T("ambientColor %s\n"), color(c));
    Indent(level+1);
    c = sm->GetDiffuse(mStart);
    fprintf(mStream, _T("diffuseColor %s\n"), color(c));
    Indent(level+1);
    c = sm->GetSpecular(mStart);
    fprintf(mStream, _T("specularColor %s\n"), color(c));
    Indent(level+1);
    fprintf(mStream, _T("shininess %s\n"),
            floatVal(sm->GetShininess(mStart)));
    Indent(level+1);
    fprintf(mStream, _T("transparency %s\n"),
            floatVal(1.0f - sm->GetOpacity(mStart)));
    float si = sm->GetSelfIllum(mStart);
    if (si > 0.0f) {
        Indent(level+1);
        c = sm->GetDiffuse(mStart);
        Point3 p = si * Point3(c.r, c.g, c.b);
        fprintf(mStream, _T("emissiveColor %s\n"), color(p));
    }
    Indent(level);
    fprintf(mStream, _T("}\n"));


    TextureDesc* td = GetMatTex(node);
    if (!td) {
        OutputNoTexture(level);
        return FALSE;
    }

    Indent(level);
    fprintf(mStream, _T("Texture2 {\n"));
    Indent(level+1);
    fprintf(mStream, _T("filename \"%s\"\n"), td->url);
    Indent(level);
    fprintf(mStream, _T("}\n"));

    BitmapTex* bm = td->tex;
    delete td;

    StdUVGen* uvGen = bm->GetUVGen();
    if (!uvGen) {
        return FALSE;
    }

    // Get the UV offset and scale value for Texture2Transform
    float uOff = uvGen->GetUOffs(mStart);
    float vOff = uvGen->GetVOffs(mStart);
    float uScl = uvGen->GetUScl(mStart);
    float vScl = uvGen->GetVScl(mStart);
    float ang =  uvGen->GetAng(mStart);

    if (uOff == 0.0f && vOff == 0.0f && uScl == 1.0f && vScl == 1.0f &&
        ang == 0.0f) {
        return FALSE;
    }

    Indent(level);
    fprintf(mStream, _T("Texture2Transform {\n"));
    if (uOff != 0.0f || vOff != 0.0f) {
        Indent(level+1);
        UVVert p = UVVert(uOff, vOff, 0.0f);
        fprintf(mStream, _T("translation %s\n"), texture(p));
    }
    if (ang != 0.0f) {
        Indent(level+1);
        fprintf(mStream, _T("rotation %s\n"), floatVal(ang));
    }
    if (uScl != 1.0f || vScl != 1.0f) {
        Indent(level+1);
        UVVert p = UVVert(uScl, vScl, 0.0f);
        fprintf(mStream, _T("scaleFactor %s\n"), texture(p));
    }
    Indent(level);
    fprintf(mStream, _T("}\n"));

    return FALSE;
}

// Create a VRMNL primitive sphere, if appropriate.  
// Returns TRUE if a primitive is created
BOOL
VRBLExport::VrblOutSphere(INode * node, Object *obj, int level)
{
    SimpleObject* so = (SimpleObject*) obj;
    float radius, hemi;
    int basePivot, genUV, smooth;
    BOOL td = HasTexture(node);

    // Reject "base pivot" mapped, non-smoothed and hemisphere spheres
    so->pblock->GetValue(SPHERE_RECENTER, mStart, basePivot, FOREVER);
    so->pblock->GetValue(SPHERE_GENUVS, mStart, genUV, FOREVER);
    so->pblock->GetValue(SPHERE_HEMI, mStart, hemi, FOREVER);
    so->pblock->GetValue(SPHERE_SMOOTH, mStart, smooth, FOREVER);
    if (!smooth || basePivot || (genUV && td) || hemi > 0.0f)
        return FALSE;

    so->pblock->GetValue(SPHERE_RADIUS, mStart, radius, FOREVER);
    
    Indent(level);

    fprintf(mStream, _T("Sphere { radius %s }\n"), floatVal(radius));
 
    return TRUE;
}

// Create a VRMNL primitive cylinder, if appropriate.  
// Returns TRUE if a primitive is created
BOOL
VRBLExport::VrblOutCylinder(INode* node, Object *obj, int level)
{
    SimpleObject* so = (SimpleObject*) obj;
    float radius, height;
    int sliceOn, genUV, smooth;
    BOOL td = HasTexture(node);

    // Reject sliced, non-smooth and mapped cylinders
    so->pblock->GetValue(CYLINDER_GENUVS, mStart, genUV, FOREVER);
    so->pblock->GetValue(CYLINDER_SLICEON, mStart, sliceOn, FOREVER);
    so->pblock->GetValue(CYLINDER_SMOOTH, mStart, smooth, FOREVER);
    if (sliceOn || (genUV && td) || !smooth)
        return FALSE;

    so->pblock->GetValue(CYLINDER_RADIUS, mStart, radius, FOREVER);
    so->pblock->GetValue(CYLINDER_HEIGHT, mStart, height, FOREVER);
    Indent(level);
    fprintf(mStream, _T("Separator {\n"));
    Indent(level+1);
    if (mZUp) {
        fprintf(mStream, _T("Rotation { rotation 1 0 0 %s }\n"),
                floatVal(float(PI/2.0)));
        Indent(level+1);
        fprintf(mStream, _T("Translation { translation 0 %s 0 }\n"),
                floatVal(float(height/2.0)));
    } else {
        Point3 p = Point3(0.0f, 0.0f, height/2.0f);
        fprintf(mStream, _T("Translation { translation %s }\n"), point(p));
    }
    Indent(level+1);
    fprintf(mStream, _T("Cylinder { radius %s "), floatVal(radius));
    fprintf(mStream, _T("height %s }\n"), floatVal(float(fabs(height))));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    
    return TRUE;
}

// Create a VRMNL primitive cone, if appropriate.  
// Returns TRUE if a primitive is created
BOOL
VRBLExport::VrblOutCone(INode* node, Object *obj, int level)
{
    SimpleObject* so = (SimpleObject*) obj;
    float radius1, radius2, height;
    int sliceOn, genUV, smooth;
    BOOL td = HasTexture(node);

    // Reject sliced, non-smooth and mappeded cones
    so->pblock->GetValue(CONE_GENUVS, mStart, genUV, FOREVER);
    so->pblock->GetValue(CONE_SLICEON, mStart, sliceOn, FOREVER);
    so->pblock->GetValue(CONE_SMOOTH, mStart, smooth, FOREVER);
    so->pblock->GetValue(CONE_RADIUS2, mStart, radius2, FOREVER);
    if (sliceOn || (genUV &&td) || !smooth || radius2 > 0.0f)
        return FALSE;

    so->pblock->GetValue(CONE_RADIUS1, mStart, radius1, FOREVER);
    so->pblock->GetValue(CONE_HEIGHT, mStart, height, FOREVER);
    Indent(level);
    
    fprintf(mStream, _T("Separator {\n"));
    Indent(level+1);
    if (mZUp) {
        if (height > 0.0f)
            fprintf(mStream, _T("Rotation { rotation 1 0 0 %s }\n"),
                    floatVal(float(PI/2.0)));
        else
            fprintf(mStream, _T("Rotation { rotation 1 0 0 %s }\n"),
                    floatVal(float(-PI/2.0)));
        Indent(level+1);
        fprintf(mStream, _T("Translation { translation 0 %s 0 }\n"),
                floatVal(float(fabs(height)/2.0)));
    } else {
        Point3 p = Point3(0.0f, 0.0f, (float)fabs(height)/2.0f);
        fprintf(mStream, _T("Translation { translation %s }\n"), point(p));
    }
    Indent(level+1);

    fprintf(mStream, _T("Cone { bottomRadius %s "), floatVal(radius1));
    fprintf(mStream, _T("height %s }\n"), floatVal(float(fabs(height))));
    
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Create a VRMNL primitive cube, if appropriate.  
// Returns TRUE if a primitive is created
BOOL
VRBLExport::VrblOutCube(INode* node, Object *obj, int level)
{
    Mtl* mtl = node->GetMtl();
    // Multi materials need meshes
    if (mtl && mtl->IsMultiMtl())
        return FALSE;

    SimpleObject* so = (SimpleObject*) obj;
    float length, width, height;
    BOOL td = HasTexture(node);

    int genUV, lsegs, wsegs, hsegs;
    so->pblock->GetValue(BOXOBJ_GENUVS, mStart, genUV, FOREVER);
    so->pblock->GetValue(BOXOBJ_LSEGS,  mStart, lsegs, FOREVER);
    so->pblock->GetValue(BOXOBJ_WSEGS,  mStart, hsegs, FOREVER);
    so->pblock->GetValue(BOXOBJ_HSEGS,  mStart, wsegs, FOREVER);
    if ((genUV && td) || lsegs > 1 || hsegs > 1 || wsegs > 1)
        return FALSE;

    so->pblock->GetValue(BOXOBJ_LENGTH, mStart, length, FOREVER);
    so->pblock->GetValue(BOXOBJ_WIDTH, mStart,  width,  FOREVER);
    so->pblock->GetValue(BOXOBJ_HEIGHT, mStart, height, FOREVER);
    Indent(level);
    fprintf(mStream, _T("Separator {\n"));
    Indent(level+1);
    Point3 p = Point3(0.0f,0.0f,height/2.0f);
    // VRML cubes grow from the middle, MAX grows from z=0
    fprintf(mStream, _T("Translation { translation %s }\n"), point(p));
    Indent(level+1);

    if (mZUp) {
        fprintf(mStream, _T("Cube { width %s "),
                floatVal(float(fabs(width))));
        fprintf(mStream, _T("height %s "),
                floatVal(float(fabs(length))));
        fprintf(mStream, _T(" depth %s }\n"),
                floatVal(float(fabs(height))));
    } else {
        fprintf(mStream, _T("Cube { width %s "),
                floatVal(float(fabs(width))));
        fprintf(mStream, _T("height %s "),
                floatVal(float(fabs(height))));
        fprintf(mStream, _T(" depth %s }\n"),
                floatVal(float(fabs(length))));
    }
    Indent(level);
    fprintf(mStream, _T("}\n"));
    
    return TRUE;
}

// Output a perspective camera
BOOL
VRBLExport::VrblOutCamera(INode* node, Object* obj, int level)
{
    // compute camera transform
    ViewParams vp;
    CameraState cs;
    Interval iv;
    CameraObject *cam = (CameraObject *)obj;
    cam->EvalCameraState(0, iv, &cs);
    vp.fov = cs.fov / 1.3333f;

    Indent(level);
    fprintf(mStream, _T("DEF %s_Animated PerspectiveCamera {\n"), mNodes.GetNodeName(node));
    Indent(level+1);
    fprintf(mStream, _T("position 0 0 0\n"));
    Indent(level+1);
    fprintf(mStream, _T("heightAngle %s\n"), floatVal(vp.fov));
    if (!mZUp) {
        Indent(level+1);
        fprintf(mStream, _T("orientation 1 0 0 %s\n"),
                floatVal(float(-PI/2.0)));
    }
    Indent(level);
    fprintf(mStream, _T("}\n"));

    return TRUE;
}

// Output an omni light
BOOL
VRBLExport::VrblOutPointLight(INode* node, LightObject* light, int level)
{
    LightState ls;
    Interval iv = FOREVER;

    light->EvalLightState(mStart, iv, &ls);

    Indent(level);
    fprintf(mStream, _T("DEF %s PointLight {\n"), mNodes.GetNodeName(node));
    Indent(level+1);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart, FOREVER)));
    Indent(level+1);
    Point3 col = light->GetRGBColor(mStart, FOREVER);
    fprintf(mStream, _T("color %s\n"), color(col));
    Indent(level+1);
    fprintf(mStream, _T("location 0 0 0\n"));

    Indent(level+1);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Output a directional light
BOOL
VRBLExport::VrblOutDirectLight(INode* node, LightObject* light, int level)
{
    LightState ls;
    Interval iv = FOREVER;

    light->EvalLightState(mStart, iv, &ls);

    Indent(level);
    fprintf(mStream, _T("DEF %s DirectionalLight {\n"),  mNodes.GetNodeName(node));
    Indent(level+1);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart, FOREVER)));
    Indent(level+1);
    Point3 col = light->GetRGBColor(mStart, FOREVER);

    fprintf(mStream, _T("color %s\n"), color(col));

    Indent(level+1);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Output a Spot Light
BOOL
VRBLExport::VrblOutSpotLight(INode* node, LightObject* light, int level)
{
    LightState ls;
    Interval iv = FOREVER;

    Point3 dir(0,0,-1);
    light->EvalLightState(mStart, iv, &ls);
    Indent(level);
    fprintf(mStream, _T("DEF %s SpotLight {\n"),  mNodes.GetNodeName(node));
    Indent(level+1);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart,FOREVER)));
    Indent(level+1);
    Point3 col = light->GetRGBColor(mStart, FOREVER);
    fprintf(mStream, _T("color %s\n"), color(col));
    Indent(level+1);
    fprintf(mStream, _T("location 0 0 0\n"));
    Indent(level+1);
    fprintf(mStream, _T("direction %s\n"), normPoint(dir));
    Indent(level+1);
    fprintf(mStream, _T("cutOffAngle %s\n"),
            floatVal(DegToRad(ls.fallsize)));
    Indent(level+1);
    fprintf(mStream, _T("dropOffRate %s\n"),
            floatVal(1.0f - ls.hotsize/ls.fallsize));
    Indent(level+1);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Output an omni light at the top-level Separator
BOOL
VRBLExport::VrblOutTopPointLight(INode* node, LightObject* light)
{
    LightState ls;
    Interval iv = FOREVER;

    light->EvalLightState(mStart, iv, &ls);

    Indent(1);
    fprintf(mStream, _T("DEF %s PointLight {\n"),  mNodes.GetNodeName(node));
    Indent(2);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart, FOREVER)));
    Indent(2);
    Point3 col = light->GetRGBColor(mStart, FOREVER);
    fprintf(mStream, _T("color %s\n"), color(col));
    Indent(2);
    Point3 p = node->GetObjTMAfterWSM(mStart).GetTrans();
    fprintf(mStream, _T("location %s\n"), point(p));

    Indent(2);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(1);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Output a directional light at the top-level Separator
BOOL
VRBLExport::VrblOutTopDirectLight(INode* node, LightObject* light)
{
    LightState ls;
    Interval iv = FOREVER;

    light->EvalLightState(mStart, iv, &ls);

    Indent(1);
    fprintf(mStream, _T("DEF %s DirectionalLight {\n"),  mNodes.GetNodeName(node));
    Indent(2);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart, FOREVER)));
    Indent(2);
    Point3 col = light->GetRGBColor(mStart, FOREVER);
    fprintf(mStream, _T("color %s\n"), color(col));
    Point3 p = Point3(0,0,-1);

    Matrix3 tm = node->GetObjTMAfterWSM(mStart);
    Point3 trans, s;
    Quat q;
    AffineParts parts;
    decomp_affine(tm, &parts);
    q = parts.q;
    Matrix3 rot;
    q.MakeMatrix(rot);
    p = p * rot;
    
    Indent(2);
    fprintf(mStream, _T("direction %s\n"), normPoint(p));
    Indent(2);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(1);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Output a spot light at the top-level Separator
BOOL
VRBLExport::VrblOutTopSpotLight(INode* node, LightObject* light)
{
    LightState ls;
    Interval iv = FOREVER;

    light->EvalLightState(mStart, iv, &ls);
    Indent(1);
    fprintf(mStream, _T("DEF %s SpotLight {\n"),  mNodes.GetNodeName(node));
    Indent(2);
    fprintf(mStream, _T("intensity %s\n"),
            floatVal(light->GetIntensity(mStart,FOREVER)));
    Indent(2);
    Point3 col = light->GetRGBColor(mStart, FOREVER);
    fprintf(mStream, _T("color %s\n"), color(col));
    Indent(2);
    Point3 p = node->GetObjTMAfterWSM(mStart).GetTrans();
    fprintf(mStream, _T("location %s\n"), point(p));

    Matrix3 tm = node->GetObjTMAfterWSM(mStart);
    p = Point3(0,0,-1);
    Point3 trans, s;
    Quat q;
    AffineParts parts;
    decomp_affine(tm, &parts);
    q = parts.q;
    Matrix3 rot;
    q.MakeMatrix(rot);
    p = p * rot;

    Indent(2);
    fprintf(mStream, _T("direction %s\n"), normPoint(p));
    Indent(2);
    fprintf(mStream, _T("cutOffAngle %s\n"),
            floatVal( DegToRad(ls.fallsize)));
    Indent(2);
    fprintf(mStream, _T("dropOffRate %s\n"),
            floatVal(1.0f - ls.hotsize/ls.fallsize));
    Indent(2);
    fprintf(mStream, _T("on %s\n"), ls.on ? _T("TRUE") : _T("FALSE"));
    Indent(1);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Create a light at the top-level of the file
void
VRBLExport::OutputTopLevelLight(INode* node, LightObject *light)
{
    Class_ID id = light->ClassID();
    if (id == Class_ID(OMNI_LIGHT_CLASS_ID, 0))
        VrblOutTopPointLight(node, light);
    else if (id == Class_ID(DIR_LIGHT_CLASS_ID, 0))
        VrblOutTopDirectLight(node, light);
    else if (id == Class_ID(SPOT_LIGHT_CLASS_ID, 0) ||
             id == Class_ID(FSPOT_LIGHT_CLASS_ID, 0))
        VrblOutTopSpotLight(node, light);
    
}

// Output a VRML Inline node.
BOOL
VRBLExport::VrblOutInline(VRMLInsObject* obj, int level)
{
    Indent(level);
    fprintf(mStream, _T("WWWInline {\n"));
    Indent(level+1);
    fprintf(mStream, _T("name %s\n"), obj->GetUrl().data());
    float size = obj->GetSize() * 2.0f;
    Indent(level+1);
    Point3 p = Point3(size, size, size);
    fprintf(mStream, _T("bboxSize %s\n"), scalePoint(p));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Distance comparison function for sorting LOD lists.
static int
DistComp(LODObj** obj1, LODObj** obj2)
{
    float diff = (*obj1)->dist - (*obj2)->dist;
    if (diff < 0.0f) return -1;
    if (diff > 0.0f) return 1;
    return 0;
}

// Create a level-of-detail object.
BOOL
VRBLExport::VrblOutLOD(INode *node, LODObject* obj, int level)
{
    int numLod = obj->NumRefs();
    Tab<LODObj*> lodObjects = obj->GetLODObjects();
    int i;

    if (numLod == 0)
        return TRUE;

    lodObjects.Sort((CompareFnc) DistComp);

    if (numLod > 1) {
        Indent(level);
        fprintf(mStream, _T("LOD {\n"));
        Indent(level+1);
        Point3 p = node->GetObjTMAfterWSM(mStart).GetTrans();
        fprintf(mStream, _T("center %s\n"), point(p));
        Indent(level+1);
        fprintf(mStream, _T("range [ "));
        for(i = 0; i < numLod-1; i++) {
            if (i < numLod-2)
                fprintf(mStream, _T("%s, "), floatVal(lodObjects[i]->dist));
            else
                fprintf(mStream, _T("%s ]\n"), floatVal(lodObjects[i]->dist));
        }
    }

    for(i = 0; i < numLod; i++) {
        INode *node = lodObjects[i]->node;
        INode *parent = node->GetParentNode();
        VrblOutNode(node, parent, level+1, TRUE, FALSE);
    }

    if (numLod > 1) {
        Indent(level);
        fprintf(mStream, _T("}\n"));
    }

    return TRUE;
}

// Output an AimTarget.
BOOL
VRBLExport::VrblOutTarget(INode* node, int level)
{
    INode* lookAt = node->GetLookatNode();
    if (!lookAt)
        return TRUE;
    Object* lookAtObj = lookAt->EvalWorldState(mStart).obj;
    Class_ID id = lookAtObj->ClassID();
    // Only generate aim targets for targetted spot lights and cameras
    if (id != Class_ID(SPOT_LIGHT_CLASS_ID, 0) &&
        id != Class_ID(LOOKAT_CAM_CLASS_ID, 0))
        return TRUE;
    Indent(level);
    fprintf(mStream, _T("AimTarget_ktx_com {\n"));
    if (mGenFields) {
        Indent(level+1);
        fprintf(mStream, _T("fields [ SFString aimer ]\n"));
    }
    Indent(level+1);
	if ( (id == Class_ID(LOOKAT_CAM_CLASS_ID, 0)) && IsEverAnimated(lookAt))
		fprintf(mStream, _T("aimer \"%s_Animated\"\n"), mNodes.GetNodeName(lookAt));
	else
		fprintf(mStream, _T("aimer \"%s\"\n"), mNodes.GetNodeName(lookAt));
    Indent(level);
    fprintf(mStream, _T("}\n"));
    return TRUE;
}

// Write out the VRML for nodes we know about, including VRML helper nodes, 
// lights, cameras and VRML primitives
BOOL
VRBLExport::VrblOutSpecial(INode* node, INode* parent,
                             Object* obj, int level)
{
    Class_ID id = obj->ClassID();

    /*
    if (id == Class_ID(MR_BLUE_CLASS_ID1, MR_BLUE_CLASS_ID2)) {
        level++;
        VrblOutMrBlue(node, parent, (MrBlueObject*) obj,
                      &level, FALSE);
    }
    */

    if (id == Class_ID(OMNI_LIGHT_CLASS_ID, 0))
        return VrblOutPointLight(node, (LightObject*) obj, level+1);

    if (id == Class_ID(DIR_LIGHT_CLASS_ID, 0))
        return VrblOutDirectLight(node, (LightObject*) obj, level+1);

    if (id == Class_ID(SPOT_LIGHT_CLASS_ID, 0) ||
        id == Class_ID(FSPOT_LIGHT_CLASS_ID, 0))
        return VrblOutSpotLight(node, (LightObject*) obj, level+1);

    if (id == Class_ID(VRML_INS_CLASS_ID1, VRML_INS_CLASS_ID2))
        return VrblOutInline((VRMLInsObject*) obj, level+1);

    if (id == Class_ID(LOD_CLASS_ID1, LOD_CLASS_ID2))
        return VrblOutLOD(node, (LODObject*) obj, level+1);

    if (id == Class_ID(SIMPLE_CAM_CLASS_ID, 0) ||
        id == Class_ID(LOOKAT_CAM_CLASS_ID, 0))
        return VrblOutCamera(node, obj, level+1);

    if (id == Class_ID(TARGET_CLASS_ID, 0))
        return VrblOutTarget(node, level+1);

    // If object has modifiers or WSMs attached, do not output as
    // a primitive
    SClass_ID sid = node->GetObjectRef()->SuperClassID();
    if (sid == WSM_DERIVOB_CLASS_ID ||
        sid == DERIVOB_CLASS_ID)
        return FALSE;

    if (!mPrimitives)
        return FALSE;

    // Otherwise look for the primitives we know about
    if (id == Class_ID(SPHERE_CLASS_ID, 0))
        return VrblOutSphere(node, obj, level+1);

    if (id == Class_ID(CYLINDER_CLASS_ID, 0))
        return VrblOutCylinder(node, obj, level+1);

    if (id == Class_ID(CONE_CLASS_ID, 0))
        return VrblOutCone(node, obj, level+1);

    if (id == Class_ID(BOXOBJ_CLASS_ID, 0))
        return VrblOutCube(node, obj, level+1);

    return FALSE;
        
}

static BOOL
IsLODObject(Object* obj)
{
    return obj->ClassID() == Class_ID(LOD_CLASS_ID1, LOD_CLASS_ID2);
}

// Returns TRUE iff an object or one of its ancestors in animated
static BOOL
IsEverAnimated(INode* node)
{
 // need to sample transform
    Class_ID id = node->EvalWorldState(0).obj->ClassID();
    if (id == Class_ID(SIMPLE_CAM_CLASS_ID, 0) ||
        id == Class_ID(LOOKAT_CAM_CLASS_ID, 0)) return TRUE;

    for (; !node->IsRootNode(); node = node->GetParentNode())
        if (node->IsAnimated())
            return TRUE;
    return FALSE;
}

// Returns TRUE for object that we want a VRML node to occur
// in the file.  
BOOL
VRBLExport::isVrblObject(INode * node, Object *obj, INode* parent)
{
    if (!obj)
        return FALSE;

	if(exportSelected && node->Selected() == FALSE)
		return FALSE;

    Class_ID id = obj->ClassID();
    // Mr Blue nodes only 1st class if stand-alone

    // only animated light come out in scene graph
    if (IsLight(node) ||
        (id == Class_ID(SIMPLE_CAM_CLASS_ID, 0) ||
         id == Class_ID(LOOKAT_CAM_CLASS_ID, 0)))
        return IsEverAnimated(node);

    return (obj->IsRenderable() ||
            id == Class_ID(LOD_CLASS_ID1, LOD_CLASS_ID2) ||
            node->NumberOfChildren() > 0 //||
            ) &&
            (mExportHidden || !node->IsHidden());        
}

// Write the VRML for a single object.
void
VRBLExport::VrblOutObject(INode* node, INode* parent, Object* obj, int level)
{
    BOOL isTriMesh = obj->CanConvertToType(triObjectClassID);
        
    BOOL multiMat = FALSE, twoSided = FALSE;
    // Output the material
    if (obj->IsRenderable())
        multiMat = OutputMaterial(node, twoSided, level+1);

    // First check for VRML primitives and other special objects
    if (VrblOutSpecial(node, parent, obj, level)) {
        return;
    }

    // Otherwise output as a triangle mesh
    if (isTriMesh) {
        TriObject *tri = (TriObject *)obj->ConvertToType(0, triObjectClassID);
        OutputTriObject(node, tri, multiMat, twoSided, level+1);
        if(obj != (Object *)tri)
            tri->DeleteThis();
    }
}

// Get the distance to the line of sight target
float 
GetLosProxDist(INode* node, TimeValue t)
{
    Point3 p0 = node->GetObjTMAfterWSM(t).GetTrans();
    Matrix3 tmat;
    node->GetTargetTM(t,tmat);
    Point3 p1 = tmat.GetTrans();
    return Length(p1-p0);
}

// Get the vector to the line of sight target
Point3
GetLosVector(INode* node, TimeValue t)
{
    Point3 p0 = node->GetObjTMAfterWSM(t).GetTrans();
    Matrix3 tmat;
    node->GetTargetTM(t,tmat);
    Point3 p1 = tmat.GetTrans();
    return p1-p0;
}

// Return TRUE iff the controller is a TCB controller
static BOOL 
IsTCBControl(Control *cont)
{
    return ( cont && (
        cont->ClassID()==Class_ID(TCBINTERP_FLOAT_CLASS_ID,0)    ||
        cont->ClassID()==Class_ID(TCBINTERP_POSITION_CLASS_ID,0) ||
        cont->ClassID()==Class_ID(TCBINTERP_ROTATION_CLASS_ID,0) ||
        cont->ClassID()==Class_ID(TCBINTERP_POINT3_CLASS_ID,0)   ||
        cont->ClassID()==Class_ID(TCBINTERP_SCALE_CLASS_ID,0)));
}

// Return TRUE iff the keys are different in any way.
static BOOL
TCBIsDifferent(ITCBKey *k, ITCBKey* oldK)
{
    return k->tens    != oldK->tens   ||
           k->cont    != oldK->cont   ||
           k->bias    != oldK->bias   ||
           k->easeIn  != oldK->easeIn ||
           k->easeOut != oldK->easeOut;
}

// returns TRUE iff the position keys are exactly the same
static BOOL
PosKeysSame(ITCBPoint3Key& k1, ITCBPoint3Key& k2)
{
    if (TCBIsDifferent(&k1, &k2))
        return FALSE;
    return k1.val == k2.val;
}

// returns TRUE iff the rotation keys are exactly the same
static BOOL
RotKeysSame(ITCBRotKey& k1, ITCBRotKey& k2)
{
    if (TCBIsDifferent(&k1, &k2))
        return FALSE;
    return k1.val.axis == k2.val.axis && k1.val.angle == k2.val.angle;
}

// returns TRUE iff the scale keys are exactly the same
static BOOL
ScaleKeysSame(ITCBScaleKey& k1, ITCBScaleKey& k2)
{
    if (TCBIsDifferent(&k1, &k2))
        return FALSE;
    return k1.val.s == k2.val.s;
}

// Write out all the keyframe data for the TCB given controller
BOOL
VRBLExport::WriteTCBKeys(INode* node, Control *cont,
                         int type, int level)
{
    ITCBFloatKey fkey, ofkey;
    ITCBPoint3Key pkey, opkey;
    ITCBRotKey rkey, orkey;
    ITCBScaleKey skey, oskey;
    ITCBKey *k, *oldK;	
    int num = cont->NumKeys();
    Point3 pval;
    Quat q, qLast = IdentQuat();
    AngAxis rval;
    ScaleValue sval;
    Interval valid;
    Point3 p, po;

    // Get the keyframe interface
    IKeyControl *ikeys = GetKeyControlInterface(cont);
    
    // Gotta have some keys
    if (num == NOT_KEYFRAMEABLE || num == 0 || !ikeys) {
        return FALSE;
    }
    
    // Set up 'k' to point at the right derived class
    switch (type) {
    case KEY_FLOAT: k = &fkey; oldK = &ofkey; break;
    case KEY_POS:   k = &pkey; oldK = &opkey; break;
    case KEY_ROT:   k = &rkey; oldK = &orkey; break;
    case KEY_SCL:   k = &skey; oldK = &oskey; break;
    case KEY_COLOR: k = &pkey; oldK = &opkey; break;
    default: return FALSE;
    }
    
    for (int i=0; i<ikeys->GetNumKeys(); i++) {
        ikeys->GetKey(i,k);
        if (k->time < mStart)
            continue;

        if (i == 0 || TCBIsDifferent(k, oldK)) {
            Indent(level);
            fprintf(mStream, _T("AnimationStyle_ktx_com {\n"));
            Indent(level+1);
            if (mGenFields)
                fprintf(mStream, _T("fields [ SFBool loop, SFBitMask splineUse, SFFloat tension, SFFloat continuity, SFFloat bias, SFFloat easeTo, SFFloat easeFrom, SFVec3f pivotOffset ]\n"));
            Indent(level+1);
            fprintf(mStream, _T("splineUse ("));
            
            // Write flags
            BOOL hadOne = FALSE;
            if (k->tens   != 0.0f) {
                fprintf(mStream, _T("TENSION"));
                hadOne = TRUE;
            }
            if (k->cont   != 0.0f) {
                if (hadOne)
                    fprintf(mStream, _T(" | "));
                fprintf(mStream, _T("CONTINUITY"));
                hadOne = TRUE;
            }
            if (k->bias   != 0.0f) {
                if (hadOne)
                    fprintf(mStream, _T(" | "));
                fprintf(mStream, _T("BIAS"));
                hadOne = TRUE;
            }
            if (k->easeIn != 0.0f) {
                if (hadOne)
                    fprintf(mStream, _T(" | "));
                fprintf(mStream, _T("EASE_TO"));
                hadOne = TRUE;
            }
            if (k->easeOut!= 0.0f) {
                if (hadOne)
                    fprintf(mStream, _T(" | "));
                fprintf(mStream, _T("EASE_FROM"));
                hadOne = TRUE;
            }
            fprintf(mStream, _T(")\n"));
            
            // Write TCB and ease
            if (k->tens   != 0.0f) {
                Indent(level+1);
                fprintf(mStream, _T("tension %s\n"), floatVal(k->tens));
            }
            if (k->cont   != 0.0f) {
                Indent(level+1);
                fprintf(mStream, _T("continuity %s\n"), floatVal(k->cont));
            }
            if (k->bias   != 0.0f) {
                Indent(level+1);
                fprintf(mStream, _T("bias %s\n"), floatVal(k->bias));
            }
            if (k->easeIn != 0.0f) {
                Indent(level+1);
                fprintf(mStream, _T("easeTo %s\n"), floatVal(k->easeIn));
            }
            if (k->easeOut!= 0.0f) {
                Indent(level+1);
                fprintf(mStream, _T("easeFrom %s\n"), floatVal(k->easeOut));
            }

	        // get the pivot offset and remove the rotational component
	        Matrix3 m = Matrix3(TRUE);
	        Quat q = node->GetObjOffsetRot();
	        q.MakeMatrix(m);
            p = -node->GetObjOffsetPos();
	        m = Inverse(m);
	        po = VectorTransform(m, p);
            
            Indent(level+1);
            if (type != KEY_POS) fprintf(mStream, _T("pivotOffset %s\n"), point(po));
            Indent(level);
            fprintf(mStream, _T("}\n"));
            
        }
        // Write values
        switch (type) {
        case KEY_FLOAT: 
            assert(FALSE);
            break;
            
        case KEY_SCL: {
            if (i == 0 && (k->time - mStart) != 0) {
                WriteScaleKey0(node, mStart, level, TRUE);
                WriteScaleKey0(node,
                               k->time-GetTicksPerFrame(), level, TRUE);
            }
            Matrix3 tm = GetLocalTM(node, mStart);
            AffineParts parts;
            decomp_affine(tm, &parts);
            ScaleValue sv(parts.k, parts.u);
            Point3 s = sv.s;
            if (parts.f < 0.0f) s = - s;
            else s = skey.val.s;
            if (i != 0 && ScaleKeysSame(skey, oskey))
                continue;
            mHadAnim = TRUE;
            Indent(level);
            fprintf(mStream, _T("ScaleKey_ktx_com {\n"));
            Indent(level+1);
            if (mGenFields)
                fprintf(mStream,
                        _T("fields [ SFLong frame, SFVec3f scale ]\n"));
            Indent(level+1);
            fprintf(mStream, _T("frame %d\n"), (k->time - mStart)/GetTicksPerFrame());
            Indent(level+1);
            fprintf(mStream, _T("sc        #     ?                                                                                                                              0õÿ              õÿ     
         X              X              ª
              ª
              oø             ¸ @           r @           õ             #                          V             Æ @           € @           Ò @           Œ @           ü @           Þ @            @           í @            ˜ @  !         ¨ @  "          @  #         :    #          ÿÿ' ÿÿ) ÿÿ( ÿÿ. ÿÿ, ÿÿ> ÿÿ? ÿÿ@ ÿÿA ÿÿB ÿÿ> ? ? ? @ ? A ? B ? H ? I ? J ? K ? L ? H ÿÿI ÿÿJ ÿÿK ÿÿL ÿÿQ ? Q ÿÿH ÿÿ> ÿÿI ÿÿ? ÿÿJ ÿÿ@ ÿÿQ ÿÿK ÿÿY ÿÿL ÿÿA ÿÿB ÿÿ^ ÿÿ                                                                                                                                                                                                                                              %  `     ? ï                                                                                                                             ( àª
              ª
               Žù     0         Žù     @         ø    @         º @  A         È @  B         ­ @  C         â @  D         ï @  E         Ö @  F         Ž @  G         ž @  H         " @  I         1 @  J         P @  K         ^ @  L         @ @  M         l @  N         { @  O         Š @  P         © @  Q         · @  R         ™ @  S         Å @  T         Ô @  U         æ @  V         ÷ @  W          @  X          @  Y         ) @  Z         G @  [         V @  \         9 @  ]         r @  ^         € @  _         e @  `          ÿÿ# ÿÿ$ ÿÿ% ÿÿ. ÿÿ/ ÿÿ0 ÿÿ9 ÿÿ@ ÿÿG ÿÿN ÿÿO ÿÿP ÿÿY ÿÿ` ÿÿa ÿÿb ÿÿ# ? $ ? % ? . ? / ? 0 ? 9 ? @ ? G ? N ? O ? P ? Y ? ` ? a ? b ? ' ? ( ? ) ? 2 ? 3 ? 4 ? ; ? B ? I ? R ? S ? T ? [ ? d ? e ? f ? ' ÿÿ( ÿÿ) ÿÿ2 ÿÿ3 ÿÿ4 ÿÿ; ÿÿB ÿÿI ÿÿR ÿÿS ÿÿT ÿÿ[ ÿÿd ÿÿe ÿÿf ÿÿa ÿÿb ÿÿ` ÿÿe ÿÿf ÿÿd ÿÿY ÿÿ[ ÿÿ# ÿÿ$ ÿÿ' ÿÿ( ÿÿ% ÿÿ) ÿÿ. ÿÿ/ ÿÿ2 ÿÿ3 ÿÿ0 ÿÿ4 ÿÿ9 ÿÿ; ÿÿ@ ÿÿB ÿÿG ÿÿI ÿÿO ÿÿP ÿÿN ÿÿS ÿÿT ÿÿR ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          T  –     ? Ô                                                                                                                             A€ ðõÿ              õÿ              X              X              ª
     >         ª
     d         ½÷    d          @  e         Ø @  f         ½ @  g         ­ @  h         å @  i         Ê @  j         ü    j         .    j             j         :    j          @  k         æ @  l          @  m          @  n         1 @  o         Ô @  p         ù @  q         U @  r          @  s         z @  t           @  u         C @  v         h @  w         ý @  x         ó @  y         k @  z         } @  {         Z @  |             |         ÷ @  }             }         F    }         !   	 }         S   
 }         ¶    }         U    }         ¥    }         ç    }         •    }         ƒ    }         n    }         ×    }         ü    }             }         ,    }         B    }         Æ    }         8 @  ~         I @           ( @  €         Õ @           [ @  ‚         $ @  ƒ         ? @  „         w @  …         M @  †          @  ‡         1 @  ˆ         i @  ‰          @  Š         ã    Š         Ã @  ‹         • @  Œ         … @           ² @  Ž             Ž           @           º @           ­ @  ‘         Ç @  ’         Ô @  “         s    “         `    “         …    “         ³ @  ”         Ã @  •         £ @  –          ÿÿ5 ÿÿ; ÿÿ6 ÿÿ< ÿÿ« ÿÿ8 ÿÿ> ÿÿ9 ÿÿ? ÿÿ, ÿÿ% ÿÿ+ ÿÿ/ ÿÿ* ÿÿ( ÿÿ' ÿÿ. ÿÿ! ÿÿ" ÿÿ# ÿÿ$ ÿÿ- ÿÿ  ÿÿ² ÿÿH ÿÿA ÿÿJ ÿÿZ ÿÿ[ ÿÿf ÿÿ› ÿÿ¥ ÿÿÓ ÿÿÔ ÿÿZ ? [ ? f ? › ? ¥ ? Ó ? Ô ? ` ? a ? g ? Î ? Ï ? ` ÿÿa ÿÿg ÿÿÎ ÿÿÏ ÿÿS ? U ? l ? p ? q ? s ? t ? v ? w ? x ? y ? ~ ? ƒ ? ˆ ?  ? “ ? · ? ¸ ? » ? À ? Á ? Ä ? Ê ? Ø ? Ù ? Ú ? Û ? Ü ? Ý ? á ? â ? ã ? ä ? å ? æ ? ì ? ò ? ø ? S ÿÿU ÿÿl ÿÿp ÿÿq ÿÿs ÿÿt ÿÿv ÿÿw ÿÿx ÿÿy ÿÿ~ ÿÿƒ ÿÿˆ ÿÿ ÿÿ“ ÿÿ· ÿÿ¸ ÿÿ» ÿÿÀ ÿÿÁ ÿÿÄ ÿÿÊ ÿÿØ ÿÿÙ ÿÿÚ ÿÿÛ ÿÿÜ ÿÿÝ ÿÿá ÿÿâ ÿÿã ÿÿä ÿÿå ÿÿæ ÿÿì ÿÿò ÿÿø ÿÿT ÿÿ` ÿÿZ ÿÿU ÿÿa ÿÿ[ ÿÿl ÿÿÙ ÿÿÜ ÿÿÛ ÿÿÝ ÿÿØ ÿÿÚ ÿÿâ ÿÿå ÿÿä ÿÿæ ÿÿá ÿÿã ÿÿg ÿÿf ÿÿÁ ÿÿÄ ÿÿÀ ÿÿ¥ ÿÿ¸ ÿÿ» ÿÿ· ÿÿ› ÿÿw ÿÿq ÿÿt ÿÿy ÿÿv ÿÿp ÿÿs ÿÿx ÿÿÊ ÿÿò ÿÿƒ ÿÿ~ ÿÿì ÿÿÎ ÿÿÓ ÿÿÏ ÿÿÔ ÿÿø ÿÿ ÿÿ“ ÿÿˆ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         (  3     ? E"                                                                                                                               pª
              ª
              d÷             á! @           º! @            _! @  !         R! @  "         Ô! @  #         Ç! @  $         ­! @  %         î! @  &          ! @  '         y! @  (         E! @  )         8! @  *         “! @  +         †! @  ,         l! @  -         )" @  .         7" @  /         E" @  0         " @  1         þ! @  2         " @  3         !    3         *     3         O     3         d     3         >     3         ê     3         Ë     3         ¬    	 3         !   
 3         ”     3         |     3         
     3              3         ê    3         (!    3         ú    3          ÿÿE ÿÿ- ÿÿ/ ÿÿ0 ÿÿ. ÿÿ? ÿÿ9 ÿÿ8 ÿÿ@ ÿÿ7 ÿÿ6 ÿÿ& ÿÿ' ÿÿ$ ÿÿF ÿÿ% ÿÿM ÿÿN ÿÿP ÿÿQ ÿÿW ÿÿX ÿÿY ÿÿZ ÿÿ[ ÿÿ] ÿÿ^ ÿÿ_ ÿÿ` ÿÿa ÿÿh ÿÿM ? N ? P ? Q ? W ? X ? Y ? Z ? [ ? ] ? ^ ? _ ? ` ? a ? h ? a ÿÿ^ ÿÿQ ÿÿP ÿÿ` ÿÿ_ ÿÿ] ÿÿh ÿÿ[ ÿÿX ÿÿN ÿÿM ÿÿZ ÿÿY ÿÿW ÿÿy ÿÿz ÿÿ} ÿÿx ÿÿn ÿÿq ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     
       ? Ê"                                                                                                                               Àª
              ª
              ÷             S" @           d" @           y" @           Š" @           Ÿ" @           °" @           Ê" @            ÿÿ$ ÿÿ* ÿÿ0 ÿÿ6 ÿÿ< ÿÿC ÿÿK ÿÿ$ ? * ? 0 ? 6 ? < ? C ? K ? $ ÿÿ* ÿÿ0 ÿÿ6 ÿÿ< ÿÿC ÿÿL ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       ç 6 þ
   ? ¦0                                                                                                                            ü?5             õÿ            õÿ    "        X    0        X    >        ª
    K        ª
    X        ß"    ‘        ß"    Ê        #           #    )
       #    Q
       Ê#    Q
       ‡$    Q
       '$   	 Q
       ä$   
 Q
       J$    Q
       %    Q
       !%    Q
       d$    Q
       ,%    Q
       o$    Q
       $    Q
       à#    Q
       ’$    Q
       Õ#    Q
       X$    Q
       %    Q
       ø#    Q
       µ$    Q
       ï$    Q
       2$    Q
       û$    Q
       >$    Q
       Í$    Q
       $    Q
       Á$    Q
       $     Q
       $   ! Q
       Ù$   " Q
       z$   # Q
       7%   $ Q
       ë#   % Q
       ¨$   & Q
       ñ>    & T
       0 @ & U
       ¦0 @ & V
       ‹0 @ & W
       “0 @ & X
       ²ö   ' X
       D% @ ' Y
       R% @ ' Z
       `% @ ' [
       q% @ ' \
       ‚% @ ' ]
       % @ ' ^
       ž% @ ' _
       ¯% @ ' `
       À% @ ' a
       Ð% @ ' b
       à% @ ' c
       ñ% @ ' d
       —) @ ' e
       ƒ) @ ' f
       o) @ ' g
       J) @ ' h
       ú/ @ ' i
       µ/ @ ' j
       ,/ @ ' k
       b/ @ ' l
       ã/ @ ' m
       ž/ @ ' n
       =/ @ ' o
       N/ @ ' p
       0 @ ' q
       Š/ @ ' r
       v/ @ ' s
       Ì/ @ ' t
       ,   ( t
       E,   ) t
       <' @ ) u
       U' @ ) v
       y' @ ) w
       Œ' @ ) x
       æ' @ ) y
       Ÿ' @ ) z
       ¯' @ ) {
       Â' @ ) |
       Ò' @ ) }
       ù' @ ) ~
       & @ ) 
       & @ ) €
       & @ ) 
       #( @ ) ‚
       ( @ ) ƒ
       9( @ ) „
       %,   * „
       T,   + „
       +& @ + …
       :& @ + †
       I& @ + ‡
       X& @ + ˆ
       À) @ + ‰
       «) @ + Š
       Õ) @ + ‹
       + @ + Œ
       í) @ + 
       * @ + Ž
       m* @ + 
       +* @ + 
       L* @ + ‘
       ’* @ + ’
       ¹* @ + “
       + @ + ”
       Ì* @ + •
       + @ + –
       g& @ + —
       u& @ + ˜
       ƒ& @ + ™
       ‘& @ + š
       o+ @ + ›
       I+ @ + œ
       \+ @ + 
       ¡- @ + ž
       ´- @ + Ÿ
       Ÿ& @ +  
       ­& @ + ¡
       »& @ + ¢
       È& @ + £
       ) @ + ¤
       ) @ + ¥
       0) @ + ¦
       á( @ + §
       ™( @ + ¨
       º( @ + ©
       r( @ + ª
       Ê+ @ + «
       , @ + ¬
       5, @ + ­
       Ø+ @ + ®
       õ. @ + ¯
       / @ + °
       / @ + ±
       ä. @ + ²
       Æ. @ + ³
       Õ. @ + ´
       ,. @ + µ
       =. @ + ¶
       N. @ + ·
       . @ + ¸
        . @ + ¹
       . @ + º
       . @ + »
       ¡. @ + ¼
       ³. @ + ½
       ~. @ + ¾
       `. @ + ¿
       o. @ + À
       Ç- @ + Á
       Ù- @ + Â
       ë- @ + Ã
       Õ& @ + Ä
       ç& @ + Å
       è+ @ + Æ
       ÷+ @ + Ç
       ü) @ + È
       * @ + É
       * @ + Ê
       ;* @ + Ë
       \* @ + Ì
       ¥* @ + Í
       ¨+ @ + Î
       ¹+ @ + Ï
       ”+ @ + Ð
       ù& @ + Ñ
       ' @ + Ò
       û, @ + Ó
       V- @ + Ô
       Ã, @ + Õ
       - @ + Ö
       f- @ + ×
       Ö, @ + Ø
       ë, @ + Ù
       F- @ + Ú
       °, @ + Û
       d, @ + Ü
       w, @ + Ý
       Š, @ + Þ
       , @ + ß
       ' @ + à
       #' @ + á
       R( @ + â
       a( @ + ã
       ^) @ + ä
       1' @ + å
       J' @ + æ
       c' @ + ç
       n' @ + è
       à* @ + é
       (+ @ + ê
       ð* @ + ë
       8+ @ + ì
       ó( @ + í
       «( @ + î
       Ï( @ + ï
       ‡( @ + ð
       *- @ + ñ
       …- @ + ò
       7- @ + ó
       ’- @ + ô
       - @ + õ
       x- @ + ö
       (0   , ö
       C0 @ , ÷
       y0 @ , ø
       0 @ , ù
       S0 @ , ú
       Z0 @ , û
       c0 @ , ü
       m0 @ , ý
       K0 @ , þ
       æ" @ - þ
      ù" @ . þ
      8# @ / þ
      M# @ 0 þ
      b# @ 1 þ
      $# @ 2 þ
      ‹# @ 3 þ
       # @ 4 þ
      µ# @ 5 þ
 	     w# @ 6 þ
 
     ! ÿÿ ÿÿ ? # ÿÿ% ÿÿ. ? 3 ÿÿC ÿÿ; ÿÿK ÿÿ> ÿÿN ÿÿP ÿÿ@ ÿÿQ ÿÿA ÿÿE ÿÿ5 ÿÿD ÿÿ4 ÿÿ? ÿÿO ÿÿ7 ÿÿG ÿÿL ÿÿ< ÿÿM ÿÿ= ÿÿI ÿÿ9 ÿÿH ÿÿ8 ÿÿ: ÿÿJ ÿÿB ÿÿR ÿÿ6 ÿÿF ÿÿ ÿÿ~ÿÿÿÿÿÿŽÿÿ4ÿÿ ÿÿ" ÿÿ' ÿÿ( ÿÿ) ÿÿ& ÿÿ+ ÿÿ, ÿÿ- ÿÿ* ÿÿÀÿÿÛÿÿ÷ÿÿ
ÿÿ5ÿÿQÿÿ\ÿÿ‡ÿÿ—ÿÿ¢ÿÿÿÿÿÿÜÿÿÝÿÿÞÿÿ(ÿÿ+ÿÿÀ? Û? ÷? 
? 5? Q? \? ‡? —? ¢? ? ? Ü? Ý? Þ? (? +? °ÿÿÎÿÿ6ÿÿgÿÿrÿÿ˜ÿÿ£ÿÿöÿÿ÷ÿÿÊÿÿËÿÿÌÿÿ)ÿÿ,ÿÿ°? Î? 6? g? r? ˜? £? ö? ÷? Ê? Ë? Ì? )? ,? äÿÿÿÿ ÿÿ7ÿÿQÿÿgÿÿ¤ÿÿöÿÿ÷ÿÿÿÿÿÿ*ÿÿ-ÿÿä? ?  ? 7? Q? g? ¤? ö? ÷? ? ? *? -? f ÿÿ€ ÿÿ’ ÿÿ¤ ÿÿÀ ÿÿÔ ÿÿæ ÿÿõ ÿÿÿÿÿÿ+ÿÿ=ÿÿQÿÿcÿÿ|ÿÿÿÿ¡ÿÿÁÿÿíÿÿ ÿÿ-ÿÿ5ÿÿPÿÿ[ÿÿ{ÿÿ†ÿÿ—ÿÿ¢ÿÿ·ÿÿ¸ÿÿËÿÿÌÿÿÿÿÿÿÿÿÿÿJÿÿSÿÿ^ÿÿpÿÿŒÿÿžÿÿ®ÿÿÙÿÿÚÿÿÛÿÿðÿÿöÿÿýÿÿÿÿÿÿ"ÿÿ#ÿÿ$ÿÿ&ÿÿ(ÿÿ+ÿÿf ? € ? ’ ? ¤ ? À ? Ô ? æ ? õ ? ? ? +? =? Q? c? |? ? ¡? Á? í?  ? -? 5? P? [? {? †? —? ¢? ·? ¸? Ë? Ì? ? ? ? ? J? S? ^? p? Œ? ž? ®? Ù? Ú? Û? ð? ö? ý? ? ? "? #? $? &? (? +? \ ÿÿs ÿÿ‰ ÿÿ› ÿÿ² ÿÿÊ ÿÿÝ ÿÿþ ÿÿÿÿ"ÿÿ4ÿÿGÿÿZÿÿoÿÿ…ÿÿ˜ÿÿ±ÿÿäÿÿ÷ÿÿ
ÿÿÿÿ ÿÿ6ÿÿfÿÿqÿÿ|ÿÿÿÿ˜ÿÿ£ÿÿ­ÿÿ®ÿÿÁÿÿÂÿÿôÿÿõÿÿ&ÿÿAÿÿgÿÿ}ÿÿ–ÿÿ¦ÿÿÇÿÿÈÿÿÉÿÿçÿÿõÿÿüÿÿÿÿÿÿ"ÿÿ#ÿÿ%ÿÿ'ÿÿ)ÿÿ,ÿÿíÿÿ ÿÿ-ÿÿ7ÿÿPÿÿfÿÿ}ÿÿ¤ÿÿÕÿÿÖÿÿßÿÿàÿÿôÿÿõÿÿÿÿÿÿ0ÿÿ8ÿÿ¸ÿÿ¹ÿÿºÿÿ÷ÿÿþÿÿÿÿÿÿÿÿ	ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ*ÿÿ-ÿÿí?  ? -? 7? P? f? }? ¤? Õ? Ö? ß? à? ô? õ? ? ? 0? 8? ¸? ¹? º? ÷? þ? ? ? ? 	? ? ? ? ? ? ? ? $? %? &? '? *? -?  ÿÿ! ÿÿ% ÿÿgÿÿiÿÿ^ÿÿ`ÿÿ\ ÿÿf ÿÿs ÿÿ€ ÿÿ‰ ÿÿ’ ÿÿ› ÿÿ¤ ÿÿ² ÿÿÀ ÿÿÊ ÿÿÔ ÿÿ˜ÿÿ—ÿÿÿÿ†ÿÿ,ÿÿ)ÿÿ"ÿÿ%ÿÿ+ÿÿ(ÿÿ#ÿÿ$ÿÿ-ÿÿ'ÿÿ&ÿÿ*ÿÿ±ÿÿÁÿÿäÿÿíÿÿ ÿÿ÷ÿÿ ÿÿ
ÿÿÿÿ-ÿÿÝ ÿÿæ ÿÿõ ÿÿ6ÿÿ5ÿÿ7ÿÿþ ÿÿÿÿÿÿÿÿ£ÿÿ¢ÿÿ¤ÿÿ0ÿÿ­ÿÿ·ÿÿÕÿÿÁÿÿËÿÿßÿÿôÿÿÿÿõÿÿÿÿ"ÿÿ+ÿÿ4ÿÿ=ÿÿ&ÿÿÿÿÿÿçÿÿðÿÿGÿÿQÿÿZÿÿcÿÿ|ÿÿ{ÿÿ}ÿÿqÿÿ[ÿÿfÿÿPÿÿSÿÿ}ÿÿŒÿÿ^ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿüÿÿýÿÿÿÿÿÿÿÿÿÿÿÿÿÿõÿÿöÿÿ÷ÿÿoÿÿ|ÿÿgÿÿpÿÿ®ÿÿ¸ÿÿÖÿÿÂÿÿÌÿÿàÿÿAÿÿJÿÿ8ÿÿ…ÿÿÿÿÈÿÿÚÿÿ¹ÿÿÉÿÿÛÿÿºÿÿÇÿÿÙÿÿ¸ÿÿ–ÿÿžÿÿ¦ÿÿ®ÿÿ˜ÿÿ¡ÿÿ>ÿÿFÿÿ‡ÿÿ°ÿÿÀÿÿÎÿÿÛÿÿöÿÿÿÿ÷ÿÿÿÿrÿÿ\ÿÿgÿÿQÿÿËÿÿÝÿÿÌÿÿÞÿÿÊÿÿÜÿÿ7ÿÿUÿÿWÿÿAÿÿCÿÿKÿÿMÿÿ:ÿÿÞ ÿÿÝ ÿÿâ ÿÿß ÿÿà ÿÿá ÿÿæ ÿÿã ÿÿä ÿÿå ÿÿ ÿÿ  ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ
 ÿÿ                                                                                                                                                                                                                                                                                                        ½ L &    ? §;                                                                                                                            €ÿt; @            œ; @            §; @            †; @            ‘; @            õÿ              õÿ              X     ,         X     9         ª
     p         ª
     §         ß"     ¯         ß"     ·         #     ¿         #     Â         #     Å         }; @   Æ         ü0    Æ         ; @  Ç         ?; @  È         0; @  É         i: @  Ê         Z: @  Ë         Š: @  Ì         x: @  Í         Ï: @  Î         À: @  Ï         ®: @  Ð         œ: @  Ñ         î: @  Ò         Þ: @  Ó         ; @  Ô         þ: @  Õ         a; @  Ö         N; @  ×         c8 @  Ø         ¤9 @  Ù         ²9 @  Ú         À9 @  Û         —9 @  Ü         m9 @  Ý         {9 @  Þ         ‰9 @  ß         `9 @  à         ^4    à         ä4    à         j5    à         Ü3    à         N2    à         Ô2    à         Z3    à         Ì1   	 à         4   
 à         5    à         ‹5    à         û3    à         o2    à         õ2    à         {3    à         ë1    à         N4    à         Ô4    à         Z5    à         Í3    à         >2    à         Ä2    à         J3    à         ½1    à         >4    à         Ä4    à         J5    à         ¾3    à         .2    à         ´2    à         :3     à         ®1   ! à         .4   " à         ´4   # à         :5   $ à         ¯3   % à         2   & à         ¤2   ' à         *3   ( à         Ÿ1   ) à         4   * à         ¤4   + à         *5   , à          3   - à         2   . à         ”2   / à         3   0 à         1   1 à         n4   2 à         ô4   3 à         z5   4 à         ë3   5 à         ^2   6 à         ä2   7 à         j3   8 à         Û1   9 à         ’4   : à         5   ; à         ž5   < à         4   = à         ‚2   > à         3   ? à         Ž3   @ à         ý1   A à         Ú9 @ A á         Î9 @ A â         ò9 @ A ã         æ9 @ A ä         §7 @ A å         ‰7 @ A æ         8 @ A ç         æ7 @ A è         ·7 @ A é         Ç7 @ A ê         ˜7 @ A ë         8 @ A ì         $8 @ A í         õ7 @ A î         z7 @ A ï         ×7 @ A ð         C8 @ A ñ         S8 @ A ò         48 @ A ó         Ä6 @ A ô         7 @ A õ         ¼5 @ A ö         °5 @ A ÷         Ô5 @ A ø         È5 @ A ù         ä6 @ A ú         H7 @ A û         %7 @ A ü         ´6 @ A ý         ô6 @ A þ         Ó6 @ A ÿ         67 @ A          7 @ A         j7 @ A         Y7 @ A         ï5 @ A         à5 @ A         (6 @ A         6 @ A         @6 @ A         46 @ A 	        6 @ A 
        þ5 @ A         Y6 @ A         L6 @ A         s6 @ A         f6 @ A         6 @ A         €6 @ A         §6 @ A         š6 @ A         J1   B         k1   C         :1   D         *1   E         1   F         
1   G         Z1   H         ~1   I         r8 @ I         þ9   J         J: @ J         :: @ J         %:   K         :   L         8 @ L         ›8 @ L         ©8 @ L         €8 @ L         Å8 @ L         Ô8 @ L         ã8 @ L         ·8 @ L         69 @ L         D9 @ L          R9 @ L !        )9 @ L "        ÿ8 @ L #        9 @ L $        9 @ L %        ò8 @ L &         ÿÿ[ ÿÿd ÿÿm ÿÿR ÿÿ7 ÿÿ@ ÿÿI ÿÿ. ÿÿ] ÿÿf ÿÿo ÿÿT ÿÿ9 ÿÿB ÿÿK ÿÿ0 ÿÿZ ÿÿc ÿÿl ÿÿQ ÿÿ6 ÿÿ? ÿÿH ÿÿ- ÿÿY ÿÿb ÿÿk ÿÿP ÿÿ5 ÿÿ> ÿÿG ÿÿ, ÿÿX ÿÿa ÿÿj ÿÿO ÿÿ4 ÿÿ= ÿÿF ÿÿ+ ÿÿW ÿÿ` ÿÿi ÿÿN ÿÿ3 ÿÿ< ÿÿE ÿÿ* ÿÿ\ ÿÿe ÿÿn ÿÿS ÿÿ8 ÿÿA ÿÿJ ÿÿ/ ÿÿ^ ÿÿg ÿÿp ÿÿU ÿÿ: ÿÿC ÿÿL ÿÿ1 ÿÿ! ÿÿ# ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ" ÿÿ$ ÿÿÒ ÿÿÕ ÿÿÔ ÿÿð ÿÿô ÿÿõ ÿÿò ÿÿó ÿÿu ? w ? y ? { ? } ?  ?  ? ƒ ? … ? ‡ ? Ì ? Î ? Ø ? u ÿÿw ÿÿy ÿÿ{ ÿÿ} ÿÿ ÿÿ ÿÿƒ ÿÿ… ÿÿ‡ ÿÿÌ ÿÿÎ ÿÿØ ÿÿv ÿÿx ÿÿz ÿÿ| ÿÿ~ ÿÿ€ ÿÿ‚ ÿÿ„ ÿÿ† ÿÿˆ ÿÿÍ ÿÿÏ ÿÿÙ ÿÿv ? x ? z ? | ? ~ ? € ? ‚ ? „ ? † ? ˆ ? Í ? Ï ? Ù ? ‹ ÿÿŒ ÿÿ ÿÿŽ ÿÿ ÿÿ ÿÿ‘ ÿÿ’ ÿÿ“ ÿÿ” ÿÿ• ÿÿ– ÿÿ™ ÿÿš ÿÿ› ÿÿœ ÿÿ ÿÿž ÿÿŸ ÿÿ  ÿÿ¡ ÿÿ¢ ÿÿ£ ÿÿ¤ ÿÿ¥ ÿÿ¦ ÿÿ§ ÿÿª ÿÿ« ÿÿ® ÿÿ¯ ÿÿ° ÿÿ± ÿÿ² ÿÿ³ ÿÿ´ ÿÿµ ÿÿ¶ ÿÿ· ÿÿ¸ ÿÿ¹ ÿÿº ÿÿ» ÿÿ¼ ÿÿ½ ÿÿÁ ÿÿÂ ÿÿÃ ÿÿÄ ÿÿÅ ÿÿÆ ÿÿÇ ÿÿÈ ÿÿØ ÿÿÙ ÿÿ‹ ? Œ ?  ? Ž ?  ?  ? ‘ ? ’ ? “ ? ” ? • ? – ? ™ ? š ? › ? œ ?  ? ž ? Ÿ ?   ? ¡ ? ¢ ? £ ? ¤ ? ¥ ? ¦ ? § ? ª ? « ? ® ? ¯ ? ° ? ± ? ² ? ³ ? ´ ? µ ? ¶ ? · ? ¸ ? ¹ ? º ? » ? ¼ ? ½ ? Á ? Â ? Ã ? Ä ? Å ? Æ ? Ç ? È ? Ø ? Ù ? Ý ? ß ? á ? ã ? å ? ç ? ê ? ì ? Ý ÿÿß ÿÿá ÿÿã ÿÿå ÿÿç ÿÿê ÿÿì ÿÿÞ ÿÿà ÿÿâ ÿÿä ÿÿæ ÿÿè ÿÿë ÿÿí ÿÿé ? ì ? í ? é ÿÿì ÿÿí ÿÿñ ÿÿé ÿÿë ÿÿê ÿÿÞ ÿÿÝ ÿÿà ÿÿß ÿÿä ÿÿã ÿÿâ ÿÿá ÿÿæ ÿÿå ÿÿè ÿÿç ÿÿí ÿÿì ÿÿª ÿÿÆ ÿÿÇ ÿÿÈ ÿÿÅ ÿÿÂ ÿÿÃ ÿÿÄ ÿÿÁ ÿÿÍ ÿÿÌ ÿÿÏ ÿÿÎ ÿÿœ ÿÿš ÿÿ¢ ÿÿ  ÿÿ ÿÿž ÿÿ› ÿÿ£ ÿÿ¤ ÿÿ¡ ÿÿ™ ÿÿŸ ÿÿ¦ ÿÿ§ ÿÿ¥ ÿÿŒ ÿÿ ÿÿv ÿÿu ÿÿx ÿÿw ÿÿŽ ÿÿ” ÿÿ’ ÿÿ‹ ÿÿ ÿÿ ÿÿ“ ÿÿ‘ ÿÿ– ÿÿ• ÿÿz ÿÿy ÿÿ~ ÿÿ} ÿÿ€ ÿÿ ÿÿ| ÿÿ{ ÿÿ‚ ÿÿ      ¾ ’    ? ÝH "w9 À                 @               @  D                                                                           Bþÿ÷ ø?XA @            pA @            }A @            dA @            %E @            -E @            < @            ¥< @            ¬< @   	         í? @   
         VB @            6C @            ¼A @            ÷A @            ŠA @            ¢A @            ¯A @            –A @            ÎA @            B @            ÄE @            µó             ¢@ @           ÅA @           B @           DC @           9? @           E? @           LH @           BH @           TH @           õÿ             õÿ            X    l        X    Ö        ª
    Ž        ª
    F        Žù    Z        Žù    n        X<    ÷        2<    þ        @<    ‘        L<    ¶        ²;   	         Ê;    L        ý;    ë        <    	        &<            <            b<            ½;    }        ê;    ×        Ø;    h
        s<    _        È@ @  `        Ø@ @  a        à@ @  b        Ð@ @  c        þ? @  d        QC @  e        uF @  f        bF @  g        ØA @  h        %B @  i        ìA @  j        EB @  k        âA @  l        5B @  m        iH @  n        ^H @  o        rH @  p        À@ @  q        ÝH @  r        @ @  s        @ @  t        !@ @  u        +@ @  v        5@ @  w        k@ @  x        ~@ @  y        è@ @  z        A @  {        A @  |        õ@ @  }        «@ @  ~        7E @          AE @  €        ”E @          £E @  ‚        =C @  ƒ        D @  „        D @  …        'D @  †        D @  ‡        ßB @  ˆ        êB @  ‰        øB @  Š        C @  ‹        C @  Œ        C @          )C @  Ž        KC @          ? @          @ @  ‘        ÌB @  ’        ÖB @  “        iB @  ”        wB @  •        †B @  –        ”B @  —        ¢B @  ˜        ¯B @  ™        ¾B @  š        ™@ @  ›        ?@ @  œ        J@ @          U@ @  ž        `@ @  Ÿ        t@ @           ‹@ @  ¡        A @  ¢        :A @  £        IA @  ¤        ,A @  ¥        µ@ @  ¦        Õ @  §        œ? @  ¨        [ã @  ©        PE @  ª        pE @  «        `E @  ¬        ‚E @  ­        /D @  ®        ¤D @  ¯        XD @  °        ÓD @  ±        GD @  ²        ÀD @  ³        8D @  ´        ¯D @  µ        ‘Õ @  ¶        Õ @  ·        pÕ @  ¸        à     Ê        Ü$     Ü        Ù>     Ý        ¢     å        ä&     ë        ®&     ø        À&     
        E             œ< @          ´< @          6     (             *        D     2        ¸     4        ? @  5        #? @  6        .? @  7        Õ> @  8        ö> @  9        zG @  :        ? @  ;        G @  <        å> @  =        fG @  >        SG @  ?        ˆD @  @        E @  A        nD @  B        ëD @  C        ~<    F        Ä= @  G        G @  H        1G @  I        'F @  J        ‹F @  K        à= @  L        ËF @  M        EF @  N        ¬F @  O        þ= @  P        ìF @  Q        ¼< @  R        F @  S        Ò< @  T        ê< @  U        2= @  V        ÿ< @  W        I= @  X        _= @  Y        ‘= @  Z        w= @  [        «= @  \        = @  ]        !> @  ^        ÷E @  _        7> @  `        O> @  a        n> @  b        ÉG @  c        > @  d        ÝG @  e        ^> @  f        ¶G @  g        ¤G @  h        > @  i        ±> @  j        H @  k        Ã> @  l        -H @  m         > @  n        H @  o        ñG @  p        |Ú @  q        ƒÚ @  r        ´? @  s        À? @  t        º @  u        È @  v        ­ @  w        â @  x        ï @  y        Ö @  z        † @  {        ” @  |        ¯ @  }        y @  ~         @          M @  €        ‚ @          B @  ‚        Ë @  ƒ        ½ @  „        è @  …        Ú @  †        ¸ @  ‡        r @  ˆ        Ž @  ‰        ž @  Š         @  ‹         @  Œ        - @          ) @  Ž         @          ; @           @  ‘        ÷ @  ’         @  “        Ø @  ”        ½ @  •        ­ @  –        å @  —        Ê @  ˜        é @  ™        | @  š        Œ @  ›         @  œ        m @          Y @  ž        I @  Ÿ        L @           < @  ¡        á! @  ¢        º! @  £        _! @  ¤        R! @  ¥        Ô! @  ¦        Ç! @  §        ­! @  ¨        Á @  ©        ± @  ª        ´ @  «        ¤ @  ¬        « @  ­        » @  ®        î! @  ¯        œ @  °        § @  ±        — @  ²        š @  ³        Š @  ´         ! @  µ        y! @  ¶        E! @  ·        8! @  ¸        “! @  ¹        †! @  º        l! @  »         @  ¼        } @  ½        € @  ¾        p @  ¿        Ú @  À        ê @  Á        Ë @  Â        s @  Ã        c @  Ä        f @  Å        V @  Æ        Ü @  Ç        Ì @  È        Î @  É        ¾ @  Ê        L @  Ë        < @  Ì        > @  Í        . @  Î        0 @  Ï          @  Ð        " @  Ñ         @  Ò         @  Ó         @  Ô         @  Õ        ö @  Ö        ø @  ×        è @  Ø        ê @  Ù        Ú @  Ú        Z @  Û        X @  Ü        h @  Ý        J @  Þ        v @  ß        v @  à        † @  á        f @  â        – @  ã        † @  ä        Î @  å        ¾ @  æ        À @  ç        ° @  è        ² @  é        ¢ @  ê        ¤ @  ë        ” @  ì        Ü @  í        Ì @  î        )" @  ï        7" @  ð        E" @  ñ        " @  ò        p @  ó        6 @  ô        æ @  õ         @  ö         @  ÷        F @  ø        w @  ù        1 @  ú        Ô @  û        ù @  ü        U @  ý         @  þ        z @  ÿ          @           C @          h @          V @          ¨ @          ‡ @          µ @          Ü @          “ @          vC @  	        Â @  
         @          Ó @          ëC @          û @          ú @          ² @          Á @          ¹ @          †C @          ØC @          –C @          Ï @          š @          ¦C @          R @          f @          — @          ë @          ¢ @          ¶C @          © @          ÇC @           ç @  !         @  "        Ü @  #        „ @  $        ý @  %        ó @  &        O @  '        k @  (        } @  )        Z @  *        ÷ @  +        }H @  ,        ŽH @  -        Þ @  .        " @  /        1 @  0        Æ @  1        P @  2        ^ @  3        € @  4        @ @  5        l @  6        { @  7        Š @  8        Ò @  9        © @  :        · @  ;        Œ @  <        ™ @  =        Å @  >        a @  ?        8 @  @        I @  A        ( @  B        Õ @  C         H @  D        °H @  E        ü @  F        õ @  G        ƒ @  H        w @  I        4 @  J        ' @  K        ª @  L        T @  M         @  N        Þ @  O        ¶ @  P        ÷ @  Q        s @  R        Ã @  S        . @  T         @  U        @ @  V         @  W        M @  X        c @  Y         @  Z        Ô @  [        æ @  \        ƒ @  ]        ! @  ^        [ @  _        $ @  `        ? @  a        w @  b        / @  c         @  d         @  e         @  f        ø @  g          @  h        < @  i        M @  j         @  k        1 @  l        i @  m        J @  n        ý @  o        í @  p        ò @  q        â @  r         @  s         @  t        — @  u         @  v        Ç @  w        í @  x        ˜ @  y         @  z        ¨ @  {         @  |        r @  }        X @  ~        I @          Ã @  €        • @          ” @  ‚        ¹ @  ƒ        y @  „        ® @  …        n @  †        W @  ‡        g @  ˆ        ÷ @  ‰         @  Š        w @  ‹        … @  Œ         @          7 @  Ž        ' @          L @           @  ‘        . @  ’        > @  “        ² @  ”         @  •        þ! @  –        " @  —        ž @  ˜        ½ @  ™        ² @  š          @  ›        º @  œ        ­ @          Ç @  ž        Õ @  Ÿ        È @           ¢ @  ¡        Ø @  ¢        É @  £        fC @  ¤        ç @  ¥        Û @  ¦        Ÿ @  §        ‘ @  ¨        WC @  ©        ­ @  ª        ç @  «        õ @  ¬        é @  ­        Ð @  ®        Þ @  ¯        é @  °        õ @  ±        " @  ²         @  ³        1 @  ´        ò @  µ          @  ¶        þ @  ·         @  ¸        ? @  ¹        Ó @  º        „ @  »         @  ¼        h @  ½        ) @  ¾        È @  ¿        – @  À        ª @  Á        G @  Â        V @  Ã        9 @  Ä        r @  Å        € @  Æ        e @  Ç        … @  È        ¢ @  É        ¿ @  Ê        v @  Ë        “ @  Ì        ° @  Í        g @  Î        Ð @  Ï        ¦ @  Ð        Ä @  Ñ        š @  Ò        Ü @  Ó        ù @  Ô        Í @  Õ        ê @  Ö        % @  ×        B @  Ø        _ @  Ù         @  Ú        3 @  Û        P @  Ü         @  Ý        & @  Þ        4 @  ß        u @  à        g @  á         @  â        Q @  ã        Z @  ä        ] @  å          @  æ        r @  ç        k @  è        ; @  é        O @  ê        ‚ @  ë        A @  ì         @  í        a @  î        Ô @  ï        Ê @  ð        ¹ @  ñ        ÁH @  ò        Ø @  ó         @  ô        þC @  õ        ÏH @  ö        ¼ @  ÷        Ê @  ø        å @  ù        ¯ @  ú        £ @  û        c @  ü        ˜ @  ý        X @  þ         @  ÿ        ó @            @           @          ³ @          Ã @          £ @          ë @          Û @          ' @           @  	         @  
         @          	 @          ù @          ú @          ê @          6 @          & @          Ð @          ã @          ö @          ¾ @          8 @          × @           @          . @          A @          	 @          H @          ç @          A @          1 @          Y @           ¼E @  !        å     #        Q? @  $        \? @  %        i? @  &        c&     ,        Ì     .        Ž     2        ?&     D        K&     Q        È     S        8     U        Z     W        L     Y              c        Ø     f        ÌE @  g        Û? @  h        –&     i        ù     j        v? @  k        ºÚ @  l        ïE @  m        ÀÚ @  n        èE @  o        ÈÚ @  p        àE @  q        ÎÚ @  r        ÙE @  s        Wö @  t        …? @  u        ³E @  v        Ÿÿ  @  w        Í? @  x        vº @  y        ûº @  z        ? @  {        )? @  |        4? @  }        >ö            zº @  €        6ü  @          =ü  @  ‚        Mü  @  ƒ        W? @  „        c? @  …        p? @  †        †ü  @  ‡        ü  @  ˆ        Ÿ @  ‰        4>     Ž        |þ  @          Šþ  @          šþ  @  ‘        ©þ  @  ’         ÿÿL ÿÿC ÿÿz ÿÿD ÿÿ{ ÿÿE ÿÿ€ ÿÿ: ÿÿ< ÿÿT ÿÿ? ÿÿY ÿÿA ÿÿB ÿÿy ÿÿ@ ÿÿM ÿÿ; ÿÿ> ÿÿm ÿÿ= ÿÿP ÿÿ‡ ÿÿçÿÿéÿÿêÿÿèÿÿ,ÿÿ-ÿÿ£ ÿÿ§ ÿÿ¨ ÿÿ³ÿÿûÿÿÿÿïÿÿõÿÿëÿÿíÿÿîÿÿìÿÿñÿÿ÷ÿÿMÿÿÑÿÿðÿÿöÿÿÿÿ-ÿÿ.ÿÿÿÿÿÿÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ ÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ&ÿÿ(ÿÿ*ÿÿ+ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿDÿÿHÿÿIÿÿ›ÿÿœÿÿ ÿÿ¢ÿÿ¥ÿÿ¦ÿÿ§ÿÿ3ÿÿ5ÿÿ7ÿÿ=ÿÿ>ÿÿ`ÿÿaÿÿbÿÿjÿÿkÿÿnÿÿ~ÿÿÿÿ‡ÿÿˆÿÿÿÿÿÿÿÿÿÿê? ë? ì? í? î? ï? ð? ñ? ò? ó? ô? õ? ö? ÷? ø? ù? ú? û? ü? ý? þ? ÿ?  ? ? ? ? ? ? ? ? ? 	? 
? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ?  ? !? "? #? $? &? (? *? +? ,? -? .? /? 0? 1? 2? 3? 4? 5? 6? 7? 8? 9? :? ;? <? =? >? ?? @? A? D? H? I? ›? œ?  ? ¢? ¥? ¦? §? 3? 5? 7? =? >? `? a? b? j? k? n? ~? ? ‡? ˆ? ? ? ? ? eÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿ,ÿÿ4ÿÿ6ÿÿ8ÿÿ;ÿÿ<ÿÿcÿÿdÿÿeÿÿlÿÿmÿÿoÿÿ…ÿÿ†ÿÿÿÿÿÿÿÿÿÿe? f? g? h? i? j? k? l? m? n? o? p? q? r? s? t? u? v? w? x? y? z? {? |? }? ~? ? €? ? ‚? ƒ? „? …? †? ‡? ˆ? ‰? Š? ‹? Œ? ? Ž? ? ? ‘? ’? “? ”? •? –? —? ˜? ™? š? ›? œ? ? ž? Ÿ?  ? ¡? ¢? £? ¤? «? ¬? ­? ®? ¯? °? ±? ²? ³? ´? µ? ¶? ·? ¸? ¹? º? »? ¼? ½? ¾? ¿? À? Á? Â? ,? 4? 6? 8? ;? <? c? d? e? l? m? o? …? †? ? ? ? ? ÿÿžÿÿ£ÿÿ¥ÿÿ¦ÿÿ§ÿÿÃÿÿÄÿÿÅÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ-ÿÿ9ÿÿ@ÿÿAÿÿBÿÿFÿÿGÿÿHÿÿLÿÿNÿÿPÿÿRÿÿSÿÿTÿÿXÿÿZÿÿ[ÿÿ\ÿÿfÿÿgÿÿhÿÿiÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ? ž? £? ¥? ¦? §? Ã? Ä? Å? Ç? È? É? Ê? Ë? Ì? Í? Î? Ï? Ð? Ñ? Ò? Ó? Ô? Õ? ×? Ø? Ù? Ú? Û? Ý? Þ? ß? à? á? â? ã? ä? å? æ? ç? è? é? ê? ë? ì? í? î? ï? ð? ñ? ò? ó? ô? õ? ö? ÷? ø? ù? ú? û? ü? ý? þ? ÿ?  ? ? ? ? ? ? ? ? ? 	? 
? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? ? !? "? #? $? %? &? '? (? )? -? 9? @? A? B? F? G? H? L? N? P? R? S? T? X? Z? [? \? f? g? h? i? p? q? r? s? t? u? v? w? x? y? z? {? |? }? €? ? ‚? ƒ? „? ‰? Š? ‹? Œ? ? Ž? ? ? ‘? ’? “? ”? •? –? —? ˜? ™? š? ›? œ? ? ž? Ÿ?  ? ¡? ¢? £? ¤? ¥? ¦? §? ¨? ? ? ? ? ? ? ? ? -? .? /? 0? C? D? E? I? J? K? M? O? Q? U? V? W? Y? ]? ^? _? -ÿÿ.ÿÿ/ÿÿ0ÿÿCÿÿDÿÿEÿÿIÿÿJÿÿKÿÿMÿÿOÿÿQÿÿUÿÿVÿÿWÿÿYÿÿ]ÿÿ^ÿÿ_ÿÿ‹ ÿÿŒ ÿÿ ÿÿ ÿÿ‘ ÿÿ• ÿÿ£ ÿÿ¤ ÿÿ§ ÿÿ¨ ÿÿ© ÿÿ¼ ÿÿÃ ÿÿÄ ÿÿÊ ÿÿÍ ÿÿÎ ÿÿÏ ÿÿÐ ÿÿ× ÿÿØ ÿÿÙ ÿÿÚ ÿÿç ÿÿè ÿÿé ÿÿð ÿÿñ ÿÿò ÿÿó ÿÿô ÿÿý ÿÿþ ÿÿÿ ÿÿ ÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ#ÿÿ+ÿÿ,ÿÿ-ÿÿ.ÿÿVÿÿWÿÿXÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿnÿÿrÿÿtÿÿxÿÿzÿÿ„ÿÿ…ÿÿ‡ÿÿÿÿ–ÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ ÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ&ÿÿ(ÿÿ*ÿÿ+ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿCÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿVÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿªÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿÀÿÿÁÿÿÂÿÿÃÿÿÈÿÿÊÿÿÌÿÿÎÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ ÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ*ÿÿ+ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ;ÿÿ<ÿÿ=ÿÿ?ÿÿ@ÿÿAÿÿBÿÿCÿÿDÿÿEÿÿFÿÿHÿÿIÿÿJÿÿKÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿVÿÿïÿÿðÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿw ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿx ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿªÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÆÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿ ÿÿŽ ÿÿ’ ÿÿ“ ÿÿ” ÿÿ– ÿÿ— ÿÿ˜ ÿÿ™ ÿÿš ÿÿ› ÿÿœ ÿÿ ÿÿž ÿÿŸ ÿÿ  ÿÿ¡ ÿÿ¢ ÿÿ² ÿÿÿÿ ÿÿ!ÿÿ"ÿÿUÿÿ[ÿÿ\ÿÿcÿÿlÿÿqÿÿsÿÿuÿÿ†ÿÿŽÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ¤ ÿÿ§ ÿÿ¨ ÿÿ© ÿÿ¼ ÿÿÄ ÿÿÎ ÿÿÏ ÿÿÐ ÿÿØ ÿÿÙ ÿÿÚ ÿÿç ÿÿé ÿÿÿÿÿÿÿÿÿÿÿÿÿÿ-ÿÿ.ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿnÿÿÿÿ–ÿÿ¯ÿÿ°ÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ$ÿÿ%ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿMÿÿVÿÿÿÿÿÿÿÿ	ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿS ÿÿÿÿèÿÿéÿÿ%ÿÿ'ÿÿ)ÿÿCÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿVÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÄÿÿÅÿÿÆÿÿÇÿÿÉÿÿËÿÿÍÿÿÏÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿX ÿÿŠ ÿÿ ÿÿ¥ ÿÿ¦ ÿÿª ÿÿ« ÿÿ¬ ÿÿ­ ÿÿ® ÿÿ¯ ÿÿ° ÿÿ± ÿÿ¶ ÿÿ· ÿÿ¸ ÿÿ¹ ÿÿº ÿÿ» ÿÿ½ ÿÿ¾ ÿÿ¿ ÿÿÀ ÿÿÁ ÿÿÂ ÿÿÅ ÿÿÆ ÿÿÈ ÿÿÉ ÿÿË ÿÿÌ ÿÿÑ ÿÿÒ ÿÿÓ ÿÿÔ ÿÿÕ ÿÿÖ ÿÿÛ ÿÿÜ ÿÿÝ ÿÿÞ ÿÿß ÿÿà ÿÿá ÿÿâ ÿÿã ÿÿä ÿÿå ÿÿæ ÿÿê ÿÿë ÿÿì ÿÿí ÿÿî ÿÿï ÿÿõ ÿÿö ÿÿ÷ ÿÿø ÿÿù ÿÿú ÿÿû ÿÿü ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿCÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿYÿÿZÿÿdÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿmÿÿoÿÿvÿÿwÿÿyÿÿ{ÿÿ‚ÿÿƒÿÿˆÿÿ‰ÿÿ‹ÿÿŒÿÿÿÿÿÿ—ÿÿ˜ÿÿ±ÿÿ²ÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿñ ÿÿò ÿÿó ÿÿý ÿÿþ ÿÿÿ ÿÿ	ÿÿ
ÿÿÿÿ ÿÿ!ÿÿ"ÿÿ#ÿÿ?ÿÿ@ÿÿCÿÿDÿÿEÿÿFÿÿHÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿïÿÿðÿÿv ÿÿÇ ÿÿkÿÿpÿÿŠÿÿ•ÿÿô ÿÿ ÿÿÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿAÿÿBÿÿIÿÿJÿÿKÿÿLÿÿ³ ÿÿ´ ÿÿµ ÿÿ• ÿÿÃ ÿÿÊ ÿÿÍ ÿÿ× ÿÿè ÿÿð ÿÿzÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿeÿÿfÿÿgÿÿ˜ÿÿ™ÿÿªÿÿ®ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿÀÿÿÁÿÿÂÿÿÃÿÿÈÿÿÊÿÿÌÿÿÎÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿüÿÿýÿÿþÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ*ÿÿ+ÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿ
ÿÿl ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿªÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÆÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿ¨ÿÿ©ÿÿªÿÿÆÿÿÖÿÿÜÿÿÿÿÿÿ ÿÿ*ÿÿ+ÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ ÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ&ÿÿ(ÿÿ*ÿÿ+ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿïÿÿðÿÿñÿÿòÿÿóÿÿôÿÿõÿÿöÿÿ÷ÿÿøÿÿùÿÿúÿÿûÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ!ÿÿ"ÿÿ#ÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ,ÿÿ-ÿÿ.ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿCÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿVÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿ]ÿÿ^ÿÿ_ÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿ«ÿÿ¬ÿÿ­ÿÿŠ ÿÿ ÿÿŽ ÿÿ’ ÿÿ“ ÿÿ” ÿÿ– ÿÿ— ÿÿ˜ ÿÿ™ ÿÿš ÿÿ› ÿÿœ ÿÿ ÿÿž ÿÿŸ ÿÿ  ÿÿ¡ ÿÿ¢ ÿÿ¥ ÿÿ¦ ÿÿª ÿÿ« ÿÿ¬ ÿÿ­ ÿÿ® ÿÿ¯ ÿÿ° ÿÿ± ÿÿ² ÿÿ³ ÿÿ´ ÿÿµ ÿÿ¶ ÿÿ· ÿÿ¸ ÿÿ¹ ÿÿº ÿÿ» ÿÿ½ ÿÿ¾ ÿÿ¿ ÿÿÀ ÿÿÁ ÿÿÂ ÿÿÅ ÿÿÆ ÿÿÇ ÿÿÈ ÿÿÉ ÿÿË ÿÿÌ ÿÿÑ ÿÿÒ ÿÿÓ ÿÿÔ ÿÿÕ ÿÿÖ ÿÿÛ ÿÿÜ ÿÿÝ ÿÿÞ ÿÿß ÿÿà ÿÿá ÿÿâ ÿÿã ÿÿä ÿÿå ÿÿæ ÿÿê ÿÿë ÿÿì ÿÿí ÿÿî ÿÿï ÿÿõ ÿÿö ÿÿ÷ ÿÿø ÿÿù ÿÿú ÿÿû ÿÿü ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ ÿÿ!ÿÿ"ÿÿ$ÿÿ%ÿÿ&ÿÿ'ÿÿ(ÿÿ)ÿÿ*ÿÿ/ÿÿ0ÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿ9ÿÿ:ÿÿ;ÿÿ<ÿÿ=ÿÿ>ÿÿ?ÿÿ@ÿÿAÿÿBÿÿCÿÿDÿÿEÿÿFÿÿGÿÿHÿÿIÿÿJÿÿKÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿYÿÿZÿÿ[ÿÿ\ÿÿcÿÿdÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿoÿÿpÿÿqÿÿsÿÿuÿÿvÿÿwÿÿyÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ†ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ—ÿÿ˜ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿªÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÆÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿ%ÿÿ'ÿÿ)ÿÿCÿÿLÿÿMÿÿNÿÿOÿÿPÿÿQÿÿRÿÿSÿÿTÿÿUÿÿVÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿ¨ÿÿ©ÿÿªÿÿÆÿÿÖÿÿÜÿÿÿÿÿÿ ÿÿ*ÿÿ+ÿÿ±ÿÿ²ÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÄÿÿÅÿÿÆÿÿÇÿÿÉÿÿËÿÿÍÿÿÏÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿ1ÿÿ2ÿÿ3ÿÿ4ÿÿ5ÿÿ6ÿÿ7ÿÿ8ÿÿWÿÿXÿÿYÿÿZÿÿ[ÿÿ\ÿÿ]ÿÿ^ÿÿ_ÿÿ`ÿÿaÿÿbÿÿcÿÿdÿÿeÿÿfÿÿgÿÿhÿÿiÿÿjÿÿkÿÿlÿÿmÿÿnÿÿoÿÿpÿÿqÿÿrÿÿsÿÿtÿÿuÿÿvÿÿwÿÿxÿÿyÿÿzÿÿ{ÿÿ|ÿÿ}ÿÿ~ÿÿÿÿ€ÿÿÿÿ‚ÿÿƒÿÿ„ÿÿ…ÿÿ†ÿÿ‡ÿÿˆÿÿ‰ÿÿŠÿÿ‹ÿÿŒÿÿÿÿŽÿÿÿÿÿÿ‘ÿÿ’ÿÿ“ÿÿ”ÿÿ•ÿÿ–ÿÿ—ÿÿ˜ÿÿ™ÿÿšÿÿ›ÿÿœÿÿÿÿžÿÿŸÿÿ ÿÿ¡ÿÿ¢ÿÿ£ÿÿ¤ÿÿ¥ÿÿ¦ÿÿ§ÿÿ¨ÿÿ©ÿÿªÿÿ«ÿÿ¬ÿÿ­ÿÿ®ÿÿ¯ÿÿ°ÿÿ±ÿÿ²ÿÿ³ÿÿ´ÿÿµÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿ»ÿÿ¼ÿÿ½ÿÿ¾ÿÿ¿ÿÿÀÿÿÁÿÿÂÿÿÃÿÿÄÿÿÅÿÿÆÿÿÇÿÿÈÿÿÉÿÿÊÿÿËÿÿÌÿÿÍÿÿÎÿÿÏÿÿÐÿÿÑÿÿÒÿÿÓÿÿÔÿÿÕÿÿÖÿÿ×ÿÿØÿÿÙÿÿÚÿÿÛÿÿÜÿÿÝÿÿÞÿÿßÿÿàÿÿáÿÿâÿÿãÿÿäÿÿåÿÿæÿÿçÿÿèÿÿéÿÿêÿÿëÿÿìÿÿíÿÿîÿÿÕÿÿ×ÿÿØÿÿÖÿÿ´ÿÿÿÿðÿÿïÿÿòÿÿøÿÿôÿÿúÿÿóÿÿùÿÿ	ÿÿÿÿ
ÿÿÔÿÿÿÿ¶ÿÿ·ÿÿ¸ÿÿ¹ÿÿºÿÿÈÿÿÌÿÿÙÿÿÛÿÿÜÿÿÚÿÿÒÿÿ.ÿÿ/ÿÿIÿÿJÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿ	ÿÿ
ÿÿÿÿÿÿÿÿµÿÿÿÿÿÿüÿÿýÿÿþÿÿÿÿÿ ÿÿÿÿÿÿÐÿÿÀÿÿÁÿÿÂÿÿÃÿÿÊÿÿÎÿÿÝÿÿßÿÿàÿÿÞÿÿÓÿÿ=ÿÿ–ÿÿ‹ ÿÿ?ÿÿAÿÿ@ÿÿBÿÿ ÿÿ&ÿÿ#ÿÿ)ÿÿ"ÿÿ(ÿÿ!ÿÿ'ÿÿ‘ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ‘ ÿÿ+ÿÿ,ÿÿVÿÿrÿÿsÿÿtÿÿuÿÿ„ÿÿ…ÿÿ‘ÿÿ’ÿÿ;ÿÿ<ÿÿDÿÿFÿÿ ÿÿ ÿÿ‘ ÿÿ+ÿÿ,ÿÿVÿÿrÿÿsÿÿtÿÿuÿÿ„ÿÿ…ÿÿ‘ÿÿ’ÿÿ;ÿÿ<ÿÿDÿÿFÿÿUÿÿ” ÿÿ• ÿÿ– ÿÿCÿÿDÿÿEÿÿFÿÿHÿÿWÿÿXÿÿ‡ÿÿCÿÿEÿÿHÿÿVÿÿWÿÿXÿÿ„ÿÿ…ÿÿ‡ÿÿ‘ÿÿ’ÿÿCÿÿDÿÿEÿÿFÿÿHÿÿŒ ÿÿ ÿÿ ÿÿ‘ ÿÿ• ÿÿ#ÿÿ+ÿÿ,ÿÿVÿÿWÿÿXÿÿrÿÿsÿÿtÿÿuÿÿ‡ÿÿ;ÿÿ<ÿÿVÿÿWÿÿ„ÿÿ…ÿÿ‘ÿÿ’ÿÿCÿÿDÿÿEÿÿFÿÿ¤ ÿÿ© ÿÿŒ ÿÿ ÿÿ ÿÿ‘ ÿÿ• ÿÿ#ÿÿ+ÿÿ,ÿÿVÿÿWÿÿXÿÿrÿÿsÿÿtÿÿuÿÿ‡ÿÿ;ÿÿ<ÿÿVÿÿWÿÿ„ÿÿ…ÿÿ‘ÿÿ’ÿÿCÿÿDÿÿEÿÿFÿÿ‡ÿÿHÿÿÿÿÿÿÿÿ	ÿÿÿÿúÿÿÿÿûÿÿ
ÿÿùÿÿøÿÿ%ÿÿ+ÿÿ$ÿÿ*ÿÿ³ ÿÿ´ ÿÿµ ÿÿÚ ÿÿöÿÿ÷ÿÿUÿÿòÿÿç ÿÿôÿÿVÿÿóÿÿè ÿÿõÿÿ¼ ÿÿTÿÿÃ ÿÿÄ ÿÿÎ ÿÿÊ ÿÿÏ ÿÿÐ ÿÿØ ÿÿ× ÿÿÙ ÿÿÍ ÿÿé ÿÿSÿÿð ÿÿñ ÿÿó ÿÿþÿÿô ÿÿÿÿÿò ÿÿýÿÿüÿÿý ÿÿÿ ÿÿÿÿ ÿÿÿÿþ ÿÿÿÿ ÿÿ+ÿÿ,ÿÿJÿÿKÿÿ[ÿÿ\ÿÿZÿÿ^ÿÿ_ÿÿ]ÿÿÄÿÿÅÿÿÇÿÿÃÿÿfÿÿëÿÿeÿÿêÿÿÉÿÿÈÿÿËÿÿÊÿÿ4ÿÿ3ÿÿXÿÿYÿÿsÿÿüÿÿãÿÿtÿÿýÿÿâÿÿÍÿÿÌÿÿhÿÿlÿÿjÿÿiÿÿmÿÿkÿÿ.ÿÿùÿÿúÿÿpÿÿøÿÿxÿÿÿÿwÿÿ ÿÿ§ÿÿ¤ÿÿÿÿœÿÿ¦ÿÿ¥ÿÿ£ÿÿ€ÿÿ	ÿÿÿÿÿÿüÿÿýÿÿ¨ÿÿûÿÿ~ÿÿÿÿ}ÿÿÿÿ¢ÿÿŸÿÿ›ÿÿšÿÿ¡ÿÿ ÿÿžÿÿ|ÿÿÿÿ{ÿÿÿÿÿÿÿ ÿÿþÿÿzÿÿÿÿyÿÿÿÿ‚ÿÿÿÿÿÿ
ÿÿŠÿÿÿÿ‰ÿÿÿÿˆÿÿÿÿ‡ÿÿÿÿ†ÿÿÿÿ…ÿÿÿÿ„ÿÿÿÿƒÿÿÿÿŒÿÿÿÿ‹ÿÿÿÿŽÿÿÿÿÿÿÿÿÿÿÿÿ“ÿÿÿÿ’ÿÿÿÿ‘ÿÿÿÿÿÿÿÿ”ÿÿÿÿ¬ÿÿ­ÿÿ®ÿÿ«ÿÿ(ÿÿ$ÿÿŠÿÿÿÿŒÿÿÿÿ¥ÿÿŽÿÿ‰ÿÿ‹ÿÿÿÿ“ÿÿ’ÿÿ”ÿÿÿÿ‘ÿÿžÿÿ›ÿÿ¦ÿÿœÿÿŸÿÿcÿÿÿÿ ÿÿÿÿgÿÿÿÿ¡ÿÿÿÿeÿÿfÿÿbÿÿÿÿÿÿÿÿ¢ÿÿ`ÿÿÿÿ&ÿÿ£ÿÿ§ÿÿ¤ÿÿdÿÿÿÿaÿÿÿÿhÿÿñÿÿgÿÿðÿÿoÿÿnÿÿÿÿƒÿÿ™ÿÿ‚ÿÿÿÿÿÿÿÿFÿÿ@ÿÿAÿÿ6ÿÿCÿÿDÿÿ5ÿÿBÿÿEÿÿFÿÿGÿÿ8ÿÿIÿÿJÿÿ7ÿÿHÿÿKÿÿÿÿÿÿ˜ÿÿ€ÿÿ~ÿÿÿÿÿÿ9ÿÿ/ÿÿ°ÿÿ¯ÿÿ9ÿÿ8ÿÿ³ÿÿÿÿ7ÿÿ<ÿÿ´ÿÿ-ÿÿÿÿµÿÿ/ÿÿ±ÿÿ:ÿÿ²ÿÿ;ÿÿÿÿÎÿÿLÿÿMÿÿ(ÿÿÏÿÿvÿÿrÿÿtÿÿxÿÿÐÿÿjÿÿûÿÿiÿÿúÿÿ0ÿÿÑÿÿuÿÿqÿÿsÿÿwÿÿÒÿÿlÿÿùÿÿkÿÿøÿÿ„ÿÿ:ÿÿ)ÿÿ»ÿÿ<ÿÿ;ÿÿ=ÿÿIÿÿ>ÿÿHÿÿÿÿ­ÿÿ1ÿÿ–ÿÿzÿÿ×ÿÿnÿÿïÿÿmÿÿîÿÿÓÿÿÔÿÿNÿÿOÿÿÕÿÿyÿÿ?ÿÿuÿÿþÿÿäÿÿÿÿÿÿÿÿ•ÿÿ2ÿÿ©ÿÿªÿÿBÿÿõÿÿôÿÿ…ÿÿ‡ÿÿ†ÿÿˆÿÿ÷ÿÿöÿÿØÿÿÿÿÿÿÿÿÿÿ·ÿÿÿÿÿÿÿÿÿÿ¸ÿÿ4ÿÿ3ÿÿ¶ÿÿ2ÿÿGÿÿÿÿ"ÿÿ!ÿÿ#ÿÿ¹ÿÿ5ÿÿºÿÿ6ÿÿ$ÿÿEÿÿ	ÿÿP      /  <    ? xJ         	                                                                                                                   `I              þH              -I              °I @           'J @           J @           çI @           šI @           ûI @ 	          |_    	         ­c    	         ‹c    	 
        rU    	         UU    	         äX    	         íX    	         Ão   	         U    	         @U    	 #        ŽI   
 $       ŽI    %       ÈI    %       5J    %       EV     &       :V     '       -V     (       æV     )       W     +       _W     ,       æH    -  	     d     .  
     ×ò    /       YI    /       é­     0       xJ @  1       ù­     2       eJ @  3       ƒI    3       GI    3       ®     4       IJ @  5       nI    5        ®     6       VJ @  7       àI    :       àI    <       ‚ò    <       E ÿÿD ÿÿF ÿÿŸ ÿÿª ÿÿ¦ ÿÿ¤ ÿÿž ÿÿ¥ ÿÿ ÿÿ  ? ¡ ÿÿ° ÿÿ. ÿÿ[ ÿÿt ÿÿN ÿÿ_ ÿÿ£ ÿÿ« ? Ç ?  ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿJ ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿÏ ÿÿÕ ÿÿà ÿÿá ÿÿ¤ ÿÿ¤ ? Æ ÿÿÆ ÿÿÆ ÿÿÆ ÿÿ ÿÿ— ÿÿ1 ÿÿª ÿÿ¦ ÿÿ¥ ÿÿá ÿÿá ÿÿà ÿÿà ÿÿÏ ÿÿÏ ÿÿÕ ÿÿÕ ÿÿÇ ÿÿÏ ÿÿÕ ÿÿÏ ? Õ ?  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ?  ÿÿ, ÿÿ, ÿÿ, ÿÿ ÿÿ, ÿÿ, ÿÿ, ? , ÿÿ, ÿÿ, ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                b - Ø    " .O         …                                                                                                               ð5 	            5             ØJ             ýL @          |_            »M @         €M @         ŸM @         *M @ 	        GM @ 
        M @         IK           Ím            »N @         ám            O @         K           `K           oK           SK           ‹K           òm            sN @         ÿm            ÒN @         n            ƒN @         n            ”N @         /n            MN @         An            ¨N @         Qn            bN @          _n     !       æN @  "       wn     #       O @  $       n     %       3N @  &       –n     '       'N @  (       Ÿn     )       ?N @  *       @K    9  	     N    F  	     gM @  F  
     ­c     R  
     ‹c     a  
     rU     p  
     UU       
     äX     ‹  
     íX     —  
     U     ¦  
     @U     µ  
     Ýc     À  
     !o    Á  
     d     Å       >d     Ç       Fm    È       Pm    É       Pm    Ê       †P    Ë       †P    Ì       Ÿì  	   Ï       Ÿì     Ò       Œ_     Ó       •_     Ô       „_     Õ       "K 
   Õ       ËL    Õ       ¶L    Õ       ãL    Õ       yL 
   Õ       cL 
   Õ       1L 
   Õ       L 
   Õ       òK 
   Õ       GL 
   Õ       ŽL 
   Õ       L 
    Õ       §L 
  ! Õ       æJ 
  " Õ       ˜K   # Õ       ´K   $ Õ       ¦K   % Õ       ÙK   & Õ       ÆK   ' Õ       K 
  ( Õ       3Ì    ( Ö       .O @ ( ×       K 
  ) ×       ùJ 
  * ×       /K   + ×       p¨   + Ø       ùM @ , Ø       ÙM @ - Ø         ÿÿU ÿÿ8ÿÿ ÿÿ9ÿÿ@ÿÿ>ÿÿ?ÿÿ;ÿÿ<ÿÿ:ÿÿ> ÿÿF ÿÿA ÿÿB ÿÿ@ ÿÿK ÿÿ< ÿÿCÿÿ=ÿÿ5 ÿÿ| ÿÿt ÿÿ} ÿÿc ÿÿb ÿÿ_ ÿÿ\ ÿÿV ÿÿa ÿÿe ÿÿ^ ÿÿg ÿÿ" ÿÿM ÿÿO ÿÿN ÿÿR ÿÿQ ÿÿ. ÿÿ+ ÿÿ% ÿÿ5 ÿÿBÿÿAÿÿ:ÿÿ>ÿÿJÿÿeÿÿhÿÿkÿÿmÿÿnÿÿsÿÿwÿÿzÿÿ}ÿÿ‰ÿÿ‹ÿÿtÿÿtÿÿƒÿÿƒÿÿhÿÿhÿÿwÿÿwÿÿkÿÿkÿÿnÿÿnÿÿ]ÿÿ]ÿÿqÿÿqÿÿeÿÿeÿÿzÿÿzÿÿ}ÿÿ}ÿÿRÿÿSÿÿIÿÿKÿÿZÿÿZÿÿ=ÿÿ]ÿÿeÿÿhÿÿkÿÿnÿÿpÿÿtÿÿvÿÿwÿÿyÿÿzÿÿ|ÿÿ}ÿÿƒÿÿIÿÿRÿÿZÿÿeÿÿhÿÿkÿÿnÿÿqÿÿtÿÿwÿÿzÿÿ}ÿÿƒÿÿHÿÿ\ÿÿdÿÿgÿÿjÿÿmÿÿpÿÿsÿÿvÿÿyÿÿ|ÿÿ‰ÿÿHÿÿQÿÿYÿÿ\ÿÿdÿÿgÿÿjÿÿmÿÿpÿÿsÿÿvÿÿyÿÿ|ÿÿƒÿÿ‰ÿÿIÿÿRÿÿZÿÿ]ÿÿeÿÿhÿÿkÿÿnÿÿqÿÿtÿÿwÿÿzÿÿ}ÿÿƒÿÿŠÿÿIÿÿRÿÿZÿÿ]ÿÿeÿÿhÿÿkÿÿnÿÿqÿÿtÿÿwÿÿzÿÿ}ÿÿƒÿÿŠÿÿHÿÿ\ÿÿdÿÿgÿÿjÿÿmÿÿpÿÿsÿÿvÿÿyÿÿ|ÿÿ‰ÿÿHÿÿ\ÿÿdÿÿgÿÿjÿÿmÿÿpÿÿsÿÿvÿÿyÿÿ|ÿÿ‰ÿÿIÿÿRÿÿZÿÿ]ÿÿeÿÿhÿÿkÿÿnÿÿqÿÿtÿÿwÿÿzÿÿ}ÿÿƒÿÿŠÿÿIÿÿRÿÿZÿÿ]ÿÿeÿÿhÿÿkÿÿnÿÿqÿÿtÿÿwÿÿzÿÿ}ÿÿƒÿÿŠÿÿHÿÿ\ÿÿdÿÿgÿÿjÿÿmÿÿpÿÿsÿÿvÿÿyÿÿ|ÿÿE ÿÿ?ÿÿ@ÿÿAÿÿBÿÿdÿÿgÿÿJÿÿ9" 9ÿÿŠÿÿŠ" KÿÿSÿÿŒÿÿK" S" Œ" ;ÿÿ< ÿÿjÿÿŠÿÿŒÿÿ\ÿÿS ÿÿ] ÿÿ\ ÿÿY ÿÿF ÿÿN ÿÿM ÿÿQ ÿÿL ÿÿO ÿÿK ÿÿJ ÿÿP ÿÿR ÿÿ> ÿÿ ÿÿ ÿÿ
 ÿÿC ÿÿ ÿÿ	 ÿÿ- ÿÿ/ ÿÿ ÿÿ: ÿÿ ÿÿ ÿÿa ÿÿ` ÿÿ= "  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ "  ÿÿ ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ  ÿÿ ÿÿ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   U * Ä &   " ÞS         …        €                                                                                        @               ûÆO              9P @           ÕP @           °P @           >Q @           rP @           R @           “R @           >R @ 	          &Q @ 
     	     ]R @      
     xR @           ãO @           ŽP @           P @           ÿO @           VP @           R @           ÌQ @           °Q @           ”Q @           çQ @           ­R @           sQ @           WQ @           |_             [O            KO            ;O            pO            ‚O            ŽO            •O             òP   !         þP   "         Q   #         @K   #         N   # 
        ­c    #         ‹c    #          rU    # ,        UU    # 8        äX    # B        íX    # L        U    # X        @U    # d        d    # g        >d    # h        uš   # i        uš   # j        Ÿì  	  # t        Ÿì    # ~         •_    #    "     3É    # ‚   "     îR @ # ƒ   "     VÉ    # „   "     ÿR @ # …   "     hÉ    # †   "     S @ # ‡   "     ªÉ    # ˆ   "     eS @ # ‰   "     ÄÉ    # Š   "     ,S @ # ‹   "     Ê    # Œ   "     LS @ #    "     EÊ    # Ž   "     ÞS @ #    "     \Ê    #    "     ÞR @ # ‘   "     ×Ê    # ’   "     ‚S @ # “   "     fË    # ”   "     ¥S @ # •   "     ‘Ë    # –   "     ½S @ # —   "     ¼Ë    # ˜   "     ÆR @ # ™   "     œO   $ ™   "     ±O   % ™   "     ¬J   & ™   "     ˜§   & ¨   "     ˜§   ' ·  "     ÐO   ( ¼  $     ÐO   * Ã & &     Wâ    * Ä & &       ÿÿ: ÿÿ? ÿÿ> ÿÿI ÿÿ< ÿÿQ ÿÿU ÿÿR ÿÿH ÿÿS ÿÿT ÿÿ7 ÿÿ= ÿÿ9 ÿÿ8 ÿÿ; ÿÿP ÿÿN ÿÿM ÿÿL ÿÿO ÿÿV ÿÿK ÿÿJ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿC ÿÿD ÿÿE ÿÿ ÿÿ ÿÿ ÿÿG ÿÿ. " 6 ÿÿ- " O ÿÿU ÿÿV ÿÿ¥ ÿÿ¦ ÿÿÝ ÿÿN ÿÿP ÿÿ< ÿÿM ÿÿ[ ÿÿ} ÿÿ ÿÿ¥ ÿÿ± ÿÿÅ ÿÿÚ ÿÿç ÿÿ÷ ÿÿÿÿ[ ÿÿ^ ÿÿl ÿÿ} ÿÿ ÿÿ¥ ÿÿ± ÿÿÅ ÿÿÚ ÿÿç ÿÿ÷ ÿÿÿÿ[ ÿÿ^ ÿÿl ÿÿ~ ÿÿ‘ ÿÿ¦ ÿÿ² ÿÿÆ ÿÿÛ ÿÿè ÿÿø ÿÿÿÿ[ ÿÿ^ ÿÿl ÿÿ~ ÿÿ‘ ÿÿ¦ ÿÿ² ÿÿÆ ÿÿÛ ÿÿè ÿÿø ÿÿÿÿ[ ÿÿ} ÿÿ ÿÿ¥ ÿÿ± ÿÿÅ ÿÿÚ ÿÿç ÿÿ÷ ÿÿÿÿ[ ÿÿ} ÿÿ ÿÿ¥ ÿÿ± ÿÿÅ ÿÿÚ ÿÿç ÿÿ÷ ÿÿÿÿ[ ÿÿ^ ÿÿl ÿÿ~ ÿÿ‘ ÿÿ¦ ÿÿ² ÿÿÆ ÿÿÛ ÿÿè ÿÿø ÿÿÿÿ[ ÿÿ^ ÿÿl ÿÿ~ ÿÿ‘ ÿÿ¦ ÿÿ² ÿÿÆ ÿÿÛ ÿÿè ÿÿø ÿÿÿÿQ ÿÿR ÿÿ¨ ÿÿÚ ÿÿ± " ± ÿÿ? ÿÿl ÿÿ ÿÿ“ ÿÿ¨ ÿÿ² ÿÿÉ ÿÿÞ ÿÿë ÿÿ	ÿÿ? " l "  " “ " ¨ " ² " É " Þ " ë " 	" > ÿÿS ÿÿT ÿÿl ÿÿl ÿÿ~ ÿÿ ÿÿ‘ ÿÿ“ ÿÿÆ ÿÿÉ ÿÿ¦ ÿÿ¨ ÿÿ² ÿÿ² ÿÿÿÿ	ÿÿ^ ÿÿ^ ÿÿÛ ÿÿÞ ÿÿè ÿÿë ÿÿø ÿÿø ÿÿ[ ÿÿ[ ÿÿ[ " ^ " l " } "  "  " ‘ " “ " Å " Æ " È " Û " è " ø " " [ ÿÿ^ ÿÿl ÿÿ} ÿÿ ÿÿ ÿÿ‘ ÿÿ“ ÿÿÅ ÿÿÆ ÿÿÈ ÿÿÛ ÿÿè ÿÿø ÿÿÿÿ9 " : " ; " I " K " - ÿÿ. ÿÿ9 ÿÿ: ÿÿ; ÿÿI ÿÿK ÿÿê ÿÿ	 ÿÿS ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ% ÿÿ ÿÿ! ÿÿ ÿÿ ÿÿ ÿÿ$ ÿÿ ÿÿ. ÿÿ ÿÿ ÿÿ4 ÿÿ
 ÿÿ ÿÿ ÿÿ ÿÿR "  ÿÿ ÿÿS ÿÿ ÿÿ ÿÿ ÿÿ% ÿÿ ÿÿ ÿÿ4 ÿÿ ÿÿ2 ÿÿ ÿÿR " 3 " S ÿÿS ÿÿS ÿÿQ ÿÿS ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿS ÿÿS ÿÿS ÿÿS ÿÿS ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿQ ÿÿS ÿÿQ ÿÿQ ÿÿS ÿÿS " S ÿÿQ ÿÿS " Q " S ÿÿQ ÿÿ                                                              ÿÿ%T                                                                                                                                 %T              T              
 ÿÿ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               © > !ñ   ÿÿÕU        Ð…    0  À‚ @       
€                                                       €   € „8ð£ x    ÷P @      påá @           Ù× @           ” @           /m @         • @         Nj @         ,k @    	     q’ @  	  
     #à @  
       ök @  
       ˆv @         ]7 @         ÷ï @         ¢Ø @         km @        uj @        Tk @        *l @        s9 @        `° B         ¤á @        6T @ "  !     ·T @ #  "     3U @ &  $     RU  	 '  %     ŒÛ @ 	 )  &     | @ 	 *  '     ¨
  
 .  (     ÚT    =  -     Îá    >  .     ‚k   B  /     ‚k    I  3     ÕU   K  4     Ä×    M  5     Y“    O  6     ˜   Q  7     
} @ U  :     µU   X  ;     ÇÙ @  Y  <     U   ]  =     j    `  @     šU   b  A     „U   g  B     ¯è @  i  C     íT    j  D     }U   r  E     nU   x  F     £U   z  G     §U    }  H     d        I     m    ‹  L     Úk    ›  Q     °U  ! Ÿ  R     xl @" Ÿ ) S     –m @# Ÿ N T     Àl @$ Ÿ b U     œj @$ ¡ b V     Ml @% ¡ g W     'b  ( ¬ g Z     ‚•  ( ¬  Z     ºU  ) ¯  [     |_    ) Ç  c     ñÞ 
  ) È  d     Jß 
  ) É  e     =ß 
  ) Ê  f     H“ 
  ) Ë  g     Û”   ) Ì  h     @“ 
  ) Í  i     0j   ) Õ  m     jU  * à  n       + â  o     9n  
, ã Ê q     çm  
- ä ñ s     Bƒ  / î ñ u     `U  0 ñ ñ v     vU  1 ö ñ w     yQ    1 ñ z     ^ò   2 ñ {     Ê„  3 ñ |     8ñ @ 3 ñ }     ôT   4 ñ ~     üT   5 ñ      üT  6 ñ €     +ë @ 6 !ñ      äT   7 "ñ ‚     F±  ; 8ñ †     b   < >ñ ‡     ¨‘   < ?ñ ˆ     ËU  = @ñ ‰     [Ý B = Añ Š     ´Ý B = Bñ ‹     ‡Ý B = Cñ Œ     ÂU  > Eñ       Y    > Fñ      “c    > Gñ      ¡c    > Hñ      rU    > añ ’     UU    > zñ —     íX    > {ñ —     
˜  > ~ñ š     ðq    > ñ ›     Œ; @ > €ñ œ     U    > ™ñ ¡     r•   > ›ñ ¡     ·U    > ñ ¢     @U    > ¶ñ §     %T   > ¸ñ ¨     9ë    > ºñ ©     
ë    > ¼ñ ª     `î  
  > ½ñ «     ñt    > ¾ñ ¬     “	 @ > ¿ñ ­     dâ    > Áñ ­     vâ    > Ãñ ®     å—  > Æñ ±     ík   > Èñ ²     Çí  
  > Íñ ¶     d    > Ññ ¸     >d    > Ôñ ¹     Dd    > Ùñ »     e 
  > Ûñ ¼     à   > Üñ ½     S    > Ýñ ½     vS    > Þñ ¾     Bœ    > ßñ ¿     –m @ > àñ À     ¥œ    > áñ Á     ¥m @ > âñ Â     Èœ    > ãñ Ã     tm @ > äñ Ä     Pm   > åñ Å     Ÿì  	  > ëñ Ê     „_    > ìñ Ë     ®    > ïñ Ì     IJ @ > òñ Í      ®    > õñ Î     VJ @ > øñ Ï     ¶t   > ùñ Ð     Û¤   > ûñ Ñ     (É    > ýñ Ò     ¿¨ @ > ÿñ Ó     ªÉ    >  ñ Ô     eS @ > ñ Õ     ×Ê    > ñ Ö     ‚S @ > ñ ×     ÑË    > ñ Ø     y¨ @ > ñ Ù     ˜§   > ñ Ú     ÂÌ    > ñ Û     Th @ > ñ Ü     ðÌ    > 	ñ Ý     <h @ > 
ñ Þ     ¸] 
  > ñ ß     Ä] 
  > ñ à     â] 
  > ñ á     ^ 
  > ñ â     ^ 
  > ñ ã     ø^ 
  > ñ ä     €\ 	  > ñ æ     ï   > ñ è     iP   > ñ é     r×    > ñ ê     VV @ > ñ ë     ª×    > ñ ì     ¹V @ > ñ í     <Ø    > ñ î     iV @ > ñ ï     áØ    >  ñ ð     yV @ > !ñ ñ     z ÿÿ$ ÿÿ. ÿÿ8 ÿÿ~ ÿÿ' ÿÿ1 ÿÿ> ÿÿ† ÿÿÿÿG ÿÿR ÿÿe ÿÿ ÿÿÜ ÿÿT ÿÿG ÿÿe ÿÿ ÿÿÜ ÿÿ8ÿÿ<ÿÿÞ ÿÿ1ÿÿÿÿ#ÿÿÿÿR ÿÿâ ÿÿ© ÿÿ$ÿÿ%ÿÿ.ÿÿR ÿÿ ÿÿe ÿÿG ÿÿ9 ÿÿ? ÿÿ ÿÿ2ÿÿ ÿÿ'ÿÿÿÿÜ ÿÿà ÿÿ5ÿÿŽ ÿÿÂ ÿÿ† ÿÿ3ÿÿR ÿÿÿÿp ÿÿR ÿÿn ÿÿ‘ ÿÿÝ ÿÿÿÿR ÿÿ7ÿÿ4ÿÿ9ÿÿ#ÿÿ$ÿÿî ÿÿõ ÿÿè ÿÿ8 ÿÿz ÿÿå ÿÿ=ÿÿh ÿÿ(ÿÿ+ÿÿ© ÿÿ#ÿÿ$ÿÿ8 ÿÿ> ÿÿz ÿÿ~ ÿÿ(ÿÿ+ÿÿh ÿÿ© ÿÿå ÿÿè ÿÿî ÿÿõ ÿÿ9ÿÿ=ÿÿ
ÿÿ‡ ÿÿÖ ÿÿÿÿA ÿÿ€ ÿÿ® ÿÿ¸ ÿÿ© ÿÿ(ÿÿ+ÿÿq ÿÿÿÿÿÿ ÿÿ,ÿÿH ÿÿI ÿÿS ÿÿV ÿÿ[ ÿÿf ÿÿg ÿÿh ÿÿi ÿÿm ÿÿq ÿÿœ ÿÿÂ ÿÿÊ ÿÿà ÿÿ9ÿÿV ÿÿW ÿÿ[ ÿÿ] ÿÿI ÿÿm ÿÿp ÿÿœ ÿÿÂ ÿÿÊ ÿÿÞ ÿÿ9ÿÿ=ÿÿ#ÿÿ$ÿÿî ÿÿõ ÿÿà ÿÿã ÿÿI ÿÿV ÿÿ[ ÿÿm ÿÿ2ÿÿ8ÿÿ<ÿÿ.ÿÿ%ÿÿ'ÿÿ+ÿÿEÿÿà ÿÿÿÿ5ÿÿ+ÿÿ.ÿÿÿÿ%ÿÿ(ÿÿ)ÿÿ+ÿÿ› ÿÿÉ ÿÿ[ ÿÿå ÿÿè ÿÿî ÿÿõ ÿÿ	ÿÿÿÿÿÿÿÿª ÿÿ® ÿÿ¯ ÿÿ´ ÿÿµ ÿÿ¸ ÿÿ(ÿÿ.ÿÿ&ÿÿ'ÿÿ(ÿÿ(ÿÿ+ÿÿ{ ÿÿ ÿÿ€ ÿÿ— ÿÿ™ ÿÿ› ÿÿ³ ÿÿ´ ÿÿ¼ ÿÿÇ ÿÿÉ ÿÿÐ ÿÿ9 ÿÿ: ÿÿ? ÿÿ@ ÿÿA ÿÿH ÿÿI ÿÿS ÿÿV ÿÿ[ ÿÿf ÿÿg ÿÿh ÿÿi ÿÿm ÿÿq ÿÿ1ÿÿ9ÿÿ=ÿÿBÿÿœ ÿÿÊ ÿÿ9 ÿÿ: ÿÿ? ÿÿ@ ÿÿA ÿÿ™ ÿÿš ÿÿ› ÿÿÇ ÿÿÈ ÿÿÉ ÿÿ3ÿÿ4ÿÿ7ÿÿ9 ÿÿ? ÿÿG ÿÿH ÿÿM ÿÿN ÿÿO ÿÿQ ÿÿS ÿÿV ÿÿ[ ÿÿe ÿÿf ÿÿp ÿÿ ÿÿ ÿÿÜ ÿÿÿÿÿÿ'ÿÿ2ÿÿ3ÿÿ4ÿÿ7ÿÿ3ÿÿ7ÿÿ4ÿÿõ ÿÿè ÿÿî ÿÿ‡ ÿÿŒ ÿÿ ÿÿ› ÿÿÂ ÿÿÉ ÿÿÛ ÿÿâ ÿÿ› ÿÿœ ÿÿŸ ÿÿ¤ ÿÿ¥ ÿÿÉ ÿÿÊ ÿÿÍ ÿÿÏ ÿÿÑ ÿÿØ ÿÿ(ÿÿ)ÿÿà ÿÿÂ ÿÿá ÿÿå ÿÿè ÿÿî ÿÿõ ÿÿ
ÿÿ9ÿÿ=ÿÿ?ÿÿDÿÿ— ÿÿ˜ ÿÿ¨ ÿÿÏ ÿÿÐ ÿÿÑ ÿÿÒ ÿÿÖ ÿÿ ÿÿ– ÿÿ— ÿÿœ ÿÿŸ ÿÿ¢ ÿÿ¤ ÿÿ© ÿÿª ÿÿ¯ ÿÿ² ÿÿ³ ÿÿµ ÿÿ· ÿÿ¼ ÿÿ½ ÿÿÆ ÿÿÊ ÿÿÍ ÿÿÏ ÿÿÒ ÿÿÔ ÿÿá ÿÿâ ÿÿ	ÿÿÿÿÿÿ!ÿÿ&ÿÿ5ÿÿ?ÿÿEÿÿIÿÿKÿÿ‡ ÿÿ8ÿÿ<ÿÿÐ ÿÿU ÿÿÿÿq ÿÿ™ ÿÿÇ ÿÿV ÿÿq ÿÿ© ÿÿª ÿÿ« ÿÿ­ ÿÿÐ ÿÿÞ ÿÿß ÿÿå ÿÿè ÿÿî ÿÿõ ÿÿÿÿÿÿÿÿÿÿÿÿ(ÿÿ+ÿÿ.ÿÿ/ÿÿHÿÿT ÿÿV ÿÿW ÿÿ[ ÿÿ\ ÿÿ] ÿÿå ÿÿ9ÿÿ6ÿÿ4ÿÿ;ÿÿ6ÿÿ;ÿÿ† ÿÿ… ÿÿ‰ ÿÿ ÿÿT ÿÿV ÿÿW ÿÿ[ ÿÿ\ ÿÿ] ÿÿp ÿÿŠ ÿÿ– ÿÿ¢ ÿÿ² ÿÿ· ÿÿÆ ÿÿÔ ÿÿÞ ÿÿÿÿÿÿÿÿ ÿÿ%ÿÿ,ÿÿ.ÿÿ1ÿÿBÿÿ ÿÿT ÿÿV ÿÿW ÿÿ[ ÿÿ\ ÿÿ] ÿÿp ÿÿŠ ÿÿ– ÿÿ¢ ÿÿ² ÿÿ· ÿÿÆ ÿÿÔ ÿÿÞ ÿÿÿÿÿÿÿÿ ÿÿ%ÿÿ,ÿÿ.ÿÿ1ÿÿBÿÿ† ÿÿ­ ÿÿÿÿÿÿp ÿÿp ÿÿ ÿÿT ÿÿV ÿÿW ÿÿ[ ÿÿ\ ÿÿ] ÿÿp ÿÿŠ ÿÿ– ÿÿ¢ ÿÿ² ÿÿ· ÿÿÆ ÿÿÔ ÿÿÞ ÿÿÿÿÿÿÿÿ ÿÿ%ÿÿ,ÿÿ.ÿÿ1ÿÿBÿÿ ÿÿŠ ÿÿÿÿ.ÿÿ ÿÿT ÿÿV ÿÿW ÿÿ[ ÿÿ\ ÿÿ] ÿÿp ÿÿŠ ÿÿ– ÿÿ¢ ÿÿ² ÿÿ· ÿÿÆ ÿÿÔ ÿÿÞ ÿÿÿÿÿÿÿÿ ÿÿ%ÿÿ,ÿÿ.ÿÿ1ÿÿBÿÿÿÿ.ÿÿÿÿ.ÿÿÿÿ.ÿÿ« ÿÿ%ÿÿ%ÿÿ ÿÿŠ ÿÿ„ ÿÿNÿÿª ÿÿß ÿÿ/ÿÿÿÿ.ÿÿn ÿÿ‘ ÿÿÝ ÿÿÿÿHÿÿG ÿÿP ÿÿŽ ÿÿ— ÿÿÿÿ%ÿÿ<ÿÿG ÿÿT ÿÿe ÿÿ ÿÿÜ ÿÿp ÿÿq ÿÿ=ÿÿ	 ÿÿë ÿÿBÿÿBÿÿ1ÿÿ1ÿÿ.ÿÿ.ÿÿ.ÿÿ8 ÿÿn ÿÿz ÿÿ‘ ÿÿÝ ÿÿÿÿ1ÿÿ– ÿÿ² ÿÿÆ ÿÿ– ÿÿ² ÿÿÆ ÿÿ¢ ÿÿ· ÿÿÔ ÿÿ¢ ÿÿ· ÿÿÔ ÿÿh ÿÿ(ÿÿ+ÿÿ ÿÿ,ÿÿ ÿÿ,ÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÿÞ ÿÿÞ ÿÿÿÿÿÿô ÿÿí ÿÿò ÿÿó ÿÿç ÿÿä ÿÿÞ ÿÿÿÿR ÿÿ© ÿÿM ÿÿT ÿÿ] ÿÿT ÿÿ] ÿÿV ÿÿ[ ÿÿV ÿÿ[ ÿÿ\ ÿÿ\ ÿÿW ÿÿW ÿÿ ÿÿ ÿÿ2 ÿÿ ÿÿ ÿÿ= ÿÿ: ÿÿ3 ÿÿ ÿÿ2 ÿÿ ÿÿ ÿÿ= ÿÿ: ÿÿ3 ÿÿ ÿÿM ÿÿD ÿÿ ÿÿV ÿÿT ÿÿ, ÿÿP ÿÿ ÿÿ= ÿÿ3 ÿÿw ÿÿ ÿÿ¡ ÿÿf ÿÿ` ÿÿi ÿÿa ÿÿ¢ ÿÿ£ ÿÿ¤ ÿÿ$ ÿÿ§ ÿÿ¨ ÿÿ¥ ÿÿ¦ ÿÿ ÿÿ ÿÿu ÿÿJ ÿÿ= ÿÿ: ÿÿD ÿÿE ÿÿL ÿÿƒ ÿÿU ÿÿt ÿÿ… ÿÿf ÿÿ` ÿÿi ÿÿa ÿÿ† ÿÿ2 ÿÿS ÿÿ+ ÿÿ8 ÿÿ‡ ÿÿˆ ÿÿŸ ÿÿ. ÿÿ ÿÿ ÿÿ ÿÿr ÿÿm ÿÿc ÿÿ ÿÿK ÿÿH ÿÿO ÿÿ ÿÿ ÿÿ ÿÿ= ÿÿ3 ÿÿ‰ ÿÿ
 ÿÿ ÿÿ$ ÿÿƒ ÿÿU ÿÿt ÿÿR ÿÿd ÿÿf ÿÿ` ÿÿi ÿÿa ÿÿx ÿÿe ÿÿ ÿÿ ÿÿ ÿÿ= ÿÿ3 ÿÿ$ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿƒ ÿÿ	 ÿÿ ÿÿu ÿÿw ÿÿ= ÿÿ9 ÿÿ  ÿÿŸ ÿÿ5 ÿÿ7 ÿÿ ÿÿ ÿÿq ÿÿD ÿÿ6 ÿÿH ÿÿ( ÿÿž ÿÿG ÿÿQ ÿÿƒ ÿÿU ÿÿt ÿÿ“ ÿÿ ÿÿ‘ ÿÿf ÿÿ` ÿÿi ÿÿa ÿÿj ÿÿk ÿÿh ÿÿl ÿÿs ÿÿ’ ÿÿ ÿÿŽ ÿÿ= ÿÿ* ÿÿv ÿÿ' ÿÿ ÿÿ– ÿÿ— ÿÿ ÿÿc ÿÿ‹ ÿÿŒ ÿÿL ÿÿ! ÿÿ) ÿÿ ÿÿ ÿÿ/ ÿÿ0 ÿÿn ÿÿo ÿÿF ÿÿ ÿÿŠ ÿÿ1 ÿÿ ÿÿ ÿÿ‚ ÿÿ4 ÿÿ€ ÿÿ& ÿÿ ÿÿr ÿÿ„ ÿÿ% ÿÿ~ ÿÿ ÿÿ< ÿÿN ÿÿ> ÿÿ\ ÿÿ@ ÿÿZ ÿÿ( ÿÿI ÿÿY ÿÿX ÿÿ? ÿÿ  ÿÿ ÿÿ  ÿÿ ÿÿ[ ÿÿy ÿÿ ÿÿ| ÿÿ} ÿÿ ÿÿ ÿÿƒ ÿÿU ÿÿt ÿÿž ÿÿ# ÿÿ” ÿÿf ÿÿ` ÿÿi ÿÿa ÿÿ• ÿÿr ÿÿ( ÿÿI ÿÿG ÿÿL ÿÿD ÿÿ- ÿÿ ÿÿW ÿÿ ÿÿ ÿÿœ ÿÿB ÿÿ ÿÿ{ ÿÿ™ ÿÿ" ÿÿC ÿÿ ÿÿš ÿÿ› ÿÿ˜ ÿÿA ÿÿ ÿÿc ÿÿ ÿÿG ÿÿG ÿÿH ÿÿ; ÿÿH ÿÿ; ÿÿ	 ÿÿ; ÿÿ ÿÿH ÿÿG ÿÿ; ÿÿ7 ÿÿG ÿÿ6 ÿÿG ÿÿ; ÿÿ; ÿÿ	 ÿÿ ÿÿ; ÿÿ ÿÿ ÿÿ; ÿÿG ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿH ÿÿ ÿÿ6 ÿÿH ÿÿ ÿÿ ÿÿ6 ÿÿ6 ÿÿG ÿÿ7 ÿÿG ÿÿ9 ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿ5 ÿÿ9 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿG ÿÿH ÿÿH ÿÿ9 ÿÿ5 ÿÿ7 ÿÿG ÿÿG ÿÿG ÿÿH ÿÿ; ÿÿG ÿÿG ÿÿG ÿÿ6 ÿÿ5 ÿÿH ÿÿ6 ÿÿG ÿÿG ÿÿG ÿÿ ÿÿ ÿÿ6 ÿÿ	 ÿÿ ÿÿ9 ÿÿ5 ÿÿ7 ÿÿG ÿÿ; ÿÿ; ÿÿ; ÿÿ6 ÿÿ; ÿÿ	 ÿÿ ÿÿ6 ÿÿG ÿÿ	 ÿÿ ÿÿ; ÿÿ9 ÿÿ5 ÿÿ7 ÿÿ6 ÿÿG ÿÿG ÿÿG ÿÿG ÿÿH ÿÿH ÿÿH ÿÿ ÿÿ; ÿÿ6 ÿÿH ÿÿ6 ÿÿG ÿÿH ÿÿ; ÿÿ6 ÿÿ; ÿÿH ÿÿG ÿÿ6 ÿÿ6 ÿÿ6 ÿÿH ÿÿG ÿÿ ÿÿG ÿÿ6 ÿÿ5 ÿÿG ÿÿ7 ÿÿ6 ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿ5 ÿÿH ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿ6 ÿÿH ÿÿG ÿÿ7 ÿÿ7 ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿG ÿÿ5 ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿG ÿÿG ÿÿG ÿÿ6 ÿÿG ÿÿG ÿÿ; ÿÿ6 ÿÿH ÿÿG ÿÿG ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿ; ÿÿ6 ÿÿG ÿÿ; ÿÿ5 ÿÿ7 ÿÿG ÿÿH ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿ; ÿÿ7 ÿÿ6 ÿÿH ÿÿG ÿÿG ÿÿ6 ÿÿ6 ÿÿ6 ÿÿ6 ÿÿ7 ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿG ÿÿH ÿÿH ÿÿG ÿÿG ÿÿH ÿÿH ÿÿH ÿÿH ÿÿH ÿÿH ÿÿH ÿÿ; ÿÿ; ÿÿ6 ÿÿ; ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ5 ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              s  á |   
 †î  p    t         @     p                                                                                                     °ƒŸ            U— @           4— @           — @           v— @          †î @          ° B      	     — @      
     ·— @   
        Ø— @         ‹Õ @        UÞ @        º° B         |           –           ù— @                    «˜ B         }˜ B         ¼™ @         HÞ          |Ÿ 
          _Ÿ 	          ¶   	        B   
         \      !     )ž B    "     •™ @    $     È– B    %     î– B    &     – B    '     Ð” B    (     &™ @   	 *     ˜ B    	 +     œ @  ! 	 ,     bœ @  " 	 -      › @  # 	 .     E› @  $ 	 /     < @  % 	 0     } @  & 	 1     ‘› @  ' 	 2     Ô› @  ( 	 3     ­œ @  ) 	 4     ñœ @  * 	 5     ßž @ + 	 6     è“ B  , 	 7     Z• B  - 	 8     ” B  . 	 9     5” B  / 	 :     €• B  0 	 ;     ¶š @  1 	 <     ‹š @  2 	 =     ]ž B  3 	 >     ‰ž B  4 	 ?     [” B  5 	 @     §• B  6 	 A     Ì• B  7 	 B     ‚” B  8 	 C     š @  9 	 D     ñ• B  : 	 E     ª” B  ; 	 F     Ú˜ @  < 
 H     àš @  = 
 I     Ð   > 
 J     ®Ÿ  ? 
 K     r™ @  @  M     ;– B  A  N     Ä“ B  B  O     ™ @  C  Q     ÷” B  D  R     µž B  E  S     ^– B  F  T     (• B  G  U     „– B  H  V     ¦– B  I  W     L™ @  J  Y     Å @  K  Z     ‚•   K  Z     7Ÿ   L  [     |_     P  \     	ž @  Q  ^     ÙŸ   R  `     ³“     | b    rÕ    | b    Ý    | b    ÷  @  € | c    «“    € | c    ßj     | d    óŸ   ‚ | f    D˜ B  ƒ | g    Ÿ   „ | h    Mš @  … | i    æ @  † | j    —    ‡ | k    :ì     ˆ | l    rU     ‰ | l    UU     Š | l    íX     ¨ | m    U     © | m    r•    ª | m    @U     « | m    dâ     ¬ | m    vâ     ­ | n    d     ´ | p    E°    ¶ | q    Ÿì  	   » | r    •_     ¼ | s    )¢    ½ | t    ?¢    ¿ | u    ¢    À | v    V¢    Á | w    Û¤    Ð | x    ‡    ß | y    a±    à | z   $    á | |   èÿÿäS äV ØÿÿÙÿÿÓÿÿÉÿÿÇÿÿÛÿÿÕÿÿÖÿÿîÿÿôÿÿÿÿúÿÿ ÿÿÿÿÑÿÿµ ÿÿ ÿÿèÿÿÒÿÿÔÿÿÊÿÿÿÿÿÿí ÿÿÿÿäS äS "ÿÿ+ÿÿéÿÿ5ÿÿµ S µ V äS ÿÿBÿÿïÿÿyÿÿoÿÿµÿÿÊÿÿÿÿÿÿ‹ÿÿªÿÿÿÿá ÿÿã ÿÿ× ÿÿÆ ÿÿÿÿTÿÿ)ÿÿ:ÿÿòÿÿÿÿ[ÿÿjÿÿÿÿÿÿCÿÿNÿÿ­ÿÿº ÿÿÍ ÿÿ¼ ÿÿ¾ ÿÿÏ ÿÿÞÿÿ×ÿÿ•ÿÿŸÿÿÀ ÿÿÑ ÿÿÓ ÿÿÂ ÿÿÂÿÿÕ ÿÿÄ ÿÿ€ÿÿçÿÿÝÿÿÏÿÿ¡ÿÿÙ ÿÿ¸ ÿÿ‰ÿÿÈ ÿÿ¦ÿÿÛ ÿÿÊ ÿÿÝ ÿÿß ÿÿ—ÿÿpÿÿÀÿÿÂÿÿpÿÿ‹ÿÿÔÿÿÿÿõÿÿ¸ ÿÿº ÿÿ¼ ÿÿ¾ ÿÿÀ ÿÿÂ ÿÿÄ ÿÿÆ ÿÿÈ ÿÿÊ ÿÿÍ ÿÿÏ ÿÿÑ ÿÿÓ ÿÿÕ ÿÿ× ÿÿÙ ÿÿÛ ÿÿÝ ÿÿß ÿÿá ÿÿã ÿÿí ÿÿÿÿÿÿ"ÿÿ+ÿÿ5ÿÿBÿÿTÿÿbÿÿoÿÿyÿÿÿÿÿÿ9ÿÿMÿÿiÿÿ‹ÿÿÀÿÿÊÿÿÏÿÿÝÿÿãÿÿçÿÿãÿÿéÿÿûÿÿcÿÿµÿÿÏÿÿzÿÿµÿÿµ ÿÿ+ ÿÿ+ ÿÿ¸ ÿÿº ÿÿ¼ ÿÿ¾ ÿÿÀ ÿÿÂ ÿÿÄ ÿÿÆ ÿÿÈ ÿÿÊ ÿÿÍ ÿÿÏ ÿÿÑ ÿÿÓ ÿÿÕ ÿÿ× ÿÿÙ ÿÿÛ ÿÿÝ ÿÿß ÿÿá ÿÿã ÿÿTÿÿbÿÿoÿÿyÿÿ‹ÿÿ•ÿÿŸÿÿ¦ÿÿ+ ÿÿ+ ÿÿ+ ÿÿ+ ÿÿÿÿ+ÿÿ5ÿÿÿÿÀÿÿÛÿÿçÿÿÿÿ¦ÿÿ­ÿÿcÿÿµÿÿÂÿÿÏÿÿãÿÿçÿÿÓÿÿÖÿÿÙÿÿÑÿÿÒÿÿò
 
 
 
 
 
 )
 9
 :
 C
 M
 N
 [
 i
 j
 òÿÿÿÿÿÿÿÿÿÿÿÿ)ÿÿ9ÿÿ:ÿÿCÿÿMÿÿNÿÿ[ÿÿiÿÿjÿÿµ ÿÿÿÿW ÿÿ ÿÿ S  S  S  V  V  ÿÿ ÿÿQ ÿÿ ÿÿX ÿÿ ÿÿ ÿÿr ÿÿ^ ÿÿR ÿÿg ÿÿ	 ÿÿ  ÿÿ= ÿÿD ÿÿK ÿÿA ÿÿP ÿÿf ÿÿ ÿÿq ÿÿR ÿÿa ÿÿC ÿÿ- ÿÿ/ ÿÿ0 ÿÿ6 ÿÿ9 ÿÿ< ÿÿ ÿÿE ÿÿH ÿÿ. ÿÿ1 ÿÿ7 ÿÿ8 ÿÿ; ÿÿ ÿÿB ÿÿG ÿÿI ÿÿJ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿg ÿÿ ÿÿ	 ÿÿ ÿÿ! ÿÿi ÿÿY ÿÿ ÿÿ ÿÿ= ÿÿD ÿÿ  ÿÿK ÿÿA ÿÿ ÿÿ] ÿÿ ÿÿO ÿÿ: ÿÿ[ ÿÿ3 ÿÿ2 ÿÿj ÿÿ> ÿÿp ÿÿ$ ÿÿ% ÿÿ( ÿÿ) ÿÿ" ÿÿ# ÿÿ* ÿÿ+ ÿÿ& ÿÿ' ÿÿL ÿÿ\ ÿÿP ÿÿ ÿÿ4 ÿÿ5 ÿÿh ÿÿF ÿÿ, ÿÿZ ÿÿN ÿÿ ÿÿ  ÿÿ@ ÿÿm ÿÿQ ÿÿn ÿÿX ÿÿk ÿÿ ÿÿr ÿÿ ÿÿl ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ? ÿÿU ÿÿo 
 
 S  V R ÿÿR ÿÿR ÿÿR ÿÿR ÿÿ
 S  V 
 S  V R ÿÿR ÿÿ	 ÿÿR ÿÿM ÿÿR S R V 
 S R ÿÿK ÿÿR ÿÿR ÿÿR ÿÿ  ÿÿR ÿÿR ÿÿR ÿÿ ÿÿR ÿÿR ÿÿP ÿÿR ÿÿA ÿÿR ÿÿR ÿÿR ÿÿK ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿM ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR ÿÿR       
      ÿÿ	¢         ˆ             @                                                                                                        @5 	            |_            Å¡ @         Ù¡ @         š¡           	¢ @         ï¡ @         ¥¡ 
          >d            ¹¡            ÿÿ@ ÿÿI ÿÿ ÿÿW ÿÿP ÿÿ ÿÿ7 ÿÿ@ ÿÿI ÿÿP ÿÿW ÿÿP ÿÿW ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ	 ÿÿ	 ÿÿ	 ÿÿ	 ÿÿ	 ÿÿ  ÿÿ	 ÿÿ	 ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       i $ -0  ÿÿŠ«        VH         €`                                                                                                       ïŒ¤           ±¤ D          ¹© @        ý¤ D        û© @        )ª @   
     ‚•         |_           g¤      	   yQ       
   Ù¤         Ù§        >¤         ­c     /     ‹c     C     rU     Y     UU     o          p     äX     „     íX     ˜     U     ®     r•    ¯     @U     Å     dâ     Æ     vâ     Ç     õê     È     ´c     Ü     d     Ý         ñ     $¢ 	   ò     7¢ 
   ò     Ë~     ô     Y¤ @  ö     s¢ 
  	 ö     Ö~    	 ÷     « @ 	 ø     ê~    	 ù     —ª @ 	 ú     ~¢   
 ú     ÷~    
 û     Åª @ 
 ü         
 ý     òª @ 
 þ         
 ÿ     « @ 
      #    
     r« @ 
     8    
     C« @ 
     K    
     Y« @ 
     i¡        a         0« @      q     	    Óª @  
    P¢ 
   
    £ 
   
    £ 
   
    ]£ 
   
    l£ 
   
    {         §ª @      …         ¤ @               ¤ @      ž         àª @      ­         ´ª @      »         Š« @      Í         ,¤ @      Z¢ 
        ã£ 
    !   õ£ 
    "   ·£ 
    #   Ê£ 
    $   Ü      $   vª @   $   é      $   †ª @   $   h¢ 
    %   F¢ 
    &   +£ 
    '   :£ 
    (   £ 
    )   £ 
    *   ß¢ 
    +   ð¢ 
    ,   “¢ 
    -   ¢¢ 
    .   ·¢ 
     /   È¢ 
  !  0   >d    !   0   S    ! ! 0   Ÿì  	  ! ) 0   O£ 	  " )" 0    £   " *" 0   ¹¡   " +" 0   ¥£ 	  # +& 0   iP   # -& 0   ˆ¢ 	  $ -0 0   %ÿÿ%ÿÿ%ÿÿ%ÿÿ%ÿÿÿÿV ÿÿX ÿÿl ÿÿq ÿÿ ÿÿ\ ÿÿ¤ ÿÿ§ ÿÿ  ÿÿ¢ ÿÿ^ ÿÿ¿ ÿÿÃ ÿÿ¸ ÿÿº ÿÿi ÿÿZ ÿÿ ÿÿ“ ÿÿŒ ÿÿŽ ÿÿˆ ÿÿŠ ÿÿ ÿÿ ÿÿƒ ÿÿ… ÿÿ ÿÿ³ ÿÿ| ÿÿ%ÿÿ%ÿÿ%ÿÿØ ÿÿì ÿÿî ÿÿÿÿ6ÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ—ÿÿ¢ÿÿ¤ÿÿ°ÿÿºÿÿÈÿÿÓÿÿçÿÿèÿÿ%ÿÿ%ÿÿ%ÿÿ%ÿÿÔ ÿÿê ÿÿÿÿÿÿ3ÿÿKÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ¢ÿÿ¯ÿÿºÿÿÇÿÿÓÿÿçÿÿóÿÿÿÿÔ ÿÿê ÿÿÿÿÿÿ3ÿÿKÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ¢ÿÿ¯ÿÿºÿÿÇÿÿÓÿÿçÿÿóÿÿÿÿÕ ÿÿë ÿÿÿÿÿÿÿÿ%ÿÿ4ÿÿLÿÿ[ÿÿgÿÿsÿÿ~ÿÿŠÿÿ–ÿÿ£ÿÿ°ÿÿ»ÿÿÈÿÿÔÿÿèÿÿôÿÿÿÿÕ ÿÿë ÿÿÿÿÿÿÿÿ%ÿÿ4ÿÿLÿÿ[ÿÿgÿÿsÿÿ~ÿÿŠÿÿ–ÿÿ£ÿÿ°ÿÿ»ÿÿÈÿÿÔÿÿèÿÿôÿÿÿÿ%ÿÿÔ ÿÿê ÿÿÿÿÿÿ3ÿÿKÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ¢ÿÿ¯ÿÿºÿÿÇÿÿÓÿÿçÿÿóÿÿÿÿÔ ÿÿê ÿÿÿÿÿÿ3ÿÿKÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ¢ÿÿ¯ÿÿºÿÿÇÿÿÓÿÿçÿÿóÿÿÿÿÕ ÿÿë ÿÿÿÿÿÿÿÿ%ÿÿ4ÿÿLÿÿ[ÿÿgÿÿsÿÿ~ÿÿŠÿÿ–ÿÿ£ÿÿ°ÿÿ»ÿÿÈÿÿÔÿÿèÿÿôÿÿÿÿÿÿÕ ÿÿë ÿÿÿÿÿÿÿÿ%ÿÿ4ÿÿLÿÿ[ÿÿgÿÿsÿÿ~ÿÿŠÿÿ–ÿÿ£ÿÿ°ÿÿ»ÿÿÈÿÿÔÿÿèÿÿôÿÿÿÿÿÿ'ÿÿÿÿÔ ÿÿê ÿÿÿÿÿÿ3ÿÿKÿÿZÿÿfÿÿrÿÿ}ÿÿ‰ÿÿ•ÿÿ¢ÿÿ¯ÿÿºÿÿÇÿÿÓÿÿçÿÿóÿÿÿÿÇÿÿÔ ÿÿê ÿÿÿÿÿÿÿÿ%ÿÿ4ÿÿLÿÿ[ÿÿgÿÿsÿÿ~ÿÿŠÿÿ–ÿÿ£ÿÿÈÿÿÔÿÿèÿÿôÿÿÿÿÕ ÿÿÿÿ%ÿÿÿÿ%ÿÿ»ÿÿ»ÿÿ[ÿÿ[ÿÿ~ÿÿ~ÿÿ£ÿÿ¤ÿÿ°ÿÿ°ÿÿôÿÿôÿÿÔÿÿÔÿÿèÿÿèÿÿÈÿÿÈÿÿŠÿÿŠÿÿgÿÿgÿÿÕ ÿÿÙ ÿÿë ÿÿð ÿÿ–ÿÿ—ÿÿsÿÿsÿÿÿÿÿÿÿÿÿÿ4ÿÿ7ÿÿLÿÿNÿÿ× ÿÿë ÿÿí ÿÿ5ÿÿ ÿÿÙ ÿÿð ÿÿÿÿ7ÿÿNÿÿèÿÿôÿÿÿÿôÿÿï ÿÿMÿÿÿÿ ÿÿ
 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ  ÿÿ
 ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ	 ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿU ÿÿ8 ÿÿK ÿÿT ÿÿ! ÿÿ; ÿÿ< ÿÿ9 ÿÿ: ÿÿN ÿÿO ÿÿL ÿÿM ÿÿ\ ÿÿ] ÿÿ^ ÿÿ_ ÿÿZ ÿÿ[ ÿÿX ÿÿY ÿÿV ÿÿW ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿc ÿÿc ÿÿc ÿÿc ÿÿ ÿÿf ÿÿf ÿÿf ÿÿf ÿÿ ÿÿ ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿh ÿÿ  ÿÿ ÿÿ                                                                                                                                                                                                                    
      ÿÿ-¬        H                                                                                                                       õ« @           ¬ @           Ý« @           |_             “c             ¡c             FT             Î«           Æ«           -¬           ) ÿÿ. ÿÿ$ ÿÿ ÿÿ ÿÿ6 ÿÿ$ ÿÿ) ÿÿ. ÿÿ ÿÿ9 ÿÿ1 ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               T  ¥ R  
 #·        Ld          @               (@À                                                        @      `                    @ ÿh® 	           ^¶           ¶          · @         R¬ @        i± @        ° B         Ï¶     	     s¬ @ 	       ›¬ @  	       #· @ 	 	      †¶   	 	      '® @ 
 	      :¬     L     z“     L     ô´   L     *˜     L     Ã¬ @  L     „² @   L     –° B  ! L     Hµ  " L     B° B  # L     ï° B  $ L     G± B  % L     î¯ B  & L     #¯ @ ' L     ÞG 
   ' L     d     ( L     9¶  	 ) L      Ž® @	 * L !    J¯ @	 + L "    ª³ B 	 , L #    œ± B 	 - L $    Ý± B 	 . L %    1² B 	 / L &    î¬ @	 0 L '    ò³ @ 	 1 L (    M´ @ 	 2 L )    "­ @	 3 L *    `­ @	 4 L +    ‚•  	 4 Q +    èµ   	 5 Q ,     ¶   	 6 Q -    |_    	 E Q /    ž¯ @	 F Q 0    Ô² @ 
 G R 2    ³® @
 H R 3    ³“   
 V R 4    ™µ 
 W R 5    É¯ @
 X R 6    ÿ® @
 Y R 7    ßj    Z R 8    t¯ @ [ R 9    ×® @ \ R :    ¢´ @ ] R ;    ³ B  ^ R <    ý¶    _ R =    ©­ @ ` R >    ä­ @ a R ?    —    b R @    :ì     c R A    rU     d R A    UU     e R A    íX     p R B    U     q R B    r•    r R B    @U     s R B    dâ     t R B    vâ     u R C    d     | R E    $¢ 	   } R F    E°    ~ R G    S      R G    FT     € R H    Ÿì  	    R I    ?¢    ’ R J     £    ” R K    Û¤    ™ R L    ‡    ž R M    m³ B  Ÿ R N    a±      R O   -¬    ¢ R P   iP    ¥ R Q   «¶   ¥ R R   Í ÿÿ8ÿÿ€ ÿÿEÿÿ=ÿÿc ÿÿ ÿÿÔ ÿÿ6ÿÿHÿÿ8ÿÿ€ ÿÿ>ÿÿ4ÿÿ8ÿÿi ÿÿEÿÿ8ÿÿEÿÿDÿÿx ÿÿ€ ÿÿË ÿÿx ÿÿ€ ÿÿ‡ ÿÿ7ÿÿOÿÿeÿÿzÿÿ“ÿÿÊÿÿãÿÿ&ÿÿ)ÿÿ*ÿÿ+ÿÿ4ÿÿ8ÿÿDÿÿ)ÿÿ” ÿÿË ÿÿ‡ ÿÿÌÿÿfÿÿ*ÿÿPÿÿ{ÿÿ”ÿÿ8ÿÿù ÿÿ8ÿÿIÿÿÛ ÿÿÿÿùÿÿŸÿÿ¬ÿÿ·ÿÿŽ ÿÿÿÿ
ÿÿ” ÿÿ¥ ÿÿ-ÿÿ.ÿÿÓ ÿÿÛ ÿÿâ ÿÿê ÿÿò ÿÿù ÿÿÿÿÿÿÿÿ"ÿÿŸÿÿËÿÿ)ÿÿ*ÿÿ+ÿÿÿÿÔÿÿâ ÿÿ8ÿÿPÿÿfÿÿ{ÿÿ”ÿÿŸÿÿªÿÿ«ÿÿ¶ÿÿäÿÿÿÿ)ÿÿ*ÿÿ+ÿÿ+ÿÿ"ÿÿò ÿÿ8ÿÿÿÿê ÿÿ&ÿÿæÿÿ€ ÿÿ­ ÿÿÁ ÿÿùÿÿc ÿÿ4 ÿÿ4 ÿÿ7ÿÿOÿÿeÿÿzÿÿ“ÿÿŸÿÿªÿÿ¶ÿÿãÿÿñÿÿùÿÿ4 ÿÿ4 ÿÿ4 ÿÿ4 ÿÿLÿÿx ÿÿ€ ÿÿÿÿÔÿÿñÿÿ6ÿÿGÿÿåÿÿŽ ÿÿ ÿÿÖÿÿ¥ ÿÿÁ ÿÿ8ÿÿPÿÿfÿÿ{ÿÿ”ÿÿÌÿÿæÿÿñÿÿÿÿ
ÿÿ&ÿÿ)ÿÿ*ÿÿ+ÿÿ=ÿÿ>ÿÿÿÿ
ÿÿ­ 
 ª
 ¬
 ¶
 ·
 ­ ÿÿªÿÿ¬ÿÿ¶ÿÿ·ÿÿñÿÿc ÿÿãÿÿñÿÿ¥ ÿÿÁ ÿÿ&ÿÿ+ ÿÿ ÿÿ3 ÿÿ ÿÿ ÿÿ8 ÿÿ ÿÿ ÿÿ ÿÿP ÿÿ ÿÿE ÿÿ ÿÿ ÿÿ	 ÿÿ ÿÿG ÿÿ# ÿÿ ÿÿ& ÿÿR ÿÿJ ÿÿ' ÿÿN ÿÿ9 ÿÿ: ÿÿ ÿÿ  ÿÿ+ ÿÿ ÿÿ. ÿÿ5 ÿÿ2 ÿÿ ÿÿ ÿÿ4 ÿÿ, ÿÿ1 ÿÿ? ÿÿ/ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ  ÿÿ! ÿÿ" ÿÿ ÿÿ- ÿÿI ÿÿQ ÿÿF ÿÿ7 ÿÿO ÿÿ; ÿÿ ÿÿL ÿÿ$ ÿÿ% ÿÿ6 ÿÿ ÿÿ ÿÿ0 ÿÿ) ÿÿ* ÿÿ ÿÿ ÿÿ ÿÿ ÿÿK ÿÿ ÿÿS ÿÿ ÿÿ
 ÿÿM 
 < ÿÿ ÿÿE ÿÿ- ÿÿD ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ
 ÿÿ ÿÿ
 ÿÿ ÿÿ ÿÿ	 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ( ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ- ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ( ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ	 ÿÿ ÿÿ ÿÿ ÿÿ( ÿÿ ÿÿ( ÿÿ ÿÿ( ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ 
  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿP ÿÿ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                               <  _ 4    •Z        Ð         @            €                                                                              ?          €È¾            h¸ @           Ô· @           ¸ @           5¸ @           È @          ™¸ @          ° B           Ã¸ @   	   	     ú¸ @  
   
     •Z @        *˜          t¹ @       z» @       I» @        J 	         º @       Nº @       ¹¹ @       è¹ @       ŠG          ‹¾ @       Q¾ B        ¾ @       C¹ @       t¼ @       	½ @       ‚•         |_     &      ³º @ '      …º @ (      ‰½ @ )      =½ @ *       ¼ @ +  !    >¼ @ ,  "    ¬» @ -  #    Ú» @ .  $    Ö¼ @ /  %    ¤¼ @ 0  &    » @ 1  '    âº @ 2  (    ×½ @  3  )    ò¾   3  *    :ì     4  +    rU     5  +    UU     6  +    íX     7  ,    U     8  ,    r•    9  ,    @U     :  ,    dâ     ;  ,    vâ     <  ,    d     ?  -    >d     L  .    Dd     T  /    ³·    [ 4 1   ¨·    [ 4 1   E°    ] 4 2   Û¤    ^ 4 3   ‡    _ 4 4   y™ wÿÿ  ÿÿ ÿÿrÿÿY ÿÿ+ ÿÿ: ÿÿM ÿÿ_ ÿÿy™ y™ j ÿÿt ÿÿ     ÿÿ„ ÿÿ„ ÿÿã ÿÿÜ ÿÿAÿÿNÿÿ¢ ÿÿ¬ ÿÿ ÿÿ— ÿÿt ÿÿkÿÿdÿÿ]ÿÿ{ ÿÿÿÿ4ÿÿ+ ÿÿ: ÿÿM ÿÿ{ ÿÿÈ ÿÿÕ ÿÿAÿÿNÿÿTÿÿ¾ ÿÿµ ÿÿNÿÿAÿÿÿÿÿÿì ÿÿö ÿÿ)ÿÿ ÿÿÕ ÿÿÈ ÿÿTÿÿ  ÿÿ ÿÿ ÿÿdÿÿ ÿÿ ÿÿ ÿÿ ÿÿ{ÿÿt ÿÿÿÿ4ÿÿ+ ÿÿ: ÿÿM ÿÿ ÿÿ— ÿÿµ ÿÿÈ ÿÿÜ ÿÿì ÿÿö ÿÿ ÿÿTÿÿwÿÿ¢ ÿÿ¬ ÿÿ¾ ÿÿÕ ÿÿã ÿÿÿÿÿÿ)ÿÿ+ ÿÿ: ÿÿM ÿÿY ÿÿi ÿÿj ÿÿrÿÿdÿÿkÿÿ]
 ]ÿÿ ™  ™ + ÿÿ7 ÿÿ ÿÿ7 ÿÿ5 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ4 ÿÿ ÿÿ	 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ6 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ( ÿÿ' ÿÿ ÿÿ ÿÿ# ÿÿ$ ÿÿ! ÿÿ" ÿÿ ÿÿ& ÿÿ% ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ) ÿÿ; ÿÿ ÿÿ. ÿÿ9 ÿÿ ÿÿ ÿÿ  ÿÿ* ÿÿ
  : 
 7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ
 ™ 
 ™ 7 ÿÿ7 ÿÿ7  7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿÿ7 ÿ      '  7      eZ        Ð         @            €                                                                              á          @#Â            û¿ @           :¿ @           y¿ @           ¹¿ @           È @          -À @          ° B           XÀ @   	   	     À @  
   
     eZ @        *˜           ÚÀ @        ŠG           ªÁ @        èÁ B         eÁ @        ‚•          |_            s·           NÂ           Á @         ¿           …Â          :ì            rU            UU             íX     !       U     "       r•    #       @U     $       dâ     %       vâ     &       d     '       >d     (       ³·    )      E°    +      Û¤    1      ‡    7       · ‘  ÿÿ* ÿÿµ ÿÿ§ ÿÿ^ ÿÿ3 ÿÿ> ÿÿQ ÿÿd ÿÿ· ‘ · ‘ o ÿÿy ÿÿ*  ‚ ÿÿ‚ ÿÿy ÿÿ™ ÿÿ  ÿÿ’ ÿÿ> ÿÿQ ÿÿ¯ ÿÿ¯ ÿÿ‰ ÿÿ3 ÿÿ> ÿÿQ ÿÿ^ ÿÿn ÿÿo ÿÿ§ ÿÿ* ÿÿ ÿÿ ÿÿ  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ¹ ÿÿy ÿÿ¯ ÿÿ* ÿÿ™ ÿÿ  ÿÿ3 
 > 
 Q 
 ‰ 
 ’ 
 µ 
 3 ÿÿ> ÿÿQ ÿÿ‰ ÿÿ’ ÿÿµ ÿÿ ‘  ‘  ÿÿ ÿÿ  ÿÿ# ÿÿ ÿÿ& ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ! ÿÿ ÿÿ	 ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ$ ÿÿ ÿÿ ÿÿ ÿÿ  ÿÿ" ÿÿ ÿÿ ÿÿ
  % 
  ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ
 ‘ 
 ‘  ÿÿ ÿÿ   ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ ÿÿ 
  ÿÿ# ÿÿ ÿÿ                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  f  œ c    þT       0‘         @0         @ €                                               0                             †þ        @Æ            dÆ            þÆ @           /Ç @           ÏÇ @           Ç @           ÔÆ @           È @  	   	     ° B   
   
     EÈ @           |È @          ºÈ @         Y @        GÊ @        à          ô´        *˜          òÏ         øÈ @       ÎÍ @       þT B        ¢Ì @       ÝÅ 	          ±Å           Ä          SÃ          ÖÃ          ªÃ          jÄ  	        !Ë @	 !      CÅ  
 !       ýÃ   !  !    3Ä   !  "    ×Ä   !  #    Ì @ "  $    Å   "  %    ~Ã   "  &    ,Ã   "  '    zÅ   "  (    4Î @ #  )    WÍ @ $  *    -Ð   %  +    jÉ @ &  ,    ÒÌ @ '  -    Í @ (  .    ”Í B  )  /    ®É @ *  0    õÉ @ +  1    ªÏ   ,  2    ^Ï   -  3    9É @ .  4    ‚•   .  4    1Ï   /  5    |_     E  6    nÌ @ F  7    	Æ 
   F  8    èÂ    H  :    OË @ I  ;    †Ð   J  <    |Ë @ K  =    ªË @ L  >    ;Ì @ M  ?    ×Ë @ N  @    J“    N  @    ¬Â    Q  B    ØÂ    S  D    ÄÆ    T  E    üÂ    W  G    rÎ @ X  H    ÍÎ  
 Y  I    Ã    b c K   »Â    c c L   ûÎ  
 d c M   ‘Ê @ e c N   ÕÊ @ f c O   ¿    g c P   dP    h c Q   :ì     i c R   rU     j c R   UU     k c R   íX     l c S   U     m c S   r•    n c S   @U     o c S   dâ     p c S   vâ     q c S   d     w c T   Dd     x c U   ³·    y c V   E°    { c W   S     | c W   Pm    } c X   Ÿì  	   ‡ c Y   •_     Š c Z   „_     Œ c [   Êk    Ž c ]   Û¤    ‘ c ^   ‡    ” c _   ¹¡    • c `   j    — c b   iP    › c c   {v    œ c c   ÿÿü µ ÿÿ¥ ÿÿ ÿÿN ÿÿb ÿÿ\ ÿÿv ÿÿ• ÿÿg ÿÿo ÿÿ… ÿÿŒ ÿÿU ÿÿG ÿÿž ÿÿ· ÿÿ ÿÿ@ ÿÿÿÿÃ ÿÿÍ ÿÿå ÿÿï ÿÿÿÿ
ÿÿÝ ÿÿ ÿÿü ü )ÿÿ3ÿÿÿÿ<ÿÿ@  ÿÿ' ÿÿ@ ÿÿÓ ÿÿ3ÿÿ<ÿÿHÿÿaÿÿÿÿN ÿÿuÿÿÿÿêÿÿHÿÿcÿÿñÿÿÍ ÿÿ­ÿÿ×ÿÿwÿÿ:ÿÿõÿÿVÿÿÿÿ-ÿÿGÿÿuÿÿ†ÿÿ×ÿÿÌÿÿOÿÿ¾ÿÿg ÿÿ ÿÿ… ÿÿŒ ÿÿ¥ ÿÿOÿÿ­ÿÿ´ÿÿÀÿÿÇÿÿÎÿÿ×ÿÿàÿÿêÿÿñÿÿÿÿ-ÿÿbÿÿ¶ÿÿÌÿÿ×ÿÿêÿÿêÿÿ* ÿÿõÿÿ´ÿÿúÿÿÀÿÿÇÿÿàÿÿÎÿÿ% ÿÿÃ ÿÿÍ ÿÿ( ÿÿb ÿÿÑ ÿÿ, ÿÿÌÿÿ×ÿÿ’ÿÿžÿÿÃ ÿÿÍ ÿÿÝ ÿÿå ÿÿï ÿÿ
ÿÿÿÿ)ÿÿ’ÿÿ& ÿÿ¶ÿÿ—ÿÿ¦ÿÿU ÿÿÿÿ@ ÿÿ" ÿÿ" ÿÿGÿÿ" ÿÿ" ÿÿ" ÿÿ" ÿÿÿÿo ÿÿ3ÿÿ<ÿÿêÿÿêÿÿÿÿõÿÿ\ ÿÿ:ÿÿGÿÿ ÿÿÿÿÃ ÿÿÍ ÿÿï ÿÿ
ÿÿÿÿ†ÿÿ¦ÿÿ-ÿÿcÿÿ’ÿÿv ÿÿ
ÿÿwÿÿ
ÿÿwÿÿ+ ÿÿž ÿÿï 
 V
 —
 ï ÿÿVÿÿ—ÿÿï ÿÿ) ÿÿ• ÿÿG ÿÿ†ÿÿ¦ÿÿ’ÿÿ  ÿÿL ÿÿ
 ÿÿ    7 ÿÿ@ ÿÿG ÿÿ ÿÿA ÿÿc ÿÿ8 ÿÿ_ ÿÿC ÿÿM ÿÿF ÿÿ ÿÿd ÿÿ% ÿÿ ÿÿ ÿÿK ÿÿ$ ÿÿX ÿÿ ÿÿA ÿÿ ÿÿ5 ÿÿ ÿÿV ÿÿ  ÿÿ] ÿÿ ÿÿ ÿÿ! ÿÿ# ÿÿc ÿÿ ÿÿ_ ÿÿ& ÿÿ ÿÿ ÿÿF ÿÿ@ ÿÿ\ ÿÿ  ÿÿ ÿÿB ÿÿ ÿÿ ÿÿa ÿÿb ÿÿ ÿÿ^ ÿÿ ÿÿ[ ÿÿ ÿÿ ÿÿ	 ÿÿ
 ÿÿ ÿÿ ÿÿ2 ÿÿ* ÿÿ. ÿÿ/ ÿÿ ÿÿI ÿÿJ ÿÿ ÿÿ9 ÿÿ; ÿÿ< ÿÿ> ÿÿ" ÿÿ= ÿÿ6 ÿÿ ÿÿ+ ÿÿ, ÿÿY ÿ