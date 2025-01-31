#define DEBUG_TYPE "selector"

#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#include "liberty/LoopProf/LoopProfLoad.h"
#include "liberty/LoopProf/Targets.h"
#include "scaf/SpeculationModules/EdgeCountOracleAA.h"
#include "liberty/Speculation/CallsiteDepthCombinator_CtrlSpecAware.h"
#include "liberty/Speculation/ControlSpeculator.h"
#include "liberty/Speculation/KillFlow_CtrlSpecAware.h"
#include "liberty/Speculation/PredictionSpeculator.h"
#include "liberty/Strategy/ProfilePerformanceEstimator.h"
#include "liberty/Speculation/SmtxManager.h"
#include "liberty/Speculation/PtrResidueManager.h"
#include "scaf/Utilities/CallSiteFactory.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/SpeculationModules/GlobalConfig.h"
#include "scaf/Utilities/ReportDump.h"

#include "liberty/Speculation/RemedSelector.h"

namespace liberty
{
using namespace llvm::noelle;
namespace SpecPriv
{

void RemedSelector::getAnalysisUsage(AnalysisUsage &au) const
{
  Selector::analysisUsage(au);

  au.addRequired<LoopAA>();

  if (EnableLamp) {
    au.addRequired< SmtxSpeculationManager >();
    au.addRequired< LAMPLoadProfile >();
  }

  if (EnableEdgeProf) {
    au.addRequired< ProfileGuidedControlSpeculator >();
    //au.addRequired<KillFlow_CtrlSpecAware>();
    //au.addRequired<CallsiteDepthCombinator_CtrlSpecAware>();
  }


  if (EnableSpecPriv) {
    au.addRequired<ReadPass>();
    au.addRequired<Classify>();
    au.addRequired< ProfileGuidedPredictionSpeculator >();
    au.addRequired< PtrResidueSpeculationManager >();
  }
  au.addRequired<CallGraphWrapperPass>();
}

bool RemedSelector::runOnModule(Module &mod)
{
  DEBUG_WITH_TYPE("classify",
    errs() << "#################################################\n"
           << "Remed Selection\n\n\n");

  Vertices vertices;
  Edges edges;
  VertexWeights weights;
  VertexSet maxClique;

  if (!doSelection(vertices, edges, weights, maxClique))
    return false;

 // SpecPriv is not alwqys available
  // Combine all of these assignments into one big assignment
  auto *classify = getAnalysisIfAvailable< Classify >();
  if (!classify) { 
    errs() << "SpecPriv not available\n";
  }
  else {
    for(VertexSet::iterator i=maxClique.begin(), e=maxClique.end(); i!=e; ++i)
    {
      const unsigned v = *i;
      Loop *l = vertices[ v ];
      const HeapAssignment &asgn = classify->getAssignmentFor(l);
      assert( asgn.isValidFor(l) );

      assignment = assignment & asgn;
    }

    DEBUG_WITH_TYPE("classify", errs() << assignment );
  }
  return false;
}

const HeapAssignment &RemedSelector::getAssignment() const { return assignment; }
HeapAssignment &RemedSelector::getAssignment() { return assignment; }

void RemedSelector::computeVertices(Vertices &vertices)
{
  ModuleLoops &mloops = getAnalysis<ModuleLoops>();
  const Targets &targets = getAnalysis<Targets>();
  //const Classify &classify = getAnalysis<Classify>();
  for (Targets::iterator i = targets.begin(mloops), e = targets.end(mloops);
       i != e; ++i) {
    Loop *loop = *i;

    /*
     *const HeapAssignment &asgn = classify.getAssignmentFor(loop);
     *if (!asgn.isValidFor(loop)) {
     *  LLVM_DEBUG(errs() << "HeapAssignment invalid for loop "
     *               << loop->getHeader()->getParent()->getName()
     *               << "::" << loop->getHeader()->getName() << '\n');
     *  continue;
     *}
     */

    vertices.push_back(loop);
  }
}

void RemedSelector::resetAfterInline(
  Instruction *callsite_no_longer_exists,
  Function *caller,
  Function *callee,
  const ValueToValueMapTy &vmap,
  const CallsPromotedToInvoke &call2invoke)
{
  Classify &classify = getAnalysis< Classify >();
  ControlSpeculation *ctrlspec = getAnalysis< ProfileGuidedControlSpeculator >().getControlSpecPtr();
  ProfileGuidedPredictionSpeculator &predspec = getAnalysis< ProfileGuidedPredictionSpeculator >();
  LAMPLoadProfile &lampprof = getAnalysis< LAMPLoadProfile >();
  Read &spresults = getAnalysis< ReadPass >().getProfileInfo();

  UpdateLAMP lamp( lampprof );

  UpdateGroup group;
  group.add( &spresults );
  group.add( &classify );

  FoldManager &fmgr = * spresults.getFoldManager();

  // Hard to identify exactly which context we're updating,
  // since the context includes loops and functions, but not callsites.

  // Find every context in which 'callee' is called by 'caller'
  typedef std::vector<const Ctx *> Ctxs;
  Ctxs affectedContexts;
  for(FoldManager::ctx_iterator k=fmgr.ctx_begin(), z=fmgr.ctx_end(); k!=z; ++k)
  {
    const Ctx *ctx = &*k;
    if( ctx->type != Ctx_Fcn )
      continue;
    if( ctx->fcn != callee )
      continue;

    if( !ctx->parent )
      continue;
    if( ctx->parent->getFcn() != caller )
      continue;

    affectedContexts.push_back( ctx );
    errs() << "Affected context: " << *ctx << '\n';
  }

  // Inline those contexts to build the cmap, amap
  CtxToCtxMap cmap;
  AuToAuMap amap;
  for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
    fmgr.inlineContext(*k,vmap,cmap,amap);

  lamp.resetAfterInline(callsite_no_longer_exists, caller, callee, vmap, call2invoke);

  // Update others w.r.t each of those contexts
  for(Ctxs::const_iterator k=affectedContexts.begin(), z=affectedContexts.end(); k!=z; ++k)
    group.contextRenamedViaClone(*k,vmap,cmap,amap);

  spresults.removeInstruction( callsite_no_longer_exists );

  predspec.reset();
  ctrlspec->reset();
}

void RemedSelector::contextRenamedViaClone(
  const Ctx *changedContext,
  const ValueToValueMapTy &vmap,
  const CtxToCtxMap &cmap,
  const AuToAuMap &amap)
{
//  errs() << "  . . - Selector::contextRenamedViaClone: " << *changedContext << '\n';
  assignment.contextRenamedViaClone(changedContext,vmap,cmap,amap);
  Selector::contextRenamedViaClone(changedContext,vmap,cmap,amap);
}

bool RemedSelector::compatibleParallelizations(const Loop *A, const Loop *B) const
{
  auto *classify = getAnalysisIfAvailable< Classify >();
  if (!classify) {
    return Selector::compatibleParallelizations(A, B);
  }
  else {

    const HeapAssignment &asgnA = classify->getAssignmentFor(A);
    if ( !asgnA.isValidFor(A) ) {
     REPORT_DUMP(errs() << "HeapAssignment invalid for loop "
                  << A->getHeader()->getParent()->getName()
                  << "::" << A->getHeader()->getName() << '\n');
      return true;
    }

    const HeapAssignment &asgnB = classify->getAssignmentFor(B);
    if ( !asgnB.isValidFor(B) ) {
      REPORT_DUMP(errs() << "HeapAssignment invalid for loop "
                  << B->getHeader()->getParent()->getName()
                  << "::" << B->getHeader()->getName() << '\n');
      return true;
    }

    return compatible(asgnA, asgnB);
  }
}

char RemedSelector::ID = 0;
static RegisterPass< RemedSelector > rp("remed-selector", "Remediator Selector");
static RegisterAnalysisGroup< Selector > link(rp);

}
}
