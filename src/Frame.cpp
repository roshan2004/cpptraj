#include <cmath> // sqrt
#include <cstring> // memcpy, memset
#include "Frame.h"
#include "Constants.h" // SMALL
#include "CpptrajStdio.h"

const size_t Frame::COORDSIZE_ = 3 * sizeof(double);
const size_t Frame::BOXSIZE_ = 6 * sizeof(double);

// ---------- CONSTRUCTION/DESTRUCTION/ASSIGNMENT ------------------------------
/// CONSTRUCTOR
Frame::Frame( ) : 
  natom_(0),
  maxnatom_(0),
  ncoord_(0),
  T_(0.0),
  X_(NULL),
  V_(NULL)
{
  memset(box_, 0, BOXSIZE_);
}

/// DESTRUCTOR
// Defined since this class can be inherited.
Frame::~Frame( ) { 
  if (X_ != NULL) delete[] X_;
  if (V_ != NULL) delete[] V_;
}

// CONSTRUCTOR
Frame::Frame(int natomIn) :
  natom_(natomIn),
  maxnatom_(natomIn),
  ncoord_(natomIn*3), 
  T_(0.0),
  X_(NULL),
  V_(NULL)
{
  memset(box_, 0, BOXSIZE_);
  if (ncoord_ > 0)
    X_ = new double[ ncoord_ ];
}

#ifdef NASTRUCTDEBUG
Frame::Frame(int natomIn, const double* Xin) :
  natom_(natomIn),
  maxnatom_(natomIn),
  ncoord_(natomIn*3),
  T_(0.0),
  X_(NULL),
  V_(NULL)
{
  memset(box_, 0, BOXSIZE_);
  if (ncoord_ > 0) {
    X_ = new double[ ncoord_ ];
    memcpy(X_, Xin, natom_ * COORDSIZE_);
  }
}
#endif

// CONSTRUCTOR
Frame::Frame(std::vector<Atom> const& atoms) :
  natom_(atoms.size()),
  maxnatom_(natom_),
  ncoord_(natom_*3),
  T_(0.0),
  X_(NULL),
  V_(NULL)
{
  memset(box_, 0, BOXSIZE_);
  if (ncoord_ > 0) {
    X_ = new double[ ncoord_ ];
    Mass_.reserve( natom_ );
    for (std::vector<Atom>::const_iterator atom = atoms.begin(); atom != atoms.end(); ++atom)
      Mass_.push_back( (*atom).Mass() );
  }
}

// CONSTRUCTOR
Frame::Frame(Frame const& frameIn, AtomMask const& maskIn) : 
  natom_( maskIn.Nselected() ),
  maxnatom_(natom_),
  ncoord_(natom_*3),
  T_( frameIn.T_ ),
  X_(NULL),
  V_(NULL)
{
  if (ncoord_ > 0) {
    X_ = new double[ ncoord_ ];
    double* newX = X_;
    if ( !frameIn.Mass_.empty() ) {
      if ( frameIn.V_ != NULL ) {
        // Copy coords/mass/velo
        V_ = new double[ ncoord_ ];
        double* newV = V_;
        for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
        {
          int oldcrd = ((*atom) * 3);
          memcpy(newX, frameIn.X_ + oldcrd, COORDSIZE_);
          newX += 3;
          memcpy(newV, frameIn.V_ + oldcrd, COORDSIZE_);
          newV += 3;
          Mass_.push_back( frameIn.Mass_[*atom] );
        }
      } else {
        // Copy coords/mass
        for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
        {
          memcpy(newX, frameIn.X_ + ((*atom) * 3), COORDSIZE_);
          newX += 3;
          Mass_.push_back( frameIn.Mass_[*atom] );
        }
      }
    } else {
      // Copy coords only
      for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
      {
        memcpy(newX, frameIn.X_ + ((*atom) * 3), COORDSIZE_);
        newX += 3;
      }
    }
  }
  // Copy box coords
  memcpy(box_, frameIn.box_, BOXSIZE_);
}

// COPY CONSTRUCTOR
Frame::Frame(const Frame& rhs) :
  natom_(rhs.natom_),
  maxnatom_(rhs.maxnatom_),
  ncoord_(rhs.ncoord_),
  T_(rhs.T_),
  X_(NULL),
  V_(NULL),
  Mass_(rhs.Mass_) 
{
  // Copy box coords
  memcpy(box_, rhs.box_, BOXSIZE_);
  // Copy coords/velo; allocate for maxnatom but copy natom
  int maxncoord = maxnatom_ * 3;
  if (rhs.X_!=NULL) {
    X_ = new double[ maxncoord ];
    memcpy(X_, rhs.X_, natom_ * COORDSIZE_);
  }
  if (rhs.V_!=NULL) {
    V_ = new double[ maxncoord ];
    memcpy(V_, rhs.V_, natom_ * COORDSIZE_);
  }
}

// Frame::swap()
void Frame::swap(Frame &first, Frame &second) {
  using std::swap;
  swap(first.natom_, second.natom_);
  swap(first.maxnatom_, second.maxnatom_);
  swap(first.ncoord_, second.ncoord_);
  swap(first.T_, second.T_);
  swap(first.X_, second.X_);
  swap(first.V_, second.V_);
  first.Mass_.swap(second.Mass_);
  swap( first.box_[0], second.box_[0] );
  swap( first.box_[1], second.box_[1] );
  swap( first.box_[2], second.box_[2] );
  swap( first.box_[3], second.box_[3] );
  swap( first.box_[4], second.box_[4] );
  swap( first.box_[5], second.box_[5] );
}

// Frame::operator=()
/** Assignment operator using copy/swap idiom. */
Frame &Frame::operator=(Frame rhs) {
  swap(*this, rhs);
  return *this;
}

