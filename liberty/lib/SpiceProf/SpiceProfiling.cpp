#define DEBUG_TYPE "SpiceProf"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/CallSite.h"

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/Analysis/LoopPass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/Passes.h"

#include "liberty/Utilities/GlobalCtors.h"
#include "liberty/Utilities/InstInsertPt.h"
#include "liberty/Utilities/SplitEdge.h"

#include <iostream>
#include <fstream>
#include <set>
#include <sstream>
#include <list>

using namespace llvm;
using namespace std;
using namespace liberty;

namespace
{
  class SpiceProf : public ModulePass
  {
    bool runOnModule(Module& M);
    bool runOnLoop(Loop *lp);

    int numLoops;

    public:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const
    {
      AU.addRequired<LoopInfoWrapperPass>();
      AU.setPreservesAll();
    }

    StringRef getPassName() const { return "SpiceProf"; }
    void *getAdjustedAnalysisPointer(AnalysisID PI) { return this; }

    static char ID;
    SpiceProf() : ModulePass(ID) {}
  };
}

char SpiceProf::ID = 0;
static RegisterPass<SpiceProf> RP10("spice-prof", "Insert spice profiling instrumentation", false, false);


void getFunctionExits(Function &F,set<BasicBlock*> &bbSet)
{
  for(Function::iterator bbit = F.begin(), end = F.end();
      bbit != end; ++bbit)
  {
    BasicBlock *bb = &*bbit;
    if( isa<ReturnInst>(bb->getTerminator())
    ||  isa<ResumeInst>(bb->getTerminator()) )
    {
      bbSet.insert(bb);
    }
  }
}

bool SpiceProf::runOnLoop(Loop *Lp) {
  assert(Lp->isLoopSimplifyForm() && "did not run loop simplify\n");

  //get preheader and header
  BasicBlock *preHeader = Lp->getLoopPreheader();
  BasicBlock *header = Lp->getHeader();

  //get Module
  Module *M = (header->getParent())->getParent();


  //get all the exit blocks
  SmallVector<BasicBlock*, 16> exitBlocks;
  Lp->getExitBlocks(exitBlocks);

  //get all the PHINodes in header
  SmallVector<PHINode*, 16> headerPHIs;
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
    if (isa<PHINode>(ii)) {
      headerPHIs.push_back((PHINode*) &*ii);
      LLVM_DEBUG( errs() << "inst: " << *ii << " is a phi\n");
    }
    else break;
  }

  // insert bit cast of headerphis in the successor of header
  BasicBlock* latch = Lp->getLoopLatch();
  for(BasicBlock::iterator ii = latch->begin(), ie = latch->end(); ii != ie; ++ii){
    if( !isa<PHINode>(ii) ){
      for(auto &phi : headerPHIs)
      {
        Constant* phiV = dyn_cast<Constant>(phi);
        const Twine bitcastName = "scast." + phi->getName();
        Type* int8ptrT = Type::getInt8PtrTy(M->getContext());
        CastInst* phiCast = CastInst::CreateBitOrPointerCast(phi, int8ptrT, bitcastName, &*ii);
      }
      break;
    }
  }

  // insert spice invocation function at end of preheader (called once prior to loop)
  const char* InvocName = "__spice_start_invocation";
  FunctionCallee wrapper =  M->getOrInsertFunction(InvocName,
      Type::getVoidTy(M->getContext()), Type::getInt32Ty(M->getContext()));
  Constant *InvocFn = cast<Constant>(wrapper.getCallee());
  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M->getContext()), numLoops);
  //assert(preHeader && "Null preHeader -- Did you run loopsimplify?");
  if (!preHeader->empty())
    CallInst::Create(InvocFn, Args, "", (preHeader->getTerminator()));
  else
    CallInst::Create(InvocFn, Args, "", (preHeader));


  //debugs
  LLVM_DEBUG( errs() << "Loop with preheader " << preHeader->getName() << ": " << numLoops << "\n" );
  LLVM_DEBUG( errs() << "Exit blocks for loop with preheader " << preHeader->getName() << ":\n" );
  for ( auto bb : exitBlocks )
    LLVM_DEBUG( errs() << "\t" << bb->getName() << "\n" );
  LLVM_DEBUG( errs() << *Lp );
  LLVM_DEBUG( errs() << "\n" );

  /*
  // insert iteration begin function at beginning of header (called each loop)
  const char* IterBeginName = "LAMP_loop_iteration_begin";
  Constant *IterBeginFn = M->getOrInsertFunction(IterBeginName, Type::getVoidTy(M->getContext()), (Type *)0);

  // find insertion point (after PHI nodes) -KF 11/18/2008
  for (BasicBlock::iterator ii = header->begin(), ie = header->end(); ii != ie; ++ii) {
  if (!isa<PHINode>(ii)) {
  CallInst::Create(IterBeginFn, "", ii);
  break;
  }
  }

  // insert iteration at cannonical backedge.  exiting block insertions removed in favor of exit block
  const char* IterEndName = "LAMP_loop_iteration_end";
  Constant *IterEndFn = M->getOrInsertFunction(IterEndName, Type::getVoidTy(M->getContext()), (Type *)0);

  // cannonical backedge
  if (!latch->empty())
  CallInst::Create(IterEndFn, "", (latch->getTerminator()));
  else
  CallInst::Create(IterEndFn, "", (latch));
  */

  // insert end invocation at beginning of exit blocks
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_void_void = FunctionType::get(
      /*Result=*/Type::getVoidTy(M->getContext()),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  FunctionCallee wrapper_LoopEndFn = M->getOrInsertFunction("__spice_end_invocation", FuncTy_void_void);
  Constant *LoopEndFn = cast<Constant>(wrapper_LoopEndFn.getCallee());

  set <BasicBlock*> BBSet;
  BBSet.clear();
  for(unsigned int i = 0; i != exitBlocks.size(); i++){
    if (BBSet.find(exitBlocks[i])!=BBSet.end())
      continue;
    BBSet.insert(exitBlocks[i]);
    BasicBlock::iterator ii = exitBlocks[i]->getFirstInsertionPt();
    while (isa<PHINode>(ii)) { ii++; }
    CallInst::Create(LoopEndFn, "", &*ii);
  }

  return true;
}


