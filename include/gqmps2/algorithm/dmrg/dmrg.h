// SPDX-License-Identifier: LGPL-3.0-only

/*
* Author: Hao-Xin Wang <wanghx18@mails.tsinghua.edu.cn>
* Creation Date: 2023-04-22
*
* Description: GraceQ/MPS2 project. DMRG.
*/

#ifndef GQMPS2_ALGORITHM_DMRG_DMRG_H
#define GQMPS2_ALGORITHM_DMRG_DMRG_H

#include "gqmps2/one_dim_tn/mat_repr_mpo.h"

namespace gqmps2 {
template<typename TenT>
using RightBlockOperatorGroup = std::vector<TenT>; // right block == environment

template<typename TenT>
using LeftBlockOperatorGroup = std::vector<TenT>; //left block == system

template<typename TenT>
using BlockOperatorGroup = std::vector<TenT>;

template<typename TenT>
using BlockSiteHamiltonianTerm = std::array<TenT *, 2>;

template<typename TenT>
using BlockSiteHamiltonianTermGroup = std::vector<BlockSiteHamiltonianTerm<TenT>>;

template<typename TenT>
using SiteBlockHamiltonianTerm = std::array<TenT *, 2>;

template<typename TenT>
using SiteBlockHamiltonianTermGroup = std::vector<SiteBlockHamiltonianTerm<TenT>>;

template<typename TenT>
using SuperBlockHamiltonianTerms = std::vector<std::pair<BlockSiteHamiltonianTermGroup<TenT>,
                                                         SiteBlockHamiltonianTermGroup<TenT>>>;

template<typename TenT>
using EffectiveHamiltonianTerm = std::array<TenT *, 4>;

template<typename TenT>
using EffectiveHamiltonianTermGroup = std::vector<EffectiveHamiltonianTerm<TenT>>;

template<typename TenT>
class EffectiveHamiltonian {
 public:
  EffectiveHamiltonianTermGroup<TenT> GetEffectiveHamiltonianTermGroup() {

  }

  RightBlockOperatorGroup<TenT> right_op_gp;
  LeftBlockOperatorGroup<TenT> left_op_gp;
  MatReprMPO<TenT> mat_repr_mpo_a; //left site
  MatReprMPO<TenT> mat_repr_mpo_b; //right site
};

}

#include "gqmps2/algorithm/dmrg/dmrg_impl.h"
#include "gqmps2/algorithm/dmrg/dmrg_init.h"

#endif //GQMPS2_ALGORITHM_DMRG_DMRG_H