// ---------- CONVERT TO/FROM ARRAYS -------------------------------------------
/** Assign float array to this frame. */ 
void Frame::SetFromCRD(CRDtype const& farray, int numBoxCrd) {
  int f_ncoord = (int)(farray.size()) - numBoxCrd;
  if (f_ncoord > maxnatom_*3) {
    mprinterr("Error: Float array size (%zu) > max #coords in frame (%i)\n",
              farray.size(), maxnatom_*3);
    return;
  }
  ncoord_ = f_ncoord;
  natom_ = ncoord_ / 3;
  for (int ix = 0; ix < f_ncoord; ++ix)
    X_[ix] = (double)farray[ix];
  for (int ib = 0; ib < numBoxCrd; ++ib)
    box_[ib] = (double)farray[f_ncoord++];
  return;
}

void Frame::SetFromCRD(CRDtype const& farray, int numBoxCrd, AtomMask const& mask) {
  if (mask.Nselected() > maxnatom_) {
    mprinterr("Error: Selected # atoms in float array (%i) > max #atoms in frame (%i)\n",
              mask.Nselected(), maxnatom_);
    return;
  }
  natom_ = mask.Nselected();
  ncoord_ = natom_ * 3;
  int ix = 0;
  for (AtomMask::const_iterator atom = mask.begin(); atom != mask.end(); ++atom) {
    int ia = *atom * 3;
    X_[ix++] = (double)farray[ia++];
    X_[ix++] = (double)farray[ia++];
    X_[ix++] = (double)farray[ia++];
  }
  int f_ncoord = farray.size() - numBoxCrd;
  for (int ib = 0; ib < numBoxCrd; ++ib)
    box_[ib] = (double)farray[f_ncoord++];
  return;
}

/** Place atom coordinates into a float array. */
Frame::CRDtype Frame::ConvertToCRD(int numBoxCrd) const {
  std::vector<float> farray;
  farray.reserve( ncoord_ + numBoxCrd );
  for (int ix = 0; ix < ncoord_; ++ix)
    farray.push_back( (float)X_[ix] );
  for (int ib = 0; ib < numBoxCrd; ++ib)
    farray.push_back( (float)box_[ib] );
  return farray;
}

// ---------- ACCESS INTERNAL DATA ---------------------------------------------
// Frame::printAtomCoord()
/** Print XYZ coords of given atom */
void Frame::printAtomCoord(int atom) {
  int atmidx = atom * 3;
  if (atmidx >= ncoord_) return;
  mprintf("%i: %f %f %f\n",atom+1,X_[atmidx],X_[atmidx+1],X_[atmidx+2]);
}

// Frame::Info()
// For debugging
void Frame::Info(const char *msg) {
  if (msg!=NULL)
    mprintf("\tFrame [%s]:",msg);
  else
    mprintf("\tFrame:");
  mprintf("%i atoms, %i coords",natom_, ncoord_);
  if (V_!=NULL) mprintf(" with Velocities");
  if (!Mass_.empty()) mprintf(" with Masses");
  mprintf("\n");
}

// Frame::ReallocateX()
void Frame::ReallocateX() {
  maxnatom_ += 500;
  double *newX = new double[ maxnatom_ * 3 ];
  if (X_!=NULL) {
    memcpy(newX, X_, natom_ * COORDSIZE_);
    delete[] X_;
  }
  X_ = newX;
}

// Frame::AddXYZ()
/** Append the given XYZ coord to this frame. */
void Frame::AddXYZ(const double *XYZin) {
  if (XYZin == NULL) return;
  if (natom_ >= maxnatom_) 
    ReallocateX(); 
  memcpy(X_ + ncoord_, XYZin, COORDSIZE_);
  ++natom_;
  ncoord_ += 3;
}

// Frame::AddVec3()
void Frame::AddVec3(Vec3 const& vIn) {
  if (natom_ >= maxnatom_) 
    ReallocateX();
  memcpy(X_ + ncoord_, vIn.Dptr(), COORDSIZE_);
  ++natom_;
  ncoord_ += 3;
}

// ---------- FRAME MEMORY ALLOCATION/REALLOCATION -----------------------------
// Frame::SetupFrame()
/** Set up frame for given number of atoms, no mass or velocity information. */
int Frame::SetupFrame(int natomIn) {
  natom_ = natomIn;
  ncoord_ = natom_ * 3;
  if (natom_ > maxnatom_) {
    // Reallocate
    if (X_ != NULL) delete[] X_;
    X_ = new double[ ncoord_ ];
    maxnatom_ = natom_;
  }
  if (V_ != NULL) delete[] V_;
  Mass_.clear();
  return 0;
}

// Frame::SetupFrameM()
/** Set up frame for given atom array (mass info included), no velocities. */
int Frame::SetupFrameM(std::vector<Atom> const& atoms) {
  bool reallocate = false;
  natom_ = (int)atoms.size();
  ncoord_ = natom_ * 3;
  // First check if reallocation must occur. If so reallocate coords. Masses
  // will be reallocated as well, or allocated if not already.
  if (natom_ > maxnatom_) {
    reallocate = true;
    if (X_ != NULL) delete[] X_;
    X_ = new double[ ncoord_ ];
    maxnatom_ = natom_;
  }
  if (reallocate || Mass_.empty())
    Mass_.resize(maxnatom_);
  // Copy masses
  Darray::iterator mass = Mass_.begin();
  for (std::vector<Atom>::const_iterator atom = atoms.begin();
                                         atom != atoms.end(); ++atom)
    *(mass++) = (*atom).Mass();
  if (V_ != NULL) delete[] V_;
  return 0;
}

// Frame::SetupFrameV()
/** Set up frame for given atom array. Set up for velocity info if 
  * hasVelocity is true.
  */
