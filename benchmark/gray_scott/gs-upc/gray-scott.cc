// The solver is based on Hiroshi Watanabe's 2D Gray-Scott reaction diffusion
// code available at:
// https://github.com/kaityo256/sevendayshpc/tree/master/day5

#include <mpi.h>
#include <random>
#include <vector>

#include "gray-scott.h"

GrayScott::GrayScott(const Settings &settings, MPI_Comm comm)
    : settings(settings), comm(comm), rand_dev(), mt_gen(rand_dev()),
      uniform_dist(-1.0, 1.0) {}

GrayScott::~GrayScott() {}

void GrayScott::init() {
  init_upc();
  init_field();
}

void GrayScott::init_field() {
  const int V = (size_x + 2) * (size_y + 2) * (size_z + 2);
  u = upcxx::new_array<double>(V);
  v = upcxx::new_array<double>(V);
  u2 = upcxx::new_array<double>(V);
  v2 = upcxx::new_array<double>(V);

  for (size_t i = 0; i < V; ++i) {
    u.local()[i] = 1.0;
    v.local()[i] = 0.0;
    u2.local()[i] = 0.0;
    v2.local()[i] = 0.0;
  }

  const int d = 6;
  for (int z = settings.L / 2 - d; z < settings.L / 2 + d; z++) {
    for (int y = settings.L / 2 - d; y < settings.L / 2 + d; y++) {
      for (int x = settings.L / 2 - d; x < settings.L / 2 + d; x++) {
        if (!is_inside(x, y, z))
          continue;
        int i = g2i(x, y, z);
        u[i] = 0.25;
        v[i] = 0.33;
      }
    }
  }
}

void GrayScott::init_upc() {
  int dims[3] = {};
  const int periods[3] = {1, 1, 1};
  int coords[3] = {};

  MPI_Comm_rank(comm, &rank);
  MPI_Comm_size(comm, &procs);

  MPI_Dims_create(procs, 3, dims);
  npx = dims[0];
  npy = dims[1];
  npz = dims[2];

  MPI_Cart_create(comm, 3, dims, periods, 0, &cart_comm);
  MPI_Cart_coords(cart_comm, rank, 3, coords);
  px = coords[0];
  py = coords[1];
  pz = coords[2];

  size_x = settings.L / npx;
  size_y = settings.L / npy;
  size_z = settings.L / npz;

  if (px < settings.L % npx) {
    size_x++;
  }
  if (py < settings.L % npy) {
    size_y++;
  }
  if (pz < settings.L % npz) {
    size_z++;
  }

  offset_x = (settings.L / npx * px) + std::min(settings.L % npx, px);
  offset_y = (settings.L / npy * py) + std::min(settings.L % npy, py);
  offset_z = (settings.L / npz * pz) + std::min(settings.L % npz, pz);

  MPI_Cart_shift(cart_comm, 0, 1, &west, &east);
  MPI_Cart_shift(cart_comm, 1, 1, &down, &up);
  MPI_Cart_shift(cart_comm, 2, 1, &south, &north);

  // XY faces: size_x * (size_y + 2)
  MPI_Type_vector(size_y + 2, size_x, size_x + 2, MPI_DOUBLE, &xy_face_type);
  MPI_Type_commit(&xy_face_type);

  // XZ faces: size_x * size_z
  MPI_Type_vector(size_z, size_x, (size_x + 2) * (size_y + 2), MPI_DOUBLE,
                  &xz_face_type);
  MPI_Type_commit(&xz_face_type);

  // YZ faces: (size_y + 2) * (size_z + 2)
  MPI_Type_vector((size_y + 2) * (size_z + 2), 1, size_x + 2, MPI_DOUBLE,
                  &yz_face_type);
  MPI_Type_commit(&yz_face_type);
}

void GrayScott::iterate() {
  exchange(u, v);
  calc(u, v, u2, v2);

  u.swap(u2);
  v.swap(v2);
}

double GrayScott::calcU(double tu, double tv) const {
  return -tu * tv * tv + settings.F * (1.0 - tu);
}

double GrayScott::calcV(double tu, double tv) const {
  return tu * tv * tv - (settings.F + settings.k) * tv;
}

