/* AmberParm.cpp
 * Class that holds parameter information. Can be read in from Amber Topology,
 * PDB, or Mol2 files (implemented in the ReadParmXXX functions). The following
 * parameters of AmberParm must always be set:
 *   The names, resnames, resnums arrays.
 *   The natom, ifbox and nres variables.
 * NOTES:
 *   Eventually make the mol2 read parm function use the AddBond function.
 */
#include <cstdlib>
#include <cstring>
#include <cstdio> // For sscanf, sprintf
#include <ctime> // for writing time/date to parmtop
#include "AmberParm.h" // PtrajFile.h
#include "FortranFormat.h" 
#include "PDBfileRoutines.h"
#include "Mol2FileRoutines.h"
#include "CpptrajStdio.h"

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
  //outFrame=0;

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
}

// DESTRUCTOR
AmberParm::~AmberParm() {
  if (parmfileName!=NULL) free(parmfileName);
  if (parmName!=NULL) free(parmName);

  if (bondsh!=NULL) free(bondsh);
  if (bonds!=NULL) free(bonds);
  if (names!=NULL) free(names);
  if (resnames!=NULL) free(resnames);
  if (types!=NULL) free(types);
  if (resnums!=NULL) free(resnums);
  if (atomsPerMol!=NULL) free(atomsPerMol);
  if (mass!=NULL) free(mass);
  if (charge!=NULL) free(charge);

  if (solventMask!=NULL) free(solventMask);
  if (solventMoleculeStart!=NULL) free(solventMoleculeStart);
  if (solventMoleculeStop!=NULL) free(solventMoleculeStop);

  if (SurfaceInfo!=NULL) free(SurfaceInfo);
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
  numBonds = (int*) malloc(natom * sizeof(int));
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
  SurfaceInfo = (SurfInfo*) malloc(numSoluteAtoms * sizeof(SurfInfo));
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
  free(numBonds);
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
       strcmp(" HOH", resnameIn) == 0    )
  {
    return true;
  }
  return false;
}

/* AmberParm::SetSolventInfo()
 * If atomsPerMol has been read in, set solvent information based on what
 * the firstSolvMol is. If atomsPerMol is not set, set solvent information by
 * residue name. 
 */