int Frame::SetupFrameV(std::vector<Atom> const& atoms, bool hasVelocity) {
  bool reallocate = false;
  natom_ = (int)atoms.size();
  ncoord_ = natom_ * 3;
  if (natom_ > maxnatom_) {
    reallocate = true;
    if (X_ != NULL) delete[] X_;
    X_ = new double[ ncoord_ ];
    maxnatom_ = natom_;
  }
  if (hasVelocity) {
    if (reallocate || V_ == NULL) {
      if (V_ != NULL) delete[] V_;
      V_ = new double[ maxnatom_*3 ];
      // Since velocity might not be read in, initialize it to 0.
      memset(V_, 0, maxnatom_ * COORDSIZE_);
    }
  } else {
    if (V_ != NULL) delete[] V_;
  }
  if (reallocate || Mass_.empty())
    Mass_.resize(maxnatom_);
  // Copy masses
  Darray::iterator mass = Mass_.begin();
  for (std::vector<Atom>::const_iterator atom = atoms.begin();
                                         atom != atoms.end(); ++atom)
    *(mass++) = (*atom).Mass();
  return 0;
}

// Frame::SetupFrameFromMask()
/** Set up frame to hold # selected atoms in given mask. If mass 
  * information is passed in store the masses corresponding to
  * selected atoms in the mask. No velocity info. Only reallocate
  * memory if Nselected > maxnatom.
  */
int Frame::SetupFrameFromMask(AtomMask const& maskIn, std::vector<Atom> const& atoms) {
  bool reallocate = false;
  natom_ = maskIn.Nselected();
  ncoord_ = natom_ * 3;
  if (natom_ > maxnatom_) {
    reallocate = true;
    if (X_ != NULL) delete[] X_;
    X_ = new double[ ncoord_ ];
    maxnatom_ = natom_;
  }
  if (reallocate || Mass_.empty())
    Mass_.resize(maxnatom_);
  // Copy masses according to maskIn
  Darray::iterator mass = Mass_.begin();
  for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom) 
    *(mass++) = atoms[ *atom ].Mass();
  return 0; 
}

// ---------- FRAME SETUP OF COORDINATES ---------------------------------------
// Frame::SetCoordinatesByMask()
/** Copy raw double array into frame by mask. */
void Frame::SetCoordinatesByMask(const double* Xin, AtomMask const& maskIn) {
  // Copy only coords from Xin according to maskIn
  if (Xin==NULL) {
    mprinterr("Internal Error: SetCoordinatesByMask called with NULL pointer.\n");
    return;
  }
  if (maskIn.Nselected() > maxnatom_) {
    mprinterr("Internal Error: SetCoordinatesByMask: Selected=%i, maxnatom = %i\n",
              maskIn.Nselected(), maxnatom_);
    return;
  }
  natom_ = maskIn.Nselected();
  ncoord_ = natom_ * 3;
  double* newXptr = X_;
  for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
  {
    memcpy( newXptr, Xin + ((*atom) * 3), COORDSIZE_);
    newXptr += 3;
  }
}

// Frame::SetCoordinates()
/** Copy only coordinates, box, and T from input frame to this frame based
  * on selected atoms in mask.
  */
void Frame::SetCoordinates(Frame const& frameIn, AtomMask const& maskIn) {
  if (maskIn.Nselected() > maxnatom_) {
    mprinterr("Error: SetCoordinates: Mask [%s] selected (%i) > max natom (%i)\n",
              maskIn.MaskString(), maskIn.Nselected(), maxnatom_);
    return;
  }
  natom_ = maskIn.Nselected();
  ncoord_ = natom_ * 3;
  memcpy(box_, frameIn.box_, BOXSIZE_);
  T_ = frameIn.T_;
  double* newXptr = X_;
  for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
  {
    memcpy( newXptr, frameIn.X_ + ((*atom) * 3), COORDSIZE_);
    newXptr += 3;
  }
}

// Frame::SetCoordinates()
/** Copy only coordinates from input frame to this frame.
  */
void Frame::SetCoordinates(Frame const& frameIn) {
  if (frameIn.natom_ > maxnatom_) {
    mprinterr("Error: Frame::SetCoordinates: Input frame atoms (%i) > max natom (%i)\n",
              frameIn.natom_, maxnatom_);
    return;
  }
  natom_ = frameIn.natom_;
  ncoord_ = natom_ * 3;
  memcpy(X_, frameIn.X_, natom_ * COORDSIZE_);
}

// Frame::SetFrame()
/** Copy entire input frame (including velocity if defined in both) to this 
  * frame based on selected atoms in mask. Assumes coords and mass exist. 
  */
void Frame::SetFrame(Frame const& frameIn, AtomMask const& maskIn) {
  if (maskIn.Nselected() > maxnatom_) {
    mprinterr("Error: SetFrame: Mask [%s] selected (%i) > max natom (%i)\n",
              maskIn.MaskString(), maskIn.Nselected(), maxnatom_);
    return;
  }
  natom_ = maskIn.Nselected();
  ncoord_ = natom_ * 3;
  // Copy T/box
  memcpy(box_, frameIn.box_, BOXSIZE_);
  T_ = frameIn.T_;
  double* newXptr = X_;
  Darray::iterator mass = Mass_.begin();
  if (frameIn.V_ != NULL && V_ != NULL) {
    // Copy Coords/Mass/Velo
    double *newVptr = V_;
    for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
    {
      int oldcrd = ((*atom) * 3);
      memcpy( newXptr, frameIn.X_ + oldcrd, COORDSIZE_);
      newXptr += 3;
      memcpy( newVptr, frameIn.V_ + oldcrd, COORDSIZE_);
      newVptr += 3;
      *mass = frameIn.Mass_[*atom];
      ++mass;
    }
  } else {
    // Copy coords/mass only
    for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
    {
      memcpy( newXptr, frameIn.X_ + ((*atom) * 3), COORDSIZE_);
      newXptr += 3;
      *mass = frameIn.Mass_[*atom];
      ++mass;
    }
  }
}

