#include "Analysis_TI.h"
#include "CpptrajStdio.h"
#include "DataSet_Mesh.h"
#include "StringRoutines.h" // integerToString
#include "Bootstrap.h"

/// CONSTRUCTOR
Analysis_TI::Analysis_TI() :
  Analysis(HIDDEN),
  nskip_(0),
  dAout_(0),
  mode_(GAUSSIAN_QUAD),
  avgType_(AVG),
  debug_(0),
  n_bootstrap_pts_(0),
  n_bootstrap_samples_(100)
{}


void Analysis_TI::Help() const {
  mprintf("\t<dset0> [<dset1> ...] {nq <n quad pts> | xvals <x values>}\n"
          "\t[nskip <# to skip>] [name <set name>] [out <file>]\n"
          "\t[curveout <ti curve file>] [bsout <bootstrap vals file>]\n"
          "\t[bs_pts <points>] [bs_samples <samples>]\n"
          "  Calculate free energy from Amber TI output. If 'nskip' is specified\n"
          "  (where <# to skip> may be a comma-separated list of numbers) the average\n"
          "  DV/DL and final free energy will be calculated skipping over the specified\n"
          "  number of points (for assessing convergence).\n");
}

// Analysis_TI::Setup()
Analysis::RetType Analysis_TI::Setup(ArgList& analyzeArgs, AnalysisSetup& setup, int debugIn)
{
  debug_ = debugIn;
  int nq = analyzeArgs.getKeyInt("nq", 0);
  ArgList nskipArg(analyzeArgs.GetStringKey("nskip"), ","); // Comma-separated
  avg_interval_ = analyzeArgs.getKeyInt("interval", -1);
  avg_max_ = analyzeArgs.getKeyInt("avgmax", -1);
  avg_skip_ = analyzeArgs.getKeyInt("avgskip", 0);
  avgType_ = AVG;
  if (!nskipArg.empty()) {
    avgType_ = SKIP;
    // Specified numbers of points to skip
    nskip_.clear();
    for (int i = 0; i != nskipArg.Nargs(); i++) {
      nskip_.push_back( nskipArg.getNextInteger(0) );
      if (nskip_.back() < 0) nskip_.back() = 0;
    }
  } else if (avg_interval_ > 0)
    avgType_ = INCREMENT;
  // Get lambda values
  ArgList xArgs(analyzeArgs.GetStringKey("xvals"), ","); // Also comma-separated
  if (!xArgs.empty()) {
    xval_.clear();
    for (int i = 0; i != xArgs.Nargs(); i++)
      xval_.push_back( xArgs.getNextDouble(0.0) );
  }
  n_bootstrap_pts_ = analyzeArgs.getKeyInt("bs_pts", -1);
  n_bootstrap_samples_ = analyzeArgs.getKeyInt("bs_samples", 100);
  bootstrap_seed_ = analyzeArgs.getKeyInt("bs_seed", -1);
  std::string setname = analyzeArgs.GetStringKey("name");
  DataFile* outfile = setup.DFL().AddDataFile(analyzeArgs.GetStringKey("out"), analyzeArgs);
  DataFile* curveout = setup.DFL().AddDataFile(analyzeArgs.GetStringKey("curveout"), analyzeArgs);
  DataFile* bsout = setup.DFL().AddDataFile(analyzeArgs.GetStringKey("bsout"), analyzeArgs);
  // Select datasets from remaining args
  if (input_dsets_.AddSetsFromArgs( analyzeArgs.RemainingArgs(), setup.DSL() )) {
    mprinterr("Error: Could not add data sets.\n");
    return Analysis::ERR;
  }
  if (input_dsets_.empty()) {
    mprinterr("Error: No input data sets.\n");
    return Analysis::ERR;
  }
  if (SetQuadAndWeights(nq)) return Analysis::ERR;
  // Determine integration mode
  if (nq > 0)
    mode_ = GAUSSIAN_QUAD;
  else
    mode_ = TRAPEZOID;
  // Check that # abscissas matches # data sets
  if (xval_.size() != input_dsets_.size()) {
     mprinterr("Error: Expected %zu data sets for integration, got %zu\n",
               input_dsets_.size(), xval_.size());
    return Analysis::ERR;
  }
  // Set up output data sets
  dAout_ = setup.DSL().AddSet(DataSet::XYMESH, setname, "TI");
  if (dAout_ == 0) return Analysis::ERR;
  if (outfile != 0) outfile->AddDataSet( dAout_ );
  MetaData md(dAout_->Meta().Name(), "TIcurve");
  if (avgType_ == AVG) {
    // Single curve
    curve_.push_back( setup.DSL().AddSet(DataSet::XYMESH, md) );
    if (curve_.back() == 0) return Analysis::ERR;
  } else if (avgType_ == SKIP) {
    // As many curves as skip values
    for (Iarray::const_iterator it = nskip_.begin(); it != nskip_.end(); ++it) {
      md.SetIdx( *it );
      DataSet* ds = setup.DSL().AddSet(DataSet::XYMESH, md);
      if (ds == 0) return Analysis::ERR;
      ds->SetLegend( md.Name() + "_Skip" + integerToString(*it) );
      if (curveout != 0) curveout->AddDataSet( ds );
      curve_.push_back( ds );
    }
  }
  // NOTE: INCREMENT is set up once data set size is known 
  if (n_bootstrap_samples_ > 0) {
    // Bootstrap data sets
    orig_avg_ = setup.DSL().AddSet(DataSet::XYMESH, MetaData(dAout_->Meta().Name(), "oavg"));
    bs_avg_   = setup.DSL().AddSet(DataSet::XYMESH, MetaData(dAout_->Meta().Name(), "bsavg"));
    bs_sd_    = setup.DSL().AddSet(DataSet::XYMESH, MetaData(dAout_->Meta().Name(), "bssd"));
    if (orig_avg_ == 0 || bs_avg_ == 0 || bs_sd_ == 0) return Analysis::ERR;
    if (bsout != 0) {
      bsout->AddDataSet(orig_avg_);
      bsout->AddDataSet(bs_avg_);
      bsout->AddDataSet(bs_sd_);
    }
  }

  mprintf("    TI: Calculating TI");
  if (mode_ == GAUSSIAN_QUAD) {
    mprintf(" using Gaussian quadrature with %zu points.\n", xval_.size());
    mprintf("\t%6s %8s %8s %s\n", "Point", "Abscissa", "Weight", "SetName");
    for (unsigned int i = 0; i != xval_.size(); i++)
      mprintf("\t%6i %8.5f %8.5f %s\n", i, xval_[i], wgt_[i], input_dsets_[i]->legend());
  } else {
    mprintf(" using the trapezoid rule.\n");
    mprintf("\t%6s %8s %s\n", "Point", "Abscissa", "SetName");
    for (unsigned int i = 0; i != xval_.size(); i++)
      mprintf("\t%6i %8.5f %s\n", i, xval_[i], input_dsets_[i]->legend());
  }
  if (avgType_ == AVG)
    mprintf("\tUsing all data points in <DV/DL> calc.\n");
  else if (avgType_ == SKIP) {
    mprintf("\tSkipping first");
    for (Iarray::const_iterator it = nskip_.begin(); it != nskip_.end(); ++it)
      mprintf(" %i", *it);
    mprintf(" data points for <DV/DL> calc.\n");
  } else if (avgType_ == INCREMENT) {
    mprintf("\tAveraging every %i points after skipping %i.",avg_interval_,avg_skip_);
    if (avg_max_ != -1)
      mprintf(" Max %i points.", avg_max_);
    mprintf("\n");
  }
  mprintf("\tResult(s) of integration(s) saved in set '%s'\n", dAout_->legend());
  mprintf("\tTI curve(s) saved in set(s)");
  if (avgType_ != INCREMENT)
    for (DSarray::const_iterator ds = curve_.begin(); ds != curve_.end(); ++ds)
      mprintf(" '%s'", (*ds)->legend());
  else
    mprintf(" named '%s'", md.PrintName().c_str());
  mprintf("\n");
  if (outfile != 0) mprintf("\tResults written to '%s'\n", outfile->DataFilename().full());
  if (curveout!= 0) mprintf("\tTI curve(s) written to '%s'\n", curveout->DataFilename().full());
  if (n_bootstrap_samples_ > 0) {
    mprintf("\tBootstrap error analysis will be performed for <DV/DL> values.\n");
    if (n_bootstrap_pts_ < 1)
      mprintf("\t75%% of input data points will be used for each bootstrap resample.\n");
    else
      mprintf("\tBootstrap resample size is %i data points\n", n_bootstrap_pts_);
    mprintf("\t%i bootstrap resamples.\n", n_bootstrap_samples_);
    if (bootstrap_seed_ != -1)
      mprintf("\tBoostrap base seed is %i\n", bootstrap_seed_);
  }
  return Analysis::OK;
}

