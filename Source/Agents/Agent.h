/**
 * @file Agent.h
 * @author  C.Marsh
 * @version 1.0
 * @date 11/06/2018
 * @section LICENSE
 *
 * Copyright
 *
 * @section The Agent class has describes all the agents characteristics, and has the methods that control a single agents fate. For example natural mortality and movement.
 * Other processes such as recruitment are defined at the partition level.
 *
 */
#ifndef AGENT_H_
#define AGENT_H_

// Headers
#include "BaseClasses/Object.h"

// Namespaces
namespace niwa {
class Model;

/**
 * Class Definition
 */
class Agent { // Don't make this inherit from BaseClasses/Object.h
public:
  // Methods
  virtual                       ~Agent() = default;
  Agent(double first_growth_par, double second_growth_par, double M, unsigned age);
  virtual void                  Reset() {};
  // Accessors
  unsigned                     age() const {return age_;};
  bool                         is_alive() const {return alive_ ;};


  //Dynamices
  void                         survival();
protected:
  // Methods

  // Members
  double                      first_growth_par_;  // L_inf for von bert
  double                      second_growth_par_; // k for von bert
  double                      survival_; // natural mortality
  unsigned                    age_;
  bool                        alive_ = true;
  //


};
} /* namespace niwa */

#endif /* AGENT_H_ */
