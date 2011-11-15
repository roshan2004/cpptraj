/* AmberParm.cpp
 * Class that holds parameter information. Can be read in from Amber Topology,
 * PDB, or Mol2 files (implemented in the ReadParmXXX functions). The following
 * parameters of AmberParm must always be set:
 *   The names, resnames, resnums arrays.
 *   The natom, boxType and nres variables.
 * NOTES:
 */
#include <cstring>
#include <cstdio> // For sscanf, sprintf
#include <ctime> // for writing time/date to amber parmtop
#include "AmberParm.h" // CpptrajFile.h
#include "FortranFormat.h" 
#include "PDBfileRoutines.h"
#include "Mol2FileRoutines.h"
#include "CpptrajStdio.h"
// For searching for bonds by distance (PDB etc)
#include "DistRoutines.h"
#include "Bonds.h"

#define AMBERPOINTERS 31
#define ELECTOAMBER 18.2223
#define AMBERTOELEC 1/ELECTOAMBER
// =============================================================================

// CONSTRUCTOR
AmberParm::AmberParm() {
  debug=0;
  parmfileName=NULL;
  parmName=NULL;
  pindex=0;
  parmFrames=0;
  parmCoords=NULL;

  NbondsWithH=0;
  NbondsWithoutH=0;
  bondsh=NULL;
  bonds=NULL;
  names=NULL;
  resnames=NULL;
  types=NULL;
  resnums=NULL;
  natom=0;
  nres=0;
  finalSoluteRes=0;
  molecules=0;
  firstSolvMol=-1;
  atomsPerMol=NULL;
  mass=NULL;
  charge=NULL;
  Box[0]=0.0; Box[1]=0.0; Box[2]=0.0;
  Box[3]=0.0; Box[4]=0.0; Box[5]=0.0;
  boxType=NOBOX;

  solventMask=NULL;
  solventMolecules=0;
  solventMoleculeStart=NULL;
  solventMoleculeStop=NULL;
  solventAtoms=0;

  SurfaceInfo=NULL;
  numSoluteAtoms=0;

  numex=NULL;
  atype_index=NULL;
  NB_index=NULL;
  LJ_A=NULL;
  LJ_B=NULL;
  excludedAtoms=NULL;
  radius_set=NULL;
  gb_radii=NULL;
  gb_screen=NULL;
  ntypes=0;
  nnb=0;

  bond_rk=NULL;
  bond_req=NULL;
  angle_tk=NULL;
  angle_teq=NULL;
  dihedral_pk=NULL;
  dihedral_pn=NULL;
  dihedral_phase=NULL;
  scee_scale=NULL;
  scnb_scale=NULL;
  solty=NULL;
  anglesh=NULL;
  angles=NULL;
  dihedralsh=NULL;
  dihedrals=NULL;
  asol=NULL;
  bsol=NULL;
  hbcut=NULL;
  itree=NULL;
  join_array=NULL;
  irotat=NULL;
}

// DESTRUCTOR
AmberParm::~AmberParm() {
  if (parmfileName!=NULL) delete[] parmfileName;
  if (parmName!=NULL) delete[] parmName;

  if (bondsh!=NULL) delete[] bondsh;
  if (bonds!=NULL) delete[] bonds;
  if (names!=NULL) delete[] names;
  if (resnames!=NULL) delete[] resnames;
  if (types!=NULL) delete[] types;
  if (resnums!=NULL) delete[] resnums;
  if (atomsPerMol!=NULL) delete[] atomsPerMol;
  if (mass!=NULL) delete[] mass;
  if (charge!=NULL) delete[] charge;

  if (solventMask!=NULL) delete[] solventMask;
  if (solventMoleculeStart!=NULL) delete[] solventMoleculeStart;
  if (solventMoleculeStop!=NULL) delete[] solventMoleculeStop;

  if (SurfaceInfo!=NULL) delete[] SurfaceInfo;
  if (parmCoords!=NULL) delete[] parmCoords;

  if (numex!=NULL) delete[] numex;
  if (atype_index!=NULL) delete[] atype_index;
  if (NB_index!=NULL) delete[] NB_index;
  if (LJ_A!=NULL) delete[] LJ_A;
  if (LJ_B!=NULL) delete[] LJ_B;
  if (excludedAtoms!=NULL) delete[] excludedAtoms;
  if (radius_set!=NULL) delete[] radius_set; // getFlagFileString uses 'new'
  if (gb_radii!=NULL) delete[] gb_radii;
  if (gb_screen!=NULL) delete[] gb_screen;

  if (bond_rk!=NULL) delete[] bond_rk;
  if (bond_req!=NULL) delete[] bond_req;
  if (angle_tk!=NULL) delete[] angle_tk;
  if (angle_teq!=NULL) delete[] angle_teq;
  if (dihedral_pk!=NULL) delete[] dihedral_pk;
  if (dihedral_pn!=NULL) delete[] dihedral_pn;
  if (dihedral_phase!=NULL) delete[] dihedral_phase;
  if (solty!=NULL) delete[] solty;
  if (anglesh!=NULL) delete[] anglesh;
  if (angles!=NULL) delete[] angles;
  if (dihedralsh!=NULL) delete[] dihedralsh;
  if (dihedrals!=NULL) delete[] dihedrals;
  if (asol!=NULL) delete[] asol;
  if (bsol!=NULL) delete[] bsol;
  if (hbcut!=NULL) delete[] hbcut;
  if (itree!=NULL) delete[] itree;
  if (join_array!=NULL) delete[] join_array;
  if (irotat!=NULL) delete[] irotat;
}

/* SetDebug()
 * Set the debug level.
 */
void AmberParm::SetDebug(int debugIn) {
  debug = debugIn;
  if (debug>0) mprintf("AmberParm debug set to %i\n",debug);
}

// -----------------------------------------------------------------------------
/* AmberParm::ResName()
 * Given a residue number, set buffer with residue name and number with format:
 * <resname[res]><res+1>, e.g. ARG_11. Replace any blanks in resname with '_'.
 */
void AmberParm::ResName(char *buffer, int res) {
  char rname[NAMESIZE];
  if (res<0 || res>=nres) return;
  rname[0]=resnames[res][0];
  rname[1]=resnames[res][1];
  rname[2]=resnames[res][2];
  if (resnames[res][3]==' ') 
    rname[3]='_';
  else
    rname[3]=resnames[res][3];
  rname[4]='\0';
  sprintf(buffer,"%s%i",rname,res+1);
}

/* AmberParm::ResAtomName()
 * Given an atom number, set buffer with residue name and number along with
 * atom name with format: <resname[res]><res+1>@<atomname>, e.g. ARG_11@CA.
 * Replace any blanks in resname with '_'.
 */
void AmberParm::ResAtomName(char *buffer, int atom) {
  int res;
  char rname[NAMESIZE];
  if (atom<0 || atom>=natom) return;
  res = atomToResidue(atom);
  rname[0]=resnames[res][0];
  rname[1]=resnames[res][1];
  rname[2]=resnames[res][2];
  if (resnames[res][3]==' ') 
    rname[3]='_';
  else
    rname[3]=resnames[res][3];
  rname[4]='\0';
  sprintf(buffer,"%s%i@%s",rname,res+1,names[atom]);
}

/* AmberParm::ResidueName()
 * Return pointer to name of given residue.
 */
char *AmberParm::ResidueName(int res) {
  if (resnames==NULL) {
    mprintf("Internal Error: AmberParm::ResidueName: Residue names not set!\n");
    return NULL;
  }
  if (res>-1 && res<nres)
    return (char*)resnames[res];
  return NULL;
}

/* AmberParm::FindAtomInResidue()
 * Given a residue number and an atom name, return the atom number. If
 * the given atom name is not in the given residue, return -1.
 */
int AmberParm::FindAtomInResidue(int res, char *atname) {
  if (res < 0 || res >= nres) return -1;
  for (int atnum = resnums[res]; atnum < resnums[res+1]; atnum++) {
    if (strcmp(names[atnum],atname)==0) return atnum;
  }
  return -1;
}

// -------------------- ROUTINES FOR ACCESSING INTERNAL DATA -------------------
int AmberParm::NumExcludedAtoms(int atom) {
  if (numex==NULL) return -1;
  if (atom<0 || atom>=natom) return -1;
  return numex[atom];
}

int AmberParm::Natex(int idx) {
  if (excludedAtoms==NULL) return -1;
  return excludedAtoms[idx];
}

int AmberParm::GetLJparam(double *A, double *B, int atom1, int atom2) {
  int param, index;
  // atype_index = IAC(NATOM)
  // NB_index    = ICO(NTYPES*NTYPES)
  if (LJ_A==NULL || LJ_B==NULL) {
    mprinterr("Error: param file %s does not have LJ A/B coefficients.\n",parmName);
    return 1;
  }
  if (atype_index==NULL || NB_index==NULL) {
    mprinterr("Error: param file %s does not have LJ index information.\n",parmName);
    return 1;
  }
  param = ((ntypes*(atype_index[atom1]-1))+atype_index[atom2])-1; // cpptraj arrays start from 0
  index = NB_index[param]-1;                                      // cpptraj arrays start from 0
  *A = LJ_A[index];
  *B = LJ_B[index];
  return 0;
}

// AmberParm::SetCharges()
/// Set the atomic charges from the given array.
int AmberParm::SetCharges(double *chargeIn) {
  if (chargeIn==NULL) return 1;
  if (charge==NULL) charge = new double[ natom ];
  memcpy(charge, chargeIn, natom * sizeof(double));
  return 0;
}

// -------------------- ROUTINES PERTAINING TO SURFACE AREA --------------------
/* AssignLCPO()
 * Assign parameters for LCPO method. All radii are incremented by 1.4 Ang.
 * NOTE: Member function so it can have access to SurfInfo.
 */
void AmberParm::AssignLCPO(SurfInfo *S, double vdwradii, double P1, double P2, 
                           double P3, double P4) {
  S->vdwradii = vdwradii + 1.4;
  S->P1 = P1;
  S->P2 = P2;
  S->P3 = P3;
  S->P4 = P4;
}

/* WarnLCPO()
 * Called when the number of bonds to the atom of type atype is not usual.
 */
static void WarnLCPO(char *atype, int atom, int numBonds) {
  mprintf("Warning: Unusual number of bonds for atom %i (%i), type %-2s.\n",
          atom, numBonds, atype);
  mprintf("Using default atom parameters.\n");
}

/* AmberParm::SetSurfaceInfo()
 * Set up parameters only used in surface area calcs.
 * LCPO method from:
 *   J. Weiser, P.S. Shenkin, and W.C. Still,
 *   "Approximate atomic surfaces from linear combinations of pairwise
 *   overlaps (LCPO)", J. Comp. Chem. 20:217 (1999).
 * Adapted from gbsa=1 method in SANDER, mdread.f
 * Return the number of solute atoms for which paramters were set.
 * Return -1 on error.
 */
