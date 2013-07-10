#include "asg_gfdm.h"

#include "state.hh"
#include "asg_sampler.hh"
#include "seamx_headers.hh"
#include "iwop.hh"
#include "samp.hh"
#include "sim.hh"
#include "pol.hh"

#include "blockop.hh"
#include "parserpp.hh"

#include "CPsim.hh"

#ifdef IWAVE_USE_MPI
#include "mpisegypp.hh"
#include "mpigridpp.hh"
#else
#include "segypp.hh"
#include "gridpp.hh"
#endif
#include "segyops.hh"
#include "gridops.hh"

using TSOpt::IWaveEnvironment;
using TSOpt::IWaveState;
using TSOpt::IWaveLinState;
using TSOpt::IWaveStep;
using TSOpt::IWaveStaticInit;
using TSOpt::IWaveDynamicInit;
using TSOpt::IWaveOp;
using TSOpt::Sampler;
using TSOpt::LinSampler;
using TSOpt::Sim;
using TSOpt::StdSim;
using TSOpt::StdRCSim;
using TSOpt::StdSimData;

using TSOpt::CPSim;

#ifdef IWAVE_USE_MPI
using TSOpt::MPIGridSpace;
using TSOpt::MPISEGYSpace;
#else
using TSOpt::GridSpace;
using TSOpt::SEGYSpace;
#endif

using TSOpt::SEGYLinMute;
using TSOpt::GridWindowOp;

using TSOpt::OpNewCreatePolicy;
using TSOpt::PolicyBase;
using RVL::StdProductSpace;
using RVL::Vector;
using RVL::Components;
using RVL::LinearOp;
using RVL::LinearOpFO;
using RVL::Operator;
using RVL::OpComp;
using RVL::OpFO;
using RVL::InjectOp;
using RVL::LNLOperator;
using RVL::OperatorEvaluation;
using RVL::DerivEvaluation;
using RVL::RVLException;
using RVL::AssignFilename;
using RVL::AssignParams;
using RVL::parse;
using RVL::parse_except;

namespace ASG{

  typedef OpNewCreatePolicy<IWaveState,ASGSampler> ASGSamplerPolicy;
  typedef OpNewCreatePolicy<IWaveLinState,ASGLinSampler> ASGLinSamplerPolicy;

  /* dummy sampler policies to fill in the rest of the list */
  // class LinSamplerPolicy: public PolicyBase<IWaveState,LinSampler<IWaveLinState> > {};
  class AdjSamplerPolicy: public PolicyBase<IWaveState,LinSampler > {};
  
  typedef OpNewCreatePolicy<StdSimData<IWaveState>, StdSim<IWaveState> > StdIWavePolicy;

  typedef OpNewCreatePolicy<StdSimData<IWaveState>, StdRCSim<IWaveState> > StdRCIWavePolicy;
  //typedef OpNewCreatePolicy<StdSimData<IWaveState>, StdSim<IWaveState> > LinFwdSimPolicy;

  typedef OpNewCreatePolicy<StdSimData<IWaveState>, CPSim<IWaveState,TSIndex> > FwdCPSimPolicy;
 
  typedef OpNewCreatePolicy<StdSimData<IWaveLinState>, StdSim<IWaveLinState> > LinSimPolicy;
   
  /* dummy sim policies to fill in the rest of the list */
  //class LinFwdSimPolicy: public PolicyBase<StdSimData<IWaveState>, StdSim<IWaveState> > {};
  //class LinSimPolicy: public PolicyBase<StdSimData<IWaveLinState>, StdSim<IWaveLinState> > {};
  class AdjFwdSimPolicy: public PolicyBase<StdSimData<IWaveState>, StdSim<IWaveState> > {};
  class AdjSimPolicy: public PolicyBase<StdSimData<IWaveLinState>, StdSim<IWaveLinState> > {};
}

char ** xargv;

