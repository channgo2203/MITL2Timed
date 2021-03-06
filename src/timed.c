/***** MITL2Timed : timed.c *****/
/* Written by Yuchen Zhou, College Park, USA                              */
/* Copyright (c) 2015  Yuchen Zhou                                        */
/* Based on alternative.c from ltl2ba written by Paul Gastin and Denis    */
/* Oddoux, France                                                         */
/*                                                                        */
/* This program is free software; you can redistribute it and/or modify   */
/* it under the terms of the GNU General Public License as published by   */
/* the Free Software Foundation; either version 2 of the License, or      */
/* (at your option) any later version.                                    */
/*                                                                        */
/* This program is distributed in the hope that it will be useful,        */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of         */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          */
/* GNU General Public License for more details.                           */
/*                                                                        */
/* You should have received a copy of the GNU General Public License      */
/* along with this program; if not, write to the Free Software            */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA*/
/*                                                                        */
/* Based on the translation algorithm by Gastin and Oddoux,               */
/* presented at the 13th International Conference on Computer Aided       */
/* Verification, CAV 2001, Paris, France.                                 */
/* Proceedings - LNCS 2102, pp. 53-65                                     */

#include "ltl2ba.h"

/********************************************************************\
|*              Structures and shared variables                     *|
\********************************************************************/

extern FILE *tl_out;
extern int tl_verbose, tl_stats, tl_simp_diff;

char **t_sym_table;
TAutomata *tAutomata;
int cCount; //clock count
int t_sym_size, t_sym_id = 0, t_node_size, t_clock_size;
// struct rusage tr_debut, tr_fin;
// struct timeval t_diff;

void merge_bin_timed(TAutomata *t1, TAutomata *t2, TAutomata *t, TAutomata *out);
void merge_timed(TAutomata *t1,TAutomata *t, TAutomata *out);
void merge_event_timed(TAutomata *, TAutomata *, TAutomata *, TAutomata *);

void create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p);
void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to);

void print_timed(TAutomata *t);
void print_sub_formula(Node *n, char* subform);

/********************************************************************\
|*              Generation of the timed automata                    *|
\********************************************************************/

int t_calculate_clock_size(Node *p) /* returns the number of clocks needed */
{
  switch(p->ntyp) {
  case AND:
  case OR:
    return(t_calculate_clock_size(p->lft) + t_calculate_clock_size(p->rgt)+ 1);
  case NOT:
    return(t_calculate_clock_size(p->lft) + 1);
  case PREDICATE:
    return 1;
  case U_OPER:
  case V_OPER:
    return(t_calculate_clock_size(p->lft) + t_calculate_clock_size(p->rgt) + 1);
  case EVENTUALLY_I:{
    float d = p->intvl[1] - p->intvl[0];
    short m = ceil(p->intvl[1]/d) + 1;
    return(t_calculate_clock_size(p->lft) + 2*m + 1);
  }
  default:
    return 0;
    break;
  }
}

int t_calculate_sym_size(Node *p) /* returns the number of predicates */
{
  switch(p->ntyp) {
  case AND:
  case OR:
  case U_OPER:
  case V_OPER:
    return(t_calculate_sym_size(p->lft) + t_calculate_sym_size(p->rgt));
  case NOT:
    return (t_calculate_sym_size(p->lft));
  case EVENTUALLY_I:
    return(t_calculate_sym_size(p->lft) + 1);
  case PREDICATE:
    return 1;
  default:
    return 0;
  }
}

int t_get_sym_id(char *s) /* finds the id of a predicate, or attributes one */
{
  int i;
  for(i=0; i<t_sym_id; i++) 
    if (!strcmp(s, t_sym_table[i])) 
      return i;
  t_sym_table[t_sym_id] = (char *) malloc(sizeof(char)*(strlen(s)+1));
  strcpy(t_sym_table[t_sym_id], s);
  return t_sym_id++;
}

void create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p){
  s->tstateId = (char *) malloc(sizeof(char)*(strlen(tstateId)));
  strcpy(s->tstateId, tstateId);
  s->inv = inv;

  s->buchi = buchi;
  
  s->input = input;
  s->inputNum = inputNum;
  s->output = output;  //output true
  s->sym = NULL;
  if (p){
    if (p->lft){
      char buff[40];
      buff[0] = '\0';
      print_sub_formula(p, buff);
      s->tstateId = (char *) realloc(s->tstateId, sizeof(char)*(strlen(s->tstateId) +strlen(buff)+3));
      strcat(s->tstateId, ": ");
      strcat(s->tstateId, buff);
      s->sym = new_set(3);
      clear_set(s->sym, 3);
      add_set(s->sym, t_get_sym_id(buff));
      if (output ==1 && strstr(s->tstateId, "Gen") == NULL) s->input[0] = 1 << t_get_sym_id(buff);
      else if (strstr(s->tstateId, "Gen") != NULL){
        s->input = (unsigned short *) 0;
        s->inputNum = 0;
        free(input);
      }
    }else{
      s->sym = new_set(3); //3 is symolic set. sym_set_size is used to determine the allocation size
      clear_set(s->sym, 3);
      add_set(s->sym, t_get_sym_id(p->sym->name));
    }
  }
}

CGuard *copy_cguard(CGuard *cg){
  if (!cg){
    return (CGuard *) 0;
  }else{
    CGuard *res = (CGuard *) malloc(sizeof(CGuard));

    switch (cg->nType){
      case AND:
        res->nType = AND;
        res->cCstr = (CCstr *)0;
        res->lft = copy_cguard(cg->lft);
        res->rgt = copy_cguard(cg->rgt);
        break;

      case OR:
        res->nType = OR;
        res->cCstr = (CCstr *)0;
        res->lft = copy_cguard(cg->lft);
        res->rgt = copy_cguard(cg->rgt);
        break;

      case START:
        res->nType = START;
        res->cCstr = (CCstr *)0;
        res->lft = copy_cguard(cg->lft);
        break;

      case STOP:
      {
        res->nType = STOP;
        res->cCstr = (CCstr *)0;
        res->lft = copy_cguard(cg->lft);
        res->rgt = copy_cguard(cg->rgt);
        break;
      }

      case PREDICATE:
        res->nType = PREDICATE;
        res->cCstr = (CCstr *) malloc(sizeof(CCstr));
        res->cCstr->bndry = cg->cCstr->bndry;
        res->cCstr->cIdx = cg->cCstr->cIdx;
        res->cCstr->gType = cg->cCstr->gType;
        res->lft = NULL;
        res->rgt = NULL;
        break;

      default:
        fatal("ERROR in copying CGuard!",(char *)0);
        return (CGuard *)0;
    }
    return res;
  }
}

void modify_cguard(CGuard *res, CGuard *cg){
  if (!cg){
    res = NULL;
    return;
  }else{

    switch (cg->nType){
      case AND:
        res->nType = AND;
        res->cCstr = (CCstr *)0;
        res->lft = (CGuard *)malloc(sizeof(CGuard));
        res->rgt = (CGuard *)malloc(sizeof(CGuard));
        modify_cguard(res->lft, cg->lft);
        modify_cguard(res->rgt, cg->rgt);
        break;

      case OR:
        res->nType = OR;
        res->cCstr = (CCstr *)0;
        res->lft = (CGuard *)malloc(sizeof(CGuard));
        res->rgt = (CGuard *)malloc(sizeof(CGuard));
        modify_cguard(res->lft, cg->lft);
        modify_cguard(res->rgt, cg->rgt);
        break;

      case START:
        res->nType = START;
        res->cCstr = (CCstr *)0;
        res->lft = (CGuard *)malloc(sizeof(CGuard));
        res->rgt = NULL;
        modify_cguard(res->lft, cg->lft);
        break;

      case STOP:
      {
        res->nType = STOP;
        res->cCstr = (CCstr *)0;
        res->lft = (CGuard *)malloc(sizeof(CGuard));
        res->rgt = (CGuard *)malloc(sizeof(CGuard));
        modify_cguard(res->lft, cg->lft);
        modify_cguard(res->rgt, cg->rgt);
        break;
      }

      case PREDICATE:
        res->nType = PREDICATE;
        res->cCstr = (CCstr *) malloc(sizeof(CCstr));
        res->cCstr->bndry = cg->cCstr->bndry;
        res->cCstr->cIdx = cg->cCstr->cIdx;
        res->cCstr->gType = cg->cCstr->gType;
        res->lft = NULL;
        res->rgt = NULL;
        break;

      default:
        fatal("ERROR in modifying CGuard!",(char *)0);
    }
  }
}

void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to){
  t->cIdx = new_set(4);
  clear_set(t->cIdx, 4);
  for (int i=0; i<clockNum ; i++){
    add_set(t->cIdx, cIdxs[i]);
  }
  t->cguard = copy_cguard(cguard);
  t->from = from;
  t->to = to;
}

void copy_ttrans(TTrans *t, TTrans *fromT){
  t->cIdx = new_set(4);
  copy_set(fromT->cIdx, t->cIdx, 4);
  t->cguard = copy_cguard(fromT->cguard);
  t->from = fromT->from;
  t->to = fromT->to;
}

