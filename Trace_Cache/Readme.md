## Build

```
cd riscv-base
make
cd ..
cd uarchsim
make
```

## Newly added parameter

The new added parameter can be checked by invoke 721sim binary without any parameter.

Here is a summary:

```
--tc_en=<0/1>                 Enable trace cache
--tc_prop_m=<n>               Set M value (Maximum branch per trace). Note *the number prediction of multiple branch predictor* is also controlled by this parameter
--tc_prop_n=<n>               Set N value (Maximum instruction per trace)
--tc_capacity=<n>             Overall capacity, in number of trace, must be power of 2
--tc_assoc=<n>                Associativity, must be power of 2
--tc_heur=<0/1>               0 - No Heuristic applied; 1 - Terminate at backward jump
--tc_max_para_fill=<n>        Number of parallel fill allowed
--tc_non_blk_fill=<0/1>       Enable non-block fill
--tc_idx_with_pred_vec=<0/1>  Enable path hash (hash PC with prediction bits to generate T$ index)

```

## Generate the result

There is 154 run of simulation. To take advantage the multi-core system, we wrote a script that can run multiple instance of simulator in a single time. There are two version of the run script: One script (`721sim-partial/run/run_all_parallel.sh`)depends on GNU Parallel, which is more efficient and should be used if GNU Parallel is available on the system. However, there is no GNU Parallel installed on the ECE Grendel system, so another script (`721sim-partial/run/run_all_parallel_grendel.sh`) that doesn't depend on any external tool is wrote to accelerate test on the Grendel system.

To reproduce all the data we used in the paper, just go to the folder `721sim-partial/run/` after build the simulator, and execute:

```
$ ./update_bin.sh                # Update the 721sim binary
$ ./run_all_parallel_grendel.sh  # Or using run_all_parallel.sh if system support GNU Parallel
```

When finished, goto folder``721sim-partial/run/output` to check result

If you want to clear the result, run `$ ./clean.sh`

## The parameter used for all the data

Since the simulation is handle by the automatic parallel run script, the parameter is also handled by that. Check `721sim-partial/run/run_sim.sh` for the detail about all the parameter we used