int AmberParm::SetSurfaceInfo() {
  int *numBonds; // # of bonded neighbors each atom has (LCPO only?)
  int i,atom1,atom2;
  char atype[2];
 
  // If surface info already set up exit 
  if (SurfaceInfo!=NULL) return numSoluteAtoms;
 
  // If no bond information exit
  if (bonds==NULL) {
    mprintf("Error: SetSurfaceInfo(): Parm %s does not contain bond info.\n",parmName);
    return -1;
  } 

  // If no atom type information exit
  if (types==NULL) {
    mprintf("Error: SetSurfaceInfo(): Parm %s does not contain atom type info.\n",
            parmName);
    return -1;
  }
 
  // Get the number of bonded neighbors for each atom
  numBonds = new int[ natom ];
  memset(numBonds, 0, natom * sizeof(int));
  for (i = 0; i < NbondsWithoutH*3; i+=3) {
     atom1 = bonds[i  ] / 3;
     atom2 = bonds[i+1] / 3;
     numBonds[atom1]++;
     numBonds[atom2]++;
  }

  // DEBUG
  //for (i=0; i<natom; i++)
  //  fprintf(stdout,"DEBUG:    Atom %6i_%4s: %2i bonds.\n",i,names[i],numBonds[i]);

  // Only set parameters for solute atoms
  numSoluteAtoms = 0;
  if (firstSolvMol > 0) {
    i = 0;
    while (i < firstSolvMol) numSoluteAtoms += atomsPerMol[i++];
  } else {
    numSoluteAtoms = natom;
  }
  mprintf("[%s] Setting surface paramters for %i solute atoms.\n",parmName,numSoluteAtoms);

  // Set vdw radii and LCPO parameters
  SurfaceInfo = new SurfInfo[ numSoluteAtoms ];
  for (i=0; i < numSoluteAtoms; i++) {
    atype[0] = types[i][0];
    atype[1] = types[i][1];

    if (atype[0]=='C' && atype[1]=='T') {
      switch ( numBonds[i] ) {
        case 1: AssignLCPO(SurfaceInfo+i, 1.70, 0.77887, -0.28063, -0.0012968, 0.00039328); break;
        case 2: AssignLCPO(SurfaceInfo+i, 1.70, 0.56482, -0.19608, -0.0010219, 0.0002658);  break;
        case 3: AssignLCPO(SurfaceInfo+i, 1.70, 0.23348, -0.072627, -0.00020079, 0.00007967); break;
        case 4: AssignLCPO(SurfaceInfo+i, 1.70, 0.00000, 0.00000, 0.00000, 0.00000); break;
        default: WarnLCPO(atype,i,numBonds[i]);
                AssignLCPO(SurfaceInfo+i, 1.70, 0.77887, -0.28063, -0.0012968, 0.00039328);
      }
    } else if (atype[0]=='C' || atype[0]=='c') {
      switch ( numBonds[i] ) {
        case 2: AssignLCPO(SurfaceInfo+i, 1.70, 0.51245, -0.15966, -0.00019781, 0.00016392); break;
        case 3: AssignLCPO(SurfaceInfo+i, 1.70, 0.070344, -0.019015, -0.000022009, 0.000016875); break;
        default: WarnLCPO(atype,i,numBonds[i]);
                AssignLCPO(SurfaceInfo+i, 1.70, 0.77887, -0.28063, -0.0012968, 0.00039328);
      }
    } else if (atype[0]=='O' && atype[1]==' ') {
      AssignLCPO(SurfaceInfo+i, 1.60, 0.68563, -0.1868, -0.00135573, 0.00023743);
    } else if (atype[0]=='O' && atype[1]=='2') {
      AssignLCPO(SurfaceInfo+i, 1.60, 0.88857, -0.33421, -0.0018683, 0.00049372);
    } else if (atype[0]=='O' || atype[0]=='o') {
      switch (numBonds[i]) {
        case 1: AssignLCPO(SurfaceInfo+i, 1.60, 0.77914, -0.25262, -0.0016056, 0.00035071); break;
        case 2: AssignLCPO(SurfaceInfo+i, 1.60, 0.49392, -0.16038, -0.00015512, 0.00016453); break;
        default: WarnLCPO(atype,i,numBonds[i]);
                AssignLCPO(SurfaceInfo+i, 1.60, 0.77914, -0.25262, -0.0016056, 0.00035071);
      }
    } else if (atype[0]=='N' && atype[1]=='3') {
      switch (numBonds[i]) {
        case 1: AssignLCPO(SurfaceInfo+i, 1.65, 0.078602, -0.29198, -0.0006537, 0.00036247); break;
        case 2: AssignLCPO(SurfaceInfo+i, 1.65, 0.22599, -0.036648, -0.0012297, 0.000080038); break;
        case 3: AssignLCPO(SurfaceInfo+i, 1.65, 0.051481, -0.012603, -0.00032006, 0.000024774); break;
        default: WarnLCPO(atype,i,numBonds[i]);
                AssignLCPO(SurfaceInfo+i, 1.65, 0.078602, -0.29198, -0.0006537, 0.00036247);
      }
    } else if (atype[0]=='N' || atype[0]=='n') {
      switch (numBonds[i]) {
        case 1: AssignLCPO(SurfaceInfo+i, 1.65, 0.73511, -0.22116, -0.00089148, 0.0002523); break;
        case 2: AssignLCPO(SurfaceInfo+i, 1.65, 0.41102, -0.12254, -0.000075448, 0.00011804); break;
        case 3: AssignLCPO(SurfaceInfo+i, 1.65, 0.062577, -0.017874, -0.00008312, 0.000019849); break;
        default: WarnLCPO(atype,i,numBonds[i]);
                AssignLCPO(SurfaceInfo+i, 1.65, 0.078602, -0.29198, -0.0006537, 0.00036247);
      }
    } else if (atype[0]=='S' && atype[1]=='H') {
      AssignLCPO(SurfaceInfo+i, 1.90, 0.7722, -0.26393, 0.0010629, 0.0002179);
    } else if (atype[0]=='S' || atype[0]=='s') {
      AssignLCPO(SurfaceInfo+i, 1.90, 0.54581, -0.19477, -0.0012873, 0.00029247);
    } else if (atype[0]=='P' || atype[1]=='p') {
      switch (numBonds[i]) {
        case 3: AssignLCPO(SurfaceInfo+i, 1.90, 0.3865, -0.18249, -0.0036598, 0.0004264); break;
        case 4: AssignLCPO(SurfaceInfo+i, 1.90, 0.03873, -0.0089339, 0.0000083582, 0.0000030381); break;
        default: WarnLCPO(atype,i,numBonds[i]);
          AssignLCPO(SurfaceInfo+i, 1.90, 0.3865, -0.18249, -0.0036598, 0.0004264);
      }
    } else if (atype[0]=='Z') {
      AssignLCPO(SurfaceInfo+i, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000);
    } else if (atype[0]=='H' || atype[0]=='h') {
      AssignLCPO(SurfaceInfo+i, 0.00000, 0.00000, 0.00000, 0.00000, 0.00000);
    } else if (atype[0]=='M' && atype[1]=='G') {
      //  Mg radius = 0.99A: ref. 21 in J. Chem. Phys. 1997, 107, 5422
      //  Mg radius = 1.18A: ref. 30 in J. Chem. Phys. 1997, 107, 5422
      //  Mg radius = 1.45A: Aqvist 1992
      //  The following P1-4 values were taken from O.sp3 with two bonded 
      //  neighbors -> O has the smallest van der Waals radius 
      //  compared to all other elements which had been parametrized
      AssignLCPO(SurfaceInfo+i, 1.18, 0.49392, -0.16038, -0.00015512, 0.00016453);
    } else {
      mprintf("Warning: Using carbon SA parms for unknown atom type %i %2s\n",i,atype);
      AssignLCPO(SurfaceInfo+i, 1.70, 0.51245, -0.15966, -0.00019781, 0.00016392);
    }
  } // END LOOP OVER numSoluteAtoms 

  // DEBUG
  /*
  for (i=0; i<numSoluteAtoms; i++) {
    fprintf(stdout,"%6i %4s: %6.2lf %lf %lf %lf %lf\n",i+1,types[i],SurfaceInfo[i].vdwradii,
    fprintf(stdout,"%6i%6.2lf%12.8lf%12.8lf%12.8lf%12.8lf\n",i+1,SurfaceInfo[i].vdwradii,
            SurfaceInfo[i].P1,SurfaceInfo[i].P2,SurfaceInfo[i].P3,SurfaceInfo[i].P4);
  }
  */
  delete[] numBonds;
  return numSoluteAtoms;
}

// -------------------- ROUTINES PERTAINING TO SOLVENT INFO --------------------
/* AmberParm::IsSolventResname()
 * Return true if the residue name corresponds to solvent.
 */
bool AmberParm::IsSolventResname(NAME resnameIn) {
  if ( strcmp("WAT ", resnameIn) == 0 ||
       strcmp(" WAT", resnameIn) == 0 ||
       strcmp("HOH ", resnameIn) == 0 ||
       strcmp(" HOH", resnameIn) == 0 ||
       strcmp("TIP3", resnameIn) == 0 
     )
  {
    return true;
  }
  return false;
}

/* AmberParm::SetSolventInfo()
 * If atomsPerMol has been read in and firstSolvMol is set, determine solvent 
 * information based on what firstSolvMol is. If firstSolvMol is not set, 
 * determine solvent information by residue name, setting/resetting 
 * atomsPerMol as necessary.
 */