int AmberParm::SetSolventInfo() {
  int molAtom, maskAtom; 

  // Allocate memory
  // Since the number of solvent molecules is not yet known allocate
  // natom for solventMoleculeX arrays. Will be resized after.
  solventMask=(char*) malloc(natom * sizeof(char));
  for (maskAtom=0; maskAtom<natom; maskAtom++) solventMask[maskAtom]='F';
  solventMoleculeStart=(int*) malloc(natom * sizeof(int));
  solventMoleculeStop=(int*) malloc(natom * sizeof(int));
  solventMolecules=0;
  solventAtoms=0;

  // Treat all the molecules starting with firstSolvMol (nspsol) as solvent
  if (atomsPerMol!=NULL) {
    if (firstSolvMol==-1) {
      mprinterr("Error: SetSolventInfo(): atomsPerMol is set but firstSolvMol==-1!\n");
      return 1;
    }
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

  // Treat all residues named WAT/HOH as solvent.
  // Consider all residues up to the first solvent residue to be in a
  // single molecule.
  // Atom #s in resnums at this point should start from 0, not 1
  } else if (resnums!=NULL) {
    firstSolvMol=-1;
    for (int res=0; res < nres; res++) { 
      if ( IsSolventResname(resnames[res])) {
        // Add this residue to the list of solvent 
        molAtom = resnums[res+1] - resnums[res];
        solventAtoms += molAtom;
        solventMoleculeStart[solventMolecules] = resnums[res];
        solventMoleculeStop[ solventMolecules] = resnums[res+1];
        for (maskAtom=resnums[res]; maskAtom < resnums[res+1]; maskAtom++)
          solventMask[maskAtom] = 'T';
        // First time setup for atomsPerMol array
        if (firstSolvMol==-1) {
          // First residue is solvent, all is solvent.
          if (res==0) {
            finalSoluteRes=0; // Starts from 1, Amber convention
            firstSolvMol=1;   // Starts from 1, Amber convention
            molecules=0;
            atomsPerMol=NULL;
          } else {
            finalSoluteRes=res; // Starts from 1, Amber convention
            firstSolvMol=2;     // Starts from 1, Amber convention
            molecules=1;
            atomsPerMol = (int*) malloc( sizeof(int) );
            atomsPerMol[0] = resnums[res];
          }
        } 
        // Update atomsPerMol
        atomsPerMol = (int*) realloc(atomsPerMol, (molecules+1) * sizeof(int));
        atomsPerMol[molecules] = molAtom; 
        solventMolecules++;
        molecules++;
      } // END if residue is solvent
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
    free(solventMask);
    solventMask=NULL;
    free(solventMoleculeStart);
    solventMoleculeStart=NULL;
    free(solventMoleculeStop);
    solventMoleculeStop=NULL;

  // Resize the solventMoleculeX arrays
  } else {
    solventMoleculeStart = (int*) realloc(solventMoleculeStart, solventMolecules * sizeof(int));
    solventMoleculeStop  = (int*) realloc(solventMoleculeStop,  solventMolecules * sizeof(int));
  }

  return 0; 
}
    
// --------========= ROUTINES PERTAINING TO READING PARAMETERS =========--------
/* AmberParm::OpenParm()
 * Attempt to open file and read in parameters.
 */
int AmberParm::OpenParm(char *filename) {
  PtrajFile parmfile;

  if ( parmfile.SetupFile(filename,READ,UNKNOWN_FORMAT, UNKNOWN_TYPE,debug) ) 
    return 1;

  // Copy parm filename to parmName. Separate from File.filename in case of stripped parm
  parmName=(char*) malloc( (strlen(parmfile.basefilename)+1) * sizeof(char));
  strcpy(parmName,parmfile.basefilename);
  parmfileName=(char*) malloc( (strlen(filename)+1) * sizeof(char));
  strcpy(parmfileName,filename);

  if ( parmfile.OpenFile() ) return 1;

  switch (parmfile.fileFormat) {
    case AMBERPARM : if (ReadParmAmber(&parmfile)) return 1; break;
    case PDBFILE   : if (ReadParmPDB(&parmfile)  ) return 1; break;
    case MOL2FILE  : if (ReadParmMol2(&parmfile) ) return 1; break;
    default: 
      rprintf("Unknown parameter file type: %s\n",parmfile.filename);
      return 1;
  }

  parmfile.CloseFile();

  // Create a last dummy residue in resnums that holds natom, which would be
  // the atom number of the next residue if it existed. Atom #s in resnums
  // should correspond with cpptraj atom #s (start from 0) instead of Amber
  // atom #s (start from 1). 
  // Do this to be consistent with ptrajmask selection behavior - saves an 
  // if-then statement.
  resnums=(int*) realloc(resnums,(nres+1)*sizeof(int));
  resnums[nres]=natom;
  // DEBUG
  //fprintf(stdout,"==== DEBUG ==== Resnums for %s:\n",parmfile.filename);
  //for (err=0; err<nres; err++) 
  //  fprintf(stdout,"    %i: %i\n",err,resnums[err]);

  // Set up solvent information
  if (SetSolventInfo()) return 1;

  if (debug>0) {
    mprintf("  Number of atoms= %i\n",natom);
    mprintf("  Number of residues= %i\n",nres);
  }

  return 0;
}

/* AmberParm::ReadParmAmber() 
 * Read parameters from Amber Topology file
 */
int AmberParm::ReadParmAmber(PtrajFile *parmfile) {
  int err, atom;
  int *solvent_pointer;
  double *boxFromParm;
  int *values;

  if (debug>0) mprintf("Reading Amber Topology file %s\n",parmName);

  values=(int*) getFlagFileValues(parmfile,"POINTERS",AMBERPOINTERS,debug);
  if (values==NULL) {
    mprintf("Could not get values from topfile\n");
    return 1;
  }

  natom=values[NATOM];
  nres=values[NRES];
  NbondsWithH=values[NBONH];
  NbondsWithoutH=values[MBONA];
  if (debug>0) {
    mprintf("    Amber top contains %i atoms, %i residues.\n",natom,nres);
    mprintf("    %i bonds to hydrogen, %i other bonds.\n",NbondsWithH,NbondsWithoutH);
  }

  err=0;
  names=(NAME*) getFlagFileValues(parmfile,"ATOM_NAME",natom,debug);
  if (names==NULL) {mprintf("Error in atom names.\n"); err++;}
  types=(NAME*) getFlagFileValues(parmfile,"AMBER_ATOM_TYPE",natom,debug);
  if (types==NULL) {mprintf("Error in atom types.\n"); err++;}
  resnames=(NAME*) getFlagFileValues(parmfile,"RESIDUE_LABEL",nres,debug);
  if (resnames==NULL) {mprintf("Error in residue names.\n"); err++;}
  resnums=(int*) getFlagFileValues(parmfile,"RESIDUE_POINTER",nres,debug);
  if (resnums==NULL) {mprintf("Error in residue numbers.\n"); err++;}
  // Atom #s in resnums are currently shifted +1. Shift back to be consistent
  // with the rest of cpptraj.
  for (atom=0; atom < nres; atom++)
    resnums[atom] -= 1;
  mass=(double*) getFlagFileValues(parmfile,"MASS",natom,debug);
  if (mass==NULL) {mprintf("Error in masses.\n"); err++;}
  charge=(double*) getFlagFileValues(parmfile,"CHARGE",natom,debug);
  if (charge==NULL) {mprintf("Error in charges.\n"); err++;}
  // Convert charges to units of electron charge
  for (atom=0; atom < natom; atom++)
    charge[atom] *= (AMBERTOELEC);
  bonds=(int*) getFlagFileValues(parmfile,"BONDS_WITHOUT_HYDROGEN",NbondsWithoutH*3,debug);
  if (bonds==NULL) {mprintf("Error in bonds w/o H.\n"); err++;}
  bondsh=(int*) getFlagFileValues(parmfile,"BONDS_INC_HYDROGEN",NbondsWithH*3,debug);
  if (bondsh==NULL) {mprintf("Error in bonds inc H.\n"); err++;}
  // Get solvent info if IFBOX>0
  if (values[IFBOX]>0) {
    solvent_pointer=(int*) getFlagFileValues(parmfile,"SOLVENT_POINTERS",3,debug);
    if (solvent_pointer==NULL) {
      mprintf("Error in solvent pointers.\n"); 
      err++;
    } else {
      finalSoluteRes=solvent_pointer[0];
      molecules=solvent_pointer[1];
      firstSolvMol=solvent_pointer[2];
      free(solvent_pointer);
    }
    atomsPerMol=(int*) getFlagFileValues(parmfile,"ATOMS_PER_MOLECULE",molecules,debug);
    if (atomsPerMol==NULL) {mprintf("Error in atoms per molecule.\n"); err++;}
    // boxFromParm = {OLDBETA, BOX(1), BOX(2), BOX(3)}
    boxFromParm=(double*) getFlagFileValues(parmfile,"BOX_DIMENSIONS",4,debug);
    if (boxFromParm==NULL) {mprintf("Error in Box information.\n"); err++;}
    // Determine box type. Set Box angles and lengths from beta (boxFromParm[0])
    boxType = SetBoxInfo(boxFromParm,Box,debug);
    free(boxFromParm);
    if (debug>0) {
      mprintf("    %s contains box info: %i mols, first solvent mol is %i\n",
              parmName, molecules, firstSolvMol);
      mprintf("    BOX: %lf %lf %lf | %lf %lf %lf\n",Box[0],Box[1],Box[2],Box[3],Box[4],Box[5]);
      if (boxType==ORTHO)
        mprintf("         Box is orthogonal.\n");
      else
        mprintf("         Box is non-orthogonal.\n");
    }
  }

  free(values);

  if ( err>0 ) {
    rprintf("Error reading topfile\n");
    return 1;
  }

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
  if (nres>0 && IsSolventResname(resnames[nres-1])) {
    if (firstSolvMol==-1) {
      firstSolvMol = molecules + 1; // +1 to be consistent w/ Amber top
      finalSoluteRes = nres - 1;    // +1 to be consistent w/ Amber top
    }
  }
  atomsPerMol = (int*) realloc(atomsPerMol, (molecules+1) * sizeof(int) );
  atomsPerMol[molecules] = numAtoms;
  molecules++;
  return natom;
}

/* AmberParm::ReadParmPDB()
 * Open the PDB file specified by filename and set up topology data.
 * Mask selection requires natom, nres, names, resnames, resnums.
 */
int AmberParm::ReadParmPDB(PtrajFile *parmfile) {
  char buffer[256];
  int bufferLen;  
  int currResnum;
  int atom;
  int atomInLastMol = 0;

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
      names=(NAME*) realloc(names, (natom+1) * sizeof(NAME));
      // Leading whitespace will automatically be trimmed.
      // Name will be wrapped if it starts with a digit.
      // Asterisks will be replaced with prime char
      pdb_name(buffer, (char*)names[natom]);

      // If this residue number is different than the last, allocate mem for new res
      if (currResnum!=pdb_resnum(buffer)) {
        resnames=(NAME*) realloc(resnames, (nres+1) * sizeof(NAME));
        // Leading whitespace will automatically be trimmed.
        // Asterisks will be replaced with prime char
        pdb_resname(buffer, (char*)resnames[nres]);
        if (debug>3) mprintf("        PDBRes %i [%s]\n",nres,resnames[nres]);
        resnums=(int*) realloc(resnums, (nres+1) * sizeof(int));
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
      mprintf("PDB: firstSolvMol= %i\n",firstSolvMol);
      mprintf("PDB: finalSoluteRes= %i\n",finalSoluteRes);
      if (debug>1) {
        mprintf("PDB: Atoms Per Molecule:\n");
        for (atom=0; atom < molecules; atom++) {
          mprintf("%8i %8i\n",atom,atomsPerMol[atom]);
        } 
      }
    }
  }

  // No box for PDB - maybe change later to include unit cell info?
  boxType = NOBOX;

  if (debug>0) 
    mprintf("    PDB contains %i atoms, %i residues, %i molecules.\n",
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
int AmberParm::ReadParmMol2(PtrajFile *parmfile) {
  char buffer[MOL2BUFFERSIZE];
  int mol2bonds, atom;
  int resnum, currentResnum;
  int numbonds3, numbondsh3;
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
  names = (NAME*) malloc( natom * sizeof(NAME));
  types = (NAME*) malloc( natom * sizeof(NAME));
  charge = (double*) malloc( natom * sizeof(double));

  // Get @<TRIPOS>ATOM information
  if (Mol2ScanTo(parmfile, ATOM)) return 1;
  for (atom=0; atom < natom; atom++) {
    if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
    // atom_id atom_name x y z atom_type [subst_id [subst_name [charge [status_bit]]]]
    //sscanf(buffer,"%*i %s %*f %*f %*f %s %i %s %lf", names[atom], types[atom],
    //       &resnum,resName, charge+atom);
    //mprintf("      %i %s %s %i %s %lf\n",atom,names[atom],types[atom],resnum,resName,charge[atom]);
    Mol2AtomName(buffer,names[atom]);
    Mol2AtomType(buffer,types[atom]);
    Mol2ResNumName(buffer,&resnum,resName);
    charge[atom]=Mol2Charge(buffer);
    // Check if residue number has changed - if so record it
    if (resnum != currentResnum) {
      resnames = (NAME*) realloc(resnames, (nres+1) * sizeof(NAME));
      strcpy(resnames[nres], resName);
      resnums=(int*) realloc(resnums, (nres+1) * sizeof(int));
      resnums[nres]=atom; 
      currentResnum = resnum;
      nres++;
    }
  }

  // Get @<TRIPOS>BOND information [optional]
  NbondsWithoutH=0;
  NbondsWithH=0;
  numbonds3=0;
  numbondsh3=0;
  if (Mol2ScanTo(parmfile, BOND)==0) {
    for (atom=0; atom < mol2bonds; atom++) {
      if ( parmfile->IO->Gets(buffer,MOL2BUFFERSIZE) ) return 1;
      // bond_id origin_atom_id target_atom_id bond_type [status_bits]
      //         resnum         currentResnum
      sscanf(buffer,"%*i %i %i\n",&resnum,&currentResnum);
      // mol2 atom #s start from 1
      if ( names[resnum-1][0]=='H' || names[currentResnum-1][0]=='H' ) {
        bondsh = (int*) realloc(bondsh, (NbondsWithH+1)*3*sizeof(int));
        bondsh[numbondsh3  ]=(resnum-1)*3;
        bondsh[numbondsh3+1]=(currentResnum-1)*3;
        bondsh[numbondsh3+2]=0; // Need to assign some force constant eventually
        //mprintf("      Bond to Hydrogen %s-%s %i-%i\n",names[resnum-1],names[currentResnum-1],
        //        resnum, currentResnum);
        NbondsWithH++;
        numbondsh3+=3;
      } else {
        bonds = (int*) realloc(bonds, (NbondsWithoutH+1)*3*sizeof(int));
        bonds[numbonds3  ]=(resnum-1)*3;
        bonds[numbonds3+1]=(currentResnum-1)*3;
        bonds[numbonds3+2]=0; // Need to assign some force constant eventually
        //mprintf("      Bond %s-%s %i-%i\n",names[resnum-1],names[currentResnum-1],
        //        resnum, currentResnum);
        NbondsWithoutH++;
        numbonds3+=3;
      }
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

// -----------------------------------------------------------------------------
/* AmberParm::AtomInfo()
 * Print parm information for atom.
 */
void AmberParm::AtomInfo(int atom) {
  int res = atomToResidue(atom);
  mprintf("Atom %i:%4s Res %i:%4s Mol %i",atom+1,names[atom],res+1,resnames[res],
          atomToMolecule(atom)+1);
  if (types!=NULL)
    mprintf(" Type=%4s",types[atom]);
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
  if (this->molecules>0)
    mprintf("                                %i molecules.\n",this->molecules);
  if (this->solventMolecules>0) {
    mprintf("                                %i solvent molecules.\n",this->solventMolecules);
    mprintf("                  First solvent molecule is %i\n",this->firstSolvMol);
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
  int i, a, atom1;

  atom1 = atom + 1; // Since in atomsPerMol numbers start from 1
  a = 0;
  for (i = 0; i < molecules; i++) {
    a += atomsPerMol[i];
    if (atom1 <= a)
      return i;
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
  if (bonds!=NULL) free(bonds);
  bonds=NULL;
  if (bondsh!=NULL) free(bondsh);
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
  if (atom1<0 || atom2<0 || atom1>=natom || atom2>=natom) return 1;
  if (names!=NULL) {
    if (names[atom1][0]=='H') isH=true;
    if (names[atom2][0]=='H') isH=true;
  }
  if (isH) {
    //NbondsWithH = values[NBONH];
    bondsh = (int*) realloc(bondsh, ((NbondsWithH+1)*3) * sizeof(int));
    bondsh[NbondsWithH  ] = atom1 * 3;
    bondsh[NbondsWithH+1] = atom2 * 3;
    bondsh[NbondsWithH+2] = icb;
    NbondsWithH++;
  } else {
    //NbondsWithoutH = values[MBONA];
    bonds = (int*) realloc(bonds, ((NbondsWithoutH+1)*3) * sizeof(int));
    bonds[NbondsWithoutH  ] = atom1 * 3;
    bonds[NbondsWithoutH+1] = atom2 * 3;
    bonds[NbondsWithoutH+2] = icb;
    NbondsWithoutH++;
  }
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
      bonds=(int*) realloc(bonds, (N3+3) * sizeof(int)); 
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
  newParm->names    = (NAME*)   malloc( this->natom   * sizeof(NAME) );
  if (this->types!=NULL)
    newParm->types    = (NAME*)   malloc( this->natom   * sizeof(NAME) );
  if (this->charge!=NULL)
    newParm->charge   = (double*) malloc( this->natom   * sizeof(double));
  if (this->mass!=NULL)
    newParm->mass     = (double*) malloc( this->natom   * sizeof(double));
  newParm->resnames = (NAME*)   malloc( this->nres    * sizeof(NAME) );
  newParm->resnums  = (int*)    malloc((this->nres+1) * sizeof(int   ));
  // Need reverse of AMap, Map[tgt atom] = ref atom for setting up bonds
  ReverseMap = (int*) malloc(this->natom * sizeof(int));

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
  free(ReverseMap); 

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
  atomMap = (int*) malloc( this->natom * sizeof(int));
  for (i=0; i<this->natom; i++) atomMap[i]=-1;
  newParm->names    = (NAME*)   malloc( this->natom   * sizeof(NAME) );
  if (this->types!=NULL)
    newParm->types    = (NAME*)   malloc( this->natom   * sizeof(NAME) );
  if (this->charge!=NULL)
    newParm->charge   = (double*) malloc( this->natom   * sizeof(double));
  if (this->mass!=NULL)
    newParm->mass     = (double*) malloc( this->natom   * sizeof(double));
  newParm->resnames = (NAME*)   malloc( this->nres    * sizeof(NAME) );
  newParm->resnums  = (int*)    malloc((this->nres+1) * sizeof(int   ));

  if (this->molecules>0) 
    newParm->atomsPerMol = (int*) malloc(this->molecules * sizeof(int));

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
    if (this->types!=NULL)  strcpy(newParm->types[j], this->types[i]);
    if (this->charge!=NULL) newParm->charge[j]      = this->charge[i];
    if (this->mass!=NULL)   newParm->mass[j]        = this->mass[i];

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
  free(atomMap);

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
  if (this->types!=NULL)
    newParm->types=(NAME*) realloc(newParm->types, newParm->natom * sizeof(NAME));
  if (this->charge!=NULL)
    newParm->charge=(double*) realloc(newParm->charge, newParm->natom * sizeof(double));
  if (this->mass!=NULL)
    newParm->mass=(double*) realloc(newParm->mass, newParm->natom * sizeof(double));
  newParm->resnums=(int*) realloc(newParm->resnums, (newParm->nres+1) * sizeof(int));
  newParm->names=(NAME*) realloc(newParm->names, newParm->natom * sizeof(NAME));
  newParm->resnames=(NAME*) realloc(newParm->resnames, (newParm->nres+1) * sizeof(NAME));
  if (newParm->molecules>0)
    newParm->atomsPerMol=(int*) realloc(newParm->atomsPerMol, newParm->molecules * sizeof(int));

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
/* AmberParm::WriteAmberParm()
 * Write out information from current AmberParm to an Amber parm file
 */
int AmberParm::WriteAmberParm(char *filename) {
  PtrajFile outfile;
  char *buffer,*filebuffer;
  int solvent_pointer[3];
  int atom;
  int *values;
  double parmBox[4];
  // For date and time
  time_t rawtime;
  struct tm *timeinfo;
  size_t BufferSize;

  if (parmName==NULL) return 1;

  if ( outfile.SetupFile(filename, WRITE, AMBERPARM, STANDARD, debug) )
    return 1;

  filebuffer=NULL;
  if (outfile.OpenFile()) return 1;

  // HEADER AND TITLE
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  outfile.IO->Printf("%-44s%02i/%02i/%02i  %02i:%02i:%02i                  \n",
                     "%VERSION  VERSION_STAMP = V0001.000  DATE = ",
                     timeinfo->tm_mon,timeinfo->tm_mday,timeinfo->tm_year%100,
                     timeinfo->tm_hour,timeinfo->tm_min,timeinfo->tm_sec);
  outfile.IO->Printf("%-80s\n%-80s\n%-80s\n","%FLAG TITLE","%FORMAT(20a4)","");
  //outfile.IO->Printf("%-80s\n",parmName);

  // Calculate necessary buffer size
  BufferSize=0;
  BufferSize += (GetFortranBufferSize(F10I8,AMBERPOINTERS,0)+FFSIZE); // POINTERS
  BufferSize += (GetFortranBufferSize(F20a4,natom,0)+FFSIZE); // ATOM_NAME 
  if (charge!=NULL) BufferSize += (GetFortranBufferSize(F5E16_8,natom,0)+FFSIZE); // CHARGE
  if (mass!=NULL) BufferSize += (GetFortranBufferSize(F5E16_8,natom,0)+FFSIZE); // MASS
  BufferSize += (GetFortranBufferSize(F20a4,nres,0)+FFSIZE); // RESIDUE_LABEL
  BufferSize += (GetFortranBufferSize(F10I8,nres,0)+FFSIZE); // RESIDUE_POINTER
  if (types!=NULL) BufferSize += (GetFortranBufferSize(F20a4,natom,0)+FFSIZE); // ATOM_TYPE
  if (bondsh!=NULL) BufferSize += (GetFortranBufferSize(F10I8,NbondsWithH*3,0)+FFSIZE); // BONDSH
  if (bonds!=NULL) BufferSize += (GetFortranBufferSize(F10I8,NbondsWithoutH*3,0)+FFSIZE); // BONDS
  if (AmberIfbox(Box[4])>0) {
    if (firstSolvMol!=-1)
      BufferSize += (GetFortranBufferSize(F3I8,3,0)+FFSIZE); // SOLVENT_POINTER
    if (atomsPerMol!=NULL)
      BufferSize += (GetFortranBufferSize(F10I8,molecules,0)+FFSIZE); // ATOMSPERMOL
    BufferSize += (GetFortranBufferSize(F5E16_8,4,0)+FFSIZE); // BOX
  }
  // 1 extra char for NULL
  filebuffer = (char*) malloc( (BufferSize+1) * sizeof(char));
  mprintf("DEBUG: Parm %s: Buffer size is %lu bytes.\n",filename,BufferSize);
  if (filebuffer==NULL) {
    mprinterr("Error: Could not allocate memory to write Amber topology %s\n",filename);
    return 1;
  }
  buffer = filebuffer;

  // POINTERS
  values = (int*) calloc( AMBERPOINTERS, sizeof(int));
  values[NATOM]=natom;
  values[NRES]=nres;
  values[NBONH]=NbondsWithH;
  values[MBONA]=NbondsWithoutH;
  values[IFBOX]=AmberIfbox(Box[4]);
  buffer = DataToFortranBuffer(buffer,"%FLAG POINTERS", F10I8, values, NULL, NULL, AMBERPOINTERS);

  // ATOM NAMES
  buffer = DataToFortranBuffer(buffer,"%FLAG ATOM_NAME", F20a4, NULL, NULL, names, natom);

  // CHARGE - might be null if read from pdb
  if (charge!=NULL) {
    // Convert charges to AMBER charge units
    for (atom=0; atom<natom; atom++)
      charge[atom] *= (ELECTOAMBER);
    buffer = DataToFortranBuffer(buffer,"%FLAG CHARGE",F5E16_8, NULL, charge, NULL, natom);
  }

  // MASS - might be null if read from pdb
  if (mass!=NULL) { 
    buffer = DataToFortranBuffer(buffer,"%FLAG MASS",F5E16_8, NULL, mass, NULL, natom);
  }

  // RESIDUE LABEL - resnames
  buffer = DataToFortranBuffer(buffer,"%FLAG RESIDUE_LABEL",F20a4, NULL, NULL, resnames, nres);

  // RESIDUE POINTER - resnums, IPRES
  // Shift atom #s in resnums by 1 to be consistent with AMBER
  for (atom=0; atom < nres; atom++)
    resnums[atom] += 1;
  buffer = DataToFortranBuffer(buffer,"%FLAG RESIDUE_POINTER",F10I8, resnums, NULL, NULL, nres);
  // Now shift them back
  for (atom=0; atom < nres; atom++)
    resnums[atom] -= 1;

  // AMBER ATOM TYPE - might be null if read from pdb
  if (types!=NULL) {
    buffer = DataToFortranBuffer(buffer,"%FLAG AMBER_ATOM_TYPE",F20a4, NULL, NULL, types, natom);
  }

  // BONDS INCLUDING HYDROGEN - might be null if read from pdb
  if (bondsh != NULL) {
    buffer = DataToFortranBuffer(buffer,"%FLAG BONDS_INC_HYDROGEN",F10I8, bondsh, 
                                 NULL, NULL, NbondsWithH*3);
  }

  // BONDS WITHOUT HYDROGEN - might be null if read from pdb
  if (bonds!=NULL) {
    buffer = DataToFortranBuffer(buffer,"%FLAG BONDS_WITHOUT_HYDROGEN",F10I8, bonds, 
                          NULL, NULL, NbondsWithoutH*3);
  }

  // SOLVENT POINTERS
  if (values[IFBOX]>0) {
    if (firstSolvMol!=-1) {
      solvent_pointer[0]=finalSoluteRes;
      solvent_pointer[1]=molecules;
      solvent_pointer[2]=firstSolvMol;
      buffer = DataToFortranBuffer(buffer,"%FLAG SOLVENT_POINTERS",F3I8, solvent_pointer, 
                                   NULL, NULL, 3);
    }

    // ATOMS PER MOLECULE
    if (atomsPerMol!=NULL) {
      buffer = DataToFortranBuffer(buffer,"%FLAG ATOMS_PER_MOLECULE",F10I8, atomsPerMol, 
                                   NULL, NULL, molecules);
    }

    // BOX DIMENSIONS
    parmBox[0] = Box[4]; // beta
    parmBox[1] = Box[0]; // boxX
    parmBox[2] = Box[1]; // boxY
    parmBox[3] = Box[2]; // boxZ
    buffer = DataToFortranBuffer(buffer,"%FLAG BOX_DIMENSIONS",F5E16_8, NULL, 
                                 parmBox, NULL, 4);
  }

  // Write buffer to file
  outfile.IO->Write(filebuffer, sizeof(char), BufferSize);
  free(filebuffer);
  free(values);
  outfile.CloseFile();

  return 0;
}
