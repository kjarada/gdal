add_gdal_driver(
  TARGET ogr_12da
  SOURCES
    ogr12dadatasource.cpp
    ogr12dalayer.cpp
    ogr12dadriver.cpp
  PLUGIN_CAPABLE
  NO_DEPS
  DEF TWELVEDA_ENABLED
)

gdal_standard_includes(ogr_12da)
