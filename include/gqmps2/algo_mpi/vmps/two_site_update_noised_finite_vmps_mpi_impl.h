// SPDX-License-Identifier: LGPL-3.0-only

/*
* Author: Hao-Xin Wang <wanghx18@mails.tsinghua.edu.cn>
* Creation Date: 2021-08-31
*
* Description: GraceQ/MPS2 project. Two-site update noised finite size vMPS with MPI Paralization
*/

/**
@file two_site_update_noised_finite_vmps_mpi_impl.h
@brief Two-site update noised finite size vMPS with MPI Paralization
*/
#ifndef GQMPS2_ALGO_MPI_VMPS_TWO_SITE_UPDATE_NOISED_FINITE_VMPS_MPI_IMPL_H
#define GQMPS2_ALGO_MPI_VMPS_TWO_SITE_UPDATE_NOISED_FINITE_VMPS_MPI_IMPL_H



#include <stdlib.h>
#include "gqten/gqten.h"
#include "gqmps2/algorithm/lanczos_solver.h"                        //LanczosParams
#include "boost/mpi.hpp"                                            //boost::mpi
#include "gqmps2/algo_mpi/framework.h"                              //VMPSORDER
#include "gqmps2/algo_mpi/vmps/vmps_mpi_init.h"                     //MPI vmps initial
#include "gqmps2/algo_mpi/vmps/two_site_update_finite_vmps_mpi.h"   //TwoSiteMPIVMPSSweepParams
#include "gqmps2/algo_mpi/vmps/two_site_update_noised_finite_vmps_mpi.h" //TwoSiteMPINoisedVMPSSweepParams
#include "gqmps2/algo_mpi/lanczos_solver_mpi.h"                     //MPI Lanczos solver  
#include "gqmps2/algo_mpi/vmps/two_site_update_finite_vmps_mpi_impl.h" //SlaveTwoSiteFiniteVMPS

