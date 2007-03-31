/****************************************************************************

Name
   feh2.c -- code-generator back-end for ick parser

DESCRIPTION
   This module provides storage manglement and code degeneration
   for the INTERCAL compiler. Optimizations (formerly in this file)
   were split into dekludge.c.

LICENSE TERMS
    Copyright (C) 1996 Eric S. Raymond 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

****************************************************************************/
/*LINTLIBRARY */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sizes.h"
#include "ick.h"
#include "parser.h"
#include "fiddle.h"
#include "lose.h"
#include "feh.h"

/* AIS: Destaticed for dekludge.c */
int emitlineno;   /* line number for errors encountered during emit */

int mark112 = 0; /* AIS: Mark the next generated tuple for W112 */

/* AIS: From perpet.c */
extern int pickcompile;

/*************************************************************************
 *
 * Node allocation functions.
 *
 * Nodes are used to represent expression trees. The emit() function
 * deallocates them.
 *
 **************************************************************************/

node *newnode(void)
/* allocate and zero out a new expression node */
{
    return((node *)calloc(sizeof(node), 1));
}

node *cons(int type, node *car, node *cdr)
{
    node *np = newnode();

    np->opcode = type;
    np->lval = car;
    np->rval = cdr;

    return(np);
}

/*************************************************************************
 *
 * Variable-name mapping
 *
 * This permits us to optimize use of variable storage at runtime
 *
 **************************************************************************/

unsigned int intern(int type, unsigned int index)
{
    atom	*x;

    /* AIS: Allow use of a modifiable constant 0. */
    if ((index < 1 || index > 65535) && type!=MESH)
	lose(E200, yylineno, (char *)NULL);

    if (!oblist)
    {
	/* initialize oblist and obdex */
	oblist = malloc(ALLOC_CHUNK * sizeof(atom));
	if (!oblist)
	    lose(E778, yylineno, (char *)NULL);
	obdex = oblist;
	obcount = ALLOC_CHUNK;
    }

    /* if it's already on the oblist, return its intindex */
    for (x = oblist; x < obdex; x++)
	if (x->type == type && x->extindex == index)
	    return(x->intindex);

    /* else we must intern a new symbol */
    if (obdex >= oblist + obcount)
    {
	obcount += ALLOC_CHUNK;
	x = realloc(oblist, obcount * sizeof(atom));
	if (!x)
	    lose(E333, yylineno, (char *)NULL);
	obdex = x + (obdex - oblist);
	oblist = x;
    }
    obdex->type = type;
    obdex->extindex = index;
    obdex->memloc = 0; /* AIS: not placed in memory yet */
    if (type == ONESPOT)
	obdex->intindex = nonespots++;
    if (type == TWOSPOT)
	obdex->intindex = ntwospots++;
    if (type == TAIL)
	obdex->intindex = ntails++;
    if (type == HYBRID)
	obdex->intindex = nhybrids++;
    if (type == MESH) /* AIS: count meshes too */
        obdex->intindex = nmeshes++;
    ++obdex;

    return(obdex[-1].intindex);
}

/*************************************************************************
 *
 * This function insures a label is valid.
 *
 **************************************************************************/

void checklabel(int label)
{
    if (label < 1 || label > 65535)
	lose(E197, yylineno, (char *)NULL);
}

/*************************************************************************
 *
 * AIS: Search for the indexth COME_FROM sucking in the given tuple.
 *      Return an int representing the COME_FROM's tn-tuples+1, or -1.
 *      index is based at 1, not 0 as is usual for C.
 *
 ***************************************************************************/

int comefromsearch(tuple* tn, unsigned int index)
{
  tuple* tp;
  for (tp = tuples; tp < tuples + lineno; tp++)
  {
    if((tp->type == COME_FROM || tp->type == NEXTFROMLABEL)
	&& tp->u.target == (unsigned)(tn-tuples+1))
      index--;
    if(!index) return tp-tuples+1;
  }
  return -1;
}

/*************************************************************************
 *
 * Tuple allocation functions.
 *
 **************************************************************************/

void treset(void)
{
    tuplecount = 0;
    if (tuples)
    {
	free(tuples);
	tuples = NULL;
    }
    nonespots = ntwospots = ntails = nhybrids = 0;
    obdex = oblist;
    lineno = 0;
}

tuple *newtuple(void)
/* allocate and zero out a new expression tuple */
{
    if (lineno >= tuplecount)
    {
	tuplecount += ALLOC_CHUNK;
	if (tuples)
	    tuples = realloc(tuples, tuplecount * sizeof(tuple));
	else
	    tuples = malloc(tuplecount * sizeof(tuple));
	if (!tuples)
	{
	    lose(E666, yylineno, (char *)NULL);
	    return NULL;
	}
	memset(tuples + lineno, 0, (tuplecount - lineno) * sizeof(tuple));
    }
    if(mark112) tuples[lineno].warn112 = 1; mark112 = 0; /* AIS */
    return(tuples + lineno++);
}

/*************************************************************************
 *
 * The typecaster
 *
 * The theory here is that we associate a type with each node in order to
 * know what widths of unary-logical operator to use.
 *
 **************************************************************************/

void typecast(node *np)
{
    /* recurse so we typecast each node after all its subnodes */
    if (np == (node *)NULL)
	return;
    else if (np->lval != (node *)NULL)
	typecast(np->lval);
    if (np->rval != (node *)NULL)
	typecast(np->rval);

    /*
     * This is an entire set of type-deducing machinery right here.
     */
    if (np->opcode == MESH || np->opcode == ONESPOT || np->opcode == TAIL)
	np->width = 16;
    else if (np->opcode == TWOSPOT || np->opcode == HYBRID
		|| np->opcode == MINGLE || np->opcode == MESH32)
	np->width = 32;
    else if (np->opcode == AND || np->opcode == OR || np->opcode == XOR ||
	     np->opcode == FIN ||
	     (np->opcode >= WHIRL && np->opcode <= WHIRL5))
	np->width = np->rval->width;
    else if (np->opcode == SELECT)
	np->width = np->rval->width;	/* n-bit select has an n-bit result */
    else if (np->opcode == SUB)
	np->width = np->lval->width;	/* type of the array */
    else if (np->opcode == SLAT || np->opcode == BACKSLAT)
      np->width = np->lval->width; /* AIS: \ and / return their left arg */
}

/*************************************************************************
 *
 * The codechecker
 *
 * This checks for nasties like mismatched types in assignments that
 * can be detected at compile time -- also for errors that could cause
 * the compilation of the generated C to fail, like generated gotos to
 * nonexistent labels or duplicate labels.
 *
 * AIS: codecheck has another important job, that of filling in information
 *      about COME FROM suckpoints and ABSTAIN/REINSTATE command numbers
 *      into the tuples.
 *
 **************************************************************************/

void codecheck(void)
{
    tuple	*tp, *up;
    int         notpast1900; /* AIS */

    /* check for assignment type mismatches */
    /* This check can't be done at compile time---RTFM.  [LHH] */
/*
    for (tp = tuples; tp < tuples + lineno; tp++)
	if (tp->type == GETS)
	    if (tp->u.node->lval->width == 16 && tp->u.node->rval->width == 32)
		lose(E275, tp - tuples + 1, (char *)NULL);
*/

    /* check for duplicate labels */
    for (tp = tuples; tp < tuples + lineno; tp++)
	if (tp->label)
	    for (up = tuples; up < tuples + lineno; up++)
		if (tp != up && tp->label == up->label)
		  lose(E182, tp - tuples + 1, (char *)NULL);

    /*
     * Check that every NEXT, ABSTAIN, REINSTATE and COME_FROM actually has a
     * legitimate target label.
     */
    notpast1900 = TRUE;
    for (tp = tuples; tp < tuples + lineno; tp++)
    {
      if (tp->label == 1900) notpast1900 = FALSE; /* AIS */
        if (tp->type == NEXT
	    || tp->type == ABSTAIN || tp->type == REINSTATE
	    || tp->type == COME_FROM || tp->type == FROM
	    || tp->type == NEXTFROMLABEL) /* AIS: added FROM, NEXTFROMLABEL. */
	{
	    bool	foundit = FALSE;

	    if (tp->u.target >= 1900 && tp->u.target <= 1998)
	    {
	      /* AIS: This program uses syslib.i's random number feature... or are
	         we in syslib already? */
	      if(notpast1900) coopt = 0;
	    }
	    
	    for (up = tuples; up < tuples + lineno; up++)
		if (tp->u.target == up->label)
		{
		    foundit = TRUE;
		    break;
		}

	    if (!foundit)
	    {
	      /* AIS: Added the pickcompile check. Syslib has to be optimized
		 for PICs, so syslib.i isn't imported and so none of the lables
		 in it will appear in the program. */
		if (tp->type == NEXT &&
		    (!pickcompile||tp->u.target<1000||tp->u.target>1999))
		    lose(E129, tp - tuples + 1, (char *)NULL);
		else if (tp->type == NEXT) /* AIS */
		{tp->nexttarget=0; continue;}
		/* AIS: NEXTFROMLABEL's basically identical to COME_FROM */
		else if (tp->type == COME_FROM || tp->type == NEXTFROMLABEL)
		    lose(E444, tp - tuples + 1, (char *)NULL);
		else
		    lose(E139, tp - tuples + 1, (char *)NULL);
	    }
	    /* tell the other tuple if it is a COME FROM target */
	    /* AIS: NEXTFROMLABEL again */
	    else if (tp->type == COME_FROM || tp->type == NEXTFROMLABEL)
	    {
	        if (up->ncomefrom && !multithread) /* AIS: multithread check */
		    lose(E555, yylineno, (char *)NULL);
		else
                    up->ncomefrom++; /* AIS: to handle multiple COME FROMs */
	    }
	    /* this substitutes line numbers for label numbers
	       AIS: COME FROM now uses this too. This changes the logic
	       slightly so that an !foundit condition would fall through,
	       but as long as lose doesn't return, it's not a problem.
	       (I removed the else before the if.) */
	    if (tp->type != NEXT)
	    {
		tp->u.target = up - tuples + 1;
	    }
	    else /* AIS */
	    {
	      tp->nexttarget = up - tuples + 1;
	      up->nextable = TRUE;
	    }
	}
    }
}