TAutomata *build_timed(Node *p) /* builds an timed automaton for p */
{

  TAutomata *t1, *t2;
  
  TTrans *t = (TTrans *)0, *tmp, *tC = (TTrans *)0, *tG = (TTrans *)0;
  TState *s, *sC, *sG;
  TAutomata *tA, *tB; // tA is the output automata
  TAutomata *tOut;  //tOut is the output of automata merger

  char *stateName=NULL;
  unsigned short *input=NULL;
  CGuard *cguard=NULL;
  int *clockId=NULL;

  switch (p->ntyp) {
    case TRUE:
      s = (TState *) tl_emalloc(sizeof(TState)*1);
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(s, "T", (CGuard *) 0, (unsigned short*) 0, 0, 1, 0, NULL); //output true

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = (TTrans *)0;
      tA->tStates = s;
      tA->stateNum = 1;
      tA->eventNum = 0;
      tA->tEvents = NULL;
      break;

    case FALSE:
      s = (TState *) tl_emalloc(sizeof(TState)*1);
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(s, "F", (CGuard *) 0, (unsigned short*) 0, 0, 0, 0, NULL); //output true

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = (TTrans *)0;
      tA->tStates = s;
      tA->stateNum = 1;
      tA->eventNum = 0;
      tA->tEvents = NULL;
      break;

    case PREDICATE:
      t = emalloc_ttrans(2);  //2 states and 1 clock
      s = (TState *) tl_emalloc(sizeof(TState)*2);

      stateName= (char *) malloc (sizeof(char)*(strlen(p->sym->name))+4);
      sprintf(stateName, "p(%s)", p->sym->name);
      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 1 << t_get_sym_id(p->sym->name);
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(s, stateName, (CGuard *) 0, input, 1, 1, 0, p); //output true when p is true
      free(stateName);

      stateName= (char *) malloc (sizeof(char)*(strlen(p->sym->name))+5);
      sprintf(stateName, "!p(%s)", p->sym->name);
      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], stateName, (CGuard *) 0, input, 1, 0, 0, p); //output false when p is false
      free(stateName);

      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard *) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);

      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // reuse cguard and clockId
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);

      free_CGuard(cguard);
      free(clockId);

      cCount++;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 2;
      tA->eventNum = 0;
      tA->tEvents = NULL;
      break;

    case NOT:
      t1 = build_timed(p->lft);

      t = emalloc_ttrans(2);  //2 states 2 transitions and 1 clock
      s = (TState *) tl_emalloc(sizeof(TState)*2);

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b1;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(s, "p", (CGuard *) 0, input, 1, 0, 0, NULL); //output false when p is true

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b0;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], "!p", (CGuard *) 0, input, 1, 1, 0, NULL); //output true when p is false

      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard *) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);
      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // reuse clock and cguard
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);
      cCount++;

      free_CGuard(cguard);
      free(clockId);

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 2;
      tA->tEvents = NULL;
      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));

      // t1, tA freed inside merge function
      merge_timed(t1,tA,tOut);

      tA = tOut;
      break;

    case V_OPER:  // NOT ((NOT p) U (NOT q))
      t1= build_timed(p->lft);
      t2= build_timed(p->rgt);

      //create timed automata with 8 transitions;
      t = emalloc_ttrans(8); 
      s = (TState *) tl_emalloc(sizeof(TState)*4);

      input = (unsigned short *) malloc(sizeof(unsigned short)*2);
      input[0] = 0b10;
      input[1] = 0b11;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[0], "p", (CGuard *) 0, input, 2, 1, 1, NULL); //output 1 in p state

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b01;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], "!(!pq)", (CGuard *) 0, input, 1, 1, 1, NULL); //output 1 in !(!pq) state

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b01;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[2], "!pq", (CGuard *) 0, input, 1, 0, 0, NULL); //output 0 in !pq state


      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b00;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[3], "!p!q", (CGuard *) 0, input, 1, 0, 1, NULL); //output 0 in !p!q state

      // use tmp to iteratively edit the transition
      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard*) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);
      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // reuse cguard and clockId since it is copied inside the create_ttrans function
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);
      tmp = tmp->nxt;

      // (0 -> 2) : z > 0 | z := 0
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[2]);
      tmp = tmp->nxt;

      // (0 -> 3) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[3]);
      tmp = tmp->nxt;

      // (3 -> 0) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[0]);
      tmp = tmp->nxt;

      // (2 -> 3) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[2],  &s[3]);
      tmp = tmp->nxt;

      // (3 -> 2) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[2]);
      tmp = tmp->nxt;

      // (3 -> 1) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[1]);

      free_CGuard(cguard);
      free(clockId);

      cCount++;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 4;
      tA->tEvents = NULL;
      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      
      // t1, t2, tA freed inside merge function
      merge_bin_timed(t1,t2,tA,tOut);
      tA = tOut;
      break;

    case U_OPER: 
      t1= build_timed(p->lft);
      t2= build_timed(p->rgt);

      //create timed automata with 8 transition and 1 clock;
      t = emalloc_ttrans(8); 
      s = (TState *) tl_emalloc(sizeof(TState)*4);

      input = (unsigned short *) malloc(sizeof(unsigned short)*2);
      input[0] = 0b01;
      input[1] = 0b00;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[0], "!p", (CGuard *) 0, input, 2, 0, 1, NULL); //output 0 in !p state

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b10;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], "!(p!q)", (CGuard *) 0, input, 1, 0, 1, NULL); //output 0 in !(p!q) state

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b10;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[2], "p!q", (CGuard *) 0, input, 1, 1, 0, NULL); //output 1 in p!q state


      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b11;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[3], "pq", (CGuard *) 0, input, 1, 1, 1, NULL); //output 1 in pq state

      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard*) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);
      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // reuse cguard and clockId since create_ttrans copied them inside
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);
      tmp = tmp->nxt;

      // (0 -> 2) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[2]);
      tmp = tmp->nxt;

      // (0 -> 3) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[3]);
      tmp = tmp->nxt;

      // (3 -> 0) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[0]);
      tmp = tmp->nxt;

      // (2 -> 3) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[2],  &s[3]);
      tmp = tmp->nxt;

      // (3 -> 2) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[2]);
      tmp = tmp->nxt;

      // (3 -> 1) : z > 0 | z := 0
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[3],  &s[1]);

      free_CGuard(cguard);
      free(clockId);

      cCount++;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 4;
      tA->tEvents = NULL;
      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      
      // t1, t2, tA freed inside merge function
      merge_bin_timed(t1,t2,tA,tOut);

      tA = tOut;
      break;

    case EVENTUALLY_I: {
      t1= build_timed(p->lft);

      //create prediction generator with 2m transition and 2m clock;
      // TODO: how to add the clock reset beyond b (5)
      // TODO: add initial node for eventually automata (3)
      float d = p->intvl[1] - p->intvl[0];
      short m = ceil(p->intvl[1]/d) + 1;
      tG = emalloc_ttrans(2*m+2); 
      sG = (TState *) tl_emalloc(sizeof(TState)*(2*m + 1));

      for (int i =0; i< m; i++){
        // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
        stateName= (char *) malloc (sizeof(char)*(strlen("Gen_"))+3);
        sprintf(stateName, "Gen_%d", 2*i+1);
        input = (unsigned short *) malloc(sizeof(unsigned short)*1);

        cguard = (CGuard*) malloc(sizeof(CGuard));
        cguard->nType = START;
        cguard->lft = (CGuard*) malloc(sizeof(CGuard));
        cguard->lft->nType = PREDICATE;
        cguard->lft->cCstr = (CCstr * ) malloc(sizeof(CCstr));
        cguard->lft->cCstr->gType = START;
        cguard->lft->cCstr->cIdx = cCount+2*i;
        cguard->lft->lft = NULL;
        cguard->lft->rgt = NULL;
        cguard->rgt = NULL;

        create_tstate(&sG[0+i*2], stateName, cguard, input, 1, 0, 0, p); //output 0 first stage
        free(stateName);

        stateName= (char *) malloc (sizeof(char)*(strlen("Gen_"))+3);
        sprintf(stateName, "Gen_%d", 2*i+2);
        input = (unsigned short *) malloc(sizeof(unsigned short)*1);

        cguard = (CGuard*) malloc(sizeof(CGuard));
        cguard->nType = START;
        cguard->lft = (CGuard*) malloc(sizeof(CGuard));
        cguard->lft->nType = PREDICATE;
        cguard->lft->cCstr = (CCstr * ) malloc(sizeof(CCstr));
        cguard->lft->cCstr->gType = START;
        cguard->lft->cCstr->cIdx = cCount+2*i + 1;
        cguard->lft->lft = NULL;
        cguard->lft->rgt = NULL;
        cguard->rgt = NULL;
        // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
        create_tstate(&sG[1+i*2], stateName, cguard, input, 1, 1, 0, p); //output 1 in second stage
        free(stateName);
      }

      // add a state to disable all clocks first
      stateName= (char *) malloc (sizeof(char)*(strlen("Gen0")));
      sprintf(stateName, "Gen0");
      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      cguard = (CGuard*) malloc(sizeof(CGuard));

      cguard->nType = STOP;
      cguard->lft = (CGuard*) malloc(sizeof(CGuard));
      cguard->lft->nType = PREDICATE;
      cguard->lft->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->lft->cCstr->gType = STOP;
      cguard->lft->cCstr->cIdx = cCount;
      cguard->lft->lft = NULL;
      cguard->lft->rgt = NULL;
      cguard->rgt = (CGuard*) malloc(sizeof(CGuard));
      cguard->rgt->nType = PREDICATE;
      cguard->rgt->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->rgt->cCstr->gType = STOP;
      cguard->rgt->cCstr->cIdx = cCount+2*m-1;
      cguard->rgt->lft = NULL;
      cguard->rgt->rgt = NULL;

      create_tstate(&sG[2*m], stateName, cguard, input, 1, NULLOUT, 0, p); //output 0 
      free(stateName);
      
      tmp=tG;

      // sharing allocation and some of the data
      clockId = (int *) malloc(sizeof(int)*1);

      cguard = (CGuard*) malloc(sizeof(CGuard));
      cguard->nType = PREDICATE;
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->gType = GREATEREQUAL;
      cguard->cCstr->bndry = d;

      for (int i = 0; i < m; i++){
        // (2*i -> 2*i+1) :  * | yi (2i+1):= 0
        // reset which clock
        clockId[0] = cCount+i*2+1;
        // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
        create_ttrans(tmp, (CGuard*) 0, clockId, 1, &sG[2*i],  &sG[2*i+1]);
        tmp = tmp->nxt;

        // (2*i+1 -> 2*i+2) :  yi>=b-a | x_i+1 (2i+2):= 0 for i!=m-1
        // (2*m-1 -> 0) :  yi>=b-a | x_0 (0):= 0 for i = m-1
        cguard->cCstr->cIdx = cCount+i*2+1;

        if (i != m-1){
          
          // reset which clock
          clockId[0] = cCount+i*2+2;
          // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
          create_ttrans(tmp, cguard, clockId, 1, &sG[2*i+1],  &sG[2*i+2]);
          tmp = tmp->nxt;

        } else {

          // reset which clock
          clockId[0] = cCount;
          // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
          create_ttrans(tmp, cguard, clockId, 1, &sG[2*i+1],  &sG[0]);
          tmp = tmp->nxt;
        }

      }

      // add the initial transition to generator state 0 and 1
      // reset which clock
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, (CGuard *) 0, clockId, 1, &sG[2*m],  &sG[0]);
      tmp = tmp->nxt;

      clockId[0] = cCount+1;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, (CGuard *) 0, clockId, 1, &sG[2*m],  &sG[1]);
      tmp = tmp->nxt;

      free(clockId);
      free_CGuard(cguard);

      // prediction checker
      tC = emalloc_ttrans(4*m+3);
      sC = (TState *) tl_emalloc(sizeof(TState)*(3*m+2));

      for (int i = 0; i < m; i++){
        // s1 (!p: *) -- y1 < b
        stateName= (char *) malloc (sizeof(char)*(strlen("CHK_")+3));
        sprintf(stateName, "CHK_%d", 3*i+1);
        input = (unsigned short *) malloc(sizeof(unsigned short)*1);
        input[0] = 0;
        cguard = (CGuard *) malloc(sizeof(CGuard)); 
        cguard->nType = PREDICATE;  
        cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
        cguard->cCstr->cIdx = cCount+2*i+1;
        cguard->cCstr->gType = LESSEQUAL; 
        cguard->cCstr->bndry = p->intvl[1];
        // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
        create_tstate(&sC[0+3*i], stateName, cguard, input, 1, NULLOUT, 0, p); //output * in s1 state
        free(stateName);

        if (i != m-1){
          //s2 (p: *) -- x2 < a
          stateName= (char *) malloc (sizeof(char)*(strlen("CHK_")+3));
          sprintf(stateName, "CHK_%d", 3*i+2);
          input = (unsigned short *) malloc(sizeof(unsigned short)*1);
          input[0] = 1;
          cguard = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->nType = PREDICATE;  
          cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->cCstr->cIdx = cCount+2*i+2; // x1 clock is 2*i, x2 is 2*i +2
          cguard->cCstr->gType = LESSEQUAL; 
          cguard->cCstr->bndry = p->intvl[0];
          // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
          create_tstate(&sC[1+3*i], stateName, cguard, input, 1, NULLOUT, 0, p); //output 0 in s2 state
          free(stateName);

          //s3 (!p: *) -- z < d && x2 < a
          stateName= (char *) malloc (sizeof(char)*(strlen("CHK_")+3));
          sprintf(stateName, "CHK_%d", 3*i+3);
          input = (unsigned short *) malloc(sizeof(unsigned short)*1);
          input[0] = 0;
          cguard = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->nType = AND;
          cguard->lft = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->lft->nType = PREDICATE;
          cguard->lft->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->lft->cCstr->cIdx = cCount+2*i+2; // x1 clock is 2*i, x2 is 2*i +2
          cguard->lft->cCstr->gType = LESSEQUAL; 
          cguard->lft->cCstr->bndry = p->intvl[0];
          cguard->rgt = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->rgt->nType = PREDICATE;  
          cguard->rgt->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->rgt->cCstr->cIdx = cCount+2*m; // z clock is 2*m
          cguard->rgt->cCstr->gType = LESSEQUAL; 
          cguard->rgt->cCstr->bndry = d;
          // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
          create_tstate(&sC[2+3*i], stateName, cguard, input, 1, NULLOUT, 0, p); //output * in s3 state
          free(stateName);
        }else{
          //s2 (p: *) -- x1 < a
          stateName= (char *) malloc (sizeof(char)*(strlen("CHK_")+3));
          sprintf(stateName, "CHK_%d", 3*i+2);
          input = (unsigned short *) malloc(sizeof(unsigned short)*1);
          input[0] = 1;
          cguard = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->nType = PREDICATE;  
          cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->cCstr->cIdx = cCount; // x1 clock is 0
          cguard->cCstr->gType = LESSEQUAL; 
          cguard->cCstr->bndry = p->intvl[0];
          // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
          create_tstate(&sC[1+3*i], stateName, cguard, input, 1, NULLOUT, 0, p); //output 0 in s2 state
          free(stateName);

          //s3 (!p: *) -- z < d && x2 < a
          stateName= (char *) malloc (sizeof(char)*(strlen("CHK_")+3));
          sprintf(stateName, "CHK_%d", 3*i+3);
          input = (unsigned short *) malloc(sizeof(unsigned short)*1);
          input[0] = 0;
          cguard = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->nType = AND;
          cguard->lft = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->lft->nType = PREDICATE;
          cguard->lft->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->lft->cCstr->cIdx = cCount; // x1 clock is 0
          cguard->lft->cCstr->gType = LESSEQUAL; 
          cguard->lft->cCstr->bndry = p->intvl[0];
          cguard->rgt = (CGuard *) malloc(sizeof(CGuard)); 
          cguard->rgt->nType = PREDICATE;  
          cguard->rgt->cCstr = (CCstr *) malloc(sizeof(CCstr));
          cguard->rgt->cCstr->cIdx = cCount+2*m; // z clock is 2*m
          cguard->rgt->cCstr->gType = LESSEQUAL; 
          cguard->rgt->cCstr->bndry = d;
          // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
          create_tstate(&sC[2+3*i], stateName, cguard, input, 1, NULLOUT, 0, p); //output * in s3 state
          free(stateName);
        }
      }

      // add CHK00 and CHK01 state to link to s1 and s2 s3 first layer
      stateName= (char *) malloc (sizeof(char)*(strlen("CHK00")));
      sprintf(stateName, "CHK00");
      input = (unsigned short *) malloc(sizeof(unsigned short)*2);
      input[0] = 1;
      input[1] = 0;
      cguard = (CGuard *) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount; // x1 clock is 0
      cguard->cCstr->gType = LESSEQUAL; 
      cguard->cCstr->bndry = p->intvl[0];
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&sC[3*m], stateName, cguard, input, 2, NULLOUT, 0, p); //output 0 in s2 state
      free(stateName);

      stateName= (char *) malloc (sizeof(char)*(strlen("CHK01")));
      sprintf(stateName, "CHK01");
      input = (unsigned short *) malloc(sizeof(unsigned short)*2);
      input[0] = 1;
      input[1] = 0;
      cguard = (CGuard *) malloc(sizeof(CGuard));
      cguard->nType = PREDICATE;
      cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount+1; // y1 clock is 1
      cguard->cCstr->gType = LESSEQUAL;
      cguard->cCstr->bndry = p->intvl[0];
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&sC[3*m+1], stateName, cguard, input, 2, NULLOUT, 0, p); //output 0 in s2 state
      free(stateName);

      tmp=tC;

      // sharing mem allocation and some of the data
      cguard = (CGuard*) malloc(sizeof(CGuard));
      cguard->cCstr = (CCstr *) malloc(sizeof(CCstr));
      clockId = (int *) malloc(sizeof(int)*1);

      for (int i = 0; i < m; i++){
        
        // (3*i -> 3*i+1) :  yi >=b | *
        cguard->nType = PREDICATE;
        cguard->cCstr->cIdx = cCount+2*i+1;
        cguard->cCstr->gType = GREATEREQUAL; 
        cguard->cCstr->bndry = p->intvl[1];
        
        // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
        create_ttrans(tmp, cguard, (int *) 0, 0, &sC[3*i],  &sC[3*i+1]);
        tmp = tmp->nxt;

        if (i != m-1){
          // (3*i+1 -> 3*i+3) :  xi+1>=a | * for i!=m-1
          cguard->nType = PREDICATE;
          cguard->cCstr->cIdx = cCount+i*2+2;
          cguard->cCstr->gType = GREATEREQUAL; 
          cguard->cCstr->bndry = p->intvl[0];
          // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
          create_ttrans(tmp, cguard, (int *) 0, 0, &sC[3*i+1],  &sC[3*i+3]);
        } else {
          // (3*m-2 -> 0) :  x1>=a | * for i = m-1
          cguard->nType = PREDICATE;
          cguard->cCstr->cIdx = cCount;
          cguard->cCstr->gType = GREATEREQUAL; 
          cguard->cCstr->bndry = p->intvl[0];
          // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
          create_ttrans(tmp, cguard, (int *) 0, 0, &sC[3*i+1],  &sC[0]);
        }

        tmp = tmp->nxt;

        // (3*i+1 -> 3*i+2) :  * | z (2m):= 0 
        // reset which clock
        clockId[0] = cCount+2*m;
        // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
        create_ttrans(tmp, (CGuard*) 0, clockId, 1, &sC[3*i+1],  &sC[3*i+2]);
        tmp = tmp->nxt;

        // (3*i+2 -> 3*i+1) :  * | *
        // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
        create_ttrans(tmp, (CGuard*) 0, (int *) 0, 0, &sC[3*i+2],  &sC[3*i+1]);
        tmp = tmp->nxt;
      }

      // (3*m-> 0) :  xi >=a | *
      cguard->nType = PREDICATE;
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATEREQUAL;
      cguard->cCstr->bndry = p->intvl[0];
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, (int *) 0, 0, &sC[3*m],  &sC[0]);
      tmp = tmp->nxt;

      // (3*m+1-> 1) :  yi >=a | *
      cguard->nType = PREDICATE;
      cguard->cCstr->cIdx = cCount+1;
      cguard->cCstr->gType = GREATEREQUAL;
      cguard->cCstr->bndry = p->intvl[0];
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, (int *) 0, 0, &sC[3*m+1],  &sC[1]);
      tmp = tmp->nxt;

      // (3*m+1-> 2) :  yi >=a | z (2m): = 0
      cguard->nType = PREDICATE;
      cguard->cCstr->cIdx = cCount+1;
      cguard->cCstr->gType = GREATEREQUAL; 
      cguard->cCstr->bndry = p->intvl[0];
      //reset which clock
      clockId[0] = cCount+2*m;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 0, &sC[3*m+1],  &sC[2]);

      free(clockId);
      free_CGuard(cguard);

      cCount += 2*m + 1;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = tG;
      tA->tStates = sG;
      tA->stateNum = 2*m+1;
      tA->tEvents = NULL;

      tB = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tB->tTrans = tC;
      tB->tStates = sC;
      tB->stateNum = 3*m+2;
      tB->tEvents = NULL;
      
      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));

      // t1, tA, tB freed inside merge function
      merge_event_timed(t1,tA,tB,tOut);
      tA = tOut;
      break;
    }

    case AND:
      t1= build_timed(p->lft);
      t2= build_timed(p->rgt);

      t = emalloc_ttrans(2); 
      s = (TState *) tl_emalloc(sizeof(TState)*2);


      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b11;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[0], "and", (CGuard *) 0, input, 1, 1, 0, NULL); //output 1 in pq state

      input = (unsigned short *) malloc(sizeof(unsigned short)*3);
      input[0] = 0b10;
      input[1] = 0b01;
      input[2] = 0b00;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], "! and", (CGuard *) 0, input, 3, 0, 0, NULL); //output 0 in other state

      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard*) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);
      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // Sharing cguard and clock resets
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);

      free_CGuard(cguard);
      free(clockId);

      cCount++;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 2;
      tA->tEvents = NULL;

      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      merge_bin_timed(t1,t2,tA,tOut);
      // t1, t2, tA freed inside merge function
      tA = tOut;
      break;

    case OR:
      t1= build_timed(p->lft);
      t2= build_timed(p->rgt);

      t = emalloc_ttrans(2); 
      s = (TState *) tl_emalloc(sizeof(TState)*2);

      input = (unsigned short *) malloc(sizeof(unsigned short)*1);
      input[0] = 0b00;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[0], "or", (CGuard *) 0, input, 1, 0, 0, NULL); //output 0 in !p!q state

      input = (unsigned short *) malloc(sizeof(unsigned short)*3);
      input[0] = 0b10;
      input[1] = 0b01;
      input[2] = 0b11;
      // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
      create_tstate(&s[1], "! or", (CGuard *) 0, input, 3, 1, 0, NULL); //output 1 in other state
 
      tmp=t;
      // (0 -> 1) : z > 0 | z := 0
      cguard = (CGuard*) malloc(sizeof(CGuard)); 
      cguard->nType = PREDICATE;  
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATER; 
      cguard->cCstr->bndry = 0;
      //reset which clock
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[1]);

      tmp = tmp->nxt;

      // (1 -> 0) : z > 0 | z := 0
      // sharing cguard and clock resets
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[1],  &s[0]);

      free(clockId);
      free_CGuard(cguard);

      cCount++;

      tA = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      tA->tTrans = t;
      tA->tStates = s;
      tA->stateNum = 2;
      tA->tEvents = NULL;

      tOut = (TAutomata *) tl_emalloc(sizeof(TAutomata));
      // t1, tA, tB freed inside merge function
      merge_bin_timed(t1,t2,tA,tOut);
      tA = tOut;
      break;

    default:
      break;
  }

  return(tA);
}

