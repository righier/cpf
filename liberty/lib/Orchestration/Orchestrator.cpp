#include "liberty/Speculation/Classify.h"
#define DEBUG_TYPE "orchestrator"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/ValueMap.h"

#include "liberty/Orchestration/Orchestrator.h"
#include "liberty/Strategy/PerformanceEstimator.h"
#include "scaf/Utilities/InstInsertPt.h"
#include "scaf/Utilities/ModuleLoops.h"
#include "scaf/Utilities/Timer.h"
#include "scaf/Utilities/ControlSpeculation.h"
#include "scaf/SpeculationModules/GlobalConfig.h"

#include "scaf/Utilities/ReportDump.h"

#include <iterator>

namespace liberty {

static cl::opt<bool> IgnoreCost(
  "ignore-cost", cl::init(false), cl::Hidden,
  cl::desc("Assume all remedy cost is zero"));

namespace SpecPriv {
using namespace llvm;
using namespace llvm::noelle;

std::vector<Remediator_ptr> Orchestrator::getAvailableRemediators(Loop *A, PDG *pdg) {
  std::vector<Remediator_ptr> remeds;

  /* produce remedies for control deps */

  if (EnableEdgeProf) {
    ctrlspec->setLoopOfInterest(A->getHeader());
    auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
    ctrlSpecRemed->processLoopOfInterest(A);
    remeds.push_back(std::move(ctrlSpecRemed));
  }

  /* produce remedies for register deps */
  // reduction remediator
  auto reduxRemed = std::make_unique<ReduxRemediator>(mloops, loopAA, pdg);
  reduxRemed->setLoopOfInterest(A);
  remeds.push_back(std::move(reduxRemed));

  // counted induction variable remediator
  // disable IV remediator for PS-DSWP for now, handle it via replicable stage
  //remeds.push_back(std::make_unique<CountedIVRemediator>(&ldi));


  if (EnableSpecPriv && EnableLamp && EnableEdgeProf) {
    // full speculative analysis stack combining lamp, ctrl spec, value prediction,
    // points-to spec, separation logic spec, txio, commlibsaa, ptr-residue and
    // static analysis.
    const DataLayout &DL = A->getHeader()->getModule()->getDataLayout();
    const Ctx *ctx = rd->getCtx(A);
    const HeapAssignment &asgn = classify->getAssignmentFor(A);
    remeds.push_back(std::make_unique<MemSpecAARemediator>(
          proxy, ctrlspec, lamp, *rd, asgn, loadedValuePred, smtxLampMan,
          ptrResMan, killflowA, callsiteA, *mloops, perf));
  }

  // memory versioning remediator (used separately from the rest since it cannot
  // collaborate with analysis modules. It cannot answer modref or alias
  // queries. Only addresses dependence queries about false deps)
  remeds.push_back(std::make_unique<MemVerRemediator>());

  return remeds;
}

/*
 *std::vector<Remediator_ptr> Orchestrator::getRemediators(
 *    Loop *A, PDG *pdg, ControlSpeculation *ctrlspec,
 *    PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
 *    TargetLibraryInfo *tli,
 *    //LoopDependenceInfo &ldi,
 *    SmtxSpeculationManager &smtxLampMan,
 *    PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
 *    const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
 *    KillFlow &kill, KillFlow_CtrlSpecAware *killflowA,
 *    CallsiteDepthCombinator_CtrlSpecAware *callsiteA,
 *    PerformanceEstimator *perf) {
 *  std::vector<Remediator_ptr> remeds;
 *
 *  // control speculation remediator
 *  ctrlspec->setLoopOfInterest(A->getHeader());
 *  auto ctrlSpecRemed = std::make_unique<ControlSpecRemediator>(ctrlspec);
 *  ctrlSpecRemed->processLoopOfInterest(A);
 *  remeds.push_back(std::move(ctrlSpecRemed));
 *
 *
 *  [> produce remedies for register deps <]
 *  //TODO: re-enable these two remediators
 *
 *  // reduction remediator
 *  auto reduxRemed = std::make_unique<ReduxRemediator>(&mloops, loopAA, pdg);
 *  reduxRemed->setLoopOfInterest(A);
 *  remeds.push_back(std::move(reduxRemed));
 *
 *  // counted induction variable remediator
 *  // disable IV remediator for PS-DSWP for now, handle it via replicable stage
 *  //remeds.push_back(std::make_unique<CountedIVRemediator>(&ldi));
 *
 *
 *  [> produce remedies for memory deps <]
 *
 *  // full speculative analysis stack combining lamp, ctrl spec, value prediction,
 *  // points-to spec, separation logic spec, txio, commlibsaa, ptr-residue and
 *  // static analysis.
 *  remeds.push_back(std::make_unique<MemSpecAARemediator>(
 *      proxy, ctrlspec, &lamp, rd, asgn, loadedValuePred, &smtxLampMan,
 *      &ptrResMan, killflowA, callsiteA, kill, mloops, perf));
 *
 *  // memory versioning remediator (used separately from the rest since it cannot
 *  // collaborate with analysis modules. It cannot answer modref or alias
 *  // queries. Only addresses dependence queries about false deps)
 *  remeds.push_back(std::make_unique<MemVerRemediator>());
 *
 *  return remeds;
 *}
 */

std::vector<Critic_ptr> Orchestrator::getCritics(PerformanceEstimator *perf,
                                                 unsigned threadBudget,
                                                 LoopProfLoad *lpl) {
  std::vector<Critic_ptr> critics;

  // PS-DSWP critic
  critics.push_back(std::make_shared<PSDSWPCritic>(perf, threadBudget, lpl));
  critics.push_back(std::make_shared<DSWPCritic>(perf, threadBudget, lpl));

  // DOALL critic (covered by PS-DSWP critic)
  //critics.push_back(std::make_shared<DOALLCritic>(perf, threadBudget, lpl));

  return critics;
}

void Orchestrator::printRemediatorSelectionCnt() {
  REPORT_DUMP(errs() << "Selected Remediators:\n\n");
  for (auto const &it : remediatorSelectionCnt) {
    REPORT_DUMP(errs() << it.first << " was selected " << it.second << " times\n");
  }
}

void Orchestrator::printRemedies(Remedies &rs, bool selected) {
  REPORT_DUMP(errs() << "( ");
  auto itRs = rs.begin();
  while(itRs != rs.end()) {
    StringRef remedyName = (*itRs)->getRemedyName();
    if (remedyName.equals("locality-remedy")) {
      LocalityRemedy *localityRemed = (LocalityRemedy *)&*(*itRs);
      remedyName = localityRemed->getLocalityRemedyName();
    }
    REPORT_DUMP(errs() << remedyName);
    if ((++itRs) != rs.end())
      REPORT_DUMP(errs() << ", ");
  }
  REPORT_DUMP(errs() << " )");
}

void Orchestrator::printSelected(const SetOfRemedies &sors,
                                 const Remedies_ptr &selected, Criticism &cr) {
  REPORT_DUMP(errs() << "----------------------------------------------------\n");
  printRemedies(*selected, true);
  REPORT_DUMP(errs() << " chosen to address criticism ";
        if (cr.isControlDependence()) errs() << "(Control, "; else {
          if (cr.isMemoryDependence())
            errs() << "(Mem, ";
          else
            errs() << "(Reg, ";
          if (cr.isWARDependence())
            errs() << "WAR, ";
          else if (cr.isWAWDependence())
            errs() << "WAW, ";
          else if (cr.isRAWDependence())
            errs() << "RAW, ";
        }
        if (cr.isLoopCarriedDependence()) errs() << "LC)";
        else errs() << "II)";

        errs() << ":\n"
               << *cr.getOutgoingT();
        if (Instruction *outgoingI = dyn_cast<Instruction>(cr.getOutgoingT()))
            liberty::printInstDebugInfo(outgoingI);
        errs() << " ->\n"
               << *cr.getIncomingT();
        if (Instruction *incomingI = dyn_cast<Instruction>(cr.getIncomingT()))
            liberty::printInstDebugInfo(incomingI);
        errs() << "\n";);
  if (sors.size() > 1) {
    REPORT_DUMP(errs() << "\nAlternative remedies for the same criticism: ");
    auto itR = sors.begin();
    while (itR != sors.end()) {
      if (*itR == selected) {
        ++itR;
        continue;
      }
      printRemedies(**itR, false);
      if ((++itR) != sors.end())
        REPORT_DUMP(errs() << ", ");
      else
        REPORT_DUMP(errs() << "\n");
    }
  }
  REPORT_DUMP(errs() << "------------------------------------------------------\n\n");
}

void Orchestrator::printAllRemedies(const SetOfRemedies &sors, Criticism &cr) {
  if (sors.empty())
    return;
  REPORT_DUMP(errs() << "\nRemedies ");
  auto itR = sors.begin();
  while (itR != sors.end()) {
    printRemedies(**itR, false);
    if ((++itR) != sors.end())
      REPORT_DUMP(errs() << ", ");
  }
  REPORT_DUMP(errs() << " can address criticism ";
        if (cr.isControlDependence()) errs() << "(Control, "; else {
          if (cr.isMemoryDependence())
            errs() << "(Mem, ";
          else
            errs() << "(Reg, ";
          if (cr.isWARDependence())
            errs() << "WAR, ";
          else if (cr.isWAWDependence())
            errs() << "WAW, ";
          else if (cr.isRAWDependence())
            errs() << "RAW, ";
        }
        if (cr.isLoopCarriedDependence()) errs() << "LC)";
        else errs() << "II)";

        errs() << ":\n"
               << *cr.getOutgoingT();
        if (Instruction *outgoingI = dyn_cast<Instruction>(cr.getOutgoingT()))
            liberty::printInstDebugInfo(outgoingI);
        errs() << " ->\n"
               << *cr.getIncomingT();
        if (Instruction *incomingI = dyn_cast<Instruction>(cr.getIncomingT()))
            liberty::printInstDebugInfo(incomingI);
        errs() << "\n";);
}

// for now pick the cheapest remedy for each criticism
// Possible extension (not proven useful): perform instead global reasoning and
// consider the best set of remedies for a given set of criticisms
void Orchestrator::addressCriticisms(SelectedRemedies &selectedRemedies,
                                     unsigned long &selectedRemediesCost,
                                     Criticisms &criticisms) {
  REPORT_DUMP(errs() << "\n-====================================================-\n");
  REPORT_DUMP(errs() << "Selected Remedies:\n");
  for (Criticism *cr : criticisms) {
    auto sors = cr->getRemedies();
    const Remedies_ptr cheapestR = *(sors->begin());
    for (auto &r : *cheapestR) {
      if (!selectedRemedies.count(r)) {
        selectedRemediesCost += r->cost;
        selectedRemedies.insert(r);
      }
    }
    // FIXME: not printing the details
    //printSelected(*sors, cheapestR, *cr);

    auto itRs = cheapestR->begin();
    while(itRs != cheapestR->end()) {
      StringRef remedyName = (*itRs)->getRemedyName();
      if (remedyName.equals("locality-remedy")) {
        LocalityRemedy *localityRemed = (LocalityRemedy *)&*(*itRs);
        remedyName = localityRemed->getLocalityRemedyName();
      }
      if (remediatorSelectionCnt.count(remedyName.str()))
        ++remediatorSelectionCnt[remedyName.str()];
      else
        remediatorSelectionCnt[remedyName.str()] = 1;
      itRs++;
    }
  }
  REPORT_DUMP(errs() << "-====================================================-\n\n");
  printRemediatorSelectionCnt();
  REPORT_DUMP(errs() << "\n-====================================================-\n\n");
}

// do not try to run remediators
/*
 *bool Orchestrator::findBestStrategyGivenBestPDG(
 *    Loop *loop, llvm::noelle::PDG &pdg,PerformanceEstimator &perf, ControlSpeculation *ctrlspec,
 *    ModuleLoops &mloops, LoopProfLoad &lpl, LoopAA *loopAA,
 *    const Read &rd, const HeapAssignment &asgn, Pass &proxy,
 *    std::unique_ptr<PipelineStrategy> &strat,
 *    std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
 *    unsigned threadBudget, bool ignoreAntiOutput, bool includeReplicableStages,
 *    bool constrainSubLoops, bool abortIfNoParallelStage) {
 *
 *  BasicBlock *header = loop->getHeader();
 *  Function *fcn = header->getParent();
 *
 *  REPORT_DUMP(errs() << "Start of findBestStrategy for loop " << fcn->getName()
 *               << "::" << header->getName();
 *        Instruction *term = header->getTerminator();
 *        if (term) liberty::printInstDebugInfo(term);
 *        errs() << "\n";);
 *
 *  unsigned long maxSavings = 0;
 *
 *  Criticisms allCriticisms = Critic::getAllCriticisms(pdg);
 *  // address all possible criticisms
 *  std::vector<Remediator_ptr> remeds =
 *      getNonSpecRemediators(loop, &pdg, ctrlspec, mloops, loopAA,
 *          rd, asgn, proxy, &perf);
 *
 *  for (auto remediatorIt = remeds.begin(); remediatorIt != remeds.end();
 *       ++remediatorIt) {
 *    Remedies remedies = (*remediatorIt)->satisfy(pdg, loop, allCriticisms);
 *    for (Remedy_ptr r : remedies) {
 *      for (Criticism *c : r->resolvedC) {
 *        long tcost = 0;
 *        Remedies_ptr remedSet = std::make_shared<Remedies>();
 *        if (r->hasSubRemedies()) {
 *          for (Remedy_ptr subr : *(r->getSubRemedies())) {
 *            remedSet->insert(subr);
 *            tcost += subr->cost;
 *          }
 *        } else {
 *          remedSet->insert(r);
 *          tcost = r->cost;
 *        }
 *        // remedies are added to the edges.
 *        c->setRemovable(true);
 *        c->addRemedies(remedSet);
 *      }
 *    }
 *  }
 *  std::vector<Critic_ptr> critics = getCritics(&perf, threadBudget, &lpl); //get PSDSWP critics
 *
 *  for (auto criticIt = critics.begin(); criticIt != critics.end(); ++criticIt) {
 *    REPORT_DUMP(errs() << "\nCritic " << (*criticIt)->getCriticName() << "\n");
 *    CriticRes res = (*criticIt)->getCriticisms(pdg, loop);
 *    Criticisms &criticisms = res.criticisms;
 *    unsigned long expSpeedup = res.expSpeedup;
 *
 *    if (!expSpeedup) {
 *      REPORT_DUMP(errs() << (*criticIt)->getCriticName()
 *                   << " not applicable/profitable to " << fcn->getName()
 *                   << "::" << header->getName()
 *                   << ": not all criticisms are addressable\n");
 *      continue;
 *    }
 *
 *    std::unique_ptr<SelectedRemedies> selectedRemedies =
 *        std::unique_ptr<SelectedRemedies>(new SelectedRemedies());
 *    unsigned long selectedRemediesCost = 0;
 *    if (!criticisms.size()) {
 *      REPORT_DUMP(errs() << "\nNo criticisms generated!\n\n");
 *    } else {
 *      REPORT_DUMP(errs() << "Addressible criticisms\n");
 *      // orchestrator selects set of remedies to address the given criticisms,
 *      // computes remedies' total cost
 *      addressCriticisms(*selectedRemedies, selectedRemediesCost, criticisms);
 *    }
 *
 *    unsigned long adjRemedCosts =
 *        (long)Critic::FixedPoint * selectedRemediesCost;
 *
 *    if (IgnoreCost)
 *      adjRemedCosts = 0;
 *
 *    unsigned long savings = expSpeedup - adjRemedCosts;
 *
 *    REPORT_DUMP(errs() << "Expected Savings from critic "
 *                 << (*criticIt)->getCriticName()
 *                 << " (no remedies): " << expSpeedup
 *                 << "  and selected remedies cost: " << adjRemedCosts << "\n");
 *
 *    // for coverage purposes, given that the cost model is not complete and not
 *    // consistent among speedups and remedies cost, assume that it is always
 *    // profitable to parallelize if loop is DOALL-able.
 *    //
 *    //if (maxSavings  < savings) {
 *    if (!maxSavings || maxSavings < savings) {
 *      maxSavings = savings;
 *      strat = std::move(res.ps);
 *      sRemeds = std::move(selectedRemedies);
 *      sCritic = *criticIt;
 *    }
 *  }
 *
 *  if (maxSavings)
 *    return true;
 *
 *  // no profitable parallelization strategy was found for this loop
 *  return false;
 *}
 */

bool Orchestrator::findBestStrategy(
    Loop *loop, llvm::noelle::PDG &pdg,
    // //LoopDependenceInfo &ldi,
    // PerformanceEstimator &perf,
    // ControlSpeculation *ctrlspec,
    // PredictionSpeculation *loadedValuePred, ModuleLoops &mloops,
    // TargetLibraryInfo *tli,
    // SmtxSpeculationManager &smtxLampMan,
    // PtrResidueSpeculationManager &ptrResMan, LAMPLoadProfile &lamp,
    // const Read &rd, const HeapAssignment &asgn, Pass &proxy, LoopAA *loopAA,
    // KillFlow &kill, KillFlow_CtrlSpecAware *killflowA,
    // CallsiteDepthCombinator_CtrlSpecAware *callsiteA, LoopProfLoad &lpl,
    std::unique_ptr<PipelineStrategy> &strat,
    std::unique_ptr<SelectedRemedies> &sRemeds, Critic_ptr &sCritic,
    unsigned threadBudget, bool ignoreAntiOutput, bool includeReplicableStages,
    bool constrainSubLoops, bool abortIfNoParallelStage) {
  BasicBlock *header = loop->getHeader();
  Function *fcn = header->getParent();

  REPORT_DUMP(errs() << "Start of findBestStrategy for loop " << fcn->getName()
               << "::" << header->getName();
        Instruction *term = header->getTerminator();
        if (term) liberty::printInstDebugInfo(term);
        errs() << "\n";);

  unsigned long maxSavings = 0;

  // get all possible criticisms
  Criticisms allCriticisms = Critic::getAllCriticisms(pdg);

  // address all possible criticisms
  std::vector<Remediator_ptr> remeds =
    getAvailableRemediators(loop, &pdg);

      /*
       *getRemediators(loop, &pdg, ctrlspec, loadedValuePred, mloops, tli, //ldi,
       *               smtxLampMan, ptrResMan, lamp, rd, asgn, proxy,
       *               loopAA, kill, killflowA, callsiteA, &perf);
       */

  for (auto remediatorIt = remeds.begin(); remediatorIt != remeds.end();
       ++remediatorIt) {
    Remedies remedies = (*remediatorIt)->satisfy(pdg, loop, allCriticisms);
    for (Remedy_ptr r : remedies) {
      for (Criticism *c : r->resolvedC) {
        long tcost = 0;
        Remedies_ptr remedSet = std::make_shared<Remedies>();
        if (r->hasSubRemedies()) {
          for (Remedy_ptr subr : *(r->getSubRemedies())) {
            remedSet->insert(subr);
            tcost += subr->cost;
          }
        } else {
          remedSet->insert(r);
          tcost = r->cost;
        }
        // remedies are added to the edges.
        c->setRemovable(true);
        c->addRemedies(remedSet);
      }
    }
  }

  // receive actual criticisms from critics given the enhanced pdg
  std::vector<Critic_ptr> critics = getCritics(perf, threadBudget, lpl); //get PSDSWP critics

  for (auto criticIt = critics.begin(); criticIt != critics.end(); ++criticIt) {
    REPORT_DUMP(errs() << "\nCritic " << (*criticIt)->getCriticName() << "\n");
    CriticRes res = (*criticIt)->getCriticisms(pdg, loop);
    Criticisms &criticisms = res.criticisms;
    unsigned long expSpeedup = res.expSpeedup;

    if (!expSpeedup) {
      REPORT_DUMP(errs() << (*criticIt)->getCriticName()
                   << " not applicable/profitable to " << fcn->getName()
                   << "::" << header->getName()
                   << ": not all criticisms are addressable\n");
      continue;
    }

    std::unique_ptr<SelectedRemedies> selectedRemedies =
        std::unique_ptr<SelectedRemedies>(new SelectedRemedies());
    unsigned long selectedRemediesCost = 0;
    if (!criticisms.size()) {
      REPORT_DUMP(errs() << "\nNo criticisms generated!\n\n");
    } else {
      REPORT_DUMP(errs() << "Addressible criticisms\n");
      // orchestrator selects set of remedies to address the given criticisms,
      // computes remedies' total cost
      addressCriticisms(*selectedRemedies, selectedRemediesCost, criticisms);
    }

    unsigned long adjRemedCosts =
        (long)Critic::FixedPoint * selectedRemediesCost;
    if (IgnoreCost)
      adjRemedCosts = 0;

    unsigned long savings = expSpeedup - adjRemedCosts;

    REPORT_DUMP(errs() << "Expected Savings from critic "
                 << (*criticIt)->getCriticName()
                 << " (no remedies): " << expSpeedup
                 << "  and selected remedies cost: " << adjRemedCosts << "\n");

    // for coverage purposes, given that the cost model is not complete and not
    // consistent among speedups and remedies cost, assume that it is always
    // profitable to parallelize if loop is DOALL-able.
    //
    //if (maxSavings  < savings) {
    if (!maxSavings || maxSavings < savings) {
      maxSavings = savings;
      strat = std::move(res.ps);
      sRemeds = std::move(selectedRemedies);
      sCritic = *criticIt;
    }
  }

  if (maxSavings)
    return true;

  // no profitable parallelization strategy was found for this loop
  return false;
}

} // namespace SpecPriv
} // namespace liberty
