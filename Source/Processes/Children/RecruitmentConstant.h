/**
 * @file RecruitmentConstant.h
 * @author C.Marsh
 * @github https://github.com/Craig44
 * @date 12/07/2018
 * @section LICENSE
 *
 *
 * @section DESCRIPTION
 *
 * This process is responsible for seeding new recruits into the world
 */
#ifndef SOURCE_PROCESSES_CHILDREN_RECRUITMENT_CONSTANT_H_
#define SOURCE_PROCESSES_CHILDREN_RECRUITMENT_CONSTANT_H_

// headers
#include "Processes/Process.h"
#include "Layers/Children/Numeric/Base/NumericLayer.h"
#include "DerivedQuantities/DerivedQuantity.h"
// namespaces
namespace niwa {
namespace processes {
using std::string;
/**
 * Class definition
 */
class RecruitmentConstant : public Process {
public:
  // methods
  explicit RecruitmentConstant(Model* model);
  virtual                     ~RecruitmentConstant() = default;
  void                        DoValidate() override final { };
  void                        DoBuild() override final;
  void                        DoReset() override final { };
  void                        DoExecute() override final;
  void                        FillReportCache(ostringstream& cache) override final;

protected:
  string                      ssb_label_;
  string                      recruitment_layer_label_;
  float                       b0_;
  layers::NumericLayer*       recruitment_layer_ = nullptr;
  DerivedQuantity*            derived_quantity_ = nullptr;
  float                       initial_scalar_;

  // Reporting containers that will be printed in FillReportCache() method
  map<unsigned, float>        recruits_by_year_;
  unsigned                    initial_recruits_;
  bool                        first_enter_execute_ = true;
  map<unsigned, float>        ssb_by_year_;
  float                       scalar_;
};

} /* namespace processes */
} /* namespace niwa */

#endif /* SOURCE_PROCESSES_CHILDREN_RECRUITMENT_CONSTANT_H_ */
