/*
 * LFunctions.cc -- routines for evaluating L-functions
 *               -- between RWG basis functions 
 * 
 * homer reid    -- 11/2005 -- 2/2009
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <libhrutil.h>
#include <libTriInt.h>

#include "libscuff.h"
#include "TaylorMaster.h"

#define II cdouble(0,1)

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
int Diagnostics;
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*- PART 0: Routines to create and destroy an (opaque pointer   */
/*-         to an) LFWorkspace structure                        */
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
typedef struct LFWorkspace
 { void *pTMW; 
   void *pSPPIDTW;
 } LFWorkspace;

void *CreateLFWorkspace()
{ 
  LFWorkspace *W;

  W=(LFWorkspace *)malloc(sizeof *W);
  W->pTMW=CreateTMWorkspace();
  return (void *)W;
}

void FreeLFWorkspace(void *pLFW)
{ 
  LFWorkspace *W=(LFWorkspace *)pLFW;
  FreeTMWorkspace(W->pTMW);
  free(W);
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*- desingularized exponential routines ------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
#define EXPRELTOL  1.0e-8
#define EXPRELTOL2 EXPRELTOL*EXPRELTOL
cdouble cExpRel(cdouble x, int n)
{
  int m;
  cdouble Term, Sum;
  double mag2Term, mag2Sum;

  for(Term=1.0, m=1; m<n; m++)
   Term*=x/((double)m);

  for(Sum=0.0 ; m<100; m++)
   { Term*=x/((double)m);
     Sum+=Term;
     mag2Term=norm(Term);
     mag2Sum=norm(Sum);
     if ( mag2Term < EXPRELTOL2*mag2Sum )
      break;
   };
  return Sum;
} 

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*- PART 1: Routine to compute panel-panel integrals using     -*/
/*-         fixed-order numerical cubature for both panels.    -*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/
void PanelPanelInt_Fixed(cdouble Wavenumber, int NeedCross,
                         int NumTorqueAxes, double *GammaMatrix,
                         double *VD[3], int iQD, double *VS[3], int iQS,
                         int Order, int DeSingularize,
                         cdouble *L, cdouble *GradL, cdouble *dLdT)
{ 
  int np, ncp, npp, ncpp, m, mu, nu, ri, nta;
  double u, v, w, up, vp, wp;
  double AD[3], BD[3], AS[3], BS[3], *QD, *QS;
  double X[3], XmQD[3], XP[3], XPmQS[3], gXh[3], dgXh[3];
  double *TCR;
  int NumPts;
  double r, r2, R[3], dX[3], dG[3];
  double LTerm[3], Puv;
  cdouble Phi, Psi, Zeta, ik;
  cdouble LInner[3], GradLInner[9], dLdTInner[3*NumTorqueAxes];

  /***************************************************************/
  /* preliminary setup for numerical cubature.                   */
  /* in what follows, X runs over the 'destination triangle' and */
  /* XP runs over the 'source triangle' according to             */
  /*  X  = VD_1 +  u*(VD_2 - VD_1) +  v*(VD_3-VD_1)              */
  /*  XP = VS_1 + up*(VS_2 - VS_1) + vp*(VS_3-VS_1)              */
  /* where (V_1, V_2, V_3) are the triangle vertices and (u,v)   */
  /* are the cubature points for a 2D numerical cubature rule    */
  /* over the standard triangle with vertices at (0,0)(1,0)(0,1).*/
  /* note that the jacobian of the transformation is 4*A*AP      */
  /* where A and AP are the areas of the triangles; this         */
  /* conveniently cancels the corresponding factor coming from   */
  /* the RWG basis function prefactor                            */
  /***************************************************************/
  QD=VD[iQD];
  VecSub(VD[(iQD+1)%3],VD[iQD],AD);
  VecSub(VD[(iQD+2)%3],VD[iQD],BD);

  QS=VS[iQS];
  VecSub(VS[(iQS+1)%3],VS[iQS],AS);
  VecSub(VS[(iQS+2)%3],VS[iQS],BS);

  /***************************************************************/
  /* choose order of quadrature scheme to use.                   */
  /* TCR ('triangle cubature rule') points to a a vector of 3N   */
  /* doubles (for an N-point cubature rule).                     */
  /* TCR[3*n,3*n+1,3*n+2]=(u,v,w), where (u,v)                   */
  /* are the (x,y) coordinates of the nth quadrature point and w */
  /* is its weight.                                              */
  /* note we use the same quadrature rule for both the source    */
  /* and destination triangles.                                  */
  /***************************************************************/
  TCR=GetTCR(Order, &NumPts);

  /***************************************************************/
  /* preliminary setup before entering quadrature loops          */
  /***************************************************************/
  memset(L,0,3*sizeof(cdouble));
  if (GradL) memset(GradL,0,9*sizeof(cdouble));
  if (dLdT) memset(dLdT,0,3*NumTorqueAxes*sizeof(cdouble));

  LTerm[1]=4.0;   // this is constant throughout

  if (!NeedCross)
   LInner[2]=0.0 ;

  ik = II*Wavenumber;

  /***************************************************************/
  /* outer loop **************************************************/
  /***************************************************************/
  for(np=ncp=0; np<NumPts; np++) 
   { 
     u=TCR[ncp++]; v=TCR[ncp++]; w=TCR[ncp++];

     /***************************************************************/
     /* set XmQ and X ***********************************************/
     /***************************************************************/
     for(mu=0; mu<3; mu++)
      { XmQD[mu]=u*AD[mu] + v*BD[mu];
        X[mu]=XmQD[mu] + QD[mu];
      };

     /***************************************************************/
     /* inner loop to calculate value of inner integrand ************/
     /***************************************************************/
     memset(LInner,0,3*sizeof(cdouble));
     if (GradL) memset(GradLInner,0,9*sizeof(cdouble));
     if (dLdT) memset(dLdTInner,0,3*NumTorqueAxes*sizeof(cdouble));
     for(npp=ncpp=0; npp<NumPts; npp++)
      { 
        up=TCR[ncpp++]; vp=TCR[ncpp++]; wp=TCR[ncpp++];

        /***************************************************************/ 
        /* set XPmQp and XP ********************************************/
        /***************************************************************/
        for(mu=0; mu<3; mu++)
         { XPmQS[mu]=up*AS[mu] + vp*BS[mu];
           XP[mu]=XPmQS[mu] + QS[mu];
         };
      
        /***************************************************************/
        /* inner integrand  ********************************************/
        /***************************************************************/
        VecSub(X,XP,R);
        r=VecNorm(R);
        r2=r*r;

        /* compute L factors */
        LTerm[0]=VecDot(XmQD, XPmQS); 
        if (NeedCross)
         { VecCross(XmQD, XPmQS, gXh);
           LTerm[2]=VecDot( gXh, R );
         };
   
        /* compute Phi, Psi, Zeta factors */
        if (DeSingularize)
         Phi = cExpRel(ik*r,4) / (4.0*M_PI*r);
        else
         Phi = exp(ik*r) / (4.0*M_PI*r);

        if ( !finite(real(Phi)) ) Phi=0.0 ;
        Phi*=wp;
        Psi = Phi * (ik - 1.0/r) / r;
        Zeta = Phi * (ik*ik - 3.0*ik/r + 3.0/r2) / r2;

        /* now combine factors as necessary for the various integrands */

        /* 1. L_{0,1,2} */
        LInner[0] += LTerm[0] * Phi;
        LInner[1] += LTerm[1] * Phi;
        if ( NeedCross )
         LInner[2] += LTerm[2] * Psi;

        /* 2. d/dX_mu L_{0,1,2} */
        if ( GradL )
         for(mu=0; mu<3; mu++)
           { GradLInner[3*mu + 0] += LTerm[0]*R[mu]*Psi;
             GradLInner[3*mu + 1] += LTerm[1]*R[mu]*Psi;
             GradLInner[3*mu + 2] += gXh[mu]*Psi + LTerm[2]*R[mu]*Zeta;
           };

        /* 3. d/dTheta L_{0,1,2} */
        if ( NumTorqueAxes>0 && GammaMatrix!=0 && dLdT!=0 )
         for(nta=0; nta<NumTorqueAxes; nta++)
          { memset(dX,0,3*sizeof(double));
            memset(dG,0,3*sizeof(double));
            for(mu=0; mu<3; mu++)
             for(nu=0; nu<3; nu++)
              { dX[mu]+=GammaMatrix[9*nta + mu + 3*nu]*X[nu];
                dG[mu]+=GammaMatrix[9*nta + mu + 3*nu]*XmQD[nu];
              };
            Puv=VecDot(R,dX);
            dLdTInner[3*nta + 0] += LTerm[0]*Puv*Psi + VecDot(dG,XPmQS)*Phi;
            dLdTInner[3*nta + 1] += LTerm[1]*Puv*Psi;
            dLdTInner[3*nta + 2] += LTerm[2]*Puv*Zeta 
                                    + (  VecDot(VecCross(dG,XPmQS,dgXh),R) 
                                       + VecDot(gXh,dX)
                                      )*Psi;
          }; // for(nta= ... )

      }; /* for(npp=ncpp=0; npp<NumPts; npp++) */

     /*--------------------------------------------------------------*/
     /*- accumulate contributions to outer integral                  */
     /*--------------------------------------------------------------*/
     for(m=0; m<3; m++)
      L[m]+=w*LInner[m];
     if (GradL)
      for(m=0; m<9; m++)
       GradL[m]+=w*GradLInner[m];
     if (dLdT)
      for(m=0; m<3*NumTorqueAxes; m++)
       dLdT[m]+=w*dLdTInner[m];

   }; // for(np=ncp=0; np<nPts; np++) 
}