int AmberParm::SetSolventInfo() {
  int molAtom, maskAtom; 

  // Allocate memory
  // Since the number of solvent molecules is not yet known allocate
  // natom for solventMoleculeX arrays. Will be resized after.
  solventMask=new char[ natom ];
  memset(solventMask, 'F', natom * sizeof(char));
  solventMoleculeStart=new int[ natom ];
  solventMoleculeStop=new int[ natom ];
  solventMolecules=0;
  solventAtoms=0;

  // If atomsPerMol is set and firstSolvMol (nspsol) is also set, treat all 
  // the molecules starting with firstSolvMol as solvent.
  if (atomsPerMol!=NULL && firstSolvMol!=-1) {
    molAtom = 0;
    for (int mol=0; mol < molecules; mol++) {
      if (mol+1 >= firstSolvMol) {
        // Add this molecule to the solvent list
        solventAtoms += atomsPerMol[mol];
        for (maskAtom=molAtom; maskAtom < molAtom+atomsPerMol[mol]; maskAtom++)
          solventMask[maskAtom] = 'T';
        solventMoleculeStart[solventMolecules] = molAtom;
        solventMoleculeStop[ solventMolecules] = molAtom+atomsPerMol[mol];
        solventMolecules++;
      }
      molAtom += atomsPerMol[mol];
    }

  // Treat all residues with a recognized solvent name as solvent. This will 
  // reset atomsPerMol from the first solvent molecule on. If atomsPerMol is 
  // not set consider all residues up to the first solvent residue to be in a
  // single molecule.
  } else if (resnums!=NULL) {
    firstSolvMol=-1;
    for (int res=0; res < nres; res++) {
      //mprintf("DEBUG:\tConsidering res %i %4s",res,resnames[res]); 
      if ( IsSolventResname(resnames[res])) {
        // Add this residue to the list of solvent 
        molAtom = resnums[res+1] - resnums[res];
        solventAtoms += molAtom;
        solventMoleculeStart[solventMolecules] = resnums[res];
        solventMoleculeStop[ solventMolecules] = resnums[res+1];
        for (maskAtom=resnums[res]; maskAtom < resnums[res+1]; maskAtom++)
          solventMask[maskAtom] = 'T';
        // If firstSolvMol==-1 this residue is the first solvent molecule 
        if (firstSolvMol==-1) {
          // If atomsPerMol is not yet set up, initialize it. Consider all
          // residues up to this one to be in a single molecule.
          if (atomsPerMol==NULL) {
            // First residue is solvent, all is solvent.
            if (res==0) {
              finalSoluteRes=0;   // Starts from 1, Amber convention
              firstSolvMol=1;     // Starts from 1, Amber convention
              molecules=0;
            } else {
              finalSoluteRes=res; // Starts from 1, Amber convention
              firstSolvMol=2;     // Starts from 1, Amber convention
              molecules=1;
              atomsPerMol = new int[ 1 ];
              atomsPerMol[0] = resnums[res];
            }
          } else { 
            molecules = atomToMolecule(resnums[res]);
            firstSolvMol = molecules + 1; // Starts from 1, Amber convention
          }
        } 
        //mprintf(" solvent mol %i, mol %i\n",solventMolecules,molecules); // DEBUG
        // Update atomsPerMol
        int *tempAtomsPerMol = new int[ molecules + 1];
        if (atomsPerMol!=NULL) {
          memcpy(tempAtomsPerMol, atomsPerMol, molecules * sizeof(int));
          delete[] atomsPerMol;
        }
        atomsPerMol = tempAtomsPerMol;
        atomsPerMol[molecules] = molAtom; 
        solventMolecules++;
        molecules++;
      } // END if residue is solvent
        //else mprintf(" not solvent.\n"); // DEBUG
    }
  }

  if (debug>0) {
    mprintf("    %i solvent molecules, %i solvent atoms.\n",
            solventMolecules, solventAtoms);
    if (debug>1)
      mprintf("    FirstSolvMol= %i, FinalSoluteRes= %i\n",firstSolvMol,finalSoluteRes);
  }

  // DEBUG
  //mprintf("MOLECULE INFORMATION:\n");
  //for (int mol = 0; mol < molecules; mol++)
  //  mprintf("\t%8i %8i\n",mol,atomsPerMol[mol]);

  // Deallocate memory if no solvent 
  if (solventMolecules==0) {
    delete[] solventMask;
    solventMask=NULL;
    delete[] solventMoleculeStart;
    solventMoleculeStart=NULL;
    delete[] solventMoleculeStop;
    solventMoleculeStop=NULL;

  // Resize the solventMoleculeX arrays
  } else {
    int *tempSMstart = new int[ solventMolecules ];
    memcpy(tempSMstart, solventMoleculeStart, solventMolecules * sizeof(int));
    delete[] solventMoleculeStart;
    solventMoleculeStart = tempSMstart;
    int *tempSMstop = new int[ solventMolecules ];
    memcpy(tempSMstop, solventMoleculeStop, solventMolecules * sizeof(int));
    delete[] solventMoleculeStop;
    solventMoleculeStop = tempSMstop;
  }

  return 0; 
}
    
// --------========= ROUTINES PERTAINING TO READING PARAMETERS =========--------
/* AmberParm::OpenParm()
 * Attempt to open file and read in parameters.
 */
int AmberParm::OpenParm(char *filename, bool bondsearch, bool molsearch) {
  CpptrajFile parmfile;
  int err=0;

  if ( parmfile.SetupFile(filename,READ,UNKNOWN_FORMAT, UNKNOWN_TYPE,debug) ) 
    return 1;

  // Copy parm filename to parmName. Separate from File.filename in case of stripped parm
  parmName=new char[ strlen(parmfile.basefilename)+1 ];
  strcpy(parmName,parmfile.basefilename);
  parmfileName=new char[ strlen(filename)+1 ]; 
  strcpy(parmfileName,filename);

  if ( parmfile.OpenFile() ) return 1;

  switch (parmfile.fileFormat) {
    case OLDAMBERPARM: err = ReadParmOldAmber(&parmfile); break;
    case AMBERPARM   : err = ReadParmAmber(&parmfile);    break;
    case PDBFILE     : err = ReadParmPDB(&parmfile)  ;    break;
    case MOL2FILE    : err = ReadParmMol2(&parmfile) ;    break;
    case CHARMMPSF   : err = ReadParmPSF(&parmfile)  ;    break;
    default: 
      rprintf("Unknown parameter file type: %s\n",parmfile.filename);
      err=1;
  }

  parmfile.CloseFile();
  if (err>0) {
    mprinterr("Error reading parm file [%s]\n",filename);
    return 1;
  }

  // Create a last dummy residue in resnums that holds natom, which would be
  // the atom number of the next residue if it existed. Atom #s in resnums
  // should correspond with cpptraj atom #s (start from 0) instead of Amber
  // atom #s (start from 1). 
  // Do this to be consistent with PtrajMask selection behavior - saves an 
  // if-then statement.
  int *tempResnums = new int[ nres+1 ];
  memcpy(tempResnums, resnums, nres * sizeof(int));
  delete[] resnums;
  resnums = tempResnums;
  resnums[nres]=natom;
  // DEBUG
  //fprintf(stdout,"==== DEBUG ==== Resnums for %s:\n",parmfile.filename);
  //for (err=0; err<nres; err++) 
  //  fprintf(stdout,"    %i: %i\n",err,resnums[err]);

  // Standardize lengths of atom names and residue names. 4 chars, no
  // leading whitespace. Wrap atom names if they start with a digit, e.g.
  // 1CA becomes CA1. Replace asterisks with ', * is reserved for the mask
  // parser.
  for (int atom=0; atom < natom; atom++) { 
    PadWithSpaces(names[atom]);
    TrimName(names[atom]);
    WrapName(names[atom]);
    ReplaceAsterisk(names[atom]);
  }
  for (int res=0; res < nres; res++) {
    PadWithSpaces(resnames[res]); 
    TrimName(resnames[res]);
    ReplaceAsterisk(names[res]);
  }

  // Set up bond information if specified and necessary
  if (bondsearch) {
    if (bonds==NULL && bondsh==NULL && parmCoords!=NULL)
      GetBondsFromCoords();
  }

  // Set up molecule information if specified and necessary
  if (molsearch) {
    if (atomsPerMol==NULL)
      DetermineMolecules();
  }

  // Set up solvent information
  if (SetSolventInfo()) return 1;

  if (debug>0) {
    mprintf("  Number of atoms= %i\n",natom);
    mprintf("  Number of residues= %i\n",nres);
    mprintf("  Number of molecules= %i\n",molecules);
  }

  // Free coords if they were allocated
  if (parmCoords!=NULL) delete[] parmCoords;
  parmCoords=NULL;
  return 0;
}

// AmberParmOldAmber()
/** Read parameters from an old style (Amber < v7) topology file.
  */
int AmberParm::ReadParmOldAmber(CpptrajFile *parmfile) {
  char *title;
  int values[30], ifbox;

  if (debug>0) mprintf("Reading Old-style Amber Topology file %s\n",parmName);
  title = F_load20a4(parmfile);
  if (debug>0) mprintf("\tOld AmberParm Title: %s\n",title);
  delete[] title;
  // Pointers - same as new format except only 30 values, no NEXTRA
  int *tempvalues = (int*) F_loadFormat(parmfile, FINT, 6, 12, 30, debug);
  if (tempvalues==NULL) {
    mprintf("Could not get values from topfile\n");
    return 1;
  }
  memcpy(values, tempvalues, 30 * sizeof(int));
  delete[] tempvalues;
  // Set some commonly used values
  natom=values[NATOM];
  nres=values[NRES];
  ifbox=values[IFBOX];
  NbondsWithH=values[NBONH];
  NbondsWithoutH=values[MBONA];
  if (debug>=0) {
    mprintf("    Old Amber top contains %i atoms, %i residues.\n",natom,nres);
    mprintf("    %i bonds to hydrogen, %i other bonds.\n",NbondsWithH,NbondsWithoutH);
  }
  // Other values
  ntypes = values[NTYPES];
  nnb = values[NNB];
  // Load the rest of the parm
  // NOTE: Add error checking!
  names = (NAME*) F_loadFormat(parmfile, FCHAR, 4, 20, natom, debug);
  charge = (double*) F_loadFormat(parmfile, FDOUBLE, 16, 5, natom, debug);
  mass = (double*) F_loadFormat(parmfile, FDOUBLE, 16, 5, natom, debug);
  atype_index = (int*) F_loadFormat(parmfile,FINT, 6, 12, natom, debug);
  numex = (int*) F_loadFormat(parmfile,FINT, 6, 12, natom, debug);
  NB_index = (int*) F_loadFormat(parmfile,FINT, 6, 12, ntypes*ntypes, debug);
  resnames = (NAME*) F_loadFormat(parmfile, FCHAR, 4, 20, nres, debug);
  resnums = (int*) F_loadFormat(parmfile,FINT, 6, 12, nres, debug);
  // Atom #s in resnums are currently shifted +1. Shift back to be consistent
  // with the rest of cpptraj.
  for (int atom=0; atom < nres; atom++)
    resnums[atom] -= 1;
  // The following are not used for now
  bond_rk = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NUMBND],debug);
  bond_req = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NUMBND],debug);
  angle_tk = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NUMANG],debug);
  angle_teq = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NUMANG],debug);
  dihedral_pk = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPTRA],debug);
  dihedral_pn = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPTRA],debug);
  dihedral_phase = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPTRA],debug);
  solty = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NATYP],debug);
  // LJ params
  LJ_A = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,ntypes*(ntypes+1)/2,debug);
  LJ_B = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,ntypes*(ntypes+1)/2,debug);
  // Bonds
  bondsh = (int*) F_loadFormat(parmfile,FINT,6,12,values[NBONH]*3,debug);
  bonds = (int*) F_loadFormat(parmfile,FINT,6,12,values[NBONA]*3,debug);
  // Again not used 
  anglesh = (int*) F_loadFormat(parmfile,FINT,6,12,values[NTHETH]*4,debug); 
  angles = (int*) F_loadFormat(parmfile,FINT,6,12,values[NTHETA]*4,debug); 
  dihedralsh = (int*) F_loadFormat(parmfile,FINT,6,12,values[NPHIH]*5,debug);
  dihedrals = (int*) F_loadFormat(parmfile,FINT,6,12,values[NPHIA]*5,debug);
  // Excluded atoms
  excludedAtoms = (int*) F_loadFormat(parmfile,FINT,6,12,nnb,debug);
  // Not used 
  asol = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPHB],debug);
  bsol = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPHB],debug);
  hbcut = (double*) F_loadFormat(parmfile,FDOUBLE,16,5,values[NPHB],debug);
  // Atom types
  types = (NAME*) F_loadFormat(parmfile,FCHAR,4,20,natom,debug);
  // Not used 
  itree = (NAME*) F_loadFormat(parmfile,FCHAR,4,20,natom,debug);
  join_array = (int*) F_loadFormat(parmfile,FINT,6,12,natom,debug);
  irotat = (int*) F_loadFormat(parmfile,FINT,6,12,natom,debug);
  // Solvent/Box info
  if (ifbox > 0) {
    int *solvent_pointer=(int*) F_loadFormat(parmfile,FINT,6,12,3,debug);
    if (solvent_pointer==NULL) {
      mprintf("Error in solvent pointers.\n");
      return 1;
    } else {
      finalSoluteRes=solvent_pointer[0];
      molecules=solvent_pointer[1];
      firstSolvMol=solvent_pointer[2];
      delete[] solvent_pointer;
    }
    atomsPerMol=(int*) F_loadFormat(parmfile,FINT,6,12,molecules,debug);
    if (atomsPerMol==NULL) {mprintf("Error in atoms per molecule.\n"); return 1;}
    // boxFromParm = {OLDBETA, BOX(1), BOX(2), BOX(3)}
    double *boxFromParm=(double*)  F_loadFormat(parmfile,FDOUBLE,16,5,4,debug);
    if (boxFromParm==NULL) {mprintf("Error in box info.\n"); return 1;}
    boxType = SetBoxInfo(boxFromParm,Box,debug);
    delete[] boxFromParm;
    if (debug>0) {
      mprintf("\t%s contains box info: %i mols, first solvent mol is %i\n",
              parmName, molecules, firstSolvMol);
      mprintf("\tBOX: %lf %lf %lf | %lf %lf %lf\n",Box[0],Box[1],Box[2],Box[3],Box[4],Box[5]);
      if (boxType==ORTHO)
        mprintf("\t     Box is orthogonal.\n");
      else if (boxType==NONORTHO)
        mprintf("\t     Box is non-orthogonal.\n");
      else
        mprintf("\t     Box will be determined from first associated trajectory.\n");
    } 
  }
  return 0;
}

