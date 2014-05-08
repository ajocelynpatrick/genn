/*--------------------------------------------------------------------------
   Author: Thomas Nowotny
  
   Institute: Center for Computational Neuroscience and Robotics
              University of Sussex
	      Falmer, Brighton BN1 9QJ, UK 
  
   email to:  T.Nowotny@sussex.ac.uk
  
   initial version: 2010-02-07
  
--------------------------------------------------------------------------*/

//-----------------------------------------------------------------------
/*!  \file generateCuda.cpp 
  
  \brief Contains functions to generate code for running the simulation on 
  the GPU, and for I/O convenience functions between GPU
  and CPU space. Part of the code generation section.
*/
//--------------------------------------------------------------------------

//----------------------------------------------------------------------------
/*!
  \brief A function to generate CUDA device-side code.

  The function generates functions that will spawn kernel grids onto the GPU
  (but not the actual kernel code which is generated in "genNeuronKernel()"
  and "genSynpaseKernel()"). Generated functions include "copyGToCuda#()",
  "copyGFromCuda#()", "copyStateToCuda#()", "copyStateFromCuda#()",
  "copySpikesFromCuda#()", "copySpikeNFromCuda#()" and "stepTimeCuda#()". The 
  last mentioned function is the function that will initialize the execution
  on the GPU in the generated simulation engine. All other generated functions
  are "convenience functions" to handle data transfer from and to the GPU.
*/
//----------------------------------------------------------------------------

