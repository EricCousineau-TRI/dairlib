#include "dircon_opt_constraints.h"
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

#include "drake/math/autodiff.h"
#include "drake/math/autodiff_gradient.h"


namespace drake {
namespace systems {
namespace trajectory_optimization {

using solvers::Binding;
using solvers::Constraint;
using solvers::MathematicalProgram;
using solvers::VectorXDecisionVariable;
using Eigen::VectorXd;
using Eigen::MatrixXd;

template <typename T>
DirconDynamicConstraint<T>::DirconDynamicConstraint(const RigidBodyTree<double>& tree, DirconKinematicDataSet<T>& constraints) :
  DirconDynamicConstraint(tree, constraints, tree.get_num_positions(), tree.get_num_velocities(), tree.get_num_actuators(), constraints.countConstraints()) {}

template <typename T>
DirconDynamicConstraint<T>::DirconDynamicConstraint(const RigidBodyTree<double>& tree, DirconKinematicDataSet<T>& constraints,
                                                 int num_positions, int num_velocities, int num_inputs, int num_kinematic_constraints)
    : Constraint(num_positions + num_velocities, 1 + 2 *(num_positions+ num_velocities) + (2 * num_inputs) + (4 * num_kinematic_constraints),
                 Eigen::VectorXd::Zero(num_positions + num_velocities), Eigen::VectorXd::Zero(num_positions + num_velocities)),
      num_states_{num_positions+num_velocities}, num_inputs_{num_inputs}, num_kinematic_constraints_{num_kinematic_constraints},
      num_positions_{num_positions}, num_velocities_{num_velocities} {
  tree_ = &tree;
  constraints_ = &constraints;
}

// template <typename T>
// void DirconDynamicConstraint<T>::DoEval(
//     const Eigen::Ref<const Eigen::VectorXd>& x,
//     Eigen::VectorXd& y) const {
//     AutoDiffVecXd y_t;
//     Eval(math::initializeAutoDiff(x), y_t);
//     y = math::autoDiffToValueMatrix(y_t);
// }

template <>
void DirconDynamicConstraint<double>::DoEval(
    const Eigen::Ref<const Eigen::VectorXd>& x,
    Eigen::VectorXd& y) const {
  EvaluateConstraint(x,y);
}

template <>
void DirconDynamicConstraint<AutoDiffXd>::DoEval(
    const Eigen::Ref<const Eigen::VectorXd>& x,
    Eigen::VectorXd& y) const {
  AutoDiffVecXd y_t;
  EvaluateConstraint(math::initializeAutoDiff(x), y_t);
}

template <>
void DirconDynamicConstraint<double>::DoEval(
    const Eigen::Ref<const AutoDiffVecXd>& x, AutoDiffVecXd& y) const {
  // forward differencing
    double dx = 1e-6;

    VectorXd x_val = math::autoDiffToValueMatrix(x);
    VectorXd y0,yi;
    EvaluateConstraint(x_val,y0);

    MatrixXd dy = MatrixXd(y0.size(),x_val.size());
    for (int i=0; i < x_val.size(); i++) {
      x_val(i) += dx;
      EvaluateConstraint(x_val,yi);
      x_val(i) -= dx;
      dy.col(i) = (yi - y0)/dx;
    }
    math::initializeAutoDiffGivenGradientMatrix(y0, dy, y);
}

template <>
void DirconDynamicConstraint<AutoDiffXd>::DoEval(
    const Eigen::Ref<const AutoDiffVecXd>& x, AutoDiffVecXd& y) const {
  EvaluateConstraint(x,y);
}

// The format of the input to the eval() function is the
// tuple { timestep, state 0, state 1, input 0, input 1, force 0, force 1},
// which has a total length of 1 + 2*num_states + 2*num_inputs + dim*num_constraints.
template <typename T>
void DirconDynamicConstraint<T>::EvaluateConstraint(
    const Eigen::Ref<const VectorX<T>>& x, VectorX<T>& y) const {
  DRAKE_ASSERT(x.size() == 1 + (2 * num_states_) + (2 * num_inputs_) + 4*(num_kinematic_constraints_));

  // Extract our input variables:
  // h - current time (knot) value
  // x0, x1 state vector at time steps k, k+1
  // u0, u1 input vector at time steps k, k+1
  const auto h = x(0);
  const auto x0 = x.segment(1, num_states_);
  const auto x1 = x.segment(1 + num_states_, num_states_);
  const auto u0 = x.segment(1 + (2 * num_states_), num_inputs_);
  const auto u1 = x.segment(1 + (2 * num_states_) + num_inputs_, num_inputs_);
  const auto l0 = x.segment(1 + 2 * (num_states_ + num_inputs_), num_kinematic_constraints_);
  const auto l1 = x.segment(1 + 2 * (num_states_ + num_inputs_) + num_kinematic_constraints_, num_kinematic_constraints_);
  const auto lc = x.segment(1 + 2 * (num_states_ + num_inputs_) + 2*num_kinematic_constraints_, num_kinematic_constraints_);
  const auto vc = x.segment(1 + 2 * (num_states_ + num_inputs_) + 3*num_kinematic_constraints_, num_kinematic_constraints_);

  constraints_->updateData(x0, u0, l0);
  const auto xdot0 = constraints_->getXDot();

  constraints_->updateData(x1, u1, l1);
  const auto xdot1 = constraints_->getXDot();
  
  // Cubic interpolation to get xcol and xdotcol.
  const auto xcol = 0.5 * (x0 + x1) + h / 8 * (xdot0 - xdot1);
  const auto xdotcol = -1.5 * (x0 - x1) / h - .25 * (xdot0 + xdot1);

  constraints_->updateData(xcol, 0.5 * (u0 + u1), lc);
  auto g = constraints_->getXDot();
  g.head(num_positions_) += constraints_->getJ().transpose()*vc;
  y = xdotcol - g;
}

template <typename T>
Binding<Constraint> AddDirconConstraint(
    std::shared_ptr<DirconDynamicConstraint<T>> constraint,
    const Eigen::Ref<const VectorXDecisionVariable>& timestep,
    const Eigen::Ref<const VectorXDecisionVariable>& state,
    const Eigen::Ref<const VectorXDecisionVariable>& next_state,
    const Eigen::Ref<const VectorXDecisionVariable>& input,
    const Eigen::Ref<const VectorXDecisionVariable>& next_input,
    const Eigen::Ref<const solvers::VectorXDecisionVariable>& force,
    const Eigen::Ref<const solvers::VectorXDecisionVariable>& next_force,
    const Eigen::Ref<const solvers::VectorXDecisionVariable>& collocation_force,
    const Eigen::Ref<const solvers::VectorXDecisionVariable>& collocation_position_slack,
    MathematicalProgram* prog) {
  DRAKE_DEMAND(timestep.size() == 1);
  DRAKE_DEMAND(state.size() == constraint->num_states());
  DRAKE_DEMAND(next_state.size() == constraint->num_states());
  DRAKE_DEMAND(input.size() == constraint->num_inputs());
  DRAKE_DEMAND(next_input.size() == constraint->num_inputs());
  DRAKE_DEMAND(force.size() == constraint->num_kinematic_constraints());
  DRAKE_DEMAND(next_force.size() == constraint->num_kinematic_constraints());
  DRAKE_DEMAND(collocation_force.size() == constraint->num_kinematic_constraints());
  DRAKE_DEMAND(collocation_position_slack.size() == constraint->num_kinematic_constraints());
  return prog->AddConstraint(constraint,
                             {timestep, state, next_state, input, next_input, force,
                              next_force, collocation_force, collocation_position_slack});
}

template <typename T>
DirconKinematicConstraint<T>::DirconKinematicConstraint(const RigidBodyTree<double>& tree, DirconKinematicDataSet<T>& constraints,
                            DirconKinConstraintType type) :
  DirconKinematicConstraint(tree, constraints, std::vector<bool>(constraints.countConstraints(), false), type,
                            tree.get_num_positions(), tree.get_num_velocities(), tree.get_num_actuators(), constraints.countConstraints()) {}

template <typename T>
DirconKinematicConstraint<T>::DirconKinematicConstraint(const RigidBodyTree<double>& tree, DirconKinematicDataSet<T>& constraints,
                            std::vector<bool> is_constraint_relative, DirconKinConstraintType type) :
  DirconKinematicConstraint(tree, constraints, is_constraint_relative, type,
                            tree.get_num_positions(), tree.get_num_velocities(), tree.get_num_actuators(), constraints.countConstraints()) {}

template <typename T>
DirconKinematicConstraint<T>::DirconKinematicConstraint(const RigidBodyTree<double>& tree, DirconKinematicDataSet<T>& constraints,
                                                     std::vector<bool> is_constraint_relative, DirconKinConstraintType type, int num_positions,
                                                    int num_velocities, int num_inputs, int num_kinematic_constraints)
    : Constraint(type*num_kinematic_constraints, num_positions + num_velocities + num_inputs + num_kinematic_constraints + std::count(is_constraint_relative.begin(),is_constraint_relative.end(),true),
                 Eigen::VectorXd::Zero(type*num_kinematic_constraints), Eigen::VectorXd::Zero(type*num_kinematic_constraints)),
      num_states_{num_positions+num_velocities}, num_inputs_{num_inputs}, num_kinematic_constraints_{num_kinematic_constraints},
      num_positions_{num_positions}, num_velocities_{num_velocities}, type_{type}, is_constraint_relative_{is_constraint_relative},
      n_relative_{(int) std::count(is_constraint_relative.begin(),is_constraint_relative.end(),true)} {
  tree_ = &tree;
  constraints_ = &constraints;
  relative_map_ = MatrixXd::Zero(num_kinematic_constraints_,n_relative_);
  int j = 0;
  for (int i=0; i < num_kinematic_constraints_; i++) {
    if(is_constraint_relative_[i]) {
      relative_map_(i,j) = 1;
      j++;
    }
  }
}

template <>
void DirconKinematicConstraint<double>::DoEval(
    const Eigen::Ref<const Eigen::VectorXd>& x,
    Eigen::VectorXd& y) const {
  EvaluateConstraint(x,y);
}

template <>
void DirconKinematicConstraint<AutoDiffXd>::DoEval(
    const Eigen::Ref<const Eigen::VectorXd>& x,
    Eigen::VectorXd& y) const {
  AutoDiffVecXd y_t;
  EvaluateConstraint(math::initializeAutoDiff(x), y_t);
}

template <>
void DirconKinematicConstraint<double>::DoEval(
    const Eigen::Ref<const AutoDiffVecXd>& x, AutoDiffVecXd& y) const {
  // forward differencing
    double dx = 1e-6;

    VectorXd x_val = math::autoDiffToValueMatrix(x);
    VectorXd y0,yi;
    EvaluateConstraint(x_val,y0);

    MatrixXd dy = MatrixXd(y0.size(),x_val.size());
    for (int i=0; i < x_val.size(); i++) {
      x_val(i) += dx;
      EvaluateConstraint(x_val,yi);
      x_val(i) -= dx;
      dy.col(i) = (yi - y0)/dx;
    }
    math::initializeAutoDiffGivenGradientMatrix(y0, dy, y);
}

template <>
void DirconKinematicConstraint<AutoDiffXd>::DoEval(
    const Eigen::Ref<const AutoDiffVecXd>& x, AutoDiffVecXd& y) const {
  EvaluateConstraint(x,y);
}


template <typename T>
void DirconKinematicConstraint<T>::EvaluateConstraint(
    const Eigen::Ref<const VectorX<T>>& x, VectorX<T>& y) const {
  DRAKE_ASSERT(x.size() == num_states_ + num_inputs_ + num_kinematic_constraints_ + n_relative_);

  // Extract our input variables:
  // h - current time (knot) value
  // x0, x1 state vector at time steps k, k+1
  // u0, u1 input vector at time steps k, k+1
  const auto state = x.segment(0, num_states_);
  const auto input = x.segment(num_states_, num_inputs_);
  const auto force = x.segment(num_states_ + num_inputs_, num_kinematic_constraints_);
  const auto offset = x.segment(num_states_ + num_inputs_ + num_kinematic_constraints_, n_relative_);
  constraints_->updateData(state, input, force);
  switch(type_) {
    case kAll:
      y = VectorX<T>(3*num_kinematic_constraints_);
      y << constraints_->getC() + relative_map_*offset, constraints_->getCDot(), constraints_->getCDDot();
      break;
    case kAccelAndVel:
      y = VectorX<T>(2*num_kinematic_constraints_);
      y << constraints_->getCDot(), constraints_->getCDDot();
      break;
    case kAccelOnly:
      y = VectorX<T>(1*num_kinematic_constraints_);
      y << constraints_->getCDDot();
      break;
  }
}

// Explicitly instantiates on the most common scalar types.
template class DirconDynamicConstraint<double>;
template class DirconDynamicConstraint<AutoDiffXd>;
template class DirconKinematicConstraint<double>;
template class DirconKinematicConstraint<AutoDiffXd>;


} //drake
} //systems
} //trajectoryoptimization