// Frame::FrameCopy()
/** Return a copy of the frame */
// TODO: Obsolete
Frame *Frame::FrameCopy( ) {
  Frame* newFrame = new Frame( *this );
  return newFrame;
}

// ---------- FRAME SETUP WITH ATOM MAPPING ------------------------------------
// NOTE: Should these map routines be part of a child class used only by AtomMap?
// Frame::SetCoordinatesByMap()
/** Reorder atoms in the given target frame so that it matches the given
  * atom map with format (tgtAtom = Map[refAtom]). End result is that
  * this frame will be in the same order as the original reference. Assumes
  * that the map is complete (i.e. no unmapped atoms).
  */
void Frame::SetCoordinatesByMap(Frame const& tgtIn, std::vector<int> const& mapIn) {
  if (tgtIn.natom_ > maxnatom_) {
    mprinterr("Error: SetCoordinatesByMap: # Input map frame atoms (%i) > max atoms (%i)\n",
              tgtIn.natom_, maxnatom_);
    return;
  }
  if ((int)mapIn.size() != tgtIn.natom_) {
    mprinterr("Error: SetCoordinatesByMap: Input map size (%zu) != input frame natom (%i)\n",
              mapIn.size(), tgtIn.natom_);
    return;
  }
  natom_ = tgtIn.natom_;
  ncoord_ = natom_ * 3;
  double* newXptr = X_;
  for (std::vector<int>::const_iterator refatom = mapIn.begin(); 
                                        refatom != mapIn.end(); ++refatom)
  {
    memcpy( newXptr, tgtIn.X_ + ((*refatom) * 3), COORDSIZE_ );
    newXptr += 3;
  }
}

// Frame::SetReferenceByMap()
/** Set this frame to include only atoms from the given reference that are 
  * mapped.
  */
void Frame::SetReferenceByMap(Frame const& refIn, std::vector<int> const& mapIn) {
  if (refIn.natom_ > maxnatom_) {
    mprinterr("Error: SetReferenceByMap: # Input map frame atoms (%i) > max atoms (%i)\n",
              refIn.natom_, maxnatom_);
    return;
  }
  if ((int)mapIn.size() != refIn.natom_) {
    mprinterr("Error: SetReferenceByMap: Input map size (%zu) != input frame natom (%i)\n",
              mapIn.size(), refIn.natom_);
    return;
  }
  double* newXptr = X_;
  double* refptr = refIn.X_;
  for (std::vector<int>::const_iterator refatom = mapIn.begin(); 
                                        refatom != mapIn.end(); ++refatom)
  {
    if (*refatom != -1) {
      memcpy( newXptr, refptr, COORDSIZE_ );
      newXptr += 3;
    }
    refptr += 3;
  }
  ncoord_ = (int)(newXptr - X_);
  natom_ = ncoord_ / 3;
}

// Frame::SetTargetByMap()
/** Set this frame to include only atoms from the given target frame, remapped
  * according to the given atom map.
  */
void Frame::SetTargetByMap(Frame const& tgtIn, std::vector<int> const& mapIn) {
  if (tgtIn.natom_ > maxnatom_) {
    mprinterr("Error: SetTargetByMap: # Input map frame atoms (%i) > max atoms (%i)\n",
              tgtIn.natom_, maxnatom_);
    return;
  }
  if ((int)mapIn.size() != tgtIn.natom_) {
    mprinterr("Error: SetTargetByMap: Input map size (%zu) != input frame natom (%i)\n",
              mapIn.size(), tgtIn.natom_);
    return;
  }
  double* newXptr = X_;
  for (std::vector<int>::const_iterator refatom = mapIn.begin(); 
                                        refatom != mapIn.end(); ++refatom)
  {
    if (*refatom != -1) {
      memcpy( newXptr, tgtIn.X_ + ((*refatom) * 3), COORDSIZE_ );
      newXptr += 3;
    }
  }
  ncoord_ = (int)(newXptr - X_);
  natom_ = ncoord_ / 3;
}

// ---------- BASIC ARITHMETIC ------------------------------------------------- 
// Frame::ZeroCoords()
/** Set all coords to 0.0 */
void Frame::ZeroCoords( ) {
  memset(X_, 0, natom_ * COORDSIZE_);
}

// Frame::operator+=()
Frame &Frame::operator+=(const Frame& rhs) {
  // For now ensure same natom
  if (natom_ != rhs.natom_) {
    mprinterr("Error: Frame::operator+=: Frames have different natom.\n");
    return *this;
  }
  for (int i = 0; i < ncoord_; ++i)
    X_[i] += rhs.X_[i];
  return *this;
}

// Frame::operator-=()
Frame &Frame::operator-=(const Frame& rhs) {
  // For now ensure same natom
  if (natom_ != rhs.natom_) {
    mprinterr("Error: Frame::operator-=: Frames have different natom.\n");
    return *this;
  }
  for (int i = 0; i < ncoord_; ++i)
    X_[i] -= rhs.X_[i];
  return *this;
}

// Frame::operator*=()
Frame &Frame::operator*=(const Frame& rhs) {
  // For now ensure same natom
  if (natom_ != rhs.natom_) {
    mprinterr("Error: Frame::operator*=: Frames have different natom.\n");
    return *this;
  }
  for (int i = 0; i < ncoord_; ++i)
    X_[i] *= rhs.X_[i];
  return *this;
}

// Frame::operator*()
// NOTE: return const instance and not const reference to disallow
//       nonsense statements like (a * b) = c
const Frame Frame::operator*(const Frame& rhs) const {
  return (Frame(*this) *= rhs);
}

// Frame::Divide()
/** Divide all coord values of dividend by divisor and store in this frame.
  */
