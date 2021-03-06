/**
 * @file DoubleExponential.h
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
#ifndef DOUBLEEXPONENTIAL_H_
#define DOUBLEEXPONENTIAL_H_

// Headers
#include "Selectivities/Selectivity.h"

// Namespaces
namespace niwa {
namespace selectivities {

/**
 * Class Definition
 */
class DoubleExponential : public niwa::Selectivity {
public:
  // Methods
  explicit DoubleExponential(Model* model);
  virtual                     ~DoubleExponential() = default;
  void                        DoValidate() override final;
  void                        RebuildCache() override final;

protected:
  //Methods

private:
  // Members
  float                      x0_;
  float                      x1_;
  float                      x2_;
  float                      y0_;
  float                      y1_;
  float                      y2_;
  float                      alpha_;
};

} /* namespace selectivities */
} /* namespace niwa */
#endif /* DOUBLEEXPONENTIAL_H_ */
