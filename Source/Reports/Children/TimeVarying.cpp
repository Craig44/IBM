/**
 * @file TimeVarying.cpp
 * @author  C. Marsh
 * @date 01/06/2016
 * @section LICENSE
 *
 * Copyright NIWA Science �2016 - www.niwa.co.nz
 *
 */

#include "TimeVarying.h"

#include "TimeVarying/Manager.h"

namespace niwa {
namespace reports {

/**
 *
 */
TimeVarying::TimeVarying(Model* model) : Report(model) {
  //run_mode_    = (RunMode::Type)(RunMode::kBasic | RunMode::kSimulation);
  model_state_ = (State::Type)(State::kIterationComplete);
}
/**
 * Validate inputs
 */
void TimeVarying::DoValidate() {
  if ((model_->run_mode() == RunMode::kMSE) || (model_->run_mode() == RunMode::kBasic)|| (model_->run_mode() == RunMode::kSimulation))
    run_mode_ = model_->run_mode();

  if ((model_->run_mode() == RunMode::kMSE))
    model_state_ = State::kInputIterationComplete;
}
/**
 *
 */
void TimeVarying::DoExecute() {
  LOG_TRACE();
  timevarying::Manager& manager = *model_->managers().time_varying();
  auto time_varying = manager.objects();
  cache_ << "*"<< type_ << "[" << label_ << "]" << "\n";

  for (auto time_var : time_varying) {
    string label =  time_var->label();
    LOG_FINEST() << "Reporting for @time_varying block " << label;
    cache_ << label << " " << REPORT_R_DATAFRAME << "\n";

    map<unsigned, float>& parameter_by_year = time_var->get_parameter_by_year();
    cache_ << "year" << " Value \n";
    for (auto param : parameter_by_year) {
      cache_ << param.first << "  " << AS_DOUBLE(param.second) << "\n";
    }
    ready_for_writing_ = true;
  }
}

} /* namespace reports */
} /* namespace niwa */
