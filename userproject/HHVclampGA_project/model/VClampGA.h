/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
	      Falmer, Brighton BN1 9QJ, UK 
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
  
--------------------------------------------------------------------------*/

//--------------------------------------------------------------------------
/*! \file userproject/HHVclampGA_project/model/VClampGA.h

\brief Header file containing global variables and macros used in running the HHVClamp/VClampGA model.
*/
//--------------------------------------------------------------------------

#include <cassert>
#ifndef CPU_ONLY
#include <cuda_runtime.h>
#endif

#include "hr_time.h"
#include "stringUtils.h"
#include "utils.h" // for CHECK_CUDA_ERRORS

using namespace std;

#include "HHVClamp.cc"
#include "HHVClamp_CODE/definitions.h"
#include "randomGen.h"
#include "gauss.h"
randomGen R;
randomGauss RG;
#include "helper.h"

//#define DEBUG_PROCREATE
#include "GA.cc"

#define RAND(Y,X) Y = Y * 1103515245 +12345;X= (unsigned int)(Y >> 16) & 32767

// and some global variables
CStopWatch timer;