// TODO: simplify invariants if they are looking at same clock (4)
void merge_inv(CGuard *target, CGuard *lft, CGuard *rgt, CGuard *top){
  if (lft && rgt && top) {
    target->nType = AND;
    target->cCstr = (CCstr *)0;
    target->lft = (CGuard*) malloc(sizeof(CGuard));
    target->lft->nType = AND;
    target->lft->cCstr = (CCstr *)0;
    target->lft->lft = copy_cguard(lft);
    target->lft->rgt = copy_cguard(rgt);
    target->rgt = copy_cguard(top);
  }else if (lft && rgt){
    target->nType = AND;
    target->cCstr = (CCstr *)0;
    target->lft = copy_cguard(lft);
    target->rgt = copy_cguard(rgt);
  }else if (lft && top){
    target->nType = AND;
    target->cCstr = (CCstr *)0;
    target->lft = copy_cguard(lft);
    target->rgt = copy_cguard(top);
  }else if (rgt && top){
    target->nType = AND;
    target->cCstr = (CCstr *)0;
    target->lft = copy_cguard(rgt);
    target->rgt = copy_cguard(top);
  }else if (lft){
    modify_cguard(target, lft);
  }else if (rgt){
    modify_cguard(target, rgt);
  }else if (top){
    modify_cguard(target, top);
  }
}

/********************************************************************\
|*             Linking the Timed Automata                           *|
\********************************************************************/
void merge_ttrans_array(TTrans **t, int transNum, TTrans *tOut, TState *from, TState *to){
  tOut->cIdx = new_set(4);
  clear_set(tOut->cIdx, 4);

  int isNotFirst=0;
  tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
  CGuard *tmp=NULL;
  for (int l = 0; l< transNum; l++){
    if (t[l]){
      merge_sets(tOut->cIdx, t[l]->cIdx,4);
      if (t[l]->cguard && isNotFirst==0){
        merge_inv(tOut->cguard, t[l]->cguard, NULL,NULL);
      }else if (t[l]->cguard){
        tmp = copy_cguard(tOut->cguard);
        merge_inv(tOut->cguard, tmp , NULL ,t[l]->cguard);
      }
      isNotFirst+=(t[l]->cguard != NULL);
    }

  }
  free_CGuard(tmp);
  if (isNotFirst==0) tOut->cguard = (CGuard*) 0;

  tOut->from = from;
  // fprintf(tl_out, "%s -> %s\n",from->tstateId, to->tstateId );
  tOut->to = to;
}

void merge_ttrans(TTrans *t1, TTrans *t2, TTrans *tt, TTrans *tOut, TState *from, TState *to){
  if (t1 && t2 && tt){
    tOut->cIdx = new_set(4);
    clear_set(tOut->cIdx, 4);
    do_merge_sets(tOut->cIdx, t1->cIdx,t2->cIdx,4);
    merge_sets(tOut->cIdx, tt->cIdx,4);
    
    if (!t1->cguard && !t2->cguard && !tt->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, t1->cguard, t2->cguard, tt->cguard);
    }
  }else if (t1 && tt) {
    tOut->cIdx = new_set(4);
    clear_set(tOut->cIdx, 4);
    do_merge_sets(tOut->cIdx, t1->cIdx,tt->cIdx,4);

    if (!t1->cguard && !tt->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, t1->cguard, NULL, tt->cguard);
    }
  }else if (t2 && tt) {
    tOut->cIdx = new_set(4);
    clear_set(tOut->cIdx, 4);
    do_merge_sets(tOut->cIdx, t2->cIdx,tt->cIdx,4);

    if (!t2->cguard && !tt->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, t2->cguard, NULL, tt->cguard);
    }
  }else if (t1) {
    tOut->cIdx = dup_set(t1->cIdx, 4);

    if (!t1->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, t1->cguard, NULL, NULL);
    }
  }else if (t2){
    tOut->cIdx = dup_set(t2->cIdx, 4);

    if (!t2->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, t2->cguard, NULL, NULL);
    }
  }else if (tt){
    tOut->cIdx = dup_set(tt->cIdx, 4);

    if (!tt->cguard){
      tOut->cguard = (CGuard *) 0;
    }else{
      tOut->cguard = (CGuard *) malloc(sizeof(CGuard));
      merge_inv(tOut->cguard, tt->cguard, NULL, NULL);
    }
  }else{
    printf("ERROR! in merge_ttrans!");
  }
  tOut->from = from;
  tOut->to = to;
}

void print_sub_formula(Node *n, char* subform) {
  if (!n) return;
  char buff[40];
  buff[0] = '\0';

  switch(n->ntyp) {
  case OR:  
    strcat(subform, "("); 
    print_sub_formula(n->lft, subform);
    strcat(subform, " || "); 
    print_sub_formula(n->rgt, subform);
    strcat(subform, ")");
    break;
  case AND:
    strcat(subform, "(");
    print_sub_formula(n->lft, subform);
    strcat(subform, " && ");
    print_sub_formula(n->rgt, subform);
    strcat(subform, ")");
    break;
  case U_OPER:
    strcat(subform, "(");
    print_sub_formula(n->lft, subform);
    strcat(subform, " U ");
    print_sub_formula(n->rgt, subform);
    strcat(subform, ")");
    break;
  case V_OPER:
    strcat(subform, "(");
    print_sub_formula(n->lft, subform);
    strcat(subform, " V ");
    print_sub_formula(n->rgt, subform);
    strcat(subform, ")");
    break;
#ifdef NXT
  case NEXT:
    strcat(subform, "X");
    strcat(subform, " (");
    print_sub_formula(n->lft, subform);
    strcat(subform, ")");
    break;
#endif
// #ifdef TIMED
  case EVENTUALLY_I:
    strcat(subform, "<>_I");
    strcat(subform, " (");
    print_sub_formula(n->lft, subform);
    strcat(subform, ")");
    break;
// #endif
  case NOT:
    strcat(subform, "!");
    strcat(subform, " (");
    print_sub_formula(n->lft, subform);
    strcat(subform, ")");
    break;
  case FALSE:
    strcat(subform, "false");
    break;
  case TRUE:
    strcat(subform, "true");
    break;
  case PREDICATE:
    sprintf(buff, "%s", n->sym->name);
    strcat(subform, buff);
    break;
  case -1:
    strcat(subform, " D ");
    break;
  default:
    printf("Unknown token: ");
    tl_explain(n->ntyp);
    break;
  }
}

void merge_event_timed(TAutomata *t1, TAutomata *tB, TAutomata *tA, TAutomata *out){
  // tB is the generator and tA is the checker. t1 is the subformula automata
  // merge the input automata with the checker and generate new output from generator
  //
  int *t1Sym = dup_set(t1->tStates[0].sym, 3);
  merge_timed(t1, tA, out);

  const int numOfState = out->stateNum + tB->stateNum;

  TState *s = (TState *) tl_emalloc(sizeof(TState)*numOfState);

  // copy state in tB to tOut->state
  for (int i=0; i< tB->stateNum; i++){
    s[i].inv = copy_cguard(tB->tStates[i].inv);
    s[i].input = (unsigned short*) malloc(sizeof(unsigned short)*tB->tStates[i].inputNum);
    for (int j=0; j< tB->tStates[i].inputNum; j++){
      s[i].input[j] = tB->tStates[i].input[j];
    }

    create_tstate(&s[i], tB->tStates[i].tstateId, s[i].inv, s[i].input, tB->tStates[i].inputNum, tB->tStates[i].output, tB->tStates[i].buchi, NULL);
    // gen0 have sym but NULLOUT
    // if (s[i].output != NULLOUT){ 
    s[i].sym = dup_set(tB->tStates[0].sym, 3);
    // }
  }

  // create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p)
  for (int i=0; i < out->stateNum; i++){
    s[tB->stateNum+i].inv = copy_cguard(out->tStates[i].inv);
    s[tB->stateNum+i].input = (unsigned short*) malloc(sizeof(unsigned short)*out->tStates[i].inputNum);
    for (int j=0; j< out->tStates[i].inputNum; j++){
      s[tB->stateNum+i].input[j] = out->tStates[i].input[j];
    }

    create_tstate(&s[tB->stateNum+i], out->tStates[i].tstateId, s[tB->stateNum+i].inv, s[tB->stateNum+i].input, out->tStates[i].inputNum, out->tStates[i].output, out->tStates[i].buchi, NULL);
    if (s[tB->stateNum+i].output == NULLOUT && out->tStates[i].sym == NULL){
      s[tB->stateNum+i].sym = dup_set(t1Sym,3);
      // s[tB->stateNum+i].sym = new_set(3);
      // clear_set(s[tB->stateNum+i].sym , 3);
      // do_merge_sets(s[tB->stateNum+i].sym , tA->tStates[0].sym, t1->tStates[0].sym, 3);
    }else if (s[tB->stateNum+i].output == NULLOUT){
      s[tB->stateNum+i].sym = dup_set(out->tStates[i].sym, 3);
    }
  }

  tfree(t1Sym);

  TTrans *tt = tB->tTrans;
  out->tEvents = NULL;
  while (tt->nxt) {
    if (strstr(tt->from->tstateId, "Gen0")!=NULL && out->tEvents == NULL){
      out->tEvents=tt;
    }
    tt->to = &s[tt->to - &tB->tStates[0]];
    tt->from = &s[tt->from - &tB->tStates[0]];
    tt = tt->nxt;
  }
  if (strstr(tt->from->tstateId, "Gen0")!=NULL && out->tEvents == NULL){
    out->tEvents=tt;
  }
  tt->to = &s[tt->to - &tB->tStates[0]];
  tt->from = &s[tt->from - &tB->tStates[0]];

  tt->nxt = out->tTrans;
  tt = tt->nxt;
  while (tt->nxt) {
    tt->to = &s[tB->stateNum + tt->to-&out->tStates[0]];
    tt->from = &s[tB->stateNum + tt->from-&out->tStates[0]];
    tt = tt->nxt;
  }
  tt->to = &s[tB->stateNum + tt->to-&out->tStates[0]];
  tt->from = &s[tB->stateNum + tt->from-&out->tStates[0]];

  free_tstate(out->tStates, out->stateNum);
  free_tstate(tB->tStates, tB->stateNum);

  out->tTrans = tB->tTrans;
  out->tStates = s;
  out->eventNum = out->stateNum+1;
  out->stateNum = numOfState;

  tfree(tB);
}