int Frame::Divide(Frame const& dividend, double divisor) {
  if (divisor < SMALL) {
    mprinterr("Error: Frame::Divide(Frame,divisor): Detected divide by 0.\n");
    return 1;
  }
  // For now ensure same natom
  if (natom_ != dividend.natom_) {
    mprinterr("Error: Frame::Divide: Frames have different natom.\n");
    return 1;
  }
  for (int i=0; i < ncoord_; ++i)
    X_[i] = dividend.X_[i] / divisor;
  return 0;
}

// Frame::Divide()
/** Divide all coord values of this frame by divisor.
  */
void Frame::Divide(double divisor) {
  if (divisor < SMALL) {
    mprinterr("Error: Frame::Divide(divisor): Detected divide by 0.\n");
    return;
  }
  for (int i = 0; i < ncoord_; ++i)
    X_[i] /= divisor;
}

// Frame::AddByMask()
/** Increment atoms in this frame by the selected atoms in given frame.
  */
// TODO: Should this be removed in favor of SetByMask followed by '+='?
void Frame::AddByMask(Frame const& frameIn, AtomMask const& maskIn) {
  if (maskIn.Nselected() > maxnatom_) {
    mprinterr("Error: AddByMask: Input mask #atoms (%i) > frame #atoms (%i)\n",
              maskIn.Nselected(), maxnatom_);
    return;
  }
  unsigned int xidx = 0;
  for (AtomMask::const_iterator atom = maskIn.begin(); atom != maskIn.end(); ++atom)
  {
    unsigned int fidx = (*atom) * 3;
    X_[xidx  ] += frameIn.X_[fidx  ];
    X_[xidx+1] += frameIn.X_[fidx+1];
    X_[xidx+2] += frameIn.X_[fidx+2];
    xidx += 3;
  }
}

// ---------- COORDINATE MANIPULATION ------------------------------------------
// Frame::SCALE()
void Frame::SCALE(AtomMask const& maskIn, double sx, double sy, double sz) {
  for (AtomMask::const_iterator atom = maskIn.begin();
                                atom != maskIn.end(); atom++)
  {
    unsigned int xidx = *atom * 3;
    X_[xidx  ] *= sx;
    X_[xidx+1] *= sy;
    X_[xidx+2] *= sz;
  }
}

// Frame::Center()
/** Center coordinates to center of coordinates in Mask w.r.t. given XYZ in
  * boxcoord. When called from Action_Center boxcoord will be either origin 
  * or box center. Use geometric center if mass is NULL, otherwise center 
  * of mass will be used.
  */
void Frame::Center(AtomMask const& Mask, bool origin, bool useMassIn) 
{
  Vec3 center;
  if (useMassIn)
    center = VCenterOfMass(Mask);
  else
    center = VGeometricCenter(Mask);
  //mprinterr("  FRAME CENTER: %lf %lf %lf\n",center[0],center[1],center[2]); //DEBUG
  if (origin) 
    // Shift to coordinate origin (0,0,0)
    center.Neg();
  else {
    // Shift to box center
    center[0] = (box_[0] / 2) - center[0];
    center[1] = (box_[1] / 2) - center[1];
    center[2] = (box_[2] / 2) - center[2];
  }
  Translate(center);
}

// Frame::CenterReference()
/** Center coordinates to origin in preparation for RMSD calculation. 
  * \return translation vector from origin to reference.
  */
Vec3 Frame::CenterReference(bool useMassIn)
{
  Vec3 center;
  if (useMassIn)
    center = VCenterOfMass(0, natom_); // TODO: Replace with total version?
  else
    center = VGeometricCenter(0, natom_);
  //mprinterr("  REF FRAME CENTER: %lf %lf %lf\n",Trans[0],Trans[1],Trans[2]); //DEBUG
  // center contains translation from origin -> Ref, therefore
  // -center is translation from Ref -> origin.
  NegTranslate(center);
  return center;
}

// Frame::ShiftToGeometricCenter()
/** Shift geometric center of coordinates in frame to origin. */
void Frame::ShiftToGeometricCenter( ) {
  Vec3 frameCOM = VGeometricCenter(0, natom_);
  //mprinterr("  FRAME COM: %lf %lf %lf\n",frameCOM[0],frameCOM[1],frameCOM[2]); //DEBUG
  // Shift to common COM
  NegTranslate(frameCOM);
}

// ---------- COORDINATE CALCULATION ------------------------------------------- 
// Frame::BoxToRecip()
/** Use box coordinates to calculate reciprocal space conversions for use
  * with imaging routines. Return cell volume.
  */
// NOTE: Move to separate routine in DistRoutines?
double Frame::BoxToRecip(double *ucell, double *recip) const {
  double u12x,u12y,u12z;
  double u23x,u23y,u23z;
  double u31x,u31y,u31z;
  double volume,onevolume;

  ucell[0] = box_[0]; // ucell(1,1)
  ucell[1] = 0.0;    // ucell(2,1)
  ucell[2] = 0.0;    // ucell(3,1)
  ucell[3] = box_[1]*cos(DEGRAD*box_[5]); // ucell(1,2)
  ucell[4] = box_[1]*sin(DEGRAD*box_[5]); // ucell(2,2)
  ucell[5] = 0.0;                       // ucell(3,2)
  ucell[6] = box_[2]*cos(DEGRAD*box_[4]);                                         // ucell(1,3)
  ucell[7] = (box_[1]*box_[2]*cos(DEGRAD*box_[3]) - ucell[6]*ucell[3]) / ucell[4]; // ucell(2,3)
  ucell[8] = sqrt(box_[2]*box_[2] - ucell[6]*ucell[6] - ucell[7]*ucell[7]);       // ucell(3,3)

  // Get reciprocal vectors
  u23x = ucell[4]*ucell[8] - ucell[5]*ucell[7];
  u23y = ucell[5]*ucell[6] - ucell[3]*ucell[8];
  u23z = ucell[3]*ucell[7] - ucell[4]*ucell[6];
  u31x = ucell[7]*ucell[2] - ucell[8]*ucell[1];
  u31y = ucell[8]*ucell[0] - ucell[6]*ucell[2];
  u31z = ucell[6]*ucell[1] - ucell[7]*ucell[0];
  u12x = ucell[1]*ucell[5] - ucell[2]*ucell[4];
  u12y = ucell[2]*ucell[3] - ucell[0]*ucell[5];
  u12z = ucell[0]*ucell[4] - ucell[1]*ucell[3];
  volume=ucell[0]*u23x + ucell[1]*u23y + ucell[2]*u23z;
  onevolume = 1.0 / volume;

  recip[0] = u23x*onevolume;
  recip[1] = u23y*onevolume;
  recip[2] = u23z*onevolume;
  recip[3] = u31x*onevolume;
  recip[4] = u31y*onevolume;
  recip[5] = u31z*onevolume;
  recip[6] = u12x*onevolume;
  recip[7] = u12y*onevolume;
  recip[8] = u12z*onevolume;

  return volume;
}