// AmberParm::ReadParmAmber() 
/** Read parameters from Amber Topology file
  */
int AmberParm::ReadParmAmber(CpptrajFile *parmfile) {
  int ifbox;
  int *solvent_pointer;
  double *boxFromParm;
  int values[AMBERPOINTERS];
  char *title;
  bool chamber;         // This topology file is a chamber-created topology file

  if (debug>0) mprintf("Reading Amber Topology file %s\n",parmName);
  // Title
  // NOTE: getFlagFileString uses 'new' operator.
  title = getFlagFileString(parmfile, "TITLE",debug);
  // If title is NULL, check for CTITLE (chamber parm)
  if (title==NULL) {
    title = getFlagFileString(parmfile,"CTITLE",debug);
    chamber = true;
  } else {
    chamber = false;
  }
  if (debug>0) mprintf("\tAmberParm Title: %s\n",title);
  delete[] title;
  // Pointers
  int *tempvalues=(int*) getFlagFileValues(parmfile,F_POINTERS,AMBERPOINTERS,debug);
  if (tempvalues==NULL) {
    mprintf("Could not get values from topfile\n");
    return 1;
  }
  memcpy(values, tempvalues, AMBERPOINTERS * sizeof(int));
  delete[] tempvalues;
  // Set some commonly used values
  natom=values[NATOM];
  nres=values[NRES];
  ifbox=values[IFBOX];
  NbondsWithH=values[NBONH];
  NbondsWithoutH=values[MBONA];
  if (debug>0) {
    mprintf("    Amber top contains %i atoms, %i residues.\n",natom,nres);
    mprintf("    %i bonds to hydrogen, %i other bonds.\n",NbondsWithH,NbondsWithoutH);
  }
  // Other values
  ntypes = values[NTYPES];
  nnb = values[NNB];
  // Atom names
  names=(NAME*) getFlagFileValues(parmfile,F_NAMES,natom,debug);
  if (names==NULL) {mprintf("Error in atom names.\n"); return 1;}
  // Charge; convert to units of electron charge
  charge=(double*) getFlagFileValues(parmfile,F_CHARGE,natom,debug);
  if (charge==NULL) {mprintf("Error in charges.\n"); return 1;}
  for (int atom=0; atom < natom; atom++) charge[atom] *= (AMBERTOELEC);
  // Mass
  mass=(double*) getFlagFileValues(parmfile,F_MASS,natom,debug);
  if (mass==NULL) {mprintf("Error in masses.\n"); return 1;}
  // Atom type index
  atype_index = (int*) getFlagFileValues(parmfile,F_ATYPEIDX,natom,debug);
  if (atype_index==NULL) {mprintf("Error in atom type index.\n"); return 1;}
  // Number of excluded atoms
  numex = (int*) getFlagFileValues(parmfile,F_NUMEX,natom,debug);
  if (numex==NULL) {mprintf("Error in number of excluded atoms.\n"); return 1;}
  // Nonbonded parm index
  NB_index = (int*) getFlagFileValues(parmfile,F_NB_INDEX,ntypes*ntypes,debug);
  if (NB_index==NULL) {mprintf("Error in nonbonded parameter index.\n"); return 1;}
  // Residue names
  resnames=(NAME*) getFlagFileValues(parmfile,F_RESNAMES,nres,debug);
  if (resnames==NULL) {mprintf("Error in residue names.\n"); return 1;}
  // Residue atom #s; shift by -1 so that atom #s start from 0
  resnums=(int*) getFlagFileValues(parmfile,F_RESNUMS,nres,debug);
  if (resnums==NULL) {mprintf("Error in residue numbers.\n"); return 1;}
  for (int res=0; res < nres; res++) resnums[res] -= 1;
  // Bond force constants and equilibrium values
  bond_rk = (double*) getFlagFileValues(parmfile, F_BONDRK, values[NUMBND], debug);
  bond_req = (double*) getFlagFileValues(parmfile, F_BONDREQ, values[NUMBND], debug);
  if (bond_rk==NULL || bond_req==NULL) {mprintf("Error in bond constants.\n"); return 1;}
  // Angle force constants and equilibrium values
  angle_tk = (double*) getFlagFileValues(parmfile, F_ANGLETK, values[NUMANG], debug);
  angle_teq = (double*) getFlagFileValues(parmfile, F_ANGLETEQ, values[NUMANG], debug);
  if (angle_tk==NULL || angle_teq==NULL) {mprintf("Error in angle constants.\n"); return 1;}
  // Dihedral force constants, periodicity, and phase values
  dihedral_pk = (double*) getFlagFileValues(parmfile, F_DIHPK, values[NPTRA], debug);
  dihedral_pn = (double*) getFlagFileValues(parmfile, F_DIHPN, values[NPTRA], debug);
  dihedral_phase = (double*) getFlagFileValues(parmfile, F_DIHPHASE, values[NPTRA], debug);
  if (dihedral_pk==NULL || dihedral_pn==NULL || dihedral_phase==NULL) {
    mprintf("Error in dihedral constants.\n"); return 1;
  }
  // SOLTY: currently unused
  solty = (double*) getFlagFileValues(parmfile,F_SOLTY,values[NATYP],debug);
  // Lennard-Jones A/B coefficient
  LJ_A = (double*) getFlagFileValues(parmfile,F_LJ_A,ntypes*(ntypes+1)/2,debug);
  LJ_B = (double*) getFlagFileValues(parmfile,F_LJ_B,ntypes*(ntypes+1)/2,debug);
  if (LJ_A==NULL || LJ_B==NULL) {mprintf("Error reading LJ parameters.\n"); return 1;}
  // Bond information
  bondsh=(int*) getFlagFileValues(parmfile,F_BONDSH,NbondsWithH*3,debug);
  bonds=(int*) getFlagFileValues(parmfile,F_BONDS,NbondsWithoutH*3,debug);
  if (bondsh==NULL || bonds==NULL) {mprintf("Error in bonds.\n"); return 1;}
  // Angle information
  anglesh = (int*) getFlagFileValues(parmfile,F_ANGLESH, values[NTHETH]*4, debug);
  angles  = (int*) getFlagFileValues(parmfile,F_ANGLES , values[NTHETA]*4, debug);
  if (anglesh==NULL || angles==NULL) {mprintf("Error in angles.\n"); return 1;}
  // Dihedral information
  dihedralsh = (int*) getFlagFileValues(parmfile,F_DIHH, values[NPHIH]*5,  debug);
  dihedrals  = (int*) getFlagFileValues(parmfile,F_DIH , values[NPHIA]*5,  debug);
  if (dihedralsh==NULL || dihedrals==NULL) {mprintf("Error in dihedrals.\n"); return 1;}
  // List of excluded atoms; shift by -1 so atom #s start from 0
  excludedAtoms = (int*) getFlagFileValues(parmfile,F_EXCLUDE,nnb,debug);
  if (excludedAtoms==NULL) {mprintf("Error reading list of excluded atoms.\n"); return 1;}
  for (int atom=0; atom < nnb; atom++) excludedAtoms[atom] -= 1;
  // Hbond LJ 10-12 potential terms and cutoff
  asol  = (double*) getFlagFileValues(parmfile,F_ASOL, values[NPHB],debug);
  bsol  = (double*) getFlagFileValues(parmfile,F_BSOL, values[NPHB],debug);
  hbcut = (double*) getFlagFileValues(parmfile,F_HBCUT,values[NPHB],debug);
  // Amber atom types
  types=(NAME*) getFlagFileValues(parmfile,F_TYPES,natom,debug);
  if (types==NULL) {mprintf("Error in atom types.\n"); return 1;}
  // Tree chain classification and joining info 
  itree = (NAME*) getFlagFileValues(parmfile,F_ITREE,natom,debug);
  join_array = (int*) getFlagFileValues(parmfile,F_JOIN,natom,debug);
  // Last atom that would move if atom i was rotated; unused
  irotat = (int*) getFlagFileValues(parmfile,F_IROTAT,natom,debug);
  // GB parameters; radius set, radii, and screening parameters
  title = getFlagFileString(parmfile,"RADIUS_SET",debug);
  if (debug>0) mprintf("\tRadius Set: %s\n",title);
  delete[] title;
  gb_radii = (double*) getFlagFileValues(parmfile,F_RADII,natom,debug);
  gb_screen = (double*) getFlagFileValues(parmfile,F_SCREEN,natom,debug);
  if (gb_radii==NULL || gb_screen==NULL) {mprintf("Error reading gb parameters.\n"); return 1;}
  // Get solvent info if IFBOX>0
  if (values[IFBOX]>0) {
    solvent_pointer=(int*) getFlagFileValues(parmfile,F_SOLVENT_POINTER,3,debug);
    if (solvent_pointer==NULL) {
      mprintf("Error in solvent pointers.\n");
      return 1;
    } else {
      finalSoluteRes=solvent_pointer[0];
      molecules=solvent_pointer[1];
      firstSolvMol=solvent_pointer[2];
      delete[] solvent_pointer;
    }
    atomsPerMol=(int*) getFlagFileValues(parmfile,F_ATOMSPERMOL,molecules,debug);
    if (atomsPerMol==NULL) {mprintf("Error in atoms per molecule.\n"); return 1;}
    // boxFromParm = {OLDBETA, BOX(1), BOX(2), BOX(3)}
    boxFromParm=(double*) getFlagFileValues(parmfile,F_PARMBOX,4,debug);
    // If no box information present in the parm (such as with Chamber prmtops)
    // set the box info if ifbox = 2, otherwise set to NOBOX; the box info will 
    // eventually be set by angles from the first trajectory associated with 
    // this parm.
    if (boxFromParm==NULL) {
      if (not chamber) mprintf("Warning: Prmtop missing Box information.\n");
      // ifbox 2: truncated octahedron for certain
      if (ifbox == 2) {
        boxType = NONORTHO;
        Box[0] = 0.0; Box[1] = 0.0; Box[2] = 0.0;
        Box[3] = TRUNCOCTBETA;
        Box[4] = TRUNCOCTBETA;
        Box[5] = TRUNCOCTBETA;
      } else
        boxType = NOBOX;
    // Determine box type, set Box angles and lengths from beta (boxFromParm[0])
    } else {
      boxType = SetBoxInfo(boxFromParm,Box,debug);
      delete[] boxFromParm;
    }
    if (debug>0) {
      mprintf("\t%s contains box info: %i mols, first solvent mol is %i\n",
              parmName, molecules, firstSolvMol);
      mprintf("\tBOX: %lf %lf %lf | %lf %lf %lf\n",Box[0],Box[1],Box[2],Box[3],Box[4],Box[5]);
      if (boxType==ORTHO)
        mprintf("\t     Box is orthogonal.\n");
      else if (boxType==NONORTHO)
        mprintf("\t     Box is non-orthogonal.\n");
      else
        mprintf("\t     Box will be determined from first associated trajectory.\n");
    }
  }
  // If parm contains IFCAP or IFPERT info, print a warning since cpptraj
  // currently does not read these in.
  if (values[IFCAP] > 0) 
    mprintf("\tWarning: Parm [%s] contains CAP information, which Cpptraj ignores.\n");
  if (values[IFPERT] > 0)
    mprintf("\tWarning: Parm [%s] contains PERT information, which Cpptraj ignores.\n");

  return 0;
}

