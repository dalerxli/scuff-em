/* Copyright (C) 2005-2011 M. T. Homer Reid
 *
 * This file is part of SCUFF-EM.
 *
 * SCUFF-EM is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * SCUFF-EM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * AnalyticalDGFs.cc -- calculation of dyadic Green's functions 
 *                   -- in some analytically tractable situations
 *
 * Homer Reid 7/2015
 */
#include <stdio.h>
#include <stdlib.h>

#include <libhrutil.h>
#include <libIncField.h>
#include <libhmat.h>
#include <libSGJC.h>
#include <libscuff.h>

#define II cdouble (0,1)

using namespace scuff;

/***************************************************************/
/* general-purpose routine for evaluating lattice sums         */
/***************************************************************/
typedef void (*SummandFunction)(double *U, void *UserData, double *Sum);
int GetLatticeSum(SummandFunction Summand, void *UserData, int nSum,
                  HMatrix *LBasis, double *Sum,
                  double AbsTol, double RelTol, int MaxCells);

/***************************************************************/
/* the specific summand function passed to GetLatticeSum()     */
/***************************************************************/
typedef struct SummandData
 {
   double *X;         // evaluation point
   double *XP;        // source point
   cdouble Omega;     // angular frequency
   double *kBloch;    // point in Brillouin zone
   cdouble Epsilon;   // half-space permittivity
 } SummandData;

void MySummand(double *Gamma, void *UserData, double *Sum)
{
  /***************************************************************/
  /* unpack fields from input data structure *********************/
  /***************************************************************/
  SummandData *Data = (SummandData *)UserData;
  double *X         = Data->X;
  double *XP        = Data->XP;
  cdouble k0        = Data->Omega;
  double *kBloch    = Data->kBloch;
  cdouble Epsilon   = Data->Epsilon;
  
  double kx    = kBloch[0] + Gamma[0];
  double ky    = kBloch[1] + Gamma[1];
  if (kx==0.0 && ky==0.0)
   { kx=0.0001;
     ky=0.0001;
   };
  double kMag2 = kx*kx + ky*ky;
  double kMag  = sqrt(kMag2);
  cdouble k02  = k0*k0;
  cdouble kz   = sqrt(k02 - kMag2);
 
  // i'm not sure what to do about this ...
  if (kz==0.0) kz=1.0e-4;

  /***************************************************************/
  /* compute the 3x3 dyadic green's function.                    */
  /*                                                             */
  /* if Epsilon==0 this is the free-space (vacuum) DGF between   */
  /* points X, XP.                                               */
  /*                                                             */
  /* otherwise this is the scattering part of the DGF at a point */
  /* above an infinite half space with permittivity Epsilon;     */
  /* the height of the point is XP[2], while X[0..2] and XP[0..1]*/
  /* are not referenced.                                         */
  /***************************************************************/
  double R[3], ESign, CSign;
  cdouble rTE, rTM;
#if 0
  if (Epsilon==0.0)
   { 
     R[0]=X[0]-XP[0];
     R[1]=X[1]-XP[1];
     R[2]=X[2]-XP[2];
     ESign = CSign = (R[2] >= 0.0) ? 1.0 : -1.0;
     rTE = rTM = 1.0;
   }
  else
   { 
#endif
     R[0]=0.0;
     R[1]=0.0;
     R[2]=2.0*XP[2];
     ESign = 1.0;
     CSign = -1.0;
     if (Epsilon==0.0)
      { rTE=-1.0;
        rTM=+1.0;
      }
     else     
      { cdouble kzPrime = sqrt(Epsilon*k0*k0 - kMag2);
        cdouble ekz     = Epsilon*kz;
        rTE   = (kz  - kzPrime) / (kz+kzPrime);
        rTM   = (ekz - kzPrime) / (ekz+kzPrime);
      };
#if 0
   };
#endif

  // polarization vectors
  cdouble P[3], PBar[3];
  P[0] = -ky/kMag;
  P[1] = kx/kMag;
  P[2] = 0.0;
  PBar[0] = -1.0*ESign*kx*kz/(k0*kMag);
  PBar[1] = -1.0*ESign*ky*kz/(k0*kMag);
  PBar[2] = kMag / k0;

  // weighting coefficients for the various possible
  // orientations of the point source
  cdouble CTE[3], CTM[3];
  CTE[0] = -II*M_PI*k02*ky/(2.0*kMag*kz);
  CTE[1] =  II*M_PI*k02*kx/(2.0*kMag*kz);
  CTE[2] = 0.0;
  CTM[0] = CSign*II*k0*M_PI*kx/(2.0*kMag);
  CTM[1] = CSign*II*k0*M_PI*ky/(2.0*kMag);
  CTM[2] = II*k0*M_PI*kMag/(2.0*kz);

  cdouble ExpFac = exp( II*(kx*R[0] + ky*R[1] + kz*fabs(R[2]) ) );

  cdouble GE[3][3], GM[3][3];
  for(int Mu=0; Mu<3; Mu++)
   for(int Nu=0; Nu<3; Nu++)
    { GE[Mu][Nu] = (rTE*CTE[Nu]*P[Mu]    - rTM*CTM[Nu]*PBar[Mu])*ExpFac;
      GM[Mu][Nu] = (rTE*CTE[Nu]*PBar[Mu] + rTM*CTM[Nu]*P[Mu])*ExpFac;
    };

  /***************************************************************/
  /* add the components to the summand vector ********************/
  /***************************************************************/
  cdouble *zSum=(cdouble *)Sum;
  for(int Mu=0; Mu<3; Mu++)
   for(int Nu=0; Nu<3; Nu++)
    { zSum[ 0 + 3*Mu + Nu ] += GE[Mu][Nu];
      zSum[ 9 + 3*Mu + Nu ] += GM[Mu][Nu];
    };

}