int compare_input (TState *t1, TState *t2){
  int *syms;
  if (!t1->sym || !t2->sym) return 1;
  if (!empty_intersect_sets(t1->sym, t2->sym,3))
    syms = intersect_sets(t1->sym, t2->sym,3);
  else
    return 1;
  int symsId;
  while (!empty_set(syms,3)){
    symsId = get_set(syms,3);
    int currTrue = 0;
    // check the input and see if they coincide with at least one.
    for (int i = 0; i<t1->inputNum; i++){
      for (int j = 0; j< t2->inputNum; j++){
        if ( ((t1->input[i] >> symsId) & 0x01) == ((t2->input[j] >> symsId) & 0x01) ){
          currTrue = 1;
          break;
        }
      }
      if (currTrue) break;
    }
    if (!currTrue) return 0;
  }
  return 1;
}

void merge_bin_timed(TAutomata *t1, TAutomata *t2, TAutomata *t, TAutomata *out){
  int numOfState = 0;
  int maxInput = 1;
  for (int i= 0; i < t->stateNum; i++){
    maxInput = maxInput < t->tStates[i].inputNum ? t->tStates[i].inputNum : maxInput;
  }
  const int maxNumOfState = ((t1->stateNum < t2->stateNum) ? t2->stateNum : t1->stateNum)* t->stateNum * maxInput;
  // printf("%ld \n", sizeof(TState)*(maxNumOfState + t1->eventNum + t2->eventNum));
  TState *s = (TState *) tl_emalloc(sizeof(TState)*(maxNumOfState + t1->eventNum + t2->eventNum));
  int t1StateNum[maxNumOfState];
  int t2StateNum[maxNumOfState];
  int tStateNum[maxNumOfState];
  int tEventStateNum[t1->eventNum + t2->eventNum];

  int matches = 0;
  int events = 0;

  int gen1StateNum[maxNumOfState+t1->eventNum];
  int gen2StateNum[maxNumOfState+t1->eventNum];

  int gen1State2Num[maxNumOfState+t2->eventNum];
  int gen2State2Num[maxNumOfState+t2->eventNum];

  int gen1Count = 0;
  int gen2Count = 0;  
  int gen1Count2 = 0;
  int gen2Count2 = 0;

  for (int i=0; i<t->stateNum; i++){  
    
    for (int j=0; j< t->tStates[i].inputNum; j++) {
      for (int k=0; k< t1->stateNum; k++){
        if (t1->tStates[k].output == t->tStates[i].input[j] >> 1 && t1->tStates[k].output!= NULLOUT) {
          //make product state if the input is the same as the output
          // further need to have input equal if they have intersection on sets.
          
          for (int l=0; l< t2->stateNum; l++){
            if (t2->tStates[l].output == (t->tStates[i].input[j] & (unsigned short) 0x01) && t2->tStates[l].output!= NULLOUT && compare_input(&t2->tStates[l], &t1->tStates[k])){
              t1StateNum[matches] = k;
              tStateNum[matches]=i;
              t2StateNum[matches++] = l;
            }
          }
        }else if(t1->tStates[k].output== NULLOUT && events == 0){ // scan only one time for testing NULLOUT of t2
          for (int l=0; l< t2->stateNum; l++){
            if (t2->tStates[l].output== NULLOUT){ 
              tEventStateNum[events++] = l;
            }
          }
          tEventStateNum[events++] = k;
        }else if (events == 0){
          for (int l=0; l< t2->stateNum; l++){
            if (t2->tStates[l].output== NULLOUT){ 
              tEventStateNum[events++] = l;
            }
          }
        }else if(t1->tStates[k].output== NULLOUT && events < t1->eventNum + t2->eventNum){
          tEventStateNum[events++] = k;
        }
      }
    }

    for (int k=numOfState; k< matches; k++){
      // merge their stateId name
      s[k].tstateId = (char *) malloc(sizeof(char)*(strlen(t1->tStates[t1StateNum[k]].tstateId)+strlen(t2->tStates[t2StateNum[k]].tstateId)+strlen(t->tStates[i].tstateId) +9));
      sprintf(s[k].tstateId,"(%s , %s) x %s", t1->tStates[t1StateNum[k]].tstateId, t2->tStates[t2StateNum[k]].tstateId, t->tStates[i].tstateId);
      
      if (strstr(t1->tStates[t1StateNum[k]].tstateId,"Gen_1")!=NULL) gen1StateNum[gen1Count++] = k;
      if (strstr(t2->tStates[t2StateNum[k]].tstateId,"Gen_1")!=NULL) gen1State2Num[gen1Count2++] = k;
      if (strstr(t1->tStates[t1StateNum[k]].tstateId,"Gen_2")!=NULL) gen2StateNum[gen2Count++] = k;
      if (strstr(t2->tStates[t2StateNum[k]].tstateId,"Gen_2")!=NULL) gen2State2Num[gen2Count2++] = k;

      // merge set of symbols
      s[k].sym = new_set(3);
      if (t1->tStates[t1StateNum[k]].sym!=NULL && t2->tStates[t2StateNum[k]].sym!=NULL)
        do_merge_sets(s[k].sym, t1->tStates[t1StateNum[k]].sym, t2->tStates[t2StateNum[k]].sym,3);
      else if (t1->tStates[t1StateNum[k]].sym!=NULL)
        s[k].sym = dup_set(t1->tStates[t1StateNum[k]].sym, 3);
      else if (t2->tStates[t2StateNum[k]].sym!=NULL)
        s[k].sym = dup_set(t2->tStates[t2StateNum[k]].sym, 3);

      s[k].buchi = (t1->tStates[t1StateNum[k]].buchi & t2->tStates[t2StateNum[k]].buchi) | t->tStates[i].buchi;
      
      // merge invariants 
      if (!t1->tStates[t1StateNum[k]].inv && !t2->tStates[t2StateNum[k]].inv && !t->tStates[i].inv){
        s[k].inv = (CGuard*) 0;
      }else{
        s[k].inv = (CGuard*) malloc(sizeof(CGuard)); 
        merge_inv(s[k].inv, t1->tStates[t1StateNum[k]].inv,t2->tStates[t2StateNum[k]].inv,t->tStates[i].inv);
      }

      // merge inputs
      s[k].inputNum=0;
      if (t1->tStates[t1StateNum[k]].inputNum>0 && t2->tStates[t2StateNum[k]].inputNum>0) {
        s[k].input= (unsigned short *) malloc(sizeof(unsigned short)*t1->tStates[t1StateNum[k]].inputNum*t2->tStates[t2StateNum[k]].inputNum);
      }else{
        s[k].input= (unsigned short *) malloc(sizeof(unsigned short)*(t1->tStates[t1StateNum[k]].inputNum + t2->tStates[t2StateNum[k]].inputNum));
      }
      if (t1->tStates[t1StateNum[k]].inputNum>0) {
        for (int l=0; l< t1->tStates[t1StateNum[k]].inputNum; l++){
          for (int n=0; n< t2->tStates[t2StateNum[k]].inputNum; n++){
            s[k].input[s[k].inputNum++] = t1->tStates[t1StateNum[k]].input[l] | t2->tStates[t2StateNum[k]].input[n];
          }
        }
      }else if (t2->tStates[t2StateNum[k]].inputNum>0 ){
        for (int n=0; n< t2->tStates[t2StateNum[k]].inputNum; n++){
          s[k].input[s[k].inputNum++] = t2->tStates[t2StateNum[k]].input[n];
        }
      }

      // merge output
      s[k].output = t->tStates[i].output;
    }
    numOfState = matches;
  }

  //add previous ended state back to the merged states
  for (int i=0; i<events; i++){
    if (i < t2->eventNum){
      int inputNum=0;
      s[numOfState+i].input= (unsigned short *) malloc(sizeof(unsigned short)*t2->tStates[tEventStateNum[i]].inputNum);
      for (int l=0; l< t2->tStates[tEventStateNum[i]].inputNum; l++){
        s[numOfState+i].input[inputNum++] = t2->tStates[tEventStateNum[i]].input[l];
      }
      s[numOfState+i].inv = copy_cguard(t2->tStates[tEventStateNum[i]].inv);
      create_tstate(&s[numOfState+i], t2->tStates[tEventStateNum[i]].tstateId, s[numOfState+i].inv, s[numOfState+i].input, t2->tStates[tEventStateNum[i]].inputNum, t2->tStates[tEventStateNum[i]].output, t2->tStates[tEventStateNum[i]].buchi, NULL);
      if (strstr(t2->tStates[tEventStateNum[i]].tstateId,"Gen_1")!=NULL) gen1State2Num[gen1Count2++] = numOfState+i;
      if (strstr(t2->tStates[tEventStateNum[i]].tstateId,"Gen_2")!=NULL) gen2State2Num[gen2Count2++] = numOfState+i;
      s[numOfState+i].sym = dup_set(t2->tStates[tEventStateNum[i]].sym ,3);
    }else{
      int inputNum=0;
      s[numOfState+i].input= (unsigned short *) malloc(sizeof(unsigned short)*t1->tStates[tEventStateNum[i]].inputNum);
      for (int l=0; l< t1->tStates[tEventStateNum[i]].inputNum; l++){
        s[numOfState+i].input[inputNum++] = t1->tStates[tEventStateNum[i]].input[l];
      }
      s[numOfState+i].inv = copy_cguard(t1->tStates[tEventStateNum[i]].inv);
      create_tstate(&s[numOfState+i], t1->tStates[tEventStateNum[i]].tstateId, s[numOfState+i].inv, s[numOfState+i].input, t1->tStates[tEventStateNum[i]].inputNum, t1->tStates[tEventStateNum[i]].output, t1->tStates[tEventStateNum[i]].buchi, NULL);
      if (strstr(t1->tStates[tEventStateNum[i]].tstateId,"Gen_1")!=NULL) gen1StateNum[gen1Count++] = numOfState+i;
      if (strstr(t1->tStates[tEventStateNum[i]].tstateId,"Gen_2")!=NULL) gen2StateNum[gen2Count++] = numOfState+i;
      s[numOfState+i].sym = dup_set(t1->tStates[tEventStateNum[i]].sym ,3);
    }
  }

  //merge transitions
  TTrans *tt = t->tTrans;
  TTrans *tOut = (TTrans *) emalloc_ttrans(1);
  TTrans *tmp = tOut;
  while (tt){
    // find tt->from in tStateNum then get the corresponding t1->tStates[] then find the transitions there

    // find the From and to idx for t->tStates that corresponds to tt-> from and tt->to  there should be only one,
    int tStateFrom = -1;
    int tStateTo= -1;
    int i = 0;
    while ((tStateFrom == -1 || tStateTo == -1) && i < t->stateNum) {
      if( &t->tStates[i] == tt->from) {
        tStateFrom = i;
      }
      if( &t->tStates[i] == tt->to) {
        tStateTo = i;
      }
      i++;
    }

    /**********************************************************************************************************/
    /* Find the appearance of those idx in the match table, it is possible to have multiple entries satisfies */
    /* that in the table. Merge them one by one if find match                                                 */
    /**********************************************************************************************************/

    for(i=0; i<numOfState; i++){
      if (tStateNum[i] == tStateFrom){
        for (int j=0; j<numOfState; j++){
          if (tStateNum[j] == tStateTo){
            // i, j are the indexes of one transitions

            //pointer iterate through the linked list to find the transition
            TTrans* t1Match = t1->tTrans;
            TTrans* t2Match = t2->tTrans;
            
            // find transition from t1StateNum[i] to t1StateNum[j]
            if (t1StateNum[i] == t1StateNum[j]){
              t1Match = NULL;
            }else{
              while (t1Match){
                if ( (t1Match->from == &(t1->tStates[t1StateNum[i]])) && (t1Match->to == &(t1->tStates[t1StateNum[j]])) ){
                  break;
                }
                t1Match = t1Match->nxt;
              }
            }

            // find transition from t2StateNum[i] to t2StateNum[j]
            if (t2StateNum[i] == t2StateNum[j]){
              t2Match = NULL;
            }else{
              while (t2Match){
                if ( (t2Match->from == &(t2->tStates[t2StateNum[i]])) && (t2Match->to == &(t2->tStates[t2StateNum[j]])) ){
                  break;
                }
                t2Match = t2Match->nxt;
              }
            }

            if (t1Match && t2Match) { // if both is not NULL
              // merge the transitions t1Match t2Match and tt
              tmp->nxt = (TTrans *) emalloc_ttrans(1);
              merge_ttrans(t1Match, t2Match, tt, tmp->nxt, &s[i], &s[j]);
              // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              
              tmp = tmp->nxt;
            }else if (t1Match){
              if (t2StateNum[j] == t2StateNum[i]){
                // merge the transitions t1Match t2Match and tt
                tmp->nxt = (TTrans *) emalloc_ttrans(1);
                merge_ttrans(t1Match, NULL, tt, tmp->nxt, &s[i], &s[j]);
                // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
                
                tmp = tmp->nxt;
              }else {
                // printf("Didn't merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              }

            }else if (t2Match){
              if (t1StateNum[j] == t1StateNum[i]){
                // merge the transitions t1Match t2Match and tt
                tmp->nxt = (TTrans *) emalloc_ttrans(1);
                merge_ttrans(NULL, t2Match, tt, tmp->nxt, &s[i], &s[j]);
                // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
                
                tmp = tmp->nxt;
              }else {
                // printf("Didn't merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              }
            }else {
              // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
            }


          }
        }
      }
    }

    tt=tt->nxt;
  }

  //merge transition that go to itself
  for (int k=0; k < t->stateNum; k++){

    int tStateTo = k;
    int tStateFrom = k;

    for(int i=0; i<numOfState; i++){
      if (tStateNum[i] == tStateFrom){
        for (int j=0; j<numOfState; j++){
          if (tStateNum[j] == tStateTo){
            if (i==j) continue;
            // i, j are the indexes of one transitions

            //pointer iterate through the linked list to find the transition
            TTrans* t1Match = t1->tTrans;
            TTrans* t2Match = t2->tTrans;
            
            // find transition from t1StateNum[i] to t1StateNum[j]
            if (t1StateNum[i] == t1StateNum[j]){
              t1Match = NULL;
            }else{
              while (t1Match){
                if ( (t1Match->from == &(t1->tStates[t1StateNum[i]])) && (t1Match->to == &(t1->tStates[t1StateNum[j]])) ){
                  break;
                }
                t1Match = t1Match->nxt;
              }
            }

            // find transition from t2StateNum[i] to t2StateNum[j]
            if (t2StateNum[i] == t2StateNum[j]){
              t2Match = NULL;
            }else{
              while (t2Match){
                if ( (t2Match->from == &(t2->tStates[t2StateNum[i]])) && (t2Match->to == &(t2->tStates[t2StateNum[j]])) ){
                  break;
                }
                t2Match = t2Match->nxt;
              }
            }

            if (t1Match && t2Match) { // if both is not NULL
              // merge the transitions t1Match t2Match and tt
              tmp->nxt = (TTrans *) emalloc_ttrans(1);
              merge_ttrans(t1Match, t2Match, tt, tmp->nxt, &s[i], &s[j]);
              // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              
              tmp = tmp->nxt;
            }else if (t1Match){
              if (t2StateNum[j] == t2StateNum[i]){
                // merge the transitions t1Match t2Match and tt
                tmp->nxt = (TTrans *) emalloc_ttrans(1);
                merge_ttrans(t1Match, NULL, tt, tmp->nxt, &s[i], &s[j]);
                // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
                
                tmp = tmp->nxt;
              }else {
                // printf("Didn't merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              }

            }else if (t2Match){
              if (t1StateNum[j] == t1StateNum[i]){
                // merge the transitions t1Match t2Match and tt
                tmp->nxt = (TTrans *) emalloc_ttrans(1);
                merge_ttrans(NULL, t2Match, tt, tmp->nxt, &s[i], &s[j]);
                // printf("Merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
                
                tmp = tmp->nxt;
              }else {
                // printf("Didn't merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              }
            }else {
              // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t2 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t2->tStates[t2StateNum[i]].tstateId, t2->tStates[t2StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
            }

          }
        }
      }
    }
  }


  //add previous ended transition back to the merged automata
  tt = tOut->nxt;
  while (tt->nxt) {
    tt = tt->nxt;
  }

  if (t2->tEvents != NULL){
    tt->nxt = t2->tEvents;
    out->tEvents = tt->nxt;
    while (tt->nxt) {

      if (strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_1")!=NULL){
        int allocation = 1;
        for (int i=0; i< gen1Count2; i++){
          if (included_set(tt->nxt->from->sym, s[gen1State2Num[i]].sym, 3) && allocation > 0  ){
            tt->nxt->to = &s[gen1State2Num[i]];
            tt->nxt->from = &s[numOfState + tt->nxt->from - &t2->tStates[0] - tEventStateNum[0]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen1State2Num[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen1State2Num[i]];
            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else if ( strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_2")!=NULL ){
        int allocation = 1;
        for (int i=0; i< gen2Count2; i++){
          if (included_set(tt->nxt->from->sym, s[gen2State2Num[i]].sym, 3) && allocation > 0){
            tt->nxt->to = &s[gen2State2Num[i]];
            tt->nxt->from = &s[numOfState + tt->nxt->from - &t2->tStates[0] - tEventStateNum[0]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen2State2Num[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen2State2Num[i]];
            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else {
        tt = tt->nxt;

        tt->to = &s[numOfState + tt->to - &t2->tStates[0] - tEventStateNum[0]];
        tt->from = &s[numOfState + tt->from - &t2->tStates[0] - tEventStateNum[0]];
      }
    }
  }

  if (t1->tEvents != NULL){
    tt->nxt = t1->tEvents;
    out->tEvents = tt->nxt;
    while (tt->nxt) {

      if (strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_1")!=NULL){
        int allocation = 1;
        for (int i=0; i< gen1Count; i++){
          if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation > 0 ){
            tt->nxt->to = &s[gen1StateNum[i]];
            tt->nxt->from = &s[numOfState + tt->nxt->from + t2->eventNum - &t1->tStates[0] - tEventStateNum[t2->eventNum]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen1StateNum[i]];
            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else if ( strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_2")!=NULL ){
        int allocation = 1;
        for (int i=0; i< gen2Count; i++){
          if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation > 0){
            tt->nxt->to = &s[gen2StateNum[i]];
            tt->nxt->from = &s[numOfState + t2->eventNum + tt->nxt->from - &t1->tStates[0] - tEventStateNum[t2->eventNum]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen2StateNum[i]];
            // create_ttrans(tt->nxt,  temp->cguard, NULL, 0, temp->from, &s[gen2StateNum[i]]);
            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else {
        tt = tt->nxt;

        tt->to = &s[numOfState + tt->to + t2->eventNum  - &t1->tStates[0] - tEventStateNum[t2->eventNum ]];
        tt->from = &s[numOfState + tt->from + t2->eventNum - &t1->tStates[0] - tEventStateNum[t2->eventNum ]];
      }
    }
  }

  if (t2->tEvents != NULL && t1->tEvents != NULL){
    out->tEvents = t2->tEvents;
  }
  free_ttrans(t->tTrans, 1);
  free_ttrans_until(t1->tTrans,t1->tEvents);
  free_ttrans_until(t2->tTrans,t2->tEvents);

  free_tstate(t1->tStates, t1->stateNum);
  free_tstate(t2->tStates, t2->stateNum);
  free_tstate(t->tStates, t->stateNum);

  //create return Automata out
  // out = (TAutomata *) malloc(sizeof(TAutomata));
  out->stateNum = numOfState + events;
  out->tStates = s;
  out->tTrans = tOut->nxt;
  out->eventNum = events;

  free_ttrans(tOut,0);

  tfree(t1);
  tfree(t2);
  tfree(t);
}

void merge_timed(TAutomata *t1, TAutomata *t, TAutomata *out){
  int numOfState = 0;
  int maxInput = 1;
  for (int i= 0; i < t->stateNum; i++){
    maxInput = maxInput < t->tStates[i].inputNum ? t->tStates[i].inputNum : maxInput;
  }
  const int maxNumOfState = t1->stateNum* t->stateNum * maxInput;
  // printf("%d \n", maxNumOfState+t1->eventNum);
  TState *s = (TState *) tl_emalloc(sizeof(TState)*(maxNumOfState+t1->eventNum));

  int gen1StateNum[maxNumOfState+t1->eventNum];
  int gen2StateNum[maxNumOfState+t1->eventNum];

  int gen1Count = 0;
  int gen2Count = 0;

  int t1StateNum[maxNumOfState+1];
  int tStateNum[maxNumOfState];
  int tEventStateNum[t1->eventNum];

  int matches = 0;
  int events = 0;

  for (int i=0; i<t->stateNum; i++){  
    
    for (int j=0; j< t->tStates[i].inputNum; j++) {
      for (int k=0; k< t1->stateNum; k++){
        if (t1->tStates[k].output == t->tStates[i].input[j] && t1->tStates[k].output!= NULLOUT ) {
          //make product state if the input is the same as the output
          t1StateNum[matches] = k;
          tStateNum[matches++]=i;
        }else if(t1->tStates[k].output== NULLOUT && events < t1->eventNum){
          tEventStateNum[events++] = k;
        }
      }
    }

    for (int k=numOfState; k< matches; k++){
      // merge their stateId name
      s[k].tstateId = (char *) malloc(sizeof(char)*(strlen(t1->tStates[t1StateNum[k]].tstateId)+strlen(t->tStates[i].tstateId) +5));
      sprintf(s[k].tstateId,"%s x %s", t1->tStates[t1StateNum[k]].tstateId, t->tStates[i].tstateId);
      // merge set of symbols
      s[k].sym = dup_set(t1->tStates[t1StateNum[k]].sym,3);

      if (strstr(s[k].tstateId,"Gen_1")!=NULL) gen1StateNum[gen1Count++] = k;
      if (strstr(s[k].tstateId,"Gen_2")!=NULL) gen2StateNum[gen2Count++] = k;

      // merge invariants 
      if (!t1->tStates[t1StateNum[k]].inv && !t->tStates[i].inv){
        s[k].inv = (CGuard*) 0;
      }else{
        s[k].inv = (CGuard*) malloc(sizeof(CGuard)); 
        merge_inv(s[k].inv, t1->tStates[t1StateNum[k]].inv, NULL ,t->tStates[i].inv);
      }

      // merge inputs
      s[k].inputNum=0;
      s[k].input= (unsigned short *) malloc(sizeof(unsigned short)*t1->tStates[t1StateNum[k]].inputNum);
      for (int l=0; l< t1->tStates[t1StateNum[k]].inputNum; l++){
        s[k].input[s[k].inputNum++] = t1->tStates[t1StateNum[k]].input[l];
      }

      // merge output
      s[k].output = t->tStates[i].output;      

      // merge buchi
      s[k].buchi = t1->tStates[t1StateNum[k]].buchi | t->tStates[i].buchi;
    }
    numOfState = matches;
  }

  //add previous ended state back to the merged states
  for (int i=0; i<events; i++){

    int inputNum=0;
    s[numOfState+i].input= (unsigned short *) malloc(sizeof(unsigned short)*t1->tStates[tEventStateNum[i]].inputNum);
    for (int l=0; l< t1->tStates[tEventStateNum[i]].inputNum; l++){
      s[numOfState+i].input[inputNum++] = t1->tStates[tEventStateNum[i]].input[l];
    }
    s[numOfState+i].inv = copy_cguard(t1->tStates[tEventStateNum[i]].inv);
    create_tstate(&s[numOfState+i], t1->tStates[tEventStateNum[i]].tstateId, s[numOfState+i].inv, s[numOfState+i].input, t1->tStates[tEventStateNum[i]].inputNum, t1->tStates[tEventStateNum[i]].output, t1->tStates[tEventStateNum[i]].buchi, NULL);
    if (strstr(s[numOfState+i].tstateId,"Gen_1")!=NULL) gen1StateNum[gen1Count++] = numOfState+i;
    if (strstr(s[numOfState+i].tstateId,"Gen_2")!=NULL) gen2StateNum[gen2Count++] = numOfState+i;
    s[numOfState+i].sym = dup_set(t1->tStates[tEventStateNum[i]].sym ,3);
  }  


  //merge transitions
  TTrans *tt = t->tTrans;
  TTrans *tOut = (TTrans *) emalloc_ttrans(1);
  TTrans *tmp = tOut;
  while (tt){
    // find tt->from in tStateNum then get the corresponding t1->tStates[] then find the transitions there

    // find the From and to idx for t->tStates that corresponds to tt-> from and tt->to  there should be only one,
    int tStateFrom = -1;
    int tStateTo= -1;
    int i = 0;
    while ((tStateFrom == -1 || tStateTo == -1) && i < t->stateNum) {
      if( &t->tStates[i] == tt->from) {
        tStateFrom = i;
      }
      if( &t->tStates[i] == tt->to) {
        tStateTo = i;
      }
      i++;
    }

    /**********************************************************************************************************/
    /* Find the appearance of those idx in the match table.                                                   */
    /**********************************************************************************************************/

    for(i=0; i<numOfState; i++){
      if (tStateNum[i] == tStateFrom){
        for (int j=0; j<numOfState; j++){
          if (tStateNum[j] == tStateTo){
            // i, j are the indices of one transitions

            //pointer iterate through the linked list to find the transition
            TTrans* t1Match = t1->tTrans;
            
            // find transition from t1StateNum[i] to t1StateNum[j]
            if (t1StateNum[i] == t1StateNum[j]){
              t1Match = NULL;
            }else{
              while (t1Match){
                if ( (t1Match->from == &(t1->tStates[t1StateNum[i]])) && (t1Match->to == &(t1->tStates[t1StateNum[j]])) ){
                  break;
                }
                t1Match = t1Match->nxt;
              }
            }

            if (t1Match) { // if both is not NULL
              // merge the transitions t1Match tt
              tmp->nxt = (TTrans *) emalloc_ttrans(1);
              merge_ttrans(t1Match, NULL, tt, tmp->nxt, &s[i], &s[j]);
              
              tmp = tmp->nxt;
            }else{
              if (strstr(t->tStates[tStateNum[i]].tstateId, "CHK01")!=NULL || strstr(t->tStates[tStateNum[i]].tstateId, "CHK00")!=NULL){
                tmp->nxt = (TTrans *) emalloc_ttrans(1);
                merge_ttrans(NULL, NULL, tt, tmp->nxt, &s[i], &s[j]);
                tmp = tmp->nxt;
              }else{
                // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
              }
            }

          }
        }
      }
    }

    tt=tt->nxt;
  }

  //merge transition that go to itself
  for (int k=0; k < t->stateNum; k++){

    int tStateTo = k;
    int tStateFrom = k;

    for(int i=0; i<numOfState; i++){
      if (tStateNum[i] == tStateFrom){
        for (int j=0; j<numOfState; j++){
          if (tStateNum[j] == tStateTo){
            if (i==j) continue;
            // i, j are the indexes of one transitions

            //pointer iterate through the linked list to find the transition
            TTrans* t1Match = t1->tTrans;
            
            // find transition from t1StateNum[i] to t1StateNum[j]
            if (t1StateNum[i] == t1StateNum[j]){
              t1Match = NULL;
            }else{
              while (t1Match){
                if ( (t1Match->from == &(t1->tStates[t1StateNum[i]])) && (t1Match->to == &(t1->tStates[t1StateNum[j]])) ){
                  break;
                }
                t1Match = t1Match->nxt;
              }
            }

            if (t1Match) { // if both is not NULL
              // merge the transitions t1Match t2Match and tt
              tmp->nxt = (TTrans *) emalloc_ttrans(1);
              merge_ttrans(t1Match, NULL, NULL, tmp->nxt, &s[i], &s[j]);
              
              tmp = tmp->nxt;
            }else{
              // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
            }

          }
        }
      }
    }
  }

  //add previous ended transition back to the merged automata
  // need to check if it is Gen0. Checker is closed system. Gen next link need to change to any state that has Gen_1 and Gen_2 within
  tt = tOut;
  while (tt->nxt) {
    tt = tt->nxt;
  }
  if (t1->tEvents != NULL){
    tt->nxt = t1->tEvents;
    out->tEvents = tt->nxt;
    while (tt->nxt) {

      if (strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_1")!=NULL){
        int allocation = 1;
        for (int i=0; i< gen1Count; i++){
          if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation > 0 ){
            tt->nxt->to = &s[gen1StateNum[i]];
            tt->nxt->from = &s[numOfState + tt->nxt->from - &t1->tStates[0] - tEventStateNum[0]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen1StateNum[i]];

            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else if ( strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_2")!=NULL ){
        int allocation = 1;
        for (int i=0; i< gen2Count; i++){
          if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation > 0){
            tt->nxt->to = &s[gen2StateNum[i]];
            tt->nxt->from = &s[numOfState + tt->nxt->from - &t1->tStates[0] - tEventStateNum[0]];
            allocation--;
          }else if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation <= 0 ){
            TTrans *temp =tt->nxt;
            tt->nxt =  (TTrans *) emalloc_ttrans(1); 
            // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
            copy_ttrans(tt->nxt, temp);
            tt->nxt->to = &s[gen2StateNum[i]];
            tt = tt->nxt;
            tt->nxt = temp;
          }
        }
        tt = tt->nxt;
      } else {
        tt = tt->nxt;

        tt->to = &s[numOfState + tt->to - &t1->tStates[0] - tEventStateNum[0]];
        tt->from = &s[numOfState + tt->from - &t1->tStates[0] - tEventStateNum[0]];
      }
    }
  }

  // free every transition before t1.event
  free_ttrans_until(t1->tTrans, t1->tEvents);
  free_ttrans(t->tTrans, 1);

  // free every states
  free_tstate(t1->tStates, t1->stateNum);
  free_tstate(t->tStates, t->stateNum);

  //create return Automata out
  // out = (TAutomata *) malloc(sizeof(TAutomata));
  out->stateNum = numOfState + events;
  out->tStates = s;
  out->tTrans = tOut->nxt;
  out->eventNum = events;

  free_ttrans(tOut,0);
  tfree(t1);
  tfree(t);
}

void merge_map_timed(TAutomata *t1, TAutomata *t, TAutomata *out){

  // t1 is map automata, t is the formula automata

  // for (int i=0; i<numOfState; i++){
  //   printf("state: %s ",s[i].tstateId);
  //   print_CGuard(s[i].inv);
  //   printf("\n");
  // }

  // copied from merge timed since if map have multiple symbols need to search in system for the symbol pairs to match.
  int numOfState = 0;
  int maxInput = 1;
  for (int i= 0; i < t->stateNum; i++){
    maxInput = maxInput < t->tStates[i].inputNum ? t->tStates[i].inputNum : maxInput;
  }
  int numOfSyms = count_set(t->tStates[0].sym,3);

  const int maxNumOfState = t1->stateNum* pow(t->stateNum/numOfSyms,numOfSyms) * maxInput;
  TState *s = (TState *) tl_emalloc(sizeof(TState)*(maxNumOfState+t1->eventNum));

  int gen1StateNum[maxNumOfState];
  int gen2StateNum[maxNumOfState];

  int gen1Count = 0;
  int gen2Count = 0;

  int t1StateNum[maxNumOfState];

  int matches = 0;
  

  int matchTable[numOfSyms][maxNumOfState];

  for (int i=0; i<t->stateNum; i++){  
    
    for (int j=0; j< t->tStates[i].inputNum; j++) {
      for (int k=0; k< t1->stateNum; k++){
        // TODO: merge CHKs for different locations (1)
        // 1.  if  t1.s[k] syms not same as t.[i] syms 
        //     find intersection of the syms set and find in the states after k
        // 2.  merge multiple states (maximum is determined by the number of symbols in the map) if there are more
        
        // assume there are only two cases
        // 1. map and mitl automata have same amount of syms in every states
        // 2. every state of mitl automata have only one individually 
        // (3) we are ignoring cases where map have n syms and some mitl automata have i and j syms, i!=j and i+j = n
        if (t1->tStates[k].output == t->tStates[i].input[j] && same_sets(t1->tStates[k].sym, t->tStates[i].sym, 3) ) {
          // make product state if the input is the same as the output
          matchTable[0][matches] = i;
          t1StateNum[matches++]=k;
          fprintf(tl_out,"Table: %i %i -- %i\n", i,k,matches-1);
          numOfSyms=1; // trick the state matcher to think there is only one symbs
        }else if(t1->tStates[k].output == t->tStates[i].input[j]){
          int *resSet = intersect_sets(t1->tStates[k].sym, t->tStates[i].sym, 3);
          int *diffSet = dup_set(t1->tStates[k].sym,  3);
          while (!empty_set(resSet,3)){
            rem_set(diffSet, get_set(resSet,3));
          }
          resSet = intersect_sets(t1->tStates[k].sym, t->tStates[i].sym, 3);
          int matchStart = matches;
          int matchEnd = matches+1;
          int tmpIdx = get_set(resSet,3);
          matchTable[tmpIdx][matches] = i;
          // fprintf(tl_out,"test %i,%i:%i", tmpIdx, matches, i);
          while (!empty_set(diffSet,3)){
            int symbId = get_set(diffSet,3);
            int *tmpSet = new_set(3);
            add_set(tmpSet, symbId);

            int saveStart = matchStart;
            int found = 0;

            for (int ns = 0; ns<t->stateNum; ns++){
              for (int ms =0; ms < t->tStates[ns].inputNum; ms++){
                int cpi=matchStart;
                // find the one with correct syms and input in l
                if(ns>=i && t->tStates[ns].input[ms] ==0 && same_sets(tmpSet, t->tStates[ns].sym, 3) ){
                  found = 1;
                  for (cpi = matchStart; cpi<matchEnd; cpi++){
                    for (int cpl = 0; cpl<numOfSyms; cpl++){
                      matchTable[cpl][cpi+matchEnd-matchStart] = matchTable[cpl][cpi];

                      // fprintf(tl_out,"test cp %i,%i:%i", cpl, cpi+matchEnd-matchStart, matchTable[cpl][cpi]);
                    }
                    matchTable[symbId][cpi] = ns;
                    // fprintf(tl_out,"test new %i,%i:%i", symbId, cpi, ns);
                    if (empty_set(diffSet,3)) {// if last one
                      t1StateNum[matches++]=k;
                      fprintf(tl_out,"Table: ");
                      for (int ls=0;ls<numOfSyms;ls++){
                        fprintf(tl_out,"%i -- ", matchTable[ls][matches-1] );
                        // if (strstr(t->tStates[matchTable[ls][matches-1]].tstateId,"CHK_2")!=NULL)
                        //   fprintf(tl_out,"x");
                        // else if (strstr(t->tStates[matchTable[ls][matches-1]].tstateId,"CHK_4")!=NULL)
                        //   fprintf(tl_out,"y");
                      }
                      fprintf(tl_out,"%i : %i\n", k, matches-1);
                    }
                  }
                  matchEnd = matchEnd-matchStart+matchEnd;
                  matchStart = cpi;
                }
                else if (ns<i && t1->tStates[k].output!=0 && t->tStates[ns].input[ms] ==0 && same_sets(tmpSet, t->tStates[ns].sym, 3) ){
                  found = 1;
                  for (cpi = matchStart; cpi<matchEnd; cpi++){
                    for (int cpl = 0; cpl<numOfSyms; cpl++){
                      matchTable[cpl][cpi+matchEnd-matchStart] = matchTable[cpl][cpi];
                    }
                    matchTable[symbId][cpi] = ns;
                    if (empty_set(diffSet,3)) {// if last one
                      t1StateNum[matches++]=k;
                      fprintf(tl_out,"Table: ");
                      for (int ls=0;ls<numOfSyms;ls++){
                        fprintf(tl_out,"%i -- ", matchTable[ls][matches-1] );
                        // if (strstr(t->tStates[matchTable[ls][matches-1]].tstateId,"CHK_2")!=NULL)
                        //   fprintf(tl_out,"x ");
                        // else if (strstr(t->tStates[matchTable[ls][matches-1]].tstateId,"CHK_4")!=NULL)
                        //   fprintf(tl_out,"y ");
                      }
                      fprintf(tl_out,"%i : %i\n", k, matches-1);
                    }
                  }
                  matchEnd = matchEnd-matchStart+matchEnd;
                  matchStart = cpi;
                }
                
              }
            }

            if (found){
              // after finishing the loop need to reset end to previous end and start state
              matchEnd = matchStart;
              matchStart = saveStart;
            }else{
              break;
            }
          }
        }
      }
    }

    for (int k=numOfState; k< matches; k++){
      // merge their stateId name
      int stateIdlen = 0;
      for (int l = 0; l< numOfSyms; l++){
        stateIdlen+=strlen(t->tStates[matchTable[l][k]].tstateId);
      }
      s[k].tstateId = (char *) malloc(sizeof(char)*(strlen(t1->tStates[t1StateNum[k]].tstateId)+stateIdlen +5));
      char* tmpId = (char *) malloc(sizeof(char)*(strlen(t1->tStates[t1StateNum[k]].tstateId)+stateIdlen +5));
      sprintf(s[k].tstateId,"%s x %s", t1->tStates[t1StateNum[k]].tstateId, t->tStates[matchTable[0][k]].tstateId);
      strcpy(tmpId,s[k].tstateId);
      for (int l = 1; l< numOfSyms; l++){
        sprintf(s[k].tstateId,"%s - %s", tmpId, t->tStates[matchTable[l][k]].tstateId);
        strcpy(tmpId,s[k].tstateId);
      }
      // merge set of symbols
      s[k].sym = dup_set(t1->tStates[t1StateNum[k]].sym,3);
      
      if (strstr(s[k].tstateId,"Gen_1")!=NULL) gen1StateNum[gen1Count++] = k;
      if (strstr(s[k].tstateId,"Gen_2")!=NULL) gen2StateNum[gen2Count++] = k;

      // merge invariants 
      int isNotFirst=0;
      s[k].inv = (CGuard*) malloc(sizeof(CGuard)); 
      CGuard *tmp;
      for (int l = 0; l< numOfSyms; l++){
        if (isNotFirst){
          tmp = copy_cguard(s[k].inv);
          merge_inv(s[k].inv, tmp , NULL ,t->tStates[matchTable[l][k]].inv);

        }else{
          merge_inv(s[k].inv, t1->tStates[t1StateNum[k]].inv , NULL ,t->tStates[matchTable[l][k]].inv);
        }
        isNotFirst|=(t->tStates[matchTable[l][k]].inv != NULL);

      }
      if (!isNotFirst && !t1->tStates[t1StateNum[k]].inv) s[k].inv = (CGuard*) 0;

      // merge inputs
      s[k].inputNum=0;
      s[k].input= (unsigned short *) malloc(sizeof(unsigned short)*t1->tStates[t1StateNum[k]].inputNum);
      for (int l=0; l< t1->tStates[t1StateNum[k]].inputNum; l++){
        s[k].input[s[k].inputNum++] = t1->tStates[t1StateNum[k]].input[l];
      }

      // merge output
      s[k].output = t->tStates[i].output;      

      // merge buchi
      s[k].buchi = t1->tStates[t1StateNum[k]].buchi | t->tStates[i].buchi;
    }
    numOfState = matches;
  }

  // printf("Printing matching table:\nTable[0]: ");
  // for (int i=0; i<numOfState; i++){
  //   printf("%i, ",matchTable[0][i]);
  // }
  // printf("\n");

  //merge transitions
  TTrans *tt = t1->tTrans;
  TTrans *tOut = (TTrans *) emalloc_ttrans(1);
  TTrans *tmp = tOut;
  while (tt){
    // find tt->from in t1StateNum then get the corresponding t->tStates[] then find the transitions there

    // find the From and to idx for t1->tStates that corresponds to tt-> from and tt->to  there should be only one,
    int t1StateFrom = -1;
    int t1StateTo= -1;
    int i = 0;
    while ((t1StateFrom == -1 || t1StateTo == -1) && i < t1->stateNum) {
      if( &t1->tStates[i] == tt->from) {
        t1StateFrom = i;
      }
      if( &t1->tStates[i] == tt->to) {
        t1StateTo = i;
      }
      i++;
    }

    /**********************************************************************************************************/
    /* Find the appearance of those idx in the match table.                                                   */
    /**********************************************************************************************************/

    for(i=0; i<numOfState; i++){
      if (t1StateNum[i] == t1StateFrom){
        for (int j=0; j<numOfState; j++){
          if (t1StateNum[j] == t1StateTo){
            // i, j are the indices of one transitions

            //pointer iterate through the linked list to find the transition
            TTrans* tMatch[numOfSyms+1];
            int mergable=1;
            for (int l=0; l<numOfSyms; l++){
              tMatch[l]= t->tTrans;
              // find transition from t1StateNum[i] to t1StateNum[j]
              if (matchTable[l][i] == matchTable[l][j]){
                tMatch[l] = NULL;
              }else{
                while (tMatch[l]){
                  if ( (tMatch[l]->from == &(t->tStates[matchTable[l][i]])) && (tMatch[l]->to == &(t->tStates[matchTable[l][j]])) ){
                    
                    break;
                  }
                  tMatch[l] = tMatch[l]->nxt;
                }
              }
              if (tMatch[l]==NULL && matchTable[l][i] != matchTable[l][j]){
                // do not merge at all
                mergable = 0;
                break;
              }

            }

            
            if (mergable) { // if both is not NULL
              // merge the transitions t1Match tt
              tmp->nxt = (TTrans *) emalloc_ttrans(1);
              tMatch[numOfSyms] = tt;
              merge_ttrans_array(tMatch, numOfSyms+1, tmp->nxt, &s[i], &s[j]);

              tmp = tmp->nxt;
            }else{
              // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t->tStates[matchTable[0][i]].tstateId, t->tStates[matchTable[0][j]].tstateId);
            }

          }
        }
      }
    }

    tt=tt->nxt;
  }

  //map location can stay where it is.
  // for (int k=0; k < t1->stateNum; k++){

  //   int t1StateTo = k;
  //   int t1StateFrom = k;

  //   for(int i=0; i<numOfState; i++){
  //     if (t1StateNum[i] == t1StateFrom){
  //       for (int j=0; j<numOfState; j++){
  //         if (t1StateNum[j] == t1StateTo){
  //           if (i==j) continue;
  //           // i, j are the indexes of one transitions

  //                       //pointer iterate through the linked list to find the transition
  //           TTrans* tMatch[numOfSyms];
  //           int mergable=1;
  //           for (int l=0; l<numOfSyms; l++){
  //             tMatch[l]= t->tTrans;
  //             // find transition from t1StateNum[i] to t1StateNum[j]
  //             if (matchTable[l][i] == matchTable[l][j]){
  //               tMatch[l] = NULL;
  //             }else{
  //               while (tMatch[l]){
  //                 if ( (tMatch[l]->from == &(t->tStates[matchTable[l][i]])) && (tMatch[l]->to == &(t->tStates[matchTable[l][j]])) ){
  //                   break;
  //                 }
  //                 tMatch[l] = tMatch[l]->nxt;
  //               }
  //             }
  //             if (tMatch[l]==NULL && matchTable[l][i] != matchTable[l][j]){
  //               // do not merge at all
  //               mergable = 0;
  //               break;
  //             }

  //           }

            
  //           if (mergable) { // if both is not NULL
  //             // merge the transitions t1Match tt
  //             tmp->nxt = (TTrans *) emalloc_ttrans(1);
  //             merge_ttrans_array(tMatch, numOfSyms, tmp->nxt, &s[i], &s[j]);
              
  //             tmp = tmp->nxt;
  //           }else{
  //             // printf("Cannot merge transition of the following: \n t1 %s -> %s \n t %s -> %s \n", t1->tStates[t1StateNum[i]].tstateId, t1->tStates[t1StateNum[j]].tstateId, t->tStates[tStateNum[i]].tstateId, t->tStates[tStateNum[j]].tstateId);
  //           }

  //         }
  //       }
  //     }
  //   }
  // }

  // TODO: Gen0 for different location need to be merged. (3)
  // 1. Combined state Gen0: subformula 1 + subformula 2, so invariants are the same
  // 2. Gen0:sub1->loc1 have update x1=0 and Gen0:sub2 ->loc1 have update x2=0, merge the updates
  // tt = tOut;
  // while (tt->nxt) {
  //   if (strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_1")!=NULL){
  //     int allocation = 1;
  //     for (int i=0; i< gen1Count; i++){
  //       if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation > 0 ){
  //         tt->nxt->to = &s[gen1StateNum[i]];
  //         tt->nxt->from = &s[numOfState + tt->nxt->from - &t1->tStates[0]];
  //         allocation--;
  //       }else if (included_set(tt->nxt->from->sym, s[gen1StateNum[i]].sym, 3) && allocation <= 0 ){
  //         TTrans *temp =tt->nxt;
  //         tt->nxt =  (TTrans *) emalloc_ttrans(1); 
  //         // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
  //         copy_ttrans(tt->nxt, temp);
  //         tt->nxt->to = &s[gen1StateNum[i]];

  //         tt = tt->nxt;
  //         tt->nxt = temp;
  //       }
  //     }
  //     tt = tt->nxt;
  //   } else if ( strstr(tt->nxt->from->tstateId,"Gen0")!=NULL && strstr(tt->nxt->to->tstateId,"Gen_2")!=NULL ){

  //     int allocation = 1;
  //     for (int i=0; i< gen2Count; i++){
  //       if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation > 0){
  //         tt->nxt->to = &s[gen2StateNum[i]];
  //         tt->nxt->from = &s[numOfState + tt->nxt->from - &t1->tStates[0]];
  //         allocation--;
  //       }else if (included_set(tt->nxt->from->sym, s[gen2StateNum[i]].sym, 3) && allocation <= 0 ){
  //         TTrans *temp =tt->nxt;
  //         tt->nxt =  (TTrans *) emalloc_ttrans(1); 
  //         // create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
  //         copy_ttrans(tt->nxt, temp);
  //         tt->nxt->to = &s[gen2StateNum[i]];
  //         tt = tt->nxt;
  //         tt->nxt = temp;
  //       }
  //     }
  //     tt = tt->nxt;
  //   } else {
  //     tt = tt->nxt;

  //     tt->to = &s[numOfState + tt->to - &t1->tStates[0]];
  //     tt->from = &s[numOfState + tt->from - &t1->tStates[0]];
  //   }
    
  // }
  //create return Automata out
  // out = (TAutomata *) malloc(sizeof(TAutomata));
  out->stateNum = numOfState;
  out->tStates = s;
  out->tTrans = tOut->nxt;
  out->eventNum = 0;


  free_ttrans_until(t1->tTrans, t1->tEvents);
  free_ttrans(tOut,0);

  // copy back the states with Gen in t
  // copy t->tStates[i] if it is gen to out->tStates end and it is not in product with other loc states
  numOfState = out->stateNum;
  int genStateNum = 0;
  int refGen[t->stateNum];

  // TODO: merge the Gen0 (2)
  for (int i =0; i< t->stateNum; i++){
    refGen[i] = -1;
    if (strstr(t->tStates[i].tstateId, "Gen")!=NULL && strstr(t->tStates[i].tstateId, "p(")==NULL && strstr(t->tStates[i].tstateId, "CHK_")==NULL){
      refGen[i] = genStateNum;
      int inputNum=0;
      out->tStates[numOfState+genStateNum].input= (unsigned short *) malloc(sizeof(unsigned short)*t->tStates[i].inputNum);
      for (int l=0; l< t->tStates[i].inputNum; l++){
        out->tStates[numOfState+genStateNum].input[inputNum++] = t->tStates[i].input[l];
      }
      out->tStates[numOfState+genStateNum].inv = copy_cguard(t->tStates[i].inv);
      create_tstate(&out->tStates[numOfState+genStateNum], t->tStates[i].tstateId, out->tStates[numOfState+genStateNum].inv, out->tStates[numOfState+genStateNum].input, t->tStates[i].inputNum, t->tStates[i].output, t->tStates[i].buchi, NULL);
      out->tStates[numOfState+genStateNum].sym = dup_set(t->tStates[i].sym ,3);
      genStateNum++;
    }
  }

  //adjust transitions accordingly
  tt = out->tTrans;
  while(tt->nxt){
    tt = tt->nxt;
  }

  TTrans* tGen = t->tTrans;
  while(tGen){
    if (refGen[tGen->to - &t->tStates[0]]!=-1 && refGen[tGen->from - &t->tStates[0]]!=-1){
      // fprintf(tl_out,"%s -> %s :%i \n", out->tStates[numOfState+refGen[tGen->from - &t->tStates[0]]].tstateId, out->tStates[numOfState+refGen[tGen->to - &t->tStates[0]]].tstateId, out->tStates[numOfState+refGen[tGen->to - &t->tStates[0]]].output);
      
      if (out->tStates[numOfState+refGen[tGen->to - &t->tStates[0]]].output==0 && strstr(out->tStates[numOfState+refGen[tGen->from - &t->tStates[0]]].tstateId, "Gen0")!=NULL){
        // fprintf(tl_out,"ignore %s -> %s :%i \n", out->tStates[numOfState+refGen[tGen->from - &t->tStates[0]]].tstateId, out->tStates[numOfState+refGen[tGen->to - &t->tStates[0]]].tstateId, out->tStates[numOfState+refGen[tGen->to - &t->tStates[0]]].output);
        // do nothing dont copy the edge.
      }else{
        tt->nxt = tGen;
        tt = tt->nxt;
        // printf("%s-> %s \n", tGen->from->tstateId, tGen->to->tstateId);
        tt->from = &out->tStates[numOfState + refGen[tGen->from - &t->tStates[0]]];
        tt->to = &out->tStates[numOfState + refGen[tGen->to - &t->tStates[0]]];
        // printf("%s-> %s \n", tt->from->tstateId, tt->to->tstateId);
        
      }
    }

    if (tGen != tt) {
      // if tGen is not add back to output free the ttrans
      TTrans* tmp = tGen;
      tGen = tGen->nxt;
      free_ttrans(tmp, 0);
    } else{
      tGen = tGen->nxt;
    }
  }
  tt->nxt = NULL;

  out->stateNum = numOfState+genStateNum;

  free_tstate(t->tStates, t->stateNum);
  free_tstate(t1->tStates, t1->stateNum);

  tfree(t1);
  tfree(t);
  
  // appoint initial state accordingly

  // make it easy to divide automata
}

/********************************************************************\
|*                Create Timed Automata of the map                  *|
\********************************************************************/
TAutomata *create_map(int nodeNum){
  TAutomata *t = (TAutomata *) tl_emalloc(sizeof(TAutomata));
  TState *s = (TState *)tl_emalloc(sizeof(TState)*nodeNum);
  CGuard *cguard;
  int *clockId;

  // void create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p){
  for (int i=0; i<nodeNum; i++){
    s[i].tstateId = (char *)malloc(sizeof(char)*6);
    sprintf(s[i].tstateId, "loc%i",i);

    cguard = (CGuard *) malloc(sizeof(CGuard));
    cguard->nType = PREDICATE;
    cguard->cCstr = (CCstr *)(CCstr * )  malloc(sizeof(CCstr));
    cguard->cCstr->cIdx = cCount;
    cguard->cCstr->gType = LESSEQUAL;
    cguard->cCstr->bndry = 1;

    create_tstate(&s[i], s[i].tstateId, cguard, (unsigned short *) 0, 0, 0, 0, NULL);
    s[i].sym = new_set(3);
    clear_set(s[i].sym,3);
    add_set(s[i].sym, t_get_sym_id("a"));
  }
  s[nodeNum-1].output= 1;
  TTrans *tt = emalloc_ttrans(1);
  TTrans *tmp = tt;


  for (int i=0; i<nodeNum; i++){
    for (int j=i+1; j<nodeNum; j++){
      tmp->nxt = emalloc_ttrans(1);
      tmp = tmp->nxt;

      cguard = (CGuard*) malloc(sizeof(CGuard));
      cguard->nType = PREDICATE;
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATEREQUAL;
      cguard->cCstr->bndry = 1;
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;

      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[i],  &s[j]);

      cguard = (CGuard*) malloc(sizeof(CGuard));
      cguard->nType = PREDICATE;
      cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
      cguard->cCstr->cIdx = cCount;
      cguard->cCstr->gType = GREATEREQUAL;
      cguard->cCstr->bndry = 1;
      clockId = (int *) malloc(sizeof(int)*1);
      clockId[0] = cCount;

      tmp->nxt = emalloc_ttrans(1);
      tmp = tmp->nxt;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[j],  &s[i]);

    }
  }
  cCount++;

  t->tTrans = tt->nxt;
  t->tStates = s;
  t->stateNum = nodeNum;
  t->tEvents = NULL;
  t->eventNum = 0;
  return t;

  free_ttrans(tt,0);
}

TAutomata *create_map_loop(int nodeNum, int ifb, int timeInt){
  // nodeNum>=4
  if (nodeNum <4){
    nodeNum =4;
  }
  TAutomata *t = (TAutomata *) tl_emalloc(sizeof(TAutomata));
  TState *s = (TState *)tl_emalloc(sizeof(TState)*nodeNum);
  CGuard *cguard;
  int *clockId;

  // void create_tstate(TState *s, char *tstateId, CGuard *inv, unsigned short *input, unsigned short inputNum, unsigned short output, unsigned short buchi, Node* p){
  char tstateId[6];
  for (int i=0; i<nodeNum; i++){
    sprintf(tstateId, "loc%i",i);

    cguard = (CGuard *) malloc(sizeof(CGuard));
    cguard->nType = PREDICATE;
    cguard->cCstr = (CCstr *)(CCstr * )  malloc(sizeof(CCstr));
    cguard->cCstr->cIdx = cCount;
    cguard->cCstr->gType = LESSEQUAL;
    cguard->cCstr->bndry = timeInt;

    create_tstate(&s[i], tstateId, cguard, (unsigned short *) 0, 0, 0, 0, NULL);
    s[i].sym = new_set(3);
    clear_set(s[i].sym,3);
    add_set(s[i].sym, t_get_sym_id("a"));
    if(ifb)
      add_set(s[i].sym, t_get_sym_id("b"));
  }
  s[nodeNum-1].output= 1<<t_get_sym_id("a");
  if(ifb)
    s[nodeNum-3].output= 1<<t_get_sym_id("b");
  TTrans *tt = emalloc_ttrans(1);
  TTrans *tmp = tt;

  //share same cguard and clockId
  cguard = (CGuard*) malloc(sizeof(CGuard));
  cguard->nType = PREDICATE;
  cguard->cCstr = (CCstr * ) malloc(sizeof(CCstr));
  cguard->cCstr->cIdx = cCount;
  cguard->cCstr->gType = GREATEREQUAL;
  cguard->cCstr->bndry = 1;
  clockId = (int *) malloc(sizeof(int)*1);
  clockId[0] = cCount;

  for (int i=0; i<nodeNum-1; i++){
      tmp->nxt = emalloc_ttrans(1);
      tmp = tmp->nxt;
      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[i],  &s[i+1]);

      tmp->nxt = emalloc_ttrans(1);
      tmp = tmp->nxt;

      // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
      create_ttrans(tmp, cguard, clockId, 1, &s[i+1],  &s[i]);

  }
  tmp->nxt = emalloc_ttrans(1);
  tmp = tmp->nxt;
  // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
  create_ttrans(tmp, cguard, clockId, 1, &s[nodeNum-1],  &s[0]);

  tmp->nxt = emalloc_ttrans(1);
  tmp = tmp->nxt;
  // void create_ttrans(TTrans *t, CGuard *cguard, int *cIdxs, int clockNum, TState *from, TState *to)
  create_ttrans(tmp, cguard, clockId, 1, &s[0],  &s[nodeNum-1]);
  free(clockId);
  free_CGuard(cguard);

  cCount++;

  t->tTrans = tt->nxt;
  t->tStates = s;
  t->stateNum = nodeNum;
  t->tEvents = NULL;
  t->eventNum = 0;

  free_ttrans(tt,0);
  return t;
}

/********************************************************************\
|*                Display of the Timed Automata                     *|
\********************************************************************/

void print_CGuard(CGuard *cg){
  if (!cg){
    fprintf(tl_out, "* ");
    return;
  }else{
    switch (cg->nType){
      case AND:
        print_CGuard(cg->lft);
        fprintf(tl_out, "&& ");
        print_CGuard(cg->rgt);
        break;

      case OR:
        print_CGuard(cg->lft);
        fprintf(tl_out, "|| ");
        print_CGuard(cg->rgt);
        break;

      case START:
        fprintf(tl_out, "z%d'==1", cg->lft->cCstr->cIdx);
        break;

      case STOP:
      {
        int start = cg->lft->cCstr->cIdx;
        int end = cg->rgt->cCstr->cIdx;
        int first = 1;
        for (int i =start; i<=end; i++){
          if (first){
            fprintf(tl_out, "z%d'==0", i);
            first = 0;
          } else if (!first) fprintf(tl_out, "&& z%d'==0", i);
        }
        break;
      }

      case PREDICATE:
        fprintf(tl_out, "z%d ", cg->cCstr->cIdx);
        if(cg->cCstr->gType == GREATER)    fprintf(tl_out, "> ");
        else if(cg->cCstr->gType == GREATEREQUAL)    fprintf(tl_out, ">= ");
        else if(cg->cCstr->gType == LESS)    fprintf(tl_out, "< ");
        else if(cg->cCstr->gType == LESSEQUAL)    fprintf(tl_out, "<= ");
        fprintf(tl_out, "%d ", cg->cCstr->bndry);
        break;

      default:
        printf("Strange type: %d", cg->nType);
        break;
    }
  }
}

void CGuard_to_xml(CGuard *cg, char* res){
  if (!cg){
    res = NULL;
    return;
  }else{
    char buffer[15];
    switch (cg->nType){
      case AND:
        strcat(res, "(");
        CGuard_to_xml(cg->lft, res);
        strcat(res, " && ");
        CGuard_to_xml(cg->rgt, res);
        strcat(res, ")");
        break;

      case OR:
        strcat(res, "(");
        CGuard_to_xml(cg->lft, res);
        strcat(res, " || ");
        CGuard_to_xml(cg->lft, res);
        strcat(res, ")");
        break;
      
      case START:
        sprintf(buffer, "z[%d]'==1", cg->lft->cCstr->cIdx);
        strcat(res, buffer);
        break;

      case STOP:
      {
        int start = cg->lft->cCstr->cIdx;
        int end = cg->rgt->cCstr->cIdx;
        int first = 1;
        for (int i =start; i<=end; i++){
          if (first){
            sprintf(buffer, "z[%d]'==0", i);
            first = 0;
            strcat(res, buffer);
          } else if (!first){
            sprintf(buffer, "&& z[%d]'==0", i);
            strcat(res, buffer);
          }
        }
        break;
      }

      case PREDICATE:
        sprintf(buffer, "z[%d]", cg->cCstr->cIdx);
        strcat(res, buffer);
        if(cg->cCstr->gType == GREATER)    strcat(res, ">");
        else if(cg->cCstr->gType == GREATEREQUAL)    strcat(res, ">=");
        else if(cg->cCstr->gType == LESS)    strcat(res, "<");
        else if(cg->cCstr->gType == LESSEQUAL)    strcat(res, "<=");
        sprintf(buffer, "%d", cg->cCstr->bndry);
        strcat(res, buffer);
        break;
    }
  }
}

void print_timed(TAutomata *t) /* dumps the alternating automaton */
{
 //  int i;
 //  ATrans *t;

 //  fprintf(tl_out, "init :\n");
 //  for(t = transition[0]; t; t = t->nxt) {
 //    print_set(t->to, 0);
 //    fprintf(tl_out, "\n");
 //  }
  TTrans *tmp;
  tmp = t->tTrans;
  int j = 0;
  while (tmp != NULL) {
    fprintf(tl_out, "Transition %i : %i to %i \n", j, (int) (tmp->from - &t->tStates[0]), (int) (tmp->to - &t->tStates[0]));
    fprintf(tl_out, "Clock reseted: ");
    print_set(tmp->cIdx, 4);
    fprintf(tl_out, "\nGuards Condition: ");
    print_CGuard(tmp->cguard);
    fprintf(tl_out, "\n");
    j++;
    tmp = tmp->nxt;
  }
  for (int i=0; i< t->stateNum; i++){
    fprintf(tl_out, "State %i : %s \n   input: (", i, t->tStates[i].tstateId);
    for (int j=0; j< t->tStates[i].inputNum; j++){
      fprintf(tl_out,"%#06x, ", t->tStates[i].input[j]);
    }
    fprintf(tl_out,") output: (%#06x) \n", t->tStates[i].output);

    if (t->tStates[i].sym != NULL){
      fprintf(tl_out, "   symbols: ");
      print_set(t->tStates[i].sym,3);
      fprintf(tl_out, "\n");
    }
    fprintf(tl_out, "   invariants: ");
    print_CGuard(t->tStates[i].inv);
    fprintf(tl_out, "\n");
  }


 //  for(i = node_id - 1; i > 0; i--) {
 //    if(!label[i])
 //      continue;
 //    fprintf(tl_out, "state %i : ", i);
 //    dump(label[i]);
 //    fprintf(tl_out, "\n");
 //    for(t = transition[i]; t; t = t->nxt) {
 //      if (empty_set(t->pos, 1) && empty_set(t->neg, 1))
	// fprintf(tl_out, "1");
 //      print_set(t->pos, 1);
 //      if (!empty_set(t->pos,1) && !empty_set(t->neg,1)) fprintf(tl_out, " & ");
 //      print_set(t->neg, 2);
 //      fprintf(tl_out, " -> ");
 //      print_set(t->to, 0);
 //      fprintf(tl_out, "\n");
 //    }
 //  }
}


void timed_to_xml(TAutomata *t, int clockSize, FILE *xml) /* dumps the alternating automaton */
{
 //  int i;
 //  ATrans *t;

 //  fprintf(tl_out, "init :\n");
 //  for(t = transition[0]; t; t = t->nxt) {
 //    print_set(t->to, 0);
 //    fprintf(tl_out, "\n");
 //  }
  fprintf(xml, "#!/usr/bin/python\nfrom pyuppaal import *\ndef main():\n\tbuchiLoc = []\n\tinitalLoc = []\n\tlocid = 0\n\tlocations = []\n\ttransitions = []\n");

  int j = 0;
  char buffer[80];
  char setBuffer[50];
  for (int i=0; i< t->stateNum; i++){
    buffer[0] = '\0';
    CGuard_to_xml(t->tStates[i].inv, buffer);
    if(t->tStates[i].buchi== 1){
      fprintf(xml,"\tlocations.append( Location(invariant=\"%s\", urgent=False, committed=False, name='loc%i_b', id = 'id'+str(locid)) )\n", buffer, i);
      fprintf(xml,"\tbuchiLoc.append(locid)\n");
    }else{
      fprintf(xml,"\tlocations.append( Location(invariant=\"%s\", urgent=False, committed=False, name='loc%i', id = 'id'+str(locid)) )\n", buffer, i);
    }
    if ( (strstr(t->tStates[i].tstateId, "loc0")) && (strstr(t->tStates[i].tstateId, "CHK00: <>_I (a)") || strstr(t->tStates[i].tstateId, "CHK01: <>_I (a)") ) && (strstr(t->tStates[i].tstateId, "CHK00: <>_I (b)") || strstr(t->tStates[i].tstateId, "CHK01: <>_I (b)") ))
      fprintf(xml,"\tinitalLoc.append(locid)\n");

    fprintf(xml, "\tlocid +=1\n");
  }
  fprintf(xml, "\tlocations.append(Location(invariant='', urgent=False, committed=False, name='final', id = 'id'+str(locid)))\n");

  fprintf(xml, "\tlocations.append(Location(invariant='', urgent=False, committed=False, name='initial', id = 'id'+str(locid+1)))\n");

  TTrans *tmp;
  tmp = t->tTrans;

  while (tmp != NULL) {
    buffer[0] = '\0';
    CGuard_to_xml(tmp->cguard, buffer);
    setBuffer[0] = '\0';
    set_to_xml(tmp->cIdx, setBuffer);
    if (strstr(tmp->from->tstateId,"CHK_2: <>_I (b)") && strstr(tmp->to->tstateId,"CHK_4: <>_I (b)")){
      fprintf(xml, "\ttransitions.append( Transition(locations[%i], locations[%i], guard='%s', assignment='%s', synchronisation='ch[1]!') )\n", (int) (tmp->from - &t->tStates[0]), (int) (tmp->to - &t->tStates[0]) , buffer, setBuffer);
    }
    else if (strstr(tmp->from->tstateId,"CHK_2: <>_I (a)") && strstr(tmp->to->tstateId,"CHK_4: <>_I (a)"))
      fprintf(xml, "\ttransitions.append( Transition(locations[%i], locations[%i], guard='%s', assignment='%s', synchronisation='ch[0]!') )\n", (int) (tmp->from - &t->tStates[0]), (int) (tmp->to - &t->tStates[0]) , buffer, setBuffer);
    else
      fprintf(xml, "\ttransitions.append( Transition(locations[%i], locations[%i], guard='%s', assignment='%s') )\n", (int) (tmp->from - &t->tStates[0]), (int) (tmp->to - &t->tStates[0]) , buffer, setBuffer);
    j++;
    tmp = tmp->nxt;
  }

  fprintf(xml, "\tfor i in buchiLoc:\n\t\ttransitions.append( Transition(locations[i], locations[locid], guard='', assignment=''))\n");
  fprintf(xml, "\tfor i in initalLoc:\n\t\ttransitions.append( Transition(locations[locid+1], locations[i], guard='', assignment=''))\n");

  fprintf(xml, "\ttemplate = Template('sys', locations=locations, transitions=transitions, initlocation=locations[locid+1])\n");

  fprintf(xml, "\ttemplate.layout(auto_nails = True);\n\tnta = NTA(system = 'system sys;', templates=[template], declaration='clock z[%i]; broadcast chan ch[2];')\n\tf = open('test.xml', 'w')\n\tf.write(nta.to_xml())\nif __name__ == '__main__':\n\tmain()\n",clockSize);

 //  for(i = node_id - 1; i > 0; i--) {
 //    if(!label[i])
 //      continue;
 //    fprintf(tl_out, "state %i : ", i);
 //    dump(label[i]);
 //    fprintf(tl_out, "\n");
 //    for(t = transition[i]; t; t = t->nxt) {
 //      if (empty_set(t->pos, 1) && empty_set(t->neg, 1))
  // fprintf(tl_out, "1");
 //      print_set(t->pos, 1);
 //      if (!empty_set(t->pos,1) && !empty_set(t->neg,1)) fprintf(tl_out, " & ");
 //      print_set(t->neg, 2);
 //      fprintf(tl_out, " -> ");
 //      print_set(t->to, 0);
 //      fprintf(tl_out, "\n");
 //    }
 //  }
}
/********************************************************************\
|*                       Main method                                *|
\********************************************************************/

void mk_timed(Node *p) /* generates an timed automata for p */
{
  // if(tl_stats) getrusage(RUSAGE_SELF, &tr_debut);

  t_clock_size = t_calculate_clock_size(p) + 1; /* number of states in the automaton */
  // t_label = (Node **) tl_emalloc(t_node_size * sizeof(Node *));
  // t_transition = (TTrans **) tl_emalloc(t_node_size * sizeof(TTrans *));
  // node_size = node_size / (8 * sizeof(int)) + 1;

  t_sym_size = t_calculate_sym_size(p); /* number of predicates */
  if (t_sym_size) t_sym_table = (char **) tl_emalloc(t_sym_size * sizeof(char *));
  t_sym_size = t_sym_size / (8 * sizeof(int)) + 1;
  
//   final_set = make_set(-1, 0);
  cCount = 0;

  TAutomata* mapAutomata = create_map_loop(6,1,2);


  print_timed(mapAutomata);

  tAutomata = build_timed(p); /* generates the alternating automaton */

  // fprintf(tl_out, "Clock count total: %i\n",cCount);

  print_timed(tAutomata);


  TAutomata *tResult = (TAutomata *) tl_emalloc(sizeof(TAutomata));
  merge_map_timed(mapAutomata, tAutomata, tResult);

  print_timed(tResult);

  FILE *xml;
  char outputFileName[20] = "scripts/uppaal.py";
  xml = fopen(outputFileName, "w");

  if (xml == NULL) {
    fprintf(stderr, "Can't open output file %s!\n", outputFileName);
    exit(1);
  }
  timed_to_xml(tResult, t_clock_size, xml);

  free_ttrans(tResult->tTrans,1);
  free_tstate(tResult->tStates, tResult->stateNum);

  tfree(tResult);

  free_all_ttrans();
  releasenode(1, p);

  tfree(t_sym_table);
  fclose(xml);


//   if(tl_verbose) {
//     fprintf(tl_out, "\nAlternating automaton before simplification\n");
//     print_alternating();
//   }

//   if(tl_simp_diff) {
//     simplify_astates(); /* keeps only accessible states */
//     if(tl_verbose) {
//       fprintf(tl_out, "\nAlternating automaton after simplification\n");
//       print_alternating();
//     }
//   }
  
//   if(tl_stats) {
//     getrusage(RUSAGE_SELF, &tr_fin);
//     timeval_subtract (&t_diff, &tr_fin.ru_utime, &tr_debut.ru_utime);
//     fprintf(tl_out, "\nBuilding and simplification of the alternating automaton: %i.%06is",
// 		t_diff.tv_sec, t_diff.tv_usec);
//     fprintf(tl_out, "\n%i states, %i transitions\n", astate_count, atrans_count);
//   }

//   releasenode(1, p);
//   tfree(label);
}
// #endif