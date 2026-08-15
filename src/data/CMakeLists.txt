dzc_add_interface_target(dzc_data_core)
dzc_add_interface_target(dzc_data_pcl)

# PCL discovery and linking are intentionally deferred until the concrete
# point-cloud I/O implementation. This is the only target allowed to link PCL.