int main(int argc, char ** argv) {

  using namespace ASG;
  try { 

    // put there to avoid forgetting
    if (argc<2) { 
      cerr<<"asgblin: linearized fwd map\n";
      cerr<<"usage: asgblin.x par=<par file>\n";
      exit(0);
    }

    /* set up execution environment */
    int ts=0;
#ifdef IWAVE_USE_MPI
    MPI_Init_thread(&argc,&argv,MPI_THREAD_FUNNELED,&ts);
#endif

    PARARRAY * pars = NULL;
    FILE * stream = NULL;
    IWaveEnvironment(argc,argv,ts,&pars,&stream);

#ifdef IWAVE_USE_MPI
    if (retrieveGroupID() == MPI_UNDEFINED) {
      fprintf(stream,"NOTE: finalize MPI, cleanup, exit\n");
    }
    else {

#endif
      if (retrieveRank()==0) { 
	cerr<<"IWAVE++::ASG forward linearized map, bulkmod only\n"; 
      }

      /*********************************************************
       *             PARAMETER EXTRACTION                      *
       *********************************************************/

      /* files for acoustic params */
      ps_printall(*pars,stream);
      string bulkname = "";
      string buoyname = "";
      string dbulkname = "";
      string hdrname = "";
      string trcname = "";
      parse_except<string>(*pars,"bulkmod",bulkname);
      parse_except<string>(*pars,"buoyancy",buoyname);
      parse_except<string>(*pars,"dbulkmod",dbulkname);
      parse_except<string>(*pars,"datafile",trcname);
      parse_except<string>(*pars,"hdrfile",hdrname);

      // assign window widths - default = 0;
      RPNT w;
      RASN(w,RPNT_0);
      parse<float>(*pars,"ww1",w[0]);
      parse<float>(*pars,"ww2",w[1]);
      parse<float>(*pars,"ww3",w[2]);

      float sm=0.0f;
      float wm=0.0f;
      float tm=0.0f;
      parse<float>(*pars,"mute_slope",sm);
      parse<float>(*pars,"mute_zotime",tm);
      parse<float>(*pars,"mute_width",wm);

      /*********************************************************
       *               INPUT CONSTRUCTION                      *
       *********************************************************/
      
#ifdef IWAVE_USE_MPI
      MPIGridSpace m1sp(bulkname, "bulkmod");
      MPIGridSpace dm1sp(dbulkname, "dbulkmod");
      MPIGridSpace m2sp(buoyname, "buoyancy");
#else
      GridSpace m1sp(bulkname, "bulkmod");
      GridSpace dm1sp(dbulkname, "dbulkmod");
      GridSpace m2sp(buoyname, "buoyancy");
#endif

      StdProductSpace<float> msp(m1sp,m2sp);
      Vector<float> x(msp);
      Components<float> cx(x);

      Vector<float> dx(dm1sp);
   
      /* make SEGY space and vector */
#ifdef IWAVE_USE_MPI
      MPISEGYSpace tsp(hdrname);
#else
      SEGYSpace tsp(hdrname);
#endif
    
      Vector<float> dy(tsp);

      /* assign files */
      AssignFilename m1fn(bulkname);
      AssignFilename m2fn(buoyname);
      cx[0].eval(m1fn);
      cx[1].eval(m2fn);

      AssignFilename tfn(trcname);
      dy.eval(tfn);

      AssignFilename dm1fn(dbulkname);
      dx.eval(dm1fn);
    
      /*********************************************************
       *     OPERATOR CONSTRUCCTION, EVALUATION                *
       *********************************************************/

      /* create operator without mute, window */
      IWaveOp<
      ASGSamplerPolicy,
	ASGLinSamplerPolicy,
	AdjSamplerPolicy,
	StdIWavePolicy,
	StdRCIWavePolicy, //FwdCPSimPolicy
	LinSimPolicy,
	AdjFwdSimPolicy,
	AdjSimPolicy
	> 
	iwop(msp,tsp,*pars,stream,asg_gfdm);

      GridWindowOp wop(dm1sp,cx[0],w);

      InjectOp<float> inj(x,0);

      SEGYLinMute mute(sm,tm,wm);
      LinearOpFO<float> mop(tsp,tsp,mute,mute);

      OpComp<float> op(wop,inj,iwop,mop);

      Vector<float> x0(op.getDomain());
      x0.zero();

      OperatorEvaluation<float> opeval(op,x0);

      /* evaluate derivative */    
      opeval.getDeriv().applyOp(dx,dy);

      /*********************************************************
       *                    CLEANUP                            *
       *********************************************************/
    
      iwave_fdestroy();

#ifdef IWAVE_USE_MPI
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
#endif

  }
  catch (RVLException & e) {
    e<<"asgblin.x: ABORT\n";
    if (!retrieveGlobalRank()) e.write(cerr);
#ifdef IWAVE_USE_MPI
    MPI_Abort(MPI_COMM_WORLD,0);
#endif
    exit(1);
  }
}