double GrayScott::laplacian(int x, int y, int z,
                            const std::vector<double> &s) const {
  double ts = 0.0;
  ts += s[l2i(x - 1, y, z)];
  ts += s[l2i(x + 1, y, z)];
  ts += s[l2i(x, y - 1, z)];
  ts += s[l2i(x, y + 1, z)];
  ts += s[l2i(x, y, z - 1)];
  ts += s[l2i(x, y, z + 1)];
  ts += -6.0 * s[l2i(x, y, z)];

  return ts / 6.0;
}

void GrayScott::calc(const std::vector<double> &u, const std::vector<double> &v,
                     std::vector<double> &u2, std::vector<double> &v2) {
  for (int z = 1; z < size_z + 1; z++) {
    for (int y = 1; y < size_y + 1; y++) {
      for (int x = 1; x < size_x + 1; x++) {
        const int i = l2i(x, y, z);
        double du = 0.0;
        double dv = 0.0;
        du = settings.Du * laplacian(x, y, z, u);
        dv = settings.Dv * laplacian(x, y, z, v);
        du += calcU(u[i], v[i]);
        dv += calcV(u[i], v[i]);
        du += settings.noise * uniform_dist(mt_gen);
        u2[i] = u[i] + du * settings.dt;
        v2[i] = v[i] + dv * settings.dt;
      }
    }
  }
}

void GrayScott::exchange_xy(upcxx::global_ptr<double> &data) const {

  MPI_Status st;

  // Send XY face z=size_z to north and receive z=0 from south
  upcxx::rpc_ff(north, [&]() {
    upcxx::copy(data + l2i(1, 0, size_z),
                data + l2i(1, 0, 0),
                size_x);
  });

  // Send XY face z=1 to south and receive z=size_z+1 from north
  upcxx::rpc_ff(south, [&]() {
    upcxx::copy(data + l2i(1, 0, 1),
                data + l2i(1, 0, size_z + 1),
                size_x);
  });

  // Send XY face z=size_z to north and receive z=0 from south
  MPI_Sendrecv(&local_data[l2i(1, 0, size_z)], 1, xy_face_type, north, 1,
               &local_data[l2i(1, 0, 0)], 1, xy_face_type, south, 1,
               cart_comm, &st);
  // Send XY face z=1 to south and receive z=size_z+1 from north
  MPI_Sendrecv(&local_data[l2i(1, 0, 1)], 1, xy_face_type, south, 1,
               &local_data[l2i(1, 0, size_z + 1)], 1, xy_face_type, north, 1,
               cart_comm, &st);
}

void GrayScott::exchange_xz(upcxx::global_ptr<double> &data) const {
  MPI_Status st;

  // Send XZ face y=size_y to up and receive y=0 from down
  MPI_Sendrecv(&local_data[l2i(1, size_y, 1)], 1, xz_face_type, up, 2,
               &local_data[l2i(1, 0, 1)], 1, xz_face_type, down, 2, cart_comm,
               &st);
  // Send XZ face y=1 to down and receive y=size_y+1 from up
  MPI_Sendrecv(&local_data[l2i(1, 1, 1)], 1, xz_face_type, down, 2,
               &local_data[l2i(1, size_y + 1, 1)], 1, xz_face_type, up, 2,
               cart_comm, &st);
}

void GrayScott::exchange_yz(std::vector<double> &local_data) const {
  MPI_Status st;

  // Send YZ face x=size_x to east and receive x=0 from west
  MPI_Sendrecv(&local_data[l2i(size_x, 0, 0)], 1, yz_face_type, east, 3,
               &local_data[l2i(0, 0, 0)], 1, yz_face_type, west, 3, cart_comm,
               &st);
  // Send YZ face x=1 to west and receive x=size_x+1 from east
  MPI_Sendrecv(&local_data[l2i(1, 0, 0)], 1, yz_face_type, west, 3,
               &local_data[l2i(size_x + 1, 0, 0)], 1, yz_face_type, east, 3,
               cart_comm, &st);
}

void GrayScott::exchange(upcxx::global_ptr<double> &u,
                         upcxx::global_ptr<double> &v) const {
  exchange_xy(u);
  exchange_xz(u);
  exchange_yz(u);

  exchange_xy(v);
  exchange_xz(v);
  exchange_yz(v);
}