void genCudaCode(int deviceID, ostream &mos)
{
  mos << "entering genCudaCode" << endl;
  string cppFile, hppFile;
  ofstream os, osh;
  unsigned int type, tmp, size;

  //----------------------------------
  // open and setup cuda[deviceID].hpp

  hppFile = path + tS("/") + model->name + tS("_CODE_CUDA_") + tS(deviceID) + tS("/cuda") + tS(deviceID) + tS(".hpp");
  osh.open(hppFile.c_str());
  writeHeader(osh);
  osh << endl;
  osh << "//-------------------------------------------------------------------------" << endl;
  osh << "/*! \\file cuda" << deviceID << ".hpp" << endl << endl;
  osh << "\\brief File generated by GeNN for the model " << model->name << " containing CUDA declarations." << endl;
  osh << "*/" << endl;
  osh << "//-------------------------------------------------------------------------" << endl << endl;


  //--------------------------
  // open and setup control.cu

  cppFile = path + tS("/") + model->name + tS("_CODE_CUDA_") + tS(deviceID) + tS("/control.cu");
  os.open(cppFile.c_str());
  writeHeader(os);
  os << endl;
  os << "//-------------------------------------------------------------------------" << endl;
  os << "/*! \\file control.cu" << endl << endl;
  os << "\\brief File generated by GeNN for the model " << model->name << " containing CUDA control code." << endl;
  os << "*/" << endl;
  os << "//-------------------------------------------------------------------------" << endl << endl;


  //------------------------
  // add control.cu includes

  os << "#include \"utils.h\"" << endl;
  os << "#include \"cuda" << deviceID << ".hpp\"" << endl;
  os << "#include \"neuron.cu\"" << endl;
  if (model->synapseGrpN>0) os << "#include \"synapse.cu\"" << endl;
  os << "#ifndef RAND" << endl;
  os << "#define RAND(Y, X) Y = Y * 1103515245 + 12345; X = (unsigned int) (Y >> 16) & 32767" << endl;
  os << "#endif" << endl;
  os << endl;


  //------------------------
  // device neuron variables

  for (int i = 0; i < model->neuronGrpN; i++) {
    osh << "// neuron variables" << endl;
    type = model->neuronType[i];
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      osh << nModels[type].varTypes[j] << " *" << "d_" << nModels[type].varNames[j] << model->neuronName[i] << ";" << endl;
    }
  }
  osh << endl;

  //-------------------------
  // device synapse variables

  for (int i = 0; i< model->postSynapseType.size(); i++){
    osh << "// synapse variables" << endl;
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      osh << postSynModels[type].varTypes[j] << " *d_" << postSynModels[type].varNames[j] << model->synapseName[i] << ";" << endl;
      // should work at the moment but if we make postsynapse vectors independent of synapses this may change
    }
  }
  for (int i = 0; i < model->synapseGrpN; i++) {
    if (model->synapseGType[i] == INDIVIDUALG) {
      osh << model->ftype << " *d_gp" << model->synapseName[i] << ";" << endl;
      if (model->synapseType[i] == LEARN1SYNAPSE) {
	osh << model->ftype << " *d_grawp" << model->synapseName[i] << ";" << endl;
      }
    }
    if (model->synapseGType[i] == INDIVIDUALID) {
      osh << "unsigned int *d_gp" << model->synapseName[i] << ";" << endl;
    }
    if (model->synapseConnType[i] == SPARSE) {
      osh << "unsigned int *d_gp" << model->synapseName[i] << "_indInG;" << endl;
      osh << "unsigned int *d_gp" << model->synapseName[i] << "_ind;" << endl;
    } 
  }
  osh << endl;


  //----------------------------
  // device block and grid sizes

  osh << "// CUDA block and grid sizes" << endl;
  if (model->synapseGrpN > 0) { 
    unsigned int synapseGridSz = model->padSumSynapseKrnl[model->synapseGrpN - 1];   
    synapseGridSz = synapseGridSz / synapseBlkSz[deviceID];
    osh << "dim3 sThreadsCuda" << deviceID << "(" << synapseBlkSz[deviceID] << ", 1);" << endl;
    osh << "dim3 sGridCuda" << deviceID << "(" << synapseGridSz << ", 1);" << endl;
    osh << endl;
  }
  if (model->lrnGroups > 0) {
    unsigned int learnGridSz = model->padSumLearnN[model->lrnGroups - 1];
    learnGridSz = learnGridSz / learnBlkSz[deviceID];
    osh << "dim3 lThreadsCuda" << deviceID << "(" << learnBlkSz[deviceID] << ", 1);" << endl;
    osh << "dim3 lGridCuda" << deviceID << "(" << learnGridSz << ", 1);" << endl;
    osh << endl;
  }
  unsigned int neuronGridSz = model->padSumNeuronN[model->neuronGrpN - 1];
  neuronGridSz = neuronGridSz / neuronBlkSz[deviceID];
  osh << "dim3 nThreadsCuda" << deviceID << "(" << neuronBlkSz[deviceID] << ", 1);" << endl;
  if (neuronGridSz < deviceProp[deviceID].maxGridSize[1]) {
    osh << "dim3 nGridCuda" << deviceID << "(" << neuronGridSz << ", 1);" << endl;
  }
  else {
    int sqGridSize = ceil(sqrt(neuronGridSz));
    osh << "dim3 nGridCuda" << deviceID << "(" << sqGridSize << ", " << sqGridSize << ");" << endl;
  }
  osh << endl;

  osh << "// CUDA device functions" << endl;


  //------------------------------
  // CUDA device memory allocation

  osh << "void allocateMemCuda" << deviceID << "();" << endl;
  os << "void allocateMemCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  // allocate CUDA neuron variables
  os << "  // neuron variables" << endl;
  for (int i = 0; i < model->neuronGrpN; i++) {
    type = model->neuronType[i];
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      if ((nModels[type].varNames[j] == "V") && (model->neuronDelaySlots[i] != 1)) {
	size = theSize(nModels[type].varTypes[j]) * model->neuronN[i] * model->neuronDelaySlots[i];
      }
      else {
	size = theSize(nModels[type].varTypes[j]) * model->neuronN[i];
      }
      os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_" << nModels[type].varNames[j];
      os << model->neuronName[i] << ", " << size << "));" << endl;
    }
    os << endl; 
  }

  // allocate CUDA synapse variables
  os << "  // synapse variables" << endl;
  for (int i = 0; i < model->synapseGrpN; i++) {
    if (model->synapseGType[i] == INDIVIDUALG) {
      // (cases necessary here when considering sparse reps as well)
      //os << "  size = " << model->neuronN[model->synapseSource[i]] << " * " << model->neuronN[model->synapseTarget[i]] << ";" << endl;
      //os << "  cudaMalloc((void **) &d_gp" << model->synapseName[i] << ", sizeof(" << model->ftype << ") * size);" << endl;
      /*if (model->synapseConnType[i] == SPARSE){
	os << "  cudaMalloc((void **) &d_gp" << model->synapseName[i] << "_ind, sizeof(unsigned int) * size);" << endl;
	os << "  cudaMalloc((void **) &d_gp" << model->synapseName[i] << "_indInG, sizeof(unsigned int) * ("<< model->neuronN[model->synapseSource[i]] << " + 1));" << endl;
	os << "  size = sizeof(" << model->ftype << ") * g" << model->synapseName[i] << ".connN; " << endl;
	}
	else{*/
      if (model->synapseConnType[i] != SPARSE) {
	size = theSize(model->ftype) * model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]];
	os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_gp" << model->synapseName[i] << ", " << size << "));" << endl;
      }
      if (model->synapseType[i] == LEARN1SYNAPSE) {
	size = theSize(model->ftype) * model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]]; // not sure if correct
	os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_grawp" << model->synapseName[i] << ", " << size;
	os << ")); // raw synaptic conductances of group " << model->synapseName[i] << endl;
      }
    }
    // note, if GLOBALG we put the value at compile time
    if (model->synapseGType[i] == INDIVIDUALID) {
      tmp = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]];
      size = tmp >> logUIntSz;
      if (tmp > (size << logUIntSz)) size++;
      os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_gp" << model->synapseName[i] << ", ";
      os << size << ")); // synaptic connectivity of group " << model->synapseName[i] << endl;
    }
  }
  for (int i = 0; i < model->postSynapseType.size(); i++) {
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      os << "  " << postSynModels[type].varNames[j] << model->synapseName[i] << " = new ";
      os << postSynModels[type].varTypes[j] << "[" << (model->neuronN[model->synapseTarget[i]]) <<  "];" << endl;
    }
  }
  os << "}" << endl;
  os << endl;


  //-----------------------------
  // allocate sparse array memory

  osh << "void allocateAllSparseArraysCuda" << deviceID << "();" << endl;
  os << "void allocateAllSparseArraysCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  for (int i = 0; i < model->synapseGrpN; i++) {
    if (model->synapseConnType[i] == SPARSE) {
      if (model->synapseGType[i] != GLOBALG) {
	os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_gp" << model->synapseName[i];
	os << ", " theSize(model->ftype) << " * g" << model->synapseName[i] << ".connN));" << endl;
      }
      os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_gp" << model->synapseName[i];
      os << "_ind, " << sizeof(unsigned int) << " * g" << model->synapseName[i] << ".connN));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMalloc((void **) &d_gp" << model->synapseName[i];
      os << "_indInG, " << sizeof(unsigned int) << " * (" << model->neuronN[model->synapseSource[i]];
      os << " + 1)));" << endl;
    }
  }
  os << "}" << endl; 
  os << endl;

  // ------------------------------------------------------------------------
  // allocating conductance arrays for sparse matrices
  /*
    for (int i= 0; i < model->synapseGrpN; i++) {
    if (model->synapseConnType[i] == SPARSE){
    os << "void allocateSparseArray" << model->synapseName[i] << "(unsigned int i, unsigned int gsize)" << endl; //i=synapse index
    os << "{" << endl;
    os << "  g" << model->synapseName[i] << ".gp= new " << model->ftype << "[gsize];" << endl; // synaptic conductances of group " << model->synapseName[i];
    //mem += gsize * theSize(model->ftype); //TODO: But we need to find a way
    os << "  g" << model->synapseName[i] << ".gIndInG= new unsigned int[";
    os << model->neuronN[model->synapseSource[i]] << "+ 1];"; // index where the next neuron starts in the synaptic conductances of group " << model->synapseName[i];
    os << endl;
    mem+= model->neuronN[model->synapseSource[i]]*sizeof(int);
    os << "  g" << model->synapseName[i] << ".gInd= new unsigned int[gsize];" << endl; // postsynaptic neuron index in the synaptic conductances of group " << model->synapseName[i];
    //mem += gsize * sizeof(int);
    //  }
    os << "  cudaMalloc((void **)&d_gp" << model->synapseName[i] << ", sizeof(" << model->ftype << ")*gsize);" << endl;
    os << "  cudaMalloc((void **)&d_gp" << model->synapseName[i] << "_ind, sizeof(unsigned int)*gsize);" << endl;
    os << "  cudaMalloc((void **)&d_gp" << model->synapseName[i] << "_indInG, sizeof(unsigned int)*("<< model->neuronN[model->synapseSource[i]] << "+1));" << endl;
    os << "}" << endl; 
    }}*/


  //-------------------------------
  // copying conductances to device

  osh << "void copyGToCuda" << deviceID << "();" << endl;
  os << "void copyGToCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  for (int i = 0; i < model->synapseGrpN; i++) {
    if (model->synapseGType[i] == INDIVIDUALG) {
      if (model->synapseConnType[i] == SPARSE){          
	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_gp" << model->synapseName[i] << ", g" << model->synapseName[i];
	os << ".gp, g" << model->synapseName[i] << ".connN * " << theSize(model->ftype);
	os << ", cudaMemcpyHostToDevice));" << endl;
      }
      else {
	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_gp" << model->synapseName[i] << ", gp" << model->synapseName[i];
      	os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	os << ", cudaMemcpyHostToDevice));" << endl;
      }
      if (model->synapseType[i] == LEARN1SYNAPSE) {
        os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_grawp" << model->synapseName[i]<< ", grawp" << model->synapseName[i];
	if (model->synapseConnType[i] == SPARSE) {
          os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	  os << ", cudaMemcpyHostToDevice));" << endl;}
	else {
          os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	  os << ", cudaMemcpyHostToDevice));" << endl;
	}
      } 
    }
    if (model->synapseGType[i] == INDIVIDUALID) {
      if (model->synapseConnType[i] == SPARSE) {
      	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_gp" << model->synapseName[i] << ", g" << model->synapseName[i] << ".gp, ";
      }
      else {
	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_gp" << model->synapseName[i] << ", gp" << model->synapseName[i] << ", ";
      }
      tmp = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]];
      size = (tmp >> logUIntSz);
      if (tmp > (size << logUIntSz)) size++;
      size = size * sizeof(unsigned int);
      os << ", " << size << ", cudaMemcpyHostToDevice));" << endl;
      if (model->synapseType[i] == LEARN1SYNAPSE) {
        os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_grawp" << model->synapseName[i]<< ", grawp" << model->synapseName[i];
	if (model->synapseConnType[i]==SPARSE) {
          os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	  os << ", cudaMemcpyHostToDevice));" << endl;
	}
	else {
          os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	  os << ", cudaMemcpyHostToDevice));" << endl;
	}
      }
    }
  }
  os << "}" << endl;
  os << endl;


  // ------------------------------------------------------------------------
  // copying explicit input(if any) to device

  osh << "void copyInpToDeviceCuda" << deviceID << "();" << endl;
  os << "void copyInpToDeviceCuda" << deviceID << "()" << endl;
  os << "{" << endl;
  os << "}" << endl;
  os << endl;


  // ------------------------------------------------------------------------
  // copying conductances from device

  osh << "void copyGFromCuda" << deviceID << "();" << endl;
  os << "void copyGFromCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  for (int i = 0; i < model->synapseGrpN; i++) {
    if (model->synapseGType[i] == INDIVIDUALG) {
      if (model->synapseConnType[i]==SPARSE){
	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(g" << model->synapseName[i] << ".gp, d_gp" << model->synapseName[i];
	os << ", g" << model->synapseName[i] << ".connN * " << theSize(model->ftype) << ", cudaMemcpyDeviceToHost));" << endl;
      }
      else {
	os << "  CHECK_CUDA_ERRORS(cudaMemcpy(gp" << model->synapseName[i] << ", d_gp" << model->synapseName[i];      
      	size = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	os << ", " << model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
	os << ", cudaMemcpyDeviceToHost));" << endl;
      }  
      if (model->synapseType[i] == LEARN1SYNAPSE) {
        os << "  CHECK_CUDA_ERRORS(cudaMemcpy(grawp" << model->synapseName[i]<< ", d_grawp" << model->synapseName[i];
        size = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
        os << ", " << size << ", cudaMemcpyDeviceToHost));" << endl;
      }
    }
    if (model->synapseGType[i] == INDIVIDUALID) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(gp" << model->synapseName[i] << ", d_gp" << model->synapseName[i];
      tmp = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]];
      size = (tmp >> logUIntSz);
      if (tmp > (size << logUIntSz)) size++;
      size = size * sizeof(unsigned int);
      os << ", " << size << ", cudaMemcpyDeviceToHost));" << endl;
      if (model->synapseType[i] == LEARN1SYNAPSE) {
        os << "  CHECK_CUDA_ERRORS(cudaMemcpy(grawp" << model->synapseName[i]<< ", d_grawp" << model->synapseName[i];
        size = model->neuronN[model->synapseSource[i]] * model->neuronN[model->synapseTarget[i]] * theSize(model->ftype);
        os << ", " << size << ", cudaMemcpyDeviceToHost));" << endl;
      }
    }
  }
  os << "}" << endl;
  os << endl;


  // ------------------------------------------------------------------------
  // copying values to device

  osh << "void copyStateToCuda" << deviceID << "();" << endl;
  os << "void copyStateToCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  os << "  void *devPtr;" << endl;
  os << "  unsigned int tmp = 0;" << endl;
  os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_done));" << endl;
  os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, &tmp, " << sizeof(int) << ", cudaMemcpyHostToDevice));" << endl;
  for (int i = 0; i < model->neuronGrpN; i++) {
    type = model->neuronType[i];
    if (model->neuronDelaySlots[i] != 1) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_spkEvntQuePtr" << model->neuronName[i] << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, &spkEvntQuePtr" << model->neuronName[i] << ", ";
      os << sizeof(unsigned int) << ", cudaMemcpyHostToDevice));" << endl;
    }
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbscnt" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, &glbscnt" << model->neuronName[i] << ", ";
    os << sizeof(unsigned int) << ", cudaMemcpyHostToDevice));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpkEvntCnt" << model->neuronName[i] << "));" << endl;
    if (model->neuronDelaySlots[i] == 1) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, &glbSpkEvntCnt" << model->neuronName[i] << ", ";
      os << sizeof(unsigned int);
    }
    else {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, glbSpkEvntCnt" << model->neuronName[i] << ", ";
      os = model->neuronDelaySlots[i] * sizeof(unsigned int);
    }
    os << ", cudaMemcpyHostToDevice));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpk" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, glbSpk" << model->neuronName[i] << ", ";
    os << model->neuronN[i] * sizeof(unsigned int) << ", cudaMemcpyHostToDevice));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpkEvnt" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, glbSpkEvnt" << model->neuronName[i] << ", ";
    if (model->neuronDelaySlots[i] != 1) {
      os << model->neuronN[i] * sizeof(unsigned int) * model->neuronDelaySlots[i];
    }
    else {
      os << model->neuronN[i] * sizeof(unsigned int);
    }
    os << ", cudaMemcpyHostToDevice));" << endl;      
    for (int j = 0; j < model->inSyn[i].size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_inSyn" << model->neuronName[i] << j << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, inSyn" << model->neuronName[i] << j << ", ";
      os << model->neuronN[i] * theSize(model->ftype) << ", cudaMemcpyHostToDevice));" << endl;
    }
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_" << nModels[type].varNames[j] << model->neuronName[i]<< ", ";
      os << nModels[type].varNames[j] << model->neuronName[i] << ", ";
      if ((nModels[type].varNames[j] == "V") && (model->neuronDelaySlots[i] != 1)) {
	os << model->neuronN[i] * model->neuronDelaySlots[i] * theSize(nModels[type].varTypes[j]);
      }
      else {
	os << model->neuronN[i] * theSize(nModels[type].varTypes[j]);
      }
      os << "), cudaMemcpyHostToDevice));" << endl;
    }
    if (model->neuronNeedSt[i]) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_sT" << model->neuronName[i] << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(devPtr, " << "sT" << model->neuronName[i] << ", ";
      os << model->neuronN[i] * theSize(model->ftype) << ", cudaMemcpyHostToDevice));" << endl;
    }
  }
  for (int i = 0; i < model->postSynapseType.size(); i++) {
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(d_" << postSynModels[type].varNames[j] << model->synapseName[i]<< ", ";
      os << postSynModels[type].varNames[j] << model->synapseName[i] << ", ";
      os << model->neuronN[model->synapseTarget[i]] * theSize(postSynModels[type].varTypes[j]);
      os << ", cudaMemcpyHostToDevice));" << endl;
    }
  }
  os << "}" << endl;
  os << endl;


  //---------------------------
  // copying values from device

  osh << "void copyStateFromCuda" << deviceID << "();" << endl;
  os << "void copyStateFromCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  os << "  void *devPtr;" << endl;
  for (int i = 0; i < model->neuronGrpN; i++) {
    type = model->neuronType[i];
    if (model->neuronDelaySlots[i] != 1) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_spkEvntQuePtr" << model->neuronName[i] << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(&spkEvntQuePtr" << model->neuronName[i] << ", devPtr, ";
      os << sizeof(unsigned int) << ", cudaMemcpyDeviceToHost));" << endl;
    }

    //glbscnt
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbscnt" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(&glbscnt" << model->neuronName[i] << ", devPtr, ";
    os << sizeof(unsigned int) << ", cudaMemcpyDeviceToHost));" << endl;

    //glbSpkEvntCnt
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpkEvntCnt" << model->neuronName[i] << "));" << endl;
    if (model->neuronDelaySlots[i] == 1) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(&glbSpkEvntCnt" << model->neuronName[i] << ", devPtr, ";
      os << sizeof(unsigned int);
    }
    else {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(glbSpkEvntCnt" << model->neuronName[i] << ", devPtr, ";
      os << model->neuronDelaySlots[i] * sizeof(unsigned int);
    }
    os << ", cudaMemcpyDeviceToHost));" << endl;

    //glbSpk
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpk" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(glbSpk" << model->neuronName[i] << ", devPtr, ";
    os << model->neuronN[i] * sizeof(unsigned int) << ", cudaMemcpyDeviceToHost));" << endl;

    //glbSpkEvnt
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpkEvnt" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(glbSpkEvnt" << model->neuronName[i] << ", devPtr, ";
    if (model->neuronDelaySlots[i] != 1) {
      os << model->neuronN[i] * sizeof(unsigned int) * model->neuronDelaySlots[i];
    }
    else {
      os << model->neuronN[i] * sizeof(unsigned int);
    }
    os << ", cudaMemcpyDeviceToHost));" << endl;
    for (int j = 0; j < model->inSyn[i].size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_inSyn" << model->neuronName[i] << j << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(inSyn" << model->neuronName[i] << j << ", devPtr, ";
      os << model->neuronN[i] * theSize(model->ftype) << ", cudaMemcpyDeviceToHost));" << endl;
    }
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(" << nModels[type].varNames[j] << model->neuronName[i] << ", ";
      os << "d_" << nModels[type].varNames[j] << model->neuronName[i] << ", ";
      if ((nModels[type].varNames[j] == "V") && (model->neuronDelaySlots[i] != 1)) {
	os << model->neuronN[i] * model->neuronDelaySlots[i] * theSize(nModels[type].varTypes[j]);
      }
      else {
	os << model->neuronN[i] * theSize(nModels[type].varTypes[j]);
      }
      os << "), cudaMemcpyDeviceToHost));" << endl;
    }
    if (model->neuronNeedSt[i]) {
      os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_sT" << model->neuronName[i] << "));" << endl;
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(sT" << model->neuronName[i] << ", devPtr, ";
      os << model->neuronN[i] * theSize(model->ftype) << ", cudaMemcpyDeviceToHost));" << endl;
    }
  }
  for (int i = 0; i < model->postSynapseType.size(); i++) {
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaMemcpy(" << postSynModels[type].varNames[j] << model->synapseName[i] << ", ";
      os << "d_" << postSynModels[type].varNames[j] << model->synapseName[i] << ", ";
      os << model->neuronN[model->synapseTarget[i]] * theSize(postSynModels[type].varTypes[j]);
      os << ", cudaMemcpyDeviceToHost));" << endl;
    }
  }
  os << "}" << endl;
  os << endl;


  // --------------------------
  // copying spikes from device                                            

  osh << "void copySpikesFromCuda" << deviceID << "();" << endl;
  os << "void copySpikesFromCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  os << "  void *devPtr;" << endl;
  for (int i = 0; i < model->neuronGrpN; i++) {
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbSpk" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(glbSpk" << model->neuronName[i] << ", devPtr, ";
    os << "glbscnt" << model->neuronName[i] << " * " << sizeof(unsigned int) << ", cudaMemcpyDeviceToHost));" << endl;
  }
  os << "}" << endl;
  os << endl;


  // ---------------------------------
  // copying spike numbers from device

  osh << "void copySpikeNFromCuda" << deviceID << "();" << endl;
  os << "void copySpikeNFromCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  os << "  void *devPtr;" << endl;
  for (int i = 0; i < model->neuronGrpN; i++) {
    os << "  CHECK_CUDA_ERRORS(cudaGetSymbolAddress(&devPtr, d_glbscnt" << model->neuronName[i] << "));" << endl;
    os << "  CHECK_CUDA_ERRORS(cudaMemcpy(&glbscnt" << model->neuronName[i] << ", devPtr, ";
    os << sizeof(unsigned int) << ", cudaMemcpyDeviceToHost));" << endl;
  }
  os << "}" << endl;
  os << endl;

 
  // ---------------------------------------------
  // free dynamically allocated cuda device memory

  osh << "void freeMemCuda" << deviceID << "();" << endl;
  os << "void freeMemCuda" << deviceID << "()" << endl;
  os << "{" << endl;

  for (int i = 0; i < model->neuronGrpN; i++) {
    type = model->neuronType[i];
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaFree(d_" << nModels[type].varNames[j] << model->neuronName[i] << "));" << endl;
    }
  }
  for (int i = 0; i < model->synapseGrpN; i++) {
    if ((model->synapseGType[i] == (INDIVIDUALG)) || (model->synapseGType[i] == INDIVIDUALID)) {
      os << "  CHECK_CUDA_ERRORS(cudaFree(d_gp" << model->synapseName[i] << "));" << endl;  	
    }
    if (model->synapseType[i] == LEARN1SYNAPSE) {
      os << "  CHECK_CUDA_ERRORS(cudaFree(d_grawp"  << model->synapseName[i] << "));" << endl;	
    }
  }
  for (int i = 0; i < model->postSynapseType.size(); i++) {
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      os << "  CHECK_CUDA_ERRORS(cudaFree(d_" << postSynModels[type].varNames[j] << model->synapseName[i] << "));" << endl;
    }
  }
  os << "}" << endl;
  os << endl;


  //--------------------------
  // learning helper functions

  if (model->lrnGroups > 0) {
    for (int i = 0; i < model->synapseGrpN; i++) {
      if (model->synapseType[i] == LEARN1SYNAPSE) {

	osh << "__device__ " << model->ftype << " gFunc" << model->synapseName[i] << "Cuda" << deviceID;
	osh << "(" << model->ftype << " graw);" << endl;
	os << "__device__ " << model->ftype << " gFunc" << model->synapseName[i] << "Cuda" << deviceID;
	os << "(" << model->ftype << " graw)" << endl;
	os << "{" << endl;
	os << "  return " << SAVEP(model->synapsePara[i][8]/2.0) << " * (tanh(";
	os << SAVEP(model->synapsePara[i][10]) << " * (graw - ";
	os << SAVEP(model->synapsePara[i][9]) << ")) + 1.0);" << endl;
	os << "}" << endl;
	os << endl;

	osh << "__device__ " << model->ftype << " invGFunc" << model->synapseName[i] << "Cuda" << deviceID;
	osh << "(" << model->ftype << " g);" << endl;
	os << "__device__ " << model->ftype << " invGFunc" << model->synapseName[i] << "Cuda" << deviceID;
	os<< "(" << model->ftype << " g)" << endl;
	os << "{" << endl;
	os << model->ftype << " tmp = g / " << SAVEP(model->synapsePara[i][8]*2.0) << " - 1.0;" << endl;
	os << "return 0.5 * log((1.0 + tmp) / (1.0 - tmp)) /" << SAVEP(model->synapsePara[i][10]);
	os << " + " << SAVEP(model->synapsePara[i][9]) << ";" << endl;
	os << "}" << endl;
	os << endl;
      }
    }
  }


  // ------------------------------------------------------------------------
  // the actual time stepping procedure

  osh << "void stepTimeCuda" << deviceID << "(";
  os << "void stepTimeCuda" << deviceID << "(";
  for (int i = 0; i < model->neuronGrpN; i++) {
    if (model->neuronType[i] == POISSONNEURON) {
      osh << "unsigned int *rates" << model->neuronName[i] << ", ";
      os << "unsigned int *rates" << model->neuronName[i];
      os << ", // pointer to the rates of the Poisson neurons in grp " << model->neuronName[i] << endl;
      osh << "unsigned int offset" << model->neuronName[i] << ", ";
      os << "unsigned int offset" << model->neuronName[i];
      os << ", // offset on pointer to the rates in grp " << model->neuronName[i] << endl;
    }
    if (model->receivesInputCurrent[i] >= 2) {
      osh << "float *d_inputI" << model->neuronName[i] << ", ";
      os << "float *d_inputI" << model->neuronName[i];
      os << ", // Explicit input to the neurons in grp " << model->neuronName[i] << endl;
    }
  }
  osh << "float t);" << endl;
  os << "float t)" << endl;
  os << "{" << endl;

  if (model->synapseGrpN > 0) {
    os << "  if (t > 0.0) {" << endl; 
    os << "    calcSynapsesCuda" << deviceID << " <<< sGridCuda" << deviceID << ", sThreadsCuda" << deviceID << " >>> (";
    for (int i = 0; i < model->synapseGrpN; i++) {
      if (model->synapseGType[i] == INDIVIDUALG) {
	os << "d_gp" << model->synapseName[i] << ", ";
      }
      if (model->synapseConnType[i] == SPARSE) {
	os << "d_gp" << model->synapseName[i] << "_ind, ";	
	os << "d_gp" << model->synapseName[i] << "_indInG, ";
      }
      if (model->synapseGType[i] == INDIVIDUALID) {
	os << "d_gp" << model->synapseName[i] << ", ";	
      }
      if (model->synapseType[i] == LEARN1SYNAPSE) {
	os << "d_grawp"  << model->synapseName[i] << ", ";
      }
    }
    for (int i = 0; i < model->neuronGrpN; i++) {
      type = model->neuronType[i];
      os << " d_" << nModels[type].varNames[0] << model->neuronName[i]; // this is supposed to be Vm
      if (model->needSt || i < (model->neuronGrpN - 1)) {
	os << ", ";
      }    		
    }
    if (model->needSt) {
      os << "t);" << endl;
    }
    else {
      os << ");" << endl;
    }
    if (model->lrnGroups > 0) {
      os << "    learnSynapsesPostCuda" << deviceID << " <<< lGridCuda" << deviceID << ", lThreadsCuda" << deviceID << " >>> (";      
      for (int i = 0; i < model->synapseGrpN; i++) {
	if ((model->synapseGType[i] == INDIVIDUALG) || (model->synapseGType[i] == INDIVIDUALID)) {
	  os << " d_gp" << model->synapseName[i] << ", ";
	}
	if (model->synapseType[i] == LEARN1SYNAPSE) {
	  os << "d_grawp"  << model->synapseName[i] << ", ";
	}
      }
      for (int i = 0; i < model->neuronGrpN; i++) {
	type = model->neuronType[i];
	os << " d_" << nModels[type].varNames[0] << model->neuronName[i] << ", "; // this is supposed to be Vm
      }
      os << "t);" << endl;
    }
    os << "  }" << endl;
  }
  os << "  calcNeuronsCuda" << deviceID << " <<< nGridCuda" << deviceID << ", nThreadsCuda" << deviceID << " >>> (";
  for (int i = 0; i < model->neuronGrpN; i++) {
    type = model->neuronType[i];
    if (type == POISSONNEURON) {
      os << "rates" << model->neuronName[i] << ", ";
      os << "offset" << model->neuronName[i] << ", ";
    }
    if (model->receivesInputCurrent[i] >= 2) {
      os << "d_inputI" << model->neuronName[i] << ", ";
    }
    for (int j = 0; j < nModels[type].varNames.size(); j++) {
      os << " d_" << nModels[type].varNames[j] << model->neuronName[i]<< ", ";
    }
  }
  for (int i = 0; i < model->postSynapseType.size(); i++) {
    type = model->postSynapseType[i];
    for (int j = 0; j < postSynModels[type].varNames.size(); j++) {
      os << " d_" << postSynModels[type].varNames[j];
      os << model->synapseName[i] << ", ";
    }
  }
  os << "t);" << endl;
  os << "}" << endl;

  osh.close();
  os.close();
}