/* AmberParm::SetAtomsPerMolPDB()
 * Use in ReadParmPDB only, when TER is encountered or end of PDB file
 * update the atomsPerMol array. Take number of atoms in the molecule
 * (calcd as current #atoms - #atoms in previous molecule) as input. 
 * Check if the last residue is solvent; if so, set up solvent information.
 * Return the current number of atoms.
 */
int AmberParm::SetAtomsPerMolPDB(int numAtoms) {
  if (numAtoms<1) return 0;
  // Check if the current residue is a solvent molecule
  //mprintf("DEBUG: Checking if %s is solvent.\n",resnames[nres-1]);
  //if (nres>0 && IsSolventResname(resnames[nres-1])) {
  //  if (firstSolvMol==-1) {
  //    firstSolvMol = molecules + 1; // +1 to be consistent w/ Amber top
  //    finalSoluteRes = nres - 1;    // +1 to be consistent w/ Amber top
  //  }
  //}
  int *tempAPM = new int[ molecules+1 ];
  memcpy(tempAPM, atomsPerMol, molecules * sizeof(int));
  delete[] atomsPerMol;
  atomsPerMol = tempAPM;
  atomsPerMol[molecules] = numAtoms;
  molecules++;
  return natom;
}

/* AmberParm::ReadParmPDB()
 * Open the PDB file specified by filename and set up topology data.
 * Mask selection requires natom, nres, names, resnames, resnums.
 */
int AmberParm::ReadParmPDB(CpptrajFile *parmfile) {
  char buffer[256];
  int bufferLen;  
  int currResnum;
  int atom;
  int atomInLastMol = 0;
  unsigned int crdidx = 0;

  mprintf("    Reading PDB file %s as topology file.\n",parmName);
  currResnum=-1;
  memset(buffer,' ',256);

  while ( parmfile->IO->Gets(buffer,256)==0 ) {
    // If ENDMDL or END is reached stop reading
    if ( strncmp(buffer,"END",3)==0) break;
    // If TER increment number of molecules and continue
    if ( strncmp(buffer,"TER",3)==0) {
      atomInLastMol = SetAtomsPerMolPDB(natom - atomInLastMol);
      continue;
    }
    // Skip all other non-ATOM records
    if (isPDBatomKeyword(buffer)) {
      // Detect and remove trailing newline
      bufferLen = strlen(buffer);
      if (buffer[bufferLen-1] == '\n') buffer[bufferLen-1]='\0';

      // Allocate memory for atom name.
      NAME *tempname = new NAME[ natom+1 ];
      memcpy(tempname, names, natom * sizeof(NAME));
      delete[] names;
      names = tempname;
      // Leading whitespace will automatically be trimmed.
      // Name will be wrapped if it starts with a digit.
      // Asterisks will be replaced with prime char
      pdb_name(buffer, (char*)names[natom]);

      // Allocate memory for coords
      double *tempcoord = new double[ (natom+1)*3 ];
      memcpy(tempcoord, parmCoords, (natom*3)*sizeof(double) );
      delete[] parmCoords;
      parmCoords = tempcoord;
      pdb_xyz(buffer, parmCoords + crdidx);
      crdidx+=3;

      // If this residue number is different than the last, allocate mem for new res
      if (currResnum!=pdb_resnum(buffer)) {
        NAME *temprname = new NAME[ nres+1 ];
        memcpy(temprname, resnames, nres * sizeof(NAME));
        delete[] resnames;
        resnames = temprname;
        // Leading whitespace will automatically be trimmed.
        // Asterisks will be replaced with prime char
        pdb_resname(buffer, (char*)resnames[nres]);
        if (debug>3) mprintf("        PDBRes %i [%s]\n",nres,resnames[nres]);
        int *temprnum = new int[ nres+1 ];
        memcpy(temprnum, resnums, nres * sizeof(int));
        delete[] resnums;
        resnums = temprnum;
        resnums[nres]=natom; 
        currResnum=pdb_resnum(buffer);
        nres++;
  
      // If residue number hasnt changed check for duplicate atom names in res
      // NOTE: At this point nres has been incremented. Want nres-1.
      //       natom is the current atom.
      } else {
        for (atom=resnums[nres-1]; atom < natom; atom++) {
          if ( strcmp(names[natom], names[atom])==0 ) {
            mprintf("      Warning: Duplicate atom name in residue %i [%s]:%i\n",
                    nres,names[natom],natom+1);
          }
        }
      }
      // Clear the buffer
      memset(buffer,' ',256);

      natom++;
    } // END if atom/hetatm keyword
  } // END read in parmfile

  // If a TER card has been read and we are setting up the number of molecules,
  // finish up info on the last molecule read.
  if (molecules>0) {
    SetAtomsPerMolPDB(natom - atomInLastMol);
    // DEBUG
    if (debug>0) {
      //mprintf("\tPDB: firstSolvMol= %i\n",firstSolvMol);
      mprintf("\tPDB: finalSoluteRes= %i\n",finalSoluteRes);
      if (debug>1) {
        mprintf("\tPDB: Atoms Per Molecule:\n");
        for (atom=0; atom < molecules; atom++) {
          mprintf("\t     %8i %8i\n",atom,atomsPerMol[atom]);
        } 
      }
    }
  }

  // No box for PDB - maybe change later to include unit cell info?
  boxType = NOBOX;

  if (debug>0) 
    mprintf("\tPDB contains %i atoms, %i residues, %i molecules.\n",
            natom,nres,molecules);
  // If no atoms, probably issue with PDB file
  if (natom<=0) {
    mprintf("Error: No atoms in PDB file.\n");
    return 1;
  }

  return 0;
}

/* AmberParm::ReadParmMol2()
 * Read file as a Tripos Mol2 file.
 */
int AmberParm::ReadParmMol2(CpptrajFile *parmfile) {
  char buffer[MOL2BUFFERSIZE];
  int mol2bonds;
  int resnum, currentResnum;
  unsigned int crdidx = 0;
  char resName[5];

  currentResnum=-1;
  mprintf("    Reading Mol2 file %s as topology file.\n",parmName);
  // Get @<TRIPOS>MOLECULE information
  if (Mol2ScanTo(parmfile, MOLECULE)) return 1;
  //   Scan title
  if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
  if (debug>0) mprintf("      Mol2 Title: [%s]\n",buffer);
  //   Scan # atoms and bonds
  // num_atoms [num_bonds [num_subst [num_feat [num_sets]]]]
  if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
  mol2bonds=0;
  sscanf(buffer,"%i %i",&natom, &mol2bonds);
  if (debug>0) {
    mprintf("      Mol2 #atoms: %i\n",natom);
    mprintf("      Mol2 #bonds: %i\n",mol2bonds);
  }

  // Allocate memory for atom names, types, and charges.
  names = new NAME[ natom ];
  types = new NAME[ natom ];
  charge = new double[ natom ];
  // Allocate space for coords
  parmCoords = new double[ natom * 3 ];

  // Get @<TRIPOS>ATOM information
  if (Mol2ScanTo(parmfile, ATOM)) return 1;
  for (int atom=0; atom < natom; atom++) {
    if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
    // atom_id atom_name x y z atom_type [subst_id [subst_name [charge [status_bit]]]]
    //sscanf(buffer,"%*i %s %*f %*f %*f %s %i %s %lf", names[atom], types[atom],
    //       &resnum,resName, charge+atom);
    //mprintf("      %i %s %s %i %s %lf\n",atom,names[atom],types[atom],resnum,resName,charge[atom]);
    Mol2AtomName(buffer,names[atom]);
    Mol2AtomType(buffer,types[atom]);
    Mol2XYZ(buffer,parmCoords + crdidx);
    crdidx += 3;
    Mol2ResNumName(buffer,&resnum,resName);
    charge[atom]=Mol2Charge(buffer);
    // Check if residue number has changed - if so record it
    if (resnum != currentResnum) {
      NAME *temprname = new NAME[ nres+1 ];
      memcpy(temprname, resnames, nres * sizeof(NAME));
      delete[] resnames;
      resnames = temprname;
      strcpy(resnames[nres], resName);
      int *temprnum = new int[ nres+1 ];
      memcpy(temprnum, resnums, nres * sizeof(int));
      delete[] resnums;
      resnums = temprnum;
      resnums[nres]=atom; 
      currentResnum = resnum;
      nres++;
    }
  }

  // Get @<TRIPOS>BOND information [optional]
  NbondsWithoutH=0;
  NbondsWithH=0;
  if (Mol2ScanTo(parmfile, BOND)==0) {
    for (int bond=0; bond < mol2bonds; bond++) {
      if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
      // bond_id origin_atom_id target_atom_id bond_type [status_bits]
      //         resnum         currentResnum
      sscanf(buffer,"%*i %i %i\n",&resnum,&currentResnum);
      // mol2 atom #s start from 1
      AddBond(resnum-1, currentResnum-1,0);
    }
  } else {
    mprintf("      Mol2 file does not contain bond information.\n");
  }

  // No box
  boxType = NOBOX;

  mprintf("    Mol2 contains %i atoms, %i residues,\n", natom,nres);
  mprintf("    %i bonds to H, %i other bonds.\n", NbondsWithH,NbondsWithoutH);

  return 0;
}