// Analysis_TI::SetQuadAndWeights()
int Analysis_TI::SetQuadAndWeights(int nq) {
  if (nq < 1) return 0;
  xval_.resize(nq);
  wgt_.resize(nq);
  switch (nq) {
    case 1:
      xval_[0] = 0.5; wgt_[0] = 1.0;
      break;
    case 2:
      xval_[0] = 0.21132; wgt_[0] = wgt_[1] = 0.5;
      xval_[1] = 0.78867;
      break;
    case 3:
      xval_[0] = 0.1127;  wgt_[0] = wgt_[2] = 0.27777;
      xval_[1] = 0.5;     wgt_[1] = 0.44444;
      xval_[2] = 0.88729;
      break;
    case 5: 
      xval_[0] = 0.04691; wgt_[0] = wgt_[4] = 0.11846;
      xval_[1] = 0.23076; wgt_[1] = wgt_[3] = 0.23931;
      xval_[2] = 0.5;     wgt_[2] = 0.28444;
      xval_[3] = 0.76923;
      xval_[4] = 0.95308;
      break; 
    case 7:
      xval_[0] = 0.02544; wgt_[0] = wgt_[6] = 0.06474;
      xval_[1] = 0.12923; wgt_[1] = wgt_[5] = 0.13985;
      xval_[2] = 0.29707; wgt_[2] = wgt_[4] = 0.19091;
      xval_[3] = 0.5;     wgt_[3] = 0.20897;
      xval_[4] = 0.70292;
      xval_[5] = 0.87076;
      xval_[6] = 0.97455;
      break;
    case 9:
      xval_[0] = 0.01592; wgt_[0] = wgt_[8] = 0.04064;
      xval_[1] = 0.08198; wgt_[1] = wgt_[7] = 0.09032;
      xval_[2] = 0.19331; wgt_[2] = wgt_[6] = 0.13031;
      xval_[3] = 0.33787; wgt_[3] = wgt_[5] = 0.15617;
      xval_[4] = 0.5;     wgt_[4] = 0.16512;
      xval_[5] = 0.66213;
      xval_[6] = 0.80669;
      xval_[7] = 0.91802;
      xval_[8] = 0.98408;
      break;
    case 12:
      xval_[0] = 0.00922; wgt_[0] = wgt_[11] = 0.02359;
      xval_[1] = 0.04794; wgt_[1] = wgt_[10] = 0.05347;
      xval_[2] = 0.11505; wgt_[2] = wgt_[9]  = 0.08004;
      xval_[3] = 0.20634; wgt_[3] = wgt_[8]  = 0.10158;
      xval_[4] = 0.31608; wgt_[4] = wgt_[7]  = 0.11675;
      xval_[5] = 0.43738; wgt_[5] = wgt_[6]  = 0.12457;
      xval_[6] = 0.56262;
      xval_[7] = 0.68392;
      xval_[8] = 0.79366;
      xval_[9] = 0.88495;
      xval_[10] = 0.95206;
      xval_[11] = 0.99078;
      break;
    default:
      mprinterr("Error: Unsupported quadrature: %i\n", nq);
      return 1;
  }
  return 0;
}

