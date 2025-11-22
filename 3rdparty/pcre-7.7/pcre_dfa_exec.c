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


/* This module contains the external function pcre_dfa_exec(), which is an
alternative matching function that uses a sort of DFA algorithm (not a true
FSM). This is NOT Perl- compatible, but it has advantages in certain
applications. */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define NLBLOCK md             /* Block containing newline information */
#define PSSTART start_subject  /* Field containing processed string start */
#define PSEND   end_subject    /* Field containing processed string end */

#include "pcre_internal.h"


/* For use to indent debugging output */

#define SP "                   "



/*************************************************
*      Code parameters and static tables         *
*************************************************/

/* These are offsets that are used to turn the OP_TYPESTAR and friends opcodes
into others, under special conditions. A gap of 20 between the blocks should be
enough. The resulting opcodes don't have to be less than 256 because they are
never stored, so we push them well clear of the normal opcodes. */

#define OP_PROP_EXTRA       300
#define OP_EXTUNI_EXTRA     320
#define OP_ANYNL_EXTRA      340
#define OP_HSPACE_EXTRA     360
#define OP_VSPACE_EXTRA     380


/* This table identifies those opcodes that are followed immediately by a
character that is to be tested in some way. This makes is possible to
centralize the loading of these characters. In the case of Type * etc, the
"character" is the opcode for \D, \d, \S, \s, \W, or \w, which will always be a
small value. ***NOTE*** If the start of this table is modified, the two tables
that follow must also be modified. */

static const uschar coptable[] = {
  0,                             /* End                                    */
  0, 0, 0, 0, 0,                 /* \A, \G, \K, \B, \b                     */
  0, 0, 0, 0, 0, 0,              /* \D, \d, \S, \s, \W, \w                 */
  0, 0, 0,                       /* Any, AllAny, Anybyte                   */
  0, 0, 0,                       /* NOTPROP, PROP, EXTUNI                  */
  0, 0, 0, 0, 0,                 /* \R, \H, \h, \V, \v                     */
  0, 0, 0, 0, 0,                 /* \Z, \z, Opt, ^, $                      */
  1,                             /* Char                                   */
  1,                             /* Charnc                                 */
  1,                             /* not                                    */
  /* Positive single-char repeats                                          */
  1, 1, 1, 1, 1, 1,              /* *, *?, +, +?, ?, ??                    */
  3, 3, 3,                       /* upto, minupto, exact                   */
  1, 1, 1, 3,                    /* *+, ++, ?+, upto+                      */
  /* Negative single-char repeats - only for chars < 256                   */
  1, 1, 1, 1, 1, 1,              /* NOT *, *?, +, +?, ?, ??                */
  3, 3, 3,                       /* NOT upto, minupto, exact               */
  1, 1, 1, 3,                    /* NOT *+, ++, ?+, updo+                  */
  /* Positive type repeats                                                 */
  1, 1, 1, 1, 1, 1,              /* Type *, *?, +, +?, ?, ??               */
  3, 3, 3,                       /* Type upto, minupto, exact              */
  1, 1, 1, 3,                    /* Type *+, ++, ?+, upto+                 */
  /* Character class & ref repeats                                         */
  0, 0, 0, 0, 0, 0,              /* *, *?, +, +?, ?, ??                    */
  0, 0,                          /* CRRANGE, CRMINRANGE                    */
  0,                             /* CLASS                                  */
  0,                             /* NCLASS                                 */
  0,                             /* XCLASS - variable length               */
  0,                             /* REF                                    */
  0,                             /* RECURSE                                */
  0,                             /* CALLOUT                                */
  0,                             /* Alt                                    */
  0,                             /* Ket                                    */
  0,                             /* KetRmax                                */
  0,                             /* KetRmin                                */
  0,                             /* Assert                                 */
  0,                             /* Assert not                             */
  0,                             /* Assert behind                          */
  0,                             /* Assert behind not                      */
  0,                             /* Reverse                                */
  0, 0, 0, 0,                    /* ONCE, BRA, CBRA, COND                  */
  0, 0, 0,                       /* SBRA, SCBRA, SCOND                     */
  0,                             /* CREF                                   */
  0,                             /* RREF                                   */
  0,                             /* DEF                                    */
  0, 0,                          /* BRAZERO, BRAMINZERO                    */
  0, 0, 0, 0,                    /* PRUNE, SKIP, THEN, COMMIT              */
  0, 0, 0                        /* FAIL, ACCEPT, SKIPZERO                 */
};

/* These 2 tables allow for compact code for testing for \D, \d, \S, \s, \W,
and \w */

static const uschar toptable1[] = {
  0, 0, 0, 0, 0, 0,
  ctype_digit, ctype_digit,
  ctype_space, ctype_space,
  ctype_word,  ctype_word,
  0, 0                            /* OP_ANY, OP_ALLANY */
};

static const uschar toptable2[] = {
  0, 0, 0, 0, 0, 0,
  ctype_digit, 0,
  ctype_space, 0,
  ctype_word,  0,
  1, 1                            /* OP_ANY, OP_ALLANY */
};


/* Structure for holding data about a particular state, which is in effect the
current data for an active path through the match tree. It must consist
entirely of ints because the working vector we are passed, and which we put
these structures in, is a vector of ints. */

typedef struct stateblock {
  int offset;                     /* Offset to opcode */
  int count;                      /* Count for repeats */
  int ims;                        /* ims flag bits */
  int data;                       /* Some use extra data */
} stateblock;

#define INTS_PER_STATEBLOCK  (sizeof(stateblock)/sizeof(int))


#ifdef DEBUG
/*************************************************
*             Print character string             *
*************************************************/

/* Character string printing function for debugging.

Arguments:
  p            points to string
  length       number of bytes
  f            where to print

Returns:       nothing
*/

static void
pchars(unsigned char *p, int length, FILE *f)
{
int c;
while (length-- > 0)
  {
  if (isprint(c = *(p++)))
    fprintf(f, "%c", c);
  else
    fprintf(f, "\\x%02x", c);
  }
}
#endif



/*************************************************
*    Execute a Regular Expression - DFA engine   *
*************************************************/

/* This internal function applies a compiled pattern to a subject string,
starting at a given point, using a DFA engine. This function is called from the
external one, possibly multiple times if the pattern is not anchored. The
function calls itself recursively for some kinds of subpattern.

Arguments:
  md                the match_data block with fixed information
  this_start_code   the opening bracket of this subexpression's code
  current_subject   where we currently are in the subject string
  start_offset      start offset in the subject string
  offsets           vector to contain the matching string offsets
  offsetcount       size of same
  workspace         vector of workspace
  wscount           size of same
  ims               the current ims flags
  rlevel            function call recursion level
  recursing         regex recursive call level

Returns:            > 0 => number of match offset pairs placed in offsets
                    = 0 => offsets overflowed; longest matches are present
                     -1 => failed to match
                   < -1 => some kind of unexpected problem

The following macros are used for adding states to the two state vectors (one
for the current character, one for the following character). */

#define ADD_ACTIVE(x,y) \
  if (active_count++ < wscount) \
    { \
    next_active_state->offset = (x); \
    next_active_state->count  = (y); \
    next_active_state->ims    = ims; \
    next_active_state++; \
    DPRINTF(("%.*sADD_ACTIVE(%d,%d)\n", rlevel*2-2, SP, (x), (y))); \
    } \
  else return PCRE_ERROR_DFA_WSSIZE

#define ADD_ACTIVE_DATA(x,y,z) \
  if (active_count++ < wscount) \
    { \
    next_active_state->offset = (x); \
    next_active_state->count  = (y); \
    next_active_state->ims    = ims; \
    next_active_state->data   = (z); \
    next_active_state++; \
    DPRINTF(("%.*sADD_ACTIVE_DATA(%d,%d,%d)\n", rlevel*2-2, SP, (x), (y), (z))); \
    } \
  else return PCRE_ERROR_DFA_WSSIZE

#define ADD_NEW(x,y) \
  if (new_count++ < wscount) \
    { \
    next_new_state->offset = (x); \
    next_new_state->count  = (y); \
    next_new_state->ims    = ims; \
    next_new_state++; \
    DPRINTF(("%.*sADD_NEW(%d,%d)\n", rlevel*2-2, SP, (x), (y))); \
    } \
  else return PCRE_ERROR_DFA_WSSIZE

#define ADD_NEW_DATA(x,y,z) \
  if (new_count++ < wscount) \
    { \
    next_new_state->offset = (x); \
    next_new_state->count  = (y); \
    next_new_state->ims    = ims; \
    next_new_state->data   = (z); \
    next_new_state++; \
    DPRINTF(("%.*sADD_NEW_DATA(%d,%d,%d)\n", rlevel*2-2, SP, (x), (y), (z))); \
    } \
  else return PCRE_ERROR_DFA_WSSIZE

/* And now, here is the code */

