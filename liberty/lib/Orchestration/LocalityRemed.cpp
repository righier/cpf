#define DEBUG_TYPE "locality-remed"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/SmallBitVector.h"

#include "liberty/Orchestration/LocalityRemed.h"

#define DEFAULT_LOCALITY_REMED_COST 40
#define PRIVATE_ACCESS_COST 100
#define LOCAL_ACCESS_COST 5

namespace liberty {
using namespace llvm;

STATISTIC(numEligible,        "Num eligible queries");
STATISTIC(numPrivatizedPriv,  "Num privatized (Private)");
STATISTIC(numPrivatizedRedux, "Num privatized (Redux)");
STATISTIC(numPrivatizedShort, "Num privatized (Short-lived)");
STATISTIC(numSeparated,       "Num separated");
STATISTIC(numLocalityAA,      "Num removed via LocalityAA");
STATISTIC(numSubSep,          "Num separated via subheaps");

void LocalityRemedy::apply(PDG &pdg) {
  // TODO: transfer the code for application of separation logic here.
}

bool LocalityRemedy::compare(const Remedy_ptr rhs) const {
  std::shared_ptr<LocalityRemedy> sepRhs =
      std::static_pointer_cast<LocalityRemedy>(rhs);

  if (this->privateI == sepRhs->privateI)
    return this->localI < sepRhs->localI;
  return this->privateI < sepRhs->privateI;
}

bool noMemoryDep(const Instruction *src, const Instruction *dst,
                 LoopAA::TemporalRelation FW, LoopAA::TemporalRelation RV,
                 const Loop *loop, LoopAA *aa, bool rawDep) {
  // forward dep test
  LoopAA::ModRefResult forward = aa->modref(src, FW, dst, loop);
  if (LoopAA::NoModRef == forward)
    return true;

  // forward is Mod, ModRef, or Ref

  // reverse dep test
  LoopAA::ModRefResult reverse = forward;

  if (src != dst)
    reverse = aa->modref(dst, RV, src, loop);

  if (LoopAA::NoModRef == reverse)
    return true;

  if (LoopAA::Ref == forward && LoopAA::Ref == reverse)
    return true; // RaR dep; who cares.

  // At this point, we know there is one or more of
  // a flow-, anti-, or output-dependence.

  bool RAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Ref || reverse == LoopAA::ModRef);
  bool WAR = (forward == LoopAA::Ref || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);
  bool WAW = (forward == LoopAA::Mod || forward == LoopAA::ModRef) &&
             (reverse == LoopAA::Mod || reverse == LoopAA::ModRef);

  if (rawDep && !RAW)
    return true;

  if (!rawDep && !WAR && !WAW)
    return true;

  return false;
}

Remediator::RemedResp LocalityRemediator::memdep(const Instruction *A,
                                                 const Instruction *B,
                                                 bool LoopCarried, bool RAW,
                                                 const Loop *L) {

  Remediator::RemedResp remedResp;
  // conservative answer
  remedResp.depRes = DepResult::Dep;
  std::shared_ptr<LocalityRemedy> remedy =
      std::shared_ptr<LocalityRemedy>(new LocalityRemedy());
  remedy->cost = DEFAULT_LOCALITY_REMED_COST;
  remedResp.remedy = remedy;

  if (!L || !asgn.isValidFor(L))
    return remedResp;

  const Value *ptr1 = liberty::getMemOper(A);
  const Value *ptr2 = liberty::getMemOper(B);
  if (!ptr1 || !ptr2) {
    if (LoopCarried) {
      bool noDep = noMemoryDep(A, B, LoopAA::Before, LoopAA::After, L, aa, RAW);
      if (noDep) {
        ++numLocalityAA;
        remedResp.depRes = DepResult::NoDep;
      }
    }
    return remedResp;
  }
  if (!isa<PointerType>(ptr1->getType()))
    return remedResp;
  if (!isa<PointerType>(ptr2->getType()))
    return remedResp;

  const Ctx *ctx = read.getCtx(L);

  ++numEligible;

  Ptrs aus1;
  HeapAssignment::Type t1 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr1, ctx, aus1))
    t1 = asgn.classify(aus1);

  Ptrs aus2;
  HeapAssignment::Type t2 = HeapAssignment::Unclassified;
  if (read.getUnderlyingAUs(ptr2, ctx, aus2))
    t2 = asgn.classify(aus2);

  // Loop-carried queries:
  if (LoopCarried) {
    // Reduction, local and private heaps are iteration-private, thus
    // there cannot be cross-iteration flows.
    if (t1 == HeapAssignment::Redux || t1 == HeapAssignment::Local ||
        t1 == HeapAssignment::Private) {
      remedResp.depRes = DepResult::NoDep;
      if (t1 == HeapAssignment::Private) {
        ++numPrivatizedPriv;
        remedy->cost += PRIVATE_ACCESS_COST;
        remedy->privateI = A;
        remedy->localI = nullptr;
      } else if (t1 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->privateI = nullptr;
        remedy->localI = A;
      } else {
        ++numPrivatizedRedux;
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
      }
      remedResp.remedy = remedy;
      return remedResp;
    }

    if (t2 == HeapAssignment::Redux || t2 == HeapAssignment::Local ||
        t2 == HeapAssignment::Private) {
      remedResp.depRes = DepResult::NoDep;
      if (t2 == HeapAssignment::Private) {
        ++numPrivatizedPriv;
        remedy->cost += PRIVATE_ACCESS_COST;
        remedy->privateI = B;
        remedy->localI = nullptr;
      } else if (t2 == HeapAssignment::Local) {
        ++numPrivatizedShort;
        remedy->cost += LOCAL_ACCESS_COST;
        remedy->privateI = nullptr;
        remedy->localI = B;
      } else {
        ++numPrivatizedRedux;
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
      }
      remedResp.remedy = remedy;
      return remedResp;
    }
  }

  // Both loop-carried and intra-iteration queries: are they assigned to
  // different heaps?
  if (t1 != t2 && t1 != HeapAssignment::Unclassified &&
      t2 != HeapAssignment::Unclassified) {
    ++numSeparated;
    remedResp.depRes = DepResult::NoDep;
    remedy->privateI = nullptr;
    remedy->localI = nullptr;
    remedResp.remedy = remedy;
    return remedResp;
  }

  // They are assigned to the same heap.
  // Are they assigned to different sub-heaps?
  if (t1 == t2 && t1 != HeapAssignment::Unclassified) {
    const int subheap1 = asgn.getSubHeap(aus1);
    if (subheap1 > 0) {
      const int subheap2 = asgn.getSubHeap(aus2);
      if (subheap2 > 0 && subheap1 != subheap2) {
        ++numSubSep;
        remedResp.depRes = DepResult::NoDep;
        remedy->privateI = nullptr;
        remedy->localI = nullptr;
        remedResp.remedy = remedy;
        return remedResp;
      }
    }
  }

  return remedResp;
}

} // namespace liberty