idf_component_register()

add_prebuilt_library(mweb "mweb.a")

target_link_libraries(${COMPONENT_LIB} INTERFACE mweb)