/***************************************************************/
/***************************************************************/
/***************************************************************/
double GetRLBasis(HMatrix *HMLBasis, HMatrix *RLBasis)
{
  double LBasis[2][2];
  LBasis[0][0]=HMLBasis->GetEntryD(0,0);
  LBasis[0][1]=HMLBasis->GetEntryD(1,0);
  LBasis[1][0]=HMLBasis->GetEntryD(0,1);
  LBasis[1][1]=HMLBasis->GetEntryD(1,1);

  double Area= LBasis[0][0]*LBasis[1][1] - LBasis[0][1]*LBasis[1][0];
  if (Area==0.0)
   ErrExit("%s:%i: lattice has empty unit cell",__FILE__,__LINE__);
  RLBasis->SetEntry(0,0,  2.0*M_PI*LBasis[1][1] / Area );
  RLBasis->SetEntry(1,0, -2.0*M_PI*LBasis[1][0] / Area );
  RLBasis->SetEntry(0,1, -2.0*M_PI*LBasis[0][1] / Area );
  RLBasis->SetEntry(1,1,  2.0*M_PI*LBasis[0][0] / Area );

  return 4.0*M_PI*M_PI / Area;
}

/***************************************************************/
/* Vacuum DGFs computed by the plane-wave decomposition        */
/***************************************************************/
int GetVacuumDGFs(double X[3], double XP[3],
                  cdouble Omega, double kBloch[2],
                  HMatrix *LBasis,
                  double RelTol, double AbsTol, int MaxCells,
                  cdouble GE[3][3], cdouble GH[3][3])
{ 
  double RLBBuffer[9];
  HMatrix RLBasis(3,3,LHM_REAL,LHM_NORMAL,RLBBuffer);
  double BZVolume=GetRLBasis(LBasis, &RLBasis);
 
  SummandData MySummandData, *Data=&MySummandData;
  Data->X       = X;
  Data->XP      = XP;
  Data->Omega   = Omega;
  Data->kBloch  = kBloch;
  Data->Epsilon = 0.0;

  cdouble Sum[18];
  int NumCells=GetLatticeSum(MySummand, (void *)Data, 36, &RLBasis,
                            (double *)Sum, AbsTol, RelTol, MaxCells);

  for(int Mu=0; Mu<3; Mu++)
   for(int Nu=0; Nu<3; Nu++)
    { GE[Mu][Nu] = BZVolume*Sum[0 + 3*Mu + Nu]/(4.0*M_PI*M_PI*M_PI);
      GH[Mu][Nu] = BZVolume*Sum[9 + 3*Mu + Nu]/(4.0*M_PI*M_PI*M_PI);
    };

  return NumCells;
}

