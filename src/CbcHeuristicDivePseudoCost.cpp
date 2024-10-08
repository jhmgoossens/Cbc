// Copyright (C) 2008, International Business Machines
// Corporation and others.  All Rights Reserved.
// This code is licensed under the terms of the Eclipse Public License (EPL).

#if defined(_MSC_VER)
// Turn off compiler warning about long names
#pragma warning(disable : 4786)
#endif

#include "CbcHeuristicDivePseudoCost.hpp"
#include "CbcStrategy.hpp"
#include "CbcBranchDynamic.hpp"

// Default Constructor
CbcHeuristicDivePseudoCost::CbcHeuristicDivePseudoCost()
  : CbcHeuristicDive()
{
}

// Constructor from model
CbcHeuristicDivePseudoCost::CbcHeuristicDivePseudoCost(CbcModel &model)
  : CbcHeuristicDive(model)
{
}

// Destructor
CbcHeuristicDivePseudoCost::~CbcHeuristicDivePseudoCost()
{
}

// Clone
CbcHeuristicDivePseudoCost *
CbcHeuristicDivePseudoCost::clone() const
{
  return new CbcHeuristicDivePseudoCost(*this);
}

// Create C++ lines to get to current state
void CbcHeuristicDivePseudoCost::generateCpp(FILE *fp)
{
  CbcHeuristicDivePseudoCost other;
  fprintf(fp, "0#include \"CbcHeuristicDivePseudoCost.hpp\"\n");
  fprintf(fp, "3  CbcHeuristicDivePseudoCost heuristicDivePseudoCost(*cbcModel);\n");
  CbcHeuristic::generateCpp(fp, "heuristicDivePseudoCost");
  fprintf(fp, "3  cbcModel->addHeuristic(&heuristicDivePseudoCost);\n");
}

// Copy constructor
CbcHeuristicDivePseudoCost::CbcHeuristicDivePseudoCost(const CbcHeuristicDivePseudoCost &rhs)
  : CbcHeuristicDive(rhs)
{
}

// Assignment operator
CbcHeuristicDivePseudoCost &
CbcHeuristicDivePseudoCost::operator=(const CbcHeuristicDivePseudoCost &rhs)
{
  if (this != &rhs) {
    CbcHeuristicDive::operator=(rhs);
  }
  return *this;
}

