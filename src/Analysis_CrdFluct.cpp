#include <cmath>
#include "Analysis_CrdFluct.h"
#include "CpptrajStdio.h"
#include "Constants.h" // PI
#include "StringRoutines.h" // integerToString

Analysis_CrdFluct::Analysis_CrdFluct() : 
  coords_(0),
  bfactor_(true),
  windowSize_(-1)
{}

void Analysis_CrdFluct::Help() {
  mprintf("crdfluct <crd set name> [out <filename>] [window <size>]\n");
}

// Analysis_CrdFluct::Setup()
Analysis::RetType Analysis_CrdFluct::Setup(ArgList& analyzeArgs, DataSetList* datasetlist,
                            TopologyList* PFLin, int debugIn)
{
  std::string setname = analyzeArgs.GetStringNext();
  if (setname.empty()) {
    mprinterr("Error: crdfluct: Specify set name.\n");
    Help();
    return Analysis::ERR;
  }
  coords_ = (DataSet_Coords*)datasetlist->FindSetOfType( setname, DataSet::COORDS );
  if (coords_ == 0) {
    mprinterr("Error: crdfluct: Could not locate COORDS set corresponding to %s\n",
              setname.c_str());
    return Analysis::ERR;
  }
  outfilename_ = analyzeArgs.GetStringKey("out");
  windowSize_ = analyzeArgs.getKeyInt("window", -1);

  mprintf("    CRDFLUCT: Atomic fluctuations will be calcd for set %s\n", 
          coords_->Legend().c_str());
  if (windowSize_ != -1) mprintf("\tWindow size = %i\n", windowSize_);
  if (!outfilename_.empty()) mprintf("\tOutput to %s\n", outfilename_.c_str());

  // Set up data sets
  setname = analyzeArgs.GetStringNext();
  if (windowSize_ < 1) {
    // Only one data set for total B-factors
    DataSet* ds = datasetlist->AddSet( DataSet::DOUBLE, setname, "fluct" );
    if (ds == 0) return Analysis::ERR;
    outSets_.push_back( ds );
  } else {
    if (setname.empty()) setname = datasetlist->GenerateDefaultName("fluct");
    // Determine how many windows will be needed
    int nwindows = datasetlist->MaxFrames() / windowSize_;
    for (int win = 0; win < nwindows; ++win) {
      int frame = (win + 1) * windowSize_;
      DataSet* ds = datasetlist->AddSetIdx( DataSet::DOUBLE, setname, frame );
      if (ds == 0) return Analysis::ERR;
      ds->SetLegend( "F_" + integerToString( frame ) );
      outSets_.push_back( ds );
    }
    if ( (datasetlist->MaxFrames() % windowSize_) != 0 ) {
      DataSet* ds = datasetlist->AddSetIdx( DataSet::DOUBLE, setname, datasetlist->MaxFrames() );
      ds->SetLegend("Final");
      outSets_.push_back( ds );
    }
    for (SetList::iterator out = outSets_.begin(); out != outSets_.end(); ++out)
      mprintf("\t%s\n", (*out)->Legend().c_str());
  }

  return Analysis::OK;
}

// Analysis_CrdFluct::CalcBfactors()
void Analysis_CrdFluct::CalcBfactors( Frame SumCoords, Frame SumCoords2, double Nsets,
                                      DataSet& outset )
{
  SumCoords.Divide(Nsets);
  SumCoords2.Divide(Nsets);
  //SumCoords2 = SumCoords2 - (SumCoords * SumCoords);
  SumCoords *= SumCoords;
  SumCoords2 -= SumCoords;
  AtomMask::const_iterator maskat = coords_->Mask().begin();
  if (bfactor_) {
    // Set up b factor normalization
    // B-factors are (8/3)*PI*PI * <r>**2 hence we do not sqrt the fluctuations
    double bfac = (8.0/3.0)*PI*PI;
    for (int i = 0; i < SumCoords2.size(); i+=3) {
      double fluct = (SumCoords2[i] + SumCoords2[i+1] + SumCoords2[i+2]) * bfac;
      outset.Add( *maskat, &fluct );
      ++maskat; 
    }
  } else {
    // Atomic fluctuations
    for (int i = 0; i < SumCoords2.size(); i+=3) {
      double fluct = SumCoords2[i] + SumCoords2[i+1] + SumCoords2[i+2];
      if (fluct > 0)
        outset.Add( *maskat, &fluct );
      ++maskat;
    }
  }
}

// Analysis_CrdFluct::Analyze()
Analysis::RetType Analysis_CrdFluct::Analyze() {
  int end = coords_->Size();
  mprintf("\tFluctuation analysis for %i frames (%i atoms each).\n", end, 
          coords_->Natom());
  Frame currentFrame( coords_->Natom() );
  Frame SumCoords( coords_->Natom() );
  SumCoords.ZeroCoords();
  Frame SumCoords2( coords_->Natom() );
  SumCoords2.ZeroCoords();
  int w_count = 0;
  SetList::iterator out = outSets_.begin();
  for (int frame = 0; frame < end; frame++) {
    currentFrame = (*coords_)[ frame ];
    SumCoords += currentFrame;
    SumCoords2 += ( currentFrame * currentFrame );
    ++w_count;
    if (w_count == windowSize_) {
      CalcBfactors( SumCoords, SumCoords2, (double)frame, *(*out) );
      ++out;
      w_count = 0;
    }
  }

  if (windowSize_ < 1 || w_count != 0) {
    // For windowSize < 1 this is the only b-factor calc
    CalcBfactors( SumCoords, SumCoords2, (double)end, *(*out) );
    if (w_count != 0) 
      mprintf("Warning: Number of frames (%i) was not evenly divisible by window size.\n",
               end);
  }

  return Analysis::OK;
}

void Analysis_CrdFluct::Print( DataFileList* datafilelist ) {
  if (outfilename_.empty()) return;
  DataFile* outfile = datafilelist->AddDataFile(outfilename_);
  if (outfile == 0) return;
  for (SetList::iterator set = outSets_.begin(); set != outSets_.end(); ++set)
    outfile->AddSet( *set );
  if (bfactor_)
    outfile->ProcessArgs("ylabel B-factors");
  outfile->ProcessArgs("xlabel Atom noemptyframes");
}