// Frame::RMSD()
double Frame::RMSD( Frame& Ref, bool useMassIn ) {
  Matrix_3x3 U;
  Vec3 Trans, refTrans;
  return RMSD( Ref, U, Trans, refTrans, useMassIn );
}

// Frame::RMSD()
/** Get the RMSD of this Frame to Ref Frame. Ref frame must contain the same
  * number of atoms as this Frame - should be checked for before this routine
  * is called. 
  * \param Ref frame to calc RMSD to. Will be translated to origin.
  * \param U Will be set with best-fit rotation matrix.
  * \param Trans Will be set with translation of coords to origin.
  * \param refTrans Will be set with translation from origin to Ref.
  * \param useMassIn If true, mass-weight everything.
  * To reproduce the fit perform the first translation (Trans), 
  * then rotate (U), then the second translation (refTrans).
  */
double Frame::RMSD( Frame& Ref, Matrix_3x3& U, Vec3& Trans, Vec3& refTrans, bool useMassIn) 
{
  // Rotation will occur around geometric center/center of mass
  // Coords are shifted to common CoM first (Trans0-2), then
  // to original reference location (Trans3-5).
  if (useMassIn) 
    refTrans = Ref.VCenterOfMass(0, natom_);
  else 
    refTrans = Ref.VGeometricCenter(0, natom_);
  //fprintf(stderr,"  REF   COM: %lf %lf %lf\n",refTrans[0],refTrans[1],refTrans[2]); //DEBUG
  // Shift reference to COM
  Ref.NegTranslate(refTrans);
  return RMSD_CenteredRef( Ref, U, Trans, useMassIn );
  //DEBUG
  //printRotTransInfo(U,Trans);
  //fprintf(stdout,"RMS is %lf\n",rms_return);
  //return rmsval;
}

double Frame::RMSD_CenteredRef( Frame const& Ref, bool useMassIn ) {
  Matrix_3x3 U;
  Vec3 Trans;
  return RMSD_CenteredRef( Ref, U, Trans, useMassIn );
}

static inline void normalize(double* vIn) {
  double b = 1.0 / sqrt(vIn[0]*vIn[0] + vIn[1]*vIn[1] + vIn[2]*vIn[2]);
  vIn[0] *= b;
  vIn[1] *= b;
  vIn[2] *= b;
}

// Frame::RMSD_CenteredRef()
/** Get the RMSD of this Frame to given Reference Frame. Ref frame must contain 
  * the same number of atoms as this Frame and should have already been 
  * translated to coordinate origin (neither is checked for in the interest
  * of speed). 
  * \param Frame to calc RMSD to.
  * \param U Will be set to the best-fit rotation matrix
  * \param Trans will contain translation vector for this frame to origin.
  * \param useMassIn If true, mass-weight everything.
  */ 