/* AIS: Added the third argument to prexpr and prvar. It specifies
   whether the node should be freed or not. I added the third
   argument in all calls of prexpr/prvar. This protoype has been moved
   up through the file so it can be used earlier. Destaticed so it can
   be referenced by dekludge.c. */
void prexpr(node *np, FILE *fp, int freenode);
/*************************************************************************
 *
 * Code degeneration
 *
 * The theory behind this crock is that we've been handed a pointer to
 * a tuple representing a single INTERCAL statement, possibly with an
 * expression tree hanging off it and twisting slowly, slowly in the wind.
 *
 * Our mission, should we choose to accept it, is to emit C code which,
 * when linked to the INTERCAL run-time support, will do something
 * resembling the right thing.
 *
 **************************************************************************/

/*
 * If the order of statement-token defines in parser.y ever changes,
 * this will need to be reordered.
 */
char *enablersm1[MAXTYPES+1] =
{
    "UNKNOWN", /* AIS: so comments can be ABSTAINED/REINSTATED */
    "GETS",
    "RESIZE",
    "NEXT",
    "GO_AHEAD", /* AIS: Added for backtracking */
    "GO_BACK",  /* AIS: Added for backtracking */
    "FORGET",
    "RESUME",
    "STASH",
    "RETRIEVE",
    "IGNORE",
    "REMEMBER",
    "ABSTAIN",
    "REINSTATE",
    "DISABLE",
    "ENABLE",
    "MANYFROM", /* AIS: Added ABSTAIN expr FROM gerunds */
    "GIVE_UP",
    "READ_OUT",
    "WRITE_IN",
    "PIN",
    "COME_FROM",
    "NEXTFROMLABEL", /* AIS */
    "NEXTFROMEXPR", /* AIS */
    "NEXTFROMGERUND", /* AIS */
    "COMPUCOME", /* AIS: Added COMPUCOME */
    "GERUCOME", /* AIS: This is COME FROM gerunds */
    "TRY_AGAIN", /* AIS: Added TRY AGAIN */
    "FROM", /* AIS: ABSTAIN expr FROM LABEL */
};
char** enablers = enablersm1+1;

assoc vartypes[] =
{
    { ONESPOT,	"ONESPOT" },
    { TWOSPOT,	"TWOSPOT" },
    { TAIL,	"TAIL" },
    { HYBRID,	"HYBRID" },
    { 0,	(char *)NULL }
};

static assoc forgetbits[] =
{
    { ONESPOT,	"oneforget" },
    { TWOSPOT,	"twoforget" },
    { TAIL,	"tailforget" },
    { HYBRID,	"hyforget" },
    { 0,	(char *)NULL }
};

/* AIS: Destatic. This is now needed in perpet.c. */
assoc varstores[] =
{
    { ONESPOT,	"onespots" },
    { TWOSPOT,	"twospots" },
    { TAIL,	"tails" },
    { HYBRID,	"hybrids" },
    { 0,	(char *)NULL }
};

static assoc typedefs[] =
{
    { ONESPOT,	"type16" },
    { TWOSPOT,	"type32" },
    { TAIL,	"type16" },
    { HYBRID,	"type32" },
    { 0,	(char *)NULL }
};

char *nameof(int value, assoc table[])
/* return string corresponding to value in table */
{
    assoc	*ap;

    for (ap = table; ap->name; ap++)
	if (ap->value == value)
	    return(ap->name);
    return((char *)NULL);
}

/* AIS: Code for printing explanations (mixed C/INTERCAL code that lets
   the user know what the meaning of an expression is). This is paraphrased
   from the prexpr/prvar code lower down. It's passed to yuk so that the
   explain ('e') command works. It's also included in the degenerated C
   code when the option -c is used, so the person looking at the code can
   debug both the INTERCAL and ick itself more effectively, and used by
   -h to produce its optimizer-debug output. */

unsigned long varextern(unsigned long intern, int vartype)
{
  atom *x;
  if(!oblist) lose(E778, emitlineno, (char*) NULL);
  for (x = oblist; x < obdex; x++)
    if (x->type == vartype && (unsigned)x->intindex == intern)
      return(x->extindex);
  if(vartype==MESH) return 0; /* the mesh wasn't used after all */
  lose(E778, emitlineno, (char*) NULL);
  return 0; /* never reached */
}

void explvar(node* np, FILE* fp)
{
  node *sp;
  switch(np->opcode)
  {
  case ONESPOT:
    (void) fprintf(fp, ".%lu", varextern(np->constant,ONESPOT));
    break;
  case TWOSPOT:
    (void) fprintf(fp, ":%lu", varextern(np->constant,TWOSPOT));
    break;
  case TAIL:
    (void) fprintf(fp, ",%lu", varextern(np->constant,TAIL));
    break;
  case HYBRID:
    (void) fprintf(fp, ";%lu", varextern(np->constant,HYBRID));
    break;
  case SUB:
    (void) fprintf(fp, "(");
    explvar(np->lval, fp);
    (void) fprintf(fp, " SUB ");
    for (sp = np->rval ; sp ; sp = sp->rval)
      explexpr(sp->lval, fp);
    (void) fprintf(fp, ")");
    break;
  default:
    lose(E778, emitlineno, (char*) NULL);
  }
}

/* unlike prexpr, this doesn't free its operands */
void explexpr(node* np, FILE* fp)
{
  if(!np) return;
  switch (np->opcode)
  {
  case MINGLE:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " $ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case SELECT:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " ~ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case SLAT:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " / ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case BACKSLAT:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " \\ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case AND:
    (void) fprintf(fp, "(& ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case OR:
    (void) fprintf(fp, "(V ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case XOR:
    (void) fprintf(fp, "(? ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case FIN:
    if (Base < 3)
      lose(E997, emitlineno, (char *)NULL);
    (void) fprintf(fp, "(^ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case WHIRL:
  case WHIRL2:
  case WHIRL3:
  case WHIRL4:
  case WHIRL5:
    if (np->opcode - WHIRL + 3 > Base)
      lose(E997, emitlineno, (char *)NULL);
    if(np->opcode == WHIRL)
      (void) fprintf(fp, "(@ ");
    else
      (void) fprintf(fp, "(%d@ ", np->opcode - WHIRL + 1);
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case MESH:
    if(variableconstants) /* AIS */
      (void) fprintf(fp, "meshes[%lu]", np->constant);
    else
      (void) fprintf(fp, "0x%lx", np->constant);
    break;

  case MESH32:
    (void) fprintf(fp, "0x%lx", np->constant);
    break;

  case ONESPOT:
  case TWOSPOT:
  case TAIL:
  case HYBRID:
  case SUB:
    explvar(np, fp);
    break;

    /* cases from here down are generated by the optimizer */
  case C_AND:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case C_OR:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " | ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case C_XOR:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " ^ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;
	
  case C_NOT:
    (void) fprintf(fp, "(~ ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;	

  case C_NOTEQUAL:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " != ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case C_A:
    (void) fprintf(fp, "a");
    break;

  case C_NNAND:
    (void) fprintf(fp, "(!!(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, "))");
    break;

  case C_AND1ADD1:
    (void) fprintf(fp, "((");
    explexpr(np->rval,fp);
    (void) fprintf(fp, " & 0x1) + 0x1)");
    break;

  case C_1PLUS:
    (void) fprintf(fp, "(");
    explexpr(np->rval,fp);
    (void) fprintf(fp, " + 0x1)");
    break;

  case C_2MINUS:
    (void) fprintf(fp, "(0x2 - ");
    explexpr(np->rval,fp);
    (void) fprintf(fp, ")");
    break;

  case C_2SUBAND1:
    (void) fprintf(fp, "(0x2 - (");
    explexpr(np->rval,fp);
    (void) fprintf(fp, " & 0x1))");
    break;

  case C_LSHIFT:
    (void) fprintf(fp, "((");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ") << 0x1)");
    break;

  case C_LSHIFTIN1:
    (void) fprintf(fp, "(((");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ") << 0x1) | 0x1)");
    break;

  case C_LSHIFT2:
    (void) fprintf(fp, "((");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ") << 0x2)");
    break;

  case C_LSHIFT8:
    (void) fprintf(fp, "((");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ") << 0x8)");
    break;

  case C_RSHIFTBY:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " >> ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case C_RSHIFT:
    (void) fprintf(fp, "((");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " & ");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ") >> 0x1)");
    break;

  case C_XORGREATER:
    (void) fprintf(fp, "(");
    explexpr(np->lval, fp);
    (void) fprintf(fp, " > (");
    explexpr(np->rval, fp);
    (void) fprintf(fp, " ^ ");
    explexpr(np->lval, fp);
    (void) fprintf(fp, "))");
    break;
      
  case C_ISNONZERO:
    (void) fprintf(fp, "(!!");
    explexpr(np->rval, fp);
    (void) fprintf(fp, ")");
    break;

  case INTERSECTION:
    explexpr(np->lval, fp);
    (void) fprintf(fp, " + ");
    explexpr(np->rval, fp);
    break;

  case GETS:
  case RESIZE:
    explexpr(np->lval, fp);
    (void) fprintf(fp, " <- ");
    explexpr(np->rval, fp);
    break;

  case BY:
    explexpr(np->lval, fp);
    (void) fprintf(fp, " BY ");
    explexpr(np->rval, fp);
    break;
    
  default: 
    lose(E778, emitlineno, (char*) NULL);
    break;
  }
}

