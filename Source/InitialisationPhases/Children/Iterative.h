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

#include "Layers/Children/NumericLayer.h"
#include "Layers/Children/CategoricalLayer.h"
#include "World/WorldView.h"

// namespaces
namespace niwa {
class TimeStep;
class NumericLayer;
class WorldView;

namespace initialisationphases {
using std::string;
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

  // members
  unsigned                    years_;
  vector<TimeStep*>           time_steps_;
  unsigned                    number_agents_;
  string                      intial_layer_label_;
  string                      recruitement_layer_label_;
  niwa::layers::NumericLayer* initial_layer_ = nullptr;
  niwa::layers::CategoricalLayer* recruitement_layer_ = nullptr;

  string                      recruitment_label_for_single_cases_;
  bool                        single_recruitment_case_ = false;
  WorldView*                  world_ = nullptr;
  string					            growth_process_label_;
  string					            natural_mortality_label_;
  float                       shortcut_m_;

};

} /* namespace initialisationphases */
} /* namespace niwa */

#endif /* ITERATIVE_H_ */