// Analysis_TI::Analyze()
Analysis::RetType Analysis_TI::Analyze() {
  Darray sum(nskip_.size(), 0.0);
  DataSet_Mesh& DA = static_cast<DataSet_Mesh&>( *dAout_ );
  Iarray lastSkipPoint; // Points after which averages can be recorded
  for (Iarray::const_iterator it = nskip_.begin(); it != nskip_.end(); ++it)
    lastSkipPoint.push_back( *it - 1 );
  // Loop over input data sets. 
  for (unsigned int idx = 0; idx != input_dsets_.size(); idx++) {
    DataSet_1D const& ds = static_cast<DataSet_1D const&>( *(input_dsets_[idx]) );
    if (ds.Size() < 1) {
      mprinterr("Error: Set '%s' is empty.\n", ds.legend());
      return Analysis::ERR;
    }
    mprintf("\t%s (%zu points).\n", ds.legend(), ds.Size());
    // Bootstrap error analysis for average DV/DL
    if (n_bootstrap_samples_ > 0) {
      mprintf("\tPerforming bootstrap error analysis of <DV/DL>\n");
      Bootstrap BS;
      double bs_mean = 0.0;
      if (n_bootstrap_pts_ < 1) {
        n_bootstrap_pts_ = (int)(0.75*(double)input_dsets_[idx]->Size());
        mprintf("\t%i points used in each bootstrap resample.\n", n_bootstrap_pts_);
      }
      BS.Init(input_dsets_[idx], n_bootstrap_pts_, n_bootstrap_samples_, 
              bootstrap_seed_, debug_);
      if (bootstrap_seed_ != -1) ++bootstrap_seed_;
      double bs_sd = BS.Resample(bs_mean);
      double avg0 = ds.Avg();
      mprintf("\tOriginal avg= %g  Resample avg= %g  Resample SD= %g\n",
              avg0, bs_mean, bs_sd);
      ((DataSet_Mesh*)orig_avg_)->AddXY(xval_[idx], avg0);
      ((DataSet_Mesh*)bs_avg_)->AddXY(xval_[idx], bs_mean);
      ((DataSet_Mesh*)bs_sd_)->AddXY(xval_[idx], bs_sd);
    }

    // Determine if skip values are valid for this set.
    Darray Npoints; // Number of points after skipping
    for (Iarray::const_iterator it = nskip_.begin(); it != nskip_.end(); ++it) {
      int np = (int)ds.Size() - *it;
      if (np < 1) {
        mprinterr("Error: Skipped too many points (set '%s' size is %zu)\n",ds.legend(),ds.Size());
        return Analysis::ERR;
      }
      Npoints.push_back((double)np);
    }
    // Calculate averages for each value of skip
    Darray avg(nskip_.size(), 0.0);
    for (int i = 0; i != (int)ds.Size(); i++) {
      for (unsigned int j = 0; j != nskip_.size(); j++)
        if (i > lastSkipPoint[j])
          avg[j] += ds.Dval( i );
    }
    // Store average DV/DL for each value of skip
    for (unsigned int j = 0; j != nskip_.size(); j++) {
      avg[j] /= Npoints[j];
      if (debug_ > 0)
        mprintf("\t%s Skip= %i <DV/DL>= %g\n", ds.legend(), nskip_[j], avg[j]);
      DataSet_Mesh& CR = static_cast<DataSet_Mesh&>( *(curve_[j]) );
      CR.AddXY(xval_[idx], avg[j]);
      if (mode_ == GAUSSIAN_QUAD)
        sum[j] += (wgt_[idx] * avg[j]);
    }
  }
  // Integrate each curve if not doing quadrature
  if (mode_ != GAUSSIAN_QUAD) {
    for (unsigned int j = 0; j != nskip_.size(); j++) {
      DataSet_Mesh const& CR = static_cast<DataSet_Mesh const&>( *(curve_[j]) );
      sum[j] = CR.Integrate_Trapezoid();
    }
  }
  DA.ModifyDim(Dimension::X).SetLabel("PtsSkipped");
  for (unsigned int j = 0; j != nskip_.size(); j++)
    DA.AddXY(nskip_[j], sum[j]);

  return Analysis::OK;
}
