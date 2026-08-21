#include <pcl/io/pcd_io.h>

#include <cstddef>

namespace dzc {
namespace {

static_assert(sizeof(pcl::PCDReader) > std::size_t{0U},
    "The dzc_data_pcl target requires PCL I/O headers.");

} // namespace
} // namespace dzc