static int
internal_dfa_exec(
  dfa_match_data *md,
  const uschar *this_start_code,
  const uschar *current_subject,
  int start_offset,
  int *offsets,
  int offsetcount,
  int *workspace,
  int wscount,
  int ims,
  int  rlevel,
  int  recursing)
{
stateblock *active_states, *new_states, *temp_states;
stateblock *next_active_state, *next_new_state;

const uschar *ctypes, *lcc, *fcc;
const uschar *ptr;
const uschar *end_code, *first_op;

int active_count, new_count, match_count;

/* Some fields in the md block are frequently referenced, so we load them into
independent variables in the hope that this will perform better. */

const uschar *start_subject = md->start_subject;
const uschar *end_subject = md->end_subject;
const uschar *start_code = md->start_code;

#ifdef SUPPORT_UTF8
BOOL utf8 = (md->poptions & PCRE_UTF8) != 0;
#else
BOOL utf8 = FALSE;
#endif

rlevel++;
offsetcount &= (-2);

wscount -= 2;
wscount = (wscount - (wscount % (INTS_PER_STATEBLOCK * 2))) /
          (2 * INTS_PER_STATEBLOCK);

DPRINTF(("\n%.*s---------------------\n"
  "%.*sCall to internal_dfa_exec f=%d r=%d\n",
  rlevel*2-2, SP, rlevel*2-2, SP, rlevel, recursing));

ctypes = md->tables + ctypes_offset;
lcc = md->tables + lcc_offset;
fcc = md->tables + fcc_offset;

match_count = PCRE_ERROR_NOMATCH;   /* A negative number */

active_states = (stateblock *)(workspace + 2);
next_new_state = new_states = active_states + wscount;
new_count = 0;

first_op = this_start_code + 1 + LINK_SIZE +
  ((*this_start_code == OP_CBRA || *this_start_code == OP_SCBRA)? 2:0);

/* The first thing in any (sub) pattern is a bracket of some sort. Push all
the alternative states onto the list, and find out where the end is. This
makes is possible to use this function recursively, when we want to stop at a
matching internal ket rather than at the end.

If the first opcode in the first alternative is OP_REVERSE, we are dealing with
a backward assertion. In that case, we have to find out the maximum amount to
move back, and set up each alternative appropriately. */

if (*first_op == OP_REVERSE)
  {
  int max_back = 0;
  int gone_back;

  end_code = this_start_code;
  do
    {
    int back = GET(end_code, 2+LINK_SIZE);
    if (back > max_back) max_back = back;
    end_code += GET(end_code, 1);
    }
  while (*end_code == OP_ALT);

  /* If we can't go back the amount required for the longest lookbehind
  pattern, go back as far as we can; some alternatives may still be viable. */

#ifdef SUPPORT_UTF8
  /* In character mode we have to step back character by character */

  if (utf8)
    {
    for (gone_back = 0; gone_back < max_back; gone_back++)
      {
      if (current_subject <= start_subject) break;
      current_subject--;
      while (current_subject > start_subject &&
             (*current_subject & 0xc0) == 0x80)
        current_subject--;
      }
    }
  else
#endif

  /* In byte-mode we can do this quickly. */

    {
    gone_back = (current_subject - max_back < start_subject)?
      current_subject - start_subject : max_back;
    current_subject -= gone_back;
    }

  /* Now we can process the individual branches. */

  end_code = this_start_code;
  do
    {
    int back = GET(end_code, 2+LINK_SIZE);
    if (back <= gone_back)
      {
      int bstate = end_code - start_code + 2 + 2*LINK_SIZE;
      ADD_NEW_DATA(-bstate, 0, gone_back - back);
      }
    end_code += GET(end_code, 1);
    }
  while (*end_code == OP_ALT);
 }

/* This is the code for a "normal" subpattern (not a backward assertion). The
start of a whole pattern is always one of these. If we are at the top level,
we may be asked to restart matching from the same point that we reached for a
previous partial match. We still have to scan through the top-level branches to
find the end state. */

else
  {
  end_code = this_start_code;

  /* Restarting */

  if (rlevel == 1 && (md->moptions & PCRE_DFA_RESTART) != 0)
    {
    do { end_code += GET(end_code, 1); } while (*end_code == OP_ALT);
    new_count = workspace[1];
    if (!workspace[0])
      memcpy(new_states, active_states, new_count * sizeof(stateblock));
    }

  /* Not restarting */

  else
    {
    int length = 1 + LINK_SIZE +
      ((*this_start_code == OP_CBRA || *this_start_code == OP_SCBRA)? 2:0);
    do
      {
      ADD_NEW(end_code - start_code + length, 0);
      end_code += GET(end_code, 1);
      length = 1 + LINK_SIZE;
      }
    while (*end_code == OP_ALT);
    }
  }

workspace[0] = 0;    /* Bit indicating which vector is current */

DPRINTF(("%.*sEnd state = %d\n", rlevel*2-2, SP, end_code - start_code));

/* Loop for scanning the subject */

ptr = current_subject;
for (;;)
  {
  int i, j;
  int clen, dlen;
  unsigned int c, d;

  /* Make the new state list into the active state list and empty the
  new state list. */

  temp_states = active_states;
  active_states = new_states;
  new_states = temp_states;
  active_count = new_count;
  new_count = 0;

  workspace[0] ^= 1;              /* Remember for the restarting feature */
  workspace[1] = active_count;

#ifdef DEBUG
  printf("%.*sNext character: rest of subject = \"", rlevel*2-2, SP);
  pchars((uschar *)ptr, strlen((char *)ptr), stdout);
  printf("\"\n");

  printf("%.*sActive states: ", rlevel*2-2, SP);
  for (i = 0; i < active_count; i++)
    printf("%d/%d ", active_states[i].offset, active_states[i].count);
  printf("\n");
#endif

  /* Set the pointers for adding new states */

  next_active_state = active_states + active_count;
  next_new_state = new_states;

  /* Load the current character from the subject outside the loop, as many
  different states may want to look at it, and we assume that at least one
  will. */

  if (ptr < end_subject)
    {
    clen = 1;        /* Number of bytes in the character */
#ifdef SUPPORT_UTF8
    if (utf8) { GETCHARLEN(c, ptr, clen); } else
#endif  /* SUPPORT_UTF8 */
    c = *ptr;
    }
  else
    {
    clen = 0;        /* This indicates the end of the subject */
    c = NOTACHAR;    /* This value should never actually be used */
    }

  /* Scan up the active states and act on each one. The result of an action
  may be to add more states to the currently active list (e.g. on hitting a
  parenthesis) or it may be to put states on the new list, for considering
  when we move the character pointer on. */

  for (i = 0; i < active_count; i++)
    {
    stateblock *current_state = active_states + i;
    const uschar *code;
    int state_offset = current_state->offset;
    int count, codevalue;
#ifdef SUPPORT_UCP
    int chartype, script;
#endif

#ifdef DEBUG
    printf ("%.*sProcessing state %d c=", rlevel*2-2, SP, state_offset);
    if (clen == 0) printf("EOL\n");
      else if (c > 32 && c < 127) printf("'%c'\n", c);
        else printf("0x%02x\n", c);
#endif

    /* This variable is referred to implicity in the ADD_xxx macros. */

    ims = current_state->ims;

    /* A negative offset is a special case meaning "hold off going to this
    (negated) state until the number of characters in the data field have
    been skipped". */

    if (state_offset < 0)
      {
      if (current_state->data > 0)
        {
        DPRINTF(("%.*sSkipping this character\n", rlevel*2-2, SP));
        ADD_NEW_DATA(state_offset, current_state->count,
          current_state->data - 1);
        continue;
        }
      else
        {
        current_state->offset = state_offset = -state_offset;
        }
      }

    /* Check for a duplicate state with the same count, and skip if found. */

    for (j = 0; j < i; j++)
      {
      if (active_states[j].offset == state_offset &&
          active_states[j].count == current_state->count)
        {
        DPRINTF(("%.*sDuplicate state: skipped\n", rlevel*2-2, SP));
        goto NEXT_ACTIVE_STATE;
        }
      }

    /* The state offset is the offset to the opcode */

    code = start_code + state_offset;
    codevalue = *code;

    /* If this opcode is followed by an inline character, load it. It is
    tempting to test for the presence of a subject character here, but that
    is wrong, because sometimes zero repetitions of the subject are
    permitted.

    We also use this mechanism for opcodes such as OP_TYPEPLUS that take an
    argument that is not a data character - but is always one byte long. We
    have to take special action to deal with  \P, \p, \H, \h, \V, \v and \X in
    this case. To keep the other cases fast, convert these ones to new opcodes.
    */

    if (coptable[codevalue] > 0)
      {
      dlen = 1;
#ifdef SUPPORT_UTF8
      if (utf8) { GETCHARLEN(d, (code + coptable[codevalue]), dlen); } else
#endif  /* SUPPORT_UTF8 */
      d = code[coptable[codevalue]];
      if (codevalue >= OP_TYPESTAR)
        {
        switch(d)
          {
          case OP_ANYBYTE: return PCRE_ERROR_DFA_UITEM;
          case OP_NOTPROP:
          case OP_PROP: codevalue += OP_PROP_EXTRA; break;
          case OP_ANYNL: codevalue += OP_ANYNL_EXTRA; break;
          case OP_EXTUNI: codevalue += OP_EXTUNI_EXTRA; break;
          case OP_NOT_HSPACE:
          case OP_HSPACE: codevalue += OP_HSPACE_EXTRA; break;
          case OP_NOT_VSPACE:
          case OP_VSPACE: codevalue += OP_VSPACE_EXTRA; break;
          default: break;
          }
        }
      }
    else
      {
      dlen = 0;         /* Not strictly necessary, but compilers moan */
      d = NOTACHAR;     /* if these variables are not set. */
      }


    /* Now process the individual opcodes */

    switch (codevalue)
      {

/* ========================================================================== */
      /* Reached a closing bracket. If not at the end of the pattern, carry
      on with the next opcode. Otherwise, unless we have an empty string and
      PCRE_NOTEMPTY is set, save the match data, shifting up all previous
      matches so we always have the longest first. */

      case OP_KET:
      case OP_KETRMIN:
      case OP_KETRMAX:
      if (code != end_code)
        {
        ADD_ACTIVE(state_offset + 1 + LINK_SIZE, 0);
        if (codevalue != OP_KET)
          {
          ADD_ACTIVE(state_offset - GET(code, 1), 0);
          }
        }
      else if (ptr > current_subject || (md->moptions & PCRE_NOTEMPTY) == 0)
        {
        if (match_count < 0) match_count = (offsetcount >= 2)? 1 : 0;
          else if (match_count > 0 && ++match_count * 2 >= offsetcount)
            match_count = 0;
        count = ((match_count == 0)? offsetcount : match_count * 2) - 2;
        if (count > 0) memmove(offsets + 2, offsets, count * sizeof(int));
        if (offsetcount >= 2)
          {
          offsets[0] = current_subject - start_subject;
          offsets[1] = ptr - start_subject;
          DPRINTF(("%.*sSet matched string = \"%.*s\"\n", rlevel*2-2, SP,
            offsets[1] - offsets[0], current_subject));
          }
        if ((md->moptions & PCRE_DFA_SHORTEST) != 0)
          {
          DPRINTF(("%.*sEnd of internal_dfa_exec %d: returning %d\n"
            "%.*s---------------------\n\n", rlevel*2-2, SP, rlevel,
            match_count, rlevel*2-2, SP));
          return match_count;
          }
        }
      break;

/* ========================================================================== */
      /* These opcodes add to the current list of states without looking
      at the current character. */

      /*-----------------------------------------------------------------*/
      case OP_ALT:
      do { code += GET(code, 1); } while (*code == OP_ALT);
      ADD_ACTIVE(code - start_code, 0);
      break;

      /*-----------------------------------------------------------------*/
      case OP_BRA:
      case OP_SBRA:
      do
        {
        ADD_ACTIVE(code - start_code + 1 + LINK_SIZE, 0);
        code += GET(code, 1);
        }
      while (*code == OP_ALT);
      break;

      /*-----------------------------------------------------------------*/
      case OP_CBRA:
      case OP_SCBRA:
      ADD_ACTIVE(code - start_code + 3 + LINK_SIZE,  0);
      code += GET(code, 1);
      while (*code == OP_ALT)
        {
        ADD_ACTIVE(code - start_code + 1 + LINK_SIZE,  0);
        code += GET(code, 1);
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_BRAZERO:
      case OP_BRAMINZERO:
      ADD_ACTIVE(state_offset + 1, 0);
      code += 1 + GET(code, 2);
      while (*code == OP_ALT) code += GET(code, 1);
      ADD_ACTIVE(code - start_code + 1 + LINK_SIZE, 0);
      break;

      /*-----------------------------------------------------------------*/
      case OP_SKIPZERO:
      code += 1 + GET(code, 2);
      while (*code == OP_ALT) code += GET(code, 1);
      ADD_ACTIVE(code - start_code + 1 + LINK_SIZE, 0);
      break;

      /*-----------------------------------------------------------------*/
      case OP_CIRC:
      if ((ptr == start_subject && (md->moptions & PCRE_NOTBOL) == 0) ||
          ((ims & PCRE_MULTILINE) != 0 &&
            ptr != end_subject &&
            WAS_NEWLINE(ptr)))
        { ADD_ACTIVE(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_EOD:
      if (ptr >= end_subject) { ADD_ACTIVE(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_OPT:
      ims = code[1];
      ADD_ACTIVE(state_offset + 2, 0);
      break;

      /*-----------------------------------------------------------------*/
      case OP_SOD:
      if (ptr == start_subject) { ADD_ACTIVE(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_SOM:
      if (ptr == start_subject + start_offset) { ADD_ACTIVE(state_offset + 1, 0); }
      break;


/* ========================================================================== */
      /* These opcodes inspect the next subject character, and sometimes
      the previous one as well, but do not have an argument. The variable
      clen contains the length of the current character and is zero if we are
      at the end of the subject. */

      /*-----------------------------------------------------------------*/
      case OP_ANY:
      if (clen > 0 && !IS_NEWLINE(ptr))
        { ADD_NEW(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_ALLANY:
      if (clen > 0)
        { ADD_NEW(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_EODN:
      if (clen == 0 || (IS_NEWLINE(ptr) && ptr == end_subject - md->nllen))
        { ADD_ACTIVE(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_DOLL:
      if ((md->moptions & PCRE_NOTEOL) == 0)
        {
        if (clen == 0 ||
            (IS_NEWLINE(ptr) &&
               ((ims & PCRE_MULTILINE) != 0 || ptr == end_subject - md->nllen)
            ))
          { ADD_ACTIVE(state_offset + 1, 0); }
        }
      else if ((ims & PCRE_MULTILINE) != 0 && IS_NEWLINE(ptr))
        { ADD_ACTIVE(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/

      case OP_DIGIT:
      case OP_WHITESPACE:
      case OP_WORDCHAR:
      if (clen > 0 && c < 256 &&
            ((ctypes[c] & toptable1[codevalue]) ^ toptable2[codevalue]) != 0)
        { ADD_NEW(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_NOT_DIGIT:
      case OP_NOT_WHITESPACE:
      case OP_NOT_WORDCHAR:
      if (clen > 0 && (c >= 256 ||
            ((ctypes[c] & toptable1[codevalue]) ^ toptable2[codevalue]) != 0))
        { ADD_NEW(state_offset + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_WORD_BOUNDARY:
      case OP_NOT_WORD_BOUNDARY:
        {
        int left_word, right_word;

        if (ptr > start_subject)
          {
          const uschar *temp = ptr - 1;
#ifdef SUPPORT_UTF8
          if (utf8) BACKCHAR(temp);
#endif
          GETCHARTEST(d, temp);
          left_word = d < 256 && (ctypes[d] & ctype_word) != 0;
          }
        else left_word = 0;

        if (clen > 0) right_word = c < 256 && (ctypes[c] & ctype_word) != 0;
          else right_word = 0;

        if ((left_word == right_word) == (codevalue == OP_NOT_WORD_BOUNDARY))
          { ADD_ACTIVE(state_offset + 1, 0); }
        }
      break;


      /*-----------------------------------------------------------------*/
      /* Check the next character by Unicode property. We will get here only
      if the support is in the binary; otherwise a compile-time error occurs.
      */

#ifdef SUPPORT_UCP
      case OP_PROP:
      case OP_NOTPROP:
      if (clen > 0)
        {
        BOOL OK;
        int category = _pcre_ucp_findprop(c, &chartype, &script);
        switch(code[1])
          {
          case PT_ANY:
          OK = TRUE;
          break;

          case PT_LAMP:
          OK = chartype == ucp_Lu || chartype == ucp_Ll || chartype == ucp_Lt;
          break;

          case PT_GC:
          OK = category == code[2];
          break;

          case PT_PC:
          OK = chartype == code[2];
          break;

          case PT_SC:
          OK = script == code[2];
          break;

          /* Should never occur, but keep compilers from grumbling. */

          default:
          OK = codevalue != OP_PROP;
          break;
          }

        if (OK == (codevalue == OP_PROP)) { ADD_NEW(state_offset + 3, 0); }
        }
      break;
#endif



/* ========================================================================== */
      /* These opcodes likewise inspect the subject character, but have an
      argument that is not a data character. It is one of these opcodes:
      OP_ANY, OP_ALLANY, OP_DIGIT, OP_NOT_DIGIT, OP_WHITESPACE, OP_NOT_SPACE,
      OP_WORDCHAR, OP_NOT_WORDCHAR. The value is loaded into d. */

      case OP_TYPEPLUS:
      case OP_TYPEMINPLUS:
      case OP_TYPEPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 2, 0); }
      if (clen > 0)
        {
        if ((c >= 256 && d != OP_DIGIT && d != OP_WHITESPACE && d != OP_WORDCHAR) ||
            (c < 256 &&
              (d != OP_ANY || !IS_NEWLINE(ptr)) &&
              ((ctypes[c] & toptable1[d]) ^ toptable2[d]) != 0))
          {
          if (count > 0 && codevalue == OP_TYPEPOSPLUS)
            {
            active_count--;            /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW(state_offset, count);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_TYPEQUERY:
      case OP_TYPEMINQUERY:
      case OP_TYPEPOSQUERY:
      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0)
        {
        if ((c >= 256 && d != OP_DIGIT && d != OP_WHITESPACE && d != OP_WORDCHAR) ||
            (c < 256 &&
              (d != OP_ANY || !IS_NEWLINE(ptr)) &&
              ((ctypes[c] & toptable1[d]) ^ toptable2[d]) != 0))
          {
          if (codevalue == OP_TYPEPOSQUERY)
            {
            active_count--;            /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW(state_offset + 2, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_TYPESTAR:
      case OP_TYPEMINSTAR:
      case OP_TYPEPOSSTAR:
      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0)
        {
        if ((c >= 256 && d != OP_DIGIT && d != OP_WHITESPACE && d != OP_WORDCHAR) ||
            (c < 256 &&
              (d != OP_ANY || !IS_NEWLINE(ptr)) &&
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
        int ncount = 0;
        if (count > 0 && codevalue == OP_EXTUNI_EXTRA + OP_TYPEPOSPLUS)
          {
          active_count--;           /* Remove non-match possibility */
          next_active_state--;
          }
        while (nptr < end_subject)
          {
          int nd;
          int ndlen = 1;
          GETCHARLEN(nd, nptr, ndlen);
          if (_pcre_ucp_findprop(nd, &chartype, &script) != ucp_M) break;
          ncount++;
          nptr += ndlen;
          }
        count++;
        ADD_NEW_DATA(-state_offset, count, ncount);
        }
      break;
#endif

      /*-----------------------------------------------------------------*/
      case OP_ANYNL_EXTRA + OP_TYPEPLUS:
      case OP_ANYNL_EXTRA + OP_TYPEMINPLUS:
      case OP_ANYNL_EXTRA + OP_TYPEPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 2, 0); }
      if (clen > 0)
        {
        int ncount = 0;
        switch (c)
          {
          case 0x000b:
          case 0x000c:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          if ((md->moptions & PCRE_BSR_ANYCRLF) != 0) break;
          goto ANYNL01;

          case 0x000d:
          if (ptr + 1 < end_subject && ptr[1] == 0x0a) ncount = 1;
          /* Fall through */

          ANYNL01:
          case 0x000a:
          if (count > 0 && codevalue == OP_ANYNL_EXTRA + OP_TYPEPOSPLUS)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW_DATA(-state_offset, count, ncount);
          break;

          default:
          break;
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_VSPACE_EXTRA + OP_TYPEPLUS:
      case OP_VSPACE_EXTRA + OP_TYPEMINPLUS:
      case OP_VSPACE_EXTRA + OP_TYPEPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 2, 0); }
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x000a:
          case 0x000b:
          case 0x000c:
          case 0x000d:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          break;
          }

        if (OK == (d == OP_VSPACE))
          {
          if (count > 0 && codevalue == OP_VSPACE_EXTRA + OP_TYPEPOSPLUS)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW_DATA(-state_offset, count, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_HSPACE_EXTRA + OP_TYPEPLUS:
      case OP_HSPACE_EXTRA + OP_TYPEMINPLUS:
      case OP_HSPACE_EXTRA + OP_TYPEPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + 2, 0); }
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x09:      /* HT */
          case 0x20:      /* SPACE */
          case 0xa0:      /* NBSP */
          case 0x1680:    /* OGHAM SPACE MARK */
          case 0x180e:    /* MONGOLIAN VOWEL SEPARATOR */
          case 0x2000:    /* EN QUAD */
          case 0x2001:    /* EM QUAD */
          case 0x2002:    /* EN SPACE */
          case 0x2003:    /* EM SPACE */
          case 0x2004:    /* THREE-PER-EM SPACE */
          case 0x2005:    /* FOUR-PER-EM SPACE */
          case 0x2006:    /* SIX-PER-EM SPACE */
          case 0x2007:    /* FIGURE SPACE */
          case 0x2008:    /* PUNCTUATION SPACE */
          case 0x2009:    /* THIN SPACE */
          case 0x200A:    /* HAIR SPACE */
          case 0x202f:    /* NARROW NO-BREAK SPACE */
          case 0x205f:    /* MEDIUM MATHEMATICAL SPACE */
          case 0x3000:    /* IDEOGRAPHIC SPACE */
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          break;
          }

        if (OK == (d == OP_HSPACE))
          {
          if (count > 0 && codevalue == OP_HSPACE_EXTRA + OP_TYPEPOSPLUS)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW_DATA(-state_offset, count, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
#ifdef SUPPORT_UCP
      case OP_PROP_EXTRA + OP_TYPEQUERY:
      case OP_PROP_EXTRA + OP_TYPEMINQUERY:
      case OP_PROP_EXTRA + OP_TYPEPOSQUERY:
      count = 4;
      goto QS1;

      case OP_PROP_EXTRA + OP_TYPESTAR:
      case OP_PROP_EXTRA + OP_TYPEMINSTAR:
      case OP_PROP_EXTRA + OP_TYPEPOSSTAR:
      count = 0;

      QS1:

      ADD_ACTIVE(state_offset + 4, 0);
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
          if (codevalue == OP_PROP_EXTRA + OP_TYPEPOSSTAR ||
              codevalue == OP_PROP_EXTRA + OP_TYPEPOSQUERY)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW(state_offset + count, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_EXTUNI_EXTRA + OP_TYPEQUERY:
      case OP_EXTUNI_EXTRA + OP_TYPEMINQUERY:
      case OP_EXTUNI_EXTRA + OP_TYPEPOSQUERY:
      count = 2;
      goto QS2;

      case OP_EXTUNI_EXTRA + OP_TYPESTAR:
      case OP_EXTUNI_EXTRA + OP_TYPEMINSTAR:
      case OP_EXTUNI_EXTRA + OP_TYPEPOSSTAR:
      count = 0;

      QS2:

      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0 && _pcre_ucp_findprop(c, &chartype, &script) != ucp_M)
        {
        const uschar *nptr = ptr + clen;
        int ncount = 0;
        if (codevalue == OP_EXTUNI_EXTRA + OP_TYPEPOSSTAR ||
            codevalue == OP_EXTUNI_EXTRA + OP_TYPEPOSQUERY)
          {
          active_count--;           /* Remove non-match possibility */
          next_active_state--;
          }
        while (nptr < end_subject)
          {
          int nd;
          int ndlen = 1;
          GETCHARLEN(nd, nptr, ndlen);
          if (_pcre_ucp_findprop(nd, &chartype, &script) != ucp_M) break;
          ncount++;
          nptr += ndlen;
          }
        ADD_NEW_DATA(-(state_offset + count), 0, ncount);
        }
      break;
#endif

      /*-----------------------------------------------------------------*/
      case OP_ANYNL_EXTRA + OP_TYPEQUERY:
      case OP_ANYNL_EXTRA + OP_TYPEMINQUERY:
      case OP_ANYNL_EXTRA + OP_TYPEPOSQUERY:
      count = 2;
      goto QS3;

      case OP_ANYNL_EXTRA + OP_TYPESTAR:
      case OP_ANYNL_EXTRA + OP_TYPEMINSTAR:
      case OP_ANYNL_EXTRA + OP_TYPEPOSSTAR:
      count = 0;

      QS3:
      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0)
        {
        int ncount = 0;
        switch (c)
          {
          case 0x000b:
          case 0x000c:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          if ((md->moptions & PCRE_BSR_ANYCRLF) != 0) break;
          goto ANYNL02;

          case 0x000d:
          if (ptr + 1 < end_subject && ptr[1] == 0x0a) ncount = 1;
          /* Fall through */

          ANYNL02:
          case 0x000a:
          if (codevalue == OP_ANYNL_EXTRA + OP_TYPEPOSSTAR ||
              codevalue == OP_ANYNL_EXTRA + OP_TYPEPOSQUERY)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW_DATA(-(state_offset + count), 0, ncount);
          break;

          default:
          break;
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_VSPACE_EXTRA + OP_TYPEQUERY:
      case OP_VSPACE_EXTRA + OP_TYPEMINQUERY:
      case OP_VSPACE_EXTRA + OP_TYPEPOSQUERY:
      count = 2;
      goto QS4;

      case OP_VSPACE_EXTRA + OP_TYPESTAR:
      case OP_VSPACE_EXTRA + OP_TYPEMINSTAR:
      case OP_VSPACE_EXTRA + OP_TYPEPOSSTAR:
      count = 0;

      QS4:
      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x000a:
          case 0x000b:
          case 0x000c:
          case 0x000d:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          break;
          }
        if (OK == (d == OP_VSPACE))
          {
          if (codevalue == OP_VSPACE_EXTRA + OP_TYPEPOSSTAR ||
              codevalue == OP_VSPACE_EXTRA + OP_TYPEPOSQUERY)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW_DATA(-(state_offset + count), 0, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_HSPACE_EXTRA + OP_TYPEQUERY:
      case OP_HSPACE_EXTRA + OP_TYPEMINQUERY:
      case OP_HSPACE_EXTRA + OP_TYPEPOSQUERY:
      count = 2;
      goto QS5;

      case OP_HSPACE_EXTRA + OP_TYPESTAR:
      case OP_HSPACE_EXTRA + OP_TYPEMINSTAR:
      case OP_HSPACE_EXTRA + OP_TYPEPOSSTAR:
      count = 0;

      QS5:
      ADD_ACTIVE(state_offset + 2, 0);
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x09:      /* HT */
          case 0x20:      /* SPACE */
          case 0xa0:      /* NBSP */
          case 0x1680:    /* OGHAM SPACE MARK */
          case 0x180e:    /* MONGOLIAN VOWEL SEPARATOR */
          case 0x2000:    /* EN QUAD */
          case 0x2001:    /* EM QUAD */
          case 0x2002:    /* EN SPACE */
          case 0x2003:    /* EM SPACE */
          case 0x2004:    /* THREE-PER-EM SPACE */
          case 0x2005:    /* FOUR-PER-EM SPACE */
          case 0x2006:    /* SIX-PER-EM SPACE */
          case 0x2007:    /* FIGURE SPACE */
          case 0x2008:    /* PUNCTUATION SPACE */
          case 0x2009:    /* THIN SPACE */
          case 0x200A:    /* HAIR SPACE */
          case 0x202f:    /* NARROW NO-BREAK SPACE */
          case 0x205f:    /* MEDIUM MATHEMATICAL SPACE */
          case 0x3000:    /* IDEOGRAPHIC SPACE */
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          break;
          }

        if (OK == (d == OP_HSPACE))
          {
          if (codevalue == OP_HSPACE_EXTRA + OP_TYPEPOSSTAR ||
              codevalue == OP_HSPACE_EXTRA + OP_TYPEPOSQUERY)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW_DATA(-(state_offset + count), 0, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
#ifdef SUPPORT_UCP
      case OP_PROP_EXTRA + OP_TYPEEXACT:
      case OP_PROP_EXTRA + OP_TYPEUPTO:
      case OP_PROP_EXTRA + OP_TYPEMINUPTO:
      case OP_PROP_EXTRA + OP_TYPEPOSUPTO:
      if (codevalue != OP_PROP_EXTRA + OP_TYPEEXACT)
        { ADD_ACTIVE(state_offset + 6, 0); }
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        BOOL OK;
        int category = _pcre_ucp_findprop(c, &chartype, &script);
        switch(code[4])
          {
          case PT_ANY:
          OK = TRUE;
          break;

          case PT_LAMP:
          OK = chartype == ucp_Lu || chartype == ucp_Ll || chartype == ucp_Lt;
          break;

          case PT_GC:
          OK = category == code[5];
          break;

          case PT_PC:
          OK = chartype == code[5];
          break;

          case PT_SC:
          OK = script == code[5];
          break;

          /* Should never occur, but keep compilers from grumbling. */

          default:
          OK = codevalue != OP_PROP;
          break;
          }

        if (OK == (d == OP_PROP))
          {
          if (codevalue == OP_PROP_EXTRA + OP_TYPEPOSUPTO)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          if (++count >= GET2(code, 1))
            { ADD_NEW(state_offset + 6, 0); }
          else
            { ADD_NEW(state_offset, count); }
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_EXTUNI_EXTRA + OP_TYPEEXACT:
      case OP_EXTUNI_EXTRA + OP_TYPEUPTO:
      case OP_EXTUNI_EXTRA + OP_TYPEMINUPTO:
      case OP_EXTUNI_EXTRA + OP_TYPEPOSUPTO:
      if (codevalue != OP_EXTUNI_EXTRA + OP_TYPEEXACT)
        { ADD_ACTIVE(state_offset + 4, 0); }
      count = current_state->count;  /* Number already matched */
      if (clen > 0 && _pcre_ucp_findprop(c, &chartype, &script) != ucp_M)
        {
        const uschar *nptr = ptr + clen;
        int ncount = 0;
        if (codevalue == OP_EXTUNI_EXTRA + OP_TYPEPOSUPTO)
          {
          active_count--;           /* Remove non-match possibility */
          next_active_state--;
          }
        while (nptr < end_subject)
          {
          int nd;
          int ndlen = 1;
          GETCHARLEN(nd, nptr, ndlen);
          if (_pcre_ucp_findprop(nd, &chartype, &script) != ucp_M) break;
          ncount++;
          nptr += ndlen;
          }
        if (++count >= GET2(code, 1))
          { ADD_NEW_DATA(-(state_offset + 4), 0, ncount); }
        else
          { ADD_NEW_DATA(-state_offset, count, ncount); }
        }
      break;
#endif

      /*-----------------------------------------------------------------*/
      case OP_ANYNL_EXTRA + OP_TYPEEXACT:
      case OP_ANYNL_EXTRA + OP_TYPEUPTO:
      case OP_ANYNL_EXTRA + OP_TYPEMINUPTO:
      case OP_ANYNL_EXTRA + OP_TYPEPOSUPTO:
      if (codevalue != OP_ANYNL_EXTRA + OP_TYPEEXACT)
        { ADD_ACTIVE(state_offset + 4, 0); }
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        int ncount = 0;
        switch (c)
          {
          case 0x000b:
          case 0x000c:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          if ((md->moptions & PCRE_BSR_ANYCRLF) != 0) break;
          goto ANYNL03;

          case 0x000d:
          if (ptr + 1 < end_subject && ptr[1] == 0x0a) ncount = 1;
          /* Fall through */

          ANYNL03:
          case 0x000a:
          if (codevalue == OP_ANYNL_EXTRA + OP_TYPEPOSUPTO)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          if (++count >= GET2(code, 1))
            { ADD_NEW_DATA(-(state_offset + 4), 0, ncount); }
          else
            { ADD_NEW_DATA(-state_offset, count, ncount); }
          break;

          default:
          break;
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_VSPACE_EXTRA + OP_TYPEEXACT:
      case OP_VSPACE_EXTRA + OP_TYPEUPTO:
      case OP_VSPACE_EXTRA + OP_TYPEMINUPTO:
      case OP_VSPACE_EXTRA + OP_TYPEPOSUPTO:
      if (codevalue != OP_VSPACE_EXTRA + OP_TYPEEXACT)
        { ADD_ACTIVE(state_offset + 4, 0); }
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x000a:
          case 0x000b:
          case 0x000c:
          case 0x000d:
          case 0x0085:
          case 0x2028:
          case 0x2029:
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          }

        if (OK == (d == OP_VSPACE))
          {
          if (codevalue == OP_VSPACE_EXTRA + OP_TYPEPOSUPTO)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          if (++count >= GET2(code, 1))
            { ADD_NEW_DATA(-(state_offset + 4), 0, 0); }
          else
            { ADD_NEW_DATA(-state_offset, count, 0); }
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_HSPACE_EXTRA + OP_TYPEEXACT:
      case OP_HSPACE_EXTRA + OP_TYPEUPTO:
      case OP_HSPACE_EXTRA + OP_TYPEMINUPTO:
      case OP_HSPACE_EXTRA + OP_TYPEPOSUPTO:
      if (codevalue != OP_HSPACE_EXTRA + OP_TYPEEXACT)
        { ADD_ACTIVE(state_offset + 4, 0); }
      count = current_state->count;  /* Number already matched */
      if (clen > 0)
        {
        BOOL OK;
        switch (c)
          {
          case 0x09:      /* HT */
          case 0x20:      /* SPACE */
          case 0xa0:      /* NBSP */
          case 0x1680:    /* OGHAM SPACE MARK */
          case 0x180e:    /* MONGOLIAN VOWEL SEPARATOR */
          case 0x2000:    /* EN QUAD */
          case 0x2001:    /* EM QUAD */
          case 0x2002:    /* EN SPACE */
          case 0x2003:    /* EM SPACE */
          case 0x2004:    /* THREE-PER-EM SPACE */
          case 0x2005:    /* FOUR-PER-EM SPACE */
          case 0x2006:    /* SIX-PER-EM SPACE */
          case 0x2007:    /* FIGURE SPACE */
          case 0x2008:    /* PUNCTUATION SPACE */
          case 0x2009:    /* THIN SPACE */
          case 0x200A:    /* HAIR SPACE */
          case 0x202f:    /* NARROW NO-BREAK SPACE */
          case 0x205f:    /* MEDIUM MATHEMATICAL SPACE */
          case 0x3000:    /* IDEOGRAPHIC SPACE */
          OK = TRUE;
          break;

          default:
          OK = FALSE;
          break;
          }

        if (OK == (d == OP_HSPACE))
          {
          if (codevalue == OP_HSPACE_EXTRA + OP_TYPEPOSUPTO)
            {
            active_count--;           /* Remove non-match possibility */
            next_active_state--;
            }
          if (++count >= GET2(code, 1))
            { ADD_NEW_DATA(-(state_offset + 4), 0, 0); }
          else
            { ADD_NEW_DATA(-state_offset, count, 0); }
          }
        }
      break;

/* ========================================================================== */
      /* These opcodes are followed by a character that is usually compared
      to the current subject character; it is loaded into d. We still get
      here even if there is no subject character, because in some cases zero
      repetitions are permitted. */

      /*-----------------------------------------------------------------*/
      case OP_CHAR:
      if (clen > 0 && c == d) { ADD_NEW(state_offset + dlen + 1, 0); }
      break;

      /*-----------------------------------------------------------------*/
      case OP_CHARNC:
      if (clen == 0) break;

#ifdef SUPPORT_UTF8
      if (utf8)
        {
        if (c == d) { ADD_NEW(state_offset + dlen + 1, 0); } else
          {
          unsigned int othercase;
          if (c < 128) othercase = fcc[c]; else

          /* If we have Unicode property support, we can use it to test the
          other case of the character. */

#ifdef SUPPORT_UCP
          othercase = _pcre_ucp_othercase(c);
#else
          othercase = NOTACHAR;
#endif

          if (d == othercase) { ADD_NEW(state_offset + dlen + 1, 0); }
          }
        }
      else
#endif  /* SUPPORT_UTF8 */

      /* Non-UTF-8 mode */
        {
        if (lcc[c] == lcc[d]) { ADD_NEW(state_offset + 2, 0); }
        }
      break;


#ifdef SUPPORT_UCP
      /*-----------------------------------------------------------------*/
      /* This is a tricky one because it can match more than one character.
      Find out how many characters to skip, and then set up a negative state
      to wait for them to pass before continuing. */

      case OP_EXTUNI:
      if (clen > 0 && _pcre_ucp_findprop(c, &chartype, &script) != ucp_M)
        {
        const uschar *nptr = ptr + clen;
        int ncount = 0;
        while (nptr < end_subject)
          {
          int nclen = 1;
          GETCHARLEN(c, nptr, nclen);
          if (_pcre_ucp_findprop(c, &chartype, &script) != ucp_M) break;
          ncount++;
          nptr += nclen;
          }
        ADD_NEW_DATA(-(state_offset + 1), 0, ncount);
        }
      break;
#endif

      /*-----------------------------------------------------------------*/
      /* This is a tricky like EXTUNI because it too can match more than one
      character (when CR is followed by LF). In this case, set up a negative
      state to wait for one character to pass before continuing. */

      case OP_ANYNL:
      if (clen > 0) switch(c)
        {
        case 0x000b:
        case 0x000c:
        case 0x0085:
        case 0x2028:
        case 0x2029:
        if ((md->moptions & PCRE_BSR_ANYCRLF) != 0) break;

        case 0x000a:
        ADD_NEW(state_offset + 1, 0);
        break;

        case 0x000d:
        if (ptr + 1 < end_subject && ptr[1] == 0x0a)
          {
          ADD_NEW_DATA(-(state_offset + 1), 0, 1);
          }
        else
          {
          ADD_NEW(state_offset + 1, 0);
          }
        break;
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_NOT_VSPACE:
      if (clen > 0) switch(c)
        {
        case 0x000a:
        case 0x000b:
        case 0x000c:
        case 0x000d:
        case 0x0085:
        case 0x2028:
        case 0x2029:
        break;

        default:
        ADD_NEW(state_offset + 1, 0);
        break;
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_VSPACE:
      if (clen > 0) switch(c)
        {
        case 0x000a:
        case 0x000b:
        case 0x000c:
        case 0x000d:
        case 0x0085:
        case 0x2028:
        case 0x2029:
        ADD_NEW(state_offset + 1, 0);
        break;

        default: break;
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_NOT_HSPACE:
      if (clen > 0) switch(c)
        {
        case 0x09:      /* HT */
        case 0x20:      /* SPACE */
        case 0xa0:      /* NBSP */
        case 0x1680:    /* OGHAM SPACE MARK */
        case 0x180e:    /* MONGOLIAN VOWEL SEPARATOR */
        case 0x2000:    /* EN QUAD */
        case 0x2001:    /* EM QUAD */
        case 0x2002:    /* EN SPACE */
        case 0x2003:    /* EM SPACE */
        case 0x2004:    /* THREE-PER-EM SPACE */
        case 0x2005:    /* FOUR-PER-EM SPACE */
        case 0x2006:    /* SIX-PER-EM SPACE */
        case 0x2007:    /* FIGURE SPACE */
        case 0x2008:    /* PUNCTUATION SPACE */
        case 0x2009:    /* THIN SPACE */
        case 0x200A:    /* HAIR SPACE */
        case 0x202f:    /* NARROW NO-BREAK SPACE */
        case 0x205f:    /* MEDIUM MATHEMATICAL SPACE */
        case 0x3000:    /* IDEOGRAPHIC SPACE */
        break;

        default:
        ADD_NEW(state_offset + 1, 0);
        break;
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_HSPACE:
      if (clen > 0) switch(c)
        {
        case 0x09:      /* HT */
        case 0x20:      /* SPACE */
        case 0xa0:      /* NBSP */
        case 0x1680:    /* OGHAM SPACE MARK */
        case 0x180e:    /* MONGOLIAN VOWEL SEPARATOR */
        case 0x2000:    /* EN QUAD */
        case 0x2001:    /* EM QUAD */
        case 0x2002:    /* EN SPACE */
        case 0x2003:    /* EM SPACE */
        case 0x2004:    /* THREE-PER-EM SPACE */
        case 0x2005:    /* FOUR-PER-EM SPACE */
        case 0x2006:    /* SIX-PER-EM SPACE */
        case 0x2007:    /* FIGURE SPACE */
        case 0x2008:    /* PUNCTUATION SPACE */
        case 0x2009:    /* THIN SPACE */
        case 0x200A:    /* HAIR SPACE */
        case 0x202f:    /* NARROW NO-BREAK SPACE */
        case 0x205f:    /* MEDIUM MATHEMATICAL SPACE */
        case 0x3000:    /* IDEOGRAPHIC SPACE */
        ADD_NEW(state_offset + 1, 0);
        break;
        }
      break;

      /*-----------------------------------------------------------------*/
      /* Match a negated single character. This is only used for one-byte
      characters, that is, we know that d < 256. The character we are
      checking (c) can be multibyte. */

      case OP_NOT:
      if (clen > 0)
        {
        unsigned int otherd = ((ims & PCRE_CASELESS) != 0)? fcc[d] : d;
        if (c != d && c != otherd) { ADD_NEW(state_offset + dlen + 1, 0); }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_PLUS:
      case OP_MINPLUS:
      case OP_POSPLUS:
      case OP_NOTPLUS:
      case OP_NOTMINPLUS:
      case OP_NOTPOSPLUS:
      count = current_state->count;  /* Already matched */
      if (count > 0) { ADD_ACTIVE(state_offset + dlen + 1, 0); }
      if (clen > 0)
        {
        unsigned int otherd = NOTACHAR;
        if ((ims & PCRE_CASELESS) != 0)
          {
#ifdef SUPPORT_UTF8
          if (utf8 && d >= 128)
            {
#ifdef SUPPORT_UCP
            otherd = _pcre_ucp_othercase(d);
#endif  /* SUPPORT_UCP */
            }
          else
#endif  /* SUPPORT_UTF8 */
          otherd = fcc[d];
          }
        if ((c == d || c == otherd) == (codevalue < OP_NOTSTAR))
          {
          if (count > 0 &&
              (codevalue == OP_POSPLUS || codevalue == OP_NOTPOSPLUS))
            {
            active_count--;             /* Remove non-match possibility */
            next_active_state--;
            }
          count++;
          ADD_NEW(state_offset, count);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_QUERY:
      case OP_MINQUERY:
      case OP_POSQUERY:
      case OP_NOTQUERY:
      case OP_NOTMINQUERY:
      case OP_NOTPOSQUERY:
      ADD_ACTIVE(state_offset + dlen + 1, 0);
      if (clen > 0)
        {
        unsigned int otherd = NOTACHAR;
        if ((ims & PCRE_CASELESS) != 0)
          {
#ifdef SUPPORT_UTF8
          if (utf8 && d >= 128)
            {
#ifdef SUPPORT_UCP
            otherd = _pcre_ucp_othercase(d);
#endif  /* SUPPORT_UCP */
            }
          else
#endif  /* SUPPORT_UTF8 */
          otherd = fcc[d];
          }
        if ((c == d || c == otherd) == (codevalue < OP_NOTSTAR))
          {
          if (codevalue == OP_POSQUERY || codevalue == OP_NOTPOSQUERY)
            {
            active_count--;            /* Remove non-match possibility */
            next_active_state--;
            }
          ADD_NEW(state_offset + dlen + 1, 0);
          }
        }
      break;

      /*-----------------------------------------------------------------*/
      case OP_STAR:
      case OP_MINSTAR:
      case OP_POSSTAR:
      case OP_NOTSTAR:
      case OP_NOTMINSTAR:
      case OP_NOTPOSSTAR:
      ADD_ACTIVE(state_offset + dlen + 1, 0);
      if (clen > 0)
        {
        unsigned int otherd = NOTACHAR;
        if ((ims & PCREnsion@v8@@QEBA_KXZ __imp_?source_length@Extension@v8@@QEBA_KXZ ?name@Extension@v8@@QEBAPEBDXZ __imp_?name@Extension@v8@@QEBAPEBDXZ ?GetNativeFunction@Extension@v8@@UEAA?AV?$Handle@VFunctionTemplate@v8@@@2@V?$Handle@VString@v8@@@2@@Z __imp_?GetNativeFunction@Extension@v8@@UEAA?AV?$Handle@VFunctionTemplate@v8@@@2@V?$Handle@VString@v8@@@2@@Z ??1Extension@v8@@UEAA@XZ __imp_??1Extension@v8@@UEAA@XZ ??1ExternalAsciiStringResourceImpl@v8@@UEAA@XZ __imp_??1ExternalAsciiStringResourceImpl@v8@@UEAA@XZ ?length@ExternalAsciiStringResourceImpl@v8@@UEBA_KXZ __imp_?length@ExternalAsciiStringResourceImpl@v8@@UEBA_KXZ ?data@ExternalAsciiStringResourceImpl@v8@@UEBAPEBDXZ __imp_?data@ExternalAsciiStringResourceImpl@v8@@UEBAPEBDXZ ??0ExternalAsciiStringResourceImpl@v8@@QEAA@PEBD_K@Z __imp_??0ExternalAsciiStringResourceImpl@v8@@QEAA@PEBD_K@Z ??0ExternalAsciiStringResourceImpl@v8@@QEAA@XZ __imp_??0ExternalAsciiStringResourceImpl@v8@@QEAA@XZ ??4TypeSwitch@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4TypeSwitch@v8@@QEAAAEAV01@AEBV01@@Z ??4AccessorSignature@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4AccessorSignature@v8@@QEAAAEAV01@AEBV01@@Z ??4Signature@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4Signature@v8@@QEAAAEAV01@AEBV01@@Z ??4ObjectTemplate@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4ObjectTemplate@v8@@QEAAAEAV01@AEBV01@@Z ??4FunctionTemplate@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4FunctionTemplate@v8@@QEAAAEAV01@AEBV01@@Z ??4AccessorInfo@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4AccessorInfo@v8@@QEAAAEAV01@AEBV01@@Z ??0AccessorInfo@v8@@QEAA@PEAPEAVObject@internal@1@@Z __imp_??0AccessorInfo@v8@@QEAA@PEAPEAVObject@internal@1@@Z ??4Template@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4Template@v8@@QEAAAEAV01@AEBV01@@Z ?length@Value@String@v8@@QEBAHXZ __imp_?length@Value@String@v8@@QEBAHXZ ??DValue@String@v8@@QEBAPEBGXZ __imp_??DValue@String@v8@@QEBAPEBGXZ ??DValue@String@v8@@QEAAPEAGXZ __imp_??DValue@String@v8@@QEAAPEAGXZ ?length@AsciiValue@String@v8@@QEBAHXZ __imp_?length@AsciiValue@String@v8@@QEBAHXZ ??DAsciiValue@String@v8@@QEBAPEBDXZ __imp_??DAsciiValue@String@v8@@QEBAPEBDXZ ??DAsciiValue@String@v8@@QEAAPEADXZ __imp_??DAsciiValue@String@v8@@QEAAPEADXZ ?length@Utf8Value@String@v8@@QEBAHXZ __imp_?length@Utf8Value@String@v8@@QEBAHXZ ??DUtf8Value@String@v8@@QEBAPEBDXZ __imp_??DUtf8Value@String@v8@@QEBAPEBDXZ ??DUtf8Value@String@v8@@QEAAPEADXZ __imp_??DUtf8Value@String@v8@@QEAAPEADXZ ??0ExternalAsciiStringResource@String@v8@@IEAA@XZ __imp_??0ExternalAsciiStringResource@String@v8@@IEAA@XZ ??1ExternalAsciiStringResource@String@v8@@UEAA@XZ __imp_??1ExternalAsciiStringResource@String@v8@@UEAA@XZ ??0ExternalStringResource@String@v8@@IEAA@XZ __imp_??0ExternalStringResource@String@v8@@IEAA@XZ ??1ExternalStringResource@String@v8@@UEAA@XZ __imp_??1ExternalStringResource@String@v8@@UEAA@XZ ?Dispose@ExternalStringResourceBase@String@v8@@MEAAXXZ __imp_?Dispose@ExternalStringResourceBase@String@v8@@MEAAXXZ ??0ExternalStringResourceBase@String@v8@@IEAA@XZ __imp_??0ExternalStringResourceBase@String@v8@@IEAA@XZ ??1ExternalStringResourceBase@String@v8@@UEAA@XZ __imp_??1ExternalStringResourceBase@String@v8@@UEAA@XZ ??4StackFrame@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4StackFrame@v8@@QEAAAEAV01@AEBV01@@Z ??4StackTrace@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4StackTrace@v8@@QEAAAEAV01@AEBV01@@Z ??4Message@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4Message@v8@@QEAAAEAV01@AEBV01@@Z ??4Script@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4Script@v8@@QEAAAEAV01@AEBV01@@Z ??4ScriptData@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4ScriptData@v8@@QEAAAEAV01@AEBV01@@Z ??0ScriptData@v8@@QEAA@AEBV01@@Z __imp_??0ScriptData@v8@@QEAA@AEBV01@@Z ??0ScriptData@v8@@QEAA@XZ __imp_??0ScriptData@v8@@QEAA@XZ ??1ScriptData@v8@@UEAA@XZ __imp_??1ScriptData@v8@@UEAA@XZ ??4Data@v8@@QEAAAEAV01@AEBV01@@Z __imp_??4Data@v8@@QEAAAEAV01@AEBV01@@Z ??4Data@HandleScope@v8@@QEAAAEAV012@AEBV012@@Z __imp_??4Data@HandleScope@v8@@QEAAAEAV012@AEBV012@@Z ?Initialize@Data@HandleScope@v8@@QEAAXXZ __imp_?Initialize@Data@HandleScope@v8@@QEAAXXZ 
/               1379357662              0       12087     `
    ^  a  <b  fà  xã  Íà  ä  ‘Ü  tâ  Pô  –ò  |ä  ⁄á  XÜ   ã  Í¶  Já  ¶é  ⁄í   ë  \ì  Óå  &è  Dí  Bu  4d  ∫d  jn   {  Fj   j  ^ï  ~ê  é  .r  ¸ã  tå  BÇ  Œu  xx   e  4Å  »Ç  T|  Fk  ‘k  ¶î  ™r  y  h  ûh  Ùì  Üp  Ä  ‹m  2s  \l  .z  ¬û  Í¢  L¢  ∫°  Bò  ^¶  ∆•  8•  Úw  Ë|  $°  ®§  Tü  ¯õ  Óü  éú  à†  "ù  dw  :e  ñ  8û  0ó  ∫ó  |£  "§  ∏ù  ∏è  Ín  ˆo  Zö  öñ  Íf  Äg  ∫Å  ¥t  Ñ~  >{  †y  *t  ûc  ÆÄ  võ  ÿÖ  XÖ  ‘ô  ÃÉ  HÉ  ö  ‘Ñ  PÑ  Xf  do  &i  q  Üç  ¥i  úq  
  í  ∫z  x}  ¸}  ‡l  bm  ∞s  ‚v  Rv  ∫ë  Ì   c   N ) n [ \ 2 3 p s   - . 9 z { 7  W o X 5 q t # 0 8 | b ^  ' ~ } M C ( 1 a : w `  , D x y _ u v 6 d * ] & + j i m l g f $ %  r "   V !      4 /   O Z Q R ? h Y k e H J L U P ; G I K E > = < S T F B A @   c   N ) n [ \ 2 3 p s   - . 9 z { 7  W o X 5 q t # 0 8 | b ^  ' ~ } M C ( 1 a : w `  , D x y _ u v 6 d * ] & + j i m l g f       	     $ %  r "   V !      4 /   O Z Q R ?  
 h Y k e H J L U P ; G I K E > = < S T F B A @   ??0AccessorInfo@v8@@QEAA@PEAPEAVObject@internal@1@@Z ??0ActivityControl@v8@@QEAA@AEBV01@@Z ??0ActivityControl@v8@@QEAA@XZ ??0DeclareExtension@v8@@QEAA@PEAVExtension@1@@Z ??0ExtensionConfiguration@v8@@QEAA@HQEAPEBD@Z ??0ExternalAsciiStringResource@String@v8@@IEAA@XZ ??0ExternalAsciiStringResourceImpl@v8@@QEAA@PEBD_K@Z ??0ExternalAsciiStringResourceImpl@v8@@QEAA@XZ ??0ExternalResourceVisitor@v8@@QEAA@AEBV01@@Z ??0ExternalResourceVisitor@v8@@QEAA@XZ ??0ExternalStringResource@String@v8@@IEAA@XZ ??0ExternalStringResourceBase@String@v8@@IEAA@XZ ??0OutputStream@v8@@QEAA@AEBV01@@Z ??0OutputStream@v8@@QEAA@XZ ??0PersistentHandleVisitor@v8@@QEAA@AEBV01@@Z ??0PersistentHandleVisitor@v8@@QEAA@XZ ??0Scope@Isolate@v8@@QEAA@PEAV12@@Z ??0ScriptData@v8@@QEAA@AEBV01@@Z ??0ScriptData@v8@@QEAA@XZ ??0StartupDataDecompressor@v8@@QEAA@AEBV01@@Z ??1ActivityControl@v8@@UEAA@XZ ??1Extension@v8@@UEAA@XZ ??1ExternalAsciiStringResource@String@v8@@UEAA@XZ ??1ExternalAsciiStringResourceImpl@v8@@UEAA@XZ ??1ExternalResourceVisitor@v8@@UEAA@XZ ??1ExternalStringResource@String@v8@@UEAA@XZ ??1ExternalStringResourceBase@String@v8@@UEAA@XZ ??1OutputStream@v8@@UEAA@XZ ??1PersistentHandleVisitor@v8@@UEAA@XZ ??1Scope@Isolate@v8@@QEAA@XZ ??1ScriptData@v8@@UEAA@XZ ??4AccessorInfo@v8@@QEAAAEAV01@AEBV01@@Z ??4AccessorSignature@v8@@QEAAAEAV01@AEBV01@@Z ??4ActivityControl@v8@@QEAAAEAV01@AEBV01@@Z ??4Context@v8@@QEAAAEAV01@AEBV01@@Z ??4Data@HandleScope@v8@@QEAAAEAV012@AEBV012@@Z ??4Data@v8@@QEAAAEAV01@AEBV01@@Z ??4DeclareExtension@v8@@QEAAAEAV01@AEBV01@@Z ??4Exception@v8@@QEAAAEAV01@AEBV01@@Z ??4ExtensionConfiguration@v8@@QEAAAEAV01@AEBV01@@Z ??4ExternalResourceVisitor@v8@@QEAAAEAV01@AEBV01@@Z ??4FunctionTemplate@v8@@QEAAAEAV01@AEBV01@@Z ??4HeapStatistics@v8@@QEAAAEAV01@AEBV01@@Z ??4Message@v8@@QEAAAEAV01@AEBV01@@Z ??4ObjectTemplate@v8@@QEAAAEAV01@AEBV01@@Z ??4OutputStream@v8@@QEAAAEAV01@AEBV01@@Z ??4PersistentHandleVisitor@v8@@QEAAAEAV01@AEBV01@@Z ??4ResourceConstraints@v8@@QEAAAEAV01@AEBV01@@Z ??4Script@v8@@QEAAAEAV01@AEBV01@@Z ??4ScriptData@v8@@QEAAAEAV01@AEBV01@@Z ??4Signature@v8@@QEAAAEAV01@AEBV01@@Z ??4StackFrame@v8@@QEAAAEAV01@AEBV01@@Z ??4StackTrace@v8@@QEAAAEAV01@AEBV01@@Z ??4StartupDataDecompressor@v8@@QEAAAEAV01@AEBV01@@Z ??4Template@v8@@QEAAAEAV01@AEBV01@@Z ??4TryCatch@v8@@QEAAAEAV01@AEBV01@@Z ??4TypeSwitch@v8@@QEAAAEAV01@AEBV01@@Z ??4Unlocker@v8@@QEAAAEAV01@AEBV01@@Z ??4V8@v8@@QEAAAEAV01@AEBV01@@Z ??DAsciiValue@String@v8@@QEAAPEADXZ ??DAsciiValue@String@v8@@QEBAPEBDXZ ??DUtf8Value@String@v8@@QEAAPEADXZ ??DUtf8Value@String@v8@@QEBAPEBDXZ ??DValue@String@v8@@QEAAPEAGXZ ??DValue@String@v8@@QEBAPEBGXZ ??_FLocker@v8@@QEAAXXZ ??_FUnlocker@v8@@QEAAXXZ ?Data@AccessorInfo@v8@@QEBA?AV?$Local@VValue@v8@@@2@XZ ?Dispose@ExternalStringResourceBase@String@v8@@MEAAXXZ ?GetChunkSize@OutputStream@v8@@UEAAHXZ ?GetData@Isolate@v8@@QEAAPEAXXZ ?GetIsolate@AccessorInfo@v8@@QEBAPEAVIsolate@2@XZ ?GetNativeFunction@Extension@v8@@UEAA?AV?$Handle@VFunctionTemplate@v8@@@2@V?$Handle@VString@v8@@@2@@Z ?GetOutputEncoding@OutputStream@v8@@UEAA?AW4OutputEncoding@12@XZ ?Holder@AccessorInfo@v8@@QEBA?AV?$Local@VObject@v8@@@2@XZ ?Initialize@Data@HandleScope@v8@@QEAAXXZ ?Set@Template@v8@@QEAAXPEBDV?$Handle@VData@v8@@@2@@Z ?SetDataMicrosoft C/C++ MSF 7.00
DS         -   ∞       +                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           ‡    ˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇˇ˛Ô˛Ô                     Ä      ˇˇˇˇ    ˇˇˇˇ    ˇˇˇˇ
 q    ÚÒ
      
 p    >   Ä              localeinfo_struct Ulocaleinfo_struct@@ ÛÚÒ
         q  #       p   t        F   Ä              threadlocaleinfostruct Uthreadlocaleinfostruct@@ Ò
     B   Ä              threadmbcinfostruct Uthreadmbcinfostruct@@ ÛÚÒ
 	    *      locinfo ÚÒ 
   mbcinfo ÚÒ>              localeinfo_struct Ulocaleinfo_struct@@ ÛÚÒ    q    p   t            q      p   t        &   Ä              _iobuf U_iobuf@@ Ò
     
       !        é  p    _ptr Ò t    _cnt Ò p   _base  t    _flag  t    _file  t     _charbuf Ò t   $ _bufsiz ÚÒ p  ( _tmpfname &             0 _iobuf U_iobuf@@ Ò                  q      !        6   Ä              v8::HandleScope VHandleScope@v8@@ B   à              v8::HandleScope::Data VData@HandleScope@v8@@ Ò
    B   Ä              v8::internal::Object VObject@internal@v8@@ ÛÚÒ
     
      	                 F       next Ò     limit  t    level  !  Initialize ÒB  "           v8::HandleScope::Data VData@HandleScope@v8@@ Ò
      
   ,  
    ÚÒ
 &  ,  
    '   	%         (      
     
 &    *   Ä              v8::Data VData@v8@@ ÚÒ
 ,    	   ,  -             .  Data ÛÚÒ*  /           v8::Data VData@v8@@ ÚÒ
 ,  ,  
 ,   ÚÒ
 2  ,  
    3   	1  ,  -     4      
 2    
 ,    6   Ä              v8::ScriptData VScriptData@v8@@ ÚÒ
 8    
  UU
 :     	   8  9            
 8    J   Ä              v8::Handle<v8::String> V?$Handle@VString@v8@@@v8@@ ÛÚÒ
    >   	=  8         ?      
 p    ÚÒ
 A        B  t    	=  8         C          @     D   	t   8  9             	B  8  9             	0   8  9            
 8   ÚÒ
 I  ,  
    J   	   8  9    K       	   8  9              L    M  
 8  ,   	O  8  9     K      
    u    	  8  9     Q      Ó 	  ;   <      ~ScriptData  E  PreCompile Ò D  New  F     Length Ò G     Data ÛÚÒ H     HasError ÛÚÒ N  ScriptData ÒP  operator= ÚÒ<  __local_vftable_ctor_closure ÛÚÒR      __vecDelDtor ÛÚÒ6  &S      :   v8::ScriptData VScriptData@v8@@ ÚÒ          
 U   
 V    
 W     	   8  9     K      
             Z  
 [          #   t   \         ]  :   Ä              v8::ScriptOrigin VScriptOrigin@v8@@ ÚÒ
 _   F   Ä              v8::Handle<v8::Value> V?$Handle@VValue@v8@@@v8@@ ÒJ   Ä              v8::Handle<v8::Integer> V?$Handle@VInteger@v8@@@v8@@ Ò    a  b  b   	   _  `    c      
 _   ÚÒ
 e    	a  _  f            	b  _  f           Œ  d  ScriptOrigin ÛÚÒ g  ResourceName ÛÚÒ h  ResourceLineOffset Ò h  ResourceColumnOffset ÛÚÒ a    resource_name_ ÛÚÒ b   resource_line_offset_  b   resource_column_offset_ ÚÒ:  i           v8::ScriptOrigin VScriptOrigin@v8@@ ÚÒ 	   _  `     c      
 a    
 l    *   Ä              v8::Value VValue@v8@@ 
 n    
    o  
 a    	   a  q    p       	   a  q               r     s  
 a   ÚÒ
 u    	0   a  v             	   a  q             	o  a  v            v  t  Handle<v8::Value> ÚÒ w  IsEmpty  x  Clear ÚÒ y  operator-> Ò y  operator* ÚÒ o    val_ ÒF  z           v8::Handle<v8::Value> V?$Handle@VValue@v8@@@v8@@ Ò
 b    
 |    .   Ä              v8::Integer VInteger@v8@@ 
 ~    
      
 b    	   b  Å    Ä       	   b  Å               Ç     É  
 b   ÚÒ
 Ö    	0   b  Ü             	   b  Å             	  b  Ü            v  Ñ  Handle<v8::Integer>  á  IsEmpty  à  Clear ÚÒ â  operator-> Ò â  operator* ÚÒ     val_ ÒJ  ä           v8::Handle<v8::Integer> V?$Handle@VInteger@v8@@@v8@@ Ò.   Ä              v8::Script VScript@v8@@ ÚÒ
 å   F   Ä              v8::Local<v8::Script> V?$Local@VScript@v8@@@v8@@ Ò    >  a   	é  å        è      
 _        >  ë  =  >   	é  å        í          ê     ì      >  a  >   	é  å        ï          ñ     ì  F   Ä              v8::Local<v8::Value> V?$Local@VValue@v8@@@v8@@ ÛÚÒ 	ò  å  ç            	   å  ç     ?      F  î  New  ó  Compile  ô  Run  ô  Id Ò ö  SetData .   õ           v8::Script VScript@v8@@ ÚÒ
 å  ,  
 å   ÚÒ
 û  ,  
    ü   	ù  å  ç     †      
 û    
 å    .   Ä              v8::Message VMessage@v8@@ 
 §   F   Ä              v8::Local<v8::String> V?$Local@VString@v8@@@v8@@ Ò
 §   ÚÒ
 ß    	¶  §  ®            	a  §  ®           R   Ä              v8::Handle<v8::StackTrace> V?$Handle@VStackTrace@v8@@@v8@@ ÛÚÒ 	´  §  ®            	t   §  ®             	   §               
 t    ÚÒF ©  Get  ©  GetSourceLine ÚÒ ™  GetScriptResourceName ÚÒ ™  GetScriptData ÚÒ ¨  GetStackTrace ÚÒ ≠  GetLineNumber ÚÒ ≠  GetStartPosition ÛÚÒ ≠  GetEndPosition Ò ≠  GetStartColumn Ò ≠  GetEndColumn ÛÚÒ Æ  PrintCurrentStackTrace Ò Ø  kNoLineNumberInfo ÚÒ Ø  kNoColumnInfo ÚÒ.   ∞           v8::Message VMessage@v8@@ 
 §  ,  
 ß  ,  
    ≥   	≤  §  •     ¥      
 ß    
 §    6   Ä              v8::StackTrace VStackTrace@v8@@ ÚÒ
 ∏   ∫   kLineNumber ÚÒ  kColumnOffset   kScriptName ÚÒ  kFunctionName   kIsEval ÚÒ   kIsConstructor ÛÚÒ @ kScriptNameOrSourceURL ÛÚÒ  kOverview   kDetailed V 	 t   ∫  v8::StackTrace::StackTraceOptions W4StackTraceOptions@StackTrace@v8@@ ÚÒN   Ä              v8::Local<v8::StackFrame> V?$Local@VStackFrame@v8@@@v8@@ Ò
 ∏   ÚÒ
 Ω    	º  ∏  æ    Q       	t   ∏  æ            F   Ä              v8::Local<v8::Array> V?$Local@VArray@v8@@@v8@@ ÛÚÒ 	¡  ∏  π           N   Ä              v8::Local<v8::StackTrace> V?$Local@VStackTrace@v8@@@v8@@ Ò    t   ª   	√  ∏        ƒ      v   ª  StackTraceOptions ÚÒ ø  GetFrame ÛÚÒ ¿  GetFrameCount ÚÒ ¬  AsArray  ≈  CurrentStackTrace ÚÒ6  ∆           v8::StackTrace VStackTrace@v8@@ ÚÒ
 ∏  ,  
 Ω  ,  
    …   	»  ∏  π            
 Ω    
 ∏    6   Ä              v8::StackFrame VStackFrame@v8@@ ÚÒ
 Œ   
 Œ   ÚÒ
 –    	t   Œ  —             	¶  Œ  —            	0   Œ  —            ™  “  GetLineNumber ÚÒ “  GetColumn ÚÒ ”  GetScriptName ÚÒ ”  GetScriptNameOrSourceURL ÛÚÒ ”  GetFunctionName  ‘  IsEval Ò ‘  IsConstructor ÚÒ6   ’           v8::StackFrame VStackFrame@v8@@ ÚÒ
 Œ  ,  
 –  ,  
    ÿ   	◊  Œ  œ     Ÿ      
 –    
 Œ    .   Ä              v8::String VString@v8@@ ÚÒf   à              v8::String::ExternalStringResourceBase VExternalStringResourceBase@String@v8@@ ÛÚÒ
 ﬁ    
  UÒ
 ‡     	   ﬁ  ﬂ            
 ﬁ   ÚÒ
 „  ,  
    ‰   	   ﬁ  ﬂ    Â       	   ﬁ  ﬂ               Ê     Á   	   ﬁ  ﬂ     Â       	  ﬁ  ﬂ     Q      ¬ 	  ·   ‚      ~ExternalStringResourceBase  Ë  ExternalStringResourceBase Ò ‚     Dispose  È  operator= ÚÒ‚  __local_vftable_ctor_closure ÛÚÒÍ      __vecDelDtor ÛÚÒf  .Î      ‡   v8::String::ExternalStringResourceBase VExternalStringResourceBase@String@v8@@ ÛÚÒ^   à              v8::String::ExternalStringResource VExternalStringResource@String@v8@@ ÛÚÒ
 Ì    	   Ì  Ó            
 !    ÚÒ
     
 Ì   ÚÒ
 Ú   î.1›S7R   ÇÃßp9‚N¥/˜>0àH                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        8                                                                                                                                