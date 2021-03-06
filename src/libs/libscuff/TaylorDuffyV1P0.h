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
 * TaylorDuffy.h -- header file for implementation of the taylor-duffy
 *               -- scheme for computing singular panel-panel integrals
 *
 * homer reid    -- 5/2009 -- 2/2012
 */
#ifndef TAYLORDUFFY_H
#define TAYLORDUFFY_H

#include <libhrutil.h>

namespace scuff {

#define TM_INTERVALS 10000

/***************************************************************/
/* constants ***************************************************/
/***************************************************************/
//values for the WhichG parameter to the Taylor_xx routines 
#define TM_RP                  0
#define TM_EMKR_OVER_R         1
#define TM_GRADEMKR_OVER_R     2
#define TM_EIKR_OVER_R         3
#define TM_GRADEIKR_OVER_R     4

//values for the WhichH parameter to the Taylor_xx routines 
#define TM_ONE                 0
#define TM_DOT                 1
#define TM_DOTPLUS             2
#define TM_CROSS               3
#define TM_ENORMAL             4

// values for the WhichCase parameter. note the values correspond to
// the numbers of common vertices in each case.
#define TM_COMMONVERTEX        1
#define TM_COMMONEDGE          2
#define TM_COMMONTRIANGLE      3

/***************************************************************/
/* Data structure containing various data passed back and      */
/* forth among taylor-method routines.                         */
/***************************************************************/
typedef struct TMWorkspace
 {
   /* geometric data on triangles */
   double A2, B2, AP2, BP2, L2;
   double AdB, AdAP, AdBP, AdL, AdD, AdDP;
   double BdAP, BdBP, BdDP;
   double APdBP, APdD;
   double BPdD, BPdL;
   double DdDP;

   double AdQxQP, APdQxQP, BdQxQP, BPdQxQP, LdQxQP; 
   double V1xAdQmQP, V1xBdQmQP, V1xAPdQmQP, V1xBPdQmQP; 
   double AxBdQmQP, AxAPdQmQP, AxBPdQmQP, BxAPdQmQP, BxBPdQmQP;

   double APdZHat, BPdZHat;

   /* a parameter whose significance depends on the       */
   /* choice of 'G' function:                             */
   /* g(r) = r^P             -->  __real__ GParam = P     */
   /* g(r) = e^{ikr}/r       -->           GParam = k     */
   cdouble GParam;

   /* pointers to functions that evaluate the I_n functions and  */
   /* C \times x sums for the various possible allowed choices   */
   /* of the G and H functions in the original integrand         */
   cdouble (*InFunc)(int n, cdouble Param, double X);
   void (*SiAlphaFunc)(const double *xVec, TMWorkspace *TMW, int WhichCase,
                       int *AlphaMin, int *AlphaMax, cdouble SiAlpha[7][5]);

   int WhichH;
   int WhichG;
   int nCalls;

 } TMWorkspace;

/***************************************************************/
/* prototype for the In functions for the various types of     */
/* kernels                                                     */
/***************************************************************/
cdouble In_RP(int n, cdouble GParam, double X);
cdouble In_EMKROverR(int n, cdouble GParam, double X);
cdouble In_GradEMKROverR(int n, cdouble GParam, double X);
cdouble In_EIKROverR(int n, cdouble GParam, double X);
cdouble In_GradEIKROverR(int n, cdouble GParam, double X);

/***************************************************************/
/* prototypes for the Si functions for the various types of    */
/* h-functions                                                 */
/***************************************************************/
void SiAlpha_One(const double *xVec, TMWorkspace *TMW, int WhichCase,
                 int *AlphaMin, int *AlphaMax, cdouble Si[7][5]);
void SiAlpha_Dot(const double *xVec, TMWorkspace *TMW, int WhichCase,
                 int *AlphaMin, int *AlphaMax, cdouble Si[7][5]);
void SiAlpha_DotPlus(const double *xVec, TMWorkspace *TMW, int WhichCase,
                     int *AlphaMin, int *AlphaMax, cdouble Si[7][5]);
void SiAlpha_Cross(const double *xVec, TMWorkspace *TMW, int WhichCase,
                   int *AlphaMin, int *AlphaMax, cdouble Si[7][5]);
void SiAlpha_ENormal(const double *xVec, TMWorkspace *TMW, int WhichCase,
                     int *AlphaMin, int *AlphaMax, cdouble Si[7][5]);

} // namespace scuff

#endif
