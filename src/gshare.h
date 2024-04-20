class gshare_update: public branch_update {
  public :
    unsigned int index;
};

#define HISTORY_LENGTH  15
#define TABLE_BITS      15
class gshare : public branch_predictor {
  public :
    gshare_update u;
    branch_info   bi;
    //unsigned int  history;
    std::bitset<HISTORY_LENGTH> history;
    unsigned char tab[1<<TABLE_BITS];

    gshare(void) : history("0") {
      memset(tab, 0, sizeof(tab));
    }

    branch_update *predict(branch_info & b) {
      bi = b;
		  if (b.br_flags & BR_CONDITIONAL) {
        std::bitset<HISTORY_LENGTH> adl(b.address & ((1<<TABLE_BITS)-1));
        std::bitset<HISTORY_LENGTH> hash;
        hash = (history << (TABLE_BITS - HISTORY_LENGTH)) ^ adl;

		  	u.index = hash.to_ullong();
		  	u.direction_prediction (tab[u.index] >> 1);
		  } else {
		  	u.direction_prediction (true);
		  }
		  u.target_prediction (0);
		  return &u;
    }

    void update (branch_update *u, bool taken, unsigned int target) {
		  if (bi.br_flags & BR_CONDITIONAL) {
		  	unsigned char *c = &tab[((gshare_update*)u)->index];
		  	if (taken) {
		  		if (*c < 3) (*c)++;
		  	} else {
		  		if (*c > 0) (*c)--;
		  	}
		  	history <<= 1;
		  	history |= taken;
		  	history &= (1<<HISTORY_LENGTH)-1;
		  }
    }
};