namespace gqmps2 {
using namespace gqten;
template <typename TenElemT, typename QNT>
inline GQTEN_Double TwoSiteFiniteVMPS(
    FiniteMPS<TenElemT, QNT> &mps,
    const MPO<GQTensor<TenElemT, QNT>> &mpo,
    const TwoSiteMPINoisedVMPSSweepParams &sweep_params,
    mpi::communicator& world
){
  GQTEN_Double e0(0.0);
  if(world.rank()== kMasterRank){
    e0 = MasterTwoSiteFiniteVMPS(mps,mpo,sweep_params,world);
  }else{
    SlaveTwoSiteFiniteVMPS<TenElemT, QNT>(world);
  }
  return e0;
}


template <typename TenElemT, typename QNT>
GQTEN_Double MasterTwoSiteFiniteVMPS(
  FiniteMPS<TenElemT, QNT> &mps,
  const MPO<GQTensor<TenElemT, QNT>> &mpo,
  TwoSiteMPINoisedVMPSSweepParams &sweep_params,
  mpi::communicator world
) {
assert(mps.size() == mpo.size());
std::cout << "***** Two-Site Noised Update VMPS Program (with MPI Parallel) *****" << "\n";
MasterBroadcastOrder(program_start, world );
auto [left_boundary, right_boundary]=TwoSiteFiniteVMPSInit(mps,mpo,(TwoSiteMPIVMPSSweepParams)sweep_params,world);
std::cout << "Preseted noises: \t[";
for(size_t i = 0; i < sweep_params.noises.size(); i++){
  std::cout << sweep_params.noises[i];
  if (i!=sweep_params.noises.size()-1) {
    std::cout << ", ";
  } else {
    std::cout << "]" << std::endl;
  }
}
if (sweep_params.noises.empty()) { sweep_params.noises.push_back(0.0); }
double e0(0.0);
double noise;
mps.LoadTen(left_boundary, GenMPSTenName(sweep_params.mps_path, left_boundary));
mps.LoadTen(left_boundary+1, GenMPSTenName(sweep_params.mps_path, left_boundary+1));
for (size_t sweep = 1; sweep <= sweep_params.sweeps; ++sweep) {
  if ((sweep - 1) < sweep_params.noises.size()) {
    noise = sweep_params.noises[sweep-1];
  }
  std::cout << "sweep " << sweep << std::endl;
  Timer sweep_timer("sweep");
  e0 = TwoSiteFiniteVMPSSweep(mps, mpo, sweep_params, 
                            left_boundary, right_boundary,
                            noise,  world);
  
  
  sweep_timer.PrintElapsed();
  std::cout << "\n";
}
mps.DumpTen(left_boundary, GenMPSTenName(sweep_params.mps_path, left_boundary), true);
mps.DumpTen(left_boundary+1, GenMPSTenName(sweep_params.mps_path, left_boundary+1), true);
MasterBroadcastOrder(program_final, world);
return e0;
}

template <typename TenElemT, typename QNT>
double TwoSiteFiniteVMPSSweep(
    FiniteMPS<TenElemT, QNT> &mps,
    const MPO<GQTensor<TenElemT, QNT>> &mpo,
    const TwoSiteMPINoisedVMPSSweepParams &sweep_params,
    const size_t left_boundary,
    const size_t right_boundary,
    const double noise,
    mpi::communicator world
) {
  auto N = mps.size();
  using TenT = GQTensor<TenElemT, QNT>;
  TenVec<TenT> lenvs(N - 1);
  TenVec<TenT> renvs(N - 1);
  double e0;

  for (size_t i = left_boundary; i <= right_boundary - 2; ++i) {
    // Load to-be-used tensors
    LoadRelatedTensOnTwoSiteAlgWhenRightMoving(mps, lenvs, renvs, i, left_boundary, sweep_params);
    e0 = MasterTwoSiteFiniteVMPSUpdate(mps, lenvs, renvs, mpo, sweep_params, 'r', i, noise,world);
    // Dump related tensor to HD and remove unused tensor from RAM
    DumpRelatedTensOnTwoSiteAlgWhenRightMoving(mps, lenvs, renvs, i, (TwoSiteMPIVMPSSweepParams) sweep_params);
  }
  for (size_t i = right_boundary; i >= left_boundary+2; --i) {
    LoadRelatedTensOnTwoSiteAlgWhenLeftMoving(mps, lenvs, renvs, i, right_boundary, sweep_params);
    e0 = MasterTwoSiteFiniteVMPSUpdate(mps, lenvs, renvs, mpo, sweep_params, 'l', i, noise, world);
    DumpRelatedTensOnTwoSiteAlgWhenLeftMoving(mps, lenvs, renvs, i,  (TwoSiteMPIVMPSSweepParams) sweep_params);
  }
  return e0;
}



template <typename TenElemT, typename QNT>
double MasterTwoSiteFiniteVMPSUpdate(
    FiniteMPS<TenElemT, QNT> &mps,
    TenVec<GQTensor<TenElemT, QNT>> &lenvs,
    TenVec<GQTensor<TenElemT, QNT>> &renvs,
    const MPO<GQTensor<TenElemT, QNT>> &mpo,
    const TwoSiteMPINoisedVMPSSweepParams &sweep_params,
    const char dir,
    const size_t target_site,
    const double noise,
    mpi::communicator& world
) {
    //master
  Timer update_timer("two_site_fvmps_update");
#ifdef GQMPS2_TIMING_MODE
  Timer initialize_timer("two_site_fvmps_setup_and_initial_state");
#endif
  // Assign some parameters
  auto N = mps.size();
  std::vector<std::vector<size_t>> init_state_ctrct_axes;
  size_t svd_ldims;
  size_t lsite_idx, rsite_idx;
  size_t lenv_len, renv_len;
  std::string lblock_file, rblock_file;
  init_state_ctrct_axes = {{2}, {0}};
  svd_ldims = 2;
  switch (dir) {
    case 'r':
      lsite_idx = target_site;
      rsite_idx = target_site + 1;
      lenv_len = target_site;
      renv_len = N - (target_site + 2);
      break;
    case 'l':
      lsite_idx = target_site - 1;
      rsite_idx = target_site;
      lenv_len = target_site - 1;
      renv_len = N - target_site - 1;
      break;
    default:
      std::cout << "dir must be 'r' or 'l', but " << dir << std::endl;
      exit(1);
  }

  // Lanczos
  using TenT = GQTensor<TenElemT, QNT>;
  std::vector<TenT *>eff_ham(4);
  eff_ham[0] = lenvs(lenv_len);
  // Safe const casts for MPO local tensors.
  eff_ham[1] = const_cast<TenT *>(&mpo[lsite_idx]);
  eff_ham[2] = const_cast<TenT *>(&mpo[rsite_idx]);
  eff_ham[3] = renvs(renv_len);
  

  auto init_state = new TenT;
  Contract(&mps[lsite_idx], &mps[rsite_idx], init_state_ctrct_axes, init_state);
#ifdef GQMPS2_TIMING_MODE
  initialize_timer.PrintElapsed();
#endif
  Timer lancz_timer("two_site_fvmps_lancz");
  MasterBroadcastOrder(lanczos, world);
  auto lancz_res = MasterLanczosSolver(
                       eff_ham, init_state,
                       sweep_params.lancz_params,
                       world
                   );
#ifdef GQMPS2_TIMING_MODE
  auto lancz_elapsed_time = lancz_timer.PrintElapsed();
#else
  auto lancz_elapsed_time = lancz_timer.Elapsed();
#endif
  if (fabs(noise)>=1e-10) {
    if(dir=='r'){
      MasterBroadcastOrder(contract_for_right_moving_expansion, world);
      MasterTwoSiteFiniteVMPSRightMovingExpand(
        mps, 
        lancz_res.gs_vec,
        eff_ham,
        target_site,
        noise,
        world
      );
    }else{
      MasterBroadcastOrder(contract_for_left_moving_expansion, world);
      MasterTwoSiteFiniteVMPSLeftMovingExpand(
        mps, 
        lancz_res.gs_vec,
        eff_ham,
        target_site,
        noise,
        world
      );
    }
  }

  // SVD and measure entanglement entropy
#ifdef GQMPS2_TIMING_MODE
  Timer svd_timer("two_site_fvmps_svd");
#endif

  TenT u, vt;
  using DTenT = GQTensor<GQTEN_Double, QNT>;
  DTenT s;
  GQTEN_Double actual_trunc_err;
  size_t D;
  MasterBroadcastOrder(svd, world);
  MPISVDMaster(
      lancz_res.gs_vec,
      svd_ldims, Div(mps[lsite_idx]),
      sweep_params.trunc_err, sweep_params.Dmin, sweep_params.Dmax,
      &u, &s, &vt, &actual_trunc_err, &D,
      world
  );
  delete lancz_res.gs_vec;
  auto ee = MeasureEE(s, D);

#ifdef GQMPS2_TIMING_MODE
  svd_timer.PrintElapsed();
#endif

  // Update MPS local tensor
#ifdef GQMPS2_TIMING_MODE
  Timer update_mps_ten_timer("two_site_fvmps_update_mps_ten");
#endif

  TenT the_other_mps_ten;
  switch (dir) {
    case 'r':
      mps[lsite_idx] = std::move(u);
      Contract(&s, &vt, {{1}, {0}}, &the_other_mps_ten);
      mps[rsite_idx] = std::move(the_other_mps_ten);
      break;
    case 'l':
      Contract(&u, &s, {{2}, {0}}, &the_other_mps_ten);
      mps[lsite_idx] = std::move(the_other_mps_ten);
      mps[rsite_idx] = std::move(vt);
      break;
    default:
      assert(false);
  }

#ifdef GQMPS2_TIMING_MODE
  update_mps_ten_timer.PrintElapsed();
#endif

  // Update environment tensors
#ifdef GQMPS2_TIMING_MODE
  Timer update_env_ten_timer("two_site_fvmps_update_env_ten");
#endif
  ///< TODO: parallel this part
  switch (dir) {
    case 'r':{
      MasterBroadcastOrder(growing_left_env, world);
      lenvs(lenv_len + 1) = MasterGrowLeftEnvironment(lenvs[lenv_len], mpo[target_site],mps[target_site], world);
    }break;
    case 'l':{
      MasterBroadcastOrder(growing_right_env, world);
      renvs(renv_len + 1) = MasterGrowRightEnvironment(*eff_ham[3], mpo[target_site],mps[target_site], world);
    }break;
    default:
      assert(false);
  }

#ifdef GQMPS2_TIMING_MODE
  update_env_ten_timer.PrintElapsed();
#endif

  auto update_elapsed_time = update_timer.Elapsed();
  std::cout << "Site " << std::setw(4) << target_site
            << " E0 = " << std::setw(20) << std::setprecision(kLanczEnergyOutputPrecision) << std::fixed << lancz_res.gs_eng
            << " TruncErr = " << std::setprecision(2) << std::scientific << actual_trunc_err << std::fixed
            << " D = " << std::setw(5) << D
            << " Iter = " << std::setw(3) << lancz_res.iters
            << " LanczT = " << std::setw(8) << lancz_elapsed_time
            << " TotT = " << std::setw(8) << update_elapsed_time
            << " S = " << std::setw(10) << std::setprecision(7) << ee;
  std::cout << std::scientific << std::endl;
  return lancz_res.gs_eng;
}




template <typename TenElemT, typename QNT>
inline void LoadRelatedTensOnTwoSiteAlgWhenRightMoving(
    FiniteMPS<TenElemT, QNT> &mps,
    TenVec<GQTensor<TenElemT, QNT>> &lenvs,
    TenVec<GQTensor<TenElemT, QNT>> &renvs,
    const size_t target_site,
    const size_t left_boundary,
    const TwoSiteMPINoisedVMPSSweepParams &sweep_params
) {
#ifdef GQMPS2_TIMING_MODE
  Timer preprocessing_timer("two_site_fvmps_preprocessing");
#endif
auto N = mps.size();
if (target_site != left_boundary) {
  mps.LoadTen(
      target_site + 2,
      GenMPSTenName(sweep_params.mps_path, target_site + 2)
  );
  auto renv_len = N - (target_site + 2);
  auto renv_file = GenEnvTenName("r", renv_len, sweep_params.temp_path);
  renvs.LoadTen(renv_len, renv_file);
  RemoveFile(renv_file);
} else {
  mps.LoadTen(
      target_site + 2,
      GenMPSTenName(sweep_params.mps_path, target_site + 2)
  );
  auto renv_len = (N - 1) - (target_site + 1);
  auto renv_file = GenEnvTenName("r", renv_len, sweep_params.temp_path);
  renvs.LoadTen(renv_len, renv_file);
  RemoveFile(renv_file);
  auto lenv_len = target_site;
  auto lenv_file = GenEnvTenName("l", lenv_len, sweep_params.temp_path);
  lenvs.LoadTen(lenv_len, lenv_file);
}  
#ifdef GQMPS2_TIMING_MODE
  preprocessing_timer.PrintElapsed();
#endif
}


template <typename TenElemT, typename QNT>
inline void LoadRelatedTensOnTwoSiteAlgWhenLeftMoving(
    FiniteMPS<TenElemT, QNT> &mps,
    TenVec<GQTensor<TenElemT, QNT>> &lenvs,
    TenVec<GQTensor<TenElemT, QNT>> &renvs,
    const size_t target_site,
    const size_t right_boundary,
    const TwoSiteMPINoisedVMPSSweepParams &sweep_params
){
#ifdef GQMPS2_TIMING_MODE
  Timer preprocessing_timer("two_site_fvmps_preprocessing");
#endif
const size_t N = mps.size();
if (target_site != right_boundary) {
  mps.LoadTen(
      target_site - 2,
      GenMPSTenName(sweep_params.mps_path, target_site - 2)
  );
  auto lenv_len = (target_site+1) - 2;
  auto lenv_file = GenEnvTenName("l", lenv_len, sweep_params.temp_path);
  lenvs.LoadTen(lenv_len, lenv_file);
  RemoveFile(lenv_file);
} else {
  mps.LoadTen(
      target_site - 2,
      GenMPSTenName(sweep_params.mps_path, target_site - 2)
  );
  auto renv_len = (N-1)-target_site;
  auto renv_file = GenEnvTenName("r", renv_len, sweep_params.temp_path);
  renvs.LoadTen(renv_len, renv_file);
  auto lenv_len = target_site - 1;
  auto lenv_file = GenEnvTenName("l", lenv_len, sweep_params.temp_path);
  RemoveFile(lenv_file);
}
#ifdef GQMPS2_TIMING_MODE
  preprocessing_timer.PrintElapsed();
#endif
}



template <typename TenElemT, typename QNT>
void MasterTwoSiteFiniteVMPSRightMovingExpand(
    FiniteMPS<TenElemT, QNT> &mps,
    GQTensor<TenElemT, QNT> *gs_vec,
    const std::vector< GQTensor<TenElemT, QNT> *> &eff_ham,
    const size_t target_site,
    const double noise,
    boost::mpi::communicator& world
) {
  // note: The expanded tensors are saved in *gs_vec, and mps[next_next_site]
  using TenT = GQTensor<TenElemT, QNT>;
  // we suppose mps contain mps[targe_site], mps[next_site],  mps[next_next_site]
#ifdef GQMPS2_TIMING_MODE
  Timer contract_timer("\t Contract, fuse index and scale for expansion");
#endif

#ifdef GQMPS2_MPI_TIMING_MODE
  Timer broadcast_state_timer("expansion_broadcast_state_send");
#endif
  SendBroadCastGQTensor(world, *gs_vec, kMasterRank);
  boost::mpi::broadcast(world, noise, kMasterRank);
#ifdef GQMPS2_MPI_TIMING_MODE
  broadcast_state_timer.PrintElapsed();
#endif
  const size_t split_idx = 2;
  const Index<QNT>& splited_index = eff_ham[0]->GetIndexes()[split_idx];
  const size_t task_size = splited_index.GetQNSctNum();//total task number
  const QNSectorVec<QNT>& split_qnscts = splited_index.GetQNScts();
  std::vector<TenT> res_list;
  res_list.reserve(task_size);
  const size_t slave_size = world.size() - 1 ; //total number of slaves

  IndexVec<QNT> ten_tmp_indexes(5);
  ten_tmp_indexes[0] = splited_index;
  ten_tmp_indexes[1] = gs_vec->GetIndexes()[3];
  ten_tmp_indexes[2] = eff_ham[1]->GetIndexes()[2];
  ten_tmp_indexes[3] = eff_ham[2]->GetIndexes()[2];
  ten_tmp_indexes[4] = eff_ham[2]->GetIndexes()[3];
  TenT ten_tmp_shell = TenT(ten_tmp_indexes);
  ten_tmp_shell->FuseIndex(1, 4);
  for(size_t j = 0; j<task_size;j++){
        res_list.push_back( ten_tmp_shell );
  }
  if(slave_size < task_size){
    std::vector<size_t> task_difficuty(task_size);
    for(size_t i = 0;i<task_size;i++){
      task_difficuty[i] = split_qnscts[i].GetDegeneracy();
    }
    std::vector<size_t> arraging_tasks(task_size-slave_size);
    std::iota(arraging_tasks.begin(), arraging_tasks.end(), slave_size );
    std::sort(arraging_tasks.begin(), 
                   arraging_tasks.end(), 
          [&task_difficuty](size_t task1, size_t task2){
              return task_difficuty[task1] > task_difficuty[task2];
              });
    #pragma omp parallel default(none)\
                        shared(task_size, slave_size, res_list, world, arraging_tasks)\
                        num_threads(slave_size)
    {
      size_t controlling_slave = omp_get_thread_num()+1;

      auto& bsdt = res_list[controlling_slave-1].GetBlkSparDataTen();
      const size_t task = controlling_slave-1;
      mpi::status recv_status = bsdt.MPIRecv(world, controlling_slave, task);

      #pragma omp for nowait schedule(dynamic)
      for(size_t i = 0; i < task_size - slave_size; i++){
        world.send(controlling_slave, 2*controlling_slave, arraging_tasks[i]);
        auto& bsdt = res_list[i+slave_size].GetBlkSparDataTen();
        bsdt.MPIRecv(world, controlling_slave, arraging_tasks[i]);
      }
      
      world.send(controlling_slave, 2*controlling_slave, 2*task_size);//finish signal
    }
  }else{//slave_size >= task_size
    #pragma omp parallel default(none)\
                        shared(task_size, res_list, world)\
                        num_threads(task_size)
    {
      size_t controlling_slave = omp_get_thread_num() + 1;
      size_t task = controlling_slave - 1;
      auto& bsdt = res_list[task].GetBlkSparDataTen();
      mpi::status recv_status = bsdt.MPIRecv(world, controlling_slave, task);
      world.send(controlling_slave, 2*controlling_slave, 2*task_size);//finish signal
    }
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer sum_state_timer(" parallel_summation_reduce");
#endif
  TenT* ten_tmp = new TenT();
  CollectiveLinearCombine(res_list, *ten_tmp);
#ifdef GQMPS2_MPI_TIMING_MODE
  sum_state_timer.PrintElapsed();
#endif


#ifdef GQMPS2_TIMING_MODE
  contract_timer.PrintElapsed();
  Timer expansion_timer("\t Magic expansion time");
#endif
  gs_vec->Transpose({3,0,1,2});
  TenT expanded_ten;
  ExpandMC(gs_vec, ten_tmp, {0},  &expanded_ten);
  expanded_ten.Transpose({1,2,3,0});
  (*gs_vec) = std::move(expanded_ten);
#ifdef GQMPS2_TIMING_MODE
  expansion_timer.PrintElapsed();
    
  expansion_timer.ClearAndRestart();
#endif
  size_t next_next_site = target_site + 2;
  auto expanded_index = InverseIndex(ten_tmp->GetIndexes()[0]);
  TenT expanded_zero_ten = TenT({
                               expanded_index,
                               mps[next_next_site].GetIndexes()[1],
                               mps[next_next_site].GetIndexes()[2]
                           });
  (*ten_tmp) = TenT();
  ExpandMC(mps(next_next_site), &expanded_zero_ten, {0}, ten_tmp);
  delete mps(next_next_site);
  mps(next_next_site) = ten_tmp;
#ifdef GQMPS2_TIMING_MODE
  expansion_timer.PrintElapsed();
#endif
}

template <typename TenElemT, typename QNT>
void SlaveTwoSiteFiniteVMPSRightMovingExpand(
  const std::vector< GQTensor<TenElemT, QNT> *> &eff_ham,
  boost::mpi::communicator& world
){
  using TenT = GQTensor<TenElemT, QNT>;
  TenT ground_state;
double noise;
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer broadcast_state_timer("expansion_broadcast_state_recv");
#endif
  RecvBroadCastGQTensor(world, ground_state, kMasterRank);
  boost::mpi::broadcast(world, noise, kMasterRank);
#ifdef GQMPS2_MPI_TIMING_MODE
  broadcast_state_timer.PrintElapsed();
#endif
  const size_t split_idx = 2; //index of mps tensor
  const Index<QNT>& splited_index = eff_ham[0]->GetIndexes()[split_idx];
  const size_t task_size = splited_index.GetQNSctNum();
  size_t task_count = 0;
  const size_t slave_identifier = world.rank();//number from 1
  if(slave_identifier > task_size){
    std::cout << "Slave has done task_count = " << task_count << std::endl;
    return;
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer salve_communication_timer(" slave "+std::to_string(slave_identifier) +"'s communication");
  salve_communication_timer.Suspend();
  Timer slave_work_timer(" slave "+ std::to_string(slave_identifier) +"'s work");
#endif
  size_t task = slave_identifier-1;
  TenT eff_ham0_times_state;
  TenT temp, res;
  //First contract
  TensorContraction1SectorExecutor<TenElemT, QNT> ctrct_executor(
    eff_ham[0],
    split_idx,
    task,
    &ground_state,
    {{0},{0}},
    &eff_ham0_times_state
  );
  
  ctrct_executor.Execute();

  Contract(&eff_ham0_times_state, eff_ham[1], {{0, 2}, {0, 1}}, &temp);
  eff_ham0_times_state.GetBlkSparDataTen().Clear();// save for memory
  Contract(&temp, eff_ham[2],  {{4, 1}, {0, 1}}, &res);
  temp.GetBlkSparDataTen().Clear();
  res *= noise;
  res.FuseIndex(1, 4);
  auto& bsdt = res.GetBlkSparDataTen();
  task_count++;
#ifdef GQMPS2_MPI_TIMING_MODE
  salve_communication_timer.Restart();
#endif
  bsdt.MPISend(world, kMasterRank, task);//tag = task
  world.recv(kMasterRank, 2*slave_identifier, task);//tag = 2*slave_identifier
#ifdef GQMPS2_MPI_TIMING_MODE
  salve_communication_timer.Suspend();
#endif
  while(task < task_size){
    TenT temp, res;
    ctrct_executor.SetSelectedQNSect(task);
    ctrct_executor.Execute();
    Contract(&eff_ham0_times_state, eff_ham[1], {{0, 2}, {0, 1}}, &temp);
    eff_ham0_times_state.GetBlkSparDataTen().Clear();// save for memory
    Contract(&temp, eff_ham[2],  {{4, 1}, {0, 1}}, &res);
    temp.GetBlkSparDataTen().Clear();
    res *= noise;
    res.FuseIndex(1, 4);
    auto& bsdt = res.GetBlkSparDataTen();
    task_count++;
  #ifdef GQMPS2_MPI_TIMING_MODE
    salve_communication_timer.Restart();
  #endif 
    bsdt.MPISend(world, kMasterRank, task);//tag = task
    world.recv(kMasterRank, 2*slave_identifier, task);
  #ifdef GQMPS2_MPI_TIMING_MODE
    salve_communication_timer.Suspend();
  #endif
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  slave_work_timer.PrintElapsed();
  salve_communication_timer.PrintElapsed();
#endif
  std::cout << "Slave " << slave_identifier<< " has done task_count = " << task_count << std::endl;
}
  
  
template <typename TenElemT, typename QNT>
void MasterTwoSiteFiniteVMPSLeftMovingExpand(
    FiniteMPS<TenElemT, QNT> &mps,
    GQTensor<TenElemT, QNT> *gs_vec,
    const std::vector< GQTensor<TenElemT, QNT> *> &eff_ham,
    const size_t target_site,
    const double noise,
    boost::mpi::communicator& world
) {
  using TenT = GQTensor<TenElemT, QNT>;

  size_t next_next_site = target_site - 2;
#ifdef GQMPS2_TIMING_MODE
  Timer contract_timer("\t Contract, fuse index and scale for expansion");
#endif

#ifdef GQMPS2_MPI_TIMING_MODE
  Timer broadcast_state_timer("expansion_broadcast_state_send");
#endif
  SendBroadCastGQTensor(world, *gs_vec, kMasterRank);
  boost::mpi::broadcast(world, noise, kMasterRank);
#ifdef GQMPS2_MPI_TIMING_MODE
  broadcast_state_timer.PrintElapsed();
#endif
  const size_t split_idx = 0;
  const Index<QNT>& splited_index = gs_vec->GetIndexes()[split_idx];
  const size_t task_size = splited_index.GetQNSctNum();//total task number
  const QNSectorVec<QNT>& split_qnscts = splited_index.GetQNScts();
  std::vector<TenT> res_list;
  res_list.reserve(task_size);
  const size_t slave_size = world.size() - 1 ; //total number of slaves

  IndexVec<QNT> ten_tmp_indexes(5);
  ten_tmp_indexes[0] = splited_index;
  ten_tmp_indexes[1] = eff_ham[3]->GetIndexes()[2];
  ten_tmp_indexes[2] = eff_ham[2]->GetIndexes()[2];
  ten_tmp_indexes[3] = eff_ham[1]->GetIndexes()[0];
  ten_tmp_indexes[4] = eff_ham[1]->GetIndexes()[2];
  TenT ten_tmp_shell = TenT(ten_tmp_indexes);
  ten_tmp_shell->Transpose({0, 3, 4, 2, 1});
  ten_tmp_shell->FuseIndex(0, 1);
  for(size_t j = 0; j<task_size;j++){
        res_list.push_back( ten_tmp_shell );
  }

  if(slave_size < task_size){
    std::vector<size_t> task_difficuty(task_size);
    for(size_t i = 0;i<task_size;i++){
      task_difficuty[i] = split_qnscts[i].GetDegeneracy();
    }
    std::vector<size_t> arraging_tasks(task_size-slave_size);
    std::iota(arraging_tasks.begin(), arraging_tasks.end(), slave_size );
    std::sort(arraging_tasks.begin(), 
                   arraging_tasks.end(), 
          [&task_difficuty](size_t task1, size_t task2){
              return task_difficuty[task1] > task_difficuty[task2];
              });
    #pragma omp parallel default(none)\
                        shared(task_size, slave_size, res_list, world, arraging_tasks)\
                        num_threads(slave_size)
    {
      size_t controlling_slave = omp_get_thread_num()+1;

      auto& bsdt = res_list[controlling_slave-1].GetBlkSparDataTen();
      const size_t task = controlling_slave-1;
      mpi::status recv_status = bsdt.MPIRecv(world, controlling_slave, task);

      #pragma omp for nowait schedule(dynamic)
      for(size_t i = 0; i < task_size - slave_size; i++){
        world.send(controlling_slave, 2*controlling_slave, arraging_tasks[i]);
        auto& bsdt = res_list[i+slave_size].GetBlkSparDataTen();
        bsdt.MPIRecv(world, controlling_slave, arraging_tasks[i]);
      }
      
      world.send(controlling_slave, 2*controlling_slave, 2*task_size);//finish signal
    }
  }else{//slave_size >= task_size
    #pragma omp parallel default(none)\
                        shared(task_size, res_list, world)\
                        num_threads(task_size)
    {
      size_t controlling_slave = omp_get_thread_num() + 1;
      size_t task = controlling_slave - 1;
      auto& bsdt = res_list[task].GetBlkSparDataTen();
      mpi::status recv_status = bsdt.MPIRecv(world, controlling_slave, task);
      world.send(controlling_slave, 2*controlling_slave, 2*task_size);//finish signal
    }
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer sum_state_timer(" parallel_summation_reduce");
#endif
  TenT* ten_tmp = new TenT();
  CollectiveLinearCombine(res_list, *ten_tmp);
#ifdef GQMPS2_MPI_TIMING_MODE
  sum_state_timer.PrintElapsed();
#endif

#ifdef GQMPS2_TIMING_MODE
  contract_timer.PrintElapsed();
  Timer expansion_timer("\t Magic expansion time");
#endif
  TenT expanded_ten;
  ExpandMC(gs_vec, ten_tmp, {0}, &expanded_ten);
  *gs_vec = std::move(expanded_ten);
#ifdef GQMPS2_TIMING_MODE
  expansion_timer.PrintElapsed();
  
  expansion_timer.ClearAndRestart();
#endif

  auto expanded_index = InverseIndex(ten_tmp->GetIndexes()[0]);
  TenT expanded_zero_ten = TenT({
                               mps[next_next_site].GetIndexes()[0],
                               mps[next_next_site].GetIndexes()[1],
                               expanded_index
                           });
  *ten_tmp = TenT();
  ExpandMC(mps(next_next_site), &expanded_zero_ten, {2}, ten_tmp);
  delete mps(next_next_site);
  mps(next_next_site) = ten_tmp;
#ifdef GQMPS2_TIMING_MODE
  expansion_timer.PrintElapsed();
#endif
}


template <typename TenElemT, typename QNT>
void SlaveTwoSiteFiniteVMPSLeftMovingExpand(
  const std::vector< GQTensor<TenElemT, QNT> *> &eff_ham,
  boost::mpi::communicator& world
){
  using TenT = GQTensor<TenElemT, QNT>;
  TenT ground_state;
double noise;
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer broadcast_state_timer("expansion_broadcast_state_recv");
#endif
  RecvBroadCastGQTensor(world, ground_state, kMasterRank);
  boost::mpi::broadcast(world, noise, kMasterRank);
#ifdef GQMPS2_MPI_TIMING_MODE
  broadcast_state_timer.PrintElapsed();
#endif
  const size_t split_idx = 0; //index of mps tensor
  const Index<QNT>& splited_index = ground_state.GetIndexes()[split_idx];
  const size_t task_size = splited_index.GetQNSctNum();
  size_t task_count = 0;
  const size_t slave_identifier = world.rank();//number from 1
  if(slave_identifier > task_size){
    std::cout << "Slave has done task_count = " << task_count << std::endl;
    return;
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  Timer salve_communication_timer(" slave "+std::to_string(slave_identifier) +"'s communication");
  salve_communication_timer.Suspend();
  Timer slave_work_timer(" slave "+ std::to_string(slave_identifier) +"'s work");
#endif
  size_t task = slave_identifier-1;
  TenT eff_ham0_times_state;
  TenT temp, res;
  //First contract
  TensorContraction1SectorExecutor<TenElemT, QNT> ctrct_executor(
    &ground_state,
    split_idx,
    task,
    eff_ham[3],
    {{3},{0}},
    &eff_ham0_times_state
  );
  
  ctrct_executor.Execute();
  Contract(&eff_ham0_times_state, eff_ham[2], {{2,3}, {1, 3}}, &temp);
  eff_ham0_times_state.GetBlkSparDataTen().Clear();// save for memory
  Contract(&temp, eff_ham[1],  {{1,3},{1,3}}, &res);
  temp.GetBlkSparDataTen().Clear();
  res *= noise;
  res.Transpose({0, 3, 4, 2, 1});
  res.FuseIndex(0,1);
  auto& bsdt = res.GetBlkSparDataTen();
  task_count++;
#ifdef GQMPS2_MPI_TIMING_MODE
  salve_communication_timer.Restart();
#endif
  bsdt.MPISend(world, kMasterRank, task);//tag = task
  world.recv(kMasterRank, 2*slave_identifier, task);//tag = 2*slave_identifier
#ifdef GQMPS2_MPI_TIMING_MODE
  salve_communication_timer.Suspend();
#endif
  while(task < task_size){
    TenT temp, res;
    ctrct_executor.SetSelectedQNSect(task);
    ctrct_executor.Execute();
    Contract(&eff_ham0_times_state, eff_ham[2], {{2,3}, {1, 3}}, &temp);
    eff_ham0_times_state.GetBlkSparDataTen().Clear();// save for memory
    Contract(&temp, eff_ham[1],  {{1,3},{1,3}}, &res);
    temp.GetBlkSparDataTen().Clear();
    res *= noise;
    res.Transpose({0, 3, 4, 2, 1});
    res.FuseIndex(0,1);
    auto& bsdt = res.GetBlkSparDataTen();
    task_count++;
  #ifdef GQMPS2_MPI_TIMING_MODE
    salve_communication_timer.Restart();
  #endif 
    bsdt.MPISend(world, kMasterRank, task);//tag = task
    world.recv(kMasterRank, 2*slave_identifier, task);
  #ifdef GQMPS2_MPI_TIMING_MODE
    salve_communication_timer.Suspend();
  #endif
  }
#ifdef GQMPS2_MPI_TIMING_MODE
  slave_work_timer.PrintElapsed();
  salve_communication_timer.PrintElapsed();
#endif
  std::cout << "Slave " << slave_identifier<< " has done task_count = " << task_count << std::endl;
}


}//gqmps2
#endif