/* AIS: Added the third argument to prexpr and prvar. It specifies
   whether the node should be freed or not. I added the third
   argument in all calls of prexpr/prvar. */

/* AIS: I moved prexpr's prototype higher in the file.
   Destaticed so the optimizer can access it. */

void prvar(node *np, FILE *fp, int freenode)
/* print out args to pass to storage manager for reference */
{
    node	*sp;
    int		dim;

    switch (np->opcode)
    {
    case ONESPOT:
	(void) fprintf(fp, "onespots[%lu]", np->constant);
	break;

    case TWOSPOT:
	(void) fprintf(fp, "twospots[%lu]", np->constant);
	break;

    case TAIL:
	(void) fprintf(fp, "TAIL, &tails[%lu]", np->constant);
	break;

    case HYBRID:
	(void) fprintf(fp, "HYBRID, &hybrids[%lu]", np->constant);
	break;

    case SUB:
	{
	  (void) fprintf(fp, "aref(");
	  prvar(np->lval, fp, freenode);

	  dim = 0;
	  for (sp = np->rval ; sp ; sp = sp->rval)
	    dim++;
	  (void) fprintf(fp, ", %d", dim);

	  for (sp = np->rval ; sp ; sp = sp->rval) {
	    (void) fprintf(fp, ", ");
	    prexpr(sp->lval, fp, freenode);
	  }
	  (void) fprintf(fp, ")");
	}
	break;
    default: /* Added by AIS */
      lose(E778, emitlineno, (char*) NULL);
      break;      
    }
}

void ooprvar(node *np, FILE *fp, int freenode)
/* AIS: Print out the overloaded version */
{
    node	*sp;
    int		dim;

    switch (np->opcode)
    {
    case ONESPOT:
	(void) fprintf(fp, "oo_onespots[%lu]", np->constant);
	break;

    case TWOSPOT:
	(void) fprintf(fp, "oo_twospots[%lu]", np->constant);
	break;

    case TAIL:
    case HYBRID:
      /* This should never be reached */
      lose(E778, emitlineno, (char*) NULL);
      break;
      
    case SUB:
	{
	  (void) fprintf(fp, "aref(");
	  prvar(np->lval, fp, freenode);

	  dim = 0;
	  for (sp = np->rval ; sp ; sp = sp->rval)
	    dim++;
	  (void) fprintf(fp, ", %d", dim);

	  for (sp = np->rval ; sp ; sp = sp->rval) {
	    (void) fprintf(fp, ", ");
	    prexpr(sp->lval, fp, freenode);
	  }
	  (void) fprintf(fp, ")");
	}
	break;
    default:
      lose(E778, emitlineno, (char*) NULL);
      break;
    }
}

/* AIS: Give us a mesh with value x */
static unsigned long meshval(unsigned long x)
{
  if(variableconstants)
    return intern(MESH, x);
  else
    return x;
}

/* AIS: This is the reverse of prexpr, in a way.
   It degenerates an expression that causes *np to become equal to *target.
   If this is impossible at any point, it degenerates code that causes
   error 277 (and itself causes error 278 if the situation is inevitable). */
void revprexpr(node *np, FILE *fp, node *target)
{
  node* temp;
  switch (np->opcode)
  {
  case MINGLE:
    /* We can use select to determine what np->lval and np->rval have
       to become equal to, as long as we're in base 2. */
    if(Base!=2)
    {
      fprintf(fp, "  lose(E277, lineno, (char*) NULL);\n");
      lwarn(W278, emitlineno, (char*) NULL);
      break;
    }
    temp=cons(MESH,0,0);
    temp->constant=meshval(0xAAAAAAAALU);
    temp->width=32;
    temp=cons(SELECT,target,temp);
    temp->width=target->width;
    revprexpr(np->lval, fp, temp);
    free(temp->rval);
    free(temp);
    temp=cons(MESH,0,0);
    temp->constant=meshval(0x55555555LU);
    temp->width=32;
    temp=cons(SELECT,target,temp);
    temp->width=target->width;
    revprexpr(np->rval, fp, temp);
    free(temp->rval);
    free(temp);
    break;

  case SELECT:
    /* Set the left of the select to the target, and the right
       to 0xffffffff or 0xffff. This only works in base 2. */
    if(Base!=2)
    {
      fprintf(fp, "  lose(E277, lineno, (char*) NULL);\n");
      lwarn(W278, emitlineno, (char*) NULL);
      break;
    }
    temp=cons(MESH,0,0);
    temp->constant=meshval(target->width==32?0xFFFFFFFFLU:0xFFFFLU);
    temp->width=target->width;
    revprexpr(np->lval, fp, target);
    revprexpr(np->rval, fp, temp);
    free(temp);
    break;

  case BACKSLAT:
    /* Unimplemented. This isn't even in the parser yet, so it's a
       mystery how we got here. */
    lose(E778, emitlineno, (char*) NULL);
    break;

  case SLAT:
    /* We need to set the true value of the LHS... */

    /* Copied and modified from the GETS code */
    if(!pickcompile)
    {
      (void) fprintf(fp,"  (void) assign((char*)&");
      prvar(np->lval, fp, 0);
      (void) fprintf(fp,", %s", nameof(np->lval->opcode, vartypes));
      (void) fprintf(fp,", %s[%lu], ", nameof(np->lval->opcode, forgetbits),
		     np->lval->constant);
      prexpr(target, fp, 0);
      (void) fprintf(fp,");  \n");
    }
    else /* AIS: Added this case for the simpler PIC assignment rules */
    {
      (void) fprintf(fp,"\tif(ignore%s%lu) ",
		     nameof(np->lval->opcode,varstores),
		     np->lval->constant);
      prexpr(np->lval, fp, 0);
      (void) fprintf(fp, " = ");
      prexpr(target, fp, 0);
      (void) fprintf(fp, ";  \n");
    }

    /* ... and we need to cause overloading to happen. This is a copy
       of part of the code for SLAT, modified to work in this context. */
    ooprvar(np->lval, fp, 0);
    /* Do something highly non-portable with pointers that should work
       anyway. Each pointer needs to be given a unique code; so we use
       the hex representation of np casted to an unsigned long.
       Technically speaking, np->rval could be casted to anything; but
       all implementations I've ever seen cast unique pointers to unique
       numbers, which is good enough for our purposes. */
    (void) fprintf(fp, ".get=og%lx;\n  ", (unsigned long)np);
    ooprvar(np->lval, fp, 0);
    (void) fprintf(fp, ".set=os%lx;\n", (unsigned long)np);
    break;

  case AND:
  case OR:
  case XOR:
  case FIN:
  case WHIRL:
  case WHIRL2:
  case WHIRL3:
  case WHIRL4:
  case WHIRL5:
    temp=cons(np->opcode-AND+REV_AND,0,target);
    temp->width=temp->rval->width=np->width;
    revprexpr(np->rval, fp, temp);
    free(temp);
    break;

  case MESH:
    if(!variableconstants)
    {
      /* Can't set a mesh in this case */
      fprintf(fp, "  lose(E277, lineno, (char*) NULL);\n");
      lwarn(W278, emitlineno, (char*) NULL);
      break;
    }
    (void) fprintf(fp,"  meshes[%lu] = ",np->constant);
    prexpr(target, fp, 0);
    (void) fprintf(fp,";\n");
    break;

  case ONESPOT:
  case TWOSPOT:
  case TAIL:
  case HYBRID:
  case SUB:
    /* Copy the code for the GETS statement; this is almost the same
       thing. Modified because we're assigning target to np, not
       np->lval to np->rval, and to not free(). */
    if(opoverused&&
       (np->opcode==ONESPOT||np->opcode==TWOSPOT)) /* AIS */
    {
      (void) fprintf(fp,"  ");
      ooprvar(np, fp, 0);
      (void) fprintf(fp,".set(");
      prexpr(target, fp, 0);
      (void) fprintf(fp,",(void(*)())os%dspot%lu);\n",
		     (np->opcode==TWOSPOT)+1, np->constant);
    }
    else if(!pickcompile)
    {
      node* sp;
      
      if (np->opcode != SUB) {
	sp = np;
	(void) fprintf(fp,"  (void) assign((char*)&");
      }
      else {
	sp = np->lval;
	(void) fprintf(fp,"  (void) assign(");
      }
      prvar(np, fp, 0);
      (void) fprintf(fp,", %s", nameof(sp->opcode, vartypes));
      (void) fprintf(fp,", %s[%lu], ", nameof(sp->opcode, forgetbits),
		     sp->constant);
      prexpr(target, fp, 0);
      (void) fprintf(fp,");\n");
    }
    else /* AIS: Added this case for the simpler PIC assignment rules */
    {
      (void) fprintf(fp,"  if(ignore%s%lu) ",
		     nameof(np->opcode,varstores),
		     np->constant);
      prexpr(np, fp, 0);
      (void) fprintf(fp, " = ");
      prexpr(target, fp, 0);
      (void) fprintf(fp, ";\n");
    }
    break;

    /* cases from here down are generated by the optimizer, and so
       should never come up here and are errors. The exception is
       C_A, which should only ever appear in a target expression,
       so is also an error. */
  case MESH32:
  case C_AND:
  case C_OR:
  case C_XOR:
  case C_NOT:
  case C_NOTEQUAL:
  case C_A:
  case C_NNAND:
  case C_AND1ADD1:
  case C_1PLUS:
  case C_2MINUS:
  case C_2SUBAND1:
  case C_LSHIFT:
  case C_LSHIFTIN1:
  case C_LSHIFT2:
  case C_LSHIFT8:
  case C_RSHIFTBY:
  case C_RSHIFT:
  case C_XORGREATER:
  case C_ISNONZERO:
  case GETS: /* should never come up */
  default:
    lose(E778, emitlineno, (char*) NULL);
    break;
  }
}