double Frame::RMSD_CenteredRef( Frame const& Ref, Matrix_3x3& U, Vec3& Trans, bool useMassIn)
{
  double total_mass, cp[3], sig3, b[9];
  // Need to init U?
  // Trans is set at the end
  // Rotation will occur around geometric center/center of mass
  Trans.Zero();
  if (useMassIn) {
    Darray::iterator mass = Mass_.begin();
    total_mass = 0.0;
    for (int ix = 0; ix < ncoord_; ix += 3) {
      total_mass += *mass;
      Trans[0] += (X_[ix  ] * (*mass));
      Trans[1] += (X_[ix+1] * (*mass));
      Trans[2] += (X_[ix+2] * (*mass));
      ++mass;
    }
  } else {
    total_mass = (double)natom_;
    for (int ix = 0; ix < ncoord_; ix += 3) {
      Trans[0] += X_[ix  ];
      Trans[1] += X_[ix+1];
      Trans[2] += X_[ix+2];
    }
  }
  if (total_mass<SMALL) {
    mprinterr("Error: Frame::RMSD: Divide by zero.\n");
    return -1;
  }
  Trans[0] /= total_mass;
  Trans[1] /= total_mass;
  Trans[2] /= total_mass;
  //mprinterr("  FRAME COM: %lf %lf %lf\n",Trans[0],Trans[1],Trans[2]); //DEBUG

  // Shift to common COM
  Trans.Neg();
  Translate(Trans);
  //for (int i = 0; i < natom_; i++) {
  //  mprinterr("\tSHIFTED FRAME %i: %f %f %f\n",i,X_[i*3],X_[i*3+1],X_[i*3+2]); //DEBUG
  //  mprinterr("\tSHIFTED REF %i  : %f %f %f\n",i,Ref.X_[i*3],Ref.X_[i*3+1],Ref.X_[i*3+2]); //DEBUG
  //}

  // Use Kabsch algorithm to calculate optimum rotation matrix.
  // U = [(RtR)^.5][R^-1]
  double mwss = 0.0;
  Matrix_3x3 rot(0.0);
  // Calculate covariance matrix of Coords and Reference (R = Xt * Ref)
  Darray::iterator mass = Mass_.begin();
  double atom_mass = 1.0;
  for (int i = 0; i < ncoord_; i += 3)
  {
    double xt = X_[i  ];
    double yt = X_[i+1];
    double zt = X_[i+2];
    double xr = Ref.X_[i  ];
    double yr = Ref.X_[i+1];
    double zr = Ref.X_[i+2];
    // Use atom_mass to hold mass for this atom if specified
    if (useMassIn) 
      atom_mass = *(mass++);
    mwss += atom_mass * ( (xt*xt)+(yt*yt)+(zt*zt)+(xr*xr)+(yr*yr)+(zr*zr) );
    // Calculate the Kabsch matrix: R = (rij) = Sum(yni*xnj)
    rot[0] += atom_mass*xt*xr;
    rot[1] += atom_mass*xt*yr;
    rot[2] += atom_mass*xt*zr;

    rot[3] += atom_mass*yt*xr;
    rot[4] += atom_mass*yt*yr;
    rot[5] += atom_mass*yt*zr;

    rot[6] += atom_mass*zt*xr;
    rot[7] += atom_mass*zt*yr;
    rot[8] += atom_mass*zt*zr;
  }
  mwss *= 0.5;    // E0 = 0.5*Sum(xn^2+yn^2) 
  //DEBUG
  //mprinterr("ROT:\n%lf %lf %lf\n%lf %lf %lf\n%lf %lf %lf\n",
  //          rot[0],rot[1],rot[2],rot[3],rot[4],rot[5],rot[6],rot[7],rot[8]);
  //mprinterr("MWSS: %lf\n",mwss);
  // calculate Kabsch matrix multiplied by its transpose: RtR
  Matrix_3x3 Evector = rot.TransposeMult(rot); 
  // Diagonalize
  Vec3 Eigenvalue;
  if (Evector.Diagonalize_Sort( Eigenvalue )) return 0;
  // a3 = a1 x a2
  Evector[6] = (Evector[1]*Evector[5]) - (Evector[2]*Evector[4]); 
  Evector[7] = (Evector[2]*Evector[3]) - (Evector[0]*Evector[5]); 
  Evector[8] = (Evector[0]*Evector[4]) - (Evector[1]*Evector[3]);
  // Evector dot transpose rot: b = R . ak
  b[0] = Evector[0]*rot[0] + Evector[1]*rot[3] + Evector[2]*rot[6];
  b[1] = Evector[0]*rot[1] + Evector[1]*rot[4] + Evector[2]*rot[7];
  b[2] = Evector[0]*rot[2] + Evector[1]*rot[5] + Evector[2]*rot[8];
  normalize(b);
  b[3] = Evector[3]*rot[0] + Evector[4]*rot[3] + Evector[5]*rot[6];
  b[4] = Evector[3]*rot[1] + Evector[4]*rot[4] + Evector[5]*rot[7];
  b[5] = Evector[3]*rot[2] + Evector[4]*rot[5] + Evector[5]*rot[8];
  normalize(b+3);
  b[6] = Evector[6]*rot[0] + Evector[7]*rot[3] + Evector[8]*rot[6];
  b[7] = Evector[6]*rot[1] + Evector[7]*rot[4] + Evector[8]*rot[7];
  b[8] = Evector[6]*rot[2] + Evector[7]*rot[5] + Evector[8]*rot[8];
  normalize(b+6);
  // b3 = b1 x b2
  cp[0] = (b[1]*b[5]) - (b[2]*b[4]); 
  cp[1] = (b[2]*b[3]) - (b[0]*b[5]); 
  cp[2] = (b[0]*b[4]) - (b[1]*b[3]);
  if ( (cp[0]*b[6] + cp[1]*b[7] + cp[2]*b[8]) < 0.0 )
    sig3 = -1.0;
  else
    sig3 = 1.0;
  b[6] = cp[0];
  b[7] = cp[1];
  b[8] = cp[2];
  // U has the best rotation 
  U[0] = (Evector[0]*b[0]) + (Evector[3]*b[3]) + (Evector[6]*b[6]);  
  U[1] = (Evector[1]*b[0]) + (Evector[4]*b[3]) + (Evector[7]*b[6]);
  U[2] = (Evector[2]*b[0]) + (Evector[5]*b[3]) + (Evector[8]*b[6]);

  U[3] = (Evector[0]*b[1]) + (Evector[3]*b[4]) + (Evector[6]*b[7]);
  U[4] = (Evector[1]*b[1]) + (Evector[4]*b[4]) + (Evector[7]*b[7]);
  U[5] = (Evector[2]*b[1]) + (Evector[5]*b[4]) + (Evector[8]*b[7]);

  U[6] = (Evector[0]*b[2]) + (Evector[3]*b[5]) + (Evector[6]*b[8]);
  U[7] = (Evector[1]*b[2]) + (Evector[4]*b[5]) + (Evector[7]*b[8]);
  U[8] = (Evector[2]*b[2]) + (Evector[5]*b[5]) + (Evector[8]*b[8]);

  // E=E0-sqrt(mu1)-sqrt(mu2)-sig3*sqrt(mu3) 
  double rms_return = mwss - sqrt(fabs(Eigenvalue[0])) 
                           - sqrt(fabs(Eigenvalue[1]))
                           - (sig3*sqrt(fabs(Eigenvalue[2])));
  if (rms_return<0) {
    //mprinterr("RMS returned is <0 before sqrt, setting to 0 (%f)\n", rms_return);
    rms_return = 0.0;
  } else
    rms_return = sqrt((2.0*rms_return)/total_mass);
  //DEBUG
  //printRotTransInfo(U,Trans);
  //fprintf(stdout,"RMS is %lf\n",rms_return);
  return rms_return;
}

