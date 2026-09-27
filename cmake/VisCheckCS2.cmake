# Keep integration in the owning repository, not in an untracked submodule file.
set(VISCHECK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/external/VisCheckCS2")
add_library(VisCheckCS2 STATIC
    "${VISCHECK_DIR}/VisCheckCS2/Parser.cpp"
    "${VISCHECK_DIR}/VisCheckCS2/OptimizedGeometry.cpp")
target_include_directories(VisCheckCS2 PUBLIC "${VISCHECK_DIR}/VisCheckCS2")
add_executable(VPhysToOpt "${VISCHECK_DIR}/VPhysToOpt.cpp")
target_link_libraries(VPhysToOpt PRIVATE VisCheckCS2)