/***************************************************************/
/* calculate integrals over a single pair of triangles using a */
/* complicated bifurcation tree to determine the best strategy */
/* based on how near the two triangles are to each other.      */
/***************************************************************/
void PanelPanelInt(LFWorkspace *W, 
                   cdouble Wavenumber, int NeedCross,
                   int NumTorqueAxes, double *GammaMatrix, 
                   RWGObject *O1, int np1, int iQ1, 
                   RWGObject *O2, int np2, int iQ2, 
                   cdouble *L, cdouble *GradL, cdouble *dLdT)
{ 
  double rCC, rRel;
  double *PV1[3], *PV2[3];
  int iTemp, XXPFlipped;
  int ncv, Index[3], IndexP[3];
  RWGPanel *P1, *P2;

  /***************************************************************/
  /* extract panel vertices **************************************/
  /***************************************************************/
  P1=O1->Panels[np1];
  P2=O2->Panels[np2];

  PV1[0]=O1->Vertices + 3*P1->VI[0];
  PV1[1]=O1->Vertices + 3*P1->VI[1];
  PV1[2]=O1->Vertices + 3*P1->VI[2];

  PV2[0]=O2->Vertices + 3*P2->VI[0];
  PV2[1]=O2->Vertices + 3*P2->VI[1];
  PV2[2]=O2->Vertices + 3*P2->VI[2];

  /***************************************************************/
  /* simplest possibility is same panel on same object: just use */
  /* taylor's common-triangle scheme for full panel integral.    */
  /***************************************************************/
  if( P1==P2 )
   { 
     L[0]=TaylorMaster(W->pTMW, TM_COMMONTRIANGLE, TM_EIKR_OVER_R, TM_DOT,
                       Wavenumber, 0.0, PV1[iQ1], PV1[(iQ1+1)%3],
                       PV1[(iQ1+2)%3], PV1[(iQ1+1)%3], 
                       PV1[(iQ1+2)%3], PV1[iQ1], PV1[iQ2], 1.0);

     L[1]=4.0*TaylorMaster(W->pTMW, TM_COMMONTRIANGLE, TM_EIKR_OVER_R, TM_ONE,
                           Wavenumber, 0.0, PV1[iQ1], PV1[(iQ1+1)%3],
                           PV1[(iQ1+2)%3], PV1[(iQ1+1)%3], 
                           PV1[(iQ1+2)%3], PV1[iQ1], PV1[iQ2], 1.0);

    L[2]=0.0;   /* cross-product integral vanishes for common triangle */

    if (GradL) memset(GradL,0,9*sizeof(cdouble));
    if (dLdT) memset(GradL,0,3*NumTorqueAxes*sizeof(cdouble));
    return;
   };

  /***************************************************************/
  /* extract a little more information on the panel pair         */
  /***************************************************************/
  rCC=VecDistance(P1->Centroid, P2->Centroid);
  rRel=rCC/fmax(P1->Radius,P2->Radius);

  if (O1==O2)
   ncv=O1->CountCommonVertices(np1,np2,Index,IndexP);
  else
   ncv=0;

  /***************************************************************/
  /* if there are no common vertices and the imaginary part of   */
  /* kr is >= 25 (i.e. the panel-panel integrals will be down    */
  /* by a factor of e^{-25}) then we just call them zero.        */
  /***************************************************************/
  if ( ncv==0 && (imag(Wavenumber))*rCC>25.0 )
   { memset(L,0,3*sizeof(cdouble));
     if (GradL) memset(GradL,0,9*sizeof(cdouble));
     if (dLdT) memset(dLdT,0,3*NumTorqueAxes*sizeof(cdouble));
     return;
   };

  /***************************************************************/
  /* the next simplest possibility is that the panels are far    */
  /* enough away from each other that we don't need to           */
  /* desingularize the kernel in the integrand.                  */
  /***************************************************************/
  if ( rRel > 2*DESINGULARIZATION_RADIUS )
   PanelPanelInt_Fixed(Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix, 
                       PV1, iQ1, PV2, iQ2, 4, 0, L, GradL, dLdT);
  else if ( rRel > DESINGULARIZATION_RADIUS )
   PanelPanelInt_Fixed(Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix, 
                       PV1, iQ1, PV2, iQ2, 7, 0, L, GradL, dLdT);
  else if ( O1!=O2 && rRel>0.5*DESINGULARIZATION_RADIUS )
   PanelPanelInt_Fixed(Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix, 
                       PV1, iQ1, PV2, iQ2, 14 , 0, L, GradL, dLdT);
  else if ( O1!=O2 ) /* this should happen rarely */
   PanelPanelInt_Fixed(Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix, 
                       PV1, iQ1, PV2, iQ2, 20 , 0, L, GradL, dLdT);
  else
   { 
     /***************************************************************/
     /* panels are relatively near each other on the same object.   */
     /***************************************************************/

     /***************************************************************/
     /* 1. first handle the high-frequency case. for large Kappa,   */
     /*    the desingularized cubature method doesn't work well,    */
     /*    but the 'taylor method' works fine.                      */
     /*    we use the taylor method for panels with 1, 2, or 3      */
     /*    common vertices, and non-desingularized quadrature for   */
     /*    panels with no common vertices. (this latter may not be  */
     /*    very accurate, but in the high-frequency limit the       */
     /*    panel-panel integral over panels with no common vertices */
     /*    decays exponentially with kappa, whereas it decays       */
     /*    algebraically with kappa for panels with common vertices,*/
     /*    so the contributions from panels with no common vertices */
     /*    will be small and do not need to be evaluated to high    */
     /*    accuracy)                                                */
     /***************************************************************/
     if ( (imag(Wavenumber)*fmax(P1->Radius, P2->Radius)) > 2.0 )
      {
        if (ncv==2)
         {
           L[0]=TaylorMaster(W->pTMW, TM_COMMONEDGE, TM_EIKR_OVER_R, TM_DOT, Wavenumber, 0.0,
                             PV1[Index[0]],  PV1[Index[1]],  PV1[3-Index[0]-Index[1]],
                             PV2[IndexP[1]], PV2[3-IndexP[0]-IndexP[1]],
                             PV1[iQ1], PV2[iQ2], 1.0);

           L[1]=4.0*TaylorMaster(W->pTMW, TM_COMMONEDGE, TM_EIKR_OVER_R,  TM_ONE, Wavenumber, 0.0,
                                 PV1[Index[0]],  PV1[Index[1]],  PV1[3-Index[0]-Index[1]],
                                 PV2[IndexP[1]], PV2[3-IndexP[0]-IndexP[1]],
                                 PV1[iQ1], PV2[iQ2], 1.0);

           L[2]=TaylorMaster(W->pTMW, TM_COMMONEDGE, TM_EIKR_OVER_R, TM_CROSS, Wavenumber, 0.0,
                             PV1[Index[0]],  PV1[Index[1]],  PV1[3-Index[0]-Index[1]],
                             PV2[IndexP[1]], PV2[3-IndexP[0]-IndexP[1]],
                             PV1[iQ1], PV2[iQ2], 1.0);
         }
        else if (ncv==1)
         { 
           L[0]=TaylorMaster(W->pTMW, TM_COMMONVERTEX, TM_EIKR_OVER_R, TM_DOT, Wavenumber, 0.0,
                             PV1[Index[0]],   PV1[ (Index[0]+1)%3],  PV1[ (Index[0]+2)%3 ],
                             PV2[ (IndexP[0]+1)%3], PV2[ (IndexP[0]+2)%3 ],
                             PV1[iQ1], PV2[iQ2], 1.0);

           L[1]=4.0*TaylorMaster(W->pTMW, TM_COMMONVERTEX, TM_EIKR_OVER_R, TM_ONE, Wavenumber, 0.0,
                                 PV1[Index[0]],   PV1[ (Index[0]+1)%3],  PV1[ (Index[0]+2)%3 ],
                                 PV2[ (IndexP[0]+1)%3], PV2[ (IndexP[0]+2)%3 ],
                                 PV1[iQ1], PV2[iQ2], 1.0);

           L[2]=TaylorMaster(W->pTMW, TM_COMMONVERTEX, TM_EIKR_OVER_R, TM_CROSS, Wavenumber, 0.0,
                             PV1[Index[0]],   PV1[ (Index[0]+1)%3],  PV1[ (Index[0]+2)%3 ],
                             PV2[ (IndexP[0]+1)%3], PV2[ (IndexP[0]+2)%3 ],
                             PV1[iQ1], PV2[iQ2], 1.0);

         }
        else if (ncv==0)
         { PanelPanelInt_Fixed(Wavenumber, NeedCross, 0, 0, 
                               PV1, iQ1, PV2, iQ2, 20 , 0, L, 0, 0); 
printf("Howdage\n");
         };
        return;
      };

     /***************************************************************/
     /* 2. otherwise, we use the desingularized quadrature method.  */
     /***************************************************************/
     StaticPPIData MySPPID, *SPPID;
     int rp;
     double A[3], AP[3], B[3], BP[3];
     double VmVP[3], VmQ[3], VPmQP[3], QmQP[3], QxQP[3], VScratch[3];
     double gDot[9], gCross[9];
     cdouble ik, ikPowers[RPMAX+2], ikPowers2[RPMAX+1];

     /***************************************************************/
     /* 2a. do some preliminary setup *******************************/
     /***************************************************************/
#if 0
     if (O1==O2 && np1>np2)
      { iTemp=np1; np1=np2; np2=iTemp;   
        iTemp=iQ1; iQ1=iQ2; iQ2=iTemp; 
      };
     ncv=O1->CountCommonVertices(np1,np2,Index,IndexP);
#endif
     double *pV1, *pV2, *pV3, *pV1P, *pV2P, *pV3P, *pQ, *pQP;
     double V1[3], V2[3], V3[3], V1P[3], V2P[3], V3P[3], Q[3], QP[3];
     if (ncv==1)
      { 
        pV1 = O1->Vertices + 3*O1->Panels[np1]->VI[ Index[0] ];
        pV2 = O1->Vertices + 3*O1->Panels[np1]->VI[ (Index[0]+1) %3 ];
        pV3 = O1->Vertices + 3*O1->Panels[np1]->VI[ (Index[0]+2) %3 ];
     
        //V1P = V1; 
        pV1P = O2->Vertices + 3*O2->Panels[np2]->VI[ IndexP[0] ];
        pV2P = O2->Vertices + 3*O2->Panels[np2]->VI[ (IndexP[0]+1) %3 ];
        pV3P = O2->Vertices + 3*O2->Panels[np2]->VI[ (IndexP[0]+2) %3 ];
      }
     else if (ncv==2)
      { 
        pV1 = O1->Vertices  + 3*O1->Panels[np1]->VI[ Index[0] ];
        pV2  = O1->Vertices + 3*O1->Panels[np1]->VI[ Index[1] ];
        pV3  = O1->Vertices + 3*O1->Panels[np1]->VI[ 3 - Index[0] - Index[1] ];
     
        //V1P = V1;
        //V2P = V2;
        pV1P = O2->Vertices + 3*O2->Panels[np2]->VI[ IndexP[0] ];
        pV2P = O2->Vertices + 3*O2->Panels[np2]->VI[ IndexP[1] ];
        pV3P = O2->Vertices + 3*O2->Panels[np2]->VI[ 3 - IndexP[0] - IndexP[1] ];
      }
     else
      { 
        pV1  = O1->Vertices + 3*O1->Panels[np1]->VI[0];
        pV2  = O1->Vertices + 3*O1->Panels[np1]->VI[1];
        pV3  = O1->Vertices + 3*O1->Panels[np1]->VI[2];
        pV1P = O2->Vertices + 3*O2->Panels[np2]->VI[0];
        pV2P = O2->Vertices + 3*O2->Panels[np2]->VI[1];
        pV3P = O2->Vertices + 3*O2->Panels[np2]->VI[2];
      };
     
     pQ=O1->Vertices + 3*O1->Panels[np1]->VI[iQ1];
     pQP=O2->Vertices + 3*O2->Panels[np2]->VI[iQ2];

     memcpy(V1,pV1,3*sizeof(double));
     memcpy(V2,pV2,3*sizeof(double));
     memcpy(V3,pV3,3*sizeof(double));
     memcpy(V1P,pV1P,3*sizeof(double));
     memcpy(V2P,pV2P,3*sizeof(double));
     memcpy(V3P,pV3P,3*sizeof(double));
     memcpy(Q,pQ,3*sizeof(double));
     memcpy(QP,pQP,3*sizeof(double));
     
     /*****************************************************************/
     /* 2b. first compute the panel-panel integral using medium-order */
     /*     cubature with the three most singular terms removed.      */
     /*****************************************************************/
     P1=O1->Panels[np1];
     P2=O2->Panels[np2];

     PV1[0]=O1->Vertices + 3*P1->VI[0];
     PV1[1]=O1->Vertices + 3*P1->VI[1];
     PV1[2]=O1->Vertices + 3*P1->VI[2];

     PV2[0]=O2->Vertices + 3*P2->VI[0];
     PV2[1]=O2->Vertices + 3*P2->VI[1];
     PV2[2]=O2->Vertices + 3*P2->VI[2];

     PanelPanelInt_Fixed(Wavenumber, NeedCross, 0, 0,
                         PV1, iQ1, PV2, iQ2, 4, 1, L, 0, 0);

     /****************************************************************/
     /* 2c. next get static panel-panel integral data, from a lookup */
     /*     table if we have one, or else by computing it on the fly */
     /****************************************************************/
     SPPID=0;
     if( O1->SPPIDTable )
      { SPPID=GetStaticPPIData(O1->SPPIDTable, np1, np2, &MySPPID);
          
        /* this step is important: we calculated the static panel-panel data */
        /* with the object mesh in its original configuration, i.e. as       */
        /* specified in the .msh file.  however, since then we may have      */
        /* transformed (rotated and/or translated) the object. thus, we need */
        /* to do the following computation with the vertices transformed     */
        /* back to their original locations.                                 */

        O1->UnTransformPoint(V1);
        O1->UnTransformPoint(V2);
        O1->UnTransformPoint(V3);
        O1->UnTransformPoint(Q);
        O2->UnTransformPoint(V1P);
        O2->UnTransformPoint(V2P);
        O2->UnTransformPoint(V3P);
        O2->UnTransformPoint(QP);
      }
     else
      { ComputeStaticPPIData(O1, np1, O2, np2, &MySPPID);
        SPPID=&MySPPID;
      };
   
     /***************************************************************/
     /* 2d. finally, augment the desingularized integrals by adding */
     /* the contributions from the integrals of the singular terms. */
     /***************************************************************/
     VecSub(V1, V1P, VmVP);

     VecSub(V1,Q,VmQ);
     VecSub(V2,V1,A);
     VecSub(V3,V2,B);

     VecSub(V1P,QP,VPmQP);
     VecSub(V2P,V1P,AP);
     VecSub(V3P,V2P,BP);

     gDot[0]=VecDot(VmQ,VPmQP);     /* 1   */
     gDot[1]=VecDot(VmQ,AP);        /* up  */
     gDot[2]=VecDot(VmQ,BP);        /* vp  */
     gDot[3]=VecDot(A,VPmQP);       /* u   */
     gDot[4]=VecDot(A,AP);          /* uup */
     gDot[5]=VecDot(A,BP);          /* uvp */
     gDot[6]=VecDot(B,VPmQP);       /* v   */
     gDot[7]=VecDot(B,AP);          /* vup */
     gDot[8]=VecDot(B,BP);          /* vvp */

     if (NeedCross)
      { 
        VecSub(Q,QP,QmQP);
        VecCross(Q,QP,QxQP);

        gCross[0]=  VecDot(VmVP, QxQP) + VecDot(VecCross(V1, V1P, VScratch), QmQP); /* 1   */ 
        gCross[1]= -VecDot(  AP, QxQP) + VecDot(VecCross(V1, AP , VScratch), QmQP); /* up  */
        gCross[2]= -VecDot(  BP, QxQP) + VecDot(VecCross(V1, BP , VScratch), QmQP); /* vp  */
        gCross[3]=  VecDot(   A, QxQP) + VecDot(VecCross(A,  V1P, VScratch), QmQP); /* u   */ 
        gCross[4]=                       VecDot(VecCross(A,  AP,  VScratch), QmQP); /* uup */
        gCross[5]=                       VecDot(VecCross(A,  BP,  VScratch), QmQP); /* uvp */
        gCross[6]=  VecDot(   B, QxQP) + VecDot(VecCross(B,  V1P, VScratch), QmQP); /* v   */ 
        gCross[7]=                       VecDot(VecCross(B,  AP,  VScratch), QmQP); /* vup */
        gCross[8]=                       VecDot(VecCross(B,  BP,  VScratch), QmQP); /* vvp */

        L[2] -= (  VecDot(SPPID->XmXPoR3Int, QxQP) 
                 + VecDot(SPPID->XxXPoR3Int, QmQP) ) / (4.0*M_PI);
      };

     ik = II*Wavenumber;

     ikPowers[0]=1.0;
     ikPowers[1]=ik;
     ikPowers[2]=ik*ikPowers[1] / 2.0;
     ikPowers[3]=ik*ikPowers[2] / 3.0;

     ikPowers2[0]=ikPowers[2];
     ikPowers2[1]=2.0*ikPowers[3];
     ikPowers2[2]=ik*ikPowers[3];

     for(rp=-1; rp<=RPMAX; rp++)
      { 
         L[0] += ikPowers[rp+1]*(    SPPID->hrnInt[rp+1][0]*gDot[0]
                                   + SPPID->hrnInt[rp+1][1]*gDot[1]
                                   + SPPID->hrnInt[rp+1][2]*gDot[2]
                                   + SPPID->hrnInt[rp+1][3]*gDot[3]
                                   + SPPID->hrnInt[rp+1][4]*gDot[4]
                                   + SPPID->hrnInt[rp+1][5]*gDot[5]
                                   + SPPID->hrnInt[rp+1][6]*gDot[6]
                                   + SPPID->hrnInt[rp+1][7]*gDot[7]
                                   + SPPID->hrnInt[rp+1][8]*gDot[8] 
                                ) / (4.0*M_PI);
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
if (Diagnostics)
 printf("  rp=%i: L[0]=%e\n",rp+1,real(L[0]));
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

         L[1] += 4.0*ikPowers[rp+1]*SPPID->hrnInt[rp+1][0] / (4.0*M_PI);

         if(NeedCross && rp<RPMAX)
          L[2] += ikPowers2[rp+1]*(    SPPID->hrnInt[rp+1][0]*gCross[0]
                                     + SPPID->hrnInt[rp+1][1]*gCross[1]
                                     + SPPID->hrnInt[rp+1][2]*gCross[2]
                                     + SPPID->hrnInt[rp+1][3]*gCross[3]
                                     + SPPID->hrnInt[rp+1][4]*gCross[4]
                                     + SPPID->hrnInt[rp+1][5]*gCross[5]
                                     + SPPID->hrnInt[rp+1][6]*gCross[6]
                                     + SPPID->hrnInt[rp+1][7]*gCross[7]
                                     + SPPID->hrnInt[rp+1][8]*gCross[8]
                                  ) / (4.0*M_PI);

      };

   };

} 

