/**
 * @file floatNormal.h
 * @author  Scott Rasmussen (scott.rasmussen@zaita.com)
 * @version 1.0
 * @date 14/01/2013
 * @section LICENSE
 *
 * Copyright NIWA Science �2013 - www.niwa.co.nz
 *
 * @section DESCRIPTION
 *
 * The time class represents a moment of time.
 *
 * $Date: 2008-03-04 16:33:32 +1300 (Tue, 04 Mar 2008) $
 */
#ifndef DOUBLENORMAL_H_
#define DOUBLENORMAL_H_

// Headers
#include "Selectivities/Selectivity.h"

// Namespaces
namespace niwa {
namespace selectivities {

/**
 * Class definition
 */
class DoubleNormal : public niwa::Selectivity {
public:
  // Methods
  explicit DoubleNormal(Model* model);
  virtual                     ~DoubleNormal() = default;
  void                        DoValidate() override final;
  void                        RebuildCache() override final;

protected:
  //Methods

private:
  // Members
  float                      mu_;
  float                      sigma_l_;
  float                      sigma_r_;
  float                      alpha_;
};

} /* namespace selectivities */
} /* namespace niwa */
#endif /* DOUBLENORMAL_H_ */