/* AIS: Destaticed */
void prexpr(node *np, FILE *fp, int freenode)
/* print out C-function equivalent of an expression */
{
    int tempint;
    switch (np->opcode)
    {
    case MINGLE:
	(void) fprintf(fp, "mingle(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, ", ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case SELECT:
	(void) fprintf(fp, "iselect(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, ", ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case BACKSLAT: /* AIS */
      /* Unimplemented. This isn't even in the parser yet, so it's a
	 mystery how we got here. */
      lose(E778, emitlineno, (char*) NULL);
      break;

    case SLAT: /* AIS */
      (void) fprintf(fp,"((");
      ooprvar(np->lval, fp, 0);
      /* Do something highly non-portable with pointers that should work
	 anyway. Each pointer needs to be given a unique code; so we use
	 the hex representation of np casted to an unsigned long.
	 Technically speaking, np->rval could be casted to anything; but
	 all implementations I've ever seen cast unique pointers to unique
	 numbers, which is good enough for our purposes. */
      (void) fprintf(fp, ".get=og%lx),(", (unsigned long)np);
      ooprvar(np->lval, fp, 0);
      (void) fprintf(fp, ".set=os%lx),(", (unsigned long)np);
      prvar(np->lval, fp, freenode);
      /* np->rval will be freed later, when its expression is printed */
      (void) fprintf(fp, "))");
      return; /* mustn't be freed */

    case AND:
	(void) fprintf(fp, "and%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case OR:
	(void) fprintf(fp, "or%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case XOR:
	(void) fprintf(fp, "xor%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case FIN:
	if (Base < 3)
	  lose(E997, emitlineno, (char *)NULL);
	(void) fprintf(fp, "fin%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case WHIRL:
    case WHIRL2:
    case WHIRL3:
    case WHIRL4:
    case WHIRL5:
	if (np->opcode - WHIRL + 3 > Base)
	  lose(E997, emitlineno, (char *)NULL);
	(void) fprintf(fp, "whirl%d(%d, ", np->width, np->opcode - WHIRL + 1);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    /* AIS: Reversed operations */
    case REV_AND:
	(void) fprintf(fp, "rev_and%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case REV_OR:
	(void) fprintf(fp, "rev_or%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case REV_XOR:
	(void) fprintf(fp, "rev_xor%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case REV_FIN:
	if (Base < 3)
	  lose(E997, emitlineno, (char *)NULL);
	(void) fprintf(fp, "rev_fin%d(", np->width);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case REV_WHIRL:
    case REV_WHIRL2:
    case REV_WHIRL3:
    case REV_WHIRL4:
    case REV_WHIRL5:
	if (np->opcode - WHIRL + 3 > Base)
	  lose(E997, emitlineno, (char *)NULL);
	(void) fprintf(fp,
		       "rev_whirl%d(%d, ", np->width, np->opcode - WHIRL + 1);
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case MESH:
      if(variableconstants) /* AIS */
	(void) fprintf(fp, "meshes[%lu]", np->constant);
      else
	(void) fprintf(fp, "0x%lx", np->constant);
      break;

    case MESH32:
	(void) fprintf(fp, "0x%lx", np->constant);
	break;

    case ONESPOT:
    case TWOSPOT:
    case TAIL:
    case HYBRID:
	if(!opoverused||np->opcode==TAIL||np->opcode==HYBRID)
	  prvar(np, fp, freenode);
	else /* AIS */
	{
	  ooprvar(np, fp, freenode);
	  fprintf(fp, ".get(");
	  prvar(np, fp, freenode);
	  fprintf(fp,")");
	}
	break;

    case SUB:
	(void) fprintf(fp, "*(%s*)", nameof(np->lval->opcode, typedefs));
	prvar(np, fp, freenode);
	break;

	/* cases from here down are generated by the optimizer */
    case C_AND:
	(void) fprintf(fp, "(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, " & ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case C_OR:
	(void) fprintf(fp, "(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, " | ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;

    case C_XOR:
	(void) fprintf(fp, "(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, " ^ ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, ")");
	break;
	
    case C_NOT:
	(void) fprintf(fp, "(~");
        tempint=np->rval->width; /* AIS */
	prexpr(np->rval, fp, freenode);
	if (tempint == Small_digits)
	    (void) fprintf(fp, " & Max_small)");
	else
	    (void) fprintf(fp, " & Max_large)");
	break;	

	/* AIS: I added the rest of the cases */
    case C_NOTEQUAL:
      (void) fprintf(fp, "(");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " != ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ")");
      break;

    case C_A:
      (void) fprintf(fp, "a");
      break;
	
    case C_NNAND:
	(void) fprintf(fp, "(!!(");
	prexpr(np->lval, fp, freenode);
	(void) fprintf(fp, " & ");
	prexpr(np->rval, fp, freenode);
	(void) fprintf(fp, "))");
	break;

    case C_AND1ADD1:
      (void) fprintf(fp, "((");
      prexpr(np->rval,fp, freenode);
      (void) fprintf(fp, " & 0x1) + 0x1)");
      break;

    case C_1PLUS:
      (void) fprintf(fp, "(");
      prexpr(np->rval,fp, freenode);
      (void) fprintf(fp, " + 0x1)");
      break;

    case C_2MINUS:
      (void) fprintf(fp, "(0x2 - ");
      prexpr(np->rval,fp, freenode);
      (void) fprintf(fp, ")");
      break;

    case C_2SUBAND1:
      (void) fprintf(fp, "(0x2 - (");
      prexpr(np->rval,fp, freenode);
      (void) fprintf(fp, " & 0x1))");
      break;

    case C_LSHIFT:
      (void) fprintf(fp, "((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " & ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ") << 0x1)");
      break;

    case C_LSHIFTIN1:
      (void) fprintf(fp, "(((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " & ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ") << 0x1) | 0x1)");
      break;

    case C_LSHIFT2:
      (void) fprintf(fp, "((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " & ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ") << 0x2)");
      break;

    case C_LSHIFT8:
      (void) fprintf(fp, "((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " & ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ") << 0x8)");
      break;

    case C_RSHIFTBY:
      (void) fprintf(fp, "((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, ") >> ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ")");
      break;

    case C_RSHIFT:
      (void) fprintf(fp, "((");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " & ");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, ") >> 0x1)");
      break;
      
    case C_XORGREATER:
      (void) fprintf(fp, "(");
      prexpr(np->lval, fp, 0); /* Patched by Joris Huizer */
      (void) fprintf(fp, " > (");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, " ^ ");
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, "))");
      break;

    case C_ISNONZERO:
      (void) fprintf(fp, "(!!(");
      prexpr(np->rval, fp, freenode);
      (void) fprintf(fp, "))");
      break;
      
    case GETS: /* AIS: this is used only if freenode == 0 */
      if(freenode) lose(E778, emitlineno, (char*) NULL);
      prexpr(np->lval, fp, freenode);
      (void) fprintf(fp, " = ");
      prexpr(np->rval, fp, freenode);
      break;

    default: /* Added by AIS */
      if(!freenode) break; /* Be less careful when not freeing, because
			      this is used by -hH to print out its
			      intermediate optimization stages */
      lose(E778, emitlineno, (char*) NULL);
      break;
    }

    if(freenode) (void) free(np);
}

static char *nice_text(char *texts[], int lines)
{
#define MAXNICEBUF	512
  static char buf[MAXNICEBUF];
  char *cp, *text;
  int i;

  if (lines < 1)
    lines = 1;
  for (cp = buf, i = 0 ; i < lines ; ++i) {
    if (cp>buf+MAXNICEBUF-10) goto abort;
    if (i) {
      (*cp++) = '\\';
      (*cp++) = 'n';
      (*cp++) = '\\';
      (*cp++) = '\n';
      (*cp++) = '\t';
    }
    for (text = texts[i] ; text&&*text ; cp++, text++) {
      if (cp>buf+MAXNICEBUF-10) goto abort;
      if(*text == '"' || *text == '\\') {
	(*cp++) = '\\';
      }
      *cp = *text;
    }
  }
  *cp = '\0';
  return buf;
abort: (*cp++) = '.';
  (*cp++) = '.';
  (*cp++) = '.';
  *cp = '\0';
  return buf;
}

static void emit_guard(tuple *tn, FILE *fp)
/* emit execution guard for giiven tuple (note the unbalanced trailing {!) */
{
  if(tn->maybe) /* AIS */
  {
    if(!multithread) lose(E405, emitlineno, (char *)NULL);
    (void) fprintf(fp, "    gonebackto = setjmp(btjb); choicepoint();\n");
  }
  if(!flowoptimize || tn->abstainable) /* This condition by AIS */
  {
    (void) fprintf(fp, "    if (");
    if (tn->maybe) /* AIS */
      (void) fprintf(fp, "gonebackto == !(");
    if (tn->exechance < 100)
	(void) fprintf(fp, "roll(%d) && ", tn->exechance);
    if ((tn->type != NEXT && tn->type != GO_BACK && tn->type != COME_FROM
	 && /* AIS */ tn->type != NEXTFROMLABEL)
	|| tn->onceagainflag == onceagain_NORMAL)
      (void) fprintf(fp, "!ICKABSTAINED(%d))%s {\n", (int)(tn - tuples),
		     /* AIS */ tn->maybe?")":"");
    else /* AIS: [NEXT, GO_BACK, COME_FROM] ONCE needs specially
	    handled abstentions */
      (void) fprintf(fp, "!oldabstain)%s {\n",
		     /* AIS */ tn->maybe?")":"");
  }
  else
  { /* AIS */
    if(tn->maybe) lose(E778, emitlineno, (char*) NULL);
    if(!tn->initabstain)
    {
      if(tn->type != COMPUCOME && tn->type != GERUCOME
	 && tn->type != NEXTFROMEXPR && tn->type != NEXTFROMGERUND)
	(void) fprintf(fp, "    {\n");
      else (void) fprintf(fp, "    if(1) {\n"); /* COMPUCOME specifically needs
						   an if() so it can have
						   an else. */
    }
    else (void) fprintf(fp, "    if(0) {\n"); /* for exceptional cases like
						 DON'T COME FROM #1 where we
						 need a label or an else. */
  }
}

void emittextlines(FILE *fp)
{
  int i=0; /* The first textline is line 1 */
  (void) fprintf(fp, "\"\",\n");
  while(++i<yylineno)
  {
    (void) fprintf(fp, "\"%s\",\n",nice_text(textlines + i, 0));
  }
  (void) fprintf(fp, "\"\"\n");
}

void emit(tuple *tn, FILE *fp)
/* emit code for the given tuple */
{
    node *np, *sp;
    int	dim;
    int generatecfjump=0; /* AIS */
    static int pasttryagain=0; /* AIS */

    /* grind out label and source dump */
    if (yydebug || compile_only)
	(void) fprintf(fp, "    /* line %03d */\n", tn->lineno);
    if (tn->label)
	(void) fprintf(fp, "L%d:", tn->label);
    if (yydebug || compile_only)
    {
      (void) fprintf(fp, "\t/* %s */", textlines[tn->lineno]);
      /* AIS: grind out an expression explanation */
      if (tn->type == GETS || tn->type == FORGET || tn->type == RESUME
	  || tn->type == FROM || tn->type == COMPUCOME
	  || tn->type == MANYFROM || tn->type == NEXTFROMEXPR)
      {
	(void) fprintf(fp, "\n\t/* Expression is ");
	explexpr(tn->type == MANYFROM ? tn->u.node->lval :
		 tn->type == GETS ? tn->u.node->rval : tn->u.node, fp);
	(void) fprintf(fp, " */");
      }
    }
    (void) fputc('\n', fp);

    /* set up the "next" lexical line number for error messages */
    if (tn->type == NEXT) {
	tuple *up;
	for (up = tuples; up < tuples + lineno; up++)
	    if (tn->u.target == up->label) {
		emitlineno = up->lineno;
		break;
	    }
    } else if (tn->ncomefrom)
      emitlineno = comefromsearch(tn,1); /* AIS: For multithreading.
					    Return the 1st if we're forking. */
    else if (tn < tuples + lineno - 1)
	emitlineno = tn[1].lineno;
    else
	emitlineno = yylineno;
    if(!pickcompile) /* AIS: PICs can't report errors, so don't bother with lineno */
      (void) fprintf(fp, "    lineno = %d;\n", emitlineno);

    /* print warnings on -l */
    if(checkforbugs)
    {
      if(tn->warn112) lwarn(W112, emitlineno, (char*) NULL);
      if(tn->warn128) lwarn(W128, emitlineno, (char*) NULL);
      if(tn->warn534) lwarn(W534, emitlineno, (char*) NULL);
      if(tn->warn018) lwarn(W018, emitlineno, (char*) NULL);
      if(tn->warn016) lwarn(W016, emitlineno, (char*) NULL);
      if(tn->warn276) lwarn(W276, emitlineno, (char*) NULL);
      if(tn->warn239) lwarn(W239, emitlineno, (char*) NULL);
      if(tn->warn622) lwarn(W622, emitlineno, (char*) NULL);
    }
    
    /* emit debugging information */
    if (yukdebug||yukprofile)
    {
	(void) fprintf(fp, "    YUK(%d,%d);\n",
		       (int)(tn-tuples),emitlineno);
    }

    /* AIS: The +mystery option on degenerated code causes the code to
       unexpectedly terminate after 4 billion commands are run, thus
       preventing an infinite loop. Of course, it will enhance the fun
       if we don't tell the user that. (This is necessary for the
       constant-output optimizer to work properly.) */
    if(coopt) (void) fprintf(fp, "    MYSTERYLINE;\n");
    
    /* AIS: If the tuple is ONCE/AGAIN flagged, we need a delayed-action
       set of its abstention status to the AGAIN-flagged status. The
       problem is that some statements, like COME FROM, need to set after
       the command has finished, and some, like NEXT, need it before the
       command has started. At the moment, only NEXT and GO_BACK have a
       ONCE/AGAIN before it, rather than after (because neither of them
       continue in the normal fashion). */
    if ((tn->type == NEXT || tn->type == GO_BACK) &&
	tn->onceagainflag != onceagain_NORMAL)
    {
      /* ONCE/AGAIN has already been swapped by perpetrate.c in the case
	 of a preabstained statement ('DO NOT'...). So if currently have
	 a ONCE, it means that being abstained is the attractive state,
	 and if we currently have an AGAIN, it means that being
         reinstated is the attractive state. Semantics with computed
	 ABSTAIN: Don't change the abstention count unless necessary, in
	 which case change it to 0 or 1. */
      fprintf(fp,"    oldabstain = ICKABSTAINED(%d);\n", (int)(tn - tuples));
      fprintf(fp,"    ICKABSTAINED(%d) = %s;\n",
	      (int)(tn - tuples),
	      tn->onceagainflag==onceagain_ONCE ? "oldabstain ? oldabstain : 1"
	      : "0");
      /* This test-and-set must be atomic. As all statements are atomic anyway
         in the current version of ick, that isn't a problem, but if anyone
	 wants to try using POSIX's multithreading features, the above two
	 lines need to be a critical section. */
    }
    
    /* in the case of COMPUCOME, we need an extra guard */
    if (tn->type == COMPUCOME || tn->type == GERUCOME
        || tn->type == NEXTFROMEXPR || tn->type == NEXTFROMGERUND)
    {
      fprintf(fp,"    if(0)\n    {\n");
      fprintf(fp,"CCF%d:\n",compucomecount++);
      if(tn->type == COMPUCOME || tn->type == NEXTFROMEXPR)
      {
	fprintf(fp,"    if(skipto&&skipto==");
	prexpr(tn->u.node, fp, 1);
      }
      else if(tn->type == GERUCOME || tn->type == NEXTFROMGERUND)
      {
	fprintf(fp,"    if(");
	for (np = tn->u.node; np; np = np->rval)
	{
	  if (np->constant == ABSTAIN) {
	    (void) fprintf(fp,
"linetype[truelineno] == %s || linetype[truelineno] == %s || "
"linetype[truelineno] == %s || linetype[truelineno] == %s || ",
			   enablers[np->constant-GETS],
			   enablers[np->constant-GETS+2],
			   enablers[FROM-GETS], enablers[MANYFROM-GETS]);
	  } else if (np->constant == REINSTATE) {
	    (void) fprintf(fp,
"linetype[truelineno] == %s || linetype[truelineno] == %s || ",
			   enablers[np->constant-GETS],
			   enablers[np->constant-GETS+2]);
	  } else if (np->constant == COME_FROM) {
	    (void) fprintf(fp,
"linetype[truelineno] == %s || linetype[truelineno] == %s || "
"linetype[truelineno] == %s || ",
			   enablers[COME_FROM-GETS],
			   enablers[COMPUCOME-GETS],
			   enablers[GERUCOME-GETS]);
	  } else if (np->constant == NEXTFROMLABEL) {
	    (void) fprintf(fp,
"linetype[truelineno] == %s || linetype[truelineno] == %s || "
"linetype[truelineno] == %s || ",
			   enablers[NEXTFROMLABEL-GETS],
			   enablers[NEXTFROMEXPR-GETS],
			   enablers[NEXTFROMGERUND-GETS]);
	  } else {
	    (void) fprintf(fp, "linetype[truelineno] == %s || ",
			   enablers[np->constant-GETS]);
	  }
	}
	fprintf(fp, "0");
      }
      fprintf(fp,") {\n");
    }

    /* AIS: With this block placed here, you can't even have a comment
       after a TRY AGAIN line. Move it below the next check if this seems
       to be undesirable behaviour. */
    if(pasttryagain) /* AIS */
    {
      lose(E993, emitlineno, (char*)NULL);
    }

    if(flowoptimize && tn->initabstain && !tn->abstainable
       && tn->type != COMPUCOME && tn->type != COME_FROM &&
       tn->type != NEXT && tn->type != GERUCOME &&
       tn->type != NEXTFROMLABEL && tn->type != NEXTFROMEXPR &&
       tn->type != NEXTFROMGERUND) /* AIS */
      goto skipcomment; /* Look, a comment! We can completely skip all
			   degeneration of this statement (although with
			   -c, comments will appear in the degenerated
			   code in its place). The COMPUCOME condition is
			   because it is so weird. COME_FROM and NEXT are
			   exempted so labels are generated. */
    
    /* emit conditional-execution prefixes */
    if (tn->type != COME_FROM && tn->type != NEXTFROMLABEL)
	emit_guard(tn, fp);

    /* now emit the code for the statement body */
    switch(tn->type)
    {
    case GETS:
      /* AIS: variableconstants means GETS has been generalised */
      if(variableconstants)
      {
	revprexpr(tn->u.node->lval, fp, tn->u.node->rval);
	nodefree(tn->u.node);
	break;
      }
      
        /* Start of AIS optimization */
	np = tn->u.node;
	if(np->lval->opcode == SUB) np = np->lval;
	if(flowoptimize && Base == 2 && !opoverused && !variableconstants &&
	   (np->lval->opcode == TWOSPOT
	    || np->lval->opcode == HYBRID
	    || !(tn->u.node->lval->optdata & ~0xffffLU)))
	{
	  atom* op;
	  int ignorable = 1;
	  for(op = oblist; op < obdex; op++)
	  {
	    if(op->type == np->lval->opcode &&
	       (unsigned)op->intindex == np->lval->constant)
	    {
	      ignorable &= op->ignorable;
	    }
	  }
	  if(!ignorable)
	  { /* Variable can't be ignored, and expression must be in range */
	    (void) fprintf(fp,"\t");
	    prexpr(tn->u.node->lval, fp, 1);
	    (void) fprintf(fp, " = ");
	    prexpr(tn->u.node->rval, fp, 1);
	    (void) fprintf(fp, ";\n");
	    break;
	  }
	}
	/* End of AIS optimization */
	if(opoverused&&
	   (tn->u.node->lval->opcode==ONESPOT||
	    tn->u.node->lval->opcode==TWOSPOT)) /* AIS */
	{
	  (void) fprintf(fp,"\t");
	  ooprvar(tn->u.node->lval, fp, 1);
	  (void) fprintf(fp,".set(");
	  prexpr(tn->u.node->rval, fp, 1);
	  (void) fprintf(fp,",(void(*)())os%dspot%lu);\n",
			 (tn->u.node->rval->opcode==TWOSPOT)+1,
			 tn->u.node->rval->constant);
	}
	else if(!pickcompile)
	{
	  np = tn->u.node;
	  
	  if (np->lval->opcode != SUB) {
	    sp = np->lval;
	    (void) fprintf(fp,"\t(void) assign((char*)&");
	  }
	  else {
	    sp = np->lval->lval;
	    (void) fprintf(fp,"\t(void) assign(");
	  }
	  prvar(np->lval, fp, 1);
	  (void) fprintf(fp,", %s", nameof(sp->opcode, vartypes));
	  (void) fprintf(fp,", %s[%lu], ", nameof(sp->opcode, forgetbits),
			 sp->constant);
	  prexpr(np->rval, fp, 1);
	  (void) fprintf(fp,");\n");
	}
	else /* AIS: Added this case for the simpler PIC assignment rules */
	{
	  (void) fprintf(fp,"\tif(ignore%s%lu) ",
			 nameof(tn->u.node->lval->opcode,varstores),
			 tn->u.node->lval->constant);
	  prexpr(tn->u.node->lval, fp, 1);
	  (void) fprintf(fp, " = ");
	  prexpr(tn->u.node->rval, fp, 1);
	  (void) fprintf(fp, ";\n");
	}
	break;

    case RESIZE:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
	np = tn->u.node;
	dim = 0;
	for (sp = np->rval; sp; sp = sp->rval)
	  dim++;
	(void) fprintf(fp, "\tresize(");
	prvar(np->lval, fp, 1);
	(void) fprintf(fp, ", %s[%lu]", nameof(np->lval->opcode, forgetbits),
		       np->lval->constant);
	(void) fprintf(fp, ", %d", dim);
	for (sp = np->rval; sp; sp = sp->rval) {
	  (void) fprintf(fp, ", ");
	  prexpr(sp->lval, fp, 1);
        }
	(void) fprintf(fp, ");\n");
	break;

    case NEXT:
      /* Start of AIS optimization */
      if(tn->u.target>=1000 && tn->u.target<=1999 && pickcompile)
      {
	/* optimize syslib call on a PIC */
	(void) fprintf(fp, "\tsyslibopt%d();\n", tn->u.target);
	break;
      }
      if(tn->optversion)
      { /* optimizef has checked that this is a valid optimization */
	(void) fprintf(fp, "\tif(1 == ");
	prexpr(tn->u.node, fp, 1); /* frees optimizef's nodecopy */
	/* AIS: Everything now in one giant switch(), with some very strange
	   constructs (including ;{;} as a null statement; this makes
	   degenerating the code slightly easier) */
	(void) fprintf(fp, ") {pushnext(%d); skipto=%d; goto top;}} case %d:;{;\n",
		       (int)(tn - tuples + 1), tn->nexttarget, (int)(tn - tuples + 1));
	break;
      }
      /* End of AIS optimization */
      (void) fprintf(fp, /* same change as above (case rather than a label) */
		     "\tpushnext(%d); goto L%d;} case %d:;{;\n",
		     (int)(tn - tuples + 1), tn->u.target, (int)(tn - tuples + 1));
      break;

    case GO_BACK: /* By AIS */
      if(!multithread) lose(E405, emitlineno, (char*) NULL);
      (void) fprintf(fp, "\tchoiceback();\n");
      break;

    case GO_AHEAD: /* By AIS */
      if(!multithread) lose(E405, emitlineno, (char*) NULL);
      (void) fprintf(fp, "\tchoiceahead();\n");
      break;

    case RESUME:
	(void) fprintf(fp, "\tskipto = resume(");
	prexpr(tn->u.node, fp, 1);
	(void) fprintf(fp, "); goto top;\n");
	break;

    case FORGET:
	(void) fprintf(fp, "\tpopnext(");
	prexpr(tn->u.node, fp, 1);
	(void) fprintf(fp, ");\n");
	break;

    case STASH:
	for (np = tn->u.node; np; np = np->rval)
	    (void) fprintf(fp, "\tICKSTASH(%s, %lu, %s, %s%s);\n",
			   nameof(np->opcode, vartypes),
			   np->constant,
			   nameof(np->opcode, varstores),
			   /* AIS */(opoverused&&(np->opcode==ONESPOT||
						  np->opcode==TWOSPOT)?
			   "oo_":"0"),
			   /* AIS */(opoverused&&(np->opcode==ONESPOT||
						  np->opcode==TWOSPOT)?
			   nameof(np->opcode, varstores):"0"));
	break;

    case RETRIEVE:
	for (np = tn->u.node; np; np = np->rval)
	    (void) fprintf(fp, "\tICKRETRIEVE(%s, %lu, %s, %s, %s%s);\n",
			   nameof(np->opcode, varstores), np->constant,
			   nameof(np->opcode, vartypes),
			   nameof(np->opcode, forgetbits),
			   /* AIS */(opoverused&&(np->opcode==ONESPOT||
						  np->opcode==TWOSPOT)?
			   "oo_":"0"),
			   /* AIS */(opoverused&&(np->opcode==ONESPOT||
						  np->opcode==TWOSPOT)?
			   nameof(np->opcode, varstores):"0"));

	break;

    case IGNORE:
	for (np = tn->u.node; np; np = np->rval)
	    (void) fprintf(fp,"\tICKIGNORE(%s,%lu,%s) = TRUE;\n",
			   nameof(np->opcode, forgetbits),
			   np->constant,
			   nameof(np->opcode, varstores));
	break;

    case REMEMBER:
	for (np = tn->u.node; np; np = np->rval)
	    (void) fprintf(fp,"\tICKIGNORE(%s,%lu,%s) = FALSE;\n",
			   nameof(np->opcode, forgetbits),
			   np->constant,
			   nameof(np->opcode, varstores));
	break;

	/* All abstention code has been edited by AIS to allow for the new
	   abstention rules */
    case ABSTAIN:
      if(!pickcompile)
	(void) fprintf(fp, "\tif(!ICKABSTAINED(%d)) ICKABSTAINED(%d) = 1;\n", tn->u.target - 1, tn->u.target-1);
      else
	(void) fprintf(fp, "ICKABSTAINED(%d) = 1;\n", tn->u.target-1);
	break;

    case FROM:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL);
        (void) fprintf(fp, "\tICKABSTAINED(%d)+=", tn->u.target-1);
	tn->u.node->width = 32;
	prexpr(tn->u.node,fp, 1);
	(void) fprintf(fp, ";\n");
	break;

    case REINSTATE:
      if(!pickcompile)
	(void) fprintf(fp, "\tif(ICKABSTAINED(%d)) ICKABSTAINED(%d)--;\n", tn->u.target - 1, tn->u.target-1);
      else
	(void) fprintf(fp, "\tICKABSTAINED(%d)=0;\n", tn->u.target - 1);
	break;

    case ENABLE:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
        (void) fprintf(fp,
		       "\tint i;\n");
	for (np = tn->u.node; np; np = np->rval)
	{
	    (void) fprintf(fp,
			   "\n\tfor (i = 0; i < (int)(sizeof(linetype)/sizeof(int)); i++)\n");
	    if (np->constant == ABSTAIN) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2], enablers[FROM-GETS], enablers[MANYFROM-GETS]);
	    } else if (np->constant == REINSTATE) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2]);
	    } else if (np->constant == COME_FROM) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[COME_FROM-GETS], enablers[COMPUCOME-GETS], enablers[GERUCOME-GETS]);
	    } else if (np->constant == NEXTFROMLABEL) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[NEXTFROMLABEL-GETS], enablers[NEXTFROMGERUND-GETS], enablers[NEXTFROMEXPR-GETS]);
	    } else {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s)\n", enablers[np->constant-GETS]);
	    }
	    (void) fprintf(fp,
			   "\t\tif(abstained[i]) abstained[i]--;\n");
	}
	break;

    case DISABLE:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
        (void) fprintf(fp,
		       "\tint i;\n");
	for (np = tn->u.node; np; np = np->rval)
	{
	    (void) fprintf(fp,
			   "\n\tfor (i = 0; i < (int)(sizeof(linetype)/sizeof(int)); i++)\n");
	    if (np->constant == ABSTAIN) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2], enablers[FROM-GETS], enablers[MANYFROM-GETS]);
	    } else if (np->constant == REINSTATE) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2]);
	    } else if (np->constant == COME_FROM) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[COME_FROM-GETS], enablers[COMPUCOME-GETS], enablers[GERUCOME-GETS]);
	    } else if (np->constant == NEXTFROMLABEL) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[NEXTFROMLABEL-GETS], enablers[NEXTFROMGERUND-GETS], enablers[NEXTFROMEXPR-GETS]);
	    } else {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s)\n", enablers[np->constant-GETS]);
	    }
	    (void) fprintf(fp,
			   "\t\tif(!abstained[i]) abstained[i] = 1;\n");
	}
	break;

    case MANYFROM:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
        (void) fprintf(fp,
		       "\tint i;\n\tint j;\n\tj=");
	tn->u.node->lval->width=32;
	prexpr(tn->u.node->lval,fp, 1);
	(void) fprintf(fp,
		       ";\n");
	for (np = tn->u.node->rval; np; np = np->rval)
	{
	    (void) fprintf(fp,
			   "\n\tfor (i = 0; i < (int)(sizeof(linetype)/sizeof(int)); i++)\n");
	    if (np->constant == ABSTAIN) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s || linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2], enablers[FROM-GETS], enablers[MANYFROM-GETS]);
	    } else if (np->constant == REINSTATE) {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s || linetype[i] == %s)\n", enablers[np->constant-GETS], enablers[np->constant-GETS+2]);
	    } else {
	      (void) fprintf(fp,
			     "\t    if (linetype[i] == %s)\n", enablers[np->constant-GETS]);
	    }
	    (void) fprintf(fp,
			   "\t\tabstained[i] += j;\n");
	}
	break;

    case NEXTFROMEXPR:
    case NEXTFROMGERUND:
    case GERUCOME:
    case COMPUCOME: /* By AIS. Note that this doesn't even have balanced
		       braces; it's designed to work with COMPUCOME's
		       crazy guarding arrangements */
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
      fprintf(fp,"\t;} else goto CCF%d;\n",compucomecount);
      break;

    case GIVE_UP: /* AIS: Edited to allow for yuk */
      if(yukprofile||yukdebug) fprintf(fp, "\tYUKTERM;\n");
      if(multithread) fprintf(fp, "\tkillthread();\n");
      (void) fprintf(fp, "\treturn(0);\n");
      break;

    case TRY_AGAIN: /* By AIS */
      (void) fprintf(fp, "\tgoto ick_restart;\n    }\n");
      if(yukprofile||yukdebug) fprintf(fp, "    if(yukloop) goto ick_restart;\n");
      if(yukprofile||yukdebug) fprintf(fp, "    YUKTERM;\n");
      if(multithread) fprintf(fp, "\tkillthread();\n");
      (void) fprintf(fp, "    {\n\treturn(0);\n");
      /* because if TRY AGAIN is the last line, falling off the end isn't an error */
      pasttryagain=1; /* flag an error if we try any more commands */
      break;

    case WRITE_IN:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
      for (np = tn->u.node; np; np = np->rval) {
	  if (np->lval->opcode == TAIL || np->lval->opcode == HYBRID) {
	    (void) fprintf(fp,"\tbinin(");
	    prvar(np->lval, fp, 1);
	    (void) fprintf(fp, ", %s[%lu]",
			   nameof(np->lval->opcode, forgetbits),
			   np->lval->constant);
	    (void) fprintf(fp,");\n");
	  }
	  else {
	    if (np->lval->opcode != SUB) {
	      sp = np->lval;
	      (void) fprintf(fp,"\t(void) assign((char*)&");
	    }
	    else {
	      sp = np->lval->lval;
	      (void) fprintf(fp,"\t(void) assign(");
	    }
	    prvar(np->lval, fp, 1);
	    (void) fprintf(fp,", %s", nameof(sp->opcode, vartypes));
	    (void) fprintf(fp,", %s[%lu]", nameof(sp->opcode, forgetbits),
			   sp->constant);
	    (void) fprintf(fp,", pin());\n");
	  }
	}
	break;

    case READ_OUT:
      if(pickcompile) lose(E256, emitlineno, (char*) NULL); /* AIS */
	for (np = tn->u.node; np; np = np->rval)
	{
	  if (np->lval->opcode == TAIL || np->lval->opcode == HYBRID) {
	    (void) fprintf(fp,"\tbinout(");
	    prvar(np->lval, fp, 1);
	    (void) fprintf(fp,");\n");
	  }
	  else {
	    (void) fprintf(fp, "\tpout(");
	    prexpr(np->lval, fp, 1);
	    (void) fprintf(fp, ");\n");
	  }
	}
	break;

    case PIN: /* AIS, with some code borrowed from the GETS code */
      np = tn->u.node;
      (void) fprintf(fp, "\tTRISA = seq(~((");
      prexpr(np, fp, 0);
      (void) fprintf(fp, " >> 16) & 255));\n");
      (void) fprintf(fp, "\tTRISB = seq(~(");
      prexpr(np, fp, 0);
      (void) fprintf(fp, " >> 24));\n");
      (void) fprintf(fp, "\tPORTA = seq(~(");
      prexpr(np, fp, 0);
      (void) fprintf(fp, " & 255));\n");
      (void) fprintf(fp, "\tPORTB = seq(~((");
      prexpr(np, fp, 0);
      (void) fprintf(fp, " >> 8) & 255));\n");
      {
	atom* op;
	int ignorable = 1;
	for(op = oblist; op < obdex; op++)
	{
	  if(op->type == np->opcode &&
	     (unsigned)op->intindex == np->constant)
	  {
	    ignorable &= op->ignorable;
	  }
	}
	if(!ignorable)
	{ /* Variable can't be ignored, and expression must be in range */
	  (void) fprintf(fp,"\t");
	  prexpr(np, fp, 1);
	  (void) fprintf(fp, " = ");
	}
	else
	{
	  np = tn->u.node;
	  (void) fprintf(fp,"\tif(ignore%s%lu) ",
			 nameof(np->opcode,varstores),
			 np->constant);
	  prexpr(np, fp, 1);
	  (void) fprintf(fp, " = ");
	}
      }
      (void) fprintf(fp,"(TRISB<<24) | (TRISA<<16) | (PORTB<<8) | PORTA;\n");
      break;
	
    case SPLATTERED:
	dim = emitlineno - tn->lineno;
	if (tn->sharedline)
	    ++dim;
	(void) fprintf(fp, "\tlose(E000, %d, \"%s\");\n",
		       emitlineno, nice_text(textlines + tn->lineno, dim));
	break;

    case COME_FROM:
    case NEXTFROMLABEL: /* AIS */
	(void) fprintf(fp, "if(0) {C%ld: %s;}\n", (long)(tn-tuples+1),
		       tn->type==NEXTFROMLABEL ? "pushnext(truelineno+1)":"");
	/* AIS: Changed so all COME_FROMs have unique labels even if two
	   of them aim at the same line, and added the NEXT FROM case (which
	   invloves hiding COME FROM labels in an unreachable if()). */
	break;

    default:
	lose(E778, emitlineno, (char *)NULL);
	break;
    }

    if (tn->type != COME_FROM && tn->type != NEXTFROMLABEL)
	(void) fprintf(fp, "    }\n");

 skipcomment:
    
    if (tn->type == COMPUCOME || tn->type == GERUCOME ||
	tn->type == NEXTFROMEXPR || tn->type == NEXTFROMGERUND)
    { /* By AIS */
      (void) fprintf(fp,"    else goto CCF%d;\n",compucomecount);
      (void) fprintf(fp,"    ccfc++;\n");
      /* Note that due to the semantics of setjmp, this has to be written as 2
	 separate ifs. The MULTICOME macro expands to a non-multithreaded or
         multithreaded function for handling a COME FROM clash. */
      (void) fprintf(fp,"    if(ccfc==1||MULTICOME(%d,cjb))\n"
		     "\tif(setjmp(cjb) == 0) goto CCF%d;\n",
		     emitlineno, compucomecount);
      /* Of course, emitlineno is unlikely to be helpful! */
      if(tn->type == NEXTFROMEXPR || tn->type == NEXTFROMGERUND)
      {
	/* Stack up the statement we've NEXTed from */
	(void) fprintf(fp,"    pushnext(truelineno+1);\n");
      }
      (void) fprintf(fp,"    }\n");
    }

    /* AIS: Before any COMING FROM this line is done, we need to sort out
       ONCE and AGAIN situations, unless this line was a NEXT or GO_BACK.
       COME FROM is also excluded because it acts at the suckpoint, not
       at the place it's written in the program. */
    if (tn->onceagainflag != onceagain_NORMAL &&
	(tn->type != NEXT && tn->type != GO_BACK && tn->type != COME_FROM &&
	 tn->type != NEXTFROMLABEL))
    {
      /* See my comments against the NEXT code for more information.
	 This code is placed here so COME FROM ... ONCE constructs work
	 properly (the line is abstained if the COME FROM is reached in
	 execution, or its suckpoint is reached in execution). */
      fprintf(fp,"    oldabstain = ICKABSTAINED(%d);\n", (int)(tn - tuples));
      fprintf(fp,"    ICKABSTAINED(%d) = %s;\n",
	      (int)(tn - tuples),
	      tn->onceagainflag==onceagain_ONCE ? "oldabstain ? oldabstain : 1"
	      : "0");
    }

    /* AIS: This is where we start the COME FROM suckpoint code. */

    /* AIS: We need to keep track of how many COME FROMs are aiming here
       at runtime, if we don't have the very simple situation of no
       COMPUCOMEs and a single-thread program (in which case the check
       is done at compile-time by codecheck). Even without COMPUCOME,
       this can change in a multithread program due to abstentions. */
    if((tn->ncomefrom && multithread) || (tn->label && compucomesused)
       || gerucomesused)
      (void) fprintf(fp, "    ccfc = 0;\n");
    /* AIS: For NEXTING FROM this line */
    if(nextfromsused && tn->ncomefrom)
    {
      (void) fprintf(fp, "    truelineno = %d;\n", (int)(tn-tuples));
    }
    /*
     * If the statement that was just degenerated was a COME FROM target,
     * emit the code for the jump to the COME FROM.
     * AIS: Changed most of this to allow for multithreading.
     */
    while(tn->ncomefrom) /* acts as an if if singlethreading */
    {
      tuple* cf; /* local to this block */
      if(multithread || compucomesused) generatecfjump = 1;      
      cf = tuples+comefromsearch(tn,tn->ncomefrom)-1;
      if (yydebug || compile_only)
	(void) fprintf(fp,
		       "    /* line %03d is a suck point for the COME FROM "
		       "at line %03d */\n", tn->lineno, cf->lineno);
      if (cf->onceagainflag != onceagain_NORMAL)
      { /* Calculate ONCE/AGAIN when the suckpoint is passed */
	fprintf(fp,"    oldabstain = ICKABSTAINED(%d);\n", (int)(cf - tuples));
	fprintf(fp,"    ICKABSTAINED(%d) = %s;\n",
		(int)(cf - tuples),
		cf->onceagainflag==onceagain_ONCE ? "oldabstain ? oldabstain : 1"
		: "0");
      }      
      emit_guard(cf, fp);
      if(multithread || compucomesused)
	(void) fprintf(fp,
		       "\tccfc++;\tif(ccfc==1||MULTICOME(%d,cjb)) "
		       "if(setjmp(cjb) == 1) goto C%ld;\n    }\n",
		       emitlineno, (long)(cf-tuples+1));
      else /* optimize for the simple case */
	(void) fprintf(fp, "\tgoto C%ld;\n    }\n", (long)(cf-tuples+1));
      tn->ncomefrom--;
    }

    /* AIS: If the statement has a label, it might be a
       computed COME FROM target. Also check the flag that says this
       code is needed in a multithread non-COMPUCOME program.
       If (at runtime) ccfc is nonzero, we know cjb has already been set;
       otherwise, set it now. In the case of a multithread non-COMPUCOME
       program, the goto will just jump to a longjmp, switching to the
       one and only one COME FROM that hasn't been given its own thread. */
    if ((tn->label && compucomesused) || generatecfjump || gerucomesused)
    {
      if(compucomesused)
      {
	(void) fprintf(fp, "    skipto = %d;\n", tn->label);
      }
      if(gerucomesused || nextfromsused)
      {
	(void) fprintf(fp, "    truelineno = %d;\n", (int)(tn-tuples));
      }
      if(generatecfjump) (void) fprintf(fp, "    if(ccfc) goto CCF0;\n");
      if(compucomesused || gerucomesused)
      { /* check all the COMPUCOMES */
	(void) fprintf(fp, "    %sif(setjmp(cjb) == 0) goto CCF0;\n",
		       generatecfjump?"else ":"");
      }
      generatecfjump = 0;
      /* AIS: If NEXT FROM's used, this might be a NEXT return target.
	 Don't generate case labels for NEXT, as it has them already. */
      if(nextfromsused && tn->type != NEXT)
      {
	(void) fprintf(fp, "    case %u:;\n", (unsigned)(tn-tuples+1));
      }
    }

    /* AIS: Now we've finished the statement, let's switch to the next
       thread in a multithread program. */
    if(multithread) (void) fputs("    NEXTTHREAD;\n", fp);
}