// Frame::RMSD()
/** Calculate RMSD of Frame to Ref with no fitting. Frames must contain
  * same # atoms.
  */
double Frame::RMSD_NoFit( Frame const& Ref, bool useMass) {
  double rms_return = 0.0;
  double total_mass = 0.0;
  
  Darray::iterator mass = Mass_.begin();
  double atom_mass = 1.0;
  for (int i = 0; i < ncoord_; i += 3)
  {
    double xx = Ref.X_[i  ] - X_[i  ];
    double yy = Ref.X_[i+1] - X_[i+1];
    double zz = Ref.X_[i+2] - X_[i+2];
    if (useMass) 
      atom_mass = *(mass++);
    total_mass += atom_mass;
    rms_return += (atom_mass * (xx*xx + yy*yy + zz*zz));
  }

  if (total_mass<SMALL) {
    mprinterr("Error: no-fit RMSD: Divide by zero.\n");
    return -1;
  }
  if (rms_return < 0) {
    //mprinterr("Error: Frame::RMSD: Negative RMS. Coordinates may be corrupted.\n");
    //return -1;
    //mprinterr("RMS returned is <0 before sqrt, setting to 0 (%lf)\n",rms_return);
    return 0;
  }

  return (sqrt(rms_return / total_mass));
}

// Frame::DISTRMSD()
/** Calcuate the distance RMSD of Frame to Ref. Frames must contain
  * same # of atoms. Should not be called for 0 atoms.
  */
double Frame::DISTRMSD( Frame const& Ref ) {
  double TgtDist, RefDist;
  double diff, rms_return;
  double x,y,z;
  int a10,a11,a12;
  int a20,a21,a22; 
  double Ndistances = ((natom_ * natom_) - natom_) / 2;

  rms_return = 0;
  a10 = 0;
  for (int atom1 = 0; atom1 < natom_-1; ++atom1) {
    a20 = a10 + 3;
    for (int atom2 = atom1+1; atom2 < natom_; ++atom2) {
      a11 = a10 + 1;
      a12 = a10 + 2;
      a21 = a20 + 1;
      a22 = a20 + 2;
      // Tgt
      x = X_[a10] - X_[a20];
      x = x * x;
      y = X_[a11] - X_[a21];
      y = y * y;
      z = X_[a12] - X_[a22];
      z = z * z;
      TgtDist = sqrt(x + y + z);
      // Ref
      x = Ref.X_[a10] - Ref.X_[a20];
      x = x * x;
      y = Ref.X_[a11] - Ref.X_[a21];
      y = y * y;
      z = Ref.X_[a12] - Ref.X_[a22];
      z = z * z;
      RefDist = sqrt(x + y + z);
      // DRMSD
      diff = TgtDist - RefDist;
      diff *= diff;
      rms_return += diff;

      a20 += 3;
    } 
    a10 += 3;
  }

  TgtDist = rms_return / Ndistances;
  rms_return = sqrt(TgtDist);

  return rms_return;
}

// Frame::SetAxisOfRotation()
/** Given the central two atoms of a dihedral, calculate
  * a vector (U) which will be the axis for rotating the system around that 
  * dihedral and translate the coordinates (X) to the origin of the new axis.
  */
Vec3 Frame::SetAxisOfRotation(int atom1, int atom2) {
  int a1 = atom1 * 3;
  int a2 = atom2 * 3;
 
  Vec3 A1( X_[a1], X_[a1+1], X_[a1+2] ); 
  // Calculate vector of dihedral axis, which will be the new rot. axis
  Vec3 U( X_[a2]-A1[0], X_[a2+1]-A1[1], X_[a2+2]-A1[2] );
  // Normalize Vector for axis of rotation or scaling will occur!
  U.Normalize();
  // Now the rest of the coordinates need to be translated to match the new 
  // rotation axis.
  A1.Neg();
  Translate(A1);
  return U;
}

// Frame::CalculateInertia()
/** \return Center of mass of coordinates in mask. */
Vec3 Frame::CalculateInertia(AtomMask const& Mask, Matrix_3x3& Inertia)
{
  double Ivec[6]; // Ivec = xx, yy, zz, xy, xz, yz
  Vec3 CXYZ = VCenterOfMass(Mask);

  // Calculate moments of inertia and products of inertia
  Ivec[0] = 0; // xx
  Ivec[1] = 0; // yy
  Ivec[2] = 0; // zz
  Ivec[3] = 0; // xy
  Ivec[4] = 0; // xz
  Ivec[5] = 0; // yz
  for (AtomMask::const_iterator atom = Mask.begin(); atom != Mask.end(); ++atom) {
    int xidx = (*atom) * 3;
    double cx = X_[xidx  ] - CXYZ[0];
    double cy = X_[xidx+1] - CXYZ[1];
    double cz = X_[xidx+2] - CXYZ[2];
    double mass = Mass_[*atom];

    Ivec[0] += mass * ( cy * cy + cz * cz );
    Ivec[1] += mass * ( cx * cx + cz * cz );
    Ivec[2] += mass * ( cx * cx + cy * cy );
    Ivec[3] -= mass * cx * cy;
    Ivec[5] -= mass * cy * cz;
    Ivec[4] -= mass * cx * cz;
  }
  Inertia[0] = Ivec[0];
  Inertia[1] = Ivec[3];
  Inertia[2] = Ivec[4];
  Inertia[3] = Ivec[3];
  Inertia[4] = Ivec[1];
  Inertia[5] = Ivec[5];
  Inertia[6] = Ivec[4];
  Inertia[7] = Ivec[5];
  Inertia[8] = Ivec[2];
  return CXYZ;
}