/* AmberParm::ReadParmPSF()
 * Open the Charmm PSF file specified by filename and set up topology data.
 * Mask selection requires natom, nres, names, resnames, resnums.
 */
int AmberParm::ReadParmPSF(CpptrajFile *parmfile) {
  char buffer[256],tag[256],psfname[NAMESIZE];
  int bondatoms[8];
  int currResnum;
  int psfresnum;
  int psfattype;
  int nbond,nlines;

  mprintf("    Reading Charmm PSF file %s as topology file.\n",parmName);
  currResnum=-1;
  memset(buffer,' ',256);
  memset(tag,' ',256);
  tag[0]='\0';

  // Read the first line, should contain PSF...
  if (parmfile->IO->Gets(buffer,256)) return 1;
  // Sanity check
  if (buffer[0]!='P' || buffer[1]!='S' || buffer[2]!='F') {
    mprinterr("Error: ReadParmPSF(): Could not read Charmm PSF file.\n");
    return 1;
  }
  // Advance to <natom> !NATOM
  while (strncmp(tag,"!NATOM",6)!=0) {
    if (parmfile->IO->Gets(buffer,256)) return 1;
    sscanf(buffer,"%i %s",&natom,tag);
  }
  mprintf("\tPSF: !NATOM tag found, natom=%i\n",natom);
  // If no atoms, probably issue with PSF file
  if (natom<=0) {
    mprintf("Error: No atoms in PSF file.\n");
    return 1;
  }

  // Allocate memory for atom name, charge, mass.
  names=(NAME*)    new NAME[ natom ];
  mass=(double*)   new double[ natom ];
  charge=(double*) new double[ natom ];

  // Read the next natom lines
  for (int atom=0; atom < natom; atom++) {
    if (parmfile->IO->Gets(buffer,256) ) {
      mprinterr("Error: ReadParmPSF(): Reading atom %i\n",atom+1);
      return 1;
    }
    // Detect and remove trailing newline
    //bufferLen = strlen(buffer);
    //if (buffer[bufferLen-1] == '\n') buffer[bufferLen-1]='\0';
    // Read line
    // ATOM# SEGID RES# RES ATNAME ATTYPE CHRG MASS (REST OF COLUMNS ARE LIKELY FOR CMAP AND CHEQ)
    sscanf(buffer,"%*8i %*4s %i %4s %4s %4i %14lf %14lf",&psfresnum,tag,psfname,
           &psfattype,charge+atom,mass+atom);
    strcpy(names[atom],psfname);
    // If this residue number is different than the last, allocate mem for new res
    if (currResnum!=psfresnum) {
        NAME *temprname = new NAME[ nres+1 ];
        memcpy(temprname, resnames, nres * sizeof(NAME));
        delete[] resnames;
        resnames = temprname;
        strcpy(resnames[nres],tag);
        if (debug>3) mprintf("        PSFRes %i [%s]\n",nres,resnames[nres]);
        int *temprnum = new int[ nres+1 ];
        memcpy(temprnum, resnums, nres * sizeof(int));
        delete[] resnums;
        resnums = temprnum;
        resnums[nres]=atom; 
        currResnum=psfresnum;
        nres++;
    }
    // Clear the buffer
    memset(buffer,' ',256);
  } // END loop over atoms 

  // Advance to <nbond> !NBOND
  while (strncmp(tag,"!NBOND",6)!=0) {
    if (parmfile->IO->Gets(buffer,256)) return 1;
    sscanf(buffer,"%i %s",&nbond,tag);
  }
  nlines = nbond / 4;
  if ( (nbond % 4) != 0) nlines++;
  for (int bondline=0; bondline < nlines; bondline++) {
    if (parmfile->IO->Gets(buffer,256) ) {
      mprinterr("Error: ReadParmPSF(): Reading bond line %i\n",bondline+1);
      return 1;
    }
    // Each line has 4 pairs of atom numbers
    int nbondsread = sscanf(buffer,"%i %i %i %i %i %i %i %i",bondatoms,bondatoms+1,
                            bondatoms+2,bondatoms+3, bondatoms+4,bondatoms+5,
                            bondatoms+6,bondatoms+7);
    // NOTE: Charmm atom nums start from 1
    for (int bondidx=0; bondidx < nbondsread; bondidx+=2)
      AddBond(bondatoms[bondidx]-1,bondatoms[bondidx+1]-1,-1);
  }
  //mprintf("DEBUG: Charmm PSF last line after bond read:\n");
  //mprintf("\t[%s]\n",buffer);
  mprintf("\t%i bonds to hydrogen.\n\t%i bonds to non-hydrogen.\n",NbondsWithH,NbondsWithoutH);
    

  //boxType = NOBOX;

  //if (debug>0) 
    mprintf("    PSF contains %i atoms, %i residues, %i molecules.\n",
            natom,nres,molecules);

  return 0;
}

// -----------------------------------------------------------------------------
/* AmberParm::AtomInfo()
 * Print parm information for atom.
 */
void AmberParm::AtomInfo(int atom) {
  int res = atomToResidue(atom);
  mprintf("  Atom %i:",atom+1);
  mprintf("[%s]",names[atom]);
  mprintf(" Res %i:",res+1);
  mprintf("[%s]",resnames[res]);
  mprintf(" Mol %i", atomToMolecule(atom)+1);
  if (types!=NULL)
    mprintf(" Type=[%s]",types[atom]);
  if (charge!=NULL)
    mprintf(" Charge=%lf",charge[atom]);
  if (mass!=NULL)
    mprintf(" Mass=%lf",mass[atom]);
  mprintf("\n");
}

/* AmberParm::Info()
 * Print information about this parm to buffer.
 */
void AmberParm::ParmInfo() {

  mprintf(" %i: %s, %i atoms, %i res",pindex,parmfileName,natom,nres);
  if (boxType==NOBOX)
    mprintf(", no box");
  else if (boxType==ORTHO)
    mprintf(", ortho. box");
  else if (boxType==NONORTHO)
    mprintf(", non-ortho. box");
  if (molecules>0)
    mprintf(", %i mol",molecules);
  if (solventMolecules>0)
    mprintf(", %i solvent mol",solventMolecules);
  if (parmFrames>0)
    mprintf(", %i frames",parmFrames);
  mprintf("\n");
}

/* AmberParm::Summary()
 * Print a summary of atoms, residues, molecules, and solvent molecules
 * in this parm.
 */
void AmberParm::Summary() {
  mprintf("              Topology contains %i atoms.\n",this->natom);
  mprintf("                                %i residues.\n",this->nres);
  int number_of_bonds = NbondsWithH + NbondsWithoutH;
  mprintf("                                %i bonds.\n",number_of_bonds);
  if (this->molecules>0)
    mprintf("                                %i molecules.\n",this->molecules);
  if (this->solventMolecules>0) {
    mprintf("                                %i solvent molecules.\n",this->solventMolecules);
    mprintf("                  First solvent molecule is %i\n",this->firstSolvMol);
  }
}

/* AmberParm::PrintBondInfo()
 * Print information contained in bonds and bondsh arrays.
 */
void AmberParm::PrintBondInfo() {
  int atom1,atom2,atomi;
  if ((NbondsWithH + NbondsWithoutH) <= 0) {
    mprintf("NO BOND INFORMATION IN PRMTOP\n");
    return;
  }
  if (NbondsWithH>0) {
    mprintf("%i BONDS TO HYDROGEN:\n",NbondsWithH);
    for (int ibond=0; ibond < NbondsWithH * 3; ibond += 3) {
      atom1 = (bondsh[ibond  ]/3) + 1;
      atom2 = (bondsh[ibond+1]/3) + 1;
      atomi = (bondsh[ibond+2]  );
      mprintf("\tAtom %i to %i, %i\n",atom1,atom2,atomi);
    }
  }
  if (NbondsWithoutH>0) {
    mprintf("%i BONDS TO NON-HYDROGEN:\n",NbondsWithoutH);
    for (int ibond=0; ibond < NbondsWithoutH * 3; ibond += 3) {
      atom1 = (bonds[ibond  ]/3) + 1;
      atom2 = (bonds[ibond+1]/3) + 1;
      atomi = (bonds[ibond+2]  );
      mprintf("\tAtom %i to %i, %i\n",atom1,atom2,atomi);
    }
  }
}

/* AmberParm::PrintMoleculeInfo()
 * Print information on molecules in PRMTOP
 */
void AmberParm::PrintMoleculeInfo() {
  int atomcount = 0;
  int resid;
  char rtemp[32];
  if (molecules==0 || atomsPerMol==NULL) {
    mprintf("NO MOLECULE INFORMATION IN PRMTOP\n");
    return;
  }
  mprintf("MOLECULES:\n");
  for (int mol=0; mol < molecules; mol++) {
    resid = atomToResidue(atomcount);
    ResName(rtemp,resid);
    mprintf("\tMolecule %i, %i atoms, first residue %s\n",mol+1,atomsPerMol[mol],rtemp);
    atomcount += atomsPerMol[mol];
  }
}

// -----------------------------------------------------------------------------
// NOTE: The following atomToX functions do not do any memory checks!
/* AmberParm::atomToResidue()
 * Given an atom number, return corresponding residue number.
 */
int AmberParm::atomToResidue(int atom) {
  int i;

  for (i = 0; i < nres; i++)
    if ( atom>=resnums[i] && atom<resnums[i+1] )
      return i;

  return -1;
}

/* AmberParm::atomToMolecule()
 * Given an atom number, return corresponding molecule number.
 */
int AmberParm::atomToMolecule(int atom) {
  int a = 0;
  for (int i = 0; i < molecules; i++) {
    a += atomsPerMol[i];
    if (atom < a) return i;
  }
  return -1;
}

/* AmberParm::atomToSolventMolecule()
 * Given an atom number, return corresponding solvent molecule
 * NOTE: Could this be achieved with atomToMolecule and solventMask?
 */
int AmberParm::atomToSolventMolecule(int atom) {
  int i, atom1;

  atom1 = atom + 1; 
  for (i = 0; i < molecules; i++) {
    if (atom1 <= solventMoleculeStart[i])
      return -1;
    else if (atom1>solventMoleculeStart[i] && atom1<=solventMoleculeStop[i])
      return i;
  }

  return -1;
}

// -----------------------------------------------------------------------------
/* AmberParm::ResetBondInfo()
 * Reset the bonds and bondsh arrays, as well as NBONH and MBONA
 */
void AmberParm::ResetBondInfo() {
  if (bonds!=NULL) delete[] bonds;
  bonds=NULL;
  if (bondsh!=NULL) delete[] bondsh;
  bondsh=NULL;
  NbondsWithH=0;
  NbondsWithoutH=0;
}

/* AmberParm::AddBond()
 * Add bond info for the two atoms. Attempt to identify if it is a bond
 * to hydrogen or not based on names. The atom numbers should start from 0.
 * Atom indices in bond arrays are * 3.
 */ 