/***************************************************************/
/* Ground-plane DGFs computed by the image-source method       */
/***************************************************************/
void GetGroundPlaneDGFs(double *X, cdouble Omega, double *kBloch,
                        HMatrix *LBasis,
                        cdouble GE[3][3], cdouble GM[3][3])
{
  /***************************************************************/
  /* create an IncidentField structure describing the field of a */
  /* point source or a periodic array of point sources at the    */
  /* location of the image of X                                  */
  /***************************************************************/
  double XBar[3];
  XBar[0] =  X[0];
  XBar[1] =  X[1];
  XBar[2] = -X[2];
  cdouble P[3]={0.0,0.0,0.0};
  PointSource PS(XBar, P);
  PS.SetFrequency(Omega);
  int LDim=LBasis->NC;
  if (LDim>0)
   { double LBV[2][2];
     if (LDim>=1)
      { LBV[0][0]=LBasis->GetEntryD(0,0);
        LBV[0][1]=LBasis->GetEntryD(1,0);
      };
     if (LDim==2)
      { LBV[1][0]=LBasis->GetEntryD(0,1);
        LBV[1][1]=LBasis->GetEntryD(1,1);
      };
     PS.SetLattice(LDim,LBV);
     PS.SetkBloch(kBloch);
   };

  /***************************************************************/
  /* columns of DGF are fields of image source                  **/
  /***************************************************************/
  for(int Nu=0; Nu<3; Nu++)
   { 
     cdouble EH[6];

     memset(P, 0, 3*sizeof(cdouble));
     P[Nu] = (Nu==2) ? 1.0 : -1.0;
     PS.SetP(P);
     PS.SetType(LIF_ELECTRIC_DIPOLE);
     PS.GetFields(X, EH);
     for(int Mu=0; Mu<3; Mu++)
      GE[Mu][Nu] = EH[Mu] / (Omega*Omega);

     P[Nu] *= -1.0;
     PS.SetP(P);
     PS.SetType(LIF_MAGNETIC_DIPOLE);
     PS.GetFields(X, EH);
     for(int Mu=0; Mu<3; Mu++)
      GM[Mu][Nu] = EH[3+Mu] / (Omega*Omega);
   };

}

/***************************************************************/
/* Half-space DGFs computed by the plane-wave decomposition    */
/***************************************************************/
int GetHalfSpaceDGFs(cdouble Omega, double kBloch[2], double zp,
                     HMatrix *LBasis, MatProp *MP,
                     double RelTol, double AbsTol, int MaxCells,
                     cdouble GE[3][3], cdouble GM[3][3])
{ 
  double X[3]={0.0, 0.0, 0.0};
  double XP[3]={0.0, 0.0, 0.0};
  XP[2]=zp;

  SummandData MySummandData, *Data=&MySummandData;
  Data->X       = X;
  Data->XP      = XP;
  Data->Omega   = Omega;
  Data->kBloch  = kBloch;
  Data->Epsilon = MP->IsPEC() ? 0.0 : MP->GetEps(Omega);

  double Buffer[9];
  HMatrix RLBasis(3,3,LHM_REAL,LHM_NORMAL,Buffer);
  double BZVolume=GetRLBasis(LBasis, &RLBasis);

  cdouble Sum[18];
  GetLatticeSum(MySummand, (void *)Data, 36, &RLBasis,
                (double *)Sum, AbsTol, RelTol, MaxCells);

  for(int Mu=0; Mu<3; Mu++)
   for(int Nu=0; Nu<3; Nu++)
    { GE[Mu][Nu] = BZVolume*Sum[0 + 3*Mu + Nu]/(4.0*M_PI*M_PI*M_PI);
      GM[Mu][Nu] = BZVolume*Sum[9 + 3*Mu + Nu]/(4.0*M_PI*M_PI*M_PI);
    };
  return 0;
}

