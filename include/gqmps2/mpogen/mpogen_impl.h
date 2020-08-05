// SPDX-License-Identifier: LGPL-3.0-only
/*
* Author: Rongyang Sun <sun-rongyang@outlook.com>
* Creation Date: 2019-09-27 17:38
* 
* Description: GraceQ/MPS2 project. Implantation details for MPO generator.
*/
#include "gqmps2/consts.h"     // kNullIntVec
#include "gqmps2/mpogen/mpogen.h"
#include "gqmps2/mpogen/symb_alg/coef_op_alg.h"
#include "gqten/gqten.h"

#include <iostream>
#include <iomanip>
#include <algorithm>    // is_sorted
#include <map>

#include <assert.h>     // assert

#ifdef Release
  #define NDEBUG
#endif


namespace gqmps2 {
using namespace gqten;


// Forward declarations.
template <typename TenElemType>
void AddOpToHeadMpoTen(
    GQTensor<TenElemType> *, const GQTensor<TenElemType> &, const long);

template <typename TenElemType>
void AddOpToTailMpoTen(
    GQTensor<TenElemType> *, const GQTensor<TenElemType> &, const long);

template <typename TenElemType>
void AddOpToCentMpoTen(
    GQTensor<TenElemType> *, const GQTensor<TenElemType> &,
    const long, const long);


/**
Create a MPO generator. Create a MPO generator using the sites of the system
which is described by a SiteVec.

@param site_vec The local Hilbert spaces of each sites of the system.
@param zero_div The zero value of the given quantum number type which is used
       to set the divergence of the MPO.

@since version 0.2.0
*/
template <typename TenElemType>
MPOGenerator<TenElemType>::MPOGenerator(
    const SiteVec & site_vec,
    const QN & zero_div
) : N_(site_vec.size), zero_div_(zero_div), fsm_(site_vec.size) {
  pb_out_vector_.reserve(N_);
  pb_in_vector_.reserve(N_);
  id_op_vector_.reserve(N_);
  for (size_t i = 0; i < N_; ++i) {
    pb_out_vector_.emplace_back(site_vec.sites[i]);
    pb_in_vector_.emplace_back(InverseIndex(site_vec.sites[i]));
    id_op_vector_.emplace_back(GenIdOpTen_(site_vec.sites[i]));
  }
  op_label_convertor_ = LabelConvertor<GQTensorT>(id_op_vector_[0]);
  std::vector<OpLabel> id_op_label_vector;
  for(auto iter = id_op_vector_.begin(); iter< id_op_vector_.end();iter++){
    id_op_label_vector.push_back(op_label_convertor_.Convert(*iter));
  }
  fsm_.ReplaceIdOpLabels(id_op_label_vector);

  coef_label_convertor_ = LabelConvertor<TenElemType>(TenElemType(1));
}


/**
The most generic API for adding a many-body term to the MPO generator. Notice
that the indexes of the operators have to be ascending sorted.

@param coef The coefficient of the term.
@param local_ops All the local (on-site) operators in the term.
@param local_ops_idxs The site indexes of these local operators.

@since version 0.2.0
*/
template <typename TenElemType>
void MPOGenerator<TenElemType>::AddTerm(
    const TenElemType coef,
    const GQTensorVec &local_ops,
    const std::vector<int> &local_ops_idxs
) {
  assert(local_ops.size() == local_ops_idxs.size());
  assert(std::is_sorted(local_ops_idxs.cbegin(), local_ops_idxs.cend()));
  assert(local_ops_idxs.back() < N_);
  if (coef == TenElemType(0)) { return; }   // If coef is zero, do nothing.

  auto coef_label = coef_label_convertor_.Convert(coef);
  int ntrvl_ops_idxs_head = local_ops_idxs.front();
  int ntrvl_ops_idxs_tail = local_ops_idxs.back();
  OpReprVec ntrvl_ops_reprs;
  for (int i = ntrvl_ops_idxs_head; i <= ntrvl_ops_idxs_tail; ++i) {
    auto poss_it = std::find(local_ops_idxs.cbegin(), local_ops_idxs.cend(), i);
    if (poss_it != local_ops_idxs.cend()) {     // Nontrivial operator
      auto local_op_loc = poss_it - local_ops_idxs.cbegin();    // Location of the local operator in the local operators list.
      auto op_label = op_label_convertor_.Convert(local_ops[local_op_loc]);
      if (local_op_loc == 0) {
        ntrvl_ops_reprs.push_back(OpRepr(coef_label, op_label));
      } else {
        ntrvl_ops_reprs.push_back(OpRepr(op_label));
      }
    } else {
      auto op_label = op_label_convertor_.Convert(id_op_vector_[i]);
      ntrvl_ops_reprs.push_back(OpRepr(op_label));
    }
  }
  assert(
      ntrvl_ops_reprs.size() == (ntrvl_ops_idxs_tail - ntrvl_ops_idxs_head + 1)
  );

  fsm_.AddPath(ntrvl_ops_idxs_head, ntrvl_ops_idxs_tail, ntrvl_ops_reprs);
}


/**
Add a many-body term defined by physical operators and insertion operators to
the MPO generator. The indexes of the operators have to be ascending sorted.

@param coef The coefficient of the term.
@param phys_ops Operators with physical meaning in this term. Like
       \f$c^{\dagger}\f$ operator in the \f$-t c^{\dagger}_{i} c_{j}\f$
       hopping term. Its size must be larger than 1.
@param phys_ops_idxs The corresponding site indexes of the physical operators.
@param inst_ops Operators which will be inserted between physical operators and
       also behind the last physical operator as a tail string. For example the
       Jordan-Wigner string operator.
@param inst_ops_idxs_set Each element defines the explicit site indexes of the
       corresponding inserting operator. If it is set to empty (default value),
       every site between the corresponding physical operators will be inserted
       a same insertion operator.

@since version 0.2.0
*/
template <typename TenElemType>
void MPOGenerator<TenElemType>::AddTerm(
    const TenElemType coef,
    const GQTensorVec &phys_ops,
    const std::vector<int> &phys_ops_idxs,
    const GQTensorVec &inst_ops,
    const std::vector<std::vector<int>> &inst_ops_idxs_set
) {
  assert(phys_ops.size() >= 2);
  assert(phys_ops.size() == phys_ops_idxs.size());
  assert(
      (inst_ops.size() == phys_ops.size() - 1) ||
      (inst_ops.size() == phys_ops.size())
  );
  if (inst_ops_idxs_set != kNullIntVecVec) {
    assert(inst_ops_idxs_set.size() == inst_ops.size());
  }

  GQTensorVec local_ops;
  std::vector<int> local_ops_idxs;
  for (int i = 0; i < phys_ops.size()-1; ++i) {
    local_ops.push_back(phys_ops[i]);
    local_ops_idxs.push_back(phys_ops_idxs[i]);
    if (inst_ops_idxs_set == kNullIntVecVec) {
      for (int j = phys_ops_idxs[i]+1; j < phys_ops_idxs[i+1]; ++j) {
        local_ops.push_back(inst_ops[i]);
        local_ops_idxs.push_back(j);
      }
    } else {
      for (auto inst_op_idx : inst_ops_idxs_set[i]) {
        local_ops.push_back(inst_ops[i]);
        local_ops_idxs.push_back(inst_op_idx);
      }
    }
  }
  // Deal with the last physical operator and possible insertion operator tail
  // string.
  local_ops.push_back(phys_ops.back());
  local_ops_idxs.push_back(phys_ops_idxs.back());
  if (inst_ops.size() == phys_ops.size()) {
    if (inst_ops_idxs_set == kNullIntVecVec) {
      for (int j = phys_ops_idxs.back(); j < N_; ++j) {
        local_ops.push_back(inst_ops.back());
        local_ops_idxs.push_back(j);
      }
    } else {
      for (auto inst_op_idx : inst_ops_idxs_set.back()) {
        local_ops.push_back(inst_ops.back());
        local_ops_idxs.push_back(inst_op_idx);
      }
    }
  }

  AddTerm(coef, local_ops, local_ops_idxs);
}


/**
Add one-body or two-body interaction term.

@param coef The coefficient of the term.
@param op1 The first physical operator for the term.
@param op1_idx The site index of the first physical operator.
@param op2 The second physical operator for the term.
@param op2_idx The site index of the second physical operator.
@param inst_op The insertion operator for the two-body interaction term.
@param inst_op_idxs The explicit site indexes of the insertion operator.

@since version 0.2.0
*/
template <typename TenElemType>
void MPOGenerator<TenElemType>::AddTerm(
    const TenElemType coef,
    const GQTensorT &op1,
    const int op1_idx,
    const GQTensorT &op2,
    const int op2_idx,
    const GQTensorT &inst_op,
    const std::vector<int> &inst_op_idxs
) {
  if (op2 == GQTensorT()) {     // One-body interaction term
    GQTensorVec local_ops = {op1};
    std::vector<int> local_ops_idxs = {op1_idx};
    AddTerm(coef, local_ops, local_ops_idxs);     // Use the most generic API
  } else {                      // Two-body interaction term
    assert(op2_idx != 0);
    if (inst_op == GQTensorT()) {     // Trivial insertion operator
      AddTerm(coef, {op1, op2}, {op1_idx, op2_idx});
    } else {                          // Non-trivial insertion operator
      if (inst_op_idxs == kNullIntVec) {    // Uniform insertion
        AddTerm(coef, {op1, op2}, {op1_idx, op2_idx}, {inst_op});
      } else {                              // Non-uniform insertion
        AddTerm(
            coef,
            {op1, op2}, {op1_idx, op2_idx},
            {inst_op}, {inst_op_idxs}
        );
      }
    }
  }
}


template <typename TenElemType>
typename MPOGenerator<TenElemType>::PGQTensorVec
MPOGenerator<TenElemType>::Gen(void) {
  auto fsm_comp_mat_repr = fsm_.GenCompressedMatRepr();
  auto label_coef_mapping = coef_label_convertor_.GetLabelObjMapping();
  auto label_op_mapping = op_label_convertor_.GetLabelObjMapping();
  
  // Print MPO tensors virtual bond dimension.
  for (auto &mpo_ten_repr : fsm_comp_mat_repr) {
    std::cout << std::setw(3) << mpo_ten_repr.cols << std::endl;
  }

  PGQTensorVec mpo(N_);
  Index trans_vb({QNSector(zero_div_, 1)}, OUT);
  std::vector<size_t> transposed_idxs;
  for (long i = 0; i < N_; ++i) {
    if (i == 0) {
      transposed_idxs = SortSparOpReprMatColsByQN_(
                            fsm_comp_mat_repr[i], trans_vb, label_op_mapping);
      mpo[i] = HeadMpoTenRepr2MpoTen_(
                   fsm_comp_mat_repr[i], trans_vb,
                   label_coef_mapping, label_op_mapping);
    } else if (i == N_-1) {
      fsm_comp_mat_repr[i].TransposeRows(transposed_idxs);
      auto lvb = InverseIndex(trans_vb);
      mpo[i] = TailMpoTenRepr2MpoTen_(
                   fsm_comp_mat_repr[i], lvb,
                   label_coef_mapping, label_op_mapping);
    } else {
      fsm_comp_mat_repr[i].TransposeRows(transposed_idxs);
      auto lvb = InverseIndex(trans_vb);
      transposed_idxs = SortSparOpReprMatColsByQN_(
                            fsm_comp_mat_repr[i], trans_vb, label_op_mapping);
      mpo[i] = CentMpoTenRepr2MpoTen_(
                   fsm_comp_mat_repr[i], lvb, trans_vb,
                   label_coef_mapping, label_op_mapping, i);
    }
  }
  return mpo;
}


template< typename TenElemType>
QN MPOGenerator<TenElemType>::CalcTgtRvbQN_(
    const size_t x, const size_t y, const OpRepr &op_repr,
    const GQTensorVec &label_op_mapping, const Index &trans_vb) {
  auto lvb = InverseIndex(trans_vb);
  auto coor_off_set_and_qnsct = lvb.CoorInterOffsetAndQnsct(x);
  auto lvb_qn = coor_off_set_and_qnsct.qnsct.qn;
  auto op0_in_op_repr = label_op_mapping[op_repr.GetOpLabelList()[0]];
  return zero_div_ - Div(op0_in_op_repr) + lvb_qn;
}


template <typename TenElemType>
std::vector<size_t> MPOGenerator<TenElemType>::SortSparOpReprMatColsByQN_(
    SparOpReprMat &op_repr_mat, Index &trans_vb,
    const GQTensorVec &label_op_mapping) {
  std::vector<QNSector> rvb_qnscts;
  std::vector<size_t> transposed_idxs;
  for (size_t y = 0; y < op_repr_mat.cols; ++y) {
    bool has_ntrvl_op = false;
    QN col_rvb_qn;
    for (size_t x = 0; x < op_repr_mat.rows; ++x) {
      auto elem = op_repr_mat(x, y);
      if (elem != kNullOpRepr) {
        auto rvb_qn = CalcTgtRvbQN_(
                          x, y, elem, label_op_mapping, trans_vb);  
        if (!has_ntrvl_op) {
          col_rvb_qn = rvb_qn;
          has_ntrvl_op = true; 
          bool has_qn = false;
          size_t offset = 0;
          for (auto &qnsct : rvb_qnscts) {
            if (qnsct.qn == rvb_qn) {
              qnsct.dim += 1;
              auto beg_it = transposed_idxs.begin();
              transposed_idxs.insert(beg_it+offset, y);
              has_qn = true;
              break;
            } else {
              offset += qnsct.dim;
            }
          }
          if (!has_qn) {
            rvb_qnscts.push_back(QNSector(rvb_qn, 1));
            auto beg_it = transposed_idxs.begin();
            transposed_idxs.insert(beg_it+offset, y);
          }
        } else {
          assert(rvb_qn == col_rvb_qn); 
        }
      }
    }
  }
  op_repr_mat.TransposeCols(transposed_idxs);
  trans_vb = Index(rvb_qnscts, OUT);
  return transposed_idxs;
}


template <typename TenElemType>
typename MPOGenerator<TenElemType>::GQTensorT *
MPOGenerator<TenElemType>::HeadMpoTenRepr2MpoTen_(
    const SparOpReprMat &op_repr_mat,
    const Index &rvb,
    const TenElemVec &label_coef_mapping, const GQTensorVec &label_op_mapping) {
  auto pmpo_ten = new GQTensorT({pb_in_vector_.front(), rvb, pb_out_vector_.front()});
  for (size_t y = 0; y < op_repr_mat.cols; ++y) {
    auto elem = op_repr_mat(0, y);
    if (elem != kNullOpRepr) {
      auto op = elem.Realize(label_coef_mapping, label_op_mapping);
      AddOpToHeadMpoTen(pmpo_ten, op, y);
    }
  }
  return pmpo_ten;
}


template <typename TenElemType>
typename MPOGenerator<TenElemType>::GQTensorT *
MPOGenerator<TenElemType>::TailMpoTenRepr2MpoTen_(
    const SparOpReprMat &op_repr_mat,
    const Index &lvb,
    const TenElemVec &label_coef_mapping, const GQTensorVec &label_op_mapping) {
  auto pmpo_ten = new GQTensor<TenElemType>({pb_in_vector_.back(), lvb, pb_out_vector_.back()});
  for (size_t x = 0; x < op_repr_mat.rows; ++x) {
    auto elem = op_repr_mat(x, 0);
    if (elem != kNullOpRepr) {
      auto op = elem.Realize(label_coef_mapping, label_op_mapping);
      AddOpToTailMpoTen(pmpo_ten, op, x);
    }
  }
  return pmpo_ten;
}


template <typename TenElemType>
typename MPOGenerator<TenElemType>::GQTensorT *
MPOGenerator<TenElemType>::CentMpoTenRepr2MpoTen_(
    const SparOpReprMat &op_repr_mat,
    const Index &lvb,
    const Index &rvb,
    const TenElemVec &label_coef_mapping, const GQTensorVec &label_op_mapping,
    const long site) {
  auto pmpo_ten = new GQTensor<TenElemType>({lvb, pb_in_vector_[site], pb_out_vector_[site], rvb});
  for (size_t x = 0; x < op_repr_mat.rows; ++x) {
    for (size_t y = 0; y < op_repr_mat.cols; ++y) {
      auto elem = op_repr_mat(x, y);
      if (elem != kNullOpRepr) {
        auto op = elem.Realize(label_coef_mapping, label_op_mapping);
        AddOpToCentMpoTen(pmpo_ten, op, x, y);
      }
    }
  }
  return pmpo_ten;
}


template <typename TenElemType>
void AddOpToHeadMpoTen(
    GQTensor<TenElemType> *pmpo_ten, const GQTensor<TenElemType> &rop, const long rvb_coor) {
  for (long bpb_coor = 0; bpb_coor < rop.indexes[0].dim; ++bpb_coor) {
    for (long tpb_coor = 0; tpb_coor < rop.indexes[1].dim; ++tpb_coor) {
      auto elem = rop.Elem({bpb_coor, tpb_coor});
      if (elem != 0.0) {
        (*pmpo_ten)({bpb_coor, rvb_coor, tpb_coor}) = elem;
      }
    }
  }
}


template <typename TenElemType>
void AddOpToTailMpoTen(
    GQTensor<TenElemType> *pmpo_ten, const GQTensor<TenElemType> &rop, const long lvb_coor) {
  for (long bpb_coor = 0; bpb_coor < rop.indexes[0].dim; ++bpb_coor) {
    for (long tpb_coor = 0; tpb_coor < rop.indexes[1].dim; ++tpb_coor) {
      auto elem = rop.Elem({bpb_coor, tpb_coor});
      if (elem != 0.0) {
        (*pmpo_ten)({bpb_coor, lvb_coor, tpb_coor}) = elem;
      }
    }
  }
}


template <typename TenElemType>
void AddOpToCentMpoTen(
    GQTensor<TenElemType> *pmpo_ten, const GQTensor<TenElemType> &rop,
    const long lvb_coor, const long rvb_coor) {
  for (long bpb_coor = 0; bpb_coor < rop.indexes[0].dim; ++bpb_coor) {
    for (long tpb_coor = 0; tpb_coor < rop.indexes[1].dim; ++tpb_coor) {
      auto elem = rop.Elem({bpb_coor, tpb_coor});
      if (elem != 0.0) {
        (*pmpo_ten)({lvb_coor, bpb_coor, tpb_coor, rvb_coor}) = elem;
      }
    }
  }
}


template <typename TenElemType>
GQTensor<TenElemType>
MPOGenerator<TenElemType>::GenIdOpTen_(const Index &pb_out) {
  auto pb_in = InverseIndex(pb_out);
  auto id_op = GQTensorT({pb_in, pb_out});
  for (long i = 0; i < pb_out.dim; ++i) { id_op({i, i}) = 1; }
  return id_op;
}
} /* gqmps2 */ 