int AmberParm::AddBond(int atom1, int atom2, int icb) {
  bool isH=false;
  int bondidx;
  if (atom1<0 || atom2<0 || atom1>=natom || atom2>=natom) return 1;
  if (names!=NULL) {
    if (names[atom1][0]=='H') isH=true;
    if (names[atom2][0]=='H') isH=true;
  }
  if (isH) {
    //NbondsWithH = values[NBONH];
    bondidx = NbondsWithH * 3;
    int *tempbondsh = new int[ bondidx+3 ];
    memcpy(tempbondsh, bondsh, bondidx * sizeof(int));
    delete[] bondsh;
    bondsh = tempbondsh;
    bondsh[bondidx  ] = atom1 * 3;
    bondsh[bondidx+1] = atom2 * 3;
    bondsh[bondidx+2] = icb;
    NbondsWithH++;
  } else {
    //NbondsWithoutH = values[MBONA];
    bondidx = NbondsWithoutH * 3;
    int *tempbonds = new int[ bondidx+3 ];
    memcpy(tempbonds, bonds, bondidx * sizeof(int));
    delete[] bonds;
    bonds = tempbonds;
    bonds[bondidx  ] = atom1 * 3;
    bonds[bondidx+1] = atom2 * 3;
    bonds[bondidx+2] = icb;
    NbondsWithoutH++;
  }
  return 0;
}

/* AmberParm::GetBondsFromCoords()
 * Given an array of coordinates X0Y0Z0X1Y1Z1...XNYNZN determine which
 * atoms are bonded via distance search. First check for bonds within
 * residues, then check for bonds between adjacent residues. Adjacent
 * residues in different molecules are not considered.
 */
void AmberParm::GetBondsFromCoords() {
  int res, startatom, stopatom, midatom,atom1, atom2, idx1, idx2;
  double D, cut;
  int *resmols;
  if (parmCoords==NULL) return;
  mprintf("\t%s: determining bond info from distances.\n",parmName);
  // Determine bonds within residues.
  for (res = 0; res < nres; res++) {
    startatom = resnums[res];
    stopatom = resnums[res+1];
    //mprintf("\t\tDetermining bonds within residue %i\n",res);
    for (atom1 = startatom; atom1 < stopatom - 1; atom1++) {
      idx1 = atom1 * 3;
      for (atom2 = atom1 + 1; atom2 < stopatom; atom2++) {
        idx2 = atom2 * 3;
        D = DIST2_NoImage(parmCoords + idx1, parmCoords + idx2);
        //mprintf("\t\tGetting cutoff for [%s] - [%s]\n",names[atom1],names[atom2]);
        cut = GetBondedCut(names[atom1],names[atom2]);
        cut *= cut; // Op '*' less expensive than sqrt
//        if (debug>0) {
//          if (debug==1) {
//            if (D<cut) mprintf("\tBOND: %s %i to %s %i\n",names[atom1],atom1+1,
//                                names[atom2],atom2+1);
//          } else if (debug > 1) {
//            mprintf("Distance between %s %i and %s %i is %lf, cut %lf",names[atom1],atom1+1,
//                    names[atom2],atom2+1,D,cut);
//            if (D<cut) mprintf(" bonded!");
//            mprintf("\n");
//          }
//        }
        if (D < cut) AddBond(atom1,atom2,-1);
      }
    }
  }

  // If atomsPerMol has been set up, create an array that will contain the 
  // molecule number of each residue.
  resmols = new int[ nres ];
  if (atomsPerMol!=NULL) {
    int molnum = 0;
    int atotal = atomsPerMol[0];
    for (res = 0; res < nres; res++) {
      resmols[res] = molnum;
      if (resnums[res+1] >= atotal) {
        molnum++;
        if (molnum >= molecules) break;
        atotal += atomsPerMol[molnum];
      }
    }
  } else
    memset(resmols, 0, nres * sizeof(int));
  // DEBUG
  //for (res = 0; res < nres; res++) 
  //  mprintf("DEBUG\tRes %8i %4s Mol %8i\n",res+1,resnames[res],resmols[res]);

  // Determine bonds between adjacent residues.
  for (res = 1; res < nres; res++) {
    // Dont check for bonds between residues that are in different molecules
    if (resmols[res-1] != resmols[res]) continue;
    startatom = resnums[res-1];
    midatom = resnums[res];
    stopatom = resnums[res+1];
    //mprintf("\t\tDetermining bonds between residues %i and %i\n",res-1,res);
    for (atom1 = startatom; atom1 < midatom; atom1++) {
      idx1 = atom1 * 3;
      for (atom2 = midatom; atom2 < stopatom; atom2++) {
        idx2 = atom2 * 3;
        D = DIST2_NoImage(parmCoords + idx1, parmCoords + idx2);
        cut = GetBondedCut(names[atom1],names[atom2]);
        cut *= cut;
        if (D < cut) AddBond(atom1,atom2,-1);
      } 
    }
  }
  
  delete[] resmols;

  mprintf("\t%s: %i bonds to hydrogen, %i other bonds.\n",parmName,NbondsWithH,NbondsWithoutH);
}

/* AmberParm::DetermineMolecules()
 * Given that bonding information for the parm has been set up, attempt
 * to determine how many molecules (i.e. entities that are not covalently
 * bonded) there are.
 */
int AmberParm::DetermineMolecules() {
  BondInfo mol;
  int bond3;

  if (bonds==NULL && bondsh==NULL) {
    mprinterr("Error: DetermineMolecules: No bond information set up.\n");
    return 1;
  }
  mprintf("\t%s: Determining molecule information from bonds.\n",parmName);

  mol.Setup(natom);

  // Set max valences
  for (int atom=0; atom < natom; atom++) 
    mol.SetValence(atom,names[atom]);

  // Go through the bonds and bondsh arrays
  if (bondsh!=NULL) {
    bond3 = NbondsWithH * 3;
    for (int bond=0; bond < bond3; bond+=3) {
      int atom1 = bondsh[bond  ] / 3;
      int atom2 = bondsh[bond+1] / 3;
      mol.CreateBond(atom1,atom2);
    }
  }
  if (bonds!=NULL) {
    bond3 = NbondsWithoutH * 3;
    for (int bond=0; bond < bond3; bond+=3) {
      int atom1 = bonds[bond  ] / 3;
      int atom2 = bonds[bond+1] / 3;
      mol.CreateBond(atom1,atom2);
    }
  }
  atomsPerMol = mol.DetermineMolecules(&molecules);
 
  //mol.PrintBonds();

  return 0;
}

/* SetupBondArray()
 * Given an atom map and new parm, set up bond array
 * NOTE: Set up atom map to be atom*3??
 */
static int *SetupBondArray(int *atomMap, int oldN3, int *oldBonds, int *newN) {
  int *bonds;
  int N3, i, atom1, atom2;

  if (atomMap==NULL || oldBonds==NULL) return NULL;
  bonds=NULL;
  N3=0;
  // Go through Bonds with/without H, use atomMap to determine what goes into newParm
  for (i=0; i < oldN3; i+=3) {
    // Check that atom1 and atom2 exist in newParm
    // In the bond arrays atom nums are multiplied by 3
    atom1 = atomMap[ oldBonds[i]/3   ];
    atom2 = atomMap[ oldBonds[i+1]/3 ];
    if ( atom1!=-1 && atom2!=-1 ) {
      // Put new atom 1 and new atom 2 in newParm array
      int *tempbonds = new int[ N3+3 ];
      memcpy(tempbonds, bonds, N3 * sizeof(int));
      delete[] bonds;
      bonds = tempbonds; 
      bonds[N3]   = atom1 * 3;
      bonds[N3+1] = atom2 * 3;
      bonds[N3+2] = oldBonds[i+2];
      N3+=3;
    }
  }
  
  *newN = N3 / 3;
  return bonds;
}

// -----------------------------------------------------------------------------
/* AmberParm::modifyStateByMap()
 * Currently only intended for use with AtomMap.
 * This routine will create a new amber parm (newParm) base on the
 * current amber parm (this), mapping atoms in newParm to atoms
 * in this based on the given atom map.
 * NOTE: There is no guarantee that atoms that were contiguous in 
 *       this parm will be contiguous in the old parm since this is not
 *       currently enforced by AtomMap; therefore the residue information
 *       will probably be shot unless there is only 1 residue. 
 * NOTE: Molecule, solvent info etc is not copied over.
 */
AmberParm *AmberParm::modifyStateByMap(int *AMap) {
  AmberParm *newParm;
  int j=0;
  int *ReverseMap;

  newParm = new AmberParm();
  newParm->SetDebug(debug);
  // Allocate space for arrays and perform initialization
  newParm->names    = new NAME[ natom ];
  if (this->types!=NULL)
    newParm->types    = new NAME[ natom ];
  if (this->charge!=NULL)
    newParm->charge   = new double[ natom ];
  if (this->mass!=NULL)
    newParm->mass     = new double[ natom ];
  newParm->resnames = new NAME[ nres ];
  newParm->resnums  = new int[ nres+1 ];
  // Need reverse of AMap, Map[tgt atom] = ref atom for setting up bonds
  ReverseMap = new int[ natom ];

  // Loop over all atoms in this parm, map them to new parm
  for (int i=0; i < this->natom; i++) {
    j = AMap[i];
    ReverseMap[j] = i;
    strcpy(newParm->names[i], this->names[j]);
    if (this->types!=NULL)  strcpy(newParm->types[i], this->types[j]);
    if (this->charge!=NULL) newParm->charge[i] =      this->charge[j];
    if (this->mass!=NULL)   newParm->mass[i]   =      this->mass[j];
  }

  // Copy residue info. If > 1 residue the copy will likely not be correct.
  if (this->nres>1) {
    mprintf("WARNING: modifyStateByMap: %s has > 1 residue, modified parm residue info\n",parmName);
    mprintf("         will most likely not be correct!\n");
  }
  for (int res=0; res<this->nres; res++) {
    strcpy(newParm->resnames[res],this->resnames[res]);
    newParm->resnums[res] = this->resnums[res];
  }
  // Fix up IPRES
  newParm->resnums[this->nres] = this->natom;

  // Set up bond arrays
  newParm->bondsh = SetupBondArray(ReverseMap, this->NbondsWithH*3, this->bondsh,
                                   &(newParm->NbondsWithH));
  newParm->bonds  = SetupBondArray(ReverseMap, this->NbondsWithoutH*3, this->bonds,
                                   &(newParm->NbondsWithoutH));
  // Clear reverse map
  delete[] ReverseMap; 

  // Set up new parm information
  newParm->natom = this->natom;
  newParm->nres = this->nres;
  newParm->parmFrames = this->parmFrames;

  // Give mapped parm the same pindex as original parm
  newParm->pindex = this->pindex;

  // Copy box information
  for (int i=0; i<6; i++)
    newParm->Box[i] = this->Box[i];
  newParm->boxType=this->boxType;

  return newParm;
}

/* AmberParm::modifyStateByMask()
 * Adapted from ptraj
 *  The goal of this routine is to create a new AmberParm (newParm)
 *  based on the current AmberParm (this), deleting atoms that are
 *  not in the Selected array.
 * NOTE: Make all solvent/box related info dependent on IFBOX only?
 */