/***************************************************************/
/* compute the dyadic green's function at a distance Z above a */
/* PEC plate using the method of images.                       */
/***************************************************************/
void GetGroundPlaneDGFs(double Z, cdouble Omega, double *kBloch,
                        HMatrix *LBasis, cdouble GE[3][3], cdouble GM[3][3])
{
  // construct a point source at the image location
  double X0[3]={0.0, 0.0, -Z};
  cdouble P0[3] = {0,0,0}; 
  PointSource MyPSD(X0, P0);
  MyPSD.Omega=Omega;
  MyPSD.Eps=1.0;
  MyPSD.Mu=1.0;
#if 0
  if (LBasis)
   { MyPSD.SetLattice(LBasis);
     MyPSD.SetkBloch(kBloch);
   };
#endif
  int LDim=LBasis->NC;
  if (LDim>0)
   { double LBV[2][2];
     if (LDim>=1)
      { LBV[0][0]=LBasis->GetEntryD(0,0);
        LBV[0][1]=LBasis->GetEntryD(1,0);
      };
     if (LDim==2)
      { LBV[1][0]=LBasis->GetEntryD(0,1);
        LBV[1][1]=LBasis->GetEntryD(1,1);
      };
     MyPSD.SetLattice(LDim,LBV);
     MyPSD.SetkBloch(kBloch);
   };

  // get each column of the DGF
  cdouble EH[6];
  double X[3]={0.0, 0.0, Z}; 

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  MyPSD.Type=LIF_ELECTRIC_DIPOLE;
  MyPSD.P[0] = -1.0;   MyPSD.P[1] =  0.0; MyPSD.P[2] = 0.0;
  MyPSD.GetFields(X, EH);
  GE[0][0]=EH[0]; GE[1][0]=EH[1]; GE[2][0]=EH[2];

  MyPSD.P[0] =  0.0;   MyPSD.P[1] = -1.0; MyPSD.P[2] = 0.0;
  MyPSD.GetFields(X, EH);
  GE[0][1]=EH[0]; GE[1][1]=EH[1]; GE[2][1]=EH[2];
  
  MyPSD.P[0] =  0.0;   MyPSD.P[1] =  0.0; MyPSD.P[2] = 1.0;
  MyPSD.GetFields(X, EH);
  GE[0][2]=EH[0]; GE[1][2]=EH[1]; GE[2][2]=EH[2];

  /***************************************************************/
  /***************************************************************/
  /***************************************************************/
  MyPSD.Type=LIF_MAGNETIC_DIPOLE;
  MyPSD.P[0] =  1.0;   MyPSD.P[1] =  0.0; MyPSD.P[2] =  0.0;
  MyPSD.GetFields(X, EH);
  GM[0][0]=EH[3]; GM[1][0]=EH[4]; GM[2][0]=EH[5];

  MyPSD.P[0] =  0.0;   MyPSD.P[1] =  1.0; MyPSD.P[2] =  0.0;
  MyPSD.GetFields(X, EH);
  GM[0][1]=EH[3]; GM[1][1]=EH[4]; GM[2][1]=EH[5];
  
  MyPSD.P[0] =  0.0;   MyPSD.P[1] =  0.0; MyPSD.P[2] = -1.0;
  MyPSD.GetFields(X, EH);
  GM[0][2]=EH[3]; GM[1][2]=EH[4]; GM[2][2]=EH[5];

  // normalization factor needed to convert from 
  // my normalization of the DGF, which has units
  // of electric field / surface current, to the 
  // usual normalization in which the DGF has units 
  // of inverse length 
  cdouble ik2=(II*Omega)*(II*Omega);
  for(int i=0; i<3; i++)
   for(int j=0; j<3; j++)
    { GE[i][j] /= ik2;
      GM[i][j] /= ik2;
    };
}