/***************************************************************/
/* Evaluate and return the L-functions between two RWG basis   */
/* functions.                                                  */
/*                                                             */
/* Inputs:                                                     */
/*                                                             */
/*     pLFW:        the return value of an earlier call to     */
/*                  CreateLFWorkspace().                       */
/*     O1,ne1:      object and BF index of first BF            */
/*     O2,ne2:      object and BF index of second BF           */
/*     Wavenumber:  complex wavevector                         */
/*     NeedCross:   set to 1 if you need the cross-product     */
/*                  integrals. if you set this to 0, the       */
/*                  corresponding slot in the return values    */
/*                  is set to 0.                               */
/*                                                             */
/*    NumTorqueAxes: number (0--3) of axes about which to      */
/*                  compute theta derivatives.                 */
/*     GammaMatrix: matrices describing the rotation axes      */
/*                  about which d/dTheta integrals are         */
/*                  computed (for torque computations).        */
/*                  GammaMatrix[ 9*nta + (i+3*j)] = i,j entry  */
/*                  in the Gamma matrix for torque axis #nta.  */
/*                  (Not referenced if NumTorqueAxes==0).      */
/*                                                             */
/* Outputs:                                                    */
/*                                                             */
/*     L[m]:        The inner product of basis functions       */
/*                  (O1,ne1) and (O2,ne2) with operator L_m    */
/*                  (m=0, 1, 2 for bullet, nabla, times).      */
/*                                                             */
/*     GradL[3*mu + m]: d/dx_mu L[m]                           */
/*                  where the derivative is wrt displacement   */
/*                  of the first object (O1, ne1) in the x_mu  */
/*                  direction.                                 */
/*                  If GradL is NULL on input, derivatives     */
/*                  are not computed.                          */
/*                                                             */
/*     dLdT[3*nta + m]: d/dTheta L[m]                          */
/*                  where the derivative is wrt rotation of    */
/*                  the first object (O1, ne1) about an axis   */
/*                  described by GammaMatrix[nta] as           */
/*                  discussed above.                           */
/*                  If dLdT or GammaMatrix are NULL on input   */
/*                  (and/or if NumTorqueAxes==0) then          */
/*                  Theta derivatives are not computed.        */
/***************************************************************/
void GetLFunctions(void *pLFW, 
                   RWGObject *O1, int ne1, RWGObject *O2, int ne2,
                   cdouble Wavenumber, int NeedCross,
                   int NumTorqueAxes, double *GammaMatrix,
                   cdouble L[3], cdouble *GradL, cdouble *dLdT)
{ 
  LFWorkspace *W=(LFWorkspace *)pLFW;
  RWGEdge *E1, *E2;
  int m, mu;
  int nta;
  int iPPanel1, iMPanel1, iPPanel2, iMPanel2;  
  int iQP1, iQM1, iQP2, iQM2;
  double PreFac;
  cdouble LPP[3], LPM[3], LMP[3], LMM[3];
  cdouble GradLPP[9], GradLPM[9], GradLMP[9], GradLMM[9];
  cdouble dLPPdT[9], dLPMdT[9], dLMPdT[9], dLMMdT[9];

  /*--------------------------------------------------------------*/
  /*- preliminary setup ------------------------------------------*/
  /*--------------------------------------------------------------*/
  E1=O1->Edges[ne1];
  E2=O2->Edges[ne2];

  iPPanel1=E1->iPPanel;
  iMPanel1=E1->iMPanel;
  iPPanel2=E2->iPPanel;
  iMPanel2=E2->iMPanel;
  iQP1=E1->PIndex;
  iQM1=E1->MIndex;
  iQP2=E2->PIndex;
  iQM2=E2->MIndex;

  PreFac=E1->Length * E2->Length;

  /*--------------------------------------------------------------*/
  /*- compute integrals over each of the four pairs of panels    -*/
  /*--------------------------------------------------------------*/
  PanelPanelInt(W, Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix,
                O1, iPPanel1, iQP1, O2, iPPanel2, iQP2, 
                LPP, GradLPP, dLPPdT);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
#if 0
if ( (ne1==30 && ne2==38) || (ne1==45 && ne2==46) )
 printf("(%i,%i): PP=(%e,%e) (Areas=%e,%e)\n",ne1,ne2,
         real(LPP[0]),real(LPP[1]),O1->Panels[iPPanel1]->Area,O2->Panels[iPPanel2]->Area);
#endif
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
  PanelPanelInt(W, Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix,
                O1, iPPanel1, iQP1, O2, iMPanel2, iQM2, 
                LPM, GradLPM, dLPMdT);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
#if 0
if ( (ne1==30 && ne2==38) || (ne1==45 && ne2==46) )
 printf("(%i,%i): PM=(%e,%e) (Areas=%e,%e)\n",ne1,ne2,
         real(LPM[0]),real(LPM[1]),O1->Panels[iPPanel1]->Area,O2->Panels[iMPanel2]->Area);
if (ne1==45 && ne2==46) Diagnostics=1;
#endif
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  PanelPanelInt(W, Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix,
                O1, iMPanel1, iQM1, O2, iPPanel2, iQP2, 
                LMP, GradLMP, dLMPdT);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
#if 0
Diagnostics=0;
if ( (ne1==30 && ne2==38) || (ne1==45 && ne2==46) )
 printf("(%i,%i): MP=(%e,%e) (Areas=%e,%e)\n",ne1,ne2,
         real(LMP[0]),real(LMP[1]),O1->Panels[iMPanel1]->Area,O2->Panels[iPPanel2]->Area);
if (ne1==30 && ne2==38) Diagnostics=1;
#endif
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  PanelPanelInt(W, Wavenumber, NeedCross, NumTorqueAxes, GammaMatrix,
                O1, iMPanel1, iQM1, O2, iMPanel2, iQM2, 
                LMM, GradLMM, dLMMdT);

/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/
#if 0
Diagnostics=0;
if ( (ne1==30 && ne2==38) || (ne1==45 && ne2==46) )
 printf("(%i,%i): MM=(%e,%e) (Areas=%e,%e)\n",ne1,ne2,
         real(LMM[0]),real(LMM[1]),O1->Panels[iMPanel1]->Area,O2->Panels[iMPanel2]->Area);
#endif
/*!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!*/

  /*--------------------------------------------------------------*/
  /*- assemble the final quantities ------------------------------*/
  /*--------------------------------------------------------------*/
  for(m=0; m<3; m++)
   L[m]=PreFac*(LPP[m] - LPM[m] - LMP[m] + LMM[m]);

  if (GradL)
   for(m=0; m<9; m++)
    GradL[m]=PreFac*(GradLPP[m] - GradLPM[m] - GradLMP[m] + GradLMM[m]);

  if (dLdT)
   for(m=0; m<3*NumTorqueAxes; m++)
    dLdT[m]=PreFac*(dLPPdT[m] - dLPMdT[m] - dLMPdT[m] + dLMMdT[m]); 

}

