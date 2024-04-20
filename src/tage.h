#include <queue>
#include <random>
#include <iostream>

#define NUM_TABLES      4   // #tables excepts for base predictor
#define BASE_TABLE_BITS 12
#define TAGE_TABLE_BITS 10
#define TAGE_TAG_BITS   8
#define MINHISTORY      5
#define HIST_BITS       64

class tage_update: public branch_update {
  public :
    int           mainsel;
    int           altsel;

    bool          base_pred;
    unsigned int  base_index;

    bool          tage_pred[NUM_TABLES];
    unsigned int  tage_index[NUM_TABLES];
    unsigned int  tage_tag[NUM_TABLES];
};

template <std::size_t BITS>
class ghist: public std::bitset<BITS> {
  using std::bitset<BITS>::bitset;
  public :
    ghist(const std::bitset<BITS>& g) : std::bitset<BITS>{g} {}

    ghist<BITS> mask_ghist(unsigned int ghistlen) {
      int shift = (BITS - ghistlen);
      return (*this << shift) >> shift;
    }

    ghist<BITS> fold_ghist(unsigned int ghistlen, unsigned int hashlen) {
      ghist<BITS> hash(0x00);
      for (unsigned int i = 0; i < (ghistlen+hashlen-1)/hashlen; i++) {
        hash ^= (*this >> (i*hashlen));
      }

      return hash.mask_ghist(hashlen);
    }

    void update(bool result) {
      *this = (*this << 1) | ghist<BITS>(result);
    }
};

struct base_table {
  unsigned char pcnt[(1<<BASE_TABLE_BITS)];   // prediction counter
};
struct tage_table {
  unsigned char tag[(1<<TAGE_TABLE_BITS)];    // partial tag
  unsigned char pcnt[(1<<TAGE_TABLE_BITS)];   // predicton counter
  unsigned char ucnt[(1<<TAGE_TABLE_BITS)];   // useful counter
};
class tage : public branch_predictor {
  public :
    tage_update       u;
    branch_info       bi;
    ghist<HIST_BITS>  history;
    base_table        btab;
    tage_table        ttab[NUM_TABLES];
    unsigned int      tlength[NUM_TABLES];

    tage(void) : history("0") {
      memset(btab.pcnt, 0, sizeof(btab.pcnt));
      memset(ttab, 0, sizeof(ttab));
      
      double gratio = pow((double)HIST_BITS/(double)MINHISTORY, 1.0/(NUM_TABLES-1));
      tlength[0]    = MINHISTORY;
      tlength[NUM_TABLES-1] = HIST_BITS;
      for (int i = 1; i < NUM_TABLES-1; i++) {
        tlength[i] = (int)(pow(gratio, i) * (double)MINHISTORY + 0.5);
      }
    }

    branch_update *predict(branch_info &b) {
      bi            = b;
      u.base_index  = b.address & ((1<<BASE_TABLE_BITS)-1);
      u.base_pred   = btab.pcnt[u.base_index] >> 2;

		  if (b.br_flags & BR_CONDITIONAL) {
        int mainsel = -1;
        int altsel  = -1;
        for (int i = 0; i < NUM_TABLES; i++) {
          // hash calculation
          unsigned int csr, csr1, csr2;
          ghist<HIST_BITS> hmasked = history.mask_ghist(tlength[i]);
          csr  = hmasked.fold_ghist(tlength[i], TAGE_TABLE_BITS).to_ulong();
          csr1 = hmasked.fold_ghist(tlength[i], TAGE_TAG_BITS).to_ulong();
          csr2 = hmasked.fold_ghist(tlength[i], TAGE_TAG_BITS-1).to_ulong();

          int pc_hash     = ((b.address >> 2) ^ (b.address >> (2+TAGE_TABLE_BITS)));
          u.tage_index[i] = (csr ^ pc_hash) & ((1<<TAGE_TABLE_BITS)-1);
          u.tage_tag[i]   = ((b.address >> 2) ^ (csr1 ^ (csr2 << 1))) & ((1<<TAGE_TAG_BITS)-1);
          u.tage_pred[i]  = ttab[i].pcnt[u.tage_index[i]] >> 2;

          // make prediction
          if ( ttab[i].tag[u.tage_index[i]] == u.tage_tag[i] ) {
            // select tag-matched entry from table with longest history length
            altsel  = mainsel;
            mainsel = i;
          }
        }

        if ( mainsel == -1 ) {
          // from base predictor
		  	  u.direction_prediction(u.base_pred);
        } else {
          // from tagged components
          u.direction_prediction(u.tage_pred[mainsel]);
        }

        u.mainsel = mainsel;
        u.altsel  = altsel;
		  } else {
		  	u.direction_prediction (true);
		  }
		  u.target_prediction (0);
		  return &u;
    }

