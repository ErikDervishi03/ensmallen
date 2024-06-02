/**
 * @file nsga2_impl.hpp
 * @author Sayan Goswami
 * @author Nanubala Gnana Sai
 *
 * Implementation of the NSGA-II algorithm. Used for multi-objective
 * optimization problems on arbitrary functions.
 *
 * ensmallen is free software; you may redistribute it and/or modify it under
 * the terms of the 3-clause BSD license.  You should have received a copy of
 * the 3-clause BSD license along with ensmallen.  If not, see
 * http://www.opensource.org/licenses/BSD-3-Clause for more Information.
 */

#ifndef ENSMALLEN_AGEMOEA_AGEMOEA_IMPL_HPP
#define ENSMALLEN_AGEMoEA_AGEMOEA_IMPL_HPP

#include "agemoea.hpp"
#include <assert.h>

namespace ens {

inline AGEMOEA::AGEMOEA(const size_t populationSize,
                    const size_t maxGenerations,
                    const double crossoverProb,
                    const double mutationProb,
                    const double mutationStrength,
                    const double distributionIndex,
                    const double epsilon,
                    const double eta,
                    const arma::vec& lowerBound,
                    const arma::vec& upperBound) :
    numObjectives(0),
    numVariables(0),
    populationSize(populationSize),
    maxGenerations(maxGenerations),
    crossoverProb(crossoverProb),
    mutationProb(mutationProb),
    mutationStrength(mutationStrength),
    distributionIndex(distributionIndex),
    epsilon(epsilon),
    eta(eta),
    lowerBound(lowerBound),
    upperBound(upperBound)
{ /* Nothing to do here. */ }

inline AGEMOEA::AGEMOEA(const size_t populationSize,
                    const size_t maxGenerations,
                    const double crossoverProb,
                    const double mutationProb,
                    const double mutationStrength,
                    const double distributionIndex,
                    const double epsilon,
                    const double eta,
                    const double lowerBound,
                    const double upperBound) :
    numObjectives(0),
    numVariables(0),
    populationSize(populationSize),
    maxGenerations(maxGenerations),
    crossoverProb(crossoverProb),
    mutationProb(mutationProb),
    mutationStrength(mutationStrength),
    distributionIndex(distributionIndex),
    epsilon(epsilon),
    eta(eta),
    lowerBound(lowerBound * arma::ones(1, 1)),
    upperBound(upperBound * arma::ones(1, 1))
{ /* Nothing to do here. */ }

//! Optimize the function.
template<typename MatType,
         typename... ArbitraryFunctionType,
         typename... CallbackTypes>
typename MatType::elem_type AGEMOEA::Optimize(
    std::tuple<ArbitraryFunctionType...>& objectives,
    MatType& iterateIn,
    CallbackTypes&&... callbacks)
{
  // Make sure for evolution to work at least four candidates are present.
  if (populationSize < 4 && populationSize % 4 != 0)
  {
    throw std::logic_error("AGEMOEA::Optimize(): population size should be at"
        " least 4, and, a multiple of 4!");
  }

  // Convenience typedefs.
  typedef typename MatType::elem_type ElemType;
  typedef typename MatTypeTraits<MatType>::BaseMatType BaseMatType;

  BaseMatType& iterate = (BaseMatType&) iterateIn;

  // Make sure that we have the methods that we need.  Long name...
  traits::CheckArbitraryFunctionTypeAPI<ArbitraryFunctionType...,
      BaseMatType>();
  RequireDenseFloatingPointType<BaseMatType>();

  // Check if lower bound is a vector of a single dimension.
  if (lowerBound.n_rows == 1)
    lowerBound = lowerBound(0, 0) * arma::ones(iterate.n_rows, iterate.n_cols);

  // Check if upper bound is a vector of a single dimension.
  if (upperBound.n_rows == 1)
    upperBound = upperBound(0, 0) * arma::ones(iterate.n_rows, iterate.n_cols);

  // Check the dimensions of lowerBound and upperBound.
  assert(lowerBound.n_rows == iterate.n_rows && "The dimensions of "
      "lowerBound are not the same as the dimensions of iterate.");
  assert(upperBound.n_rows == iterate.n_rows && "The dimensions of "
      "upperBound are not the same as the dimensions of iterate.");

  numObjectives = sizeof...(ArbitraryFunctionType);
  numVariables = iterate.n_rows;

  // Cache calculated objectives.
  std::vector<arma::Col<ElemType> > calculatedObjectives(populationSize);

  // Population size reserved to 2 * populationSize + 1 to accommodate
  // for the size of intermediate candidate population.
  std::vector<BaseMatType> population;
  population.reserve(2 * populationSize + 1);

  // Pareto fronts, initialized during non-dominated sorting.
  // Stores indices of population belonging to a certain front.
  std::vector<std::vector<size_t> > fronts;
  // Initialised in SurvivalScoreAssignment.
  std::vector<ElemType> survivalScore;
  // Initialised during non-dominated sorting.
  std::vector<size_t> ranks;

  //! Useful temporaries for float-like comparisons.
  const BaseMatType castedLowerBound = arma::conv_to<BaseMatType>::from(lowerBound);
  const BaseMatType castedUpperBound = arma::conv_to<BaseMatType>::from(upperBound);

  // Controls early termination of the optimization process.
  bool terminate = false;

  // Generate the population based on a uniform distribution around the given
  // starting point.
  for (size_t i = 0; i < populationSize; i++)
  {
    population.push_back(arma::randu<BaseMatType>(iterate.n_rows,
        iterate.n_cols) - 0.5 + iterate);

    // Constrain all genes to be within bounds.
    population[i] = arma::min(arma::max(population[i], castedLowerBound), castedUpperBound);
  }

  Info << "AGEMOEA initialized successfully. Optimization started." << std::endl;

  // Iterate until maximum number of generations is obtained.
  Callback::BeginOptimization(*this, objectives, iterate, callbacks...);

  for (size_t generation = 1; generation <= maxGenerations && !terminate; generation++)
  {
    Info << "AGEMOEA: iteration " << generation << "." << std::endl;

    // Create new population of candidate from the present elite population.
    // Have P_t, generate G_t using P_t.
    BinaryTournamentSelection(population, castedLowerBound, castedUpperBound);

    // Evaluate the objectives for the new population.
    calculatedObjectives.resize(population.size());
    std::fill(calculatedObjectives.begin(), calculatedObjectives.end(),
        arma::Col<ElemType>(numObjectives, arma::fill::zeros));
    EvaluateObjectives(population, objectives, calculatedObjectives);

    // Perform fast non dominated sort on P_t ∪ G_t.
    ranks.resize(population.size());
    FastNonDominatedSort<BaseMatType>(fronts, ranks, calculatedObjectives);

    // Perform survival score assignment.
    survivalScore.resize(population.size());
    std::fill(survivalScore.begin(), survivalScore.end(), 0.);
    for (size_t fNum = 0; fNum < fronts.size(); fNum++)
    {
      SurvivalScoreAssignment<BaseMatType>(
          fronts[fNum], calculatedObjectives, survivalScore);
    }

    // Sort based on survival score.
    std::sort(population.begin(), population.end(),
      [this, ranks, survivalScore, population]
        (BaseMatType candidateP, BaseMatType candidateQ)
          {
            size_t idxP{}, idxQ{};
            for (size_t i = 0; i < population.size(); i++)
            {
              if (arma::approx_equal(population[i], candidateP, "absdiff", epsilon))
                idxP = i;

              if (arma::approx_equal(population[i], candidateQ, "absdiff", epsilon))
                idxQ = i;
            }

            return SurvivalScoreOperator<BaseMatType>(idxP, idxQ, ranks, survivalScore);
          }
    );

    // Yield a new population P_{t+1} of size populationSize.
    // Discards unfit population from the R_{t} to yield P_{t+1}.
    population.resize(populationSize);

    terminate |= Callback::GenerationalStepTaken(*this, objectives, iterate,
        calculatedObjectives, fronts, callbacks...);
  }

  // Set the candidates from the Pareto Set as the output.
  paretoSet.set_size(population[0].n_rows, population[0].n_cols, fronts[0].size());
  // The Pareto Set is stored, can be obtained via ParetoSet() getter.
  for (size_t solutionIdx = 0; solutionIdx < fronts[0].size(); ++solutionIdx)
  {
    paretoSet.slice(solutionIdx) =
      arma::conv_to<arma::mat>::from(population[fronts[0][solutionIdx]]);
  }

  // Set the candidates from the Pareto Front as the output.
  paretoFront.set_size(calculatedObjectives[0].n_rows, calculatedObjectives[0].n_cols,
      fronts[0].size());
  // The Pareto Front is stored, can be obtained via ParetoFront() getter.
  for (size_t solutionIdx = 0; solutionIdx < fronts[0].size(); ++solutionIdx)
  {
    paretoFront.slice(solutionIdx) =
      arma::conv_to<arma::mat>::from(calculatedObjectives[fronts[0][solutionIdx]]);
  }

  // Clear rcFront, in case it is later requested by the user for reverse
  // compatibility reasons.
  rcFront.clear();

  // Assign iterate to first element of the Pareto Set.
  iterate = population[fronts[0][0]];

  Callback::EndOptimization(*this, objectives, iterate, callbacks...);

  ElemType performance = std::numeric_limits<ElemType>::max();

  for (const arma::Col<ElemType>& objective: calculatedObjectives)
    if (arma::accu(objective) < performance)
      performance = arma::accu(objective);

  return performance;
}

//! No objectives to evaluate.
template<std::size_t I,
         typename MatType,
         typename ...ArbitraryFunctionType>
typename std::enable_if<I == sizeof...(ArbitraryFunctionType), void>::type
AGEMOEA::EvaluateObjectives(
    std::vector<MatType>&,
    std::tuple<ArbitraryFunctionType...>&,
    std::vector<arma::Col<typename MatType::elem_type> >&)
{
  // Nothing to do here.
}

//! Evaluate the objectives for the entire population.
template<std::size_t I,
         typename MatType,
         typename ...ArbitraryFunctionType>
typename std::enable_if<I < sizeof...(ArbitraryFunctionType), void>::type
AGEMOEA::EvaluateObjectives(
    std::vector<MatType>& population,
    std::tuple<ArbitraryFunctionType...>& objectives,
    std::vector<arma::Col<typename MatType::elem_type> >& calculatedObjectives)
{
  for (size_t i = 0; i < populationSize; i++)
  {
    calculatedObjectives[i](I) = std::get<I>(objectives).Evaluate(population[i]);
    EvaluateObjectives<I+1, MatType, ArbitraryFunctionType...>(population, objectives,
                                                               calculatedObjectives);
  }
}

//! Reproduce and generate new candidates.
template<typename MatType>
inline void AGEMOEA::BinaryTournamentSelection(std::vector<MatType>& population,
                                             const MatType& lowerBound,
                                             const MatType& upperBound)
{
  std::vector<MatType> children;

  while (children.size() < population.size())
  {
    // Choose two random parents for reproduction from the elite population.
    size_t indexA = arma::randi<size_t>(arma::distr_param(0, populationSize - 1));
    size_t indexB = arma::randi<size_t>(arma::distr_param(0, populationSize - 1));

    // Make sure that the parents differ.
    if (indexA == indexB)
    {
      if (indexB < populationSize - 1)
        indexB++;
      else
        indexB--;
    }

    // Initialize the children to the respective parents.
    MatType childA = population[indexA], childB = population[indexB];

    if(arma::randu() <= crossoverProb)
    Crossover(childA, childB, population[indexA], population[indexB]);

    Mutate(childA, 1.0 / static_cast<double>(numVariables),
          lowerBound, upperBound);
    Mutate(childB, 1.0 / static_cast<double>(numVariables),
          lowerBound, upperBound);

    // Add the children to the candidate population.
    children.push_back(childA);
    children.push_back(childB);
  }

  // Add the candidates to the elite population.
  population.insert(std::end(population), std::begin(children), std::end(children));
}

//! Perform simulated binary crossover (SBX) of genes for the children.
template<typename MatType>
inline void AGEMOEA::Crossover(MatType& childA,
                             MatType& childB,
                             const MatType& parentA,
                             const MatType& parentB)
{
    // Ensure that parentA and parentB are of the same size.
    arma::Cube<typename MatType::elem_type> parents(parentA.n_rows, parentA.n_cols, 2);
    parents.slice(0) = parentA;
    parents.slice(1) = parentB;
    MatType& current_min =  arma::min(parents, 2);
    MatType& current_max =  arma::max(parents, 2);


    MatType& current_diff = current_max - current_min;
    current_diff.clamp(1e-10, arma::datum::inf);

    MatType beta1 = 1 + 2.0 * (current_min - lowerBound) / current_diff;
    MatType beta2 = 1 + 2.0 * (upperBound - current_max) / current_diff;
    MatType alpha1 = 2 - arma::pow(beta1, -(eta + 1));
    MatType alpha2 = 2 - arma::pow(beta2, -(eta + 1));

    arma::vec us(size(upperBound), arma::fill::randu);
    arma::vec mask1 = us > (1.0 / alpha1);
    arma::vec betaq1 = arma::pow(us % alpha1, 1 / (eta + 1));
    betaq1 = betaq1 % (mask1 - 1.0) + arma::pow((1.0 / (2.0 - us % alpha1)), 1.0 / (eta + 1)) % mask1;

    arma::vec mask2 = us > (1.0 / alpha2);
    arma::vec betaq2 = arma::pow(us % alpha2, 1 / (eta + 1));
    betaq2 = betaq2 * (mask1 - 1.0) + arma::pow((1.0 / (2.0 - us % alpha2)), 1.0 / (eta + 1)) % mask2;
    MatType c1 = 0.5 % ((current_min + current_max) - betaq1 % current_diff);
    MatType c2 = 0.5 % ((current_min + current_max) + betaq2 % current_diff);
    us.randu();
    childA = parentA % (us <= 0.5);
    childB = parentB % (us <= 0.5);
    us.randu();
    childA = childA + c1 % ((us <= 0.5) % (childA == 0));
    childA = childA + c2 % ((us > 0.5) % (childA == 0));
    childB = childB + c2 % ((us <= 0.5) % (childB == 0));
    childB = childB + c1 % ((us > 0.5) % (childB == 0));
}

//! Perform Polynomial mutation of the candidate.
template<typename MatType>
inline void AGEMOEA::Mutate(MatType& candidate,
       double mutationRate,
       const MatType& lowerBound,
       const MatType& upperBound)
{
    const size_t numVariables = candidate.n_rows;
    for (size_t geneIdx = 0; geneIdx < numVariables; ++geneIdx)
    {
      // Should this gene be mutated?
      if (arma::randu() > mutationRate)
        continue;

      const double geneRange = upperBound(geneIdx) - lowerBound(geneIdx);
      // Normalised distance from the bounds.
      const double lowerDelta = (candidate(geneIdx) - lowerBound(geneIdx)) / geneRange;
      const double upperDelta = (upperBound(geneIdx) - candidate(geneIdx)) / geneRange;
      const double mutationPower = 1. / (distributionIndex + 1.0);
      const double rand = arma::randu();
      double value, perturbationFactor;
      if (rand < 0.5)
      {
        value = 2.0 * rand + (1.0 - 2.0 * rand) *
            std::pow(upperDelta, distributionIndex + 1.0);
        perturbationFactor = std::pow(value, mutationPower) - 1.0;
      }
      else
      {
        value = 2.0 * (1.0 - rand) + 2.0 *(rand - 0.5) *
            std::pow(lowerDelta, distributionIndex + 1.0);
        perturbationFactor = 1.0 - std::pow(value, mutationPower);
      }

      candidate(geneIdx) += perturbationFactor * geneRange;
    }
    //! Enforce bounds.
    candidate = arma::min(arma::max(candidate, lowerBound), upperBound);
}

template <typename MatType>
void AGEMOEA::NormalizeFront(std::vector<MatType>& population,
                    arma::colvec& normalization,
                    const std::vector<size_t>& front,
                    const arma::Row<size_t>& extreme)
{
  arma::Mat<typename MatType::elem_type> vectorizedFront(numVariables, front.size());
  arma::Col<typename MatType::elem_type> temp;
  for (size_t i = 0; i< front.size(); i++)
  {
    vectorizedFront.col(i) = arma::vectorise(population[front[i]], 0); 
  }
  arma::uvec unique = arma::find_unique(extreme);
  if (extreme.n_elem != unique.n_elem)
  {
    normalization = arma::max(vectorizedFront, 1);
  }
  arma::colvec hyperplane = arma::solve(vectorizedFront, new arma::colvec(numVariables, arma::fill::ones));
  if (hyperplane.has_inf() || hyperplane.has_nan() || arma::accu(hyperplane < 0.0) > 0)
  {
    normalization = arma::max(vectorizedFront, 1);
  }
  normalization = 1. / hyperplane;   
  if (hyperplane.has_inf() || hyperplane.has_nan())
  {    
    normalization = arma::max(vectorizedFront, 1);
  }
  normalization = normalization + (normalization == 0);
  vectorizedFront.each_col( [normalization](arma::vec& a){ a = a / normalization; } );
  for (size_t i = 0; i< front.size(); i++)
  {
    temp = vectorizedFront.col(i);
    population[front[i]] = temp.reshape(arma::size(population[0]));
  }
}

template <typename MatType>
inline size_t AGEMOEA::GetGeometry(const std::vector<MatType>& population,
                    const std::vector<size_t>& front,
                    const std::vector<size_t>& extreme,
                    size_t m)
{
  arma::rowvec d = PointToLineDistance<MatType> (population, front, 
                                    new arma::rowvec(numVariables, arma::fill::zeros),
                                    new arma::rowvec(numVariables, arma::fill::ones));
  for (size_t i = 0; i < extreme.size(); i++)
  {
    d[extreme[i]] = arma::datum::inf;
  }
  size_t index = arma::index_min(d);
  double avg = arma::accu(population[front[index]]) / static_cast<double> (numVariables); 
  double p = std::log(m) / std::log(1.0 / avg);
  if (p <= 0.1 || std::isnan(p)) {p = 1.0;}
  return p;
}

//! Pairwise distance for each point in the given front.
template <typename MatType>
inline arma::mat& AGEMOEA::PairwiseDistance(std::vector<MatType>& population,
                                          const std::vector<size_t>& front,
                                          size_t dimension)
{
  arma::mat final(front.size(),front.size(),arma::fill::zeros);
  
  
  for (size_t i = 0; i < front.size(); i++)
  {
    for (size_t j = i + 1; i < front.size(); j++)
    {
      final(i, j) = std::pow(arma::accu(arma::pow(arma::abs(population[i] - population[j]), dimension)), 1.0 / dimension);
      final(j, i) = final(i, j);
    }
  }
  return final;
}

//! Find the index of the of the extreme points in the given front.
template <typename MatType>
inline arma::Row<size_t> AGEMOEA::FindExtremePoints(std::vector<MatType>& population,
                                            const std::vector<size_t>& front)
{
  if(numVariables >= front.size())
  {
    return front;
  }

  arma::mat W(numVariables, numVariables, arma::fill::eye);
  W = W + 1e-6;
  arma::Row<size_t> indexes(numVariables);
  std::vector<bool> selected(front.size());
  for (size_t i = 0; i < numVariables; i++)
  {
    arma::rowvec dists = PointToLineDistance<MatType>(population, 
         front, new arma::rowvec(numVariables, arma::fill::zeros), W.row(i));
    for (size_t j = 0; j < front.size(); i++)
      if (selected[j]){dists[i] = arma::datum::inf;}
    indexes[i] = front[dists.index_min()];
    selected[dists.index_min()] = true;
  }
  return indexes;
}

//! Find the distance of a front from a line formed by two points.
template<typename MatType>
arma::rowvec AGEMOEA::PointToLineDistance(const std::vector<MatType>& population,
                                       std::vector<size_t>& front,
                                       const arma::rowvec& pointA,
                                       const arma::rowvec& pointB)
{
  arma::rowvec distances(front.size());
  MatType ba = pointB - pointA; 
  MatType pa;

  for (size_t i = 0; i < front.size(); i++)
  {
        size_t ind = front[i];

        //! Points can be matrices as well for ease of objective calculation.
        //! Hence they may need to be vectorized row wise before 
        pa = (arma::vectorise(population[ind], 1) - pointA);
        double t = arma::dot(pa, ba) / arma::dot(ba, ba);
        distances[i] = arma::accu(arma::pow((pa - t % ba), 2));
  }
  return distances;
}

//! Sort population into Pareto fronts.
template<typename MatType>
inline void AGEMOEA::FastNonDominatedSort(
    std::vector<std::vector<size_t> >& fronts,
    std::vector<size_t>& ranks,
    std::vector<arma::Col<typename MatType::elem_type> >& calculatedObjectives)
{
  std::map<size_t, size_t> dominationCount;
  std::map<size_t, std::set<size_t> > dominated;

  // Reset and initialize fronts.
  fronts.clear();
  fronts.push_back(std::vector<size_t>());

  for (size_t p = 0; p < populationSize; p++)
  {
    dominated[p] = std::set<size_t>();
    dominationCount[p] = 0;

    for (size_t q = 0; q < populationSize; q++)
    {
      if (Dominates<MatType>(calculatedObjectives, p, q))
        dominated[p].insert(q);
      else if (Dominates<MatType>(calculatedObjectives, q, p))
        dominationCount[p] += 1;
    }

    if (dominationCount[p] == 0)
    {
      ranks[p] = 0;
      fronts[0].push_back(p);
    }
  }

  size_t i = 0;

  while (!fronts[i].empty())
  {
    std::vector<size_t> nextFront;

    for (size_t p: fronts[i])
    {
      for (size_t q: dominated[p])
      {
        dominationCount[q]--;

        if (dominationCount[q] == 0)
        {
          ranks[q] = i + 1;
          nextFront.push_back(q);
        }
      }
    }

    i++;
    fronts.push_back(nextFront);
  }
  // Remove the empty final set.
  fronts.pop_back();
}

//! Check if a candidate Pareto dominates another candidate.
template<typename MatType>
inline bool AGEMOEA::Dominates(
    std::vector<arma::Col<typename MatType::elem_type> >& calculatedObjectives,
    size_t candidateP,
    size_t candidateQ)
{
  bool allBetterOrEqual = true;
  bool atleastOneBetter = false;
  size_t n_objectives = calculatedObjectives[0].n_elem;

  for (size_t i = 0; i < n_objectives; i++)
  {
    // P is worse than Q for the i-th objective function.
    if (calculatedObjectives[candidateP](i) > calculatedObjectives[candidateQ](i))
      allBetterOrEqual = false;

    // P is better than Q for the i-th objective function.
    else if (calculatedObjectives[candidateP](i) < calculatedObjectives[candidateQ](i))
      atleastOneBetter = true;
  }

  return allBetterOrEqual && atleastOneBetter;
}

//! Assign survival score to the population.
template <typename MatType>
inline void AGEMOEA::SurvivalScoreAssignment(
    const std::vector<size_t>& front,
    size_t dimension,
    std::vector<arma::Col<typename MatType::elem_type>>& calculatedObjectives,
    std::vector<typename MatType::elem_type>& survivalScore)
{
  // Convenience typedefs.
  typedef typename MatType::elem_type ElemType;

  size_t fSize = front.size();
  // Stores the sorted indices of the fronts.
  arma::uvec sortedIdx  = arma::regspace<arma::uvec>(0, 1, fSize - 1);

  for (size_t m = 0; m < numObjectives; m++)
  {
    // Cache fValues of individuals for current objective.
    arma::Col<ElemType> fValues(fSize);
    std::transform(front.begin(), front.end(), fValues.begin(),
      [&](const size_t& individual)
        {
          return calculatedObjectives[individual](m);
        });

    // Sort front indices by ascending fValues for current objective.
    std::sort(sortedIdx.begin(), sortedIdx.end(),
      [&](const size_t& frontIdxA, const size_t& frontIdxB)
        {
          return (fValues(frontIdxA) < fValues(frontIdxB));
        });

    survivalScore[front[sortedIdx(0)]] =
        std::numeric_limits<ElemType>::max();
    survivalScore[front[sortedIdx(fSize - 1)]] =
        std::numeric_limits<ElemType>::max();
    ElemType minFval = fValues(sortedIdx(0));
    ElemType maxFval = fValues(sortedIdx(fSize - 1));
    ElemType scale =
        std::abs(maxFval - minFval) == 0. ? 1. : std::abs(maxFval - minFval);

    for (size_t i = 1; i < fSize - 1; i++)
    {
      survivalScore[front[sortedIdx(i)]] +=
          (fValues(sortedIdx(i + 1)) - fValues(sortedIdx(i - 1))) / scale;
    }
  }
}

//! Comparator for survival score based sorting.
template<typename MatType>
inline bool AGEMOEA::SurvivalScoreOperator(size_t idxP,
                                    size_t idxQ,
                                    const std::vector<size_t>& ranks,
                                    const std::vector<typename MatType::elem_type>& survivalScore)
{
  if (ranks[idxP] < ranks[idxQ])
    return true;
  else if (ranks[idxP] == ranks[idxQ] && survivalScore[idxP] > survivalScore[idxQ])
    return true;

  return false;
}

} // namespace ens

#endif