/***************************************************************/
/* utility routines used to construct a GammaMatrix that may   */
/* be passed to MatrixElement to compute theta derivatives.    */
/*                                                             */
/* there are three different entry points to this routine      */
/* depending on how the user prefers to specify the axis about */
/* which objects are rotated.                                  */
/*                                                             */
/* in all cases, GammaMatrix must point to a buffer with       */
/* space for 9 doubles, which is filled in with the matrix and */
/* may be subsequently passed to MatrixElement.                */
/*                                                             */
/* Algorithm:                                                  */
/*  1. Construct the matrix Lambda that rotates the Z axis     */
/*     into alignment with the TorqueAxis.                     */
/*  2. Construct the matrix Gamma_0 such that Gamma_0*dTheta is*/
/*     the matrix that rotates through an infinitesimal angle  */
/*     dTheta about the Z axis.                                */
/*  3. Set GammaMatrix = Lambda^{-1} * Gamma_0 * Lambda.       */
/***************************************************************/

/* 1: specify torque axis as a vector of cartesian components */
void CreateGammaMatrix(double *TorqueAxis, double *GammaMatrix)
{ 
  int i, j, k, l;
  double Lambda[3][3], Gamma0[3][3], ct, st, cp, sp;
  double MyTorqueAxis[3];

  /* make a quick copy so we don't modify the user's vector */
  memcpy(MyTorqueAxis,TorqueAxis,3*sizeof(double));

  VecNormalize(MyTorqueAxis);
  ct=MyTorqueAxis[2];
  st=sqrt(1.0-ct*ct);
  cp= ( st < 1.0e-8 ) ? 1.0 : MyTorqueAxis[0] / st;
  sp= ( st < 1.0e-8 ) ? 0.0 : MyTorqueAxis[1] / st;

  Lambda[0][0]=ct*cp;  Lambda[0][1]=ct*sp;  Lambda[0][2]=-st;
  Lambda[1][0]=-sp;    Lambda[1][1]=cp;     Lambda[1][2]=0.0;
  Lambda[2][0]=st*cp;  Lambda[2][1]=st*sp;  Lambda[2][2]=ct;

  Gamma0[0][0]=0.0;    Gamma0[0][1]=-1.0;   Gamma0[0][2]=0.0;
  Gamma0[1][0]=1.0;    Gamma0[1][1]=0.0;    Gamma0[1][2]=0.0;
  Gamma0[2][0]=0.0;    Gamma0[2][1]=0.0;    Gamma0[2][2]=0.0;

  memset(GammaMatrix,0,9*sizeof(double));
  for(i=0; i<3; i++)
   for(j=0; j<3; j++)
    for(k=0; k<3; k++)
     for(l=0; l<3; l++)
      // Gamma[i+3*j] += LambdaInverse[i][k] * Gamma0[k][l] * Lambda[l][j];
      GammaMatrix[i+3*j] += Lambda[k][i] * Gamma0[k][l] * Lambda[l][j];
}


/* 2: specify torque axis as three separate cartesian components */
void CreateGammaMatrix(double TorqueAxisX, double TorqueAxisY, 
                       double TorqueAxisZ, double *GammaMatrix)
{ double TorqueAxis[3];

  TorqueAxis[0]=TorqueAxisX;
  TorqueAxis[1]=TorqueAxisY;
  TorqueAxis[2]=TorqueAxisZ;
  CreateGammaMatrix(TorqueAxis,GammaMatrix);
}

/* 3: specify (Theta,Phi) angles of torque axis */ 
void CreateGammaMatrix(double Theta, double Phi, double *GammaMatrix)
{ 
  double TorqueAxis[3];

  TorqueAxis[0]=sin(Theta)*cos(Phi);
  TorqueAxis[1]=sin(Theta)*sin(Phi);
  TorqueAxis[2]=cos(Theta);
  CreateGammaMatrix(TorqueAxis,GammaMatrix);
} 