    void update (branch_update *u, bool taken, unsigned int target) {
		  if (bi.br_flags & BR_CONDITIONAL) {
        unsigned int  base_idx  = ((tage_update*)u)->base_index;
        bool          base_pred = ((tage_update*)u)->base_pred;
		  	unsigned char *base_p   = &btab.pcnt[base_idx];
        int           mainsel   = ((tage_update*)u)->mainsel;
        int           altsel    = ((tage_update*)u)->altsel;
        bool          allocate;
        unsigned char ucnt[NUM_TABLES];
        unsigned char pcnt[NUM_TABLES];

        for ( int i = 0; i < NUM_TABLES; i++) {
          ucnt[i] = ttab[i].ucnt[((tage_update*)u)->tage_index[i]];
          pcnt[i] = ttab[i].pcnt[((tage_update*)u)->tage_index[i]];
        }


        // update predictor count
        if ( mainsel == -1 ) {
          *base_p  = cnt_update(*base_p, taken, 7);
          allocate = (base_pred != taken);
        } else {
          unsigned int  main_idx   = ((tage_update*)u)->tage_index[mainsel];
          bool          main_pred  = ((tage_update*)u)->tage_pred[mainsel];
          bool          alt_pred   = (altsel == -1) ? base_pred
                                   :                  ((tage_update*)u)->tage_pred[altsel];

          // update useful count
          bool ucnt_upd   = main_pred ^ alt_pred;
          bool ucnt_inc   = (main_pred == taken) && (alt_pred != taken);
          ttab[mainsel].pcnt[main_idx] = cnt_update(pcnt[mainsel], taken, 7);
          if ( ucnt_upd ) {
            ttab[mainsel].ucnt[main_idx] = cnt_update(ucnt[mainsel], ucnt_inc, 7);
          }

          allocate = (main_pred != taken) && (alt_pred != taken);
        }

        // allocate new entry
        if ( allocate && (mainsel < (NUM_TABLES - 1)) ) {
          int allocsel = select_allocate(mainsel, ucnt);

          if ( allocsel == -1 ) {
            // when all entry is useful,
            // decrement u_i (mainsel < i < NUM_TABLES)
            for (int i = mainsel + 1; i < NUM_TABLES; i++) {
              int idx           = ((tage_update*)u)->tage_index[i];
              ttab[i].ucnt[idx] = cnt_update(ucnt[i], false, 7);
            }
          } else {
            // allocate new entry
            int initpcnt = taken ? 0x4 : 0x3;
            int idx      = ((tage_update*)u)->tage_index[allocsel];
            ttab[allocsel].pcnt[idx] = initpcnt;
            ttab[allocsel].ucnt[idx] = 0x0;
            ttab[allocsel].tag[idx]  = ((tage_update*)u)->tage_tag[allocsel];
          }
        }

        history.update(taken);
		  }
    }

  private:
    unsigned char cnt_update(unsigned char cnt, bool dir, unsigned char max) {
      if ( dir ) {
        // dir = true : increment
        return (cnt >= max) ? max : cnt + 1;
      } else {
        // dir = false: decrement
        return (cnt == 0) ? 0 : cnt - 1;
      }
    }

    int select_allocate(int mainsel, unsigned char *ucnt) {
      std::queue<int> selq;

      for (int i = mainsel + 1; i < NUM_TABLES; i++) {
        if (ucnt[i] == 0) selq.push(i);
      }

      // allocation failed
      if ( selq.empty() ) return -1;

      // select one from non-useful entries
      double threshold = 0.5;
      int    entsel;
      std::random_device  rd;
      std::default_random_engine eng(rd());
      std::uniform_real_distribution<double> distr(0.0, 1.0);
      while ( !selq.empty() ) {
        entsel = selq.front();

        if ( distr(eng) < threshold ) break;

        selq.pop();
      }

      return entsel;
    }
};