/* AIS: Generate prototypes for slat expressions */
void emitslatproto(FILE* fp)
{
  node* np=firstslat;
  char* t="type32";
  while(np)
  {
    fprintf(fp,"%s og%lx(%s);\nvoid os%lx(%s, void(*)());\n",
	    t,(unsigned long)np,t,(unsigned long)np,t);
    np=np->nextslat;
  }
}

/* AIS: Generate bodies for slat expressions */
void emitslat(FILE* fp)
{
  node* np=firstslat;
  node* temp, *temp2;
  char* t="type32";
  while(np)
  {
    fprintf(fp,
	    "void os%lx(%s a, void(*f)())\n{\n  static int l=0;\n"
	    "  if(l)\n  {\n    if(!f)lose(E778, lineno, (char *)NULL);\n"
	    "    ((void(*)(%s,void(*)()))f)(a,0);\n    return;\n  }\n  l=1;\n",
	    (unsigned long)np,t,t);
    temp=cons(C_A, 0, 0);
    revprexpr(np->rval, fp, temp); /* revprexpr can't free */
    fprintf(fp,"  l=0;\n}\n");
    fprintf(fp,"%s og%lx(%s t)\n{\n  %s a;\n  static int l=0;\n"
	    "  if(l) return t;\n  l=1;\n  a=",
	    t,(unsigned long)np,t,t);
    prexpr(np->rval, fp, 0);
    fprintf(fp,";\n  l=0;\n  return a;\n}\n");
    np=np->nextslat;
  }
  np=firstslat;
  /* Note that the order in which the parser assembles nodes means
     that we have to free the nodes in reverse order so we don't
     free child nodes twice. */
  temp2=0;
  while(np)
  {
    temp=np->nextslat;
    np->nextslat=temp2; /* reverse the chain */
    temp2=np;
    np=temp;
  }
  np=temp2;
  while(np)
  {
    temp=np->nextslat;
    nodefree(np->rval);
    free(np);
    np=temp;
  }
}

/* feh.c ends here */