bool SpiceProf::runOnModule(Module& M)
{
  numLoops = 0;

  // Go through and instrument each loop,
  for(Module::iterator IF = M.begin(), E = M.end(); IF != E; ++IF)
  {
    Function &F = *IF;
    if(F.isDeclaration())
      continue;

    // First collect all call sites in this function, before we add more
    // and mess-up our iterators.
    /*typedef std::vector<Instruction*> Calls;
    Calls calls;
    for(Function::iterator i=F.begin(), e=F.end(); i!=e; ++i)
      for(BasicBlock::iterator j=i->begin(), z=i->end(); j!=z; ++j)
      {
        Instruction *inst = &*j;

        if( CallBase  *call = dyn_cast<CallBase>(inst) ){
          calls.push_back(inst);
        }
      }*/
    /* Using the same structures as loops to track time spent in functions
     * This should probably be done a different way
     **/
   // ++numLoops;
   // const char* InvocName = "loopProf_invocation";
   // FunctionCallee wrapper_Invoc =  M.getOrInsertFunction(InvocName,
   //     Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
   // Constant *InvocFn = cast<Constant>(wrapper_Invoc.getCallee());
   // std::vector<Value*> Args(1);
   // Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops);
   // CallInst::Create(InvocFn, Args, "", F.getEntryBlock().getFirstNonPHI() );
   // LLVM_DEBUG( errs() << "Function " << IF->getName() << ": " << numLoops << "\n" );

  //  const char* LoopEndName = "loop_exit";
  //  FunctionCallee wrapper_LoopEndFn= M.getOrInsertFunction(LoopEndName,
  //    Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()));
  //  Constant *LoopEndFn=cast<Constant>(wrapper_LoopEndFn.getCallee());
      //Type::getVoidTy(M.getContext()), Type::getInt32Ty(M.getContext()), (Type *)0);
  //  set <BasicBlock *> BBSet;
  //  BBSet.clear();

 //   getFunctionExits(F,BBSet);
 //   for(set<BasicBlock*>::iterator it = BBSet.begin(), end = BBSet.end();
 //       it != end; ++it)
 //   {
 //     BasicBlock *bb = *it;
 //     BasicBlock::iterator bbit = bb->end();
 //     --bbit; //--bbit;
 //     Instruction *inst = &*bbit;
 //     CallInst::Create(LoopEndFn, Args, "", inst);
 //   }

    // Finished inserting calls for function, now handle its loops
    LoopInfo &li = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();

    //get a list of highest level loops
    list<Loop*> loops( li.begin(), li.end() );
    while( !loops.empty() )
    {

      Loop *loop = loops.front();
      loops.pop_front();

      runOnLoop(loop);

      loops.insert( loops.end(),
          loop->getSubLoops().begin(),
          loop->getSubLoops().end());

      ++numLoops; // calls start with integer 0
    }
  }

    // Instrument every callsite within this function
    /*for(Calls::const_iterator i=calls.begin(), e=calls.end(); i!=e; ++i)
    {
      Instruction *inst = *i;

      ++numLoops;

      // Ziyang: need to ignore all llvm.* instrinsics

      Function *fcn = dyn_cast<CallBase>(inst)->getCalledFunction();
      if (fcn){ // the other case is indirect call
        if (fcn->getName().startswith("llvm."))
          continue;
      }

      Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops );
      InstInsertPt::Before(inst) << CallInst::Create(InvocFn, Args);

      if( InvokeInst *invoke = dyn_cast<InvokeInst>(inst) )
      {
        // Two successors: normal vs unwind
        // normal
        BasicBlock *upon_normal = split(invoke->getParent(), invoke->getNormalDest(), "after.invoke.normal.");
        InstInsertPt::Beginning(upon_normal) << CallInst::Create(LoopEndFn,Args);

        // Unwind
        BasicBlock *upon_exception = split(invoke->getParent(), invoke->getUnwindDest(), "after.invoke.exception.");
        InstInsertPt::Beginning(upon_exception) << CallInst::Create(LoopEndFn,Args);
        LLVM_DEBUG( errs() << "Invokesite " << *inst << ": " << numLoops << "\n" );
      }
      else
      {
        // CallInst
        InstInsertPt::After(inst) << CallInst::Create(LoopEndFn, Args);
        LLVM_DEBUG( errs() << "Callsite " << *inst << ": " << numLoops << "\n" );
      }

    }
  }*/

  /*FunctionCallee wrapper_InitFn =  M.getOrInsertFunction("loopProfInit",
      Type::getVoidTy(M.getContext()),
      Type::getInt32Ty(M.getContext()));

  Constant *InitFn = cast<Constant>(wrapper_InitFn.getCallee());
      //sot
      //(Type *)0);

  std::vector<Value*> Args(1);
  Args[0] = ConstantInt::get(Type::getInt32Ty(M.getContext()), numLoops, false);
*/
  // Create the GlobalCtor function
  std::vector<Type*>FuncTy_0_args;
  FunctionType* FuncTy_void_void = FunctionType::get(
      /*Result=*/Type::getVoidTy( M.getContext() ),
      /*Params=*/FuncTy_0_args,
      /*isVarArg=*/false);

  Function* func_printall = Function::Create(
      /*FunctionType=*/FuncTy_void_void,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Name=*/"__spice_profile_printAll", &M);

  //BasicBlock *printall_entry = BasicBlock::Create(M.getContext(), "entry", func_printall,0);
  //CallInst::Create(InitFn, Args, "", initor_entry);
  //ReturnInst::Create(M.getContext(), initor_entry);

  // Function has been created, now add it to the global ctor list
  callAfterMain(func_printall, 0);


  return false;
}






#if 0

bool isaLAMP(Instruction *inst)
{
  if(isa<CallInst>(inst) || isa<InvokeInst>(inst))
  {
    CallSite call = CallSite(inst);
    Function *f = call.getCalledFunction();
    if( f != NULL)
    {
      std::string cname = f->getNameStr();
      if( cname.find("LAMP") != std::string::npos)
      {
        return true;
      }
    }
  }
  return false;
}


#endif

#undef DEBUG_TYPE
