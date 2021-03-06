/**
 * @file Selectivity.cpp
 * @author Scott Rasmussen (scott.rasmussen@zaita.com)
 * @github https://github.com/Zaita
 * @date 12/10/2015
 * @section LICENSE
 *
 * Copyright NIWA Science �2015 - www.niwa.co.nz
 *
 */
#include "Selectivity.h"

#include "Model/Model.h"
#include "Selectivities/Manager.h"

namespace niwa {
namespace reports {

Selectivity::Selectivity(Model* model) : Report(model) {
  run_mode_    = (RunMode::Type)(RunMode::kBasic);
  model_state_ = (State::Type)(State::kIterationComplete);

  parameters_.Bind<string>(PARAM_SELECTIVITY, &selectivity_label_, "Selectivity name", "");
}

void Selectivity::DoValidate() {

}

void Selectivity::DoBuild() {
  selectivity_ = model_->managers().selectivity()->GetSelectivity(selectivity_label_);
  if (!selectivity_)
    LOG_FATAL_P(PARAM_SELECTIVITY) << " " << selectivity_label_ << " does not exist. Have you defined it?";

}


void Selectivity::DoExecute() {
  LOG_FINE();
  cache_ << "*"<< type_ << "[" << label_ << "]" << "\n";
  const map<string, Parameter*> parameters = selectivity_->parameters().parameters();

  for (auto iter : parameters) {
    Parameter* x = iter.second;
    cache_  << iter.first << ": ";

    vector<string> values = x->current_values();
    for (string value : values)
      cache_ << value << " ";
    cache_ << "\n";
  }

  cache_ << "Values " << REPORT_R_VECTOR << "\n";
  if (!selectivity_->is_length_based()) {
    LOG_FINE() << "Printing age based selectivity";
    for (unsigned i = 0; i < model_->age_spread(); ++i)
      cache_ << i + model_->min_age() << " " << selectivity_->GetResult(i) << "\n";
    ready_for_writing_ = true;
  } else {
    LOG_FINE() << "Printing length based selectivity";
    vector<float> length_mid_points = model_->length_bin_mid_points();
    for (unsigned length_ndx = 0; length_ndx <  length_mid_points.size(); ++length_ndx) {
      LOG_FINE() << "length ndx = " << length_ndx;
      LOG_FINE()  << " mid point = " << length_mid_points[length_ndx];
      cache_ << length_mid_points[length_ndx] << " " << selectivity_->GetResult(length_ndx) << "\n";
    }
    ready_for_writing_ = true;
  }
}


} /* namespace reports */
} /* namespace niwa */
