#ifndef INC_ACTION_DNAIONTRACKER_H
#define INC_ACTION_DNAIONTRACKER_H
#include "Action.h"
#include "ImagedAction.h"
class Action_DNAionTracker : public Action, ImagedAction {
  public:
    Action_DNAionTracker();
  private:
    int init();
    int setup();
    int action();

    DataSet* distance_;
    enum BINTYPE { COUNT=0, SHORTEST, TOPCONE, BOTTOMCONE };
    BINTYPE bintype_; // iarg3
    double poffset_; // darg2
    bool useMass_;
    AtomMask p1_;
    AtomMask p2_;
    AtomMask base_;
    AtomMask ions_;
};
#endif
