// predict.cc
// This file contains the main function.  The program accepts a single 
// parameter: the name of a trace file.  It drives the branch predictor
// simulation by reading the trace file and feeding the traces one at a time
// to the branch predictor.

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // in case you want to use e.g. memset
#include <assert.h>
#include <math.h>
#include <bitset>

#include "branch.h"
#include "trace.h"
#include "predictor.h"
//#include "my_predictor.h"
#include "gshare.h"
#include "bimodal.h"
#include "tage.h"

int main (int argc, char *argv[]) {

	// make sure there is one parameter

	if (argc != 3) {
		fprintf (stderr, "Usage: %s <filename>.gz <predictor type>\n", argv[0]);
		exit (1);
	}

	// open the trace file for reading

	init_trace (argv[1]);
  int pred_sel = strtol(argv[2], NULL, 0);

	// initialize competitor's branch prediction code

	//branch_predictor *p = new my_predictor ();
	branch_predictor *p_gshare = new gshare();
  branch_predictor *p_bimodal = new bimodal();
  branch_predictor *p_tage = new tage();
  branch_predictor *p;
  switch (pred_sel) {
    case 0 : 
      fprintf(stderr, "\n[Selected: Gshare]\n");
      p = p_gshare;
      break;
    case 1 :
      fprintf(stderr, "\n[Selected: Bimodal]\n");
      p = p_bimodal;
      break;
    case 2 :
      fprintf(stderr, "\n[Selected: Tage]\n");
      p = p_tage;
      break;
    default:
      fprintf(stderr, "\nError: Invalid predictor selection\n");
      exit(2);
  }


	// some statistics to keep, currently just for conditional branches

	long long int 
		tmiss = 0, 	// number of target mispredictions
		dmiss = 0; 	// number of direction mispredictions

	// keep looping until end of file

	for (;;) {

		// get a trace

		trace *t = read_trace ();

		// NULL means end of file

		if (!t) break;

		// send this trace to the competitor's code for prediction

		branch_update *u = p->predict (t->bi);

		// collect statistics for a conditional branch trace

		if (t->bi.br_flags & BR_CONDITIONAL) {

			// count a direction misprediction

			dmiss += u->direction_prediction () != t->taken;

			// count a target misprediction

			tmiss += u->target_prediction () != t->target;
		}

		// update competitor's state

		p->update (u, t->taken, t->target);
	}

	// done reading traces

	end_trace ();

	// give final mispredictions per kilo-instruction and exit.
	// each trace represents exactly 100 million instructions.

	printf ("%0.3f MPKI\n", 1000.0 * (dmiss / 1e8));
	delete p;
	exit (0);
}
