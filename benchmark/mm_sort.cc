//
// Created by lukemartinlogan on 9/13/23.
//

#include <string>
#include <mpi.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "hermes_shm/util/logging.h"
#include "hermes_shm/util/config_parse.h"
#include <filesystem>
#include <algorithm>
#include <mega_mmap/vector_mmap_mpi.h>

namespace stdfs = std::filesystem;

template<typename VecT, typename T>
class BigSortMpi {
 public:
  VecT vec_;

 public:
  void Sort(const std::string &path, int rank, int nprocs,
            size_t window_size) {
    vec_.Init(path);
    size_t data_size = vec_.size_;
    size_t shift = window_size / 2;
    size_t proc_size = data_size / nprocs;
    size_t proc_off = rank * proc_size;
    while (true) {
      bool left_sorted = SmallSort(rank, proc_off,
                                   shift, proc_size,
                                   window_size, data_size);
      MPI_Barrier(MPI_COMM_WORLD);
      bool right_sorted = SmallSort(rank, proc_off + shift,
                                    shift, proc_size,
                                    window_size, data_size);
      MPI_Barrier(MPI_COMM_WORLD);
      bool is_sorted = left_sorted && right_sorted;
      MPI_Allreduce(MPI_IN_PLACE, &is_sorted, 1, MPI_C_BOOL, MPI_LAND, MPI_COMM_WORLD);
      if (is_sorted) {
        break;
      }
    }
  }

  bool SmallSort(int rank, size_t off, size_t shift,
                 size_t proc_size, size_t window_size,
                 size_t data_size) {
    VecT vec(vec_);
    bool is_sorted = true;
    size_t cur_size = 0;
    vec += off;
    while (cur_size < proc_size && off < data_size) {
      printf("Completion of sorting windows on process %d: %.2f%%\n", rank, 100.0 * cur_size / proc_size);
      if (is_sorted && !vec.IsSorted(0, window_size)) {
        is_sorted = false;
      }
      vec.Sort(0, window_size);
      off += shift;
      vec += shift;
      cur_size += shift;
      if (off + shift > data_size) {
        shift = data_size - off;
      }
      if (off + window_size > data_size) {
        shift = window_size - off;
        window_size = data_size - off;
      }
    }
    return is_sorted;
  }
};

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);
  if (argc < 4) {
    HILOG(kFatal, "USAGE: ./parallel_sort [algo] [path] [window_size]");
  }
  int rank, nprocs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  std::string algo = argv[1];
  std::string path = argv[2];
  size_t window_size = hshm::ConfigParse::ParseSize(argv[3]);
  HILOG(kInfo, "Running {} on {} with window size {}", algo, path, window_size);

  if (algo == "mmap") {
    BigSortMpi<mm::VectorMmapMpi<int>, int> sort;
    sort.Sort(path, rank, nprocs, window_size);
  } else if (algo == "mega") {
  } else {
    HILOG(kFatal, "Unknown algorithm: {}", algo);
  }
  MPI_Finalize();
}