AmberParm *AmberParm::modifyStateByMask(int *Selected, int Nselected) {
  AmberParm *newParm;
  int selected;
  int i, ires, imol; 
  int j, jres, jmol;
  int curres, curmol; 
  int *atomMap; // Convert atom # in oldParm to newParm; -1 if atom is not in newParm

  // Allocate space for the new state
  newParm = new AmberParm(); 
  newParm->SetDebug(debug);

  // Allocate space for arrays and perform initialization
  atomMap = new int[ natom ];
  for (i=0; i<this->natom; i++) atomMap[i]=-1;
  newParm->names    = new NAME[ Nselected ];
  if (this->types!=NULL)
    newParm->types    = new NAME[ Nselected ];
  if (this->charge!=NULL)
    newParm->charge   = new double[ Nselected ];
  if (this->mass!=NULL)
    newParm->mass     = new double[ Nselected ];
  newParm->resnames = new NAME[ nres ];
  newParm->resnums  = new int[ nres+1 ];
  if (this->gb_radii!=NULL)
    newParm->gb_radii = new double[ Nselected ];
  if (this->gb_screen!=NULL) 
    newParm->gb_screen = new double[ Nselected ];
  if (this->radius_set!=NULL) {
    newParm->radius_set = new char[ strlen(this->radius_set)+1 ];
    strcpy(newParm->radius_set, this->radius_set);
  } 

  if (this->molecules>0) 
    newParm->atomsPerMol = new int[ molecules ];

  // Set first solvent molecule to -1 for now. If there are no solvent 
  // molecules left in newParm after strip it will be set to 0.
  newParm->firstSolvMol=-1;

  j = 0; 
  jres = -1; jmol = -1;
  ires = -1; imol = -1;

  // Loop over Selected atoms and set up information for the newstate if the atom is 
  // not to be deleted...
  for (selected=0; selected < Nselected; selected++) {
    // i = old atom #, j = new atom number
    i = Selected[selected];          // Atom to be kept from oldParm
    curres = this->atomToResidue(i); // Residue number of atom in oldParm
    atomMap[i]=j;                    // Store this atom in the atom map
    // Copy over atom information
    strcpy(newParm->names[j], this->names[i]);
    if (this->types!=NULL)     strcpy(newParm->types[j], this->types[i]);
    if (this->charge!=NULL)    newParm->charge[j]      = this->charge[i];
    if (this->mass!=NULL)      newParm->mass[j]        = this->mass[i];
    if (this->gb_radii!=NULL)  newParm->gb_radii[j]    = this->gb_radii[i];
    if (this->gb_screen!=NULL) newParm->gb_screen[j]   = this->gb_screen[i];

    // Check to see if we are in the same residue or not and copy relevant information
    if (ires == -1 || ires != curres) {
      jres++;
      strcpy(newParm->resnames[jres], this->resnames[curres]);
      newParm->resnums[jres] = j;
      ires = curres;
    }

    // Check to see if we are in the same molecule or not and increment #atoms in molecule
    if (this->molecules>0) {
      curmol = this->atomToMolecule(i);
      if (imol == -1 || imol != curmol) {
        jmol++;
        newParm->atomsPerMol[jmol]=1;
        imol = curmol;
      } else {
        newParm->atomsPerMol[jmol]++;
      }
    }

    // If we are keeping this atom and it belongs to a solvent molecule and 
    // the first solvent atom has not been set, set it.
    if (this->solventMolecules>0 && this->solventMask[i]=='T' && newParm->firstSolvMol<0) {
      newParm->firstSolvMol = jmol + 1;
      newParm->finalSoluteRes = jres;
    }

    // Increment the new atom counter
    j++;

  } // End loop over oldParm Selected atoms 

  // Set up bond arrays
  newParm->bondsh = SetupBondArray(atomMap, this->NbondsWithH*3, this->bondsh, 
                                   &(newParm->NbondsWithH));
  newParm->bonds  = SetupBondArray(atomMap, this->NbondsWithoutH*3, this->bonds, 
                                   &(newParm->NbondsWithoutH));
  delete[] atomMap;

  // Fix up IPRES
  newParm->resnums[jres+1] = j;

  // Set up new parm information
  newParm->natom = j;
  newParm->nres = jres+1; 
  newParm->parmFrames = this->parmFrames;
  if (this->molecules>0) 
    newParm->molecules = jmol+1;

  // Give stripped parm the same pindex as original parm
  newParm->pindex = this->pindex;
  
  // Reallocate memory 
  //if (this->types!=NULL)
  //  newParm->types=(NAME*) realloc(newParm->types, newParm->natom * sizeof(NAME));
  //if (this->charge!=NULL)
  //  newParm->charge=(double*) realloc(newParm->charge, newParm->natom * sizeof(double));
  //if (this->mass!=NULL)
  //  newParm->mass=(double*) realloc(newParm->mass, newParm->natom * sizeof(double));
  //if (this->gb_radii!=NULL)
  //  newParm->gb_radii=(double*) realloc(newParm->gb_radii, newParm->natom * sizeof(double));
  //if (this->gb_screen!=NULL)
  //  newParm->gb_screen=(double*) realloc(newParm->gb_screen, newParm->natom * sizeof(double));
  int *tempresnums = new int[ newParm->nres + 1 ];
  memcpy(tempresnums, newParm->resnums, (newParm->nres + 1) * sizeof(int));
  delete[] newParm->resnums;
  newParm->resnums = tempresnums;
  //newParm->names=(NAME*) realloc(newParm->names, newParm->natom * sizeof(NAME));
  NAME *tempresnames = new NAME[ newParm->nres ];
  memcpy(tempresnames, newParm->resnames, newParm->nres * sizeof(NAME));
  delete[] newParm->resnames;
  newParm->resnames = tempresnames;
  if (newParm->molecules>0) {
    int *tempAPM = new int[ newParm->molecules ];
    memcpy(tempAPM, newParm->atomsPerMol, newParm->molecules * sizeof(int));
    delete[] newParm->atomsPerMol;
    newParm->atomsPerMol = tempAPM;
  }

  // Set up solvent info if necessary
  if (newParm->firstSolvMol < 0) {
    // No solvent in stripped parmtop
    newParm->solventMolecules=0;
  } else {
    // Set up new solvent info based on new resnums and firstSolvMol
    if (newParm->SetSolventInfo()) {
      delete newParm;
      return NULL;
    }
  }
  
  // Copy box information
  for (i=0; i<6; i++)
    newParm->Box[i] = this->Box[i];
  newParm->boxType=this->boxType;

  return newParm;
}

// -----------------------------------------------------------------------------
// AmberParm::WriteAmberParm()
/// Write out information from current AmberParm to an Amber parm file
int AmberParm::WriteAmberParm(char *filename) {
  CpptrajFile outfile;
  CharBuffer buffer;
  int solvent_pointer[3];
  int *values;
  double parmBox[4];
  // For date and time
  time_t rawtime;
  struct tm *timeinfo;

  if (parmName==NULL) return 1;

  if ( outfile.SetupFile(filename, WRITE, AMBERPARM, STANDARD, debug) )
    return 1;

  if (outfile.OpenFile()) return 1;

  // HEADER AND TITLE (4 lines, version, flag, format, title)
  buffer.Allocate( 324 ); // (81 * 4), no space for NULL needed since using NewLine() 
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  // VERSION
  buffer.Sprintf("%-44s%02i/%02i/%02i  %02i:%02i:%02i                  \n",
                     "%VERSION  VERSION_STAMP = V0001.000  DATE = ",
                     timeinfo->tm_mon,timeinfo->tm_mday,timeinfo->tm_year%100,
                     timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec);
  // TITLE
  buffer.Sprintf("%-80s\n%-80s\n%-80s","%FLAG TITLE","%FORMAT(20a4)","");
  buffer.NewLine();
  //outfile.IO->Printf("%-80s\n",parmName);

  // POINTERS
  values = new int[ AMBERPOINTERS ];
  memset(values, 0, AMBERPOINTERS * sizeof(int));
  values[NATOM]=natom;
  values[NRES]=nres;
  values[NBONH]=NbondsWithH;
  values[MBONA]=NbondsWithoutH;
  values[IFBOX]=AmberIfbox(Box[4]);
  DataToFortranBuffer(buffer,F_POINTERS, values, NULL, NULL, AMBERPOINTERS);
  // ATOM NAMES
  DataToFortranBuffer(buffer,F_NAMES, NULL, NULL, names, natom);
  // CHARGE - might be null if read from pdb
  if (charge!=NULL) {
    // Convert charges to AMBER charge units
    double *tempCharge = new double[ natom ];
    memcpy(tempCharge, charge, natom * sizeof(double));
    for (int atom=0; atom<natom; atom++)
      tempCharge[atom] *= (ELECTOAMBER);
    DataToFortranBuffer(buffer,F_CHARGE, NULL, tempCharge, NULL, natom);
    delete[] tempCharge;
  }
  // MASS - might be null if read from pdb
  if (mass!=NULL)  
    DataToFortranBuffer(buffer,F_MASS, NULL, mass, NULL, natom);
  // RESIDUE LABEL - resnames
  DataToFortranBuffer(buffer,F_RESNAMES, NULL, NULL, resnames, nres);
  // RESIDUE POINTER - resnums, IPRES
  // Shift atom #s in resnums by +1 to be consistent with AMBER
  int *tempResnums = new int[ nres ];
  memcpy(tempResnums, resnums, nres * sizeof(int));
  for (int res=0; res < nres; res++)
    tempResnums[res] += 1;
  DataToFortranBuffer(buffer,F_RESNUMS, tempResnums, NULL, NULL, nres);
  delete[] tempResnums;
  // AMBER ATOM TYPE - might be null if read from pdb
  if (types!=NULL) 
    DataToFortranBuffer(buffer,F_TYPES, NULL, NULL, types, natom);
  // BONDS INCLUDING HYDROGEN - might be null if read from pdb
  if (bondsh != NULL) 
    DataToFortranBuffer(buffer,F_BONDSH, bondsh, NULL, NULL, NbondsWithH*3);
  // BONDS WITHOUT HYDROGEN - might be null if read from pdb
  if (bonds!=NULL) 
    DataToFortranBuffer(buffer,F_BONDS, bonds, NULL, NULL, NbondsWithoutH*3);
  // SOLVENT POINTERS
  if (values[IFBOX]>0) {
    if (firstSolvMol!=-1) {
      solvent_pointer[0]=finalSoluteRes;
      solvent_pointer[1]=molecules;
      solvent_pointer[2]=firstSolvMol;
      DataToFortranBuffer(buffer,F_SOLVENT_POINTER, solvent_pointer, NULL, NULL, 3);
    }
    // ATOMS PER MOLECULE
    if (atomsPerMol!=NULL) 
      DataToFortranBuffer(buffer,F_ATOMSPERMOL, atomsPerMol, NULL, NULL, molecules);
    // BOX DIMENSIONS
    parmBox[0] = Box[4]; // beta
    parmBox[1] = Box[0]; // boxX
    parmBox[2] = Box[1]; // boxY
    parmBox[3] = Box[2]; // boxZ
    DataToFortranBuffer(buffer,F_PARMBOX, NULL, parmBox, NULL, 4);
  }

  // Write buffer to file
  outfile.IO->Write(buffer.Buffer(), sizeof(char), buffer.CurrentSize());
  delete[] values;
  outfile.CloseFile();

  return 0;
}