bool CbcHeuristicDivePseudoCost::selectVariableToBranch(OsiSolverInterface *solver,
  const double *newSolution,
  int &bestColumn,
  int &bestRound)
{
  int numberIntegers = model_->numberIntegers();
  const int *integerVariable = model_->integerVariable();
  double integerTolerance = model_->getDblParam(CbcModel::CbcIntegerTolerance);

  // get the LP relaxation solution at the root node
  double *rootNodeLPSol = model_->continuousSolution();

  // get pseudo costs
  double *pseudoCostDown = downArray_;
  double *pseudoCostUp = upArray_;

  bestColumn = -1;
  bestRound = -1; // -1 rounds down, +1 rounds up
  double bestScore = -1.0;
  bool allTriviallyRoundableSoFar = true;
  int bestPriority = COIN_INT_MAX;
  for (int i = 0; i < numberIntegers; i++) {
    int iColumn = integerVariable[i];
    if (!isHeuristicInteger(solver, iColumn))
      continue;
    double rootValue = rootNodeLPSol[iColumn];
    double value = newSolution[iColumn];
    double fraction = value - floor(value);
    int round = 0;
    if (fabs(floor(value + 0.5) - value) > integerTolerance) {
      if (allTriviallyRoundableSoFar || (downLocks_[i] > 0 && upLocks_[i] > 0)) {

        if (allTriviallyRoundableSoFar && downLocks_[i] > 0 && upLocks_[i] > 0) {
          allTriviallyRoundableSoFar = false;
          bestScore = -1.0;
        }

        double pCostDown = pseudoCostDown[i];
        double pCostUp = pseudoCostUp[i];
        assert(pCostDown >= 0.0 && pCostUp >= 0.0);

        if (allTriviallyRoundableSoFar && downLocks_[i] == 0 && upLocks_[i] > 0)
          round = 1;
        else if (allTriviallyRoundableSoFar && downLocks_[i] > 0 && upLocks_[i] == 0)
          round = -1;
        else if (value - rootValue < -0.4)
          round = -1;
        else if (value - rootValue > 0.4)
          round = 1;
        else if (fraction < 0.3)
          round = -1;
        else if (fraction > 0.7)
          round = 1;
        else if (pCostDown < pCostUp)
          round = -1;
        else
          round = 1;

        // calculate score
        double score;
        if (round == 1)
          score = fraction * (pCostDown + 1.0) / (pCostUp + 1.0);
        else
          score = (1.0 - fraction) * (pCostUp + 1.0) / (pCostDown + 1.0);

        // if variable is binary, increase its chance of being selected
        if (solver->isBinary(iColumn))
          score *= 1000.0;

        // if priorities then use
        if (priority_) {
          int thisRound = static_cast< int >(priority_[i].direction);
          if ((thisRound & 1) != 0)
            round = ((thisRound & 2) == 0) ? -1 : +1;
          if (priority_[i].priority > bestPriority) {
            score = COIN_DBL_MAX;
          } else if (priority_[i].priority < bestPriority) {
            bestPriority = static_cast< int >(priority_[i].priority);
            bestScore = COIN_DBL_MAX;
          }
        }
        if (score > bestScore) {
          bestColumn = iColumn;
          bestScore = score;
          bestRound = round;
        }
      }
    }
  }

  return allTriviallyRoundableSoFar;
}
void CbcHeuristicDivePseudoCost::initializeData()
{
  int numberIntegers = model_->numberIntegers();
  if (!downArray_) {
    downArray_ = new double[numberIntegers];
    upArray_ = new double[numberIntegers];
  }
  // get pseudo costs
  model_->fillPseudoCosts(downArray_, upArray_);
  // allow for -999 -> force to run
  int diveOptions = (when_ > 0) ? when_ / 100 : 0;
  if (diveOptions) {
    // pseudo shadow prices
    int k = diveOptions % 100;
    if (diveOptions >= 100)
      k += 32;
    model_->pseudoShadow(k - 1);
    int numberInts = std::min(model_->numberObjects(), numberIntegers);
    OsiObject **objects = model_->objects();
    for (int i = 0; i < numberInts; i++) {
      CbcSimpleIntegerDynamicPseudoCost *obj1 = dynamic_cast< CbcSimpleIntegerDynamicPseudoCost * >(objects[i]);
      if (obj1) {
        //int iColumn = obj1->columnNumber();
        double downPseudoCost = 1.0e-2 * obj1->downDynamicPseudoCost();
        double downShadow = obj1->downShadowPrice();
        double upPseudoCost = 1.0e-2 * obj1->upDynamicPseudoCost();
        double upShadow = obj1->upShadowPrice();
        downPseudoCost = std::max(downPseudoCost, downShadow);
        downPseudoCost = std::max(downPseudoCost, 0.001 * upShadow);
        downArray_[i] = downPseudoCost;
        upPseudoCost = std::max(upPseudoCost, upShadow);
        upPseudoCost = std::max(upPseudoCost, 0.001 * downShadow);
        upArray_[i] = upPseudoCost;
      }
    }
  }
}
// Fix other variables at bounds
int CbcHeuristicDivePseudoCost::fixOtherVariables(OsiSolverInterface *solver,
  const double *solution,
  PseudoReducedCost *candidate,
  const double *random)
{
  const double *lower = solver->getColLower();
  const double *upper = solver->getColUpper();
  double integerTolerance = model_->getDblParam(CbcModel::CbcIntegerTolerance);
  double primalTolerance;
  solver->getDblParam(OsiPrimalTolerance, primalTolerance);

  int numberIntegers = model_->numberIntegers();
  const int *integerVariable = model_->integerVariable();
  const double *reducedCost = solver->getReducedCost();
  bool fixGeneralIntegers = (switches_ & 65536) != 0;
  // fix other integer variables that are at their bounds
  int cnt = 0;
#ifdef CLP_INVESTIGATE
  int numberFree = 0;
  int numberFixedAlready = 0;
#endif
  for (int i = 0; i < numberIntegers; i++) {
    int iColumn = integerVariable[i];
    if (!isHeuristicInteger(solver, iColumn))
      continue;
    if (upper[iColumn] > lower[iColumn]) {
#ifdef CLP_INVESTIGATE
      numberFree++;
#endif
      double value = solution[iColumn];
      if (value - lower[iColumn] <= integerTolerance) {
        candidate[cnt].var = iColumn;
        candidate[cnt++].pseudoRedCost = std::max(1.0e-2 * reducedCost[iColumn],
                                           downArray_[i])
          * random[i];
      } else if (upper[iColumn] - value <= integerTolerance) {
        candidate[cnt].var = iColumn;
        candidate[cnt++].pseudoRedCost = std::max(-1.0e-2 * reducedCost[iColumn],
                                           downArray_[i])
          * random[i];
      } else if (fixGeneralIntegers && fabs(floor(value + 0.5) - value) <= integerTolerance) {
        candidate[cnt].var = iColumn;
        candidate[cnt++].pseudoRedCost = std::max(-1.0e-6 * reducedCost[iColumn],
                                           1.0e-4 * downArray_[i])
          * random[i];
      }
    } else {
#ifdef CLP_INVESTIGATE
      numberFixedAlready++;
#endif
    }
  }
#ifdef CLP_INVESTIGATE
  printf("cutoff %g obj %g - %d free, %d fixed\n",
    model_->getCutoff(), solver->getObjValue(), numberFree,
    numberFixedAlready);
#endif
  return cnt;
  //return CbcHeuristicDive::fixOtherVariables(solver, solution,
  //				     candidate, random);
}

/* vi: softtabstop=2 shiftwidth=2 expandtab tabstop=2
*/
