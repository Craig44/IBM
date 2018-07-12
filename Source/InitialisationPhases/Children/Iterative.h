/**
 * @file Iterative.h
 * @author  Scott Rasmussen (scott.rasmussen@zaita.com)
 * @date 2/09/2014
 * @section LICENSE
 *
 * Copyright NIWA Science �2014 - www.niwa.co.nz
 *
 * @section DESCRIPTION
 *
 * This type of initialisation phases is our de-facto one that
 * does a year/time-step iterative approach.
 */
#ifndef INITIALISATIONPHASES_ITERATIVE_H_
#define INITIALISATIONPHASES_ITERATIVE_H_

// headers
#include "InitialisationPhases/InitialisationPhase.h"

#include "Layers/Children/Numeric/Base/NumericLayer.h"
#include "World/WorldView.h"

// namespaces
namespace niwa {
class TimeStep;
class NumericLayer;
class WorldView;

namespace initialisationphases {

/**
 *
 */
class Iterative : public niwa::InitialisationPhase {
public:
  // methods
  explicit Iterative(Model* model);
  virtual                     ~Iterative() = default;
  void                        Execute() override final;

protected:
  // methods
  void                        DoValidate() override final;
  void                        DoBuild() override final;
  bool                        CheckConvergence();

  // members
  unsigned                    years_;
  vector<TimeStep*>           time_steps_;
  Double                      lambda_;
  vector<unsigned>            convergence_years_;
  unsigned                    number_agents_;
  std::string                 intial_layer_label_ = "";
  niwa::layers::NumericLayer* initial_layer_ = nullptr;
  WorldView*                  world_ = nullptr;



};

} /* namespace initialisationphases */
} /* namespace niwa */

#endif /* ITERATIVE_H_ */