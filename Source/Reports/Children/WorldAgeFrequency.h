/**
 * @file WorldAgeFrequency.h
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 15/07/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 * This report will take print the state of the partition at two points in the initialisation phase.
 * After the approximation, and after the entire initialisation phase.
 */
#ifndef SOURCE_REPORTS_CHILDREN_WORLD_AGE_FREQUENCY_H_
#define SOURCE_REPORTS_CHILDREN_WORLD_AGE_FREQUENCY_H_

// headers
#include "Reports/Report.h"

// namespaces
namespace niwa {
class WorldView;

namespace reports {

// classes
class WorldAgeFrequency : public Report {
public:
  WorldAgeFrequency(Model* model);
  virtual                     ~WorldAgeFrequency() = default;
  void                        DoValidate() override final { };
  void                        DoBuild() override final;
  void                        DoExecute() override final;
  void                        DoReset() override final  { };


private:
  bool                        first_run_ = true;
  WorldView*                  world_;
};

} /* namespace reports */
} /* namespace niwa */

#endif /* SOURCE_REPORTS_CHILDREN_WORLD_AGE_FREQUENCY_H_ */
